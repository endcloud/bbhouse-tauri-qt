#include <QCoreApplication>
#include <memory>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include "controllers/HistoryController.h"
#include "core/HistoryStore.h"
#include "core/HistoryApi.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir(QDir::currentPath() + "/history-controller-XXXXXX");
    if (!dir.isValid()) return 1;
    const QString dataPath = dir.filePath("data");
    qputenv("BBHOUSE_DATA_DIR", dataPath.toUtf8());
    QFile blocker(dataPath); if (!blocker.open(QIODevice::WriteOnly)) return 1; blocker.close();
    int failures = 0;
    auto check = [&](bool pass, const char *label) { qInfo() << (pass ? "PASS" : "FAIL") << label; if (!pass) ++failures; };
    auto settle = [&](const std::function<bool()> &done) {
        QElapsedTimer timer; timer.start();
        while (!done() && timer.elapsed() < 5000) { app.processEvents(); QThread::msleep(5); }
        app.processEvents();
        return done();
    };
    HistoryController controller;
    int failed = 0, pageLoaded = 0, lastPage = 0;
    QVariantList items;
    QObject::connect(&controller, &HistoryController::loadFailed, [&]{ ++failed; });
    QObject::connect(&controller, &HistoryController::pageLoaded, [&](int page, QVariantList rows, int) {
        ++pageLoaded; lastPage = page; items = rows;
    });
    check(settle([&]{ return failed > 0; }) && !controller.ready(), "initial database error is caught and reported");
    check(QFile::remove(dataPath) && QDir().mkpath(dataPath), "fixture repairs inaccessible data directory");
    controller.loadPage(1);
    check(settle([&]{ return !controller.loading(); }) && controller.ready() && controller.loadError().isEmpty(),
          "failed initialization can be retried without restarting");
    if (!controller.ready() || !controller.loadError().isEmpty()) {
        qWarning() << "Fixture recovery failed:" << controller.loadError();
        QThreadPool::globalInstance()->waitForDone();
        return 1;
    }
    HistoryStore store(dataPath + "/bilibili-history.sqlite3");
    const auto id = store.startSyncRun("manual");
    BilibiliHistoryPage entries;
    for (int i = 1; i <= 31; ++i) {
        entries.items.append(HistoryApi::parseItem(QJsonObject{{"title", QString("fixture %1").arg(i)},
            {"view_at", 1000 + i}, {"progress", 15}, {"author_mid", "9007199254740993"},
            {"history", QJsonObject{{"oid", 10000 + i}, {"business", "archive"}}}}));
    }
    store.upsertItems(id, 1, entries);
    const int countBefore = pageLoaded;
    controller.loadPage(1); controller.loadPage(2);
    check(settle([&]{ return !controller.loading(); }) && pageLoaded == countBefore + 1 && lastPage == 2 && items.size() == 1,
          "rapid page requests ignore stale responses");
    const auto row = items.value(0).toMap();
    const auto views = row.value("recordedViews").toList();
    check(row.value("locallyRecorded").toBool() && row.value("viewCount").toInt() == 1 && views.size() == 1 &&
          views[0].toMap().value("progress").toInt() == 15 && row.value("authorMid").toString() == "9007199254740993",
          "QML receives saved badge data, watch progress and exact long author ID");
    controller.loadPage(100);
    check(settle([&]{ return !controller.loading(); }) && lastPage == 2, "out-of-range pages clamp to available local history");
    check(QFile::remove(store.databasePath()) && QDir().mkpath(store.databasePath()), "fixture blocks database file for read failure");
    const int retained = pageLoaded;
    controller.loadPage(1);
    check(settle([&]{ return !controller.loading(); }) && !controller.loadError().isEmpty() && pageLoaded == retained && controller.videoCount() == 31,
          "read failure retains prior page and counts instead of publishing empty success");
    // Constructor initialization and page reads may still be running here.
    // Destruction must join before store/atomics are destroyed and cancel queued delivery.
    int destroyedCallbacks = 0;
    for (int i = 0; i < 8; ++i) {
        auto shortLived = std::make_unique<HistoryController>();
        QObject::connect(shortLived.get(), &HistoryController::pageLoaded, &app,
                         [&](int, QVariantList, int) { ++destroyedCallbacks; });
        shortLived->loadPage(1);
        shortLived.reset();
    }
    QThreadPool::globalInstance()->waitForDone(); app.processEvents();
    check(destroyedCallbacks == 0, "destroyed history controllers cancel queued delivery safely");
    return failures ? 1 : 0;
}
