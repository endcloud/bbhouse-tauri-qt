// Real QML, in-memory controller doubles, blocked networking and no user data access.
#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QJSValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QTimer>
#include <memory>

class BlockedReply final : public QNetworkReply {
public:
    explicit BlockedReply(QObject *parent) : QNetworkReply(parent) {
        open(QIODevice::ReadOnly);
        setError(QNetworkReply::OperationCanceledError, "Networking disabled in download page regression");
        QTimer::singleShot(0, this, [this] { setFinished(true); emit finished(); });
    }
    void abort() override {}
    qint64 readData(char *, qint64) override { return -1; }
};
class OfflineManager final : public QNetworkAccessManager {
public:
    OfflineManager(int *requests, QObject *parent) : QNetworkAccessManager(parent), requests_(requests) {}
    QNetworkReply *createRequest(Operation, const QNetworkRequest &, QIODevice *) override {
        ++*requests_;
        return new BlockedReply(this);
    }
private:
    int *requests_;
};
class OfflineFactory final : public QQmlNetworkAccessManagerFactory {
public:
    int requests = 0;
    QNetworkAccessManager *create(QObject *parent) override { return new OfflineManager(&requests, parent); }
};
static QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems()) if (auto *found = visualChild(child, name)) return found;
    return nullptr;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    OfflineFactory offline;
    QQmlEngine engine;
    engine.setNetworkAccessManagerFactory(&offline);
    int warnings = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        warnings += errors.size();
        for (const auto &error : errors) qWarning().noquote() << error.toString();
    });
    engine.addImportPath(QCoreApplication::applicationDirPath());
    const QString root = QStringLiteral(DOWNLOAD_QML_ROOT);
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/HistoryCard.qml"), "bbhouse", 1, 0, "HistoryCard");
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/CoverPreviewOverlay.qml"), "bbhouse", 1, 0, "CoverPreviewOverlay");
    qmlRegisterType(QUrl::fromLocalFile(root + "/controls/DownloadDialog.qml"), "bbhouse", 1, 0, "DownloadDialog");
    QQmlComponent stubComponent(&engine);
    stubComponent.setData(R"(
        import QtQml
        QtObject {
            property var downloading: []
            property var library: []
            property string error: ""
            property string downloadDirectory: "/fixture/Downloads"
            property bool downloadVideo: true
            property bool downloadAudio: true
            property bool downloadDanmaku: true
            property bool downloadSubtitles: true
            property int preferredQn: 80
            property int refreshCount: 0
            property int enqueueCount: 0
            property var enqueued: ({})
            property string cancelledId: ""
            property string retriedId: ""
            property int removeCount: 0
            property string removedId: ""
            property bool removedFiles: false
            property var decodableImageFormats: ["jpg", "png"]
            property string imageTranscodeSuffix: ".jpg"
            signal coverDownloadFinished(string savedPath)
            signal taskProgress(string id, string message, int progress)
            function removeRecord(id, deleteFiles) {
                removeCount++
                removedId = id
                removedFiles = deleteFiles
            }
            function refreshLibrary() { refreshCount++ }
            function importFiles(urls) {}
            function localEntry(id) { return ({}) }
            function enqueue(entry) { enqueued = entry; enqueueCount++ }
            function cancel(id) {
                cancelledId = id
                downloading = downloading.map(function(entry) {
                    return entry.id === id ? Object.assign({}, entry, {state: "canceled"}) : entry
                })
            }
            function retry(id) {
                retriedId = id
                downloading = downloading.map(function(entry) {
                    return entry.id === id ? Object.assign({}, entry, {state: "queued"}) : entry
                })
            }
        }
    )", QUrl());
    std::unique_ptr<QObject> stub(stubComponent.create());
    if (!stub) { qCritical() << stubComponent.errors(); return 1; }
    for (const auto *name : {"DownloadController", "AppController", "HistoryController", "PlayerController"})
        engine.rootContext()->setContextProperty(name, stub.get());
    QQmlComponent component(&engine, QUrl::fromLocalFile(root + "/pages/DownloadsPage.qml"));
    std::unique_ptr<QObject> object(component.create());
    auto *page = qobject_cast<QQuickItem *>(object.get());
    if (!page) { qCritical() << component.errors(); return 1; }
    QQuickWindow window;
    window.resize(1000, 700);
    page->setParentItem(window.contentItem());
    page->setSize(QSizeF(1000, 700));
    window.show();
    auto settle = [] {
        QEventLoop loop;
        QTimer::singleShot(120, &loop, &QEventLoop::quit);
        loop.exec();
    };
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) ++failures;
    };
    auto *cards = page->findChild<QObject *>("downloadCards");
    auto count = [&] { return cards ? cards->property("count").toInt() : -1; };
    auto card = [](int n, const QString &state = "queued") {
        return QVariantMap{{"id", QString::number(n)}, {"videoKey", QString("archive:%1:0").arg(n)},
            {"business", "archive"}, {"oid", QString::number(n)},
            {"title", QString("Video %1").arg(n)}, {"authorName", "Fixture Author"},
            {"coverUrl", ""}, {"state", state}, {"progress", 35}, {"message", "Fixture status"},
            {"available", false}, {"localPath", ""}};
    };
    settle();
    auto *empty = visualChild(page, "downloadEmptyState");
    check(count() == 0 && empty && empty->isVisible(), "empty download queue renders its empty state");
    check(stub->property("refreshCount").toInt() > 0, "page creation requests local file validation");
    auto *importButton = visualChild(page, "downloadImportButton");
    check(importButton && qAbs(importButton->mapToItem(page, QPointF(importButton->width(), 0)).x() - 976) < 1,
          "import button respects the 24 pixel right margin");
    QVariantList tasks;
    for (int n = 1; n <= 35; ++n) tasks.append(card(n));
    stub->setProperty("downloading", tasks);
    settle();
    check(count() == 30 && page->property("totalPages").toInt() == 2, "queue paginates locally at thirty cards");
    QMetaObject::invokeMethod(page, "selectPage", Q_ARG(QVariant, 2));
    settle();
    check(count() == 5, "second queue page renders remaining tasks");
    page->setProperty("searchQuery", "Video 35");
    settle();
    check(count() == 1 && page->property("pageIndex").toInt() == 1, "search includes tasks outside current page and resets pagination");
    page->setProperty("searchQuery", "not-found");
    settle();
    check(count() == 0 && empty->isVisible(), "unmatched search uses an empty state");
    page->setProperty("searchQuery", "Video 35");
    settle();
    QPointer<QQuickItem> stableCard = visualChild(page, "downloadCard");
    auto *progress = visualChild(page, "downloadProgressBar");
    check(progress && progress->property("value").toInt() == 35, "task progress initializes from persisted data");
    QMetaObject::invokeMethod(stub.get(), "taskProgress", Q_ARG(QString, "35"),
                              Q_ARG(QString, "Downloading 73%"), Q_ARG(int, 73));
    settle();
    check(stableCard && stableCard == visualChild(page, "downloadCard") && progress
          && progress->property("value").toInt() == 73 && !progress->property("indeterminate").toBool()
          && stableCard->property("statisticsText").toString() == "Downloading 73%",
          "live progress and message update without recreating the delegate");
    QMetaObject::invokeMethod(stub.get(), "taskProgress", Q_ARG(QString, "other"),
                              Q_ARG(QString, "Unrelated task"), Q_ARG(int, 2));
    check(progress && progress->property("value").toInt() == 73, "progress signals are scoped to their task id");
    QMetaObject::invokeMethod(stub.get(), "taskProgress", Q_ARG(QString, "35"),
                              Q_ARG(QString, "Merging"), Q_ARG(int, -1));
    settle();
    check(progress && progress->property("indeterminate").toBool() && stableCard
          && stableCard->property("statisticsText").toString() == "Merging",
          "uncountable work switches the live progress bar to indeterminate");

    auto *removeMenu = page->findChild<QObject *>("downloadContextMenu");
    auto *removeDialog = page->findChild<QObject *>("downloadRemoveDialog");
    if (stableCard) QMetaObject::invokeMethod(stableCard, "openContextMenu");
    settle();
    auto *defaultMenu = stableCard ? stableCard->findChild<QObject *>("cardContextMenu") : nullptr;
    check(removeMenu && removeMenu->property("visible").toBool() && removeMenu->property("count").toInt() == 1
          && defaultMenu && !defaultMenu->property("visible").toBool(),
          "download right-click opens only the independent one-action menu");
    auto *removeAction = page->findChild<QObject *>("downloadRemoveAction");
    check(removeAction && removeAction->property("text").toString() == QString::fromUtf8("移除"),
          "the download context menu action is remove");
    if (removeAction) QMetaObject::invokeMethod(removeAction, "clicked");
    if (removeMenu) QMetaObject::invokeMethod(removeMenu, "close");
    settle();
    auto *removeFiles = visualChild(window.contentItem(), "downloadRemoveFiles");
    check(removeDialog && removeDialog->property("visible").toBool() && !removeDialog->property("deleteFiles").toBool()
          && removeFiles && !removeFiles->property("checked").toBool() && stub->property("removeCount").toInt() == 0,
          "remove opens a confirmation with physical deletion unchecked");
    auto clickDialogButton = [&](const QString &text) {
        const auto buttons = removeDialog ? removeDialog->findChildren<QQuickItem *>() : QList<QQuickItem *>{};
        for (auto *button : buttons) {
            if (button->isVisible() && button->property("text").toString() == text
                && button->metaObject()->indexOfSignal("clicked()") >= 0) {
                return QMetaObject::invokeMethod(button, "clicked");
            }
        }
        return false;
    };
    check(clickDialogButton(QString::fromUtf8("取消")), "confirmation exposes a working cancel button");
    settle();
    check(stub->property("removeCount").toInt() == 0 && removeDialog && !removeDialog->property("visible").toBool(),
          "canceling confirmation does not remove a record");
    if (removeAction) QMetaObject::invokeMethod(removeAction, "clicked");
    settle();
    check(clickDialogButton(QString::fromUtf8("移除")), "confirmation exposes a working remove button");
    settle();
    check(stub->property("removeCount").toInt() == 1 && stub->property("removedId").toString() == "35"
          && !stub->property("removedFiles").toBool(), "default confirmation removes only the selected record");
    if (removeAction) QMetaObject::invokeMethod(removeAction, "clicked");
    settle();
    removeFiles = visualChild(window.contentItem(), "downloadRemoveFiles");
    if (removeFiles) QMetaObject::invokeMethod(removeFiles, "clicked");
    check(removeDialog && removeDialog->property("deleteFiles").toBool(), "physical deletion is explicitly selectable");
    check(clickDialogButton(QString::fromUtf8("移除")), "explicit file deletion can be confirmed");
    settle();
    check(stub->property("removeCount").toInt() == 2 && stub->property("removedId").toString() == "35"
          && stub->property("removedFiles").toBool(), "checked confirmation passes physical deletion to the backend");
    if (removeAction) QMetaObject::invokeMethod(removeAction, "clicked");
    settle();
    removeFiles = visualChild(window.contentItem(), "downloadRemoveFiles");
    check(removeDialog && !removeDialog->property("deleteFiles").toBool() && removeFiles
          && !removeFiles->property("checked").toBool(), "each confirmation resets physical deletion to unchecked");
    clickDialogButton(QString::fromUtf8("取消"));
    settle();
    auto *cancel = visualChild(page, "downloadCancelButton");
    check(cancel && cancel->isVisible(), "active task exposes cancellation");
    if (cancel) QMetaObject::invokeMethod(cancel, "clicked");
    settle();
    auto *retry = visualChild(page, "downloadRetryButton");
    cancel = visualChild(page, "downloadCancelButton");
    check(stub->property("cancelledId").toString() == "35" && retry && retry->isVisible() && cancel && !cancel->isVisible(),
          "backend canceled state replaces cancel with retry");
    if (retry) QMetaObject::invokeMethod(retry, "clicked");
    settle();
    cancel = visualChild(page, "downloadCancelButton");
    check(stub->property("retriedId").toString() == "35" && cancel && cancel->isVisible(), "retry routes the exact task id and returns to queued actions");
    auto ready = card(99, "completed");
    ready["available"] = true;
    ready["localPath"] = "/fixture/Video 99.mp4";
    auto missing = card(100, "completed");
    missing["availabilityMessage"] = "Fixture missing media";
    stub->setProperty("library", QVariantList{ready, missing});
    page->setProperty("searchQuery", "");
    page->setProperty("showingLibrary", true);
    settle();
    check(count() == 2, "media library displays completed records separately from queue");
    auto *readyCard = visualChild(page, "downloadCard");
    check(readyCard && readyCard->property("playable").toBool(), "validated local media enables built-in playback");
    page->setProperty("searchQuery", "Video 100");
    settle();
    auto *missingCard = visualChild(page, "downloadCard");
    check(count() == 1 && missingCard && !missingCard->property("playable").toBool(), "missing local media remains visible but cannot play");
    check(missingCard && missingCard->property("displaySubtitle").toString() == "Fixture missing media", "unavailable media displays its diagnostic");
    page->setProperty("searchQuery", "fixture author");
    settle();
    check(count() == 2, "library search is case-insensitive and includes author names");
    page->setProperty("searchQuery", "");
    stub->setProperty("library", QVariantList{});
    settle();
    check(count() == 0 && empty->isVisible(), "empty library renders import guidance");

    QQmlComponent ordinaryCardComponent(&engine, QUrl::fromLocalFile(root + "/controls/HistoryCard.qml"));
    std::unique_ptr<QObject> ordinaryCardObject(ordinaryCardComponent.create());
    auto *ordinaryCard = qobject_cast<QQuickItem *>(ordinaryCardObject.get());
    if (ordinaryCard) {
        ordinaryCard->setParentItem(window.contentItem());
        ordinaryCard->setProperty("cardItem", card(66));
        QMetaObject::invokeMethod(ordinaryCard, "openContextMenu");
    }
    settle();
    auto *ordinaryMenu = ordinaryCard ? ordinaryCard->findChild<QObject *>("cardContextMenu") : nullptr;
    check(ordinaryCard && !ordinaryCard->property("contextMenuOverride").toBool() && ordinaryMenu
          && ordinaryMenu->property("visible").toBool() && ordinaryMenu->property("count").toInt() == 2,
          "ordinary video cards retain their original download and open-link menu");
    if (ordinaryMenu) QMetaObject::invokeMethod(ordinaryMenu, "close");
    if (ordinaryCard) ordinaryCard->setParentItem(nullptr);
    settle();

    QQmlComponent dialogHostComponent(&engine);
    dialogHostComponent.setData(R"(
        import QtQuick
        import bbhouse 1.0
        Item {
            property alias dialog: downloadDialog
            DownloadDialog { id: downloadDialog; objectName: "downloadDialog" }
            function show(entry) { downloadDialog.showFor(entry) }
            function submit() { downloadDialog.onPositiveClickListener() }
        }
    )", QUrl());
    std::unique_ptr<QObject> dialogHostObject(dialogHostComponent.create());
    auto *host = qobject_cast<QQuickItem *>(dialogHostObject.get());
    if (!host) { qCritical() << dialogHostComponent.errors(); return 1; }
    host->setParentItem(window.contentItem());
    host->setSize(QSizeF(1000, 700));
    auto *dialog = host->findChild<QObject *>("downloadDialog");
    QMetaObject::invokeMethod(host, "show", Q_ARG(QVariant, QVariant(card(77))));
    settle();
    check(dialog && dialog->property("visible").toBool() && dialog->property("includeVideo").toBool()
          && !dialog->property("includeAudio").toBool() && dialog->property("includeDanmaku").toBool()
          && dialog->property("includeSubtitles").toBool(), "download dialog normalizes legacy defaults to video with attachments");
    auto clickOption = [&](const char *name) {
        auto *option = visualChild(window.contentItem(), QString::fromLatin1(name));
        check(option != nullptr, "real dialog exposes content option");
        if (option) QMetaObject::invokeMethod(option, "clicked");
    };
    auto *quality = visualChild(window.contentItem(), "downloadQuality");
    check(quality && quality->isEnabled() && quality->property("currentIndex").toInt() == 1
          && dialog->property("selectedQn").toInt() == 80, "dialog quality initializes from saved preference");
    clickOption("downloadOptionAudio");
    check(!dialog->property("includeVideo").toBool() && dialog->property("includeAudio").toBool()
          && quality && !quality->isEnabled(), "selecting audio-only deselects video and disables video quality");
    clickOption("downloadOptionVideo");
    check(dialog->property("includeVideo").toBool() && !dialog->property("includeAudio").toBool()
          && quality && quality->isEnabled(), "selecting video deselects audio-only and restores video quality");
    if (quality) {
        quality->setProperty("currentIndex", 2);
        QMetaObject::invokeMethod(quality, "activated", Q_ARG(int, 2));
    }
    clickOption("downloadOptionDanmaku");
    clickOption("downloadOptionSubtitles");
    QMetaObject::invokeMethod(host, "submit");
    settle();
    auto enqueuedEntry = [&] {
        const auto value = stub->property("enqueued");
        auto entry = value.value<QJSValue>().toVariant().toMap();
        return entry.isEmpty() ? value.toMap() : entry;
    };
    auto enqueued = enqueuedEntry();
    auto options = enqueued.value("downloadOptions").toMap();
    check(stub->property("enqueueCount").toInt() == 1 && enqueued.value("id").toString() == "77"
          && options.value("video").toBool() && !options.value("audio").toBool()
          && !options.value("danmaku").toBool() && !options.value("subtitles").toBool()
          && options.value("qn").toInt() == 64, "single video download submits the selected quality and content");
    check(stub->property("downloadVideo").toBool() && stub->property("downloadDanmaku").toBool()
          && stub->property("preferredQn").toInt() == 80, "single download choices do not overwrite saved defaults or quality");
    QMetaObject::invokeMethod(host, "show", Q_ARG(QVariant, QVariant(card(78))));
    settle();
    quality = visualChild(window.contentItem(), "downloadQuality");
    check(dialog->property("selectedQn").toInt() == 80 && quality && quality->property("currentIndex").toInt() == 1,
          "reopening the dialog restores the saved quality preference");
    clickOption("downloadOptionVideo");
    check(!dialog->property("includeVideo").toBool() && !dialog->property("includeAudio").toBool()
          && dialog->property("includeDanmaku").toBool(), "media options can both be off for attachment-only downloads");
    clickOption("downloadOptionAudio");
    clickOption("downloadOptionDanmaku");
    clickOption("downloadOptionSubtitles");
    QMetaObject::invokeMethod(host, "submit");
    settle();
    enqueued = enqueuedEntry();
    options = enqueued.value("downloadOptions").toMap();
    check(stub->property("enqueueCount").toInt() == 2 && enqueued.value("id").toString() == "78"
          && !options.value("video").toBool() && options.value("audio").toBool()
          && !options.value("danmaku").toBool() && !options.value("subtitles").toBool(),
          "audio-only selection submits without video or attachments");
    QMetaObject::invokeMethod(host, "show", Q_ARG(QVariant, QVariant(card(79))));
    settle();
    for (const auto *name : {"includeVideo", "includeAudio", "includeDanmaku", "includeSubtitles"}) dialog->setProperty(name, false);
    QMetaObject::invokeMethod(host, "submit");
    settle();
    check(stub->property("enqueueCount").toInt() == 2 && dialog->property("visible").toBool()
          && !dialog->property("validationMessage").toString().isEmpty(), "empty content selection keeps dialog open and does not enqueue");
    QMetaObject::invokeMethod(dialog, "close");
    settle();
    check(offline.requests == 0, "offline page and dialog do not request network resources");
    check(warnings == 0, "download page and dialog have no QML runtime warnings");
    host->setParentItem(nullptr);
    page->setParentItem(nullptr);
    return failures ? 1 : 0;
}
