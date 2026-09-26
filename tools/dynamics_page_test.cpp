#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QJSValue>
#include <QQuickItem>
#include <QQuickWindow>
#include <memory>
#include "controllers/DynamicsController.h"

// Offline result delivery only: no Cookie, network requests or user database.
class DynamicsControllerTest {
public:
    static void append(DynamicsController &controller, int first, int count) {
        QList<DynamicFeedItem> items;
        for (int i = first; i < first + count; ++i) {
            DynamicFeedItem item;
            item.category = DynamicCategory::Video;
            item.id = QString::number(i);
            item.aid = i;
            item.pubTs = 1000000 - i;
            item.title = QStringLiteral("Fixture %1").arg(i);
            items.append(item);
        }
        controller.finishLoad(0, items, QString::number(first + count), false, {}, false);
        controller.zoneTimer_.stop();
    }
    static void zone(DynamicsController &controller, qint64 aid) {
        controller.pendingZoneAids_.removeAll(aid);
        controller.applyZoneResult(aid, QStringLiteral("游戏"));
        controller.zoneTimer_.stop();
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        fprintf(stderr, "%s\n", qPrintable(message));
    });
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    const QString root = QStringLiteral(DYNAMICS_QML_ROOT);
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/HistoryCard.qml"), "bbhouse", 1, 0, "HistoryCard");
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/CoverPreviewOverlay.qml"), "bbhouse", 1, 0, "CoverPreviewOverlay");
    QQmlComponent mockComponent(&engine);
    mockComponent.setData(R"(import QtQml
        QtObject {
            property var decodableImageFormats: ["jpg", "png"]
            property string imageTranscodeSuffix: ".jpg"
            function value(key, fallback) { return fallback }
            function setValue(key, value) {}
            function openUserSpace(mid, name, face) {}
            signal errorOccurred(string message)
            signal coverDownloadFinished(string savedPath)
        })", QUrl());
    std::unique_ptr<QObject> mock(mockComponent.create());
    DynamicsController controller;
    DynamicsControllerTest::append(controller, 1, 2000);
    engine.rootContext()->setContextProperty("DynamicsController", &controller);
    for (const char *name : {"AppPreferences", "AppController", "HistoryController", "PlayerController"})
        engine.rootContext()->setContextProperty(QString::fromLatin1(name), mock.get());
    QQmlComponent component(&engine, QUrl::fromLocalFile(root + "/pages/DynamicsPage.qml"));
    QQuickWindow window;
    window.resize(1000, 700);
    window.show();
    std::unique_ptr<QObject> object;
    QQuickItem *page = nullptr;
    QObject *grid = nullptr;
    auto settle = [&] { QEventLoop loop; QTimer::singleShot(120, &loop, &QEventLoop::quit); loop.exec(); };
    auto create = [&](int width) {
        object.reset(component.create());
        page = qobject_cast<QQuickItem *>(object.get());
        if (!page) { qCritical() << component.errors(); return false; }
        page->setParentItem(window.contentItem());
        page->setWidth(width);
        page->setHeight(700);
        settle();
        grid = page->findChild<QObject *>("dynamicsGrid");
        return grid != nullptr;
    };
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        failures += !ok;
    };
    auto anchor = [&] { return page->property("viewAnchor").value<QJSValue>().toVariant().toMap(); };
    if (!create(1000)) return 1;
    grid->setProperty("contentY", 5000.0);
    settle();
    const auto saved = anchor();
    check(!saved.value("key").toString().isEmpty(), "scroll records stable visible card identity");
    object.reset();
    if (!create(1000)) return 1;
    check(anchor().value("key") == saved.value("key") &&
          qAbs(grid->property("contentY").toDouble() - 5000.0) < 2,
          "destroy and recreate restores card and offset after layout");
    page->setWidth(660);
    settle();
    check(anchor().value("key") == saved.value("key") ||
          qAbs(anchor().value("index").toInt() - saved.value("index").toInt()) <= 1,
          "column-count change keeps remembered card in the first visible row");
    grid->setProperty("contentY", 15000.0);
    settle();
    const auto beforeTrim = anchor();
    DynamicsControllerTest::append(controller, 2001, 60);
    settle();
    check(anchor().value("key") == beforeTrim.value("key") &&
          qAbs(anchor().value("fraction").toDouble() - beforeTrim.value("fraction").toDouble()) < .01,
          "evicting earlier admissions preserves visible identity and row offset");
    const auto beforeZone = anchor();
    QQuickItem *visibleDelegate = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, visibleDelegate),
                              Q_ARG(int, beforeZone.value("index").toInt()));
    DynamicsControllerTest::zone(controller, beforeZone.value("key").toString().mid(6).toLongLong());
    settle();
    QQuickItem *updatedDelegate = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, updatedDelegate),
                              Q_ARG(int, beforeZone.value("index").toInt()));
    check(anchor().value("key") == beforeZone.value("key") && visibleDelegate &&
          updatedDelegate == visibleDelegate,
          "incremental zone resolution preserves viewport anchor and visible delegate");
    page->setProperty("searchQuery", "Fixture 2059");
    settle();
    QVariant sparse;
    QMetaObject::invokeMethod(page, "needsMoreForViewport", Q_RETURN_ARG(QVariant, sparse));
    check(grid->property("count").toInt() == 1 && sparse.toBool(),
          "nonempty one-card projection requests bounded viewport filling");
    object.reset();
    if (!create(660)) return 1;
    check(page->property("searchQuery").toString() == "Fixture 2059" && grid->property("count").toInt() == 1,
          "recreation retains search text and projection without refreshing");
    page->setProperty("searchQuery", "");
    settle();
    QMetaObject::invokeMethod(page, "needsMoreForViewport", Q_RETURN_ARG(QVariant, sparse));
    check(!sparse.toBool(), "full viewport stops sparse auto continuation");
    return failures ? 1 : 0;
}
