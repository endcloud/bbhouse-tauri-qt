#include "controllers/LiveController.h"

#include <QCoreApplication>
#include <QDebug>

class LiveControllerTest {
    class FakeController : public LiveController {
    public:
        QList<QPair<quint64, int>> requests;
    private:
        void startFetch(quint64 generation, int page) override {
            requests.append({generation, page});
        }
    };
    static LiveRoom room(const QString &id) {
        return {id, "9007199254740993", "Title", "UP", "face", "cover", "Area", "10"};
    }
    static void finish(FakeController &controller, const LiveFollowPage &page,
                       const QString &error = {}, bool unauthorized = false) {
        const auto request = controller.requests.last();
        controller.finishFetch(request.first, request.second, page, error, unauthorized);
    }
public:
    static int run() {
        int failures = 0;
        auto check = [&](bool ok, const char *label) {
            qInfo() << (ok ? "PASS" : "FAIL") << label;
            failures += !ok;
        };
        FakeController controller;
        controller.ensureLoaded();
        controller.ensureLoaded();
        check(controller.busy() && !controller.loaded() && controller.requests.size() == 1,
              "initial load starts once and is distinct from an empty result");
        finish(controller, {{}, true});
        finish(controller, {{}, true});
        check(controller.busy() && !controller.loaded() && controller.requests.last().second == 3,
              "filtered empty pages continue while the server has more");
        finish(controller, {{room("9007199254740995"), room("9007199254740995")}, true});
        check(!controller.busy() && controller.loaded() && controller.pool().size() == 1 &&
              controller.pool().first().toMap().value("roomId").toString() == "9007199254740995" &&
              controller.pool().first().toMap().value("mid").toString() == "9007199254740993",
              "first live page deduplicates and preserves long string identifiers");
        controller.ensureLoaded();
        check(controller.requests.size() == 3, "cached page navigation does not refetch");
        controller.loadMore();
        check(controller.requests.last().second == 4, "load more resumes at the next server page");
        finish(controller, {{room("9007199254740995")}, true});
        check(controller.busy() && controller.requests.last().second == 5,
              "duplicate-only pages continue rather than leaving a no-op load-more result");
        finish(controller, {{room("2"), room("2")}, false});
        check(controller.pool().size() == 2 && !controller.hasMore(),
              "cross-page deduplication preserves existing order and terminates correctly");
        const int completedRequests = controller.requests.size();
        controller.loadMore();
        check(controller.requests.size() == completedRequests, "completed pagination ignores load more");

        const auto oldPool = controller.pool();
        controller.refresh();
        check(controller.pool() == oldPool && controller.loaded() && controller.busy(),
              "refresh retains existing cards until successful replacement");
        finish(controller, {{}, true});
        finish(controller, {}, "fixture unauthorized", true);
        check(controller.pool() == oldPool && controller.loaded() && !controller.busy() &&
              controller.unauthorized() && controller.error() == "fixture unauthorized" && !controller.hasMore(),
              "failure after empty pages preserves the entire previous snapshot and pagination state");

        controller.refresh();
        check(!controller.unauthorized() && controller.error().isEmpty(),
              "retry clears previous error and authentication state");
        const auto stale = controller.requests.last();
        controller.refresh();
        const auto fresh = controller.requests.last();
        controller.finishFetch(stale.first, stale.second, {{room("stale")}, false}, {}, false);
        check(controller.busy() && controller.pool() == oldPool && fresh.first != stale.first,
              "stale success cannot replace cards or clear the active request busy state");
        controller.finishFetch(stale.first, stale.second, {}, "stale error", true);
        check(controller.busy() && controller.error().isEmpty() && !controller.unauthorized(),
              "stale failure cannot contaminate the latest request");
        finish(controller, {{room("3")}, true});
        check(controller.pool().size() == 1 &&
              controller.pool().first().toMap().value("roomId").toString() == "3" && controller.hasMore(),
              "successful refresh replaces rather than appends and resets pagination");
        controller.loadMore();
        finish(controller, {}, "network failure");
        check(controller.pool().size() == 1 && controller.hasMore() && controller.loaded() &&
              controller.error() == "network failure", "load-more failure preserves existing cards and retry cursor");
        controller.loadMore();
        check(controller.requests.last().second == 2, "load-more retry starts from the failed operation cursor");
        finish(controller, {{}, false});
        check(controller.pool().size() == 1 && !controller.hasMore() && controller.error().isEmpty(),
              "empty terminal page completes pagination without erasing existing cards");
        controller.refresh();
        finish(controller, {{}, false});
        check(controller.pool().isEmpty() && controller.loaded() && !controller.hasMore() && !controller.busy(),
              "successful empty refresh publishes a genuine empty state");

        FakeController bounded;
        bounded.ensureLoaded();
        for (int i = 0; i < LiveController::kMaxPagesPerOperation; ++i) finish(bounded, {{}, true});
        check(!bounded.busy() && !bounded.loaded() && bounded.pool().isEmpty() &&
              !bounded.error().isEmpty() && bounded.requests.size() == LiveController::kMaxPagesPerOperation,
              "unbounded empty pagination produces an explicit error rather than a false empty state");
        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    return LiveControllerTest::run();
}
