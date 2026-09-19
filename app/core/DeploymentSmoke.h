#pragma once

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <clocale>
#include <memory>
#include <utility>
#ifdef Q_OS_MACOS
#include <dlfcn.h>
#endif

#include "controllers/AppController.h"
#include "core/AppPaths.h"
#include "downloads/DownloadUtils.h"
#include "downloads/DownloadController.h"
#include "player/MpvLib.h"
#include "preferences/AppPreferences.h"

namespace deploymentSmoke {
// A future settings-page change must not silently turn this offline check into
// a network request. Local Qt resources remain available to the QML loader.
class NetworkFactory final : public QQmlNetworkAccessManagerFactory {
public:
    bool remoteRequest = false;
    QNetworkAccessManager *create(QObject *parent) override {
        class Manager final : public QNetworkAccessManager {
        public:
            Manager(bool *remote, QObject *owner)
                : QNetworkAccessManager(owner), remote_(remote) {}
        protected:
            QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                         QIODevice *outgoing) override {
                const QString scheme = request.url().scheme();
                if (scheme != "qrc" && scheme != "file" && scheme != "data") {
                    *remote_ = true;
                    return QNetworkAccessManager::createRequest(
                        GetOperation, QNetworkRequest(QUrl("qrc:/deployment-smoke-blocked")), nullptr);
                }
                return QNetworkAccessManager::createRequest(op, request, outgoing);
            }
        private:
            bool *remote_;
        };
        return new Manager(&remoteRequest, parent);
    }
};
} // namespace deploymentSmoke

// Header-only to keep the deployment entry point in the existing executable's
// translation unit. The required scratch directory is supplied by the packager.
inline int runDeploymentSmokeTest(int argc, char **argv) {
    QTextStream errors(stderr);
    QString scratchRoot;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == "--deployment-smoke-test") continue;
        if (argument == "--scratch-dir" && i + 1 < argc) {
            scratchRoot = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        errors << "DEPLOYMENT_SMOKE_FAILED: invalid arguments\n";
        return 2;
    }
    if (!QFileInfo(scratchRoot).isAbsolute() || !QFileInfo(scratchRoot).isDir()) {
        errors << "DEPLOYMENT_SMOKE_FAILED: --scratch-dir requires an existing absolute directory\n";
        return 2;
    }
    QTemporaryDir scratch(QDir(scratchRoot).filePath("deployment-smoke-XXXXXX"));
    if (!scratch.isValid()) return 2;
    qputenv("BBHOUSE_DATA_DIR", scratch.path().toUtf8());
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    for (const auto scope : {QSettings::UserScope, QSettings::SystemScope}) {
        QSettings::setPath(QSettings::IniFormat, scope, scratch.path());
        QSettings::setPath(QSettings::NativeFormat, scope, scratch.path());
    }
    QCoreApplication::setOrganizationName("bbhouse-deployment-smoke");
    QCoreApplication::setApplicationName("bbhouse-deployment-smoke");
    QCoreApplication::setApplicationVersion(QStringLiteral(BBHOUSE_APP_VERSION));
    QApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    const auto fail = [&errors](const char *message) {
        errors << "DEPLOYMENT_SMOKE_FAILED: " << message << '\n';
        return 1;
    };

    if (QFileInfo(AppPaths::cookiePath()).absolutePath() != scratch.path())
        return fail("bundle credentials are not isolated outside the app");
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", "deployment-smoke");
        database.setDatabaseName(scratch.filePath("fixture.sqlite3"));
        if (!database.open()) return fail("bundled SQLite driver");
        QSqlQuery query(database);
        if (!query.exec("CREATE TABLE fixture (value INTEGER)")
            || !query.exec("INSERT INTO fixture VALUES (42)")
            || !query.exec("SELECT value FROM fixture") || !query.next()
            || query.value(0).toInt() != 42)
            return fail("SQLite fixture read/write");
    }
    QSqlDatabase::removeDatabase("deployment-smoke");
    const auto formats = QImageReader::supportedImageFormats();
    if (!formats.contains("jpeg") || !formats.contains("webp"))
        return fail("bundled JPEG/WebP image plugins");
    QImageReader avatar(":/images/noface.jpg");
    if (avatar.read().isNull()) return fail("bundled image resource decode");

    for (const auto *path : {":/icons/bbhouse-icon-1024.png", ":/icons/bbhouse-icon-1024-mac.png"}) {
        QImageReader icon(QString::fromUtf8(path));
        if (icon.read().size() != QSize(1024, 1024)) return fail("bundled app icon decode");
    }

    auto *lib = MpvLib::instance();
    if (!lib->probe()) return fail("libmpv unavailable");
#ifdef Q_OS_MACOS
    // Check the loaded symbol's actual image, not just a requested filename.
    Dl_info image{};
    const QString frameworks = QFileInfo(QCoreApplication::applicationDirPath()
                                         + "/../Frameworks").canonicalFilePath();
    if (frameworks.isEmpty()
        || !dladdr(reinterpret_cast<const void *>(lib->create), &image)
        || !image.dli_fname
        || !QFileInfo(QString::fromLocal8Bit(image.dli_fname)).canonicalFilePath()
                .startsWith(frameworks + '/'))
        return fail("libmpv was not loaded from bundle Frameworks");
#endif
    std::unique_ptr<mpv_handle, void (*)(mpv_handle *)> player(lib->create(), lib->terminateDestroy);
    if (!player) return fail("mpv_create");
    const std::pair<const char *, const char *> options[] = {
        {"config", "no"}, {"load-scripts", "no"}, {"ytdl", "no"},
        {"terminal", "no"}, {"msg-level", "all=no"}, {"vo", "null"},
        {"ao", "null"}, {"hwdec", "no"}, {"audio-display", "no"},
        {"keep-open", "no"}, {"idle", "yes"}, {"http-proxy", ""}
    };
    for (const auto &option : options)
        if (lib->setOptionString(player.get(), option.first, option.second) < 0)
            return fail("mpv offline option");
    if (lib->initialize(player.get()) < 0) return fail("mpv_initialize");

    // A generated 0.2-second PCM WAV verifies demuxing, decoding and playback
    // through bundled libmpv without external media, ffmpeg, sound or network.
    QFile wave(scratch.filePath("silence.wav"));
    if (!wave.open(QIODevice::WriteOnly)) return fail("cannot create WAV fixture");
    constexpr quint32 samples = 1600;
    constexpr quint32 dataBytes = samples * 2;
    QDataStream wav(&wave);
    wav.setByteOrder(QDataStream::LittleEndian);
    wav.writeRawData("RIFF", 4); wav << quint32(36 + dataBytes);
    wav.writeRawData("WAVEfmt ", 8); wav << quint32(16) << quint16(1)
        << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    wav.writeRawData("data", 4); wav << dataBytes;
    for (quint32 i = 0; i < samples; ++i) wav << qint16(0);
    if (wav.status() != QDataStream::Ok) return fail("cannot write WAV fixture");
    wave.close();

    // Exercise the same resolver as downloads and cover extraction. A healthy
    // developer installation must not hide a missing executable in the bundle.
    const QString executableDirectory = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    QString aria2, ffmpeg, curl;
    for (const auto &tool : {std::pair<const char *, QString *>{"aria2c", &aria2},
                            {"ffmpeg", &ffmpeg}, {"curl", &curl}}) {
        *tool.second = DownloadUtils::findTool(QString::fromLatin1(tool.first));
        const QFileInfo executable(*tool.second);
        if (tool.second->isEmpty() || !executable.isExecutable()
            || QFileInfo(executable.canonicalFilePath()).absolutePath() != executableDirectory)
            return fail("download tool resolver did not select a bundled executable");
    }
    QProcessEnvironment toolEnvironment = QProcessEnvironment::systemEnvironment();
    for (const auto *variable : {"HOME", "USERPROFILE", "CURL_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"})
        toolEnvironment.insert(QString::fromLatin1(variable), scratch.path());
    toolEnvironment.insert("OPENSSL_MODULES", executableDirectory + "/../Frameworks/ossl-modules");
    toolEnvironment.insert("SSL_CERT_FILE", "/etc/ssl/cert.pem");
    toolEnvironment.insert("SSL_CERT_DIR", "/etc/ssl/certs");
    const auto runTool = [&](const QString &program, const QStringList &arguments) {
        QProcess process;
        process.setProcessEnvironment(toolEnvironment);
        process.setWorkingDirectory(scratch.path());
        process.setStandardInputFile(QProcess::nullDevice());
        process.start(program, arguments);
        if (!process.waitForStarted(5000)) return false;
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(5000);
            return false;
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    if (!runTool(aria2, {"--no-conf=true", "--version"}))
        return fail("bundled aria2c version execution");
    if (!runTool(ffmpeg, {"-version"})) return fail("bundled FFmpeg version execution");
    if (!runTool(curl, {"-q", "--version"})) return fail("bundled curl version execution");
    const QString encoded = scratch.filePath("encoded.m4a");
    const QString remuxed = scratch.filePath("remuxed.m4a");
    if (!runTool(ffmpeg, {"-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                         "-protocol_whitelist", "file,pipe", "-i", wave.fileName(),
                         "-c:a", "aac", encoded}) || QFileInfo(encoded).size() <= 0)
        return fail("bundled FFmpeg local AAC encoding");
    if (!runTool(ffmpeg, {"-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                         "-protocol_whitelist", "file,pipe", "-i", encoded,
                         "-c", "copy", "-movflags", "+faststart", remuxed})
        || QFileInfo(remuxed).size() <= 0)
        return fail("bundled FFmpeg local M4A remux");
    if (!runTool(ffmpeg, {"-nostdin", "-hide_banner", "-loglevel", "error", "-xerror",
                         "-protocol_whitelist", "file,pipe", "-i", remuxed,
                         "-f", "null", "-"}))
        return fail("bundled FFmpeg remuxed media decoding");
    const QString transferred = scratch.filePath("curl-fixture.wav");
    if (!runTool(curl, {"-q", "--silent", "--show-error", "--fail", "--proto", "=file",
                       "--noproxy", "*", "--output", transferred,
                       QUrl::fromLocalFile(wave.fileName()).toString(QUrl::FullyEncoded)}))
        return fail("bundled curl local file transfer");
    QFile copiedWave(transferred);
    if (!wave.open(QIODevice::ReadOnly) || !copiedWave.open(QIODevice::ReadOnly)
        || wave.readAll() != copiedWave.readAll())
        return fail("bundled curl local transfer content mismatch");
    wave.close();
    copiedWave.close();
    const QByteArray wavePath = wave.fileName().toUtf8();
    const char *command[] = {"loadfile", wavePath.constData(), nullptr};
    if (lib->commandFn(player.get(), command) < 0) return fail("mpv loadfile");
    bool loaded = false;
    bool ended = false;
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < 10000 && !ended) {
        const mpv_event *event = lib->waitEvent(player.get(), 0.1);
        if (event->event_id == MPV_EVENT_FILE_LOADED) loaded = true;
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<const mpv_event_end_file *>(event->data);
            if (end->reason != MPV_END_FILE_REASON_EOF || end->error < 0)
                return fail("mpv playback did not reach EOF");
            ended = true;
        }
    }
    if (!loaded || !ended) return fail("mpv playback timeout");
    player.reset();

    // The real settings page exercises shipped QML/FluentUI resources and Qt
    // plugins. Download settings use an empty isolated store. No history,
    // authentication, player or service controllers exist.
    AppController controller;
    DownloadController downloads(scratch.path());
    deploymentSmoke::NetworkFactory network;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&network);
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.rootContext()->setContextProperty("AppPreferences", AppPreferences::instance());
    engine.rootContext()->setContextProperty("AppController", &controller);
    engine.rootContext()->setContextProperty("DownloadController", &downloads);
    engine.rootContext()->setContextProperty("MpvLib", lib);
    bool qmlWarnings = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &app,
                     [&qmlWarnings](const QList<QQmlError> &) { qmlWarnings = true; });
    engine.load(QUrl("qrc:/qt/qml/bbhouse/qml/pages/SettingsPage.qml"));
    if (engine.rootObjects().isEmpty()) return fail("settings QML failed to instantiate");
    QTimer::singleShot(250, &app, &QCoreApplication::quit);
    app.exec();
    if (qmlWarnings) return fail("settings QML warnings");
    if (network.remoteRequest) return fail("settings attempted a remote request");
    QTextStream(stdout) << "DEPLOYMENT_SMOKE_OK: bundled libmpv, aria2c/FFmpeg/curl, local AAC/remux/file transfer, local PCM playback, SQLite, JPEG/WebP, isolated settings QML\n";
    return 0;
}
