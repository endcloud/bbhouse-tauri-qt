#ifndef HISTORY_STORE_H
#define HISTORY_STORE_H

#include <QList>
#include <QString>
#include <QPair>
#include <optional>

#include "core/HistoryModels.h"

class QSqlDatabase;

// 本地 SQLite 存储层(契约见 openspec/specs/local-history-store):
// videos(每视频一行,展示元数据镜像最近一次观看)+ view_records(每次观看一行,
// UNIQUE(video_key, view_at) 保证重同步幂等)+ sync_runs / cursor_snapshots(审计)
// + playback_positions(播放器写穿续播点)。WAL + busy timeout,双进程(应用/服务)
// 共库并发安全。阻塞式 API,约定在工作线程使用。
class HistoryStore {
   public:
    explicit HistoryStore(QString databasePath);

    const QString &databasePath() const { return databasePath_; }
    void initialize();

    QList<HistoryItem> loadItems(int offset = 0, int limit = 0x7FFFFFFF);
    int count();
    int countRecords();

    qint64 startSyncRun(const QString &source);
    void finishSyncRun(qint64 runId, const SyncResult &result, const QString &status,
                       const QString &message);
    QList<SyncRunRecord> listRecentRuns(int limit);

    // 页级事务持久化:返回 (新增观看记录数, 已存在观看记录数)
    QPair<int, int> upsertItems(qint64 runId, int pageIndex, const BilibiliHistoryPage &page);

    void exportJson(const QString &exportPath);

    // 从未在播放窗口播过返回 nullopt;0 = 本地已看完,重播从头
    std::optional<double> getPlaybackPosition(const QString &videoKey);
    void savePlaybackPosition(const QString &videoKey, double positionSeconds,
                              double durationSeconds, int timeoutSeconds = 30);

   private:
    QSqlDatabase createConnection(int timeoutSeconds = 30);
    void ensureSyncRunSourceColumn(QSqlDatabase &db);
    void migrateLegacyItems(QSqlDatabase &db);
    void recomputeViewCounts(QSqlDatabase &db, const QList<QString> &videoKeys = {});
    static QString buildVideoKey(const HistoryItem &item);
    static QString extractBvid(const QString &rawJson);
    static QString nowIso();

    QString databasePath_;
};

#endif  // HISTORY_STORE_H
