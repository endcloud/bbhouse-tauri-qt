// Noninteractive offscreen render-control tests. No app UI automation or media/network.
#include "player/DanmakuEngine.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickGraphicsDevice>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOffscreenSurface>
#include <QElapsedTimer>
#include <QDebug>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <memory>
#include <vector>

static QVariantMap entry(int i, int type = 5) {
    return {{"time", 0}, {"type", type}, {"message", QStringLiteral("弹幕测试 中文 Text %1").arg(i)}, {"fontSize", 25}};
}
static int pixels(const QImage &image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y))) ++count;
    return count;
}
int main(int argc, char **argv) {
    const bool gpu = argc > 1 && QByteArray(argv[1]) == "--opengl";
    QQuickWindow::setGraphicsApi(gpu ? QSGRendererInterface::OpenGL : QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *label) { qInfo() << (ok ? "PASS" : "FAIL") << label; if (!ok) ++failures; };
    QOpenGLContext context;
    QOffscreenSurface surface;
    if (gpu) {
        check(context.create(), "offscreen GL context created");
        surface.setFormat(context.format());
        surface.create();
        check(surface.isValid() && context.makeCurrent(&surface), "offscreen GL surface current");
        if (failures) return 1;
    }
    QQuickRenderControl render;
    QQuickWindow window(&render);
    window.setGeometry(0, 0, 640, 360);
    window.setColor(Qt::transparent);
    if (gpu) {
        window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
        check(render.initialize(), "Qt Quick RHI initialized with GL");
    }
    QImage image;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    auto target = [&](int dpr) {
        if (gpu) {
            context.makeCurrent(&surface);
            fbo = std::make_unique<QOpenGLFramebufferObject>(window.size() * dpr,
                                                    QOpenGLFramebufferObject::CombinedDepthStencil);
            auto texture = QQuickRenderTarget::fromOpenGLTexture(fbo->texture(), fbo->size());
            texture.setDevicePixelRatio(dpr);
            window.setRenderTarget(texture);
        } else {
            image = QImage(window.size() * dpr, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpr);
            auto paint = QQuickRenderTarget::fromPaintDevice(&image);
            paint.setDevicePixelRatio(dpr);
            window.setRenderTarget(paint);
        }
    };
    target(1);
    auto *engine = new DanmakuEngine(window.contentItem());
    engine->setSize(window.size());
    auto draw = [&] {
        render.polishItems();
        if (gpu) render.beginFrame();
        render.sync();
        render.render();
        if (gpu) render.endFrame();
    };
    auto capture = [&] { return gpu ? fbo->toImage() : image; };
    engine->loadEntries({entry(1)});
    engine->setPlayback(1, 1, true, true);
    draw();
    check(pixels(capture()) > 100 && engine->liveNodeCount() == 1, "text node renders visible glyphs");
    const auto first = capture();
    const auto builds = engine->nodeBuildCount();
    engine->setPlayback(1, 1, true, true);
    draw();
    check(first == capture() && builds == engine->nodeBuildCount(), "paused image stable and text node reused");
    engine->setOpacityPercent(0);
    draw();
    check(pixels(capture()) == 0 && engine->liveNodeCount() == 0, "opacity zero removes text and outline");
    engine->setOpacityPercent(100);
    draw();
    engine->setEnabled(false);
    draw();
    check(pixels(capture()) == 0, "disabled layer has no glyphs");
    engine->setEnabled(true);
    draw();
    check(pixels(capture()) > 100, "enable rebuilds current timeline");
    engine->setAreaPercent(0);
    draw();
    check(pixels(capture()) == 0, "zero area clears scene nodes");
    engine->setAreaPercent(100);
    engine->loadEntries({entry(2, 1)});
    engine->setPlayback(2, 1, true, true);
    draw();
    const auto moving = capture();
    const auto moveBuilds = engine->nodeBuildCount();
    engine->setPlayback(2.1, 1, true, true);
    draw();
    check(moving != capture() && moveBuilds == engine->nodeBuildCount(), "scroll changes pixels without rebuilding text");
    target(2);
    engine->reset();
    draw();
    check(capture().size() == QSize(1280, 720) && pixels(capture()) > 100, "DPR 2 render target produces glyphs");
    engine->setPlayback(50, 1, true, true);
    draw();
    check(pixels(capture()) == 0, "seek clears expired glyphs");

    // Scene invalidation destroys nodes; reconnecting recreates from GUI model.
    render.invalidate();
    if (gpu) check(render.initialize(), "scene graph reinitialized");
    target(1);
    engine->loadEntries({entry(3)});
    engine->setPlayback(1, 1, true, true);
    draw();
    check(pixels(capture()) > 100, "scene invalidation safely recreates text");

    for (int dpr : {1, 2}) {
        window.setGeometry(0, 0, 1920, 1080);
        engine->setSize(window.size());
        target(dpr);
        QVariantList dense;
        for (int i = 0; i < 32; ++i) dense.append(entry(i, 1));
        engine->setFontSize(20);
        engine->loadEntries(dense);
        engine->setPlayback(5, 1, true, true);
        draw();
        const auto initialBuilds = engine->nodeBuildCount();
        check(engine->liveNodeCount() >= 30, "dense scene has at least 30 text nodes");
        std::vector<double> times;
        for (int i = 0; i < 220; ++i) {
            QElapsedTimer timer; timer.start();
            engine->setPlayback(5 + i * .005, 1, true, true);
            draw();
            if (i >= 20) times.push_back(timer.nsecsElapsed() / 1e6);
        }
        std::sort(times.begin(), times.end());
        qInfo() << (gpu ? "OpenGL" : "software") << "DPR" << dpr
                << "steady frame CPU submit median/P95 ms" << times[100] << times[190];
        check(initialBuilds == engine->nodeBuildCount(), "200 steady frames reuse all text nodes");
    }
    // The sprite mode is forward-only: start exactly at the fixture timestamp,
    // drain preparation while paused, then render from cached image resources.
    engine->setImplementation("sprite");
    engine->setFontSize(25);
    auto settle = [&](int expected) {
        QElapsedTimer deadline; deadline.start();
        do {
            QCoreApplication::processEvents();
            draw();
            if (engine->liveNodeCount() == expected) return true;
            QThread::msleep(2);
        } while (deadline.elapsed() < 3000);
        return false;
    };
    for (int dpr : {1, 2}) {
        target(dpr);
        engine->reset(0);
        QVariantList sprites;
        for (int i = 0; i < 8; ++i) sprites.append(entry(i));
        engine->loadEntries(sprites);
        engine->setPlayback(0, 1, true, true);
        check(settle(8), "paused sprite preparation and bounded uploads drain completely");
        check(pixels(capture()) > 100, "sprite image nodes render text at target DPR");
        const auto cachedImage = capture();
        const auto cachedBuilds = engine->nodeBuildCount();
        engine->setPlayback(0, 1, true, true);
        draw();
        check(cachedImage == capture() && cachedBuilds == engine->nodeBuildCount(),
              "paused sprite image stable and texture nodes reused");
        engine->setOpacityPercent(0); draw();
        check(pixels(capture()) == 0, "sprite opacity zero clears text and outline");
        engine->setOpacityPercent(100);
        engine->reset(0);
        check(settle(8), "sprite cache reusable after reset");
        engine->beginTransition(50); draw();
        check(pixels(capture()) == 0, "pending seek removes all old sprite nodes");
        engine->setPlayback(0, 1, true, true);
        engine->completeTransition(0, 1, true, true); draw();
        check(engine->transitionPending() && pixels(capture()) == 0, "stale restart cannot restore old sprites");
        engine->completeTransition(50, 1, true, true); draw();
        check(!engine->transitionPending() && pixels(capture()) == 0, "seek target has no historical backfill");
    }
    render.invalidate();
    if (gpu) check(render.initialize(), "sprite scene graph reinitialized");
    target(1);
    engine->reset(0);
    engine->loadEntries({entry(4)});
    check(settle(1) && pixels(capture()) > 100, "sprite scene invalidation recreates texture safely");
    engine->reset(0);
    engine->loadEntries({entry(5, 1)});
    check(settle(1), "scrolling sprite prepared at right edge");
    const auto scrollBuilds = engine->nodeBuildCount();
    engine->setPlayback(0, 1, false, true);
    QThread::msleep(120);
    engine->setPlayback(0, 1, false, true); draw();
    const auto scrollImage = capture();
    check(pixels(scrollImage) > 100, "monotonic sprite motion enters the viewport");
    QThread::msleep(60);
    engine->setPlayback(0, 1, false, true); draw();
    check(scrollImage != capture() && scrollBuilds == engine->nodeBuildCount(),
          "scrolling moves image without reuploading texture or reshaping text");
    engine->setPlayback(0, 1, true, true); draw();
    const auto pausedSprite = capture();
    QThread::msleep(40);
    engine->setPlayback(0, 1, true, true); draw();
    check(pausedSprite == capture(), "pause freezes moving sprite pixels");
    engine->setImplementation("scene");
    engine->setPlayback(1, 1, true, true); draw();
    check(engine->liveNodeCount() == 1 && pixels(capture()) > 100, "switch back to existing scene implementation");
    for (const QString &mode : {QStringLiteral("scene"), QStringLiteral("sprite")}) {
        engine->setImplementation(mode);
        engine->reset(0);
        engine->setPlayback(0, 1, true, true);
        engine->setMergeSimilar(false);
        QVariantList duplicates;
        for (int i = 0; i < 8; ++i) duplicates.append(entry(42));
        engine->loadEntries(duplicates);
        check(settle(8), "raw duplicates render separately in both implementations");
        engine->setMergeSimilar(true);
        check(settle(1), "background merge is applied to either renderer");
        const auto merged = capture();
        engine->setMergeSimilar(false);
        check(settle(8), "disabling merge restores immutable originals");
        engine->setMergeSimilar(true);
        check(settle(1) && capture() == merged, "cached merge never doubles count suffixes");

        // The worker finishes independently; only the latest data is accepted,
        // even when a setting changes before the finished signal is delivered.
        QVariantList heavy;
        for (int i = 0; i < 20000; ++i) heavy.append(entry(42));
        engine->loadEntries(heavy);
        engine->setMergeSimilar(false);
        engine->setMergeSimilar(true);
        engine->loadEntries({entry(1), entry(2), entry(3)});
        check(settle(3), "new video coalesces in-flight merge and ignores stale results");
        engine->loadEntries(heavy);
        engine->loadEntries({});
        QElapsedTimer drain; drain.start();
        do {
            QCoreApplication::processEvents();
            draw();
            QThread::msleep(2);
        } while (drain.elapsed() < 100);
        check(engine->liveNodeCount() == 0, "cleared video stays empty when old merge completes");
        engine->setMergeSimilar(false);
        engine->setDensityLimit(3);
        engine->loadEntries(duplicates);
        check(settle(3), "engine forwards density to selected implementation");
        engine->setDensityLimit(0);
        engine->reset(0);
        check(settle(8), "removing density restriction restores original admission");
    }
    {
        auto transient = std::make_unique<DanmakuEngine>();
        QVariantList pending;
        for (int i = 0; i < 20000; ++i) pending.append(entry(42));
        transient->setMergeSimilar(true);
        transient->loadEntries(pending);
        transient.reset();
        check(QThreadPool::globalInstance()->waitForDone(5000),
              "merge worker finishes safely after its engine has been destroyed");
        QCoreApplication::processEvents();
    }
    render.invalidate();
    delete engine;
    window.setRenderTarget(QQuickRenderTarget());
    fbo.reset();
    return failures ? 1 : 0;
}
