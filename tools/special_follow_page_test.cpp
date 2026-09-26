// Real SpecialFollowPage plus the real C++ controller/metaobject. Only network
// entry points and the avatar disk cache are replaced; no invokable UI setters.
#include "controllers/SpecialFollowController.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJSValue>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

// Reuse the controller's existing friend fixture access; do not expose test APIs.
class UserSpaceControllerTest {
public:
    static void seed(SpecialFollowController &controller) {
        controller.upsReady_ = true;
        controller.currentMid_ = 123;
        auto &session = controller.sessions_[123];
        session.arc.page = 1;
        session.articles.page = 1;
        session.articles.items = {
            QVariantMap{{"id", "1"}, {"title", "Fixture article"}, {"coverUrl", ""}, {"words", 120}},
            QVariantMap{{"id", "2"}, {"title", "Other article"}, {"coverUrl", ""}, {"words", 300}}
        };
    }
};
class OfflineController final : public SpecialFollowController {
public:
    int requests = 0;
private:
    void startArcFetch(qint64, int) override { ++requests; }
    void startSeasonsFetch(qint64, int) override { ++requests; }
    void startArticlesFetch(qint64) override { ++requests; }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    QTemporaryDir fixture(QCoreApplication::applicationDirPath() + "/special-page-XXXXXX");
    if (!fixture.isValid()) return 1;
    qputenv("BBHOUSE_DATA_DIR", fixture.path().toUtf8());
    QFile avatar(fixture.filePath("AvatarSource.qml"));
    if (!avatar.open(QIODevice::WriteOnly)) return 1;
    avatar.write("import QtQml; QtObject { property string userId; property url remoteUrl; property url source }");
    avatar.close();
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    const QString root = QStringLiteral(SPECIAL_QML_ROOT);
    for (const char *type : {"HistoryCard", "CoverPreviewOverlay"})
        qmlRegisterType(QUrl::fromLocalFile(root + "/controls/" + type + ".qml"), "bbhouse", 1, 0, type);
    qmlRegisterType(QUrl::fromLocalFile(avatar.fileName()), "bbhouse", 1, 0, "AvatarSource");
    int warnings = 0, failures = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        warnings += errors.size();
        for (const auto &error : errors) qWarning() << error;
    });
    QQmlComponent appMockComponent(&engine);
    appMockComponent.setData(R"(import QtQml
        QtObject {
            property var decodableImageFormats: ["jpg", "png"]
            signal coverDownloadFinished(string savedPath)
        })", QUrl());
    std::unique_ptr<QObject> appMock(appMockComponent.create());
    if (!appMock) return 1;
    OfflineController controller;
    UserSpaceControllerTest::seed(controller);
    engine.rootContext()->setContextProperty("SpecialFollowController", &controller);
    for (const char *name : {"AppController", "HistoryController", "PlayerController"})
        engine.rootContext()->setContextProperty(name, appMock.get());
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        failures += !ok;
    };
    check(controller.metaObject()->indexOfMethod("setSearchText(QString)") < 0
          && controller.metaObject()->indexOfMethod("setCurrentTab(int)") < 0,
          "fixture uses real non-invokable setters rather than permissive mock methods");
    auto settle = [] { QEventLoop loop; QTimer::singleShot(120, &loop, &QEventLoop::quit); loop.exec(); };
    QQmlComponent component(&engine, QUrl::fromLocalFile(root + "/pages/SpecialFollowPage.qml"));
    QQuickWindow window;
    window.resize(1000, 700);
    window.show();
    auto create = [&]() {
        std::unique_ptr<QQuickItem> page(qobject_cast<QQuickItem *>(component.createWithInitialProperties({
            {"width", 1000}, {"height", 700}})));
        if (page) page->setParentItem(window.contentItem());
        else qWarning() << component.errors();
        settle();
        return page;
    };
    auto page = create();
    if (!page) return 1;
    page->setProperty("searchQuery", "Fixture");
    QMetaObject::invokeMethod(page.get(), "selectTab", Q_ARG(QVariant, 2));
    settle();
    check(controller.searchText() == "Fixture" && controller.currentTab() == 2,
          "submitted search and article selection reach real C++ writable properties");
    check(page->property("articleFiltered").value<QJSValue>().toVariant().toList().size() == 1,
          "submitted nonempty search filters actual article page");
    QPointer<QQuickItem> retired(page.get());
    page.reset();
    check(!retired, "navigation destroys original QML page tree");
    page = create();
    if (!page) return 1;
    check(page->property("searchQuery").toString() == "Fixture" && page->property("currentTab").toInt() == 2,
          "recreated special-follow page restores submitted query and article selection");
    check(page->property("articleFiltered").value<QJSValue>().toVariant().toList().size() == 1,
          "recreated special-follow page restores filtered article content");
    check(controller.requests == 0, "cached page recreation and article selection do not refetch");
    QMetaObject::invokeMethod(page.get(), "resetView");
    check(controller.searchText().isEmpty() && controller.currentTab() == 0,
          "shared space-page reset updates real controller properties without invokable setters");
    page.reset();
    check(warnings == 0, "special-follow lifecycle has no QML runtime warnings");
    return failures ? 1 : 0;
}
