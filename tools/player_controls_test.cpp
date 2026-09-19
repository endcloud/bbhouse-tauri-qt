// Offline long-season menu regression, without a decoder, network or user data.
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
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
    int qmlWarnings = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) { qmlWarnings += errors.size(); });
    engine.addImportPath(QCoreApplication::applicationDirPath());
    QQmlComponent mockComponent(&engine);
    mockComponent.setData(R"(import QtQml
        QtObject {
            property var playlist: []
            property int currentIndex: 0
            property bool kernelAvailable: true
            property bool paused: false
            property bool seasonMode: true
            property bool seekable: true
            property bool keepSpeed: false
            property bool danmakuOn: true
            property var subtitleTracks: []
            property int selectedSubtitle: -1
            function selectSubtitle(index) { selectedSubtitle = index }
            property real duration: 1500
            property real position: 30
            property real speed: 1
            property int volumePercent: 50
            property int currentQn: 80
            property string preferCodec: "avc"
            property string qualityLabel: "1080P"
            property var qualities: []
            function playByIndex(index) { currentIndex = index }
        })", QUrl());
    std::unique_ptr<QObject> controller(mockComponent.create());
    if (!controller) { qCritical() << mockComponent.errors(); return 1; }
    QVariantList episodes;
    for (int i = 0; i < 5000; ++i)
        episodes.append(QVariantMap{{"title", QString("Episode %1").arg(i + 1)}});
    controller->setProperty("playlist", episodes);
    engine.rootContext()->setContextProperty("PlayerController", controller.get());
    QQmlComponent component(&engine, QUrl::fromLocalFile(PLAYER_CONTROLS_QML));
    QElapsedTimer timer;
    timer.start();
    std::unique_ptr<QObject> object(component.create());
    auto *controls = qobject_cast<QQuickItem *>(object.get());
    if (!controls) { qCritical() << component.errors(); return 1; }
    QQuickWindow window;
    window.resize(1000, 700);
    controls->setParentItem(window.contentItem());
    controls->setWidth(1000);
    controls->setY(600);
    window.show();
    auto settle = [] { QEventLoop loop; QTimer::singleShot(120, &loop, &QEventLoop::quit); loop.exec(); };
    settle();
    int failures = 0;
    auto check = [&](bool ok, const char *label) { qInfo() << (ok ? "PASS" : "FAIL") << label; if (!ok) ++failures; };
    auto *subtitles = controls->findChild<QObject *>("playerSubtitleMenu");
    auto *off = controls->findChild<QObject *>("playerSubtitleOff");
    check(subtitles && off && off->property("checked").toBool(), "CC menu defaults to off");
    controller->setProperty("subtitleTracks", QVariantList{QVariantMap{{"label", "中文 (AI)"}}});
    QMetaObject::invokeMethod(subtitles, "open");
    settle();
    check(controls->property("menuOpen").toBool(), "CC menu keeps playback controls visible");
    controller->setProperty("selectedSubtitle", 0);
    settle();
    check(!off->property("checked").toBool(), "CC menu reflects selected AI track");
    QMetaObject::invokeMethod(off, "triggered");
    check(controller->property("selectedSubtitle").toInt() == -1, "CC off action clears selection");
    QMetaObject::invokeMethod(controls, "cancelInteraction");
    auto *menu = controls->findChild<QObject *>("playerEpisodeMenu");
    check(menu != nullptr, "real controls expose episode menu");
    if (!menu) return 1;
    auto *list = qvariant_cast<QQuickItem *>(menu->property("contentItem"));
    check(list && list->property("count").toInt() == 5000, "all 5000 episodes remain selectable");
    if (!list) return 1;
    auto delegateCount = [&] {
        auto *content = qvariant_cast<QQuickItem *>(list->property("contentItem"));
        int count = 0;
        if (content) for (auto *child : content->childItems()) if (child->property("checkable").isValid()) ++count;
        return count;
    };
    check(delegateCount() < 50, "closed menu does not instantiate the entire season");
    QMetaObject::invokeMethod(menu, "open");
    settle();
    check(delegateCount() > 0 && delegateCount() < 50, "episode menu instantiates only visible delegates");
    QMetaObject::invokeMethod(menu, "close");
    settle();
    controller->setProperty("currentIndex", 4999);
    QMetaObject::invokeMethod(menu, "open");
    settle();
    check(list->property("currentIndex").toInt() == 4999 && delegateCount() < 50,
          "jump to final episode preserves bounded delegate count");
    list->setProperty("currentIndex", 4998);
    QMetaObject::invokeMethod(list, "activateCurrent");
    settle();
    check(controller->property("currentIndex").toInt() == 4998,
          "keyboard activation switches to the selected episode");
    check(delegateCount() < 50, "closing episode menu keeps delegates bounded");
    check(qmlWarnings == 0, "long-season controls emit no QML runtime warnings");
    check(timer.elapsed() < 2500, "long-season menu operations stay below multi-second freeze threshold");
    qInfo() << "long-season menu elapsed ms" << timer.elapsed() << "delegates" << delegateCount();
    controls->setParentItem(nullptr);
    return failures ? 1 : 0;
}
