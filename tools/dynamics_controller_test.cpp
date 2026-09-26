#include "controllers/DynamicsController.h"
#include "core/DynamicApi.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>

// Call only the GUI-side result handlers. No workers, cookie, network or DB.
class DynamicsControllerTest {
public:
    static int run() {
        int failures = 0;
        auto check = [&](bool ok, const char *name) {
            qInfo() << (ok ? "PASS" : "FAIL") << name;
            failures += !ok;
        };
        auto video = [](qint64 aid, qint64 time) {
            DynamicFeedItem item;
            item.category = DynamicCategory::Video;
            item.id = QString::number(aid);
            item.aid = aid;
            item.pubTs = time;
            item.title = QStringLiteral("测试视频 %1").arg(aid);
            return item;
        };
        const auto forwarded = DynamicApi::parseItem(QJsonDocument::fromJson(R"({
            "id_str":"1", "type":"DYNAMIC_TYPE_FORWARD",
            "modules":{"module_author":{"name":"Forwarder","mid":9007199254740993,
                "face":"//i0.hdslb.com/forwarder.jpg"}},
            "orig":{"modules":{"module_author":{"name":"Original","mid":42},
                "module_dynamic":{"major":{"type":"MAJOR_TYPE_ARCHIVE",
                    "archive":{"aid":100,"title":"Video"}}}}}
        })").object());
        const auto forwardedCard = DynamicsController::toItemMap(forwarded);
        check(forwardedCard.value("authorName") == "Forwarder" &&
              forwardedCard.value("authorMid").toString() == "9007199254740993" &&
              forwardedCard.value("faceUrl") == "https://i0.hdslb.com/forwarder.jpg",
              "forwarded card name, full-width mid and avatar refer to the displayed author");
        DynamicsController controller;
        controller.setCategoryFilter("video");
        controller.refreshPending_ = true;
        controller.finishLoad(0, {video(3000000001LL, 30), video(2, 20), video(3, 10)}, "", false, "", false);
        controller.zoneTimer_.stop();
        int resets = 0, updates = 0, inserted = 0, zones = 0, emptyPublications = 0;
        auto *model = controller.cardModel();
        QObject::connect(model, &QAbstractItemModel::modelReset, [&] { ++resets; });
        QObject::connect(model, &QAbstractItemModel::dataChanged, [&] { ++updates; });
        QObject::connect(model, &QAbstractItemModel::rowsInserted, [&] { ++inserted; });
        QObject::connect(&controller, &DynamicsController::zoneNamesChanged, [&] { ++zones; });
        QObject::connect(&controller, &DynamicsController::itemsChanged, [&] {
            if (controller.items().isEmpty()) ++emptyPublications;
        });
        auto apply = [&](qint64 aid, const QString &zone) {
            controller.pendingZoneAids_.removeAll(aid);
            controller.zoneInFlight_ = true;
            controller.applyZoneResult(aid, zone);
            controller.zoneTimer_.stop();
        };
        check(controller.zoneGateActive(), "pending view lookups keep empty-state gate active");
        apply(3000000001LL, "游戏");
        check(model->rowCount() == 3 && updates == 1 && resets == 0 && inserted == 0,
              "one view response updates only one existing card without resetting projection");
        check(zones == 1 && controller.zoneNames() == QStringList{"游戏"}, "new zone published once");
        apply(2, "游戏");
        check(updates == 2 && zones == 1, "repeated zone does not rebuild filter options");
        controller.setZoneFilter("游戏");
        check(model->rowCount() == 2, "selected zone excludes unresolved video");
        const int before = inserted;
        apply(3, "游戏");
        check(model->rowCount() == 3 && inserted == before + 1 && resets == 0,
              "matching resolution incrementally inserts only the new card");
        check(!controller.zoneGateActive(), "last response releases resolution gate");
        controller.setSearchText("3000000001");
        check(model->rowCount() == 1, "search and zone intersection preserved for large aid");
        const int changes = updates;
        apply(99, "未知分区");
        check(model->rowCount() == 1 && updates == changes && zones == 1,
              "response for absent video is cached but cannot alter current cards or chips");
        controller.setSearchText("");
        controller.setZoneFilter("");
        controller.refreshPending_ = true;
        controller.poolAids_.clear();
        controller.poolIds_.clear();
        controller.aidOccurrences_.clear();
        controller.finishLoad(0, {video(2, 20), video(4, 40)}, "", false, "", false);
        controller.zoneTimer_.stop();
        check(emptyPublications == 0 && model->rowCount() == 2 && resets == 0,
              "refresh replacement publishes no temporary empty projection");
        apply(4, "知识");
        check(controller.zoneNames() == QStringList({"游戏", "知识"}),
              "newly discovered zone appends without reordering existing chips");
        controller.refreshPending_ = true;
        controller.finishLoad(0, {}, "", false, "", false);
        controller.zoneTimer_.stop();
        check(model->rowCount() == 0 && controller.zoneNames().isEmpty() && emptyPublications == 1,
              "genuinely empty refresh clears cards and obsolete zones once");
        controller.finishLoad(0, {video(100, 1)}, "next", true, {}, false);
        controller.zoneTimer_.stop();
        controller.setSearchText("fixture");
        controller.setScrollOffset(480);
        controller.scrollAnchor_ = {{"key", "100"}};
        controller.releasePageCache();
        check(controller.searchText().isEmpty() && controller.scrollOffset() == 0 &&
                      controller.scrollAnchor_.isEmpty(),
              "page release drops remembered search and scroll with the discarded feed");
        controller.busy_ = true;
        controller.finishLoad(0, {video(101, 2)}, "stale", true, {}, false);
        check(controller.pool().isEmpty() && controller.items().isEmpty() && model->rowCount() == 0 &&
              controller.zoneCache_.isEmpty() && !controller.zoneTimer_.isActive() && controller.busy(),
              "page release frees cards and zone work; stale feed cannot refill cache or clear new busy");

        // Real feed paging moves backward in publication time. Admission order must
        // not depend on publication sort or a video's earlier representative.
        DynamicsController capController;
        capController.refreshPending_ = true;
        qint64 nextAid = 1;
        for (int round = 0; round < 35; ++round) {
            QList<DynamicFeedItem> pageItems;
            for (int i = 0; i < 60; ++i) {
                const qint64 aid = nextAid++;
                pageItems.append(video(aid, 1000000 - aid));
            }
            capController.finishLoad(0, pageItems, QString::number(round + 1), true, "", false);
            capController.zoneTimer_.stop();
        }
        check(capController.pool_.size() == 2000 && capController.items_.size() == 2000,
              "descending 35-page feed caps both pool and projection at 2000");
        check(capController.pool_.first().toMap().value("aid").toLongLong() == 101 &&
              capController.pool_.last().toMap().value("aid").toLongLong() == 2100,
              "old admissions are evicted while the entire newly loaded page survives");
        check(capController.offset_ == "35" && !capController.ended_,
              "newly displayed page and continuation cursor progress together");
        check(!capController.poolAids_.contains(1) && !capController.aidOccurrences_.contains(1) &&
              !capController.pendingZoneAids_.contains(1),
              "eviction releases dedup group and queued zone lookup");
        capController.finishLoad(0, {video(101, 1), video(2101, 997899)}, "36", true, "", false);
        capController.zoneTimer_.stop();
        check(!capController.poolAids_.contains(101) && capController.poolAids_.contains(2101),
              "moving an earlier representative to the tail does not renew admission priority");
        capController.finishLoad(0, {video(2000, 1), video(2000, 2)}, "37", true, "", false);
        capController.zoneTimer_.stop();
        const auto grouped = capController.pool_[capController.findPoolIndexByAid(2000)].toMap();
        check(capController.pool_.size() == 2000 && grouped.value("duplicateCount").toInt() == 3 &&
              grouped.value("pubTs").toLongLong() == 1,
              "surviving group retains all occurrence records and earliest representative");
        capController.finishLoad(0, {video(1, 999999999)}, "38", true, "", false);
        capController.zoneTimer_.stop();
        check(capController.pool_.size() == 2000 && capController.poolAids_.contains(1) &&
              capController.aidOccurrences_.value(1).size() == 1,
              "evicted aid is readmitted as a fresh dedup group without stale occurrences");

        DynamicsController posts;
        posts.setCategoryFilter("post");
        QList<DynamicFeedItem> postItems;
        for (int i = 1; i <= 2001; ++i) {
            DynamicFeedItem item;
            item.category = DynamicCategory::Post;
            item.id = QStringLiteral("post-%1").arg(i);
            item.pubTs = 10000 - i;
            postItems.append(item);
        }
        posts.finishLoad(0, postItems, "posts-2", true, "", false);
        check(posts.pool_.size() == 2000 && !posts.poolIds_.contains("post-1") &&
              posts.poolIds_.contains("post-2001"),
              "non-video admission eviction also releases dynamic-id dedup keys");
        posts.finishLoad(0, {postItems.first(), postItems.last()}, "posts-3", true, "", false);
        check(posts.pool_.size() == 2000 && posts.poolIds_.contains("post-1") &&
              !posts.poolIds_.contains("post-2"),
              "evicted non-video id is readmitted while surviving duplicate stays unique");

        double scrollChanges = 0;
        QObject::connect(&capController, &DynamicsController::scrollOffsetChanged, [&] { ++scrollChanges; });
        capController.setScrollOffset(84.0);
        capController.setScrollOffset(84.0);
        check(capController.scrollOffset() == 84.0 && scrollChanges == 1,
              "scrollOffset stores the remembered position and dedups redundant writes");

        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    return DynamicsControllerTest::run();
}
