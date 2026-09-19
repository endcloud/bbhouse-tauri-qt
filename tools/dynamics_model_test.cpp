#include "controllers/DynamicCardModel.h"

#include <QGuiApplication>
#include <QDebug>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <memory>

namespace {
QVariantMap video(qint64 aid, const QString &id, const QString &zone = {}) {
    return {{"aid", aid}, {"id", id}, {"category", "video"}, {"zoneName", zone}};
}
QObject *delegateAt(QObject *root, int row) {
    QVariant result;
    QMetaObject::invokeMethod(root, "delegateAt", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, row));
    return result.value<QObject *>();
}
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) ++failures;
    };
    DynamicCardModel model;
    int resets = 0, changes = 0, inserts = 0, removes = 0, moves = 0, counts = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&] { ++inserts; });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&] { ++removes; });
    QObject::connect(&model, &QAbstractItemModel::rowsMoved, [&] { ++moves; });
    QObject::connect(&model, &DynamicCardModel::countChanged, [&] { ++counts; });

    // 两个 av 号低 32 位完全一致，必须按完整 qint64 区分。
    auto a = video(4294967301LL, "newer-a");
    auto b = video(8589934597LL, "b");
    auto c = video(12884901893LL, "c");
    model.setItems({a, b, c});
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("cards", &model);
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
Item {
    id: root
    property int created: 0
    property int destroyed: 0
    function delegateAt(row) { return rows.itemAt(row) }
    Repeater {
        id: rows
        model: cards
        delegate: Item {
            required property var cardData
            required property int index
            property string zone: cardData.zoneName || ""
            property string dynamicId: cardData.id || ""
            property int currentRow: index
            Component.onCompleted: root.created++
            Component.onDestruction: root.destroyed++
        }
    }
}
)", QUrl("qrc:/dynamics-model-test.qml"));
    std::unique_ptr<QObject> root(component.create());
    if (!root) {
        qCritical() << component.errors();
        return 1;
    }
    QPointer<QObject> da = delegateAt(root.get(), 0);
    QPointer<QObject> db = delegateAt(root.get(), 1);
    QPointer<QObject> dc = delegateAt(root.get(), 2);
    QPersistentModelIndex pa = model.index(0, 0), pb = model.index(1, 0), pc = model.index(2, 0);
    check(da && db && dc && da != db && root->property("created").toInt() == 3,
          "real QML Repeater creates distinct delegates for large video ids");
    if (!da || !db || !dc) return 1;

    const int oldInserts = inserts, oldCounts = counts;
    a["zoneName"] = "Music";
    model.setItems({a, b, c});
    b["zoneName"] = "Games";
    model.setItems({a, b, c});
    c["zoneName"] = "Science";
    model.setItems({a, b, c});
    check(changes == 3 && inserts == oldInserts && removes == 0 && moves == 0 && counts == oldCounts,
          "individual zone replies emit only row data changes");
    check(delegateAt(root.get(), 0) == da && delegateAt(root.get(), 1) == db && delegateAt(root.get(), 2) == dc
              && da->property("zone") == "Music" && db->property("zone") == "Games"
              && dc->property("zone") == "Science" && root->property("created").toInt() == 3
              && root->property("destroyed").toInt() == 0,
          "zone bindings update without recreating any card delegate");
    check(!model.setItems({a, b, c}) && changes == 3 && inserts == oldInserts && counts == oldCounts,
          "identical projection produces no notifications");

    auto d = video(17179869189LL, "d", "Music");
    model.setItems({d, a, b, c});
    check(delegateAt(root.get(), 1) == da && delegateAt(root.get(), 2) == db && delegateAt(root.get(), 3) == dc
              && pa.row() == 1 && pb.row() == 2 && pc.row() == 3
              && root->property("created").toInt() == 4 && root->property("destroyed").toInt() == 0,
          "new matching card inserts while preserving all existing delegates and persistent indexes");

    a["id"] = "earlier-a";
    model.setItems({c, b, a, d});
    check(delegateAt(root.get(), 0) == dc && delegateAt(root.get(), 1) == db && delegateAt(root.get(), 2) == da
              && pa.row() == 2 && pb.row() == 1 && pc.row() == 0 && moves > 0
              && da->property("dynamicId") == "earlier-a" && da->property("currentRow").toInt() == 2
              && root->property("created").toInt() == 4 && root->property("destroyed").toInt() == 0,
          "reorder and replacement by earlier dynamic retain the video identity and delegates");

    model.setItems({c, a});
    check(!pb.isValid() && pa.row() == 1 && pc.row() == 0 && delegateAt(root.get(), 0) == dc
              && delegateAt(root.get(), 1) == da && model.count() == 2,
          "removal invalidates only deleted rows and preserves remaining delegates");

    const QVariantMap post{{"category", "post"}, {"id", "same-id"}, {"aid", a.value("aid")}};
    const QVariantMap post2{{"category", "post"}, {"id", "another-id"}, {"aid", a.value("aid")}};
    model.setItems({a, post, post2});
    QPersistentModelIndex pp = model.index(1, 0), pp2 = model.index(2, 0);
    model.setItems({post2, a, post});
    check(pp.row() == 2 && pp2.row() == 0 && pp.data(DynamicCardModel::CardDataRole).toMap() == post,
          "non-video cards key by dynamic id even when aid fields are equal");

    const QVariantMap missing{{"category", "video"}, {"aid", -1}};
    const QVariantList malformed{a, a, missing, QVariantMap{}, QVariant("invalid")};
    model.setItems(malformed);
    const int signalsBefore = changes + inserts + removes + moves + counts;
    check(model.count() == malformed.size() && !model.setItems(malformed)
              && changes + inserts + removes + moves + counts == signalsBefore,
          "duplicate and absent identities remain separate and unchanged malformed snapshots are stable");
    model.setItems({});
    check(model.count() == 0 && model.rowCount() == 0 && !pa.isValid() && !pc.isValid() && resets == 0,
          "clear and all preceding updates never reset the model");
    check(model.rowCount(model.index(0, 0)) == 0 && !model.data({}).isValid(), "empty model rejects invalid data access");
    return failures ? 1 : 0;
}
