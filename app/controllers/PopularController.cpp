#include "controllers/PopularController.h"

#include <algorithm>
#include <memory>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QSet>
#include <QThreadPool>

#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"

namespace {
struct FetchResult { PopularPage page; QString error; };
struct PeriodsResult { QVariantList periods; QString error; };

QString publicCookie() {
    const QString path = AppPaths::cookiePath();
    // 公共榜单允许访客访问；已存在但损坏的 Cookie 文件仍需明确报告。
    return QFileInfo::exists(path) ? BilibiliApiClient::readCookieFromFile(path) : QString();
}
}

PopularController::PopularController(QObject *parent) : QObject(parent) {}

QString PopularController::key() const {
    if (activeTab_ == "weekly") return QStringLiteral("weekly:%1").arg(weeklyNumber_);
    if (activeTab_ == "ranking") return QStringLiteral("ranking:%1").arg(rankingRid_);
    return activeTab_;
}

const PopularController::State &PopularController::state() const {
    static const State empty;
    const auto found = cache_.constFind(key());
    return found == cache_.cend() ? empty : found.value();
}

void PopularController::invalidateFetch() {
    ++generation_;
    cache_[key()].busy = false;
}

void PopularController::ensureLoaded() {
    if (activeTab_ == "weekly" && weeklyNumber_ <= 0) {
        if (!periodsLoaded_ && !periodsBusy_ && periodsError_.isEmpty()) refreshPeriods();
        return;
    }
    if (!loaded() && !busy()) beginFetch(true);
}

void PopularController::refresh() {
    if (activeTab_ == "weekly" && weeklyNumber_ <= 0) {
        refreshPeriods();
        return;
    }
    beginFetch(true);
}

void PopularController::loadMore() {
    if (activeTab_ != "popular" || busy() || !hasMore()) return;
    beginFetch(!loaded());
}

void PopularController::selectTab(const QString &tab) {
    if (tab != "popular" && tab != "weekly" && tab != "precious" && tab != "ranking") return;
    if (activeTab_ != tab) {
        invalidateFetch();
        activeTab_ = tab;
        emit stateChanged();
    }
    ensureLoaded();
}

void PopularController::selectRanking(int rid) {
    if (rid < -1) return;
    if (rankingRid_ != rid || activeTab_ != "ranking") {
        invalidateFetch();
        rankingRid_ = rid;
        activeTab_ = QStringLiteral("ranking");
        emit stateChanged();
    }
    ensureLoaded();
}

void PopularController::selectWeek(int number) {
    const bool valid = std::any_of(weeklyPeriods_.cbegin(), weeklyPeriods_.cend(), [number](const QVariant &value) {
        return value.toMap().value("number").toInt() == number && number > 0;
    });
    if (!valid) return;
    if (weeklyNumber_ != number || activeTab_ != "weekly") {
        invalidateFetch();
        weeklyNumber_ = number;
        activeTab_ = QStringLiteral("weekly");
        emit stateChanged();
    }
    ensureLoaded();
}

void PopularController::beginFetch(bool replace) {
    if (activeTab_ == "weekly" && weeklyNumber_ <= 0) return;
    ++generation_;
    State &current = cache_[key()];
    current.error.clear();
    current.busy = true;
    replace_ = replace;
    pendingPage_ = replace ? 1 : current.nextPage;
    scannedPages_ = 0;
    emit stateChanged();
    startFetch(generation_, key(), pendingPage_);
}

void PopularController::startFetch(quint64 generation, const QString &selectionKey, int page) {
    const QString tab = activeTab_;
    const int rid = rankingRid_;
    const int number = weeklyNumber_;
    auto *watcher = new QFutureWatcher<FetchResult>(this);
    connect(watcher, &QFutureWatcher<FetchResult>::finished, this,
            [this, watcher, generation, selectionKey, page] {
        const auto result = watcher->result();
        watcher->deleteLater();
        finishFetch(generation, selectionKey, page, result.page, result.error);
    });
    auto promise = std::make_shared<QPromise<FetchResult>>();
    promise->start();
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, tab, rid, number, page] {
        FetchResult result;
        try {
            const QString cookie = publicCookie();
            auto &client = *BilibiliApiClient::instance();
            if (tab == "popular") result.page = PopularApi::fetchPopular(client, cookie, page, 20);
            else if (tab == "weekly") result.page = PopularApi::fetchWeekly(client, cookie, number);
            else if (tab == "precious") result.page = PopularApi::fetchPrecious(client, cookie);
            else if (rid == -1) {
                const QVariantList periods = PopularApi::fetchMusicPeriods(client, cookie);
                if (!periods.isEmpty()) {
                    const QVariantMap latest = periods.first().toMap();
                    result.page = PopularApi::fetchMusic(client, cookie, latest.value("id").toString());
                    const QString label = latest.value("label").toString();
                    if (!label.isEmpty()) result.page.description = label;
                }
            } else result.page = PopularApi::fetchRanking(client, cookie, rid);
        } catch (const std::exception &e) {
            result.error = QString::fromUtf8(e.what());
        }
        promise->addResult(result);
        promise->finish();
    });
}

void PopularController::finishFetch(quint64 generation, const QString &selectionKey, int page,
                                   const PopularPage &result, const QString &error) {
    if (generation != generation_ || selectionKey != key() || page != pendingPage_ || !busy()) return;
    State &current = cache_[selectionKey];
    QString failure = error;
    QVariantList additions;
    if (failure.isEmpty()) {
        QSet<QString> seen;
        if (!replace_) {
            for (const auto &value : current.items) seen.insert(value.toMap().value("videoKey").toString());
        }
        for (const auto &value : result.items) {
            const QString videoKey = value.toMap().value("videoKey").toString();
            if (videoKey.isEmpty() || seen.contains(videoKey)) continue;
            seen.insert(videoKey);
            additions.append(value);
        }
        ++scannedPages_;
        if (activeTab_ == "popular" && additions.isEmpty() && result.hasMore) {
            if (scannedPages_ < kMaxPagesPerOperation) {
                pendingPage_ = page + 1;
                startFetch(generation, selectionKey, pendingPage_);
                return;
            }
            failure = tr("热门分页超过安全上限，请刷新重试");
        }
    }
    current.busy = false;
    current.error = failure;
    if (failure.isEmpty()) {
        if (replace_) current.items = additions;
        else current.items.append(additions);
        current.nextPage = page + 1;
        current.hasMore = activeTab_ == "popular" && result.hasMore;
        current.loaded = true;
        current.description = result.description;
    }
    emit stateChanged();
}

void PopularController::refreshPeriods() {
    ++periodsGeneration_;
    periodsBusy_ = true;
    periodsError_.clear();
    emit stateChanged();
    startPeriodsFetch(periodsGeneration_);
}

void PopularController::startPeriodsFetch(quint64 generation) {
    auto *watcher = new QFutureWatcher<PeriodsResult>(this);
    connect(watcher, &QFutureWatcher<PeriodsResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        finishPeriodsFetch(generation, result.periods, result.error);
    });
    auto promise = std::make_shared<QPromise<PeriodsResult>>();
    promise->start();
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise] {
        PeriodsResult result;
        try {
            const QString cookie = publicCookie();
            result.periods = PopularApi::fetchWeeklyPeriods(*BilibiliApiClient::instance(), cookie);
        } catch (const std::exception &e) {
            result.error = QString::fromUtf8(e.what());
        }
        promise->addResult(result);
        promise->finish();
    });
}

void PopularController::finishPeriodsFetch(quint64 generation, const QVariantList &periods, const QString &error) {
    if (generation != periodsGeneration_ || !periodsBusy_) return;
    periodsBusy_ = false;
    periodsError_ = error;
    if (error.isEmpty()) {
        QVariantList normalized;
        QSet<int> seen;
        for (const auto &value : periods) {
            const int number = value.toMap().value("number").toInt();
            if (number <= 0 || seen.contains(number)) continue;
            seen.insert(number);
            normalized.append(value);
        }
        std::sort(normalized.begin(), normalized.end(), [](const QVariant &left, const QVariant &right) {
            return left.toMap().value("number").toInt() > right.toMap().value("number").toInt();
        });
        weeklyPeriods_ = normalized;
        periodsLoaded_ = true;
        const int selected = seen.contains(weeklyNumber_) ? weeklyNumber_
                             : normalized.isEmpty() ? 0 : normalized.first().toMap().value("number").toInt();
        if (selected != weeklyNumber_) {
            if (activeTab_ == "weekly") invalidateFetch();
            weeklyNumber_ = selected;
        }
    }
    emit stateChanged();
    if (error.isEmpty() && activeTab_ == "weekly") ensureLoaded();
}
