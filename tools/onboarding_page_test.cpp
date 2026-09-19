// Load the real new pages without account/network/file controllers. This checks
// QML bindings and bounded geometry; visual interaction remains a manual check.
#include <QApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDebug>
#include <memory>

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    bool failed = false;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        failed = true; qWarning() << errors;
    });
    QQmlComponent mocks(&engine);
    mocks.setData(R"(import QtQml
        QtObject {
            property string appVersion: "1.0.1"
            property bool busy: false
            property string qrUrl: "https://passport.bilibili.com/fixture"
            property string status: ""
            property string error: ""
            property string cookiePath: "/isolated/bilibili.cookie.txt"
            signal authenticated()
            function cancel() {}
        })", QUrl());
    std::unique_ptr<QObject> mock(mocks.create());
    if (!mock) return 1;
    engine.rootContext()->setContextProperty("AppController", mock.get());
    engine.rootContext()->setContextProperty("LoginController", mock.get());
    for (const auto &name : {"AboutPage.qml", "LoginPage.qml"}) {
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(ONBOARDING_QML_ROOT) + "/" + name));
        std::unique_ptr<QObject> object(component.create());
        auto *page = qobject_cast<QQuickItem *>(object.get());
        if (!page) { qWarning() << component.errors(); return 1; }
        QQuickWindow window;
        page->setParentItem(window.contentItem());
        for (const int width : {520, 1000}) {
            window.resize(width, 600);
            page->setSize(QSizeF(width, 600));
            app.processEvents();
            if (page->width() <= 0 || page->height() <= 0) failed = true;
        }
        page->setParentItem(nullptr);
    }
    return failed ? 1 : 0;
}
