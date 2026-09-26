// Exercise real pages across destruction/recreation; all controllers are offline doubles.
#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <memory>

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    const QString root = QStringLiteral(PAGE_STATE_QML_ROOT);
    for (const auto *type : {"HistoryCard", "CoverPreviewOverlay", "SeasonCard"})
        qmlRegisterType(QUrl::fromLocalFile(root + "/controls/" + type + ".qml"), "bbhouse", 1, 0, type);
    int warnings = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        warnings += errors.size();
        for (const auto &error : errors) qWarning().noquote() << error.toString();
    });
    QQmlComponent mockComponent(&engine);
    mockComponent.setData(R"(
        import QtQml
        QtObject {
            property var pool: []
            property var pageItems: []
            property bool busy: false
            property bool loaded: true
            property bool unauthorized: false
            property bool hasMore: false
            property string error: ""
            property string description: ""
            property string activeTab: "precious"
            property int weeklyNumber: 1
            property int rankingRid: 0
            property var weeklyPeriods: []
            property bool periodsBusy: false
            property bool periodsLoaded: true
            property string periodsError: ""
            property string searchText: "Fixture"
            property real scrollOffset: 0
            property int pageIndex: 3
            property string currentTab: "guochuang"
            property string regionalSearch: "Fixture"
            property int currentPage: 3
            property int currentTotal: 120
            property int refreshCount: 0
            property int ensureCount: 0
            property var decodableImageFormats: ["jpg", "png"]
            property string imageTranscodeSuffix: ".jpg"
            signal coverDownloadFinished(string savedPath)
            signal seasonDetailReady(var detail)
            signal errorOccurred(string message)
            signal loadFailed(string message)
            signal pageInfoChanged()
            function ensureLoaded() { ensureCount++ }
            function ensureCurrentBucketLoaded() { ensureCount++ }
            function refresh() { refreshCount++ }
            function setRegionalSearch(query) { regionalSearch = query }
            function setBucket(bucket) {}
            function loadPage(number) { currentPage = number; pageInfoChanged() }
        })", QUrl());
    std::unique_ptr<QObject> mock(mockComponent.create());
    if (!mock) { qCritical() << mockComponent.errors(); return 1; }
    for (const auto *name : {"PopularController", "PopularSeasonController", "LiveController",
                            "WatchlaterController", "BangumiController", "AppController",
                            "HistoryController", "PlayerController"})
        engine.rootContext()->setContextProperty(name, mock.get());
    QVariantList items;
    for (int i = 1; i <= 120; ++i)
        items.append(QVariantMap{{"videoKey", QString("archive:%1:0").arg(i)}, {"business", "archive"},
            {"oid", QString::number(i)}, {"title", QString("Video Fixture %1").arg(i)},
            {"authorName", "Fixture"}, {"authorMid", "123"}, {"coverUrl", ""},
            {"seasonId", QString::number(i)}, {"seasonType", 4}, {"cover", ""},
            {"roomId", QString::number(i)}, {"uname", "Fixture"}, {"mid", "123"},
            {"face", ""}, {"area", "Fixture"}, {"online", "1"}});
    mock->setProperty("pool", items);
    mock->setProperty("pageItems", items.mid(0, 30));
    QQuickWindow window;
    window.resize(1000, 700);
    window.show();
    auto settle = [] {
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
    };
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) ++failures;
    };
    auto create = [&](const char *name) {
        QQmlComponent component(&engine, QUrl::fromLocalFile(root + "/pages/" + name + ".qml"));
        auto object = std::unique_ptr<QObject>(component.create());
        auto *page = qobject_cast<QQuickItem *>(object.get());
        if (!page) { qCritical() << component.errors(); ++failures; return object; }
        page->setParentItem(window.contentItem());
        page->setSize(QSizeF(1000, 700));
        settle();
        return object;
    };
    for (const auto *name : {"PopularPage", "WatchlaterPage"}) {
        mock->setProperty("searchText", "Fixture");
        mock->setProperty("pageIndex", 3);
        mock->setProperty("scrollOffset", 0);
        auto object = create(name);
        if (!object) continue;
        check(object->property("pageIndex") == 3 && object->property("searchQuery") == "Fixture",
              "initial nonempty query does not reset saved third page");
        const char *paginationName = QString(name) == "PopularPage" ? "popularPagination" : "watchlaterPagination";
        auto *pagination = object->findChild<QObject *>(paginationName);
        check(pagination && pagination->property("pageCurrent") == 3,
              "pagination selected page matches restored data");
        object->setProperty("searchQuery", "Video");
        settle();
        check(object->property("pageIndex") == 1 && mock->property("searchText") == "Video",
              "new search persists query and resets page");
        QMetaObject::invokeMethod(object.get(), "selectPage", Q_ARG(QVariant, 3));
        settle();
        if (QString(name) == "PopularPage") {
            auto *grid = object->findChild<QObject *>("popularGrid");
            grid->setProperty("contentY", 400.0);
        }
        object.reset();
        settle();
        object = create(name);
        if (!object) continue;
        pagination = object->findChild<QObject *>(paginationName);
        check(object->property("pageIndex") == 3 && pagination && pagination->property("pageCurrent") == 3,
              "destroy and recreate preserves data page and selected pagination");
        check(object->property("searchQuery") == "Video", "destroy and recreate restores exact search text");
        if (QString(name) == "PopularPage") {
            auto *grid = object->findChild<QObject *>("popularGrid");
            check(qAbs(grid->property("contentY").toDouble() - 400.0) < 1,
                  "popular scroll restored after model layout");
        }
    }
    mock->setProperty("searchText", "Fixture");
    mock->setProperty("scrollOffset", 0);
    {
        auto page = create("LivePage");
        if (page) {
            page->setProperty("searchQuery", "Video");
            settle();
            page->findChild<QObject *>("liveGrid")->setProperty("contentY", 900.0);
            page.reset();
            settle();
            page = create("LivePage");
            if (page) {
                check(page->property("searchQuery") == "Video", "live query survives page recreation");
                check(qAbs(page->findChild<QObject *>("liveGrid")->property("contentY").toDouble() - 900.0) < 1,
                      "live scroll survives page recreation");
            }
        }
    }
    {
        mock->setProperty("activeTab", "popular");
        mock->setProperty("searchText", "");
        mock->setProperty("pageIndex", 1);
        mock->setProperty("scrollOffset", 0);
        auto page = create("PopularPage");
        if (page) {
            auto *grid = page->findChild<QObject *>("popularGrid");
            const double height = grid->property("cellHeight").toDouble();
            grid->setProperty("contentY", 20 * height + 25);
            settle();
            mock->setProperty("pool", items.mid(30));
            settle();
            check(qAbs(grid->property("contentY").toDouble() - grid->property("originY").toDouble() - (10 * height + 25)) < 1,
                  "popular head eviction keeps retained viewport row and intra-row offset");
        }
        mock->setProperty("pool", items);
        mock->setProperty("searchText", "Fixture");
    }
    for (int iteration = 0; iteration < 2; ++iteration) {
        auto page = create("BangumiPage");
        if (!page) continue;
        auto *pagination = page->findChild<QObject *>("bangumiPagination");
        check(pagination && pagination->property("pageCurrent") == 3,
              "cached bangumi page restores pagination without pageInfoChanged emission");
        check(page->property("currentTab") == "guochuang" && page->property("searchQuery") == "Fixture",
              "bangumi tab and query survive recreation");
    }
    {
        QQmlComponent component(&engine, QUrl::fromLocalFile(root + "/controls/SeasonCard.qml"));
        std::unique_ptr<QObject> card(component.create());
        auto *item = qobject_cast<QQuickItem *>(card.get());
        check(item != nullptr, "season card instantiates for decoded-size regression");
        if (item) {
            item->setParentItem(window.contentItem());
            settle();
            auto *cover = item->findChild<QObject *>("seasonCoverImage");
            const QSize size = cover ? cover->property("sourceSize").toSize() : QSize();
            check(size.width() > 0 && size.width() <= 340 && size.height() > 0 && size.height() <= 452,
                  "season cover decode dimensions stay within DPR 2 budget");
        }
    }
    check(mock->property("refreshCount") == 0, "recreating cached pages never requests refresh");
    check(warnings == 0, "real page recreation has no QML warnings");
    return failures ? 1 : 0;
}
