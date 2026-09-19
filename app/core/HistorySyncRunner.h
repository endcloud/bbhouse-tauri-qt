#ifndef HISTORY_SYNC_RUNNER_H
#define HISTORY_SYNC_RUNNER_H

#include <atomic>
#include <functional>
#include <QString>

#include "core/HistoryModels.h"
#include "core/HistoryStore.h"

// 全量爬取编排(契约见 openspec/specs/history-sync):游标翻页 + 1s 限速 +
// 页级落库 + JSON 导出 + sync_runs 审计。主应用手动同步与定时任务共用。
// 阻塞式,应在工作线程调用;cancel 由外部置位。
class HistorySyncRunner {
   public:
    struct Request {
        QString cookiePath;
        QString exportPath;
        QString source;
    };

    explicit HistorySyncRunner(HistoryStore &store);
    virtual ~HistorySyncRunner() = default;

    // status 回调用于 UI 状态栏;抛出的异常(AccessError/ApiError 等)已在审计行落库后重抛
    SyncResult run(const Request &request,
                   const std::function<void(const QString &)> &progress,
                   std::atomic_bool &cancel);

   protected:
    virtual BilibiliHistoryPage fetchPage(const QString &cookie, const HistoryCursor &cursor);
    virtual bool waitForNextPage(std::atomic_bool &cancel);

   private:
    HistoryStore &store_;
};

#endif  // HISTORY_SYNC_RUNNER_H
