#include "controllers/PopularController.h"

#include <QCoreApplication>
#include <QDebug>

class PopularControllerTest {
    struct Request { quint64 generation; QString key; int page; int rid; int week; };
    class FakeController : public PopularController {
    public:
        QList<Request> requests;
        QList<quint64> periodRequests;
    private:
        void startFetch(quint64 generation, const QString &key, int page) override {
            requests.append({generation, key, page, rankingRid(), weeklyNumber()});
        }
        void startPeriodsFetch(quint64 generation) override { periodRequests.append(generation); }
    };
    static QVariantMap item(const QString &id) {
        return {{"videoKey", id}, {"title", id}, {"mid", QStringLiteral("9007199254740993")}};
    }
    static QVariantMap period(int number) {
        return {{"number", number}, {"label", QString::number(number)}};
    }
    static void finish(FakeController &controller, const PopularPage &result, const QString &error = {}) {
        const auto request = controller.requests.last();
        controller.finishFetch(request.generation, request.key, request.page, result, error);
    }
    static void finishPeriods(FakeController &controller, const QVariantList &periods, const QString &error = {}) {
        controller.finishPeriodsFetch(controller.periodRequests.last(), periods, error);
    }
    static QString firstId(const FakeController &controller) {
        return controller.pool().isEmpty() ? QString() : controller.pool().first().toMap().value("videoKey").toString();
    }
public:
    static int run() {
        int failures = 0;
        auto check = [&](bool passed, const char *message) {
            qInfo() << (passed ? "PASS" : "FAIL") << message;
            failures += !passed;
        };

        FakeController popular;
        popular.ensureLoaded();
        popular.ensureLoaded();
        check(popular.busy() && !popular.loaded() && popular.requests.size() == 1 &&
              popular.requests.last().page == 1, "initial request is idempotent and starts on page one");
        finish(popular, {{item("a"), item("a"), item("b")}, true, "popular description"});
        check(popular.loaded() && !popular.busy() && popular.pool().size() == 2 && popular.hasMore() &&
              popular.description() == "popular description", "initial snapshot deduplicates and exposes description");
        popular.loadMore();
        check(popular.requests.last().page == 2, "popular continuation requests next page");
        finish(popular, {{item("b")}, true, {}});
        check(popular.busy() && popular.requests.last().page == 3,
              "duplicate-only continuation advances until it finds new items");
        finish(popular, {{item("c"), item("a")}, false, {}});
        check(popular.pool().size() == 3 && !popular.hasMore(), "cross-page duplicates removed and server end respected");
        const int finishedCount = popular.requests.size();
        popular.loadMore();
        check(popular.requests.size() == finishedCount, "terminal pagination cannot fetch extra pages");

        const QVariantList oldPool = popular.pool();
        popular.refresh();
        const Request staleRefresh = popular.requests.last();
        popular.refresh();
        popular.finishFetch(staleRefresh.generation, staleRefresh.key, staleRefresh.page,
                            {{item("stale")}, true, {}}, {});
        check(popular.busy() && popular.pool() == oldPool,
              "outdated refresh cannot replace old cards or release latest busy state");
        finish(popular, {}, "offline");
        check(popular.loaded() && popular.pool() == oldPool && !popular.hasMore() && popular.error() == "offline",
              "failed refresh preserves cards and pagination state");
        popular.refresh();
        check(popular.error().isEmpty(), "retry clears previous request error");
        finish(popular, {{item("d")}, true, {}});
        popular.loadMore();
        finish(popular, {}, "page unavailable");
        popular.loadMore();
        check(popular.requests.last().page == 2 && firstId(popular) == "d",
              "failed continuation retries original cursor while retaining old cards");
        finish(popular, {{}, false, {}});
        check(popular.pool().size() == 1 && !popular.hasMore(), "empty final continuation retains existing cards");

        popular.selectRanking(0);
        const Request oldRanking = popular.requests.last();
        popular.selectRanking(3);
        const Request musicRanking = popular.requests.last();
        popular.finishFetch(oldRanking.generation, oldRanking.key, oldRanking.page, {{item("wrong")}, false, {}}, {});
        check(popular.busy() && popular.pool().isEmpty() && musicRanking.rid == 3,
              "rapid ranking selection ignores earlier ranking response");
        finish(popular, {{item("rank3")}, false, {}});
        popular.selectRanking(-1);
        check(popular.requests.last().rid == -1 && popular.requests.last().key == "ranking:-1",
              "music popularity uses a separate ranking selection");
        finish(popular, {{item("music")}, false, "latest music period"});
        popular.selectRanking(3);
        check(firstId(popular) == "rank3" && !popular.busy(), "returning to a loaded ranking uses its own cached snapshot");
        popular.selectTab("popular");
        check(firstId(popular) == "d" && !popular.hasMore(), "popular cache retains its own pagination after visiting ranking");
        popular.selectTab("precious");
        finish(popular, {{item("precious")}, false, {}});
        popular.loadMore();
        check(firstId(popular) == "precious" && !popular.hasMore() && !popular.busy(),
              "precious is a complete non-paginated list");

        FakeController weekly;
        weekly.selectTab("weekly");
        weekly.ensureLoaded();
        check(weekly.periodsBusy() && weekly.periodRequests.size() == 1 && weekly.requests.isEmpty(),
              "weekly waits for its directory before requesting a nonzero period");
        finishPeriods(weekly, {period(8), period(10), period(0), period(9), period(10)});
        check(weekly.periodsLoaded() && weekly.weeklyNumber() == 10 && weekly.weeklyPeriods().size() == 3 &&
              weekly.weeklyPeriods().first().toMap().value("number").toInt() == 10 && weekly.requests.last().week == 10,
              "period directory filters invalid duplicates and defaults to newest sorted number");
        const Request oldWeek = weekly.requests.last();
        weekly.selectWeek(8);
        check(weekly.requests.last().week == 8 && weekly.requests.last().key == "weekly:8",
              "historical week parameter is sent to the matching request");
        weekly.finishFetch(oldWeek.generation, oldWeek.key, oldWeek.page, {{item("week10")}, false, {}}, {});
        check(weekly.busy() && weekly.pool().isEmpty(), "rapid week selection ignores the older period success");
        weekly.finishFetch(oldWeek.generation, oldWeek.key, oldWeek.page, {}, "old week failure");
        check(weekly.busy() && weekly.error().isEmpty(), "older period error cannot contaminate current selection");
        finish(weekly, {{item("week8")}, false, "week eight"});
        weekly.selectWeek(9);
        finish(weekly, {{item("week9")}, false, {}});
        const int fetchedWeeks = weekly.requests.size();
        weekly.selectWeek(8);
        check(firstId(weekly) == "week8" && weekly.requests.size() == fetchedWeeks,
              "historical weeks keep separate reusable snapshots");
        weekly.refresh();
        finish(weekly, {}, "week unavailable");
        check(firstId(weekly) == "week8" && weekly.description() == "week eight" && weekly.loaded(),
              "failed week refresh retains the selected period snapshot");
        weekly.refresh();
        finish(weekly, {{item("week8-new")}, false, {}});
        check(firstId(weekly) == "week8-new" && weekly.error().isEmpty(), "week refresh can retry successfully");

        weekly.refreshPeriods();
        finishPeriods(weekly, {period(11), period(10), period(9), period(8)});
        check(weekly.weeklyNumber() == 8 && firstId(weekly) == "week8-new" && !weekly.busy(),
              "directory refresh preserves a still valid historical selection");
        weekly.refreshPeriods();
        const quint64 staleDirectory = weekly.periodRequests.last();
        weekly.refreshPeriods();
        weekly.finishPeriodsFetch(staleDirectory, {period(99)}, {});
        check(weekly.periodsBusy() && weekly.weeklyNumber() == 8,
              "stale directory cannot replace the active directory generation");
        finishPeriods(weekly, {}, "directory offline");
        check(weekly.periodsError() == "directory offline" && weekly.weeklyNumber() == 8 &&
              firstId(weekly) == "week8-new", "directory failure leaves cached periods and cards independently usable");
        weekly.refreshPeriods();
        finishPeriods(weekly, {period(11), period(10)});
        check(weekly.weeklyNumber() == 11 && weekly.requests.last().week == 11,
              "removed historical selection falls back to newest available period");
        finish(weekly, {{item("week11")}, false, {}});
        weekly.refreshPeriods();
        finishPeriods(weekly, {});
        check(weekly.periodsLoaded() && weekly.weeklyNumber() == 0 && weekly.pool().isEmpty() && !weekly.busy(),
              "successful empty directory clears invalid selection without requesting period zero");
        const int emptyDirectoryRequests = weekly.requests.size();
        weekly.selectWeek(0);
        weekly.selectWeek(11);
        weekly.ensureLoaded();
        check(weekly.requests.size() == emptyDirectoryRequests, "empty directory never emits fabricated period requests");

        FakeController failedDirectory;
        failedDirectory.selectTab("weekly");
        finishPeriods(failedDirectory, {}, "temporary failure");
        check(!failedDirectory.periodsLoaded() && !failedDirectory.periodsBusy() && failedDirectory.requests.isEmpty(),
              "initial directory error is distinct from a successful empty directory");
        failedDirectory.refreshPeriods();
        finishPeriods(failedDirectory, {period(12)});
        check(failedDirectory.periodsError().isEmpty() && failedDirectory.requests.last().week == 12,
              "directory can retry independently after an initial failure");
        failedDirectory.selectTab("popular");
        const Request currentPopular = failedDirectory.requests.last();
        failedDirectory.refreshPeriods();
        finishPeriods(failedDirectory, {period(13)});
        check(failedDirectory.activeTab() == "popular" && failedDirectory.requests.last().generation == currentPopular.generation &&
              failedDirectory.busy(), "background directory changes do not interrupt another active tab");

        FakeController bounded;
        bounded.ensureLoaded();
        for (int i = 0; i < PopularController::kMaxPagesPerOperation; ++i) finish(bounded, {{}, true, {}});
        check(!bounded.busy() && !bounded.loaded() && !bounded.error().isEmpty() &&
              bounded.requests.size() == PopularController::kMaxPagesPerOperation,
              "endless empty popular pages terminate with explicit retryable error");
        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    return PopularControllerTest::run();
}
