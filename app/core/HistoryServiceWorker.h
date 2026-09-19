#ifndef HISTORY_SERVICE_WORKER_H
#define HISTORY_SERVICE_WORKER_H

#include "core/HistoryScheduler.h"
#include <QDateTime>
#include <QMap>
#include <QString>
#include <atomic>
#include <functional>

// The production timer and the offline regression use this same single-flight
// tick. Only the sync runner is injected; state/config reads are real and atomic.
class HistoryServiceWorker {
public:
    enum class TickResult { Waiting, Stopped, Completed, SyncFailed };
    using Runner = std::function<void(const HistoryServiceConfig &, std::atomic_bool &)>;
    HistoryServiceWorker(QString configPath, Runner runner);
    TickResult tick(const QDateTime &now, std::atomic_bool &cancel);
    QString statePath() const;
private:
    void loadState();
    void saveAttempt(const QString &key, const QDate &date);
    QString configPath_;
    Runner runner_;
    QMap<QString, QDate> attempts_;
    bool busy_ = false;
};

int runHistoryServiceWorker(int argc, char **argv);

#endif
