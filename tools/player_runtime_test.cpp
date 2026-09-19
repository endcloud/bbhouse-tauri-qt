// Headless checks: real libmpv ABI/events/ownership plus the danmaku media clock.
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>
#include <QThread>
#include <QVariantMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QTimer>
#include <cmath>
#include <functional>
#include <iostream>

#include "player/MpvClient.h"
#include "player/MpvLib.h"
#include "player/DanmakuEngine.h"

namespace {
int failures = 0;
void check(bool result, const char *name) {
    std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
    if (!result) ++failures;
}
bool until(const std::function<bool()> &predicate, int timeout = 4000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return predicate();
}
QVariantMap entry(double time, int type, const QString &message) {
    return {{"time", time}, {"type", type}, {"message", message}, {"fontSize", 25}};
}
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    auto *lib = MpvLib::instance();
    check(lib->probe(), "libmpv loads with complete symbols");
    if (!lib->available()) return 1;
    QTemporaryDir privacyTemp(QCoreApplication::applicationDirPath() + "/player-privacy-XXXXXX");
    check(privacyTemp.isValid(), "privacy fixture stays in build directory");
    const QString privateLog = privacyTemp.path() + "/mpv.log";
    qputenv("BBHOUSE_MPV_LOG", privateLog.toUtf8());
    qputenv("BBHOUSE_MPV_LOG_LEVEL", "all=trace");
    auto *client = MpvClient::create();
    check(client != nullptr, "real mpv initializes");
    if (!client) return 1;
    check(!QFile::exists(privateLog) && client->getPropertyString("options/log-file").isEmpty(),
          "inherited debug env cannot enable raw native media logs");
    qunsetenv("BBHOUSE_MPV_LOG");
    qunsetenv("BBHOUSE_MPV_LOG_LEVEL");
    client->setPropertyString("ao", "null");
    bool initialPauseReceived = false;
    bool uiPaused = true; // PlayerController's idle/new-window initial state.
    QObject::connect(client, &MpvClient::pausedChanged, [&](bool value) {
        initialPauseReceived = true;
        uiPaused = value;
    });
    check(until([&] { return initialPauseReceived; }) && !uiPaused,
          "fresh kernel reports default playing state to initially paused UI");
    client->setPaused(true);
    check(client->getFlag("pause"), "official MPV_FORMAT_FLAG round trip");
    client->setPropertyString("speed", "1.5");
    check(std::abs(client->getPropertyDouble("speed") - 1.5) < 0.001,
          "official MPV_FORMAT_DOUBLE round trip");
    bool pauseObserved = false;
    QObject::connect(client, &MpvClient::pausedChanged, [&](bool value) { pauseObserved = value; });
    check(until([&] { return pauseObserved; }), "pause property observation uses correct ABI");

    // A local silent PCM file exercises version-specific loadfile start options
    // without Bilibili, credentials, a GL scene graph, or sound output.
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/player-runtime-XXXXXX");
    check(temp.isValid(), "temporary fixture stays in build directory");
    const QString filename = temp.path() + "/silence.wav";
    QFile file(filename);
    check(file.open(QIODevice::WriteOnly), "create local audio fixture");
    QDataStream wav(&file);
    wav.setByteOrder(QDataStream::LittleEndian);
    wav.writeRawData("RIFF", 4); wav << quint32(36 + 32000);
    wav.writeRawData("WAVEfmt ", 8); wav << quint32(16) << quint16(1) << quint16(1)
        << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    wav.writeRawData("data", 4); wav << quint32(32000);
    wav.writeRawData(QByteArray(32000, '\0').constData(), 32000);
    file.close();
    int restarts = 0;
    double restartPosition = -1;
    QObject::connect(client, &MpvClient::playbackRestarted, [&](double value) { ++restarts; restartPosition = value; });
    bool loaded = false;
    bool loadedUiPaused = true;
    bool failed = false;
    QObject::connect(client, &MpvClient::fileLoaded, [&] { loaded = true; loadedUiPaused = uiPaused; });
    QObject::connect(client, &MpvClient::playbackError, [&](int, const QString &) { failed = true; });
    client->loadSingle(filename, 0.25);
    check(until([&] { return loaded || failed; }) && loaded && !failed,
          "loadfile with start option works for installed mpv API");
    check(!loadedUiPaused && uiPaused == client->getFlag("pause"),
          "file-loaded reconciles UI pause state before notifying loaded");
    client->setPaused(!uiPaused);
    check(until([&] { return uiPaused; }) && client->getFlag("pause"),
          "first play/pause action after autostart pauses immediately");
    check(until([&] { return client->getPropertyDouble("duration") > 1.9; }),
          "media duration is readable as double");
    check(until([&] { return restarts > 0; }) && std::abs(restartPosition - .25) < .1,
          "initial load reports confirmed playback restart position");
    client->setPaused(!uiPaused);
    check(until([&] { return !uiPaused; }) && !client->getFlag("pause"),
          "second play/pause action resumes and updates UI");
    // This fixture is audio-only with ao=null: a paused audio seek need not
    // emit PLAYBACK_RESTART until resumed. Exercise the event while playing.
    const int priorRestarts = restarts;
    check(client->command({"seek", "1.25", "absolute+exact"}) >= 0,
          "exact seek accepted by installed mpv");
    check(until([&] { return restarts > priorRestarts && std::abs(restartPosition - 1.25) < .1; }),
          "playing seek reports playback restart at requested target");
    bool ended = false;
    QObject::connect(client, &MpvClient::ended, [&] { ended = true; client->setPaused(true); });
    check(client->command({"seek", "2", "absolute+exact"}) >= 0
              && until([&] { return ended; }), "final item reaches keep-open EOF");
    const int eofRestarts = restarts;
    check(client->command({"seek", "0", "absolute+exact"}) >= 0,
          "system Play can seek final item back to start");
    client->setPaused(false);
    check(until([&] { return restarts > eofRestarts && !client->getFlag("eof-reached")
                         && !client->getFlag("pause"); }),
          "system Play after EOF restarts playback");
    client->loadSingle(temp.path() + "/does-not-exist.mp4", 0);
    check(until([&] { return failed; }), "END_FILE failure is reported immediately");
    loaded = false;
    failed = false;
    client->loadSingle(temp.path() + "/obsolete.mp4", 0);
    client->loadSingle(filename, 0.25);
    check(until([&] { return loaded || failed; }) && loaded && !failed,
          "late failure from replaced entry does not fail current media");
    loaded = false;
    client->setPaused(false);
    until([&] { return !uiPaused; });
    uiPaused = true;
    client->loadSingle(filename, .25);
    check(until([&] { return loaded; }) && !loadedUiPaused && !uiPaused,
          "new media reconciles consumer even when mpv pause never changed");
    // Direct media must ignore inherited proxy env, independently of API proxy settings.
    QTcpServer mediaServer, proxyTrap;
    check(mediaServer.listen(QHostAddress::LocalHost) && proxyTrap.listen(QHostAddress::LocalHost),
          "local media and proxy trap listen");
    int mediaRequests = 0, proxyRequests = 0, slowAudioRequests = 0;
    QStringList audioRequests;
    QFile fixture(filename);
    check(fixture.open(QIODevice::ReadOnly), "read local HTTP media fixture");
    const QByteArray wavBody = fixture.readAll();
    QObject::connect(&proxyTrap, &QTcpServer::newConnection, [&] {
        while (auto *socket = proxyTrap.nextPendingConnection()) {
            ++proxyRequests;
            socket->close();
            socket->deleteLater();
        }
    });
    QObject::connect(&mediaServer, &QTcpServer::newConnection, [&] {
        while (auto *socket = mediaServer.nextPendingConnection()) {
            auto bytes = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, bytes] {
                bytes->append(socket->readAll());
                if (!bytes->contains("\r\n\r\n")) return;
                ++mediaRequests;
                const QByteArray path = bytes->split(' ').value(1);
                if (path.contains("audio")) audioRequests.append(QString::fromUtf8(path));
                if (path == "/stalled-audio.wav") return;
                if (path.startsWith("/missing-audio")) {
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }
                const bool slow = bytes->startsWith("GET /slow-audio.wav ");
                if (slow) ++slowAudioRequests;
                auto respond = [socket, wavBody] {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: " +
                              QByteArray::number(wavBody.size()) + "\r\nConnection: close\r\n\r\n" + wavBody);
                    socket->disconnectFromHost();
                };
                if (slow) QTimer::singleShot(350, socket, respond);
                else respond();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QHash<QByteArray, QByteArray> oldEnv;
    const QList<QByteArray> envKeys = {"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY", "no_proxy", "NO_PROXY"};
    const QByteArray trapUrl = "http://127.0.0.1:" + QByteArray::number(proxyTrap.serverPort());
    for (const auto &key : envKeys) {
        oldEnv.insert(key, qgetenv(key.constData()));
        qputenv(key.constData(), key.toLower() == "no_proxy" ? QByteArray("") : trapUrl);
    }
    loaded = false;
    failed = false;
    client->loadSingle(QString("http://127.0.0.1:%1/silence.wav").arg(mediaServer.serverPort()), 0);
    check(until([&] { return loaded || failed; }) && loaded && !failed,
          "real mpv plays HTTP media with inherited proxy env present");
    check(mediaRequests > 0 && proxyRequests == 0, "media reaches origin and never the proxy trap");
    for (const auto &key : envKeys) {
        const auto saved = oldEnv.value(key);
        if (saved.isNull()) qunsetenv(key.constData()); else qputenv(key.constData(), saved);
    }
    client->setPropertyString("network-timeout", "1");
    QTimer uiPulse;
    uiPulse.setInterval(20);
    int pulses = 0;
    QObject::connect(&uiPulse, &QTimer::timeout, [&] { ++pulses; });
    loaded = false;
    failed = false;
    uiPulse.start();
    QElapsedTimer attachTime;
    attachTime.start();
    client->loadDash(filename, QString("http://127.0.0.1:%1/slow-audio.wav").arg(mediaServer.serverPort()), 0);
    check(until([&] { return loaded || failed; }, 3000) && loaded && !failed,
          "slow external DASH audio completes without blocking local HTTP responder");
    uiPulse.stop();
    check(slowAudioRequests > 0 && pulses >= 5 && attachTime.elapsed() < 1800,
          "GUI event-loop timers continue while external audio is loading");
    int loadNotifications = 0;
    QObject::connect(client, &MpvClient::fileLoaded, [&] { ++loadNotifications; });
    const int priorSlowRequests = slowAudioRequests;
    const auto audioAddress = [&](const QString &path) {
        return QString("http://127.0.0.1:%1/%2").arg(mediaServer.serverPort()).arg(path);
    };
    client->loadDashCandidates(filename, {audioAddress("slow-audio.wav"),
                                         audioAddress("obsolete-audio.wav")}, 0);
    check(until([&] { return slowAudioRequests > priorSlowRequests; }),
          "replacement fixture reaches pending external audio request");
    loaded = false;
    failed = false;
    client->loadSingle(filename, 0.25);
    check(until([&] { return loaded || failed; }) && loaded && !failed,
          "new episode loads while previous external audio is pending");
    QElapsedTimer lateReplies;
    lateReplies.start();
    until([&] { return lateReplies.elapsed() >= 500; });
    check(loadNotifications == 1 && !failed,
          "cancelled external audio cannot publish a stale loaded or failure event");
    check(!audioRequests.contains("/obsolete-audio.wav"),
          "episode replacement discards remaining audio candidates");
    const int beforeStoppedAudio = slowAudioRequests;
    client->loadDashCandidates(filename, {audioAddress("slow-audio.wav"),
                                         audioAddress("stopped-audio.wav")}, 0);
    check(until([&] { return slowAudioRequests > beforeStoppedAudio; }),
          "stop fixture reaches pending external audio request");
    client->stop();
    lateReplies.restart();
    until([&] { return lateReplies.elapsed() >= 500; });
    check(loadNotifications == 1 && !failed, "stopped audio load cannot revive playback state");
    check(!audioRequests.contains("/stopped-audio.wav"),
          "stop discards remaining audio candidates");
    loaded = false;
    failed = false;
    client->loadDash(filename, temp.path() + "/missing-audio.wav", 0);
    check(until([&] { return loaded || failed; }) && failed && !loaded,
          "asynchronous external audio failure reports error without false ready state");
    loaded = false;
    failed = false;
    audioRequests.clear();
    const int beforeFallback = loadNotifications;
    client->loadDashCandidates(filename, {audioAddress("missing-audio.wav"),
                                         audioAddress("fallback-audio.wav"),
                                         audioAddress("unused-audio.wav")}, 0);
    check(until([&] { return loaded || failed; }) && loaded && !failed,
          "failed DASH audio advances to next candidate without failing video");
    check(audioRequests == QStringList{"/missing-audio.wav", "/fallback-audio.wav"}
              && loadNotifications == beforeFallback + 1,
          "audio candidates retain API order and stop after first success");
    loaded = false;
    failed = false;
    audioRequests.clear();
    int failureNotifications = 0;
    QObject::connect(client, &MpvClient::playbackError, [&](int, const QString &) {
        ++failureNotifications;
    });
    client->loadDashCandidates(filename, {audioAddress("missing-audio-a.wav"),
                                         audioAddress("missing-audio-b.wav")}, 0);
    check(until([&] { return loaded || failed; }) && failed && !loaded
              && failureNotifications == 1
              && audioRequests == QStringList{"/missing-audio-a.wav", "/missing-audio-b.wav"},
          "all audio candidates exhausted emit a single failure without false readiness");
    loaded = false;
    failed = false;
    audioRequests.clear();
    // A network timeout longer than our candidate limit proves the watchdog
    // advances the audio list, rather than relying on mpv's HTTP failure.
    client->setPropertyString("network-timeout", "30");
    const int beforeTimeout = loadNotifications;
    attachTime.restart();
    client->loadDashCandidates(filename, {audioAddress("stalled-audio.wav"),
                                         audioAddress("timeout-fallback-audio.wav")}, 0);
    check(until([&] { return loaded || failed; }, 11000) && loaded && !failed
              && attachTime.elapsed() >= 7000,
          "audio watchdog cancels stalled candidate and loads its backup");
    lateReplies.restart();
    until([&] { return lateReplies.elapsed() >= 150; });
    check(audioRequests == QStringList{"/stalled-audio.wav", "/timeout-fallback-audio.wav"}
              && loadNotifications == beforeTimeout + 1 && !failed,
          "cancelled audio attempt replies cannot overwrite successful fallback");
    client->stop();
    auto retained = client->sharedHandle();
    std::weak_ptr<mpv_handle> weak = retained;
    delete client;
    double speed = 0;
    check(!weak.expired() && lib->getProperty(retained.get(), "speed", MPV_FORMAT_DOUBLE, &speed) >= 0,
          "renderer handle ownership survives QObject teardown");
    retained.reset();
    check(weak.expired(), "last owner releases mpv handle");

    DanmakuEngine engine;
    engine.setPlayback(1, 1, true, true);
    const double pausedTime = engine.mediaTime();
    QThread::msleep(80);
    check(engine.mediaTime() == pausedTime, "paused media clock stays fixed");
    engine.setPlayback(1, 2, false, true);
    QThread::msleep(20);
    check(engine.mediaTime() > 1.025 && engine.mediaTime() < 1.3,
          "media clock resumes at current speed without paused wall time");
    engine.setPlayback(100, 1, true, true);
    check(engine.mediaTime() == 100, "seek reanchors media clock");
    engine.setPlayback(100, 1, false, true);
    QThread::msleep(20);
    const double beforeCorrection = engine.mediaTime();
    engine.setPlayback(100, 1, false, true);
    check(engine.mediaTime() >= beforeCorrection, "decoder clock correction never jumps backwards");
    engine.setPlayback(99.9, 1, true, true);
    engine.reset(99.9);
    check(engine.mediaTime() == 99.9, "explicit small seek bypasses clock smoothing");
    engine.setImplementation("sprite");
    engine.setImplementation("invalid");
    check(engine.implementation() == "sprite", "engine rejects invalid implementation");
    engine.beginTransition(20);
    engine.setPlayback(0, 1, false, true);
    QThread::msleep(20);
    check(engine.mediaTime() == 20, "start/seek gate holds target despite stale samples");
    engine.beginTransition(40);
    engine.completeTransition(20, 1, false, true);
    check(engine.transitionPending() && engine.mediaTime() == 40, "older seek restart cannot release newer target");
    engine.completeTransition(40, 1, true, true);
    check(!engine.transitionPending() && engine.mediaTime() == 40, "paused restart releases target without animation");
    engine.setPlayback(40, 2, false, true);
    QThread::msleep(30);
    const double spriteBeforeSample = engine.mediaTime();
    engine.setPlayback(39.9, 2, false, true);
    check(engine.mediaTime() >= spriteBeforeSample, "sprite clock ignores decoder quantization without reverse movement");
    engine.setPlayback(40, 2, true, true);
    const double frozen = engine.mediaTime();
    QThread::msleep(30);
    check(engine.mediaTime() == frozen, "sprite pause holds precise visual clock");
    engine.setPlayback(40, 2, false, false);
    QThread::msleep(30);
    check(engine.mediaTime() == frozen, "sprite buffering freezes visual clock");
    engine.beginTransition(10);
    QThread::msleep(110);
    engine.completeTransition(11, 10, true, true);
    check(!engine.transitionPending() && engine.mediaTime() == 11, "delayed restart allows elapsed playback at higher speed");
    engine.reset(frozen);
    engine.setImplementation("scene");
    check(engine.implementation() == "scene" && engine.mediaTime() == frozen, "implementation switch preserves playback position");
    return failures ? 1 : 0;
}
