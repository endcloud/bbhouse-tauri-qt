#ifndef HISTORY_SERVICE_CONTROLLER_H
#define HISTORY_SERVICE_CONTROLLER_H

#include <QObject>
#include <QVariantList>
#include "core/HistoryScheduler.h"

// GUI bridge; native scheduler and SQLite reads always run off the UI thread.
class HistoryServiceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY stateChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    Q_PROPERTY(bool statusKnown READ statusKnown NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY stateChanged)
    Q_PROPERTY(QString logError READ logError NOTIFY stateChanged)
    Q_PROPERTY(QString time READ time NOTIFY stateChanged)
    Q_PROPERTY(QString cycle READ cycle NOTIFY stateChanged)
    Q_PROPERTY(int weekday READ weekday NOTIFY stateChanged)
    Q_PROPERTY(QVariantList runs READ runs NOTIFY stateChanged)
public:
    explicit HistoryServiceController(QObject *parent = nullptr);
    bool busy() const { return busy_; }
    bool registered() const { return status_.registered; }
    bool enabled() const { return status_.enabled; }
    bool statusKnown() const { return statusKnown_; }
    QString error() const { return error_; }
    QString diagnostic() const { return status_.diagnostic; }
    QString logError() const { return logError_; }
    QString time() const { return config_.time; }
    QString cycle() const { return config_.cycle; }
    int weekday() const { return config_.weekday; }
    QVariantList runs() const { return runs_; }

    Q_INVOKABLE void requestOpen();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void registerService();
    Q_INVOKABLE void save(const QString &time, const QString &cycle, int weekday);
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void unregisterService();
signals:
    void stateChanged();
    void registrationRequired();
    void openRequested();
    void operationFinished(QString message);
    void operationFailed(QString message);
private:
    enum class Action { Open, Refresh, Register, Save, Enable, Unregister };
    void perform(Action action, const QString &time = {}, const QString &cycle = {},
                 int weekday = 1, bool enabled = true);
    static HistoryServiceConfig currentPaths();
    bool busy_ = false;
    bool statusKnown_ = false;
    QString error_;
    QString logError_;
    HistoryServiceConfig config_;
    HistoryServiceStatus status_;
    QVariantList runs_;
};
#endif
