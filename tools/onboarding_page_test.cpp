// Load the real new pages without account/network/file controllers. This checks
// QML bindings and bounded geometry; visual interaction remains a manual check.
#include <QApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
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
            property string appVersion: "2.0.3"
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
        if (QString::fromLatin1(name) == "AboutPage.qml") {
            QQmlComponent stateComponent(&engine);
            stateComponent.setData("import QtQml; QtObject { property var value: ({}) }", QUrl());
            std::unique_ptr<QObject> state(stateComponent.create());
            page->setProperty("navigationState", QVariant::fromValue(state.get()));
            auto settle = [&] { QEventLoop loop; QTimer::singleShot(100, &loop, &QEventLoop::quit); loop.exec(); };
            settle();
            auto *scroll = page->findChild<QObject *>("aboutScrollView");
            if (!scroll) return 1;
            scroll->setProperty("contentY", 400.0);
            object.reset();
            object.reset(component.createWithInitialProperties({
                {"navigationState", QVariant::fromValue(state.get())}, {"width", 1000}, {"height", 600}}));
            page = qobject_cast<QQuickItem *>(object.get());
            if (!page) return 1;
            page->setParentItem(window.contentItem());
            settle();
            scroll = page->findChild<QObject *>("aboutScrollView");
            const bool restored = scroll && qAbs(scroll->property("contentY").toDouble() - 400.0) < 1;
            qInfo() << (restored ? "PASS" : "FAIL") << "About recreation restores scroll after layout";
            failed |= !restored;
            page->setParentItem(nullptr);
            object.reset();
            continue;
        }
        page->setParentItem(nullptr);
    }
    return failed ? 1 : 0;
}
