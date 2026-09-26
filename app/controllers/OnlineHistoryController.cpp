#include "core/ControllerTask.h"
#include "core/CardAuthor.h"
#include "core/PlaybackEntry.h"
#include "controllers/OnlineHistoryController.h"

#include <QMetaObject>
#include <QThreadPool>

#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"
#include "core/HistoryApi.h"

namespace {
constexpr int kPageSize = 30;
}  // namespace

OnlineHistoryController::OnlineHistoryController(QObject *parent) : QObject(parent) {}

bool OnlineHistoryController::busy() const { return busy_.load(); }

bool OnlineHistoryController::loaded() const { return loaded_; }

qreal OnlineHistoryController::scrollOffset() const { return scrollOffset_; }

void OnlineHistoryController::setScrollOffset(qreal value) {
    if (qFuzzyCompare(scrollOffset_ + 1, value + 1)) return;
    scrollOffset_ = value;
    emit scrollOffsetChanged();
}

void OnlineHistoryController::ensureLoaded() {
    if (loaded_ || busy_.load()) return;
    refresh();
}

QString OnlineHistoryController::searchText() const { return searchText_; }

void OnlineHistoryController::setSearchText(const QString &value) {
    if (searchText_ == value) return;
    searchText_ = value;
    emit searchTextChanged();
}

bool OnlineHistoryController::ended() const { return ended_; }

bool OnlineHistoryController::unauthorized() const { return unauthorized_; }

QVariantList OnlineHistoryController::pool() const { return pool_; }

void OnlineHistoryController::refresh() {
    if (busy_) return;
    const int generation = ++generation_;
    ended_ = false;
    unauthorized_ = false;
    hasCursor_ = false;
    lastCursorKey_.clear();
    cursor_ = {};
    pool_.clear();
    poolKeys_.clear();
    emit endedChanged();
    emit unauthorizedChanged();
    emit poolChanged();
    startFetch(generation);
}

void OnlineHistoryController::loadMore() {
    if (ended_) return;
    startFetch(generation_);
}

void OnlineHistoryController::startFetch(int generation) {
    // busy 单闸门:刷新与续载互斥,QML 侧重复触发在此兜底
    if (busy_.exchange(true)) return;
    emit busyChanged();

    const HistoryCursor cursor = cursor_;
    runControllerTask(this, [this, generation, cursor] {
        QList<HistoryItem> items;
        HistoryCursor nextCursor;
        bool hasCursor = false;
        QString error;
        bool unauthorized = false;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            const BilibiliHistoryPage page = HistoryApi::fetchPage(
                    *BilibiliApiClient::instance(), cookie, cursor, kPageSize);
            items = page.items;
            nextCursor = page.cursor;
            hasCursor = true;
        } catch (const ApiUnauthorizedError &e) {
            unauthorized = true;
            error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        return [this, generation, items, nextCursor, hasCursor, error, unauthorized] {
            finishFetch(generation, items, nextCursor, hasCursor, error, unauthorized);
        };
    });
}

void OnlineHistoryController::finishFetch(int generation, const QList<HistoryItem> &items,
                                          const HistoryCursor &nextCursor, bool hasCursor,
                                          const QString &error, bool unauthorized) {
    if (generation != generation_) return;  // Expired page results cannot clear a newer busy state.
    busy_.store(false);
    emit busyChanged();

    if (!loaded_) {
        loaded_ = true;
        emit loadedChanged();
    }

    if (!error.isEmpty()) {
        // 失败不中断已加载内容,游标进度保留,后续滚动/刷新可重试
        unauthorized_ = unauthorized;
        emit unauthorizedChanged();
        emit loadFailed(error);
        return;
    }
    unauthorized_ = false;
    emit unauthorizedChanged();

    if (items.isEmpty()) {
        setEnded();  // 终止信号一:空页
        return;
    }

    // video_key 去重:连续页重复条目不重复渲染
    bool poolDirty = false;
    for (const HistoryItem &item : items) {
        if (poolKeys_.contains(item.videoKey)) continue;
        poolKeys_.insert(item.videoKey);
        pool_.append(toItemMap(item));
        poolDirty = true;
    }
    if (poolDirty) trimPool();

    // 终止信号二/三:游标缺 business / 游标与上页相同(防死循环),
    // 口径与 HistorySyncRunner 一致
    if (!hasCursor || nextCursor.business.trimmed().isEmpty()) {
        setEnded();
    } else {
        const QString key = cursorKey(nextCursor);
        if (hasCursor_ && key == lastCursorKey_) {
            setEnded();
        } else {
            lastCursorKey_ = key;
            hasCursor_ = true;
            cursor_ = nextCursor;
        }
    }
    if (poolDirty) emit poolChanged();
}

void OnlineHistoryController::setEnded() {
    if (ended_) return;
    ended_ = true;
    emit endedChanged();
}

void OnlineHistoryController::trimPool() {
    // 内存安全上限,远高于正常单次浏览量;仅托底超长会话下的无限累积,
    // 不改变到底判定/去重/搜索投影的正常路径(见 online-history-ui 规格)。
    const int excess = pool_.size() - kMaxPoolEntries;
    if (excess <= 0) return;
    for (int i = 0; i < excess; ++i)
        poolKeys_.remove(pool_.at(i).toMap().value("videoKey").toString());
    pool_.remove(0, excess);
}

QVariantMap OnlineHistoryController::toItemMap(const HistoryItem &item) {
    // 键名与 HistoryController::toVariantList 一致;云端条目无多次观看聚合
    // (viewCount 恒 0,无 viewRecords),卡片多次观看角标自然不显
    QVariantMap map;
    map.insert("videoKey", item.videoKey);
    map.insert("title", item.title);
    map.insert("subtitle", item.subtitle);
    map.insert("coverUrl", item.coverUrl);
    map.insert("authorName", item.authorName);
    map.insert("authorMid", QString::number(item.authorMid));
    map.insert("faceUrl", historyAuthorFaceUrl(item));
    map.insert("viewAt", item.viewAt);
    map.insert("progress", item.progress);
    map.insert("duration", item.duration);
    map.insert("badge", item.badge);
    map.insert("linkUrl", item.linkUrl);
    map.insert("viewCount", item.viewCount);
    map.insert("rawJson", item.rawJson);
    map.insert("business", item.business);
    map.insert("oid", item.oid);
    map.insert("kid", item.kid);
    return PlaybackEntry::normalize(map);
}

QString OnlineHistoryController::cursorKey(const HistoryCursor &cursor) {
    return QStringLiteral("%1:%2:%3")
            .arg(cursor.max)
            .arg(cursor.viewAt)
            .arg(cursor.business);
}

void OnlineHistoryController::releasePageCache() {
    ++generation_;
    busy_ = false;
    ended_ = false;
    unauthorized_ = false;
    hasCursor_ = false;
    cursor_ = {};
    lastCursorKey_.clear();
    pool_ = {};
    poolKeys_ = {};
    // 超时回收后重建页面必须重新拉取首页；导航记忆随数据一起失效。
    loaded_ = false;
    scrollAnchor_ = {};
    scrollOffset_ = 0;
    searchText_.clear();
    emit loadedChanged();
    emit scrollOffsetChanged();
    emit searchTextChanged();
    emit busyChanged();
    emit endedChanged();
    emit unauthorizedChanged();
    emit poolChanged();
}
