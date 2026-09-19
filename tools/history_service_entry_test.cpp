#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QDebug>
#include "core/HistoryScheduler.h"
#include "core/HistoryStore.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir(QDir::currentPath() + "/history-entry-XXXXXX");
    if (!dir.isValid()) return 1;
    int failures = 0;
    auto check = [&](bool pass, const char *label) { qInfo() << (pass ? "PASS" : "FAIL") << label; if (!pass) ++failures; };
    auto run = [&](const QStringList &args) {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "headless-entry-must-not-load-gui");
        env.insert("BBHOUSE_DATA_DIR", dir.filePath("must-not-use-default-data"));
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(HISTORY_APP_PATH), args);
        if (!process.waitForStarted(5000) || !process.waitForFinished(10000)) { process.kill(); process.waitForFinished(); return -99; }
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -98;
    };
    const QString configPath = dir.filePath("config with spaces.json");
    HistoryServiceConfig config;
    config.executablePath = QStringLiteral(HISTORY_APP_PATH);
    config.cookiePath = dir.filePath("missing-cookie.txt");
    config.databasePath = dir.filePath("history.sqlite3");
    config.exportPath = dir.filePath("export.json");
    HistoryScheduler::saveConfig(configPath, config);
    check(run({"--history-sync-once"}) == 2, "headless entry rejects missing config");
    check(run({"--history-sync-once", "--config", "relative.json"}) == 2, "headless entry rejects relative config path");
    check(run({"--history-sync-once", "--config", dir.filePath("absent.json")}) == 2, "headless entry rejects absent config");
    const QStringList args{"--history-sync-once", "--config", configPath};
    check(run(args) == 3, "credential failure exits without initializing a GUI platform");
    HistoryStore store(config.databasePath);
    const auto runs = store.listRecentRuns(10);
    check(runs.size() == 1 && runs[0].status == "failed" && runs[0].source == "scheduled" && runs[0].pages == 0,
          "headless failure writes scheduled audit to configured database");
    check(!QFile::exists(dir.filePath("must-not-use-default-data/bilibili-history.sqlite3")), "entry never falls back to app database");
    QFile cookie(config.cookiePath); if (!cookie.open(QIODevice::WriteOnly)) return 1; cookie.write("SESSDATA=offline-fixture"); cookie.close();
    QLockFile lock(config.databasePath + ".sync.lock"); lock.setStaleLockTime(0); check(lock.tryLock(), "parent holds cross-process lock");
    check(run(args) == 3 && store.listRecentRuns(10).size() == 1, "real second process refuses concurrent sync before API access");
    lock.unlock();
    config.enabled = false; HistoryScheduler::saveConfig(configPath, config);
    check(run(args) == 0 && store.listRecentRuns(10).size() == 1, "disabled config safely ignores a delayed scheduler invocation");
    return failures ? 1 : 0;
}
