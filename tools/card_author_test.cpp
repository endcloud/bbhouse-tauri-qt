// 实际 QML 卡片的离线事件回归：无桌面操作、无网络、无真实观看记录。
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <memory>

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    QQmlComponent mockComponent(&engine);
    mockComponent.setData(R"(
        import QtQml
        QtObject {
            property var decodableImageFormats: ["png", "jpg", "jpeg"]
            property string imageTranscodeSuffix: ""
            property int playCount: 0
            function openWith(entries) { playCount++ }
        }
    )", QUrl());
    std::unique_ptr<QObject> mock(mockComponent.create());
    if (!mock) {
        qCritical() << mockComponent.errors();
        return 1;
    }
    engine.rootContext()->setContextProperty("AppController", mock.get());
    engine.rootContext()->setContextProperty("PlayerController", mock.get());
    QQmlComponent cardComponent(&engine, QUrl::fromLocalFile(CARD_QML_PATH));
    std::unique_ptr<QObject> card(cardComponent.create());
    auto *cardItem = qobject_cast<QQuickItem *>(card.get());
    auto *author = card ? card->findChild<QQuickItem *>("cardAuthorButton") : nullptr;
    auto *menu = card ? card->findChild<QObject *>("cardContextMenu") : nullptr;
    if (!cardItem || !author || !menu) {
        qCritical() << cardComponent.errors() << "missing actual card test hooks";
        return 1;
    }
    QQmlComponent observerComponent(&engine);
    observerComponent.setData(R"(
        import QtQml
        QtObject {
            id: observer
            property var card
            property int authorCount: 0
            property int coverCount: 0
            property var lastAuthor: ({})
            property Connections events: Connections {
                target: observer.card
                function onAuthorClicked(author) {
                    observer.authorCount++
                    observer.lastAuthor = author
                }
                function onCoverClicked(sourceItem) { observer.coverCount++ }
            }
        }
    )", QUrl());
    std::unique_ptr<QObject> observer(observerComponent.createWithInitialProperties(
            {{"card", QVariant::fromValue(card.get())}}));
    if (!observer) {
        qCritical() << observerComponent.errors();
        return 1;
    }
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        failures += !ok;
    };
    // 仅执行 URL 纯函数，不让 Image 请求真实 CDN；覆盖带旧转码、查询与非 CDN 地址。
    QFile formatFile(QFileInfo(QStringLiteral(CARD_QML_PATH)).dir().filePath("../js/Format.js"));
    check(formatFile.open(QIODevice::ReadOnly), "cover URL helper is available");
    QString formatSource = QString::fromUtf8(formatFile.readAll());
    formatSource.replace(".pragma library", "");
    QJSEngine formatEngine;
    const auto result = formatEngine.evaluate(formatSource);
    check(!result.isError(), "cover URL helper evaluates without UI or network");
    auto thumbnail = [&](const QString &url) {
        return formatEngine.globalObject().property("cardCoverThumbnailUrl").call({url}).toString();
    };
    const QString original = "https://i0.hdslb.com/bfs/archive/fixture.jpg";
    const QString suffix = "@400w_225h_1c.webp";
    check(thumbnail(original) == original + suffix, "video covers request fixed 400x225 WebP");
    check(thumbnail(original + "@672w_378h_1c.avif") == original + suffix,
          "existing CDN transform is replaced instead of appended");
    check(thumbnail(original + suffix) == original + suffix, "fixed cover transform is idempotent");
    auto seasonCover = [&](const QString &url) {
        return formatEngine.globalObject().property("seasonCoverUrl").call({url}).toString();
    };
    check(seasonCover(original) == original + "@.webp" &&
          seasonCover(original + suffix) == original + "@.webp" &&
          seasonCover(original + "@.webp") == original + "@.webp",
          "season posters convert format only and remove old crop/size parameters");
    check(seasonCover(original + "@400w_225h_1c.webp?fixture=a@b#preview") ==
              original + "@.webp?fixture=a@b#preview",
          "season poster format conversion preserves query and fragment");
    check(thumbnail(original + "@600w.jpg?fixture=a@b#preview") == original + suffix + "?fixture=a@b#preview",
          "query and fragment are preserved independently from transform");
    check(thumbnail("//i1.hdslb.com/fixture.png") == "https://i1.hdslb.com/fixture.png" + suffix &&
          thumbnail("https://i1.biliimg.com/fixture.png") == "https://i1.biliimg.com/fixture.png" + suffix,
          "protocol-relative and alternate Bilibili CDN covers use the same transform");
    for (const auto &url : {QString(), QStringLiteral("qrc:/images/noface.jpg"),
            QStringLiteral("file:///offline/cover.jpg"), QStringLiteral("data:image/png;base64,AAAA"),
            QStringLiteral("https://example.invalid/cover.jpg"),
            QStringLiteral("https://hdslb.com.example.invalid/cover.jpg")}) {
        check(thumbnail(url) == url, "non-CDN and empty covers remain unchanged");
        check(seasonCover(url) == url, "non-CDN season covers remain unchanged");
    }
    check(formatEngine.globalObject().property("stripImageTranscode").call({original + suffix}).toString() == original,
          "full-size preview continues to use the original image URL");
    auto count = [&] { return observer->property("authorCount").toInt(); };
    auto setCard = [&](const QString &business, const QVariant &mid) {
        card->setProperty("cardItem", QVariantMap{
            {"business", business}, {"oid", 100}, {"title", "Offline video"},
            {"authorName", "Offline author"}, {"authorMid", mid},
            {"faceUrl", "https://example.invalid/avatar.png"}});
    };
    setCard("archive", "9007199254740993");
    QMetaObject::invokeMethod(card.get(), "openAuthor");
    const auto payload = qvariant_cast<QJSValue>(observer->property("lastAuthor"));
    check(count() == 1 && payload.property("mid").toString() == "9007199254740993" &&
          payload.property("name").toString() == "Offline author" &&
          payload.property("faceUrl").toString() == "https://example.invalid/avatar.png",
          "valid archive opens displayed author with exact mid and avatar");
    card->setProperty("authorNavigationEnabled", false);
    QMetaObject::invokeMethod(card.get(), "openAuthor");
    check(count() == 1 && !author->activeFocusOnTab(),
          "Special Follow opt-out prevents navigation and keyboard tab focus");
    card->setProperty("authorNavigationEnabled", true);
    for (const auto &invalidMid : {QString(), QStringLiteral("0"), QStringLiteral("-1"),
                                   QStringLiteral("not-a-mid")}) {
        setCard("archive", invalidMid);
        QMetaObject::invokeMethod(card.get(), "openAuthor");
    }
    check(count() == 1, "missing and invalid mid cannot open a space");
    setCard("article", "42");
    QMetaObject::invokeMethod(card.get(), "openAuthor");
    check(count() == 1, "non-video author line cannot open a space");
    setCard("pgc", "42");
    QMetaObject::invokeMethod(card.get(), "openAuthor");
    check(count() == 2, "PGC author with a valid mid can open a space");

    setCard("archive", "9007199254740993");
    QQuickWindow window;
    window.setGeometry(0, 0, 400, 500);
    cardItem->setParentItem(window.contentItem());
    cardItem->setHeight(cardItem->implicitHeight());
    window.show();
    app.processEvents();
    author->forceActiveFocus(Qt::TabFocusReason);
    check(author->hasActiveFocus(), "author accepts keyboard focus in actual card");
    auto key = [&](int keyCode, bool repeat = false) {
        QKeyEvent press(QEvent::KeyPress, keyCode, Qt::NoModifier, QString(), repeat);
        QKeyEvent release(QEvent::KeyRelease, keyCode, Qt::NoModifier, QString(), repeat);
        QCoreApplication::sendEvent(&window, &press);
        QCoreApplication::sendEvent(&window, &release);
    };
    key(Qt::Key_Enter);
    check(count() == 3, "Enter emits exactly one author activation");
    key(Qt::Key_Return);
    key(Qt::Key_Space);
    check(count() == 5, "Return and Space each activate the author once");
    key(Qt::Key_Space, true);
    check(count() == 5, "key auto-repeat does not reopen the author");
    auto click = [&](Qt::MouseButton button) {
        const QPointF local = author->mapToScene(QPointF(author->width() / 2, author->height() / 2));
        const QPointF global = window.mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global, button, button, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press);
        QCoreApplication::sendEvent(&window, &release);
    };
    click(Qt::LeftButton);
    check(count() == 6, "author pointer click emits exactly once");
    click(Qt::RightButton);
    check(count() == 6 && !menu->property("visible").toBool(),
          "author right-click does not navigate or open video context menu");
    check(mock->property("playCount").toInt() == 0 && observer->property("coverCount").toInt() == 0,
          "author activation never triggers playback or cover preview");
    window.contentItem()->forceActiveFocus();
    const QVariantMap saved{{"business", "archive"}, {"oid", "100"}, {"title", "Saved video"},
        {"viewCount", 1}, {"viewRecords", QVariantList{1700000000}},
        {"recordedViews", QVariantList{QVariantMap{{"viewAt", 1700000000}, {"progress", 65}}}}};
    card->setProperty("cardItem", saved);
    auto *badge = card->findChild<QQuickItem *>("historyRecordedBadge");
    check(badge && !badge->isVisible(), "single view badge stays hidden by default on other pages");
    card->setProperty("showRecordedBadge", true);
    check(badge && badge->isVisible() && card->property("viewTimestampsTooltip").toString().contains("1:05"),
          "local saved badge includes a single recorded watch and its progress");
    cardItem->setParentItem(nullptr);
    return failures ? 1 : 0;
}
