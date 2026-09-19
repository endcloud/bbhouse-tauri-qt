// Native OpenGL regression with no visible window, network or user data.
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <functional>
#include <iostream>
#include "player/PlayerController.h"

void setLocalPlayerTestRoot(const QString &path);
int localPlayerCookieReads();

int runLocalPlayerRenderTest(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
        failures += !ok;
        return ok;
    };
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/local-render-XXXXXX");
    if (!check(temp.isValid(), "native video fixtures stay inside build")) return 1;
    setLocalPlayerTestRoot(temp.path());
    const QString firstPath = temp.filePath(QStringLiteral("本地 视频.mp4"));
    const QString secondPath = temp.filePath(QStringLiteral("第二个视频.mp4"));
    if (!check(QFile::copy(QStringLiteral(BBHOUSE_LOCAL_VIDEO_FIXTURE), firstPath) &&
               QFile::copy(firstPath, secondPath), "copy synthetic red H.264 fixtures")) return 1;
    auto entry = [](const QString &id, const QString &path) -> QVariantMap {
        return {{"business", "local"}, {"isLocal", true}, {"videoKey", "local:" + id},
                {"localPath", path}, {"title", "Local render fixture"}};
    };
    QOpenGLContext gl;
    gl.setFormat(format);
    if (!check(gl.create(), "native OpenGL context created")) return 1;
    QOffscreenSurface surface;
    surface.setFormat(gl.format());
    surface.create();
    if (!check(surface.isValid() && gl.makeCurrent(&surface), "invisible OpenGL surface current")) return 1;
    QQuickRenderControl render;
    QQuickWindow window(&render);
    window.setGeometry(0, 0, 128, 72);
    window.setColor(Qt::black);
    window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&gl));
    if (!check(render.initialize(), "production Qt Quick OpenGL renderer initialized")) return 1;
    QOpenGLFramebufferObject fbo(window.size(), QOpenGLFramebufferObject::CombinedDepthStencil);
    window.setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo.texture(), fbo.size()));
    PlayerController player;
    QString error;
    QObject::connect(&player, &PlayerController::errorOccurred,
                     [&](const QString &message) { error = message; });
    player.setVolumePercent(0);
    auto draw = [&] {
        gl.makeCurrent(&surface);
        render.polishItems();
        render.beginFrame();
        render.sync();
        render.render();
        render.endFrame();
    };
    auto pump = [&](int milliseconds) {
        QElapsedTimer timer;
        timer.start();
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(2);
        } while (timer.elapsed() < milliseconds);
    };
    auto redFrame = [&] {
        const QColor center = fbo.toImage().pixelColor(64, 36);
        return center.red() > 240 && center.green() < 15 && center.blue() < 15;
    };
    auto until = [&](const std::function<bool()> &predicate) {
        QElapsedTimer timer;
        timer.start();
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            draw();
            if (predicate()) return true;
            QThread::msleep(2);
        } while (timer.elapsed() < 5000);
        return predicate();
    };
    player.openWith({entry("first", firstPath)}); // DownloadsPage opens before navigation.
    auto *item = player.videoItem();
    auto *client = item->client();
    if (!check(client, "local open creates mpv before QML mount")) return 1;
    client->setPropertyString("ao", "null");
    const auto firstRevision = item->clientRevision();
    pump(300);
    check(player.loading() && !item->renderReady() && client->getPropertyString("path").isEmpty(),
          "unmounted local video remains pending without prematurely loading mpv");
    item->setParentItem(window.contentItem());
    item->setSize(window.size());
    item->setVisible(true);
    check(until([&] { return !player.loading() && item->renderReady() && redFrame(); }) && error.isEmpty(),
          "mounting production MpvVideoItem starts local playback and renders red video pixels");
    check(client->getPropertyString("vid") == "1" &&
              client->getPropertyDouble("video-out-params/w") == 128,
          "video track remains selected with valid libmpv output");

    player.closeRequested();
    draw(); // Retire the old render context while its OpenGL context is current.
    item->setParentItem(nullptr);
    player.openWith({entry("first", firstPath)});
    player.openWith({entry("second", secondPath)});
    client = item->client();
    if (!check(client, "reopening constructs a new playback kernel")) return 1;
    client->setPropertyString("ao", "null");
    item->notifier()->initialized(firstRevision);
    item->notifier()->failed(firstRevision, QStringLiteral("stale render failure"));
    pump(150);
    check(player.loading() && !item->renderReady() && client->getPropertyString("path").isEmpty() && error.isEmpty(),
          "stale renderer-ready and error notifications cannot start a reopened kernel");
    item->setParentItem(window.contentItem());
    check(until([&] {
              return !player.loading() && redFrame() &&
                  client->getPropertyString("path") == secondPath;
          }) && error.isEmpty(), "rapid switch before mount loads and renders only the latest local video");
    player.closeRequested();
    draw();
    item->setParentItem(nullptr);
    player.openWith({entry("cancelled", firstPath)});
    const auto cancelledRevision = item->clientRevision();
    check(player.loading() && !item->renderReady(), "reopened unmounted video awaits renderer");
    player.closeRequested();
    item->notifier()->initialized(cancelledRevision);
    item->notifier()->failed(cancelledRevision, QStringLiteral("cancelled render failure"));
    pump(150);
    check(!item->client() && !item->renderReady() && !player.loading() && error.isEmpty(),
          "closing while waiting cancels queued render notifications without restarting playback");
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(localPlayerCookieReads() == 0 && !QFile::exists(temp.filePath("history.sqlite3")),
          "native rendering and reopen leave online credentials and history untouched");
    render.invalidate();
    return failures ? 1 : 0;
}
