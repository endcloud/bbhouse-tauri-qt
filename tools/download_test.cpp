#include "downloads/DownloadController.h"
#include "downloads/DownloadStore.h"
#include "downloads/DownloadUtils.h"
#include "downloads/DownloadWorker.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlDatabase>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QtConcurrent>
#include <QDebug>

int runDownloadManagementTests();

namespace {
void put(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("fixture write failed");
}
QByteArray read(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
bool ffmpeg(const QStringList &args) {
    QProcess child; child.start(DownloadUtils::findTool("ffmpeg"), QStringList{"-nostdin", "-hide_banner", "-loglevel", "error"} + args);
    return child.waitForFinished(20000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
}
QString asynchronously(const std::function<void()> &operation) {
    QFutureWatcher<QString> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([operation] {
        try { operation(); return QString(); }
        catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
        catch (...) { return QStringLiteral("canceled"); }
    }));
    loop.exec(); return watcher.result();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/download-fixture-XXXXXX");
    if (!temp.isValid()) return 1;
    qputenv("BBHOUSE_DATA_DIR", temp.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    int failures = 0;
    auto check = [&](bool ok, const char *name) { qInfo() << (ok ? "PASS" : "FAIL") << name; failures += !ok; };
    failures += runDownloadManagementTests();
    const auto connectionsBefore = QSqlDatabase::connectionNames();
    int failedOpenCount = 0;
    for (int attempt = 0; attempt < 32; ++attempt) {
        // A directory is never a valid SQLite file on any supported platform.
        try { DownloadStore(temp.path()).load(); }
        catch (const std::exception &) { ++failedOpenCount; }
    }
    check(failedOpenCount == 32 && QSqlDatabase::connectionNames() == connectionsBefore,
          "failed database opens do not retain registered SQL connections");
    const QByteArray subtitle = R"({"body":[{"from":0.125,"to":1.5,"content":"你好\n第二行"},{"from":-1,"to":2,"content":"invalid"},{"from":3599.9996,"to":3601,"content":"boundary"}]})";
    const auto srt = DownloadUtils::subtitleToSrt(subtitle);
    check(srt.contains("00:00:00,125 --> 00:00:01,500\n你好\n第二行")
        && srt.contains("2\n01:00:00,000 --> 01:00:01,000") && !srt.contains("invalid"), "subtitle conversion timing, multiline and invalid rows");
    bool rejected = false;
    try { DownloadUtils::subtitleToSrt("<html>blocked</html>"); } catch (...) { rejected = true; }
    check(rejected, "reject malformed subtitle responses");
    const QByteArray xml("<?xml version=\"1.0\"?><i><d p=\"1,1,25,1,0,0,u,0\">fixture</d></i>");
    check(DownloadUtils::danmakuXml(xml) == xml, "XML archive validation");
    rejected = false;
    try { DownloadUtils::danmakuXml("<html>error</html>"); } catch (...) { rejected = true; }
    check(rejected, "reject HTTP error page as danmaku XML");
    check(!DownloadUtils::safeName("../CON:bad?").contains('/')
        && DownloadUtils::safeName("CON") == "_CON", "portable filename sanitization");
    const auto base = DownloadUtils::reserveBase(temp.path(), "同名/视频");
    const auto secondBase = DownloadUtils::reserveBase(temp.path(), "同名/视频");
    check(base != secondBase && QFileInfo(base).dir().exists() && QFileInfo(secondBase).dir().exists(), "timestamp namespaces never overwrite existing files");
    const QVariantMap metadata{{"title", "fixture"}, {"oid", "9007199254740991"}, {"cid", "5000000000"},
        {"cookie", "SESSDATA=secret"}, {"rawJson", "secret"}, {"url", "https://media.invalid/?token=secret"},
        {"coverUrl", "https://i0.hdslb.com/example.jpg?token=secret"}};
    const auto clean = DownloadUtils::safeMetadata(metadata);
    const auto encoded = QJsonDocument(QJsonObject::fromVariantMap(clean)).toJson();
    check(!encoded.contains("secret") && clean.value("cid") == "5000000000"
        && clean.value("oid") == "9007199254740991", "metadata allowlist removes credentials and preserves large identifiers");
    const QString dbDir = temp.path() + "/database";
    DownloadStore store(dbDir + "/downloads.sqlite"); store.initialize();
    QVariantMap recovery = clean;
    recovery["id"] = "recovery"; recovery["createdAt"] = "2026-01-01"; recovery["state"] = "running";
    store.save(recovery); recovery["message"] = "updated"; store.save(recovery);
    check(store.load().size() == 1 && store.load().first().toMap().value("message") == "updated", "independent SQLite upsert persists one task");
    const QString imported = temp.path() + "/imported.mp4";
    put(imported, "fixture"); put(temp.path() + "/imported.xml", xml); put(temp.path() + "/imported.srt", srt.toUtf8());
    {
        DownloadController controller(dbDir);
        check(controller.downloading().first().toMap().value("state") == "interrupted", "restart marks unfinished task retryable without network");
        controller.setDownloadDirectory(QUrl::fromLocalFile(temp.path()).toString());
        check(controller.downloadDirectory() == temp.path(), "folder chooser file URL becomes absolute path");
        controller.importFiles({QUrl::fromLocalFile(imported), QUrl::fromLocalFile(imported)});
        check(controller.library().size() == 1, "local import avoids duplicate records");
        const QString id = controller.library().first().toMap().value("id").toString();
        const auto entry = controller.localEntry(id);
        check(entry.value("business") == "local" && entry.value("danmakuPath") == temp.path() + "/imported.xml"
            && entry.value("subtitlePath") == temp.path() + "/imported.srt", "local playback includes same-name XML and SRT");
        QFile::remove(imported);
        check(controller.localEntry(id).isEmpty() && !controller.library().first().toMap().value("available").toBool(), "missing imported media cannot be played");
        controller.enqueue({{"business", "archive"}, {"oid", "1"}, {"downloadOptions", QVariantMap{{"video", false}, {"audio", false}, {"danmaku", false}, {"subtitles", false}}}});
        check(controller.error().contains(QStringLiteral("至少")), "per-task empty resource selection is rejected");
        check(controller.downloadVideo() && !controller.downloadAudio(), "new defaults select video without audio-only");
        controller.setDownloadAudio(true);
        check(!controller.downloadVideo() && controller.downloadAudio(), "audio-only preference deselects video");
        controller.setDownloadVideo(true);
        check(controller.downloadVideo() && !controller.downloadAudio(), "video preference deselects audio-only");
        controller.setDownloadVideo(false);
        check(!controller.downloadVideo() && !controller.downloadAudio(), "deselecting video permits attachment-only defaults");
        controller.shutdown(); // inspect persisted queue snapshots without making API requests
        controller.setAria2Path(QCoreApplication::applicationFilePath());
        controller.setFfmpegPath(QCoreApplication::applicationFilePath());
        controller.enqueue({{"business", "archive"}, {"oid", "1"}, {"downloadOptions", QVariantMap{{"video", true}, {"audio", false}, {"danmaku", false}, {"subtitles", false}, {"qn", 64}}}});
        const auto videoRecord = controller.downloading().first().toMap();
        check(videoRecord.value("downloadVideo").toBool() && videoRecord.value("downloadAudio").toBool()
            && videoRecord.value("preferredQn").toInt() == 64 && controller.preferredQn() == 80,
            "video queue includes audio and persists only the task quality");
        controller.enqueue({{"business", "archive"}, {"oid", "2"}, {"downloadOptions", QVariantMap{{"video", false}, {"audio", true}, {"danmaku", false}, {"subtitles", false}}}});
        const auto audioRecord = controller.downloading().first().toMap();
        check(!audioRecord.value("downloadVideo").toBool() && audioRecord.value("downloadAudio").toBool(), "audio-only queue does not request video");

    }
    // Completed-only cleanup is scoped to the exact task base, never directory-wide.
    const QString cleanupBase = temp.path() + "/cleanup";
    put(cleanupBase + ".mp4", "final media");
    put(cleanupBase + ".xml", xml); put(cleanupBase + ".srt", srt.toUtf8());
    put(temp.path() + "/unrelated.video.m4s", "other task");
    for (const auto *stream : {".video.m4s", ".audio.m4s"})
        for (const auto *suffix : {"", ".complete", ".aria2"}) put(cleanupBase + stream + suffix, "temporary");
    QVariantMap cleanupRecord{{"basePath", cleanupBase}, {"localPath", cleanupBase + ".mp4"}, {"state", "failed"}};
    check(!DownloadUtils::cleanupCompleted(cleanupRecord) && QFileInfo::exists(cleanupBase + ".video.m4s"), "failed downloads retain resumable streams");
    cleanupRecord["state"] = "canceled";
    check(!DownloadUtils::cleanupCompleted(cleanupRecord) && QFileInfo::exists(cleanupBase + ".audio.m4s"), "canceled downloads retain resumable streams");
    cleanupRecord["state"] = "completed"; cleanupRecord["localPath"] = cleanupBase + ".missing.mp4";
    check(!DownloadUtils::cleanupCompleted(cleanupRecord) && QFileInfo::exists(cleanupBase + ".video.m4s"), "missing final media prevents cleanup");
    cleanupRecord["localPath"] = cleanupBase + ".mp4";
    check(DownloadUtils::cleanupCompleted(cleanupRecord), "completed media permits temporary cleanup");
    bool removed = true;
    for (const auto *stream : {".video.m4s", ".audio.m4s"})
        for (const auto *suffix : {"", ".complete", ".aria2"}) removed &= !QFileInfo::exists(cleanupBase + stream + suffix);
    check(removed && read(cleanupBase + ".mp4") == "final media" && read(cleanupBase + ".xml") == xml
        && read(cleanupBase + ".srt") == srt.toUtf8() && read(temp.path() + "/unrelated.video.m4s") == "other task",
        "cleanup removes only task tracks/checkpoints and preserves final files and other tasks");
    check(DownloadUtils::cleanupCompleted(cleanupRecord), "cleanup is idempotent");
    QDir().mkdir(cleanupBase + ".audio.m4s");
    check(!DownloadUtils::cleanupCompleted(cleanupRecord) && QFileInfo(cleanupBase + ".audio.m4s").isDir(), "cleanup failure leaves unexpected directories untouched");
    cleanupRecord["state"] = "imported";
    check(!DownloadUtils::cleanupCompleted(cleanupRecord), "imported media never triggers download cleanup");

    auto flag = std::make_shared<std::atomic_bool>(true);
    const auto canceled = DownloadWorker::run({{"id", "canceled"}}, flag, [](const QString &, int) {});
    check(canceled.value("state") == "canceled", "pre-canceled task does not resolve API or read credentials");

    bool haveTools = true;
    for (const QString &tool : {"aria2c", "ffmpeg", "curl"}) haveTools &= !DownloadUtils::findTool(tool).isEmpty();
    if (!haveTools) { qInfo() << "SKIP real process integration: install aria2, ffmpeg and curl"; return failures ? 1 : 0; }
    const QString video = temp.path() + "/source video.mp4";
    const QString audio = temp.path() + "/source audio.m4a";
    const bool generated = ffmpeg({"-f", "lavfi", "-i", "color=c=black:s=32x32:r=5", "-t", "0.6", "-c:v", "mpeg4", "-an", video})
        && ffmpeg({"-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100", "-t", "0.6", "-c:a", "aac", "-vn", audio});
    check(generated, "generate isolated video/audio fixtures using FFmpeg");
    if (!generated) return 1;
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "loopback HTTP fixture starts");
    QMap<QByteArray, QByteArray> bodies{{"/video", read(video)}, {"/audio", read(audio)}, {"/xml", xml}, {"/subtitle", subtitle}};
    int authenticatedRequests = 0;
    int resumedRequests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                if (!request.contains("\r\n\r\n")) { socket->setProperty("request", request); return; }
                if (socket->property("responded").toBool()) return;
                socket->setProperty("responded", true);
                const QByteArray target = request.split(' ').value(1).split('?').first();
                if (target == "/slow") return;
                if (target == "/progress" || target == "/unknown-size") {
                    const int totalCount = target == "/progress" ? 24 : 8;
                    const auto range = QRegularExpression("(?i)range: bytes=(\\d+)-").match(QString::fromLatin1(request));
                    const int offset = range.hasMatch() ? range.captured(1).toInt() : 0;
                    const int count = totalCount - offset / 65536;
                    if (offset > 0) ++resumedRequests;
                    socket->write((offset > 0 ? QByteArray("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes ")
                            + QByteArray::number(offset) + '-' + QByteArray::number(totalCount * 65536 - 1)
                            + '/' + QByteArray::number(totalCount * 65536) + "\r\n" : QByteArray("HTTP/1.1 200 OK\r\n"))
                        + "Connection: close\r\n"
                        + (target == "/progress" ? QByteArray("Content-Length: ")
                            + QByteArray::number(count * 65536) + "\r\n" : QByteArray()) + "\r\n");
                    auto *timer = new QTimer(socket);
                    timer->setInterval(150);
                    QObject::connect(timer, &QTimer::timeout, socket, [socket, timer, count] {
                        const int sent = socket->property("chunksSent").toInt() + 1;
                        socket->setProperty("chunksSent", sent);
                        socket->write(QByteArray(65536, 'x'));
                        if (sent == count) { timer->stop(); socket->disconnectFromHost(); }
                    });
                    timer->start();
                    return;
                }
                if (request.toLower().contains("cookie: fixture=\"quoted\\value\"") && request.contains("https://www.bilibili.com/")) ++authenticatedRequests;
                if (!bodies.contains(target)) {
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else {
                    QByteArray body = bodies.value(target);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size())
                        + "\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n" + body);
                }
                socket->disconnectFromHost();
            });
        }
    });
    const QString host = QString("http://127.0.0.1:%1").arg(server.serverPort());
    const QString downloadedVideo = temp.path() + "/download 中文 video.m4s";
    const QString downloadedAudio = temp.path() + "/download audio.m4s";
    const QString merged = temp.path() + "/result.mp4";
    const QString cookie = "fixture=\"quoted\\value\"";
    // Invalid ambient proxy must not affect explicitly direct media requests.
    qputenv("HTTP_PROXY", "http://127.0.0.1:1"); qputenv("ALL_PROXY", "http://127.0.0.1:1");
    flag->store(false);
    const QString result = asynchronously([&] {
        DownloadWorker::downloadStream({host + "/missing", host + "/video?token=fixture"}, downloadedVideo, cookie, flag);
        DownloadWorker::downloadStream({host + "/audio"}, downloadedAudio, cookie, flag);
        DownloadWorker::mergeStreams(downloadedVideo, downloadedAudio, merged, flag);
        if (DownloadWorker::fetchAttachment(host + "/xml", cookie, flag) != xml) throw std::runtime_error("XML fetch mismatch");
        if (DownloadUtils::subtitleToSrt(DownloadWorker::fetchAttachment(host + "/subtitle", cookie, flag)) != srt)
            throw std::runtime_error("subtitle fetch mismatch");
    });
    check(result.isEmpty(), "real aria2 stdin mirror fallback, FFmpeg stream copy and curl config pipeline");
    if (!result.isEmpty()) qWarning() << result;
    check(read(downloadedVideo) == read(video) && read(downloadedAudio) == read(audio), "aria2 preserves complete stream bytes with Unicode and spaces in paths");
    check(authenticatedRequests >= 4, "credentials and referer reach tools through stdin with quoting intact");
    check(ffmpeg({"-i", merged, "-map", "0:v:0", "-map", "0:a:0", "-f", "null", "-"}), "merged MP4 contains decodable video and audio");
    QList<int> reported;
    bool progressOnWorker = true;
    int heartbeatCount = 0;
    QTimer heartbeat;
    heartbeat.setInterval(20);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeatCount; });
    heartbeat.start();
    const QString progressPath = temp.path() + "/progress.m4s";
    const QString progressResult = asynchronously([&] {
        DownloadWorker::downloadStream({host + "/progress"}, progressPath, cookie, flag, {}, [&](int value) {
            reported.append(value);
            progressOnWorker &= QThread::currentThread() != app.thread();
        });
    });
    heartbeat.stop();
    int intermediate = 0;
    bool monotonic = true;
    for (int index = 0; index < reported.size(); ++index) {
        if (reported[index] >= 0 && reported[index] < 100) ++intermediate;
        if (index > 0 && reported[index] < reported[index - 1]) monotonic = false;
    }
    check(progressResult.isEmpty() && reported.first() == -1 && reported.last() == 100
        && intermediate >= 2 && monotonic, "real throttled aria2 transfer reports repeated monotonic intermediate progress");
    check(progressOnWorker && heartbeatCount >= 30, "progress processing stays on worker while UI event loop keeps ticking");
    reported.clear();
    const QString receiptResult = asynchronously([&] {
        DownloadWorker::downloadStream({host + "/progress"}, progressPath, cookie, flag, {},
            [&](int value) { reported.append(value); });
    });
    check(receiptResult.isEmpty() && reported == QList<int>{100}, "verified completed stream reports success without redownloading");
    reported.clear();
    const QString resumePath = temp.path() + "/resumed.m4s";
    put(resumePath, QByteArray(8 * 65536, 'x'));
    const QString resumeResult = asynchronously([&] {
        DownloadWorker::downloadStream({host + "/progress"}, resumePath, cookie, flag, {},
            [&](int value) { reported.append(value); });
    });
    check(resumeResult.isEmpty() && resumedRequests > 0 && QFileInfo(resumePath).size() == 24 * 65536
        && reported.size() >= 3 && reported[1] > 33 && reported.last() == 100,
        "resumed stream includes existing bytes in progress and completes through HTTP range");
    reported.clear();
    const QString unknownResult = asynchronously([&] {
        DownloadWorker::downloadStream({host + "/unknown-size"}, temp.path() + "/unknown.m4s", cookie, flag, {},
            [&](int value) { reported.append(value); });
    });
    check(unknownResult.isEmpty() && reported == QList<int>{-1, 100}, "unknown total remains indeterminate until successful completion");
    reported.clear();
    QTimer::singleShot(1450, &app, [flag] { flag->store(true); });
    QElapsedTimer progressCancellation; progressCancellation.start();
    const QString progressCancelResult = asynchronously([&] {
        DownloadWorker::downloadStream({host + "/progress"}, temp.path() + "/canceled.m4s", cookie, flag, {},
            [&](int value) { reported.append(value); });
    });
    check(!progressCancelResult.isEmpty() && !reported.contains(100)
        && progressCancellation.elapsed() < 4000
        && !QFileInfo::exists(temp.path() + "/canceled.m4s.complete"),
        "canceling an active aria2 transfer is prompt and never reports completion");
    flag->store(false);
    const QByteArray before = read(merged);
    const QString overwrite = asynchronously([&] { DownloadWorker::mergeStreams(downloadedVideo, downloadedAudio, merged, flag); });
    check(!overwrite.isEmpty() && read(merged) == before, "FFmpeg refuses to overwrite existing media");
    const QString badTarget = temp.path() + "/broken.mp4";
    const QString badMerge = asynchronously([&] { DownloadWorker::mergeStreams(cleanupBase + ".xml", {}, badTarget, flag); });
    check(!badMerge.isEmpty() && !QFileInfo::exists(badTarget)
        && QDir(temp.path()).entryList({".bbhouse-merge-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
        "failed FFmpeg merge leaves neither final nor temporary output");
    QTimer::singleShot(250, &app, [flag] { flag->store(true); });
    QElapsedTimer elapsed; elapsed.start();
    const QString cancelResult = asynchronously([&] { DownloadWorker::fetchAttachment(host + "/slow", cookie, flag); });
    check(!cancelResult.isEmpty() && elapsed.elapsed() < 4000, "cancellation kills a blocked curl child promptly");
    return failures ? 1 : 0;
}
