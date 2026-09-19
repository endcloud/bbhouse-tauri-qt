#include "core/HistoryServiceWorker.h"
#include "core/HistoryServiceSchedule.h"
#include "core/HistoryStore.h"
#include "core/HistorySyncRunner.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <thread>
#endif

namespace {
[[noreturn]] void stateError(const char *message) { throw std::runtime_error(message); }
}

HistoryServiceWorker::HistoryServiceWorker(QString configPath, Runner runner)
    : configPath_(std::move(configPath)), runner_(std::move(runner)) {
    if (!QDir::isAbsolutePath(configPath_) || !runner_)
        stateError("Invalid history service worker configuration path or runner");
    // Validate at startup, even when no run is due.
    HistoryScheduler::loadConfig(configPath_);
    loadState();
}

QString HistoryServiceWorker::statePath() const {
    return QFileInfo(configPath_).dir().filePath("history-service-state.json");
}

void HistoryServiceWorker::loadState() {
    QFile file(statePath());
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        stateError("Cannot read history service attempt state");
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (file.error() != QFileDevice::NoError || error.error != QJsonParseError::NoError || !document.isObject())
        stateError("Invalid history service attempt state");
    const QJsonObject root = document.object();
    if (root.value("version").toInt(-1) != 1 || !root.value("attempts").isObject())
        stateError("Unsupported history service attempt state");
    const QJsonObject entries = root.value("attempts").toObject();
    static const QRegularExpression keyPattern(QStringLiteral("^[0-9a-f]{64}$"));
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QString text = it.value().toString();
        const QDate date = QDate::fromString(text, Qt::ISODate);
        if (!keyPattern.match(it.key()).hasMatch() || !date.isValid() || date.toString(Qt::ISODate) != text)
            stateError("Invalid history service attempt entry");
        attempts_.insert(it.key(), date);
    }
}

void HistoryServiceWorker::saveAttempt(const QString &key, const QDate &date) {
    QJsonObject entries;
    for (auto it = attempts_.cbegin(); it != attempts_.cend(); ++it)
        entries.insert(it.key(), it.value().toString(Qt::ISODate));
    entries.insert(key, date.toString(Qt::ISODate));
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"attempts", entries}}).toJson();
    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        stateError("Cannot persist history service attempt state; synchronization was not started");
    attempts_.insert(key, date);
}

HistoryServiceWorker::TickResult HistoryServiceWorker::tick(const QDateTime &now, std::atomic_bool &cancel) {
    if (cancel.load()) return TickResult::Stopped;
    if (busy_) return TickResult::Waiting;
    QScopedValueRollback<bool> busy(busy_, true);
    const HistoryServiceConfig config = HistoryScheduler::loadConfig(configPath_);
    if (!HistoryServiceSchedule::due(config, now, attempts_)) return TickResult::Waiting;
    saveAttempt(HistoryServiceSchedule::key(config), now.date());
    if (cancel.load()) return TickResult::Stopped;
    try {
        runner_(config, cancel);
    } catch (const std::exception &) {
        // HistorySyncRunner records request failures in sync_runs. Stay alive
        // for the next schedule; never log Cookie or signed request URLs here.
        return cancel.load() ? TickResult::Stopped : TickResult::SyncFailed;
    }
    return cancel.load() ? TickResult::Stopped : TickResult::Completed;
}

int runHistoryServiceWorker(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream errors(stderr);
    QCommandLineParser parser;
    parser.addOption({"history-service-worker", "Run the Windows history service worker."});
    parser.addOption({"config", "Absolute history service configuration path.", "path"});
    parser.addOption({"stop-handle", "Inherited stop event handle.", "handle"});
    parser.addOption({"ready-handle", "Inherited startup-ready event handle.", "handle"});
    if (!parser.parse(app.arguments()) || !parser.positionalArguments().isEmpty()
        || !parser.isSet("history-service-worker") || !parser.isSet("config")
        || !QFileInfo(parser.value("config")).isAbsolute() || !parser.isSet("stop-handle") || !parser.isSet("ready-handle")) {
        errors << "Invalid arguments: --history-service-worker --config <absolute path> --stop-handle <event> --ready-handle <event>\n";
        return 2;
    }
#ifndef Q_OS_WIN
    errors << "History service worker requires Windows\n";
    return 2;
#else
    const auto inheritedEvent = [&](const QString &option) -> HANDLE {
        bool validNumber = false;
        const qulonglong numeric = parser.value(option).toULongLong(&validNumber, 10);
        const HANDLE event = reinterpret_cast<HANDLE>(static_cast<quintptr>(numeric));
        DWORD flags = 0;
        if (!validNumber || !numeric || numeric != static_cast<qulonglong>(reinterpret_cast<quintptr>(event))
            || event == INVALID_HANDLE_VALUE || !GetHandleInformation(event, &flags)
            || !(flags & HANDLE_FLAG_INHERIT) || WaitForSingleObject(event, 0) == WAIT_FAILED)
            return nullptr;
        return event;
    };
    const HANDLE stopEvent = inheritedEvent("stop-handle");
    const HANDLE readyEvent = inheritedEvent("ready-handle");
    if (!stopEvent || !readyEvent || stopEvent == readyEvent) {
        errors << "Invalid inherited history service stop or startup-ready event\n";
        return 2;
    }
    QLockFile workerLock(QFileInfo(parser.value("config")).dir().filePath("history-service-worker.lock"));
    workerLock.setStaleLockTime(0);
    if (!workerLock.tryLock()) {
        errors << "History service worker is already active or its configuration directory is not writable\n";
        CloseHandle(stopEvent);
        CloseHandle(readyEvent);
        return 2;
    }
    std::atomic_bool cancel{WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0};
    std::atomic_bool monitorFinished{false}, monitorFailed{false};
    std::thread monitor([&] {
        while (!monitorFinished.load()) {
            const DWORD result = WaitForSingleObject(stopEvent, 100);
            if (result == WAIT_OBJECT_0) { cancel.store(true); return; }
            if (result != WAIT_TIMEOUT) { monitorFailed.store(true); cancel.store(true); return; }
        }
    });
    int exitCode = 0;
    try {
        HistoryServiceWorker worker(parser.value("config"), [](const HistoryServiceConfig &config, std::atomic_bool &cancelled) {
            HistoryStore store(config.databasePath);
            store.initialize();
            HistorySyncRunner runner(store);
            runner.run({config.cookiePath, config.exportPath, SyncSource::Scheduled}, {}, cancelled);
        });
        QTimer timer;
        bool polling = false;
        const auto poll = [&] {
            // A fetch runs a nested event loop. Do not reload configuration or
            // quit that loop from a reentrant timer; the stop thread sets cancel.
            if (polling) return;
            QScopedValueRollback<bool> guard(polling, true);
            try {
                const auto result = worker.tick(QDateTime::currentDateTime(), cancel);
                if (result == HistoryServiceWorker::TickResult::Stopped) app.quit();
                else if (result == HistoryServiceWorker::TickResult::SyncFailed)
                    errors << "Scheduled history synchronization failed; check the local sync audit and file permissions\n" << Qt::flush;
            } catch (const std::exception &e) {
                errors << QString::fromUtf8(e.what()) << '\n' << Qt::flush;
                exitCode = 2;
                app.quit();
            }
        };
        QObject::connect(&timer, &QTimer::timeout, &app, poll);
        if (!SetEvent(readyEvent)) stateError("Cannot signal history service worker startup-ready event");
        timer.start(500);
        QTimer::singleShot(0, &app, poll);
        app.exec();
    } catch (const std::exception &e) {
        errors << QString::fromUtf8(e.what()) << '\n';
        exitCode = 2;
    }
    monitorFinished.store(true);
    monitor.join();
    CloseHandle(stopEvent);
    CloseHandle(readyEvent);
    if (monitorFailed.load()) {
        errors << "History service stop event wait failed\n";
        return 4;
    }
    return exitCode;
#endif
}
