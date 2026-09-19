#include "core/HistoryServiceEntry.h"
#include "core/HistoryScheduler.h"
#include "core/HistoryStore.h"
#include "core/HistorySyncRunner.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QTextStream>
#include <atomic>
#include <csignal>

namespace {
std::atomic_bool cancelled{false};
void cancelRun(int) { cancelled.store(true); }
}

int runHistoryServiceEntry(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption({"history-sync-once", "Run one scheduled history synchronization."});
    parser.addOption({"config", "Absolute history service configuration path.", "path"});
    QTextStream errors(stderr);
    if (!parser.parse(app.arguments()) || !parser.positionalArguments().isEmpty() ||
        !parser.isSet("config") || !QFileInfo(parser.value("config")).isAbsolute()) {
        errors << "Invalid arguments: --history-sync-once --config <absolute path>\n";
        return 2;
    }
    HistoryServiceConfig config;
    try {
        config = HistoryScheduler::loadConfig(parser.value("config"));
    } catch (const std::exception &e) {
        errors << QString::fromUtf8(e.what()) << '\n';
        return 2;
    }
    if (!config.enabled) return 0;
    std::signal(SIGINT, cancelRun);
    std::signal(SIGTERM, cancelRun);
    try {
        HistoryStore store(config.databasePath);
        store.initialize();
        HistorySyncRunner runner(store);
        HistorySyncRunner::Request request{config.cookiePath, config.exportPath, SyncSource::Scheduled};
        runner.run(request, {}, cancelled);
        return cancelled.load() ? 4 : 0;
    } catch (const std::exception &e) {
        errors << QString::fromUtf8(e.what()) << '\n';
        return 3;
    }
}
