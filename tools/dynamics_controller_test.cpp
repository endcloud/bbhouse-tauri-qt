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
        return failures ? 1 : 0;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    return DynamicsControllerTest::run();
}
