#include "controllers/BangumiController.h"
#include "core/ApiErrors.h"
#include "preferences/AppPreferences.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

class RegionalBangumiTest {
    class FakeController : public BangumiController {
    public:
        QList<QPair<int, int>> listRequests;
        QList<SeasonRequest> detailRequests;
    private:
        void startFetch(int bucket, int page) override { listRequests.append({bucket, page}); }
        void startSeasonFetch() override {
            if (!queuedSeason_) return;
            detailRequests.append(*queuedSeason_);
            queuedSeason_.reset();
            seasonBusy_ = true;
        }
    };
public:
    static int run() {
        int failures = 0;
        auto check = [&](bool ok, const char *label) {
            qInfo() << (ok ? "PASS" : "FAIL") << label;
            failures += !ok;
        };
        check(BangumiApi::isRegionalTitle("劇名（僅限港澳台地區）") &&
              BangumiApi::isRegionalTitle("剧名(仅限港澳台地区)") &&
              BangumiApi::isRegionalTitle("Title（ 僅限港澳臺地區 ）"),
              "traditional simplified and bracket variants identify regional editions");
        check(!BangumiApi::isRegionalTitle("港澳台旅行") &&
              !BangumiApi::isRegionalTitle("普通番剧") &&
              !BangumiApi::isRegionalTitle("Title（僅限台灣地區）"),
              "ordinary titles and other territory labels are not opted in");
        auto ordinary = BangumiApi::parseSeason({{"season_id", 42}, {"title", "Ordinary"}});
        auto regional = BangumiApi::parseSeason({{"season_id", 43}, {"title", "Alpha（僅限港澳台地區）"}});
        check(!ordinary.regional && regional.regional &&
              BangumiController::toItemMap(regional).value("regional").toBool() &&
              !BangumiController::toItemMap(ordinary).value("regional").toBool(),
              "season parsing and card maps carry only explicit regional metadata");
        int calls = 0;
        auto scanned = BangumiApi::scanRegionalFollowList([&](int pn) {
            ++calls;
            BangumiApi::FollowListPage page;
            page.total = 61;
            if (pn == 1) page.items = {ordinary};
            if (pn == 2) page.items = {regional, regional};
            if (pn == 3) {
                auto second = regional;
                second.seasonId = 44;
                page.items = {regional, second};
            }
            return page;
        });
        check(calls == 3 && scanned.size() == 2 && scanned.last().seasonId == 44,
              "full follow-list scan finds later pages and deduplicates across pages");
        bool partialRejected = false;
        try {
            BangumiApi::scanRegionalFollowList([&](int pn) {
                if (pn == 2) throw ApiError(-1, "offline fixture failure");
                return BangumiApi::FollowListPage{{regional}, 31};
            });
        } catch (const ApiError &) { partialRejected = true; }
        check(partialRejected, "failed scan never returns a misleading partial snapshot");
        bool emptyRejected = false, hugeRejected = false;
        try { BangumiApi::scanRegionalFollowList([](int) { return BangumiApi::FollowListPage{{}, 1}; }); }
        catch (const ApiError &) { emptyRejected = true; }
        try { BangumiApi::scanRegionalFollowList([](int) {
            return BangumiApi::FollowListPage{{}, qint64(BangumiApi::kMaxRegionalScanPages) * 30 + 1};
        }); } catch (const ApiError &) { hugeRejected = true; }
        check(emptyRejected && hugeRejected, "incomplete and unbounded scans produce explicit errors");
        QList<BangumiApi::BangumiSeason> many;
        for (int i = 0; i < 65; ++i) {
            auto item = regional;
            item.seasonId = 100 + i;
            item.title = (i < 31 ? "Alpha" : "Beta") + QString::number(i) + "（僅限港澳台地區）";
            many.append(item);
        }
        auto page = BangumiApi::regionalPage(many, " beta ", 2);
        check(page.total == 34 && page.items.size() == 4 && page.items.first().seasonId == 161,
              "regional search filters full snapshot before 30-item pagination");
        check(BangumiApi::regionalPage(many, "missing", 99).items.isEmpty() &&
              BangumiApi::regionalPage(many, "Alpha", 99).items.size() == 1,
              "empty search and out-of-range pages are bounded");

        FakeController controller;
        int listErrors = 0, detailErrors = 0, ready = 0;
        QObject::connect(&controller, &BangumiController::loadFailed, [&] { ++listErrors; });
        QObject::connect(&controller, &BangumiController::errorOccurred, [&] { ++detailErrors; });
        QObject::connect(&controller, &BangumiController::seasonDetailReady, [&] { ++ready; });
        controller.ensureCurrentBucketLoaded();
        controller.setBucket(3);
        controller.finishFetch(1, 1, {}, 0, "stale bucket failure", true);
        check(listErrors == 0 && !controller.unauthorized() && controller.listRequests.last().first == 3,
              "switching tabs while loading fetches new bucket and hides obsolete errors");
        controller.regionalItems_ = many;
        controller.finishFetch(3, 1, {}, many.size(), {}, false);
        controller.loadPage(3);
        check(controller.currentPage() == 3 && controller.pageItems().size() == 5 &&
              controller.listRequests.size() == 2, "regional page navigation uses snapshot without API requests");
        controller.setRegionalSearch("Beta");
        check(controller.currentPage() == 1 && controller.currentTotal() == 34,
              "new regional query resets page and counts matching items only");
        const auto oldCards = controller.pageItems();
        controller.refresh();
        controller.finishFetch(3, 1, {}, 0, "refresh failed", false);
        check(controller.pageItems() == oldCards && listErrors == 1,
              "failed regional refresh preserves existing cards");
        controller.seasonDetail(43, true);
        const auto first = controller.detailRequests.last();
        controller.seasonDetail(44, false);
        controller.finishSeasonFetch(first, {}, "old request failure");
        check(detailErrors == 0 && controller.detailRequests.size() == 2 &&
              controller.detailRequests.last().proxy.type() == QNetworkProxy::NoProxy,
              "latest detail intent wins and ordinary details capture direct connection");
        const QVariantMap detail{{"seasonId", 44}, {"regional", false}};
        controller.finishSeasonFetch(controller.detailRequests.last(), detail, {});
        check(ready == 1 && controller.seasonDetail(44, false) == detail,
              "latest successful detail is cached within its request scope");
        controller.seasonDetail(44, true);
        check(controller.detailRequests.size() == 3,
              "regional and ordinary details do not share cached results");
        const auto beforeConfig = controller.detailRequests.last();
        auto *preferences = AppPreferences::instance();
        check(preferences->saveProxySettings("http", "localhost", 7901, "fixture", "session secret"),
              "fixture proxy settings validate");
        controller.finishSeasonFetch(beforeConfig, {{"seasonId", 44}, {"regional", true}}, {});
        check(ready == 1 && controller.detailRequests.size() == 4 && controller.seasonCache_.isEmpty() &&
              controller.detailRequests.last().proxy.port() == 7901,
              "proxy change rejects old cache result and restarts latest regional request with new snapshot");
        controller.finishSeasonFetch(controller.detailRequests.last(), {{"seasonId", 44}, {"regional", true}}, {});
        check(ready == 2, "new proxy detail result is published");
        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory(QDir::currentPath() + "/regional-bangumi-test-XXXXXX");
    if (!directory.isValid()) return 1;
    QCoreApplication::setOrganizationName("bbhouse-offline-tests");
    QCoreApplication::setApplicationName("regional-bangumi");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    qputenv("BBHOUSE_DATA_DIR", directory.path().toUtf8());
    return RegionalBangumiTest::run();
}
