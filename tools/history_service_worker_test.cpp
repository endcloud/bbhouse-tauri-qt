#include "core/HistoryServiceWorker.h"
#include "core/HistoryServiceSchedule.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <cstring>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <QProcess>
#include <windows.h>
#endif

namespace {
QString throws(const std::function<void()> &action) {
    try { action(); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
    return {};
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot write fixture");
}
QDateTime at(int day, int hour, int minute, int second = 0) {
    return QDateTime(QDate(2026, 9, day), QTime(hour, minute, second), QTimeZone::UTC);
}
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--history-service-worker") == 0)
            return runHistoryServiceWorker(argc, argv);
    QCoreApplication app(argc, argv);
    QDir project(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()); project.cdUp();
    if (!project.mkpath("build")) return 1;
    QTemporaryDir temp(project.filePath("build/history-service-worker-test-XXXXXX"));
    if (!temp.isValid()) return 1;
    int failures = 0;
    const auto check = [&](bool value, const char *name) {
        qInfo() << (value ? "PASS" : "FAIL") << name; failures += !value;
    };
    HistoryServiceConfig config;
    config.executablePath = temp.filePath("app.exe");
    config.cookiePath = temp.filePath("unused-cookie.txt");
    config.databasePath = temp.filePath("unused-history.sqlite3");
    config.exportPath = temp.filePath("unused-export.json");
    config.time = "13:45";
    const QString configPath = temp.filePath("config.json");
    HistoryScheduler::saveConfig(configPath, config);
    QMap<QString, QDate> attempts;
    using HistoryServiceSchedule::due;
    using HistoryServiceSchedule::key;
    check(due(config, at(19, 13, 45), attempts), "daily schedule is due in selected minute");
    check(!due(config, at(19, 13, 44), attempts) && !due(config, at(19, 13, 46), attempts),
          "early polling and resuming after a missed minute never catch up");
    auto disabled = config; disabled.enabled = false;
    check(!due(disabled, at(19, 13, 45), attempts), "disabled schedule never runs");
    auto weekly = config; weekly.cycle = "weekly"; weekly.weekday = 6;
    check(due(weekly, at(19, 13, 45), attempts) && !due(weekly, at(20, 13, 45), attempts),
          "weekly schedule restricts the ISO weekday");
    attempts.insert(key(config), QDate(2026, 9, 19));
    check(!due(config, at(19, 13, 45, 59), attempts) && !due(config, at(18, 13, 45), attempts)
              && due(config, at(20, 13, 45), attempts),
          "attempt high water prevents repeated minutes and date rollback but allows next day");
    auto later = config; later.time = "14:00";
    check(due(later, at(19, 14, 0), attempts) && !due(config, at(19, 13, 45), attempts),
          "changing schedule permits new time while restoring old schedule remembers attempt");
    auto sunday = config; sunday.weekday = 7;
    check(key(sunday) == key(config), "irrelevant daily weekday changes cannot replay an attempt");
    const QTimeZone newYork("America/New_York");
    if (newYork.isValid()) {
        auto dst = config; dst.time = "01:30";
        const auto first = QDateTime::fromString("2026-11-01T05:30:00Z", Qt::ISODate).toTimeZone(newYork);
        const auto second = QDateTime::fromString("2026-11-01T06:30:00Z", Qt::ISODate).toTimeZone(newYork);
        QMap<QString, QDate> dstAttempts;
        const bool firstDue = due(dst, first, dstAttempts);
        dstAttempts.insert(key(dst), first.date());
        check(firstDue && !due(dst, second, dstAttempts), "DST repeated local hour runs only once");
    }

    std::atomic_bool cancel{false};
    int runs = 0;
    using Result = HistoryServiceWorker::TickResult;
    HistoryServiceWorker worker(configPath, [&](const HistoryServiceConfig &value, std::atomic_bool &) {
        ++runs;
        check(value.databasePath == config.databasePath && QFile::exists(temp.filePath("history-service-state.json")),
              "attempt is persisted before runner receives explicit configured paths");
    });
    check(worker.tick(at(19, 13, 44), cancel) == Result::Waiting && runs == 0, "idle tick does not run");
    check(worker.tick(at(19, 13, 45), cancel) == Result::Completed && runs == 1, "due tick runs once");
    check(worker.tick(at(19, 13, 45, 30), cancel) == Result::Waiting && runs == 1, "repeated tick stays idle");
    HistoryServiceWorker restarted(configPath, [&](const auto &, auto &) { ++runs; });
    check(restarted.tick(at(19, 13, 45, 40), cancel) == Result::Waiting && runs == 1,
          "restart reads persisted attempt and cannot duplicate the current minute");
    HistoryScheduler::saveConfig(configPath, later);
    check(restarted.tick(at(19, 14, 0), cancel) == Result::Completed && runs == 2, "next tick sees modified schedule");
    HistoryScheduler::saveConfig(configPath, config);
    check(restarted.tick(at(19, 13, 45), cancel) == Result::Waiting, "restored schedule retains persisted protection");
    cancel.store(true);
    check(restarted.tick(at(20, 13, 45), cancel) == Result::Stopped && runs == 2, "stop before tick prevents a request");
    cancel.store(false);
    HistoryServiceWorker cancelled(configPath, [&](const auto &, auto &stop) { ++runs; stop.store(true); });
    check(cancelled.tick(at(20, 13, 45), cancel) == Result::Stopped && runs == 3, "runner shares cancellation flag with host monitor");
    cancel.store(false);
    HistoryServiceWorker retry(configPath, [&](const auto &, auto &) { ++runs; throw std::runtime_error("offline failure"); });
    check(retry.tick(at(21, 13, 45), cancel) == Result::SyncFailed && runs == 4,
          "sync failure preserves a live worker for the next scheduled date");
    check(retry.tick(at(21, 13, 45, 50), cancel) == Result::Waiting && runs == 4,
          "failed attempt is not retried repeatedly in the same minute");
    check(retry.tick(at(22, 13, 45), cancel) == Result::SyncFailed && runs == 5,
          "next scheduled date remains runnable after failure");
    HistoryServiceWorker *nested = nullptr;
    HistoryServiceWorker reentrant(configPath, [&](const auto &, auto &) {
        ++runs;
        write(configPath, "invalid during synchronous network event loop");
        check(nested->tick(at(23, 13, 45), cancel) == Result::Waiting,
              "nested event-loop tick neither reloads configuration nor starts another run");
        HistoryScheduler::saveConfig(configPath, config);
    });
    nested = &reentrant;
    check(reentrant.tick(at(23, 13, 45), cancel) == Result::Completed && runs == 6, "single flight survives nested polling");
    write(configPath, "{ invalid");
    check(!throws([&] { reentrant.tick(at(24, 13, 45), cancel); }).isEmpty(), "invalid live configuration fails explicitly");
    check(!throws([&] { HistoryServiceWorker invalid(configPath, [](const auto &, auto &) {}); }).isEmpty(),
          "invalid startup configuration fails before event loop");
    HistoryScheduler::saveConfig(configPath, config);
    write(worker.statePath(), "{ broken state");
    check(!throws([&] { HistoryServiceWorker invalid(configPath, [](const auto &, auto &) {}); }).isEmpty(),
          "corrupt attempt state is never treated as unattempted");
    write(worker.statePath(), "{\"version\":1,\"attempts\":{\"bad\":\"2026-09-19\"}}");
    check(!throws([&] { HistoryServiceWorker invalid(configPath, [](const auto &, auto &) {}); }).isEmpty(),
          "malformed persisted key is rejected");
    QFile::remove(worker.statePath());
    HistoryServiceWorker cannotSave(configPath, [&](const auto &, auto &) { ++runs; });
    QDir().mkpath(worker.statePath());
    check(!throws([&] { cannotSave.tick(at(24, 13, 45), cancel); }).isEmpty() && runs == 6,
          "state commit failure prevents the network request");
    check(!QFile::exists(config.cookiePath) && !QFile::exists(config.databasePath) && !QFile::exists(config.exportPath),
          "offline regression never opens credentials or history data");
#ifdef Q_OS_WIN
    // Exercise the real worker entry in an isolated child. Disabled fixture
    // prevents any API/SQLite access; only this build-local config is touched.
    QDir().rmdir(worker.statePath());
    disabled = config; disabled.enabled = false;
    HistoryScheduler::saveConfig(configPath, disabled);
    const auto child = [&](bool invalidConfig, bool invalidStop) {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        const HANDLE stop = CreateEventW(&attributes, TRUE, FALSE, nullptr);
        const HANDLE ready = CreateEventW(&attributes, TRUE, FALSE, nullptr);
        if (!stop || !ready) {
            if (stop) CloseHandle(stop);
            if (ready) CloseHandle(ready);
            check(false, "create inherited worker test events");
            return;
        }
        if (invalidConfig) write(configPath, "invalid startup configuration");
        QProcess process;
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->inheritHandles = true;
            args->flags |= CREATE_NO_WINDOW;
        });
        process.start(QCoreApplication::applicationFilePath(), {
            "--history-service-worker", "--config", configPath,
            "--stop-handle", invalidStop ? QStringLiteral("0") : QString::number(reinterpret_cast<quintptr>(stop)),
            "--ready-handle", QString::number(reinterpret_cast<quintptr>(ready))});
        const bool started = process.waitForStarted(5000);
        if (invalidConfig || invalidStop) {
            const bool finished = process.waitForFinished(5000);
            check(started && finished && process.exitCode() == 2 && WaitForSingleObject(ready, 0) == WAIT_TIMEOUT,
                  invalidStop ? "invalid stop handle exits without startup-ready handshake"
                              : "bad configuration exits without startup-ready handshake");
        } else {
            const bool acknowledged = WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0;
            SetEvent(stop);
            const bool finished = process.waitForFinished(5000);
            check(started && acknowledged && finished && process.exitCode() == 0,
                  "valid worker signals startup-ready then stops cooperatively on inherited event");
        }
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(3000); }
        CloseHandle(stop);
        CloseHandle(ready);
        HistoryScheduler::saveConfig(configPath, disabled);
    };
    child(false, false);
    child(true, false);
    child(false, true);
#endif
    qInfo() << "history service worker failures:" << failures;
    return failures ? 1 : 0;
}
