#ifndef HISTORY_MODELS_H
#define HISTORY_MODELS_H

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QVector>

// 历史/观看记录域模型(契约同原 HistoryItem.cs)。UI 无关。
struct ViewRecord {
    qint64 viewAt = 0;
    int progress = 0;
};

struct HistoryItem {
    QString videoKey;  // business:oid:kid
    qint64 kid = 0;
    qint64 oid = 0;
    QString business;
    QString title;
    QString subtitle;
    QString coverUrl;
    QString authorName;
    qint64 authorMid = 0;
    qint64 viewAt = 0;
    int progress = 0;
    int duration = 0;
    QString badge;
    QString linkUrl;
    QString rawJson;
    int viewCount = 0;  // 已记录观看事件总数(store 装载时填充)
    QList<ViewRecord> viewRecords;  // 全部观看事件,新→旧(store 装载时填充)
};

struct HistoryCursor {
    qint64 max = 0;
    qint64 viewAt = 0;
    QString business;
    int pageSize = 0;
};

struct BilibiliHistoryPage {
    HistoryCursor cursor;
    QList<HistoryItem> items;
    QString rawJson;
};

struct SyncResult {
    int inserted = 0;
    int updated = 0;
    int seen = 0;
    int pages = 0;
    bool stoppedByEmptyPage = false;
};

// 同步来源(sync_runs.source 数据契约)
namespace SyncSource {
inline constexpr const char *Manual = "manual";
inline constexpr const char *Scheduled = "scheduled";
}  // namespace SyncSource

struct SyncRunRecord {
    qint64 id = 0;
    QString source;
    QString startedAt;
    QString finishedAt;  // 可空
    int pages = 0;
    int seen = 0;
    int inserted = 0;
    int updated = 0;
    QString status;
    QString message;
};

#endif  // HISTORY_MODELS_H
