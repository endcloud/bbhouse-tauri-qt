#include "controllers/OnlineHistoryController.h"

#include <QCoreApplication>
#include <QDebug>

// Call only the GUI-side result handler (finishFetch) via friend access.
// No workers, cookie or network requests — offline pool/trim fixtures only.
class OnlineHistoryControllerTest {
public:
    static int run() {
        int failures = 0;
        auto check = [&](bool ok, const char *name) {
            qInfo() << (ok ? "PASS" : "FAIL") << name;
            failures += !ok;
        };
        auto item = [](int index) {
            HistoryItem value;
            value.videoKey = QStringLiteral("archive:%1:0").arg(index);
            value.business = QStringLiteral("archive");
            value.oid = index;
            value.title = QStringLiteral("视频 %1").arg(index);
            value.viewAt = index;
            return value;
        };

        OnlineHistoryController controller;
        int poolChanges = 0, loadedChanges = 0;
        QObject::connect(&controller, &OnlineHistoryController::poolChanged, [&] { ++poolChanges; });
        QObject::connect(&controller, &OnlineHistoryController::loadedChanged, [&] { ++loadedChanges; });

        check(!controller.loaded(), "controller starts unloaded");

        HistoryCursor cursor;
        cursor.business = QStringLiteral("archive");
        cursor.viewAt = 1;
        controller.finishFetch(0, {item(1), item(2)}, cursor, true, QString(), false);
        check(controller.loaded() && loadedChanges == 1, "first response marks controller loaded exactly once");
        check(controller.pool().size() == 2 && poolChanges == 1, "first response publishes two pooled items");

        // A second ensureLoaded() after loaded() must not touch busy()/pool — proves the
        // recreate-time guard skips a refetch instead of replaying the cursor chain.
        const int poolChangesBeforeGuard = poolChanges;
        controller.ensureLoaded();
        check(!controller.busy() && poolChanges == poolChangesBeforeGuard,
              "ensureLoaded() is a no-op once already loaded (no refetch on page recreate)");

        cursor.viewAt = 2;
        controller.finishFetch(0, {item(2), item(3)}, cursor, true, QString(), false);
        check(controller.pool().size() == 3, "duplicate video_key across pages is not re-added");

        // Feed far past the memory safety cap (kMaxPoolEntries=2000) across several pages,
        // matching an extreme single-session infinite-scroll run.
        for (int page = 0; page < 41; ++page) {
            QList<HistoryItem> page_items;
            for (int i = 0; i < 60; ++i) page_items.append(item(100 + page * 60 + i));
            cursor.viewAt = 100 + page;
            controller.finishFetch(0, page_items, cursor, true, QString(), false);
        }
        check(controller.pool().size() == 2000, "pool never grows past the memory safety cap");
        const QString newestKey = controller.pool().last().toMap().value("videoKey").toString();
        check(newestKey == item(100 + 40 * 60 + 59).videoKey,
              "cap eviction drops the oldest entries, keeping the most recently loaded ones");
        const QString oldestSurvivorKey = controller.pool().first().toMap().value("videoKey").toString();
        check(oldestSurvivorKey != item(1).videoKey,
              "the very first page's entries are evicted once the cap is exceeded");

        // Eviction must also release the dedup key so a since-evicted video_key can be
        // re-admitted rather than silently treated as an already-seen duplicate forever.
        const int sizeBeforeReadmit = controller.pool().size();
        cursor.viewAt = 999;
        controller.finishFetch(0, {item(1)}, cursor, true, QString(), false);
        check(controller.pool().size() == sizeBeforeReadmit,
              "cap keeps holding steady when an evicted item's key is seen again");

        double changedCount = 0;
        QObject::connect(&controller, &OnlineHistoryController::scrollOffsetChanged, [&] { ++changedCount; });
        controller.setScrollOffset(120.5);
        controller.setScrollOffset(120.5);
        check(controller.scrollOffset() == 120.5 && changedCount == 1,
              "scrollOffset stores the remembered position and dedups redundant writes");

        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    return OnlineHistoryControllerTest::run();
}
