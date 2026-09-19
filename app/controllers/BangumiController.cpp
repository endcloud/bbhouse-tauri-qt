#include "controllers/BangumiController.h"

#include <cmath>
#include <memory>

#include <QFutureWatcher>
#include <QPromise>
#include <QSet>
#include <QThreadPool>

#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"
#include "preferences/AppPreferences.h"

namespace {
QString cacheKey(qint64 id, bool regional) {
    return QString::number(id) + (regional ? QStringLiteral(":regional") : QStringLiteral(":direct"));
}
struct ListResult {
    QList<BangumiApi::BangumiSeason> items;
    qint64 total = 0;
    QString error;
    bool unauthorized = false;
};
struct DetailResult { QVariantMap detail; QString error; };
}

BangumiController::BangumiController(QObject *parent) : QObject(parent) {
    connect(AppPreferences::instance(), &AppPreferences::proxySettingsChanged, this, [this] {
        seasonCache_.clear();
        // 配置变更后，已经排队/在途的地区详情只能用新快照重新获取。
        if (seasonBusy_ && latestSeason_.regional) {
            latestSeason_.generation = ++seasonGeneration_;
            latestSeason_.proxy = AppPreferences::instance()->regionalProxy();
            queuedSeason_ = latestSeason_;
        }
    });
}

bool BangumiController::busy() const { return busy_; }
bool BangumiController::unauthorized() const { return unauthorized_; }
QVariantList BangumiController::pageItems() const { return buckets_[currentBucket_ - 1].items; }
int BangumiController::currentPage() const { return buckets_[currentBucket_ - 1].page; }
qint64 BangumiController::currentTotal() const { return buckets_[currentBucket_ - 1].total; }
int BangumiController::currentBucket() const { return currentBucket_; }

void BangumiController::setBucket(int bucket) {
    if (bucket < 1 || bucket > 3 || bucket == currentBucket_) return;
    currentBucket_ = bucket;
    unauthorized_ = false;
    emit unauthorizedChanged();
    emit currentBucketChanged();
    emit pageInfoChanged();
    emit pageItemsChanged();
    ensureCurrentBucketLoaded();
}

void BangumiController::loadPage(int page) {
    if (page < 1 || busy_) return;
    if (currentBucket_ == 3 && buckets_[2].page > 0) {
        projectRegionalPage(page);
        return;
    }
    busy_ = true;
    emit busyChanged();
    startFetch(currentBucket_, page);
}

void BangumiController::refresh() {
    if (busy_) return;
    busy_ = true;
    emit busyChanged();
    startFetch(currentBucket_, qMax(1, currentPage()));
}

void BangumiController::ensureCurrentBucketLoaded() {
    if (buckets_[currentBucket_ - 1].page >= 1 || busy_) return;
    busy_ = true;
    emit busyChanged();
    startFetch(currentBucket_, 1);
}

void BangumiController::setRegionalSearch(const QString &query) {
    if (regionalSearch_ == query) return;
    regionalSearch_ = query;
    if (buckets_[2].page > 0) projectRegionalPage(1);
}

void BangumiController::projectRegionalPage(int page) {
    const auto result = BangumiApi::regionalPage(regionalItems_, regionalSearch_, page);
    BucketState &state = buckets_[2];
    state.total = result.total;
    state.page = qBound(1, page, qMax(1, int((result.total + 29) / 30)));
    state.items.clear();
    for (const auto &season : result.items) state.items.append(toItemMap(season));
    if (currentBucket_ == 3) {
        emit pageItemsChanged();
        emit pageInfoChanged();
    }
}

void BangumiController::startFetch(int bucket, int page) {
    auto *watcher = new QFutureWatcher<ListResult>(this);
    connect(watcher, &QFutureWatcher<ListResult>::finished, this, [this, watcher, bucket, page] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (bucket == 3 && result.error.isEmpty()) regionalItems_ = result.items;
        QVariantList items;
        for (const auto &season : result.items) items.append(toItemMap(season));
        finishFetch(bucket, page, items, result.total, result.error, result.unauthorized);
    });
    auto promise = std::make_shared<QPromise<ListResult>>();
    promise->start();
    watcher->setFuture(promise->future());
    // 不捕获控制器；销毁/关闭时异步任务仍可安全结束。
    QThreadPool::globalInstance()->start([promise, bucket, page] {
        ListResult result;
        try {
            const QString cookie = BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            auto &api = *BilibiliApiClient::instance();
            if (bucket == 3) {
                result.items = BangumiApi::scanRegionalFollowList([&api, &cookie](int pn) {
                    return BangumiApi::fetchFollowList(api, cookie, 1, pn, BangumiApi::kMaxPageSize);
                });
                result.total = result.items.size();
            } else {
                const auto response = BangumiApi::fetchFollowList(api, cookie, bucket, page,
                                                                 BangumiApi::kMaxPageSize);
                // 常规桶保留服务端页码，总数仍是服务端桶总数。
                for (const auto &season : response.items)
                    if (!season.regional) result.items.append(season);
                result.total = response.total;
            }
        } catch (const ApiUnauthorizedError &e) {
            result.unauthorized = true;
            result.error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
        promise->addResult(result);
        promise->finish();
    });
}

void BangumiController::finishFetch(int bucket, int page, const QVariantList &items,
                                    qint64 total, const QString &error, bool unauthorized) {
    busy_ = false;
    emit busyChanged();
    if (!error.isEmpty()) {
        // 异桶晚到错误不污染当前 tab；旧卡片始终保留。
        if (bucket == currentBucket_) {
            unauthorized_ = unauthorized;
            emit unauthorizedChanged();
            emit loadFailed(error);
        }
    } else {
        if (bucket == currentBucket_) {
            unauthorized_ = false;
            emit unauthorizedChanged();
        }
        if (bucket == 3) {
            projectRegionalPage(page);
        } else {
            QVariantList deduped;
            QSet<qint64> seen;
            for (const QVariant &value : items) {
                const auto map = value.toMap();
                const qint64 id = map.value("seasonId").toLongLong();
                if (id <= 0 || seen.contains(id)) continue;
                seen.insert(id);
                deduped.append(map);
            }
            buckets_[bucket - 1] = {page, total, deduped};
            if (bucket == currentBucket_) {
                emit pageItemsChanged();
                emit pageInfoChanged();
            }
        }
    }
    if (bucket != currentBucket_) ensureCurrentBucketLoaded();
}

QVariantMap BangumiController::seasonDetail(qint64 seasonId, bool regional) {
    if (seasonId <= 0) return {};
    latestSeason_ = {seasonId, regional, ++seasonGeneration_,
                     regional ? AppPreferences::instance()->regionalProxy()
                              : QNetworkProxy(QNetworkProxy::NoProxy)};
    queuedSeason_.reset();
    const auto it = seasonCache_.constFind(cacheKey(seasonId, regional));
    if (it != seasonCache_.constEnd()) return it.value();
    queuedSeason_ = latestSeason_;
    if (!seasonBusy_) startSeasonFetch();
    return {};
}

void BangumiController::startSeasonFetch() {
    if (!queuedSeason_) return;
    const SeasonRequest request = *queuedSeason_;
    queuedSeason_.reset();
    seasonBusy_ = true;
    auto *watcher = new QFutureWatcher<DetailResult>(this);
    connect(watcher, &QFutureWatcher<DetailResult>::finished, this, [this, watcher, request] {
        const auto result = watcher->result();
        watcher->deleteLater();
        finishSeasonFetch(request, result.detail, result.error);
    });
    auto promise = std::make_shared<QPromise<DetailResult>>();
    promise->start();
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, request] {
        DetailResult output;
        try {
            const QString cookie = BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            std::unique_ptr<BilibiliApiClient> regionalClient;
            if (request.regional) regionalClient = std::make_unique<BilibiliApiClient>(request.proxy);
            auto &api = regionalClient ? *regionalClient : *BilibiliApiClient::instance();
            const auto result = BangumiApi::fetchSeason(api, cookie, request.id);
            QVariantList episodes;
            for (const auto &episode : result.episodes) {
                episodes.append(QVariantMap{{"epId", episode.epId}, {"cid", episode.cid},
                    {"title", episode.title}, {"longTitle", episode.longTitle},
                    {"coverUrl", episode.coverUrl}, {"duration", episode.durationSeconds},
                    {"badge", episode.badge}});
            }
            output.detail = {{"seasonId", result.seasonId}, {"title", result.title},
                {"coverUrl", result.coverUrl}, {"episodes", episodes},
                {"lastEpId", result.lastEpId}, {"lastEpIndex", result.lastEpIndex},
                {"lastTimeSeconds", result.lastTimeSeconds}, {"regional", request.regional}};
        } catch (const std::exception &e) { output.error = QString::fromUtf8(e.what()); }
        promise->addResult(output);
        promise->finish();
    });
}

void BangumiController::finishSeasonFetch(const SeasonRequest &request, const QVariantMap &detail,
                                           const QString &error) {
    seasonBusy_ = false;
    if (request.generation == seasonGeneration_) {
        if (error.isEmpty()) {
            seasonCache_.insert(cacheKey(request.id, request.regional), detail);
            emit seasonDetailReady(detail);
        } else { emit errorOccurred(error); }
    }
    if (queuedSeason_) startSeasonFetch();
}

QVariantMap BangumiController::toItemMap(const BangumiApi::BangumiSeason &season) {
    QVariantMap map;
    map.insert("seasonId", season.seasonId);
    map.insert("seasonType", season.seasonType);
    map.insert("seasonTypeName", season.seasonTypeName);
    map.insert("title", season.title);
    map.insert("regional", season.regional);
    map.insert("cover", season.cover);
    map.insert("squareCover", season.squareCover);
    map.insert("badge", season.badge);
    map.insert("totalCount", season.totalCount);
    map.insert("isFinish", season.isFinish);
    map.insert("newEpIndexShow", season.newEpIndexShow);
    // NaN = 条目无评分:显式布尔键,QML 侧不做 NaN 判等
    map.insert("ratingAvailable", !std::isnan(season.ratingScore));
    map.insert("ratingScore", season.ratingScore);
    map.insert("ratingCount", season.ratingCount);
    map.insert("progressText", season.progressText);
    map.insert("subtitle", season.subtitle);
    map.insert("evaluate", season.evaluate);
    map.insert("url", season.url);
    map.insert("rawJson", season.rawJson);
    return map;
}
