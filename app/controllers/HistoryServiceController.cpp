#include "controllers/HistoryServiceController.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QThreadPool>
#include "core/AppPaths.h"
#include "core/HistoryStore.h"
#include "core/ApiErrors.h"

HistoryServiceController::~HistoryServiceController() {
    workerPool_.waitForDone();
}

HistoryServiceController::HistoryServiceController(QObject *parent) : QObject(parent) { workerPool_.setMaxThreadCount(1); }

HistoryServiceConfig HistoryServiceController::currentPaths() {
    HistoryServiceConfig config;
    config.executablePath = QCoreApplication::applicationFilePath();
    config.cookiePath = QFileInfo(AppPaths::cookiePath()).absoluteFilePath();
    config.databasePath = QFileInfo(AppPaths::dbPath()).absoluteFilePath();
    config.exportPath = QFileInfo(AppPaths::exportPath()).absoluteFilePath();
    return config;
}

void HistoryServiceController::requestOpen() { perform(Action::Open); }
void HistoryServiceController::refresh() { perform(Action::Refresh); }
void HistoryServiceController::registerService() { perform(Action::Register); }
void HistoryServiceController::save(const QString &time, const QString &cycle, int weekday) {
    perform(Action::Save, time, cycle, weekday);
}
void HistoryServiceController::setEnabled(bool enabled) { perform(Action::Enable, {}, {}, 1, enabled); }
void HistoryServiceController::unregisterService() { perform(Action::Unregister); }

void HistoryServiceController::perform(Action action, const QString &time, const QString &cycle,
                                       int weekday, bool enabled) {
    if (busy_) return;
    busy_ = true;
    error_.clear();
    emit stateChanged();
    const QString path = QFileInfo(AppPaths::dataDir() + "/history-service.json").absoluteFilePath();
    const auto paths = currentPaths();
    workerPool_.start([this, action, time, cycle, weekday, enabled, path, paths] {
        HistoryScheduler scheduler(path);
        HistoryServiceStatus status;
        HistoryServiceConfig config = paths;
        QString error, logError, message;
        QVariantList runs;
        try {
            // Corrupt configuration remains visible, but does not prevent removing a task.
            if (action != Action::Unregister && QFileInfo::exists(path))
                config = HistoryScheduler::loadConfig(path);
            if (action == Action::Register) {
                config.executablePath = paths.executablePath;
                config.cookiePath = paths.cookiePath;
                config.databasePath = paths.databasePath;
                config.exportPath = paths.exportPath;
                scheduler.install(config);
                message = tr("定时服务已注册");
            } else if (action == Action::Save) {
                const auto current = scheduler.query();
                if (!current.error.isEmpty()) throw std::runtime_error(current.error.toStdString());
                config.enabled = current.enabled;
                config.time = time;
                config.cycle = cycle;
                config.weekday = weekday;
                // Re-saving also repairs executable/cookie paths after moving the application.
                config.executablePath = paths.executablePath;
                config.cookiePath = paths.cookiePath;
                config.databasePath = paths.databasePath;
                config.exportPath = paths.exportPath;
                scheduler.update(config);
                message = tr("运行计划已保存");
            } else if (action == Action::Enable) {
                scheduler.setEnabled(enabled);
                config.enabled = enabled;
                message = enabled ? tr("定时服务已启用") : tr("定时服务已暂停");
            } else if (action == Action::Unregister) {
                scheduler.uninstall();
                message = tr("定时服务已注销，本地数据与配置已保留");
            }
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
            // Failed edits must not be presented as successfully saved settings.
            try { if (QFileInfo::exists(path)) config = HistoryScheduler::loadConfig(path); }
            catch (...) {}
        }
        status = scheduler.query();
        if (!status.error.isEmpty()) {
            if (!error.isEmpty()) error += "\n";
            error += status.error;
        }
        if (status.registered && !QFileInfo::exists(path) && error.isEmpty())
            error = tr("服务配置丢失，请注销后重新注册");
        // A separate log failure must not hide actual registration state.
        try {
            if (QFileInfo::exists(paths.databasePath)) {
                HistoryStore store(paths.databasePath);
                for (const auto &run : store.listRecentRuns(30)) {
                    runs.append(QVariantMap{{"source", run.source}, {"startedAt", run.startedAt},
                        {"finishedAt", run.finishedAt}, {"status", run.status}, {"pages", run.pages},
                        {"seen", run.seen}, {"inserted", run.inserted}, {"existing", run.updated},
                        {"message", run.message}});
                }
            }
        } catch (const std::exception &e) { logError = QString::fromUtf8(e.what()); }
        QMetaObject::invokeMethod(this, [this, action, status, config, error, logError, message, runs] {
            busy_ = false;
            status_ = status;
            statusKnown_ = status.error.isEmpty();
            config_ = config;
            error_ = error;
            logError_ = logError;
            runs_ = runs;
            emit stateChanged();
            if (!error.isEmpty()) {
                emit operationFailed(error);
                // Registered tasks with missing/corrupt local config must remain manageable:
                // the window exposes diagnostics and confirmed unregistration.
                if (action == Action::Open && statusKnown_ && status.registered) emit openRequested();
                return;
            }
            if (action == Action::Open) {
                if (status.registered) emit openRequested();
                else emit registrationRequired();
            } else if (action == Action::Register) {
                emit openRequested();
            }
            if (!message.isEmpty()) emit operationFinished(message);
        }, Qt::QueuedConnection);
    });
}
