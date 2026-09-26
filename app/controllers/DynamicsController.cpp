#include "core/ControllerTask.h"
#include "core/PlaybackEntry.h"
#include "controllers/DynamicsController.h"

#include <algorithm>

#include <QMetaObject>
#include <QThreadPool>

#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"
#include "core/DynamicApi.h"
#include "core/VideoZoneApi.h"
#include "core/VideoZones.h"

namespace {
// 单轮续载:拉取至当前筛选新增 >= 24 卡,或 has_more=false,或 6 页上限
constexpr int kMaxPagesPerRound = 6;
constexpr int kTargetMatchesPerRound = 24;
// 分区补查串行节流间隔(ms)
constexpr int kZoneQueryIntervalMs = 250;
// 加载池内存安全上限,远高于正常单次浏览量;仅托底超长会话下的无限累积。
constexpr int kMaxPoolEntries = 2000;
}  // namespace

DynamicsController::DynamicsController(QObject *parent) : QObject(parent) {
    zoneTimer_.setSingleShot(true);
    zoneTimer_.setInterval(kZoneQueryIntervalMs);
    connect(&zoneTimer_, &QTimer::timeout, this, &DynamicsController::pumpZoneQueue);
}

bool DynamicsController::busy() const { return busy_.load(); }

bool DynamicsController::ended() const { return ended_; }

bool DynamicsController::unauthorized() const { return unauthorized_; }

QVariantList DynamicsController::pool() const { return pool_; }

QVariantList DynamicsController::items() const { return items_; }

QString DynamicsController::categoryFilter() const { return categoryFilter_; }

QString DynamicsController::zoneFilter() const { return zoneFilter_; }

QString DynamicsController::searchText() const { return searchText_; }

bool DynamicsController::zoneGateActive() const { return zoneGateActive_; }

qreal DynamicsController::scrollOffset() const { return scrollOffset_; }

void DynamicsController::setScrollOffset(qreal value) {
    if (qFuzzyCompare(scrollOffset_ + 1, value + 1)) return;
    scrollOffset_ = value;
    emit scrollOffsetChanged();
}

QString DynamicsController::categoryToString(DynamicCategory category) {
    switch (category) {
        case DynamicCategory::Video: return QStringLiteral("video");
        case DynamicCategory::Pgc: return QStringLiteral("pgc");
        case DynamicCategory::Live: return QStringLiteral("live");
        case DynamicCategory::Article: return QStringLiteral("article");
        case DynamicCategory::Post: return QStringLiteral("post");
        case DynamicCategory::Other: return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

bool DynamicsController::matchesFeedItem(const DynamicFeedItem &item,
                                         const FilterSnapshot &filter) {
    const QString category = categoryToString(item.category);
    if (filter.category != QLatin1String("all") && category != filter.category) return false;
    if (!filter.search.isEmpty()) {
        const QString title = item.title.toLower();
        const QString author = item.authorName.toLower();
        if (!title.contains(filter.search) && !author.contains(filter.search)) return false;
    }
    return true;
}

int DynamicsController::findPoolIndexByAid(qint64 aid) const {
    for (int i = 0; i < pool_.size(); ++i) {
        if (pool_.at(i).toMap().value("aid").toLongLong() == aid) return i;
    }
    return -1;
}

void DynamicsController::setCategoryFilter(const QString &value) {
    if (categoryFilter_ == value) return;
    categoryFilter_ = value;
    emit categoryFilterChanged();
    reproject();
    updateZoneGate();
    // 回到视频档:挂起队列继续串行排空(离开期间不补查)
    if (categoryFilter_ == QLatin1String("video") && !zoneInFlight_ &&
        !pendingZoneAids_.isEmpty()) {
        zoneTimer_.start();
    }
}

void DynamicsController::setZoneFilter(const QString &value) {
    if (zoneFilter_ == value) return;
    zoneFilter_ = value;
    emit zoneFilterChanged();
    reproject();
}

void DynamicsController::setSearchText(const QString &value) {
    if (searchText_ == value) return;
    searchText_ = value;
    searchLower_ = value.trimmed().toLower();
    emit searchTextChanged();
    reproject();
}

void DynamicsController::refresh() {
    if (busy_) return;
    ++generation_;
    ended_ = false;
    unauthorized_ = false;
    offset_.clear();
    // 去重分组一并重置;既有池保留至新数据到达(刷新不闪空白),成功一轮后
    // 整体替换。分区会话缓存保留(同一 av 号会话内不重复补查)
    poolIds_.clear();
    poolAids_.clear();
    aidOccurrences_.clear();
    pendingZoneAids_.clear();
    refreshPending_ = true;
    emit endedChanged();
    emit unauthorizedChanged();
    updateZoneGate();
    startLoad();
}

void DynamicsController::loadMore() {
    if (ended_) return;
    startLoad();
}

void DynamicsController::startLoad() {
    // busy 单闸门:刷新与续载互斥,QML 侧重复触发在此兜底
    if (busy_.exchange(true)) return;
    emit busyChanged();

    const int generation = generation_;
    const QString offset = offset_;
    const FilterSnapshot snapshot{categoryFilter_, zoneFilter_, searchLower_};

    runControllerTask(this, [this, generation, offset, snapshot] {
        QList<DynamicFeedItem> collected;
        QString nextOffset = offset;
        bool hasMore = true;
        bool unauthorized = false;
        QString error;
        QString cookie;
        try {
            cookie = BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        if (error.isEmpty()) {
            BilibiliApiClient &api = *BilibiliApiClient::instance();
            int pages = 0;
            int matched = 0;
            while (pages < kMaxPagesPerRound && matched < kTargetMatchesPerRound && hasMore) {
                DynamicFeedPage page;
                try {
                    page = DynamicApi::fetchFeed(api, cookie, nextOffset);
                } catch (const ApiUnauthorizedError &e) {
                    unauthorized = true;
                    error = QString::fromUtf8(e.what());
                    break;
                } catch (const std::exception &e) {
                    error = QString::fromUtf8(e.what());
                    break;
                }
                for (const DynamicFeedItem &item : page.items) {
                    if (matchesFeedItem(item, snapshot)) matched++;
                    collected.append(item);
                }
                pages++;
                if (!page.hasMore || page.offset.trimmed().isEmpty()) {
                    hasMore = false;  // 终止信号:无更多
                    break;
                }
                if (page.offset == nextOffset) {
                    hasMore = false;  // 同 offset 重复,防死循环
                    break;
                }
                nextOffset = page.offset;
            }
        } else {
            hasMore = false;
        }
        return [this, generation, collected, nextOffset, hasMore, error, unauthorized] {
            finishLoad(generation, collected, nextOffset, hasMore, error, unauthorized);
        };
    });
}

void DynamicsController::finishLoad(int generation, const QList<DynamicFeedItem> &collected,
                                    const QString &nextOffset, bool hasMore,
                                    const QString &error, bool unauthorized) {
    if (generation != generation_) return;  // Expired page results cannot clear a newer busy state.
    busy_.store(false);
    emit busyChanged();

    if (!error.isEmpty()) {
        // 失败不中断已加载内容(刷新场景下旧池继续呈现),offset 进度保留,
        // 后续滚动/刷新可重试;refreshPending 保持,成功一轮后仍整体重建
        unauthorized_ = unauthorized;
        emit unauthorizedChanged();
        emit loadFailed(error);
        return;
    }
    unauthorized_ = false;
    emit unauthorizedChanged();

    // 刷新成功一轮:丢弃旧池与旧投影,以本轮数据重建(去重分组已在 refresh 清空)
    if (refreshPending_) {
        pool_.clear();
        nextPoolOrder_ = 0;
        refreshPending_ = false;
    }

    mergeItems(collected);
    emit poolChanged();
    updateZoneNames();
    reproject();
    offset_ = nextOffset;
    if (!hasMore) setEnded();
    queueZoneLookups();
}

void DynamicsController::mergeItems(const QList<DynamicFeedItem> &collected) {
    bool dirty = false;
    for (const DynamicFeedItem &item : collected) {
        if (item.category == DynamicCategory::Video && item.aid > 0) {
            // 视频类按 av 号去重:出现记录累积;代表条目取组内发布最早者
            // (并列取先入池)。更早条目到达时替换代表,池重排后移动到其
            // 在动态流中的自然位置
            aidOccurrences_[item.aid].append({item.pubTs, item.authorName, item.isForward});
            const int existing = poolAids_.contains(item.aid) ? findPoolIndexByAid(item.aid) : -1;
            if (existing < 0) {
                poolAids_.insert(item.aid);
                QVariantMap map = toItemMap(item);
                map.insert("_poolOrder", ++nextPoolOrder_);
                if (zoneCache_.contains(item.aid)) {
                    map.insert("zoneName", zoneCache_.value(item.aid));
                }
                pool_.append(map);
            } else {
                QVariantMap map = pool_.at(existing).toMap();
                const qint64 currentTs = map.value("pubTs").toLongLong();
                const bool earlier =
                        item.pubTs > 0 && (currentTs <= 0 || item.pubTs < currentTs);
                if (earlier) {
                    const QVariant order = map.value("_poolOrder");
                    map = toItemMap(item);
                    map.insert("_poolOrder", order);
                    if (zoneCache_.contains(item.aid)) {
                        map.insert("zoneName", zoneCache_.value(item.aid));
                    }
                    pool_[existing] = map;
                }
            }
            dirty = true;
            continue;
        }
        // 非视频按动态 id 独立呈现(同 id 重复条目不重复渲染)
        if (!item.id.isEmpty() && poolIds_.contains(item.id)) continue;
        if (!item.id.isEmpty()) poolIds_.insert(item.id);
        QVariantMap map = toItemMap(item);
        map.insert("_poolOrder", ++nextPoolOrder_);
        pool_.append(map);
        dirty = true;
    }
    if (!dirty) return;

    decorateDuplicates();
    // 池按 pubTs 降序稳定排序:代表条目换成更早投稿时移动到自然位置,
    // 并列时保持先入池顺序(动态流自然序)
    std::stable_sort(pool_.begin(), pool_.end(),
                     [](const QVariant &a, const QVariant &b) {
                         return a.toMap().value("pubTs").toLongLong() >
                                b.toMap().value("pubTs").toLongLong();
                     });
    trimPool();
}

void DynamicsController::trimPool() {
    const int excess = pool_.size() - kMaxPoolEntries;
    if (excess <= 0) return;
    QList<int> oldest;
    oldest.reserve(pool_.size());
    for (int i = 0; i < pool_.size(); ++i) oldest.append(i);
    std::sort(oldest.begin(), oldest.end(), [this](int a, int b) {
        return pool_[a].toMap().value("_poolOrder").toULongLong() <
               pool_[b].toMap().value("_poolOrder").toULongLong();
    });
    oldest.resize(excess);
    std::sort(oldest.begin(), oldest.end(), std::greater<int>());
    for (const int index : oldest) {
        const QVariantMap map = pool_.at(index).toMap();
        if (map.value("category").toString() == QLatin1String("video")) {
            const qint64 aid = map.value("aid").toLongLong();
            if (aid > 0) {
                poolAids_.remove(aid);
                aidOccurrences_.remove(aid);
                pendingZoneAids_.removeAll(aid);
            }
        } else {
            poolIds_.remove(map.value("id").toString());
        }
        pool_.removeAt(index);
    }
}

void DynamicsController::decorateDuplicates() {
    for (int i = 0; i < pool_.size(); ++i) {
        QVariantMap map = pool_.at(i).toMap();
        const qint64 aid = map.value("aid").toLongLong();
        if (aid <= 0) continue;
        const QList<Occurrence> occurrences = aidOccurrences_.value(aid);
        map.insert("duplicateCount", occurrences.size());
        QVariantList records;
        records.reserve(occurrences.size());
        for (const Occurrence &occurrence : occurrences) {
            QVariantMap record;
            record.insert("pubTs", occurrence.pubTs);
            record.insert("authorName", occurrence.authorName);
            record.insert("isForward", occurrence.isForward);
            records.append(record);
        }
        // tooltip 按发布时间倒序
        std::stable_sort(records.begin(), records.end(),
                         [](const QVariant &a, const QVariant &b) {
                             return a.toMap().value("pubTs").toLongLong() >
                                    b.toMap().value("pubTs").toLongLong();
                         });
        map.insert("duplicateTimes", records);
        pool_[i] = map;
    }
}

void DynamicsController::reproject() {
    const FilterSnapshot snapshot{categoryFilter_, zoneFilter_, searchLower_};
    QVariantList items;
    items.reserve(pool_.size());
    for (const QVariant &value : pool_) {
        if (matchesFilter(value.toMap(), snapshot)) items.append(value);
    }
    if (items_ == items) return;
    emit itemsAboutToChange();
    items_ = items;
    cardModel_.setItems(items_);
    emit itemsChanged();
}

void DynamicsController::updateZoneNames() {
    QStringList present;
    for (const QVariant &value : pool_) {
        const auto item = value.toMap();
        const QString zone = item.value("zoneName").toString();
        if (item.value("category").toString() == "video" && !zone.isEmpty() && !present.contains(zone))
            present.append(zone);
    }
    // Keep discovery order so each response cannot reorder existing chips.
    QStringList next;
    for (const auto &zone : zoneNames_) if (present.contains(zone)) next.append(zone);
    for (const auto &zone : present) if (!next.contains(zone)) next.append(zone);
    if (next == zoneNames_) return;
    zoneNames_ = next;
    emit zoneNamesChanged();
}

bool DynamicsController::matchesFilter(const QVariantMap &item,
                                       const FilterSnapshot &filter) const {
    const QString category = item.value("category").toString();
    if (filter.category != QLatin1String("all") && category != filter.category) return false;
    if (!filter.zone.isEmpty() && category == QLatin1String("video") &&
        item.value("zoneName").toString() != filter.zone) {
        return false;
    }
    if (!filter.search.isEmpty()) {
        const QString title = item.value("title").toString().toLower();
        const QString author = item.value("authorName").toString().toLower();
        if (!title.contains(filter.search) && !author.contains(filter.search)) return false;
    }
    return true;
}

void DynamicsController::queueZoneLookups() {
    bool added = false;
    for (const QVariant &value : pool_) {
        const QVariantMap map = value.toMap();
        if (map.value("category").toString() != QLatin1String("video")) continue;
        const qint64 aid = map.value("aid").toLongLong();
        if (aid <= 0) continue;
        if (zoneCache_.contains(aid)) continue;   // 会话缓存命中:至多一次请求
        if (pendingZoneAids_.contains(aid)) continue;
        pendingZoneAids_.append(aid);
        added = true;
    }
    updateZoneGate();
    if (added && categoryFilter_ == QLatin1String("video") && !zoneInFlight_) {
        zoneTimer_.start();
    }
}

void DynamicsController::updateZoneGate() {
    // 仅视频档存在"解析中"语义:待解析清空或离开视频档即关门
    const bool active = categoryFilter_ == QLatin1String("video") &&
                        (!pendingZoneAids_.isEmpty() || zoneInFlight_);
    if (zoneGateActive_ == active) return;
    zoneGateActive_ = active;
    emit zoneGateActiveChanged();
}

void DynamicsController::pumpZoneQueue() {
    if (zoneInFlight_ || pendingZoneAids_.isEmpty()) return;
    // 非视频档不补查(挂起队列保留,切回视频档后继续)
    if (categoryFilter_ != QLatin1String("video")) return;
    const qint64 aid = pendingZoneAids_.takeFirst();
    zoneInFlight_ = true;
    updateZoneGate();
    const auto generation = zoneGeneration_;
    runControllerTask(this, [this, aid, generation] {
        QString zoneName;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            const VideoViewInfo info = VideoZoneApi::fetchViewByAid(
                    *BilibiliApiClient::instance(), cookie, aid);
            // 分区名必须经静态表解析(响应 tname 抽样恒空);主分区 = 顶级分区。
            // 解析失败按"未知分区"参与呈现与筛选,不重试
            QString name;
            QString topName;
            zoneName = VideoZones::tryResolve(info.tid, info.tidV2, &name, &topName)
                               ? topName
                               : Loc::get("未知分区");
        } catch (const std::exception &) {
            zoneName = Loc::get("未知分区");
        }
        return [this, aid, zoneName, generation] {
            if (generation == zoneGeneration_) applyZoneResult(aid, zoneName);
        };
    });
}

void DynamicsController::applyZoneResult(qint64 aid, const QString &zoneName) {
    zoneInFlight_ = false;
    zoneCache_.insert(aid, zoneName);
    bool dirty = false;
    for (int i = 0; i < pool_.size(); ++i) {
        QVariantMap map = pool_.at(i).toMap();
        if (map.value("category").toString() != QLatin1String("video")) continue;
        if (map.value("aid").toLongLong() != aid) continue;
        if (!map.value("zoneName").toString().isEmpty()) continue;
        map.insert("zoneName", zoneName);
        pool_[i] = map;
        dirty = true;
    }
    if (dirty) {
        emit poolChanged();
        updateZoneNames();
        reproject();  // 局部字段更新,投影即时纳入新解析分区
    }
    updateZoneGate();
    if (!pendingZoneAids_.isEmpty() && categoryFilter_ == QLatin1String("video")) {
        zoneTimer_.start();  // 串行节流:间隔 250ms 取下一个
    }
}

void DynamicsController::setEnded() {
    if (ended_) return;
    ended_ = true;
    emit endedChanged();
}

QVariantMap DynamicsController::toItemMap(const DynamicFeedItem &item) {
    QVariantMap map;
    map.insert("id", item.id);
    map.insert("category", categoryToString(item.category));
    map.insert("majorType", item.majorType);
    map.insert("title", item.title);
    map.insert("subtitle", item.summary);
    map.insert("coverUrl", item.coverUrl);
    map.insert("authorName", item.authorName);
    map.insert("authorMid", QString::number(item.authorMid));
    map.insert("faceUrl", item.authorFaceUrl);
    map.insert("pubTs", item.pubTs);
    map.insert("viewAt", item.pubTs);  // HistoryCard 时间字段口径
    map.insert("duration", item.durationSeconds);
    map.insert("aid", item.aid);
    map.insert("oid", item.aid);  // PlayerController archive 解析路径按 oid 取 av 号
    // 失效动态不可点击跳转:置空链接(卡片点击与"打开链接"菜单项随之禁用)
    map.insert("linkUrl", item.isUnavailable ? QString() : item.linkUrl);
    map.insert("isForward", item.isForward);
    // invalid 复用 HistoryCard 的失效禁播口径;dynamicUnavailable 驱动占位渲染
    // (无封面/无标题,标签呈"动态已失效")
    map.insert("invalid", item.isUnavailable);
    map.insert("dynamicUnavailable", item.isUnavailable);
    map.insert("rawJson", item.rawJson);
    // business 兼容历史域取值:archive + oid>0 使视频档卡片主体可起播;
    // pgc 有 epid 即可起播,缺 cid 由播放器补全,其余类型不在可播集合
    switch (item.category) {
        case DynamicCategory::Video: map.insert("business", QStringLiteral("archive")); break;
        case DynamicCategory::Pgc: map.insert("business", QStringLiteral("pgc")); break;
        case DynamicCategory::Live: map.insert("business", QStringLiteral("live")); break;
        case DynamicCategory::Article: map.insert("business", QStringLiteral("article")); break;
        case DynamicCategory::Post: map.insert("business", QStringLiteral("post")); break;
        case DynamicCategory::Other: map.insert("business", QStringLiteral("other")); break;
    }
    // 业务标签:video/pgc/live/article 由 business 直映;post 呈"图文";
    // other 回退条目 majorType,再回退"动态"
    QString badge = item.majorType;
    if (item.category == DynamicCategory::Post) {
        badge = Loc::get("图文");
    } else if (item.category == DynamicCategory::Other &&
               item.majorType.trimmed().isEmpty()) {
        badge = Loc::get("动态");
    }
    map.insert("badge", badge);
    // 播放列表键(av 号口径,与历史域 video_key 同形);非视频为空不参与去重
    map.insert("videoKey",
               item.aid > 0 ? QStringLiteral("av") + QString::number(item.aid) : QString());
    map.insert("zoneName", QString());
    map.insert("duplicateCount", 1);
    return PlaybackEntry::normalize(map);
}

void DynamicsController::releasePageCache() {
    ++generation_;
    ++zoneGeneration_;
    zoneTimer_.stop();
    busy_ = false;
    zoneInFlight_ = false;
    ended_ = false;
    unauthorized_ = false;
    refreshPending_ = false;
    offset_.clear();
    pool_ = {};
    items_ = {};
    cardModel_.setItems({});
    zoneNames_ = {};
    poolIds_ = {};
    poolAids_ = {};
    aidOccurrences_ = {};
    zoneCache_ = {};
    pendingZoneAids_ = {};
    zoneFilter_.clear();
    searchText_.clear();
    searchLower_.clear();
    scrollAnchor_ = {};
    scrollOffset_ = 0;
    updateZoneGate();
    emit scrollAnchorChanged();
    emit scrollOffsetChanged();
    emit busyChanged();
    emit endedChanged();
    emit unauthorizedChanged();
    emit poolChanged();
    emit itemsChanged();
    emit zoneNamesChanged();
    emit zoneFilterChanged();
    emit searchTextChanged();
}
