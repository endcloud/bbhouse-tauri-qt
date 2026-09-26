// Actual page recreation, layout and sliding-window regression; no network or account data.
#include <QApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QJSValue>
#include <memory>
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    int warnings = 0, failures = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        warnings += errors.size(); for (const auto &e : errors) qWarning() << e;
    });
    const QString root = QStringLiteral(ONLINE_QML_ROOT);
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/HistoryCard.qml"), "bbhouse", 1, 0, "HistoryCard");
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/CoverPreviewOverlay.qml"), "bbhouse", 1, 0, "CoverPreviewOverlay");
    QQmlComponent mocks(&engine);
    mocks.setData(R"(import QtQml
        QtObject {
            property var pool: []
            property bool busy: false
            property bool ended: true
            property bool unauthorized: false
            property string searchText: "Video"
            property real scrollOffset: 4000
            property var scrollAnchor: ({})
            property int requests: 0
            property var decodableImageFormats: ["jpg", "png"]
            function ensureLoaded() {}
            function loadMore() { requests++; busy = true }
            function refresh() { requests++; pool = [] }
            signal coverDownloadFinished(string savedPath)
            signal loadFailed(string message)
        })", QUrl());
    std::unique_ptr<QObject> mock(mocks.create());
    if (!mock) return 1;
    for (const char *name : {"OnlineHistoryController", "AppController", "HistoryController", "PlayerController"})
        engine.rootContext()->setContextProperty(name, mock.get());
    auto rows = [](int first, int last) {
        QVariantList items;
        for (int n = first; n <= last; ++n) items.append(QVariantMap{
            {"videoKey", QString("archive:%1:0").arg(n)}, {"title", QString("Video %1").arg(n)},
            {"oid", QString::number(n)}, {"business", "archive"}, {"coverUrl", ""}, {"authorName", "Fixture"}});
        return items;
    };
    auto settle = [](int ms = 120) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
    auto check = [&](bool ok, const char *label) { qInfo() << (ok ? "PASS" : "FAIL") << label; failures += !ok; };
    auto anchor = [](QObject *page) {
        QVariant result;
        QMetaObject::invokeMethod(page, "captureAnchor", Q_RETURN_ARG(QVariant, result));
        return result.value<QJSValue>().toVariant().toMap();
    };
    mock->setProperty("pool", rows(1, 300));
    QQmlComponent component(&engine, QUrl::fromLocalFile(root + "/pages/OnlineHistoryPage.qml"));
    QQuickWindow window;
    window.resize(1000, 700); window.show();
    auto create = [&](int width) {
        auto *item = qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"width", width}, {"height", 700}}));
        if (item) item->setParentItem(window.contentItem());
        else qWarning() << component.errors();
        return item;
    };
    std::unique_ptr<QQuickItem> page(create(1000));
    if (!page) return 1;
    settle();
    auto *grid = page->findChild<QObject *>("onlineHistoryGrid");
    check(grid && qAbs(grid->property("contentY").toDouble() - 4000) < 1,
          "first layout restores saved offset instead of clamping to zero");
    check(page->property("searchQuery").toString() == "Video" && mock->property("requests").toInt() == 0,
          "search echo does not reset viewport or refetch a loaded pool");
    if (!grid) return 1;
    grid->setProperty("contentY", 8500.0);
    settle();
    auto before = anchor(page.get());
    check(!before.value("key").toString().isEmpty(), "actual viewport has a stable item anchor");
    page.reset();
    page.reset(create(650));
    settle();
    grid = page->findChild<QObject *>("onlineHistoryGrid");
    auto after = anchor(page.get());
    auto visibleKeys = page->property("filteredItems").value<QJSValue>().toVariant().toList();
    int savedIndex = -1;
    for (int i = 0; i < visibleKeys.size(); ++i) if (visibleKeys[i].toMap().value("videoKey") == before.value("key")) savedIndex = i;
    check(savedIndex >= after.value("index").toInt() && savedIndex < after.value("index").toInt() + 2,
          "recreation at different column count keeps saved item in the first visible row");
    before = anchor(page.get());
    mock->setProperty("pool", rows(21, 320));
    settle();
    after = anchor(page.get());
    check(before.value("key") == after.value("key"), "head eviction keeps the visible retained item anchored");
    page->setProperty("searchQuery", "Video 320");
    settle();
    mock->setProperty("ended", false);
    settle(550);
    check(mock->property("requests").toInt() == 1 && mock->property("busy").toBool(),
          "one matching card below viewport capacity schedules continuation");
    mock->setProperty("ended", true);
    mock->setProperty("busy", false);
    settle(500);
    check(mock->property("requests").toInt() == 1, "ended feed stops automatic fill");
    page.reset();
    page.reset(create(650));
    settle();
    check(page->property("searchQuery").toString() == "Video 320"
          && page->findChild<QObject *>("onlineHistoryGrid")->property("count").toInt() == 1,
          "submitted search survives actual page destruction and recreation");
    // Failed continuation keeps its cursor and accepts a later user scroll.
    page->setProperty("searchQuery", "Video");
    settle();
    mock->setProperty("ended", false);
    QMetaObject::invokeMethod(mock.get(), "loadFailed", Q_ARG(QString, "fixture offline"));
    grid = page->findChild<QObject *>("onlineHistoryGrid");
    grid->setProperty("contentY", grid->property("contentHeight").toDouble() - 500);
    QMetaObject::invokeMethod(page.get(), "maybeLoadMore");
    check(mock->property("requests").toInt() == 2,
          "user scroll retries a failed continuation without refreshing the pool");
    mock->setProperty("ended", true);
    mock->setProperty("busy", false);
    check(warnings == 0, "online history recreation has no QML warnings");
    page.reset();
    return failures ? 1 : 0;
}
