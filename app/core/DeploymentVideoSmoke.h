#pragma once

#ifdef Q_OS_WIN
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QThread>
#include <QThreadPool>
#include "player/PlayerController.h"

// Runs the actual compiled player, GL renderer and Windows media backend from
// the stripped package. All media and controller persistence remain in scratch.
inline bool runDeploymentVideoSmoke(const QString &scratch) {
    const QString path = scratch + QStringLiteral("/本地 video.mp4");
    if (!QFile::copy(QStringLiteral(":/deployment/screenshot-red-h264.mp4"), path)) return false;
    QOpenGLContext gl;
    gl.setFormat(QSurfaceFormat::defaultFormat());
    if (!gl.create()) return false;
    QOffscreenSurface surface;
    surface.setFormat(gl.format());
    surface.create();
    if (!surface.isValid() || !gl.makeCurrent(&surface)) return false;
    QQuickRenderControl render;
    QQuickWindow window(&render);
    window.setGeometry(0, 0, 128, 72);
    window.setColor(Qt::black);
    window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&gl));
    if (!render.initialize()) return false;
    QOpenGLFramebufferObject fbo(window.size(), QOpenGLFramebufferObject::CombinedDepthStencil);
    window.setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo.texture(), fbo.size()));
    // A hidden native HWND exercises GetForWindow without displaying test UI.
    QWindow mediaWindow;
    mediaWindow.create();
    PlayerController player;
    bool failed = false;
    QObject::connect(&player, &PlayerController::errorOccurred, &player,
                     [&](const QString &) { failed = true; });
    auto draw = [&] {
        if (!gl.makeCurrent(&surface)) { failed = true; return; }
        render.polishItems();
        render.beginFrame();
        render.sync();
        render.render();
        render.endFrame();
    };
    for (int session = 0; session < 2 && !failed; ++session) {
        player.openWith({QVariantMap{{"business", "local"}, {"isLocal", true},
            {"videoKey", QStringLiteral("local:deployment-%1").arg(session)},
            {"localPath", path}, {"title", "Deployment video fixture"}}});
        player.attachMediaWindow(&mediaWindow);
        auto *item = player.videoItem();
        auto *client = item->client();
        if (!client) { failed = true; break; }
        client->setPropertyString("ao", "null");
        item->setParentItem(window.contentItem());
        item->setSize(window.size());
        item->setVisible(true);
        bool pixels = false;
        QElapsedTimer timer;
        timer.start();
        while (!failed && timer.elapsed() < 8000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            draw();
            const QColor color = fbo.toImage().pixelColor(64, 36);
            pixels = color.red() > 240 && color.green() < 15 && color.blue() < 15;
            if (pixels && !player.loading() && client->getPropertyDouble("time-pos") >= 0.5) break;
            QThread::msleep(5);
        }
        if (!pixels || player.loading() || client->getPropertyDouble("time-pos") < 0.5)
            failed = true;
        // Exercise SMTC state/timeline updates after first frame, then detach.
        client->setPaused(true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        player.closeRequested();
        draw();
        item->setParentItem(nullptr);
    }
    player.closeRequested();
    draw();
    player.videoItem()->setParentItem(nullptr);
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    render.invalidate();
    return !failed;
}
#endif
