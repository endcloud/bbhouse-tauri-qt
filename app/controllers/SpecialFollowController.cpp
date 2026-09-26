#include "core/ControllerTask.h"
#include "controllers/SpecialFollowController.h"

#include <QDateTime>
#include <QMetaObject>
#include <QThreadPool>
#include <QUrl>
#include <QScopeGuard>

#include "core/ArcSearchApi.h"
#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/ArticleApi.h"
#include "core/BilibiliApiClient.h"
#include "core/CookieHelpers.h"
#include "core/RelationApi.h"
#include "core/SeasonsApi.h"

namespace {
constexpr int kArcPageSize = ArcSearchApi::kPageSize;        // 30(服务端)
constexpr int kSeasonsPageSize = SeasonsApi::kMaxPageSize;   // 20(服务端上限)
constexpr int kArticlePageSize = 30;                         // 客户端切片
constexpr int kManagePageSize = 20;                          // 关注列表浏览页大小
constexpr int kManageSearchMaxPage = 5;                      // 搜索分页服务端封顶
constexpr int kSeedPageSize = 20;                            // 种子导入分页
constexpr int kSeedMaxPages = 50;                            // 安全上限
constexpr int kSeasonArchivesMaxPages = 200;                 // 拉全循环安全上限

// 投稿条目 → 播放/卡片统一形状(videoKey/oid 与 HistoryCard 整链契约对齐)。
// 播放/弹幕数经 subtitle 呈现(spec SHOULD 副信息;viewCount 留 0 —— 该键在
// HistoryCard 中是"多次观看"角标语义,不可挪用播放量)。
QVariantMap arcVideoToItemMap(const ArcVideo &video) {
    QVariantMap map;
    map.insert("videoKey", QStringLiteral("av:%1").arg(video.aid));
    map.insert("title", video.title);
    map.insert("subtitle", QStringLiteral("播放 %1 · 弹幕 %2")
                                   .arg(QString::number(video.play),
                                        QString::number(video.review)));
    map.insert("coverUrl", video.coverUrl);
    map.insert("authorName", QString());
    map.insert("viewAt", video.created);
    map.insert("progress", 0);
    map.insert("duration", video.durationSeconds);
    map.insert("badge", QString());
    map.insert("linkUrl",
               QStringLiteral("https://www.bilibili.com/video/%1")
                       .arg(video.bvid.isEmpty()
                                    ? QStringLiteral("av%1").arg(video.aid)
                                    : video.bvid));
    map.insert("viewCount", 0);
    map.insert("business", QStringLiteral("archive"));
    map.insert("oid", video.aid);
    map.insert("kid", 0);
    return map;
}

QVariantMap partitionToItemMap(const ArcPartition &partition) {
    QVariantMap map;
    map.insert("tid", partition.tid);
    map.insert("name", partition.name);
    map.insert("count", partition.count);
    return map;
}

QVariantMap seasonToItemMap(const SeasonSummary &summary) {
    QVariantMap map;
    map.insert("id", summary.id);
    map.insert("isSeries", summary.isSeries);
    map.insert("title", summary.name);
    map.insert("coverUrl", summary.coverUrl);
    map.insert("total", summary.total);
    return map;
}

// 合集内视频 → 批量起播条目(archive 管线直通;cid 由播放器解析补齐)
QVariantMap seasonArchiveToEntryMap(const SeasonArchive &archive) {
    QVariantMap map;
    map.insert("videoKey", QStringLiteral("av:%1").arg(archive.aid));
    map.insert("title", archive.title);
    map.insert("subtitle", QString());
    map.insert("coverUrl", archive.coverUrl);
    map.insert("business", QStringLiteral("archive"));
    map.insert("oid", archive.aid);
    map.insert("kid", 0);
    map.insert("duration", archive.durationSeconds);
    map.insert("progress", 0);
    map.insert("linkUrl",
               QStringLiteral("https://www.bilibili.com/video/%1")
                       .arg(archive.bvid.isEmpty()
                                    ? QStringLiteral("av%1").arg(archive.aid)
                                    : archive.bvid));
    return map;
}

QVariantMap articleToItemMap(const ArticleListEntry &entry) {
    QVariantMap map;
    map.insert("listId", entry.id);
    map.insert("title", entry.name);
    map.insert("coverUrl", entry.coverUrl);
    map.insert("articlesCount", entry.articlesCount);
    map.insert("read", entry.read);
    map.insert("words", entry.words);
    map.insert("updateTime", entry.updateTime);
    map.insert("linkUrl", QStringLiteral("https://www.bilibili.com/read/readlist/rl%1")
                                  .arg(entry.id));
    return map;
}

QVariantMap followUserToItemMap(const FollowUser &user) {
    QVariantMap map;
    map.insert("mid", user.mid);
    map.insert("name", user.name);
    map.insert("faceUrl", user.faceUrl);
    map.insert("sign", user.sign);
    return map;
}

QVariantMap entryToItemMap(const SpecialFollowStore::Entry &entry) {
    QVariantMap map;
    map.insert("mid", entry.mid);
    map.insert("name", entry.name);
    map.insert("faceUrl", entry.faceUrl);
    map.insert("addedAt", entry.addedAt);
    return map;

}

SpecialFollowStore::Entry entryFromMap(const QVariantMap &map) {
    SpecialFollowStore::Entry entry;
    entry.mid = map.value("mid").toLongLong();
    entry.name = map.value("name").toString();
    entry.faceUrl = map.value("faceUrl").toString();
    entry.addedAt = map.value("addedAt").toLongLong();
    return entry;
}

qint64 selfMidFromCookie(const QString &cookie) {
    return CookieHelpers::tryGetCookieValue(cookie, QStringLiteral("DedeUserID"))
            .toLongLong();
}
}  // namespace

SpecialFollowController::SpecialFollowController(QObject *parent) : QObject(parent) {}

bool SpecialFollowController::upsReady() const { return upsReady_; }

QVariantList SpecialFollowController::ups() const { return ups_; }

qint64 SpecialFollowController::currentMid() const { return currentMid_; }

bool SpecialFollowController::unauthorized() const { return unauthorized_; }

QVariantList SpecialFollowController::arcItems() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? QVariantList() : it->arc.items;
}

QVariantList SpecialFollowController::arcPartitions() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? QVariantList() : it->arc.partitions;
}

int SpecialFollowController::arcPage() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? 0 : it->arc.page;
}

qint64 SpecialFollowController::arcTotal() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? 0 : it->arc.total;
}

int SpecialFollowController::arcOrder() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? 0 : it->arc.order;
}

qint64 SpecialFollowController::arcTid() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? 0 : it->arc.tid;
}

bool SpecialFollowController::arcBusy() const { return arcBusy_.load(); }

QVariantList SpecialFollowController::seasonItems() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? QVariantList() : it->seasons.items;
}

int SpecialFollowController::seasonPage() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? 0 : it->seasons.page;
}

qint64 SpecialFollowController::seasonTotal() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? 0 : it->seasons.total;
}

bool SpecialFollowController::seasonBusy() const { return seasonsBusy_.load(); }

bool SpecialFollowController::seasonVideosBusy() const {
    return seasonVideosBusy_.load();
}

QVariantList SpecialFollowController::articleItems() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? QVariantList() : it->articles.items;
}

int SpecialFollowController::articlePage() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd() ? 0 : it->articles.page;
}

qint64 SpecialFollowController::articleTotal() const {
    const auto it = sessions_.constFind(currentMid_);
    return it == sessions_.constEnd()
                   ? 0
                   : static_cast<qint64>(it->articles.items.size());
}

bool SpecialFollowController::articleBusy() const { return articlesBusy_.load(); }

bool SpecialFollowController::manageBusy() const { return manageBusy_.load(); }

QString SpecialFollowController::manageError() const { return manageError_; }

QVariantList SpecialFollowController::manageMembers() const {
    return manageBrowseItems_;
}

int SpecialFollowController::manageBrowsePage() const { return manageBrowsePage_; }

qint64 SpecialFollowController::manageBrowseTotal() const {
    return manageBrowseTotal_;
}

QVariantList SpecialFollowController::manageSearchResults() const {
    return manageSearchItems_;
}

int SpecialFollowController::manageSearchPage() const { return manageSearchPage_; }

qint64 SpecialFollowController::manageSearchTotal() const {
    return manageSearchTotal_;
}

bool SpecialFollowController::manageSearchMode() const { return manageSearchMode_; }

void SpecialFollowController::setCurrentTab(int value) {
    if (currentTab_ == value) return;
    currentTab_ = value;
    emit currentTabChanged();
}

void SpecialFollowController::setSearchText(const QString &value) {
    if (searchText_ == value) return;
    searchText_ = value;
    emit searchTextChanged();
}


// ---- UP 快照装载 + 一次性种子导入 ----

void SpecialFollowController::ensureReady() {
    if (upsReady_) return;
    if (seedBusy_.exchange(true)) return;
    startSeedOrLoad();
}

void SpecialFollowController::startSeedOrLoad() {
    runControllerTask(this, [this, store = store_] {
        QList<SpecialFollowStore::Entry> members;
        QString error;
        // 文件存在标志决定是否播种:load/save 均经 store 互斥
        const bool fileExists = [store, &members] {
            const SpecialFollowStore::Snapshot snapshot = store->load();
            members = snapshot.members;
            return snapshot.fileExists;
        }();
        if (!fileExists) {
            // 一次性种子导入:仅当文件不存在(此后服务端分组变更不影响本地列表)
            try {
                const QString cookie =
                        BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
                const qint64 selfMid = selfMidFromCookie(cookie);
                if (selfMid <= 0) {
                    throw ApiUnauthorizedError(Loc::get(
                            "cookie 中没有找到 DedeUserID,无法导入特别关注分组"));
                }
                QList<FollowUser> users;
                for (int pn = 1; pn <= kSeedMaxPages; ++pn) {
                    const QList<FollowUser> page = RelationApi::fetchSpecialTag(
                            *BilibiliApiClient::instance(), cookie, pn, kSeedPageSize);
                    users.append(page);
                    if (page.size() < kSeedPageSize) break;  // 末页(不足一页即止)
                }
                const qint64 now = QDateTime::currentSecsSinceEpoch();
                for (const FollowUser &user : users) {
                    if (user.mid <= 0) continue;
                    SpecialFollowStore::Entry entry;
                    entry.mid = user.mid;
                    entry.name = user.name;
                    entry.faceUrl = user.faceUrl;
                    entry.addedAt = now;
                    members.append(entry);
                }
                if (!members.isEmpty()) store->save(members);
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
        }
        return [this, members, error] {
            seedBusy_.store(false);
            if (!error.isEmpty()) emit seedFailed(error);
            finishUps(members);
        };
    });
}

void SpecialFollowController::finishUps(
        const QList<SpecialFollowStore::Entry> &members) {
    ups_.clear();
    ups_.reserve(members.size());
    for (const SpecialFollowStore::Entry &entry : members) {
        ups_.append(entryToItemMap(entry));
    }
    upsReady_ = true;
    emit upsReadyChanged();
    emit upsChanged();
    applyMidFallback();
    processSaveQueue();
}

// 选中 UP 缺席时回落首项(首装/被移除/保存后统一走这里)
void SpecialFollowController::applyMidFallback() {
    for (const QVariant &value : ups_) {
        if (value.toMap().value("mid").toLongLong() == currentMid_) {
            return;  // 当前选中仍在列表:保持
        }
    }
    const qint64 target =
            ups_.isEmpty() ? 0 : ups_.first().toMap().value("mid").toLongLong();
    if (target != currentMid_) selectUp(target);
}

void SpecialFollowController::selectUp(qint64 mid) {
    if (currentMid_ == mid) return;
    currentMid_ = mid;
    emit currentMidChanged();
    emitTabReset();
}

void SpecialFollowController::emitTabReset() {
    // 切头像:三档可见属性全部重绑定到新会话(缓存命中零网络直出)
    emit arcItemsChanged();
    emit arcInfoChanged();
    emit arcCriteriaChanged();
    emit seasonItemsChanged();
    emit seasonInfoChanged();
    emit articleInfoChanged();
}

SpecialFollowController::UpSession *SpecialFollowController::session(qint64 mid) {
    sessionUse_.removeAll(mid);
    sessionUse_.append(mid);
    while (sessionUse_.size() > 12) {
        const auto oldest = sessionUse_.takeFirst();
        if (oldest == currentMid_) { sessionUse_.append(oldest); continue; }
        sessions_.remove(oldest);
    }
    return &sessions_[mid];
}

void SpecialFollowController::ensureCurrentTabLoaded(int tab) {
    if (currentMid_ <= 0) return;
    UpSession *state = session(currentMid_);
    switch (tab) {
        case 0: if (state->arc.page <= 0) loadArcPage(1); break;
        case 1: if (state->seasons.page <= 0) loadSeasonsPage(1); break;
        case 2: if (state->articles.page <= 0) loadArticles(); break;
        default: break;
    }
}

// ---- 投稿档(服务端 30/页;分区/排序为服务端参数) ----

void SpecialFollowController::loadArcPage(int page) {
    if (currentMid_ <= 0 || page < 1) return;
    if (arcBusy_.exchange(true)) {
        pendingArcMid_ = currentMid_;
        pendingArcPage_ = page;
        return;
    }
    emit arcBusyChanged();
    startArcFetch(currentMid_, page);
}

void SpecialFollowController::setArcOrder(int order) {
    if (currentMid_ <= 0 || order < 0 || order > 2) return;
    UpSession *state = session(currentMid_);
    if (state->arc.order == order) return;
    state->arc.order = order;
    state->arc.page = 0;
    emit arcCriteriaChanged();
    loadArcPage(1);  // 切排序:重置第一页并重新拉取
}

void SpecialFollowController::setArcPartition(qint64 tid) {
    if (currentMid_ <= 0) return;
    UpSession *state = session(currentMid_);
    if (state->arc.tid == tid) return;
    state->arc.tid = tid;
    state->arc.page = 0;
    emit arcCriteriaChanged();
    loadArcPage(1);  // 切分区:重置第一页并重新拉取(服务端 tid 过滤)
}

void SpecialFollowController::refreshArc() {
    // 重拉当前页:未装载时等价首拉(页码 0 → 兜底第 1 页;保旧卡整体替换)
    loadArcPage(qMax(1, arcPage()));
}

void SpecialFollowController::startArcFetch(qint64 mid, int page) {
    // 调用方已置 busy_;凭据源每次现读;排序/分区取发起时刻快照
    const UpSession *state = session(mid);
    const int order = state->arc.order;
    const qint64 tid = state->arc.tid;
    const auto generation = pageGeneration_;
    runControllerTask(this, [this, generation, mid, page, order, tid] {
        QVariantList items;
        QVariantList partitions;
        qint64 total = 0;
        QString error;
        bool unauthorized = false;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            const ArcSearchPage result = ArcSearchApi::fetch(
                    *BilibiliApiClient::instance(), cookie, mid,
                    static_cast<ArcSortOrder>(order), tid, page);
            items.reserve(result.videos.size());
            for (const ArcVideo &video : result.videos) {
                QVariantMap item = arcVideoToItemMap(video);
                item.insert("authorMid", QString::number(mid));
                items.append(item);
            }
            for (const ArcPartition &partition : result.partitions) {
                partitions.append(partitionToItemMap(partition));
            }
            total = result.total;
        } catch (const ApiUnauthorizedError &e) {
            unauthorized = true;
            error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        return [this, generation, mid, page, order, tid, items, partitions, total, error,
                 unauthorized] {
            if (generation != pageGeneration_) return;
            finishArcFetch(mid, page, order, tid, items, partitions, total,
                           error, unauthorized);
        };
    });
}

void SpecialFollowController::finishArcFetch(qint64 mid, int page, int order,
                                             qint64 tid, const QVariantList &items,
                                             const QVariantList &partitions,
                                             qint64 total, const QString &error,
                                             bool unauthorized) {
    arcBusy_.store(false);
    emit arcBusyChanged();
    const auto pending = qScopeGuard([this] { drainPendingArc(); });
    UpSession *state = session(mid);
    if (state->arc.order != order || state->arc.tid != tid) {
        if (mid == currentMid_ && pendingArcMid_ == 0) {
            pendingArcMid_ = mid;
            pendingArcPage_ = qMax(1, state->arc.page);
        }
        return;
    }
    if (!error.isEmpty()) {
        if (mid == currentMid_) {
            unauthorized_ = unauthorized;
            emit unauthorizedChanged();
            emit loadFailed(error);
        }
        return;
    }
    if (mid == currentMid_) {
        unauthorized_ = false;
        emit unauthorizedChanged();
    }
    state->arc.page = page;
    state->arc.total = total;
    state->arc.items = items;
    // 分区索引:响应自带 tlist;空响应(末页越界等)保留既有档位
    if (!partitions.isEmpty()) state->arc.partitions = partitions;
    if (mid == currentMid_) {
        emit arcItemsChanged();
        emit arcInfoChanged();
    }
}

// ---- 合集档(服务端 20/页;合集+系列合并) ----

void SpecialFollowController::loadSeasonsPage(int page) {
    if (currentMid_ <= 0 || page < 1) return;
    if (seasonsBusy_.exchange(true)) {
        pendingSeasonsMid_ = currentMid_;
        pendingSeasonsPage_ = page;
        return;
    }
    emit seasonBusyChanged();
    startSeasonsFetch(currentMid_, page);
}

void SpecialFollowController::refreshSeasons() {
    loadSeasonsPage(qMax(1, seasonPage()));
}

void SpecialFollowController::startSeasonsFetch(qint64 mid, int page) {
    const auto generation = pageGeneration_;
    runControllerTask(this, [this, generation, mid, page] {
        QVariantList items;
        qint64 total = 0;
        QString error;
        bool unauthorized = false;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            // page_size 20 为端点硬上限(21+ 以 data=null 的 -400 失败)
            const SeasonsSeriesPage result = SeasonsApi::fetchSeasonsSeriesList(
                    *BilibiliApiClient::instance(), cookie, mid, page, kSeasonsPageSize);
            items.reserve(result.items.size());
            for (const SeasonSummary &summary : result.items) {
                items.append(seasonToItemMap(summary));
            }
            total = result.pageTotal;
        } catch (const ApiUnauthorizedError &e) {
            unauthorized = true;
            error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        return [this, generation, mid, page, items, total, error, unauthorized] {
            if (generation != pageGeneration_) return;
            finishSeasonsFetch(mid, page, items, total, error, unauthorized);
        };
    });
}

void SpecialFollowController::finishSeasonsFetch(qint64 mid, int page,
                                                 const QVariantList &items,
                                                 qint64 total, const QString &error,
                                                 bool unauthorized) {
    seasonsBusy_.store(false);
    emit seasonBusyChanged();
    const auto pending = qScopeGuard([this] { drainPendingSeasons(); });
    if (!error.isEmpty()) {
        if (mid == currentMid_) {
            unauthorized_ = unauthorized;
            emit unauthorizedChanged();
            emit loadFailed(error);
        }
        return;
    }
    if (mid == currentMid_) {
        unauthorized_ = false;
        emit unauthorizedChanged();
    }
    UpSession *state = session(mid);
    state->seasons.page = page;
    state->seasons.total = total;
    state->seasons.items = items;
    if (mid == currentMid_) {
        emit seasonItemsChanged();
        emit seasonInfoChanged();
    }
}

void SpecialFollowController::loadSeasonVideos(int requestId, bool isSeries,
                                               qint64 id) {
    if (currentMid_ <= 0 || id <= 0 || requestId <= 0) return;
    if (seasonVideosBusy_.exchange(true)) return;
    emit seasonVideosBusyChanged();
    const qint64 mid = currentMid_;
    const auto generation = pageGeneration_;
    runControllerTask(this, [this, generation, requestId, isSeries, id, mid] {
        QVariantList entries;
        QString error;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            BilibiliApiClient &api = *BilibiliApiClient::instance();
            QList<SeasonArchive> archives;
            qint64 total = 0;
            // 循环翻页拉全:累计到达 total 或空页即止(安全上限兜底)
            for (int page = 1; page <= kSeasonArchivesMaxPages; ++page) {
                const SeasonArchivesPage result =
                        isSeries ? SeasonsApi::fetchSeriesArchives(api, cookie, mid, id,
                                                                   page)
                                 : SeasonsApi::fetchSeasonArchives(api, cookie, mid, id,
                                                                   page);
                total = result.total;
                if (result.archives.isEmpty()) break;
                archives.append(result.archives);
                if (total > 0 && static_cast<qint64>(archives.size()) >= total) break;
            }
            entries.reserve(archives.size());
            for (const SeasonArchive &archive : archives) {
                QVariantMap entry = seasonArchiveToEntryMap(archive);
                entry.insert("authorMid", QString::number(mid));
                entries.append(entry);
            }
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        return [this, generation, requestId, entries, error] {
            if (generation != pageGeneration_) return;
            seasonVideosBusy_.store(false);
            emit seasonVideosBusyChanged();
            if (error.isEmpty()) {
                emit seasonVideosReady(requestId, entries);
            } else {
                // 拉取失败:既有播放列表与当前播放不受影响
                emit seasonVideosFailed(error);
            }
        };
    });
}

// ---- 专栏档(一次全量,客户端 30/页) ----

void SpecialFollowController::loadArticles() {
    if (currentMid_ <= 0) return;
    if (articlesBusy_.exchange(true)) {
        pendingArticlesMid_ = currentMid_;
        return;
    }
    emit articleBusyChanged();
    startArticlesFetch(currentMid_);
}

void SpecialFollowController::refreshArticles() {
    loadArticles();
}

void SpecialFollowController::startArticlesFetch(qint64 mid) {
    const auto generation = pageGeneration_;
    runControllerTask(this, [this, generation, mid] {
        QVariantList items;
        QString error;
        bool unauthorized = false;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            const ArticleListsPage result = ArticleApi::fetchArticleLists(
                    *BilibiliApiClient::instance(), cookie, mid);
            items.reserve(result.lists.size());
            for (const ArticleListEntry &entry : result.lists) {
                items.append(articleToItemMap(entry));
            }
        } catch (const ApiUnauthorizedError &e) {
            unauthorized = true;
            error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        return [this, generation, mid, items, error, unauthorized] {
            if (generation != pageGeneration_) return;
            finishArticlesFetch(mid, items, error, unauthorized);
        };
    });
}

void SpecialFollowController::finishArticlesFetch(qint64 mid,
                                                  const QVariantList &items,
                                                  const QString &error,
                                                  bool unauthorized) {
    articlesBusy_.store(false);
    emit articleBusyChanged();
    const auto pending = qScopeGuard([this] { drainPendingArticles(); });
    if (!error.isEmpty()) {
        if (mid == currentMid_) {
            unauthorized_ = unauthorized;
            emit unauthorizedChanged();
            emit loadFailed(error);
        }
        return;
    }
    if (mid == currentMid_) {
        unauthorized_ = false;
        emit unauthorizedChanged();
    }
    UpSession *state = session(mid);
    state->articles.items = items;
    // 刷新保页码,但页码越界(列表缩短)时钳回末页(至少 1)
    const int maxPage = qMax(1, static_cast<int>((items.size() + kArticlePageSize - 1) /
                                                 kArticlePageSize));
    state->articles.page =
            state->articles.page >= 1 ? qMin(state->articles.page, maxPage) : 1;
    if (mid == currentMid_) emit articleInfoChanged();
}

void SpecialFollowController::setArticlePage(int page) {
    if (currentMid_ <= 0 || page < 1) return;
    UpSession *state = session(currentMid_);
    if (state->articles.page == page) return;
    state->articles.page = page;
    emit articleInfoChanged();  // 本地切片:零网络
}

// ---- 管理弹层 ----

void SpecialFollowController::openManage() {
    manageBrowseItems_.clear();
    manageBrowsePage_ = 0;
    manageBrowseTotal_ = 0;
    manageSearchItems_.clear();
    manageSearchPage_ = 0;
    manageSearchTotal_ = 0;
    manageKeyword_.clear();
    manageSearchMode_ = false;
    manageError_.clear();
    // 勾选池:本地成员全量预勾选(合并语义的基础 —— 未显式取消即保留)
    checkedInfo_.clear();
    for (const QVariant &value : ups_) {
        const SpecialFollowStore::Entry entry = entryFromMap(value.toMap());
        if (entry.mid > 0) checkedInfo_.insert(entry.mid, entry);
    }
    presentedMids_.clear();
    emit manageBrowseChanged();
    emit manageResultsChanged();
    emit manageErrorChanged();
    manageLoadMore();  // 拉浏览第 1 页(busy 闸门在 loadMore 内)
}

void SpecialFollowController::manageLoadMore() {
    if (manageBusy_.exchange(true)) return;
    // 搜索分页服务端封顶 5 页:越界直接回落(按钮态由 QML 依 manageSearchPage 禁用)
    if (manageSearchMode_ && manageSearchPage_ >= kManageSearchMaxPage) {
        manageBusy_.store(false);
        return;
    }
    emit manageBusyChanged();
    const int next = manageSearchMode_ ? manageSearchPage_ + 1
                                       : manageBrowsePage_ + 1;
    startManageFetch(manageSearchMode_, next);
}

void SpecialFollowController::manageSearch(const QString &keyword) {
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        // 清空关键词:回到已装载的分页浏览列表(零网络)
        manageKeyword_.clear();
        manageSearchMode_ = false;
        manageSearchItems_.clear();
        manageSearchPage_ = 0;
        manageSearchTotal_ = 0;
        emit manageResultsChanged();
        return;
    }
    if (manageSearchMode_ && trimmed == manageKeyword_) return;  // 未变化
    if (manageBusy_.exchange(true)) return;  // 在途拉取:丢弃本次(可再次触发)
    manageKeyword_ = trimmed;
    manageSearchMode_ = true;
    manageSearchItems_.clear();
    manageSearchPage_ = 0;
    manageSearchTotal_ = 0;
    emit manageBusyChanged();
    emit manageResultsChanged();
    startManageFetch(true, 1);
}

void SpecialFollowController::startManageFetch(bool searchMode, int page) {
    // 关键词值捕获(池线程不得读主线程可变的 manageKeyword_)
    const QString keyword = manageKeyword_;
    const auto generation = pageGeneration_;
    runControllerTask(this, [this, generation, searchMode, page, keyword] {
        QVariantList users;
        qint64 total = 0;
        QString error;
        bool unauthorized = false;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            const qint64 selfMid = selfMidFromCookie(cookie);
            if (selfMid <= 0) {
                throw ApiUnauthorizedError(
                        Loc::get("登录失效: 账号未登录或 SESSDATA 已失效"));
            }
            const FollowingsPage result =
                    searchMode
                            ? RelationApi::fetchFollowingsSearch(
                                      *BilibiliApiClient::instance(), cookie, selfMid,
                                      keyword, page, kManagePageSize)
                            : RelationApi::fetchFollowings(
                                      *BilibiliApiClient::instance(), cookie, selfMid,
                                      page, kManagePageSize);
            for (const FollowUser &user : result.users) {
                users.append(followUserToItemMap(user));
            }
            total = result.total;
        } catch (const ApiUnauthorizedError &e) {
            unauthorized = true;
            error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        return [this, generation, searchMode, page, users, total, error, unauthorized] {
            if (generation != pageGeneration_) return;
            finishManageFetch(searchMode, page, users, total, error,
                              unauthorized);
        };
    });
}

void SpecialFollowController::finishManageFetch(bool searchMode, int page,
                                                const QVariantList &users,
                                                qint64 total, const QString &error,
                                                bool unauthorized) {
    manageBusy_.store(false);
    emit manageBusyChanged();
    if (!error.isEmpty()) {
        // 失败保留已呈现内容可继续勾选;"加载更多"兼做重试
        manageError_ = error;
        emit manageErrorChanged();
        if (unauthorized) {
            unauthorized_ = true;
            emit unauthorizedChanged();
        }
        return;
    }
    manageError_.clear();
    emit manageErrorChanged();
    if (searchMode) {
        if (page == 1) {
            manageSearchItems_ = users;  // 新关键词:首页整体替换
        } else {
            // 翻页追加(按 mid 去重,防服务端翻页重叠)
            QSet<qint64> seen;
            for (const QVariant &value : manageSearchItems_) {
                seen.insert(value.toMap().value("mid").toLongLong());
            }
            for (const QVariant &value : users) {
                const qint64 mid = value.toMap().value("mid").toLongLong();
                if (!seen.contains(mid)) {
                    seen.insert(mid);
                    manageSearchItems_.append(value);
                }
            }
        }
        manageSearchPage_ = page;
        manageSearchTotal_ = total;
    } else {
        QSet<qint64> seen;
        for (const QVariant &value : manageBrowseItems_) {
            seen.insert(value.toMap().value("mid").toLongLong());
        }
        for (const QVariant &value : users) {
            const QVariantMap map = value.toMap();
            const qint64 mid = map.value("mid").toLongLong();
            presentedMids_.insert(mid);  // 本次会话呈现过:保存时可被取消勾选移除
            if (!seen.contains(mid)) {
                seen.insert(mid);
                manageBrowseItems_.append(map);
            }
        }
        manageBrowsePage_ = page;
        manageBrowseTotal_ = total;
    }
    emit manageBrowseChanged();
    emit manageResultsChanged();
}

bool SpecialFollowController::isMemberChecked(qint64 mid) const {
    return checkedInfo_.contains(mid);
}

void SpecialFollowController::setMemberChecked(qint64 mid, const QString &name,
                                               const QString &faceUrl, bool checked) {
    if (mid <= 0) return;
    if (checked) {
        if (checkedInfo_.contains(mid)) return;
        SpecialFollowStore::Entry entry;
        entry.mid = mid;
        // 本地已有成员沿用本地资料;池外新成员即时捕获弹层呈现的昵称/头像
        for (const QVariant &value : ups_) {
            const QVariantMap map = value.toMap();
            if (map.value("mid").toLongLong() == mid) {
                entry = entryFromMap(map);
                break;
            }
        }
        if (entry.name.isEmpty() && entry.faceUrl.isEmpty()) {
            entry.name = name;
            entry.faceUrl = faceUrl;
            entry.addedAt = QDateTime::currentSecsSinceEpoch();
        }
        checkedInfo_.insert(mid, entry);
    } else {
        checkedInfo_.remove(mid);  // 仅对呈现过且被显式取消的成员生效
    }
}

void SpecialFollowController::saveManage() {
    SaveOperation operation;
    operation.management = true;
    operation.checked = checkedInfo_;
    operation.presented = presentedMids_;
    saveQueue_.enqueue(operation);
    emit localFollowBusyChanged();
    ensureLocalReady();
    processSaveQueue();
}

bool SpecialFollowController::containsUp(qint64 mid) const {
    for (const auto &value : ups_)
        if (value.toMap().value("mid").toLongLong() == mid) return true;
    return false;
}

void SpecialFollowController::ensureLocalReady() {
    if (upsReady_ || seedBusy_.exchange(true)) return;
    runControllerTask(this, [this, store = store_] {
        const auto members = store->load().members;
        return [this, members] {
            seedBusy_ = false;
            finishUps(members);
        };
    });
}

void SpecialFollowController::setUpFollowed(qint64 mid, const QString &name,
                                           const QString &face, bool followed) {
    if (mid <= 0) return;
    SaveOperation operation;
    operation.mid = mid;
    operation.name = name;
    operation.face = face;
    operation.followed = followed;
    saveQueue_.enqueue(operation);
    emit localFollowBusyChanged();
    ensureLocalReady();
    processSaveQueue();
}

void SpecialFollowController::processSaveQueue() {
    if (!upsReady_ || seedBusy_ || saveBusy_ || saveQueue_.isEmpty()) return;
    const SaveOperation operation = saveQueue_.dequeue();
    QList<SpecialFollowStore::Entry> merged;
    QSet<qint64> kept;
    for (const auto &value : ups_) {
        const auto entry = entryFromMap(value.toMap());
        const bool remove = operation.management
            ? operation.presented.contains(entry.mid) && !operation.checked.contains(entry.mid)
            : entry.mid == operation.mid && !operation.followed;
        if (remove) continue;
        merged.append(entry);
        kept.insert(entry.mid);
    }
    if (operation.management) {
        for (auto it = operation.checked.constBegin(); it != operation.checked.constEnd(); ++it)
            if (!kept.contains(it.key())) merged.append(it.value());
    } else if (operation.followed && !kept.contains(operation.mid)) {
        merged.append({operation.mid, operation.name, operation.face, QDateTime::currentSecsSinceEpoch()});
    }
    saveBusy_ = true;
    emit localFollowBusyChanged();
    runControllerTask(this, [this, store = store_, merged, management = operation.management] {
        const bool ok = store->save(merged);
        return [this, merged, management, ok] {
            // Keep the gate closed while finishUps emits signals; queued requests
            // must derive from this persisted snapshot, not a stale previous one.
            if (ok) {
                finishUps(merged);
                if (management) emit manageSaved();
            } else {
                manageError_ = Loc::get("保存特别关注列表失败");
                emit manageErrorChanged();
                emit loadFailed(manageError_);
            }
            saveBusy_ = false;
            emit localFollowBusyChanged();
            processSaveQueue();
        };
    });
}

void SpecialFollowController::drainPendingArc() {
    if (arcBusy_) return;
    const qint64 mid = pendingArcMid_;
    const int page = pendingArcPage_;
    pendingArcMid_ = 0;
    if (mid > 0 && mid == currentMid_) loadArcPage(page);
}

void SpecialFollowController::drainPendingSeasons() {
    if (seasonsBusy_) return;
    const qint64 mid = pendingSeasonsMid_;
    const int page = pendingSeasonsPage_;
    pendingSeasonsMid_ = 0;
    if (mid > 0 && mid == currentMid_) loadSeasonsPage(page);
}

void SpecialFollowController::drainPendingArticles() {
    if (articlesBusy_) return;
    const qint64 mid = pendingArticlesMid_;
    pendingArticlesMid_ = 0;
    if (mid > 0 && mid == currentMid_) loadArticles();
}

void SpecialFollowController::releasePageCache() {
    ++pageGeneration_;
    sessions_ = {};
    sessionUse_ = {};
    pendingArcMid_ = 0;
    pendingSeasonsMid_ = 0;
    pendingArticlesMid_ = 0;
    arcBusy_ = false;
    seasonsBusy_ = false;
    articlesBusy_ = false;
    seasonVideosBusy_ = false;
    manageBusy_ = false;
    unauthorized_ = false;
    manageBrowseItems_ = {};
    manageSearchItems_ = {};
    manageBrowsePage_ = 0;
    manageBrowseTotal_ = 0;
    manageSearchPage_ = 0;
    manageSearchTotal_ = 0;
    manageKeyword_.clear();
    manageSearchMode_ = false;
    manageError_.clear();
    presentedMids_ = {};
    checkedInfo_ = {};
    searchText_.clear();
    emit searchTextChanged();
    // Local follow membership and queued persistence are shared business state.
    emitTabReset();
    emit unauthorizedChanged();
    emit arcBusyChanged();
    emit seasonBusyChanged();
    emit articleBusyChanged();
    emit seasonVideosBusyChanged();
    emit manageBusyChanged();
    emit manageBrowseChanged();
    emit manageResultsChanged();
    emit manageErrorChanged();
}
