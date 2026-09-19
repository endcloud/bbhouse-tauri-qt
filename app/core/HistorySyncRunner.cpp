#include "core/HistorySyncRunner.h"

#include <QThread>
#include <QElapsedTimer>
#include <QLockFile>
#include <QSet>

#include "core/ApiErrors.h"
#include "core/BilibiliApiClient.h"
#include "core/HistoryApi.h"

namespace {
constexpr int kPageSize = 30;
}

HistorySyncRunner::HistorySyncRunner(HistoryStore &store) : store_(store) {}

SyncResult HistorySyncRunner::run(const Request &request,
                                  const std::function<void(const QString &)> &progress,
                                  std::atomic_bool &cancel) {
    QLockFile lock(store_.databasePath() + ".sync.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock())
        throw std::runtime_error(Loc::get("无法开始同步：已有同步任务运行，或缓存目录不可写").toStdString());
    SyncResult result;
    const qint64 runId = store_.startSyncRun(request.source);

    try {
        const QString cookie = BilibiliApiClient::readCookieFromFile(request.cookiePath);

        HistoryCursor cursor;
        QSet<QString> seenCursors;

        while (!cancel.load()) {
            if (progress) {
                progress(Loc::get("同步中:第 %1 页,新增 %2 条")
                                 .arg(QString::number(result.pages + 1),
                                      QString::number(result.inserted)));
            }
            const BilibiliHistoryPage page =
                    fetchPage(cookie, cursor);
            result.pages++;
            result.seen += page.items.size();

            const auto upsert = store_.upsertItems(runId, result.pages, page);
            result.inserted += upsert.first;
            result.updated += upsert.second;

            // 空页正常结束；缺失/循环游标是异常，不伪装云端历史已翻尽。
            if (page.items.isEmpty()) {
                result.stoppedByEmptyPage = true;
                break;
            }
            const QString nextCursorKey = QString("%1:%2:%3")
                                                  .arg(page.cursor.max)
                                                  .arg(page.cursor.viewAt)
                                                  .arg(page.cursor.business);
            if (page.cursor.business.trimmed().isEmpty())
                throw std::runtime_error(Loc::get("历史接口返回了无效游标，同步已停止；已保存的数据保留").toStdString());
            if (seenCursors.contains(nextCursorKey))
                throw std::runtime_error(Loc::get("历史游标重复或循环，同步已停止；已保存的数据保留").toStdString());
            seenCursors.insert(nextCursorKey);
            cursor = page.cursor;

            if (progress) {
                progress(Loc::get("已请求 %1 页,读取 %2 条,新增 %3 条")
                                 .arg(QString::number(result.pages),
                                      QString::number(result.seen),
                                      QString::number(result.inserted)));
            }
            if (!waitForNextPage(cancel)) break;
        }

        if (cancel.load()) {
            result.stoppedByEmptyPage = false;
            store_.finishSyncRun(runId, result, "cancelled", Loc::get("已取消"));
            return result;
        }

        store_.exportJson(request.exportPath);
        store_.finishSyncRun(runId, result, "success",
                             Loc::get("已导出到 %1").arg(request.exportPath));
        return result;
    } catch (const ApiUnauthorizedError &e) {
        store_.finishSyncRun(runId, result, "failed", QString::fromUtf8(e.what()));
        throw;
    } catch (const std::exception &e) {
        store_.finishSyncRun(runId, result, "failed", QString::fromUtf8(e.what()));
        throw;
    }
}

BilibiliHistoryPage HistorySyncRunner::fetchPage(const QString &cookie, const HistoryCursor &cursor) {
    return HistoryApi::fetchPage(*BilibiliApiClient::instance(), cookie, cursor, kPageSize);
}

bool HistorySyncRunner::waitForNextPage(std::atomic_bool &cancel) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 1000) {
        if (cancel.load()) return false;
        QThread::msleep(static_cast<unsigned long>(qBound(qint64(0), qint64(1000) - elapsed.elapsed(), qint64(50))));
    }
    return !cancel.load();
}
