// Offline PlayerController regression: real mpv, isolated paths, no network/Cookie.
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <functional>
#include <iostream>

#include "core/AppPaths.h"
#include "core/PlaybackEntry.h"
#include "player/PlayerController.h"
#include "player/MpvLib.h"
#include "online_danmaku_loader_checks.h"
#include <mpv/render.h>

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
int runLocalPlayerRenderTest(int argc, char **argv);
#endif

// This executable supplies isolated AppPaths instead of linking AppPaths.cpp.
// Counting Cookie path queries also detects otherwise invisible heartbeat reads.
namespace {
QString testRoot;
int cookieReads = 0;
int failures = 0;
void check(bool result, const char *name) {
    std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
    if (!result) ++failures;
}
bool until(const std::function<bool()> &predicate, int timeout = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return predicate();
}
void writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size(), "write isolated fixture");
}
QVariantMap entry(const QString &id, const QString &path) {
    return {{"business", "local"}, {"isLocal", true}, {"videoKey", "local:" + id},
            {"localPath", path}, {"title", "Local fixture " + id}};
}
QByteArray xml(const char *text) {
    return QByteArray("<i><d p=\"1,1,25,16777215,0,0,fixture,1\">") + text + "</d></i>";
}
}
// Headless audio/privacy regression still honors the production readiness gate.
// A real software render context supplies readiness without requiring OpenGL.
class SoftwareRenderer {
public:
    ~SoftwareRenderer() { reset(); }
    void reset() {
        if (context_) {
            const char *stop[] = {"stop", nullptr};
            MpvLib::instance()->commandFn(handle_.get(), stop);
            MpvLib::instance()->renderContextFree(context_);
        }
        context_ = nullptr;
        handle_.reset();
    }
    void attach(MpvVideoItem *item) {
        if (!item->client()) return;
        auto handle = item->client()->sharedHandle();
        if (handle_ == handle) return;
        reset();
        handle_ = handle;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID, nullptr}};
        check(MpvLib::instance()->renderContextCreate(&context_, handle.get(), params) >= 0,
              "headless software renderer initialized before media load");
        if (context_) item->notifier()->initialized(item->clientRevision());
    }
private:
    std::shared_ptr<mpv_handle> handle_;
    mpv_render_context *context_ = nullptr;
};

void setLocalPlayerTestRoot(const QString &path) {
    testRoot = path;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testRoot);
}
int localPlayerCookieReads() { return cookieReads; }

QString AppPaths::repoRoot() { return testRoot; }
QString AppPaths::dataDir() { return testRoot; }
QString AppPaths::dbPath() { return testRoot + "/history.sqlite3"; }
QString AppPaths::cookiePath() { ++cookieReads; return testRoot + "/absent.cookie.txt"; }
QString AppPaths::normalCookiePath() { ++cookieReads; return testRoot + "/absent.normal.cookie.txt"; }
QString AppPaths::exportPath() { return testRoot + "/history.json"; }

int main(int argc, char **argv) {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (argc > 1 && (QByteArray(argv[1]) == "--macos-opengl" || QByteArray(argv[1]) == "--windows-opengl"))
        return runLocalPlayerRenderTest(argc, argv);
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    checkOnlineDanmakuReuse(check, [](const std::function<bool()> &predicate) { return until(predicate); });
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/local-player-XXXXXX");
    check(temp.isValid(), "fixtures remain within build and clean themselves up");
    testRoot = temp.path();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testRoot);
    const QString media = testRoot + "/本地 媒体.wav";
    QFile file(media);
    check(file.open(QIODevice::WriteOnly), "create local WAV fixture");
    QDataStream wav(&file);
    wav.setByteOrder(QDataStream::LittleEndian);
    const int bytes = 20 * 16000;
    wav.writeRawData("RIFF", 4); wav << quint32(36 + bytes);
    wav.writeRawData("WAVEfmt ", 8); wav << quint32(16) << quint16(1) << quint16(1)
        << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    wav.writeRawData("data", 4); wav << quint32(bytes);
    wav.writeRawData(QByteArray(bytes, '\0').constData(), bytes);
    file.close();
    const QString siblingXml = testRoot + "/本地 媒体.xml";
    writeFile(siblingXml, xml("sibling"));
    writeFile(testRoot + "/alternate.xml", xml("alternate"));
    writeFile(testRoot + "/captions.srt", "1\n00:00:01,000 --> 00:00:03,000\nLocal subtitle\n");
    auto first = entry("1", media);
    first.insert("danmakuPath", testRoot + "/alternate.xml");
    first.insert("subtitlePath", testRoot + "/captions.srt");
    check(PlaybackEntry::playable(first), "readable absolute local media is playable");
    check(!PlaybackEntry::playable(entry("url", "https://example.invalid/video.mp4")), "network URLs cannot enter local playback");
    writeFile(testRoot + "/empty.mp4", {});
    check(!PlaybackEntry::playable(entry("empty", testRoot + "/empty.mp4")), "empty file is not playable");
    check(!PlaybackEntry::playable(entry("directory", testRoot)), "directory is not playable");

    PlayerController player;
    SoftwareRenderer renderer;
    QVariantList loadedDanmaku;
    int danmakuEvents = 0;
    QString error;
    QObject::connect(&player, &PlayerController::danmakuLoaded, [&](const QVariantList &items) {
        loadedDanmaku = items; ++danmakuEvents;
        player.danmakuItem()->loadEntries(items);
    });
    QObject::connect(&player, &PlayerController::errorOccurred, [&](const QString &message) { error = message; });
    player.setVolumePercent(0);
    player.openWith({first});
    check(player.loading() && player.videoItem()->client()->getPropertyString("path").isEmpty(),
          "local media waits until a renderer is ready");
    renderer.attach(player.videoItem());
    check(until([&] { return !player.loading() && danmakuEvents > 0; }) && error.isEmpty(),
          "real mpv starts local media without a Cookie");
    auto *client = player.videoItem()->client();
    check(client && player.localMedia() && player.qualities().isEmpty() && !player.fallback(),
          "local mode retains common kernel with no online quality or fallback");
    if (!client) return 1;
    check(loadedDanmaku.size() == 1 && loadedDanmaku.first().toMap().value("message") == "sibling",
          "same-name XML takes precedence over explicitly provided alternate XML");
    check(client->getPropertyString("http-header-fields").isEmpty(), "local playback has no Cookie header");
    check(until([&] { return player.subtitleTracks().size() == 1; }), "local SRT catalog becomes available");
    check(player.selectedSubtitle() == -1 && client->getPropertyString("sid") == "no"
          && !client->getFlag("sub-visibility"), "local SRT starts disabled");
    player.selectSubtitle(0);
    player.selectSubtitle(-1);
    check(until([&] { return client->getPropertyString("track-list/1/type") == "sub"; })
          && player.selectedSubtitle() == -1 && !client->getFlag("sub-visibility"),
          "turning CC off invalidates pending sidecar selection callback");
    player.selectSubtitle(0);
    check(until([&] { return client->getPropertyString("track-list/1/type") == "sub"
                            && client->getFlag("sub-visibility"); }), "CC selection attaches and displays local SRT");
    check(player.selectedSubtitle() == 0 && client->getPropertyString("sid") != "no", "selected SRT has native track ID");
    player.toggleSubtitle();
    check(player.selectedSubtitle() == -1 && !client->getFlag("sub-visibility"), "S shortcut disables local subtitle");
    player.toggleSubtitle();
    check(player.selectedSubtitle() == 0 && client->getFlag("sub-visibility"), "S shortcut restores current first track");
    const QString originalPath = client->getPropertyString("path");
    player.setQuality(120);
    check(!player.loading() && client->getPropertyString("path") == originalPath, "quality controls cannot re-resolve local media");
    check(until([&] { return player.position() > 5.5; }, 8000), "local polling crosses online position-save threshold");
    client->ended();
    player.closeRequested();
    renderer.reset();
    QThreadPool::globalInstance()->waitForDone();
    check(cookieReads == 0 && !QFileInfo::exists(AppPaths::dbPath()),
          "polling, EOF and close never read Cookie or create online history database");

    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (!ffmpeg.isEmpty()) {
        const QString embedded = testRoot + "/embedded.mkv";
        QProcess mux;
        mux.start(ffmpeg, {"-v", "error", "-i", media, "-i", testRoot + "/captions.srt",
            "-map", "0:a", "-map", "1:s", "-c", "copy", "-metadata:s:s:0", "title=Embedded fixture",
            "-disposition:s:0", "default", embedded});
        const bool muxed = mux.waitForFinished(10000) && mux.exitCode() == 0;
        check(muxed, "generate isolated embedded default subtitle fixture");
        if (muxed) {
            player.openWith({entry("embedded", embedded)});
            renderer.attach(player.videoItem());
            check(until([&] { return !player.loading() && player.subtitleTracks().size() == 1; })
                  && player.selectedSubtitle() == -1
                  && !player.videoItem()->client()->getFlag("sub-visibility"),
                  "embedded default-flag subtitle is listed but never automatically enabled");
            player.selectSubtitle(0);
            check(player.videoItem()->client()->getFlag("sub-visibility")
                  && player.videoItem()->client()->getPropertyString("sid") != "no",
                  "CC selects embedded subtitle by mpv track ID");
            player.seek(2);
            check(player.selectedSubtitle() == 0, "seeking preserves explicit local subtitle selection");
            player.closeRequested();
            renderer.reset();
        }
    } else {
        std::cout << "SKIP embedded subtitle fixture: ffmpeg unavailable\n";
    }

    error.clear();
    player.openWith({entry("missing", testRoot + "/missing.mp4")});
    check(until([&] { return !error.isEmpty(); }) && player.playlist().isEmpty(), "missing media rejected before playlist insertion");
    error.clear();
    const QString secondMedia = testRoot + "/second.wav";
    check(QFile::copy(media, secondMedia), "create second media fixture");
    danmakuEvents = 0;
    auto noSidecars = entry("2", secondMedia);
    noSidecars.insert("subtitlePath", "https://example.invalid/captions.srt");
    player.openWith({noSidecars});
    renderer.attach(player.videoItem());
    check(until([&] { return !player.loading(); }) && error.isEmpty() && danmakuEvents == 0,
          "missing sibling XML silently skips while media starts");
    check(player.selectedSubtitle() == -1 && !player.videoItem()->client()->getFlag("sub-visibility"),
          "new media resets prior subtitle selection without persistence");
    check(player.videoItem()->client()->getPropertyDouble("track-list/count") == 1,
          "remote subtitle path is not loaded");
    player.closeRequested();
    renderer.reset();
    writeFile(testRoot + "/second.xml", xml("new-generation"));
    danmakuEvents = 0;
    loadedDanmaku.clear();
    player.openWith({first});
    player.openWith({entry("2", secondMedia)}); // before queued first XML delivery
    renderer.attach(player.videoItem());
    check(until([&] { return !player.loading() && danmakuEvents > 0; }) && error.isEmpty(), "rapid switch starts latest local entry");
    check(danmakuEvents == 1 && loadedDanmaku.first().toMap().value("message") == "new-generation",
          "old queued XML result is excluded by generation");
    player.closeRequested();
    renderer.reset();
    check(QFile::remove(secondMedia), "remove only test fixture to simulate deleted library media");
    player.openWith({first, entry("2", media)});
    // A stored entry can disappear between entry checks and a later playlist click.
    check(QFile::remove(media), "simulate file removal after insertion");
    error.clear();
    player.playByIndex(0);
    check(until([&] { return !player.loading() && !error.isEmpty(); }), "playlist replay rechecks file existence before loading");
    check(!player.fallback() && cookieReads == 0, "missing local file never falls back to online durl");
    player.closeRequested();
    renderer.reset();
    QThreadPool::globalInstance()->waitForDone();
    error.clear();
    writeFile(testRoot + "/broken.mp4", "this is not a media stream");
    player.openWith({entry("broken", testRoot + "/broken.mp4")});
    renderer.attach(player.videoItem());
    check(until([&] { return !player.loading() && !error.isEmpty(); }) && !player.fallback(),
          "mpv local decode failure stops without online candidate fallback");
    player.closeRequested();
    renderer.reset();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(!QFileInfo::exists(AppPaths::dbPath()) && cookieReads == 0, "all local scenarios preserve online history and credentials");
    // A missing isolated Cookie stops resolution before any network request.
    // Deleting a controller must finish tasks that use its HistoryStore even
    // without the application's global-pool shutdown barrier.
    bool completedAtDestruction = true;
    for (int i = 0; i < 8; ++i) {
        const int before = cookieReads;
        auto transient = std::make_unique<PlayerController>();
        transient->openWith({QVariantMap{{"videoKey", "archive:1"}, {"business", "archive"},
                                       {"oid", "1"}, {"title", "Offline teardown fixture"}}});
        transient.reset();
        completedAtDestruction &= cookieReads == before + 1;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
    check(completedAtDestruction,
          "controller destruction joins pending resolution before releasing its isolated store");
    return failures ? 1 : 0;
}
