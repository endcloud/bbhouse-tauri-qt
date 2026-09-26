#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <memory>

#include "controllers/PopularController.h"

class PopularControllerTest : public PopularController {
public:
    void respond(const QVariantList &items, bool more = false, const QString &error = {}) {
        finishFetch(generation_, key(), pendingPage_, {items, more, {}}, error);
    }
    void periods(const QVariantList &items) { finishPeriodsFetch(periodsGeneration_, items, {}); }
private:
    void startFetch(quint64, const QString &, int) override {}
    void startPeriodsFetch(quint64) override {}
};

static QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems()) if (auto *found = visualChild(child, name)) return found;
    return nullptr;
}

static QStringList warnings;
static void logger(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if ((type == QtWarningMsg || type == QtCriticalMsg) && !message.startsWith("Populating font family aliases")) warnings.append(message);
    fprintf(stderr, "%s\n", qPrintable(message));
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
#endif
    QApplication app(argc, argv);
    qInstallMessageHandler(logger);
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    const QString root = QStringLiteral(POPULAR_QML_ROOT);
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/HistoryCard.qml"), "bbhouse", 1, 0, "HistoryCard");
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/CoverPreviewOverlay.qml"), "bbhouse", 1, 0, "CoverPreviewOverlay");
    QQmlComponent mockComponent(&engine);
    mockComponent.setData(R"(import QtQml
        QtObject {
            property var decodableImageFormats: ["jpg", "png"]
            property string imageTranscodeSuffix: ".jpg"
            property string openedMid: ""
            property string requestedSeason: ""
            function openUserSpace(mid, name, face) { openedMid = mid }
            function seasonDetail(id, regional) { requestedSeason = String(id); return ({}) }
            signal seasonDetailReady(var detail)
            signal errorOccurred(string message)
            signal coverDownloadFinished(string savedPath)
        })", QUrl());
    std::unique_ptr<QObject> mock(mockComponent.create());
    PopularControllerTest controller;
    engine.rootContext()->setContextProperty("PopularController", &controller);
    engine.rootContext()->setContextProperty("PopularSeasonController", mock.get());
    engine.rootContext()->setContextProperty("AppController", mock.get());
    engine.rootContext()->setContextProperty("HistoryController", mock.get());
    engine.rootContext()->setContextProperty("PlayerController", mock.get());
    QQmlComponent component(&engine, QUrl::fromLocalFile(root + "/pages/PopularPage.qml"));
    std::unique_ptr<QObject> object(component.create());
    auto *page = qobject_cast<QQuickItem *>(object.get());
    if (!page) { qCritical() << component.errors(); return 1; }
    QQuickWindow window;
    window.resize(1000, 700);
    page->setParentItem(window.contentItem());
    page->setWidth(1000);
    page->setHeight(700);
    window.show();
    auto settle = [&] { QEventLoop loop; QTimer::singleShot(60, &loop, &QEventLoop::quit); loop.exec(); };
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) ++failures;
    };
    QQmlComponent selectorProbeComponent(&engine);
    selectorProbeComponent.setData(R"(
        import QtQml
        import FluentUI
        QtObject {
            property var selector
            function open(dark) {
                FluTheme.darkMode = dark ? FluThemeType.Dark : FluThemeType.Light
                selector.popup.open()
            }
            function readable() {
                var list = selector.popup.contentItem
                for (var i = 0; i < Math.min(3, selector.count); ++i) {
                    var row = list.itemAtIndex(i)
                    if (!row || !row.text || row.text !== selector.textAt(i)) return false
                    var label = row.contentItem
                    if (!label.visible || label.width <= 0 || label.height <= 0 ||
                        label.text !== row.text || label.color.a < 1) return false
                    var bg = selector.popup.background.color
                    if (Math.abs(label.color.r - bg.r) < 0.5) return false
                }
                return selector.count > 0
            }
            function close() { selector.popup.close() }
        }
    )", QUrl());
    std::unique_ptr<QObject> selectorProbe(selectorProbeComponent.create());
    if (!selectorProbe) { qCritical() << selectorProbeComponent.errors(); return 1; }
    auto checkSelector = [&](const char *name) {
        auto *selector = page->findChild<QObject *>(QString::fromLatin1(name));
        check(selector != nullptr, "page exposes actual selector for popup regression");
        if (!selector) return;
        selectorProbe->setProperty("selector", QVariant::fromValue(selector));
        for (bool dark : {false, true}) {
            QMetaObject::invokeMethod(selectorProbe.get(), "open", Q_ARG(QVariant, dark));
            settle();
            QVariant readable;
            QMetaObject::invokeMethod(selectorProbe.get(), "readable", Q_RETURN_ARG(QVariant, readable));
            check(readable.toBool(), dark ? "dark popup shows model labels with readable contrast"
                                         : "light popup shows model labels with readable contrast");
            QMetaObject::invokeMethod(selectorProbe.get(), "close");
            settle();
        }
    };
    auto card = [](int n) {
        return QVariantMap{{"videoKey", QString("archive:%1:0").arg(n)}, {"business", "archive"},
            {"oid", QString::number(n)}, {"title", QString("Video %1").arg(n)},
            {"authorName", "Fixture Author"}, {"authorMid", "9007199254740993"},
            {"coverUrl", ""}, {"viewCount", 0}, {"playCount", qint64(3000000000)}};
    };
    QVariantList items;
    for (int i = 1; i <= 40; ++i) items.append(card(i));
    controller.respond(items, true);
    settle();
    auto *grid = page->findChild<QObject *>("popularGrid");
    auto count = [&] { return grid ? grid->property("count").toInt() : -1; };
    check(count() == 40, "hot list displays loaded cards through stable model");
    // Delegate lifetime must survive busy/error and a same-prefix append.
    auto *firstCard = visualChild(page, "popularCard");
    check(firstCard && firstCard->property("authorMid").toString() == "9007199254740993",
          "real HistoryCard receives exact author ID");
    controller.loadMore(); settle();
    check(firstCard && visualChild(page, "popularCard") == firstCard,
          "busy notifications preserve existing card instance");
    controller.respond({}, false, "fixture failure"); settle();
    check(count() == 40 && visualChild(page, "popularCard") == firstCard,
          "failed append keeps cards and delegates");
    controller.loadMore(); controller.respond({card(41)}, false); settle();
    check(count() == 41 && visualChild(page, "popularCard") == firstCard,
          "successful append preserves existing delegate");
    grid->setProperty("contentY", 200.0);
    controller.refresh(); controller.respond({}, false, "fixture failure"); settle();
    check(grid->property("contentY").toDouble() >= 199, "failed refresh preserves scroll position");
    controller.selectTab("weekly");
    controller.periods({QVariantMap{{"number", 390}, {"label", "Issue 390"}},
                        QVariantMap{{"number", 1}, {"label", "Issue 1"}}});
    controller.respond(items); settle();
    checkSelector("popularWeeklySelector");
    controller.refreshPeriods();
    controller.periods({QVariantMap{{"number", 390}, {"label", "Updated issue 390"}},
                        QVariantMap{{"number", 1}, {"label", "Updated issue 1"}}});
    settle();
    checkSelector("popularWeeklySelector");
    check(count() == 30 && page->property("totalPages").toInt() == 2, "weekly full list paginated locally");
    QMetaObject::invokeMethod(page, "selectPage", Q_ARG(QVariant, 2)); settle();
    check(count() == 10, "weekly second page contains remaining cards");
    page->setProperty("searchQuery", "Video 40"); settle();
    check(count() == 1 && page->property("pageIndex").toInt() == 1, "search covers complete issue and resets local page");
    page->setProperty("searchQuery", "");
    controller.selectWeek(1); controller.respond({card(99)}); settle();
    check(controller.weeklyNumber() == 1 && count() == 1, "historical issue change updates page");
    controller.selectRanking(13);
    controller.respond({QVariantMap{{"business", "pgc"}, {"videoKey", "season:456"},
        {"seasonId", "456"}, {"title", "Season fixture"}, {"invalid", false}}}); settle();
    checkSelector("popularRankingSelector");
    auto *seasonCard = visualChild(page, "popularCard");
    check(seasonCard && seasonCard->property("playable").toBool(), "season-only PGC card is playable");
    if (seasonCard) QMetaObject::invokeMethod(seasonCard, "playInNewWindow");
    check(mock->property("requestedSeason") == "456", "season-only click uses dedicated detail controller");
    page->setVisible(false);
    check(page->property("pendingSeasonId").toString().isEmpty(), "hidden cached page drops pending playback intent");
    check(warnings.isEmpty(), "populated QML page has no runtime warnings");
    page->setParentItem(nullptr);
    return failures ? 1 : 0;
}
