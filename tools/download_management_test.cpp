#include "downloads/DownloadController.h"
#include "downloads/DownloadStore.h"
#include "downloads/DownloadUtils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

namespace {
bool put(const QString &path, const QByteArray &bytes = "isolated fixture") {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool waitUntil(const std::function<bool()> &ready, int timeout = 4000) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(5);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(); deadline.start(timeout); loop.exec();
    return ready();
}
QVariantMap savedRecord(const QString &id, const QString &base, const QString &state = "completed") {
    return {{"id", id}, {"title", id}, {"createdAt", "2026-01-01"},
        {"state", state}, {"business", "local"}, {"progress", 100},
        {"localPath", base + ".mp4"}, {"danmakuPath", base + ".xml"},
        {"subtitlePaths", QStringList{base + ".srt", base + ".en.srt"}}};
}
QStringList mediaFiles(const QString &base) {
    return {base + ".mp4", base + ".xml", base + ".srt", base + ".en.srt"};
}
bool allExist(const QStringList &paths) {
    for (const auto &path : paths) if (!QFileInfo::exists(path)) return false;
    return true;
}
bool noneExist(const QStringList &paths) {
    for (const auto &path : paths) if (QFileInfo::exists(path)) return false;
    return true;
}
void configureQueue(DownloadController &controller, const QString &directory) {
    controller.setDownloadDirectory(directory);
    controller.setAria2Path(QCoreApplication::applicationFilePath());
    controller.setFfmpegPath(QCoreApplication::applicationFilePath());
}
QVariantMap queueEntry() {
    return {{"business", "archive"}, {"oid", "123"}, {"cid", "456"}, {"title", "fixture"},
        {"downloadOptions", QVariantMap{{"video", true}, {"audio", false},
            {"danmaku", false}, {"subtitles", false}}}};
}
}

int runDownloadManagementTests() {
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/download-management-XXXXXX");
    if (!temp.isValid()) return 1;
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        qInfo() << (ok ? "PASS" : "FAIL") << name;
        failures += !ok;
    };

    for (const bool deleteFiles : {false, true}) {
        const QString directory = temp.filePath(deleteFiles ? "delete" : "retain");
        QDir().mkpath(directory);
        const QString base = QDir(directory).filePath("media");
        for (const auto &path : mediaFiles(base)) check(put(path), "write isolated removal fixture");
        const QString unrelated = QDir(directory).filePath("other-video.xml");
        check(put(unrelated), "write unrelated neighboring file");
        DownloadStore store(QDir(directory).filePath("downloads.sqlite"));
        store.initialize(); store.save(savedRecord("remove-me", base, "imported"));
        {
            DownloadController controller(directory);
            if (deleteFiles) controller.removeRecord("remove-me", true);
            else controller.removeRecord("remove-me"); // The API default must retain files.
            check(waitUntil([&] { return controller.library().isEmpty(); }),
                deleteFiles ? "physical removal completes asynchronously" : "default record removal completes asynchronously");
            check(controller.error().isEmpty(), "successful record removal has no error");
        }
        check(store.load().isEmpty(), "record removal persists after controller destruction");
        check(deleteFiles ? noneExist(mediaFiles(base)) : allExist(mediaFiles(base)),
            deleteFiles ? "explicit physical removal deletes exact media and tracked sidecars"
                        : "default removal preserves media XML and SRT files");
        check(QFileInfo::exists(unrelated), "physical removal preserves unrelated neighboring files");
    }

    for (int scenario = 0; scenario < 5; ++scenario) {
        const QString directory = temp.filePath(QStringLiteral("owned-folder-%1").arg(scenario));
        const QString base = DownloadUtils::reserveBase(directory, QStringLiteral("视频"));
        const QString folder = QFileInfo(base).absolutePath();
        auto record = savedRecord("owned-folder", base, scenario == 2 ? "imported" : "completed");
        record["business"] = scenario == 2 ? "local" : "archive";
        record["basePath"] = scenario == 4 ? folder + "/different-base" : base;
        for (const auto &path : mediaFiles(base)) check(put(path), "write owned task directory fixture");
        if (scenario == 3) check(put(folder + "/.unrelated"), "write hidden unrelated task neighbor");
        DownloadStore store(QDir(directory).filePath("downloads.sqlite"));
        store.initialize(); store.save(record);
        {
            DownloadController controller(directory);
            controller.removeRecord("owned-folder", scenario != 1);
            check(waitUntil([&] { return controller.library().isEmpty(); }) && controller.error().isEmpty(),
                  "owned-directory record removal completes");
        }
        check(QFileInfo::exists(folder) == (scenario != 0),
              "only physical removal of an empty matching download folder removes its directory");
        check(QFileInfo(directory).isDir() && store.load().isEmpty(),
              "download root survives and record removal persists");
        if (scenario == 1) check(allExist(mediaFiles(base)), "record-only removal retains directory and media");
        if (scenario == 3) check(QFileInfo::exists(folder + "/.unrelated"), "nonempty task folder retains hidden unrelated files");
    }

    {
        const QString directory = temp.filePath("shared");
        QDir().mkpath(directory);
        const QString base = QDir(directory).filePath("shared-media");
        for (const auto &path : mediaFiles(base)) check(put(path), "write shared media fixture");
        DownloadStore store(QDir(directory).filePath("downloads.sqlite"));
        store.initialize();
        store.save(savedRecord("first-reference", base));
        store.save(savedRecord("second-reference", base));
        {
            DownloadController controller(directory);
            controller.removeRecord("first-reference", true);
            check(waitUntil([&] { return !controller.error().isEmpty(); })
                && controller.library().size() == 2 && allExist(mediaFiles(base)),
                "shared media deletion is rejected before touching any file or record");
        }
        check(store.load().size() == 2, "rejected shared-file removal preserves both persisted references");
    }

    {
        const QString directory = temp.filePath("progress");
        QDir().mkpath(directory);
        const QString finalMedia = QDir(directory).filePath("completed.mp4");
        check(put(finalMedia), "write fake runner final media");
        std::atomic_bool mayFinish{false}, runnerOnWorker{false};
        auto runner = [&](QVariantMap record, const std::shared_ptr<std::atomic_bool> &canceled,
                          const DownloadWorker::Progress &progress, const QNetworkProxy &) {
            runnerOnWorker.store(QThread::currentThread() != QCoreApplication::instance()->thread());
            for (int value = 1; value <= 20 && !canceled->load(); ++value) {
                progress(QStringLiteral("fixture progress"), value);
                QThread::msleep(20);
            }
            QElapsedTimer deadline; deadline.start();
            while (!mayFinish.load() && !canceled->load() && deadline.elapsed() < 4000) QThread::msleep(5);
            record["state"] = "completed"; record["progress"] = 100;
            record["localPath"] = finalMedia;
            return record;
        };
        {
            DownloadController controller(directory, nullptr, runner);
            configureQueue(controller, directory);
            int listChanges = 0, progressSignals = 0, heartbeats = 0;
            bool memoryMatchesSignal = true, signalOnUiThread = true;
            QObject::connect(&controller, &DownloadController::recordsChanged, &controller, [&] { ++listChanges; });
            QObject::connect(&controller, &DownloadController::taskProgress, &controller,
                [&](const QString &id, const QString &message, int percent) {
                    ++progressSignals;
                    signalOnUiThread &= QThread::currentThread() == QCoreApplication::instance()->thread();
                    const auto rows = controller.downloading();
                    memoryMatchesSignal &= rows.size() == 1 && rows.first().toMap().value("id") == id
                        && rows.first().toMap().value("message") == message
                        && rows.first().toMap().value("progress").toInt() == percent;
                });
            QTimer heartbeat;
            heartbeat.setInterval(10);
            QObject::connect(&heartbeat, &QTimer::timeout, &controller, [&] { ++heartbeats; });
            heartbeat.start();
            // Hold a real SQLite writer lock: enqueuing and progress must stay
            // responsive while the serialized storage worker waits for it.
            const QString connectionName = "download-management-writer-lock";
            {
                QSqlDatabase blocker = QSqlDatabase::addDatabase("QSQLITE", connectionName);
                blocker.setDatabaseName(QDir(directory).filePath("downloads.sqlite"));
                check(blocker.open(), "open isolated persistence lock connection");
                QSqlQuery query(blocker);
                check(query.exec("BEGIN IMMEDIATE"), "hold isolated SQLite writer lock");
                QElapsedTimer enqueueTime; enqueueTime.start();
                controller.enqueue(queueEntry());
                check(enqueueTime.elapsed() < 500, "enqueue does not wait for blocked SQLite persistence");
                const int baseline = listChanges;
                const bool observed = waitUntil([&] { return progressSignals >= 8; });
                check(observed && listChanges == baseline && memoryMatchesSignal,
                    "live progress updates memory and task signal without rebuilding record lists");
                check(runnerOnWorker.load() && signalOnUiThread && heartbeats >= 5,
                    "worker progress reaches UI thread while event-loop heartbeat continues under database contention");
                check(query.exec("COMMIT"), "release isolated SQLite writer lock");
                blocker.close();
            }
            QSqlDatabase::removeDatabase(connectionName);
            mayFinish.store(true);
            check(waitUntil([&] { return controller.library().size() == 1; }), "fake runner completion enters library");
        }
        DownloadStore store(QDir(directory).filePath("downloads.sqlite"));
        const auto saved = store.load();
        check(saved.size() == 1 && saved.first().toMap().value("state") == "completed"
            && saved.first().toMap().value("progress").toInt() == 100,
            "serialized asynchronous persistence saves final state after earlier queue snapshots");
    }

    {
        const QString directory = temp.filePath("active-removal");
        QDir().mkpath(directory);
        std::atomic_bool started{false}, sawCancel{false}, allowFinish{false}, filesWritten{true};
        auto runner = [&](QVariantMap record, const std::shared_ptr<std::atomic_bool> &canceled,
                          const DownloadWorker::Progress &progress, const QNetworkProxy &) {
            const QString base = record.value("basePath").toString();
            filesWritten.store(put(base + ".video.m4s") && put(base + ".audio.m4s.aria2"));
            started.store(true);
            QElapsedTimer deadline; deadline.start();
            while (!allowFinish.load() && deadline.elapsed() < 4000) {
                if (canceled->load()) sawCancel.store(true);
                QThread::msleep(5);
            }
            const QString actualBase = base + "_actual-final";
            for (const auto &path : mediaFiles(actualBase)) if (!put(path)) filesWritten.store(false);
            record["localPath"] = actualBase + ".mp4";
            record["danmakuPath"] = actualBase + ".xml";
            record["subtitlePaths"] = QStringList{actualBase + ".srt", actualBase + ".en.srt"};
            record["state"] = "completed"; record["progress"] = 100;
            progress(QStringLiteral("late progress"), 77);
            return record; // Cancellation can race with a successful merge.
        };
        QString base;
        {
            DownloadController controller(directory, nullptr, runner);
            configureQueue(controller, directory);
            controller.enqueue(queueEntry());
            check(waitUntil([&] { return started.load(); }), "active fake runner begins before removal");
            const auto pending = controller.downloading();
            if (pending.isEmpty()) { allowFinish.store(true); check(false, "active record exists"); }
            else {
                const auto record = pending.first().toMap();
                base = record.value("basePath").toString();
                const QString unrelated = QFileInfo(base).dir().filePath("unrelated.keep");
                check(put(unrelated), "write unrelated active-download neighbor");
                int lateProgressSignals = 0;
                QObject::connect(&controller, &DownloadController::taskProgress, &controller,
                    [&](const QString &, const QString &, int) { ++lateProgressSignals; });
                controller.removeRecord(record.value("id").toString(), true);
                check(waitUntil([&] { return sawCancel.load(); }) && QFileInfo::exists(base + ".video.m4s")
                    && controller.downloading().size() == 1,
                    "active removal cancels worker and waits before deleting in-use stream files");
                allowFinish.store(true);
                check(waitUntil([&] { return controller.downloading().isEmpty() && controller.library().isEmpty(); }),
                    "active removal consumes returned final paths without resurrecting library records");
                check(filesWritten.load() && noneExist(mediaFiles(base + "_actual-final"))
                    && !QFileInfo::exists(base + ".video.m4s") && !QFileInfo::exists(base + ".audio.m4s.aria2")
                    && QFileInfo::exists(unrelated) && lateProgressSignals == 0,
                    "post-cancel final media and temporary streams are precisely removed and late progress ignored");
            }
        }
        DownloadStore store(QDir(directory).filePath("downloads.sqlite"));
        check(store.load().isEmpty(), "late runner result cannot restore a removed record in persistent storage");
    }
    return failures;
}
