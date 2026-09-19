#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "core/HistorySyncRunner.h"
#include "core/HistoryApi.h"

class FakeRunner : public HistorySyncRunner {
public:
    explicit FakeRunner(HistoryStore &store) : HistorySyncRunner(store) {}
    QList<BilibiliHistoryPage> pages;
    QList<HistoryCursor> requests;
    bool failFetch = false, cancelOnWait = false;
    int waits = 0;
    bool realWait(std::atomic_bool &cancel) { return HistorySyncRunner::waitForNextPage(cancel); }
protected:
    BilibiliHistoryPage fetchPage(const QString &, const HistoryCursor &cursor) override {
        requests.append(cursor);
        if (failFetch || pages.isEmpty()) throw std::runtime_error("fixture API failure");
        return pages.takeFirst();
    }
    bool waitForNextPage(std::atomic_bool &cancel) override {
        ++waits;
        if (cancelOnWait) cancel.store(true);
        return !cancel.load();
    }
};

HistoryItem item(qint64 time, const QString &title, int progress = 20) {
    HistoryItem result;
    result.videoKey = "archive:9007199254:0";
    result.business = "archive"; result.oid = 9007199254LL;
    result.title = title; result.viewAt = time; result.progress = progress;
    result.subtitle = ""; result.coverUrl = ""; result.authorName = "fixture";
    result.badge = ""; result.linkUrl = ""; result.rawJson = "{}";
    return result;
}
BilibiliHistoryPage page(const HistoryItem &value, int cursor) {
    return {{cursor, value.viewAt, "archive", 30}, {value}, "{}"};
}
QString throws(const std::function<void()> &fn) {
    try { fn(); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
    return {};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir(QDir::currentPath() + "/history-sync-XXXXXX");
    if (!dir.isValid()) return 1;
    int failures = 0;
    auto check = [&](bool pass, const char *label) { qInfo() << (pass ? "PASS" : "FAIL") << label; if (!pass) ++failures; };
    QFile cookie(dir.filePath("cookie.txt")); if (!cookie.open(QIODevice::WriteOnly)) return 1; cookie.write("SESSDATA=offline-fixture"); cookie.close();
    HistoryStore store(dir.filePath("history.sqlite3")); store.initialize();
    const HistorySyncRunner::Request request{cookie.fileName(), dir.filePath("export.json"), SyncSource::Scheduled};
    std::atomic_bool cancel{false};
    FakeRunner first(store);
    first.pages = {page(item(200, "newer"), 1), page(item(100, "older"), 2), {}};
    const auto result = first.run(request, {}, cancel);
    check(result.pages == 3 && result.inserted == 2 && result.stoppedByEmptyPage, "cursor traversal reaches empty page");
    check(first.waits == 2 && first.requests[1].max == 1 && first.requests[2].max == 2, "every nonempty page waits and forwards server cursor");
    auto items = store.loadItems();
    check(items.size() == 1 && items[0].title == "newer" && items[0].viewAt == 200 && items[0].viewCount == 2,
          "older page preserves newer metadata while appending independent event");
    FakeRunner repeated(store);
    repeated.pages = {page(item(200, "changed same event", 99), 1), {}};
    const auto duplicate = repeated.run(request, {}, cancel);
    items = store.loadItems();
    check(duplicate.inserted == 0 && duplicate.updated == 1 && items[0].progress == 20 && items[0].viewRecords[0].progress == 20,
          "repeated observation does not overwrite recorded event or latest snapshot");
    FakeRunner newer(store); newer.pages = {page(item(300, "latest", 55), 3), {}};
    newer.run(request, {}, cancel); items = store.loadItems();
    check(items[0].viewCount == 3 && items[0].viewRecords.size() == 3 && items[0].title == "latest",
          "new watch increments count without deleting old timestamps");
    check(QFile::exists(request.exportPath) && store.listRecentRuns(1)[0].source == SyncSource::Scheduled,
          "scheduled audit and atomic export exist");
    FakeRunner loop(store);
    loop.pages = {page(item(400, "a"), 8), page(item(400, "b"), 9), page(item(400, "c"), 8)};
    check(!throws([&]{ loop.run(request, {}, cancel); }).isEmpty() && loop.requests.size() == 3,
          "multi-page cursor cycle fails without a fourth request");
    check(store.listRecentRuns(1)[0].status == "failed" && store.countRecords() == 4,
          "cursor failure is audited and committed pages survive");
    FakeRunner bad(store); auto missing = page(item(500, "missing cursor"), 0); missing.cursor.business.clear(); bad.pages = {missing};
    check(!throws([&]{ bad.run(request, {}, cancel); }).isEmpty(), "nonempty invalid cursor cannot silently truncate success");
    FakeRunner cancelled(store); cancelled.pages = {page(item(600, "cancel"), 1)}; cancelled.cancelOnWait = true;
    cancelled.run(request, {}, cancel);
    check(cancelled.requests.size() == 1 && store.listRecentRuns(1)[0].status == "cancelled", "cancel during wait prevents next API call");
    cancel.store(false);
    QLockFile lock(store.databasePath() + ".sync.lock"); lock.setStaleLockTime(0); check(lock.tryLock(), "fixture holds sync lock");
    FakeRunner locked(store);
    check(!throws([&]{ locked.run(request, {}, cancel); }).isEmpty() && locked.requests.isEmpty(), "same database lock rejects overlapping crawl");
    lock.unlock();
    FakeRunner noCookie(store); auto invalidRequest = request; invalidRequest.cookiePath = dir.filePath("missing-cookie.txt");
    check(!throws([&]{ noCookie.run(invalidRequest, {}, cancel); }).isEmpty() && noCookie.requests.isEmpty() && store.listRecentRuns(1)[0].status == "failed",
          "missing credential is audited without API request");
    FakeRunner wait(store); QElapsedTimer timer; timer.start();
    check(wait.realWait(cancel) && timer.elapsed() >= 1000, "production inter-page wait is at least one second");
    cancel.store(true); timer.restart();
    check(!wait.realWait(cancel) && timer.elapsed() < 100, "production wait reacts promptly to cancellation");
    check(QSqlDatabase::connectionNames().isEmpty(), "storage releases thread-owned SQL connections");
    // A page with a valid first row and invalid second row must leave no partial page.
    const auto before = store.countRecords();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "fixture-trigger");
        db.setDatabaseName(store.databasePath());
        if (!db.open()) return 1;
        QSqlQuery query(db);
        if (!query.exec("CREATE TRIGGER fixture_fail BEFORE INSERT ON videos WHEN NEW.title='invalid' BEGIN SELECT RAISE(ABORT, 'fixture failure'); END")) return 1;
    }
    QSqlDatabase::removeDatabase("fixture-trigger");
    auto badPage = page(item(700, "must roll back"), 1);
    badPage.items.append(item(701, "invalid"));
    const auto runId = store.startSyncRun(SyncSource::Manual);
    check(!throws([&]{ store.upsertItems(runId, 1, badPage); }).isEmpty() && store.countRecords() == before,
          "failed page rolls back inserted records");
    return failures ? 1 : 0;
}
