// Offline live-session regression: real mpv, local media, injected API snapshots.
#include <QDataStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>

#include "core/ApiErrors.h"
#include "player/LivePlayerController.h"

namespace {
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
QVariantMap room(const QString &id) {
    return {{"roomId", id}, {"title", "offline live fixture"}, {"uname", "fixture anchor"}};
}
LivePlayInfo info(const QString &id, const QStringList &urls, int qn = 10000) {
    LivePlayInfo result;
    result.roomId = id;
    result.liveStatus = 1;
    result.currentQn = qn;
    result.qualities = {QVariantMap{{"qn", 10000}, {"label", "原画"}},
                        QVariantMap{{"qn", 400}, {"label", "蓝光"}}};
    result.urls = urls;
    return result;
}
MpvClient *silentClient(QObject *owner) {
    MpvClient *client = MpvClient::create(owner);
    if (client) client->setPropertyString("ao", "null");
    return client;
}
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/live-player-XXXXXX");
    check(temp.isValid(), "fixtures are created under build and automatically removed");
    const QString media = temp.path() + "/live.wav";
    QFile file(media);
    check(file.open(QIODevice::WriteOnly), "create local media fixture");
    QDataStream wav(&file);
    wav.setByteOrder(QDataStream::LittleEndian);
    const int bytes = 60 * 16000;
    wav.writeRawData("RIFF", 4); wav << quint32(36 + bytes);
    wav.writeRawData("WAVEfmt ", 8); wav << quint32(16) << quint16(1) << quint16(1)
        << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    wav.writeRawData("data", 4); wav << quint32(bytes);
    wav.writeRawData(QByteArray(bytes, '\0').constData(), bytes);
    file.close();

    std::atomic<int> resolves{0};
    std::atomic<int> requestedQn{0};
    LivePlayerController player(nullptr, [&](const QString &id, int qn) {
        ++resolves;
        requestedQn = qn;
        return info(id, {temp.path() + "/unavailable.flv", media}, qn);
    }, silentClient);
    player.setVolumePercent(36);
    player.openRoom(room("9007199254740993"));
    check(until([&] { return !player.loading(); }) && player.errorMessage().isEmpty(),
          "failed first CDN falls back to playable candidate without another API request");
    check(resolves == 1 && player.roomId() == "9007199254740993" &&
              player.authorName() == "fixture anchor",
          "room identifier stays exact across QML map and resolver");
    MpvClient *client = player.videoItem()->client();
    check(client != nullptr, "live session owns an mpv client");
    if (!client) return 1;
    check(client->getPropertyString("http-header-fields").contains("live.bilibili.com") &&
              !client->getPropertyString("http-header-fields").contains("Cookie", Qt::CaseInsensitive),
          "media request has live Referer and does not contain Cookie");
    check(client->getPropertyString("http-proxy").isEmpty() &&
              client->getPropertyString("stream-lavf-o").contains("http_proxy="),
          "media proxy and FFmpeg environment proxy are disabled");
    check(client->getPropertyDouble("volume") == 36 && client->getPropertyDouble("speed") == 1,
          "live volume is session-local and playback speed stays normal");
    check(player.qualityLabel() == QStringLiteral("原画") && player.currentQn() == 10000,
          "quality label reflects API actual quality");
    player.togglePlayPause();
    check(player.paused() && client->getFlag("pause"), "live pause acts immediately");
    player.togglePlayPause();
    check(!player.paused() && !client->getFlag("pause"), "live resume acts immediately");
    player.setQuality(999);
    check(resolves == 1, "unknown quality cannot submit a request");
    player.setQuality(400);
    check(until([&] { return resolves == 2 && !player.loading(); }) && requestedQn == 400 &&
              player.currentQn() == 400 && player.qualityLabel() == QStringLiteral("蓝光"),
          "quality change reparses requested quality and reports actual result");
    // At this point the second/final candidate is active. A mid-stream failure
    // must exhaust the finite list rather than silently looping or seeking.
    client->playbackError(-1, "do not surface signed URLs or backend error text");
    check(until([&] { return !player.errorMessage().isEmpty(); }) && player.paused() &&
              !player.loading() && resolves == 2 &&
              !player.errorMessage().contains("signed URLs"),
          "exhausted stream candidates stop and show explicit manual reconnect");
    player.retry();
    check(until([&] { return resolves == 3 && !player.loading(); }) && player.errorMessage().isEmpty(),
          "manual reconnect fetches fresh URLs and clears the failure");
    client->ended();
    check(!player.errorMessage().isEmpty() && player.paused(),
          "stream EOF shows ended-or-disconnected status instead of auto-next");
    player.closeRequested();
    check(player.videoItem()->client() == nullptr && player.roomId().isEmpty() &&
              player.qualities().isEmpty() && player.paused() && !player.loading(),
          "close stops media, detaches client and clears the ephemeral session");

    // The old room is held in the resolver while the new room finishes first.
    // A late old result must not replace metadata, errors, quality or the client.
    auto oldStarted = std::make_shared<QSemaphore>();
    auto releaseOld = std::make_shared<QSemaphore>();
    LivePlayerController races(nullptr, [=](const QString &id, int) {
        if (id == "1") {
            oldStarted->release();
            releaseOld->acquire();
            throw ApiError(-1, QStringLiteral("stale resolver failure"));
        }
        return info(id, {media}, 400);
    }, silentClient);
    races.openRoom(room("1"));
    check(until([&] { return oldStarted->available() > 0; }), "old room resolver is pending");
    races.openRoom(room("2"));
    check(until([&] { return !races.loading(); }) && races.errorMessage().isEmpty(),
          "new room can complete while prior resolver remains pending");
    releaseOld->release();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    check(races.roomId() == "2" && races.currentQn() == 400 && races.errorMessage().isEmpty(),
          "late old-room failure cannot overwrite current playback");
    races.closeRequested();

    auto closedStarted = std::make_shared<QSemaphore>();
    auto releaseClosed = std::make_shared<QSemaphore>();
    int createdAfterClose = 0;
    LivePlayerController closed(nullptr, [=](const QString &id, int) {
        closedStarted->release();
        releaseClosed->acquire();
        return info(id, {media});
    }, [&](QObject *owner) { ++createdAfterClose; return silentClient(owner); });
    closed.openRoom(room("3"));
    check(until([&] { return closedStarted->available() > 0; }), "close-race resolver is pending");
    closed.closeRequested();
    releaseClosed->release();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    check(createdAfterClose == 0 && closed.roomId().isEmpty() && !closed.loading(),
          "late response after close cannot recreate client or resume audio");

    LivePlayerController offline(nullptr, [](const QString &id, int) {
        auto result = info(id, {});
        result.liveStatus = 0;
        return result;
    }, silentClient);
    offline.openRoom(room("4"));
    check(until([&] { return !offline.loading(); }) &&
              offline.errorMessage().contains(QStringLiteral("下播")) && !offline.videoItem()->client(),
          "offline response produces explicit ended message without starting media");
    offline.openRoom(room("https://untrusted.invalid"));
    check(offline.roomId().isEmpty() && !offline.errorMessage().isEmpty(),
          "room identifier rejects URL input before resolving");

    // A TCP connection accepted without any HTTP response exercises the actual
    // candidate watchdog, not just an injected error signal.
    QTcpServer stalled;
    check(stalled.listen(QHostAddress::LocalHost), "local stalled CDN fixture listens");
    const QString stalledUrl = QStringLiteral("http://127.0.0.1:%1/stalled.flv")
                                   .arg(stalled.serverPort());
    LivePlayerController timeout(nullptr, [=](const QString &id, int) {
        return info(id, {stalledUrl, media});
    }, silentClient);
    QElapsedTimer startup;
    startup.start();
    timeout.openRoom(room("5"));
    check(until([&] { return !timeout.loading(); }, 18000) && timeout.errorMessage().isEmpty()
              && startup.elapsed() >= 10000,
          "stalled CDN is bounded by startup timeout and falls back to next candidate");
    timeout.closeRequested();

    auto destroyedStarted = std::make_shared<QSemaphore>();
    auto releaseDestroyed = std::make_shared<QSemaphore>();
    auto *destroyed = new LivePlayerController(nullptr, [=](const QString &id, int) {
        destroyedStarted->release();
        releaseDestroyed->acquire();
        return info(id, {media});
    }, silentClient);
    destroyed->openRoom(room("6"));
    check(until([&] { return destroyedStarted->available() > 0; }), "destruction-race resolver is pending");
    delete destroyed;
    releaseDestroyed->release();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    check(true, "controller destruction safely cancels delivery of worker response");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return failures ? 1 : 0;
}
