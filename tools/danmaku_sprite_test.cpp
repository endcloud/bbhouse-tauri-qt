#include "player/DanmakuSpriteLayout.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <QDebug>
#include <cmath>
#include <functional>
#include <limits>

namespace {
QVariantMap entry(double time, int type, const QString &text = QStringLiteral("测试弹幕 Test"),
                  int color = 0xffffff, int fontSize = 25) {
    return {{"time", time}, {"type", type}, {"message", text},
            {"fontColor", color}, {"fontSize", fontSize}};
}
bool pump(DanmakuSpriteLayout &model, double time, const std::function<bool()> &ready) {
    QElapsedTimer timer;
    timer.start();
    do {
        model.advance(time);
        if (ready()) return true;
        QThread::msleep(2);
    } while (timer.elapsed() < 5000);
    return false;
}
int coloredPixels(const QImage &image) {
    int result = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) result += qAlpha(image.pixel(x, y)) > 0;
    return result;
}
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        qInfo() << (ok ? "PASS" : "FAIL") << name;
        if (!ok) ++failures;
    };
    {
        DanmakuSpriteLayout model;
        model.configure({640, 360}, 25, 100, 100, 1);
        model.load({entry(.1, 1), entry(3, 1)});
        check(model.advance(0).isEmpty(), "prefetch does not admit future comments");
        check(model.pendingPreparation() <= DanmakuSpriteLayout::MaxPreparationTasks,
              "async preparation has a fixed task bound");
        check(pump(model, 0, [&] { return model.cacheEntries() == 1 && !model.pendingPreparation(); }),
              "CPU image is prepared asynchronously before admission");
        const auto first = model.advance(.1);
        check(first.size() == 1 && first[0].position.x() == 640 &&
              coloredPixels(*first[0].image) > 100, "ready text enters at right edge with visible pixels");
        if (!first.isEmpty()) {
            const auto image = first[0].image;
            const auto prepared = model.preparedCount();
            model.advance(.6);
            check(model.frame().size() == 1 && model.frame()[0].image == image &&
                  model.frame()[0].position.x() < 640 && model.preparedCount() == prepared,
                  "steady movement reuses the same immutable image");
            const double x = model.frame()[0].position.x();
            model.advance(.6);
            check(model.frame()[0].position.x() == x, "unchanged media time freezes position");
            model.advance(3);
            check(model.preparedCount() == prepared && model.frame().size() == 2 &&
                  model.frame()[1].image == image, "repeated text shares cached image across entries");
        }
        model.reset(3);
        check(model.advance(3).size() == 1 && model.frame()[0].id == 1,
              "explicit seek does not refill earlier on-screen comments");
        model.reset(20);
        check(model.advance(20).isEmpty(), "forward seek skips expired entries");
        model.reset(0);
        check(pump(model, 0, [&] { return !model.pendingPreparation(); }), "seek cancels obsolete work safely");
        model.advance(.3);
        check(model.frame().size() == 1 && model.frame()[0].position.x() == 640,
              "slightly late ready image enters smoothly from edge");
        model.reset(0);
        model.advance(.5);
        check(model.frame().isEmpty(), "late entries outside grace period are dropped");

        model.load({entry(1, 4, "Bottom"), entry(1, 5, "Top")});
        model.reset(0);
        check(pump(model, 0, [&] { return model.cacheEntries() >= 3 && !model.pendingPreparation(); }),
              "centered text prefetch completes");
        auto centers = model.advance(1);
        check(centers.size() == 2 && centers[0].position.y() > centers[1].position.y(),
              "top and bottom comments use separate available lanes");
        model.configure({640, 360}, 25, 100, 0, 1);
        check(model.advance(1).isEmpty() && model.laneCount() == 0 && !std::isfinite(model.nextTime()),
              "zero area stops admission and wakeup scheduling");
        model.configure({640, 10}, 25, 100, 100, 1);
        check(model.advance(1).isEmpty() && !std::isfinite(model.nextTime()),
              "short viewport causes no one-millisecond wake loop");

        model.configure({640, 360}, 25, 100, 100, 1);
        model.load({entry(2, 5, "DPR text")});
        model.reset(2);
        check(pump(model, 2, [&] { return !model.frame().isEmpty(); }), "DPR baseline image prepared");
        auto normal = model.frame();
        const auto generation = model.generation();
        model.configure({640, 360}, 25, 100, 100, 2);
        check(model.generation() != generation && model.frame().isEmpty(), "DPR invalidates old generation");
        check(pump(model, 2, [&] { return !model.frame().isEmpty(); }), "high-DPR replacement prepared");
        if (!normal.isEmpty() && !model.frame().isEmpty()) {
            const auto &high = model.frame()[0];
            check(high.image != normal[0].image && high.image->devicePixelRatio() == 2 &&
                  high.image->width() >= normal[0].image->width() * 2 - 1 &&
                  std::abs(high.size.width() - normal[0].size.width()) <= 1,
                  "DPR doubles physical raster while preserving logical geometry");
        }
        model.load({entry(2, 5, "DPR replacement while pending")});
        model.configure({640, 360}, 25, 100, 100, 1);
        model.advance(2);
        model.configure({640, 360}, 25, 100, 100, 2);
        check(pump(model, 2, [&] { return !model.frame().isEmpty() && !model.pendingPreparation(); }) &&
              model.frame()[0].image->devicePixelRatio() == 2,
              "DPR change cancels in-flight low-resolution images and drains replacements while paused");
        model.load({entry(-1, 1), entry(std::numeric_limits<double>::quiet_NaN(), 1),
                    entry(3, 7), entry(3, 1, " ")});
        check(model.advance(3).isEmpty(), "invalid and unsupported entries are rejected");
    }
    {
        DanmakuSpriteLayout model;
        model.configure({640, 360}, 25, 100, 100, 1);
        QVariantList entries;
        for (int i = 0; i < 20000; ++i) entries.append(entry(0, 1, QString::number(i)));
        model.load(entries);
        model.advance(0);
        check(model.pendingPreparation() <= DanmakuSpriteLayout::MaxPreparationTasks &&
              model.activeCount() <= DanmakuSpriteLayout::MaxActive,
              "dense burst keeps task and active-image counts bounded");
        bool replacementBounded = true;
        for (int i = 0; i < 30; ++i) {
            model.load({entry(0, 1, QString("stale %1").arg(i))});
            model.advance(0);
            replacementBounded &= model.pendingPreparation() <= DanmakuSpriteLayout::MaxPreparationTasks;
        }
        check(replacementBounded, "rapid replacement cannot multiply outstanding workers");
        model.load({entry(0, 5, "CURRENT", 0xff0000)});
        check(pump(model, 0, [&] { return !model.frame().isEmpty() && !model.pendingPreparation(); }),
              "current generation eventually replaces cancelled work");
        bool onlyCurrent = model.frame().size() == 1;
        if (onlyCurrent) {
            const auto &image = *model.frame()[0].image;
            bool red = false;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto pixel = image.pixelColor(x, y);
                    if (pixel.alpha() > 200 && pixel.red() > 150 && pixel.green() < 50) red = true;
                }
            onlyCurrent = red;
        }
        check(onlyCurrent, "stale generation images never appear after replace");
        check(model.cacheBytes() <= DanmakuSpriteLayout::MaxCacheBytes &&
              model.cacheEntries() <= DanmakuSpriteLayout::MaxCacheEntries,
              "cache respects byte and entry limits");
        model.configure({640, 4000}, 96, 100, 100, 4);
        model.load({entry(0, 1, QString(256, QChar('W')), 0xffffff, 100)});
        model.advance(0);
        check(pump(model, 0, [&] { return !model.pendingPreparation(); }) && model.frame().isEmpty(),
              "oversized raster payload is rejected before allocating an enormous image");
    }
    {
        DanmakuSpriteLayout model;
        model.configure({640, 50}, 25, 100, 100, 1);
        model.load({entry(0, 1, "abc"), entry(1, 1, QString(30, QChar('W')))});
        check(model.laneCount() == 1, "collision fixture has exactly one lane");
        check(pump(model, 0, [&] { return model.cacheEntries() == 2 && !model.pendingPreparation(); }),
              "collision candidates are ready ahead of time");
        model.advance(1);
        check(model.frame().size() == 1 && model.frame()[0].id == 0,
              "faster long comment cannot catch the previous shorter comment");
        model.load({entry(100, 1)});
        model.reset(0);
        model.advance(0);
        check(!model.pendingPreparation() && model.nextTime() >= 98,
              "far-future comments do not allocate images outside the preparation window");
    }
    {
        DanmakuSpriteLayout model;
        model.configure({640, 360}, 10, 100, 100, 1);
        bool entryBudget = true;
        for (int i = 0; i < 270; ++i) {
            model.load({entry(0, 5, QString("LRU %1").arg(i))});
            entryBudget &= pump(model, 0, [&] { return !model.frame().isEmpty(); });
            entryBudget &= model.cacheEntries() <= DanmakuSpriteLayout::MaxCacheEntries;
        }
        check(entryBudget && model.cacheEntries() == DanmakuSpriteLayout::MaxCacheEntries,
              "LRU eviction enforces entry limit after hundreds of distinct texts");
        model.configure({640, 360}, 96, 100, 100, 2);
        bool byteBudget = true;
        qint64 highWater = 0;
        for (int i = 0; i < 24; ++i) {
            model.load({entry(0, 5, QString("WWWWWWWW%1").arg(i))});
            byteBudget &= pump(model, 0, [&] { return !model.frame().isEmpty(); });
            highWater = qMax(highWater, model.cacheBytes());
            byteBudget &= model.cacheBytes() <= DanmakuSpriteLayout::MaxCacheBytes;
        }
        check(byteBudget && highWater > DanmakuSpriteLayout::MaxCacheBytes / 2 && model.cacheEntries() < 24,
              "large rasters trigger byte-budget eviction before entry limit");
    }
    {
        DanmakuSpriteLayout model;
        model.configure({640, 360}, 96, 100, 100, 2);
        QVector<std::shared_ptr<const QImage>> held;
        qint64 imageCost = 0;
        bool filled = true;
        for (int i = 0; i < 30; ++i) {
            if (imageCost && model.cacheBytes() + imageCost > DanmakuSpriteLayout::MaxCacheBytes) break;
            model.load({entry(0, 5, QString("WWWWWWWW%1").arg(i, 2, 10, QChar('0')))});
            if (!pump(model, 0, [&] { return !model.frame().isEmpty(); })) { filled = false; break; }
            held.append(model.frame()[0].image);
            imageCost = held.last()->sizeInBytes();
        }
        check(filled && held.size() >= 5, "cache-pressure fixture pins resident images");
        const auto before = model.preparedCount();
        model.load({entry(0, 5, "WWWWWWWW99")});
        model.advance(0);
        check(pump(model, 0, [&] { return !model.pendingPreparation(); }) && model.frame().isEmpty() &&
              model.preparedCount() == before, "full pinned cache drops unavailable image without endless retry");
        model.load({entry(0, 5, ".")});
        check(pump(model, 0, [&] { return !model.frame().isEmpty(); }) && !model.pendingPreparation(),
              "small subsequent record proceeds after a cache-budget rejection while paused");
    }
    {
        QElapsedTimer elapsed;
        elapsed.start();
        {
            DanmakuSpriteLayout model;
            model.configure({640, 360}, 25, 100, 100, 2);
            model.load({entry(0, 1, "destroy while worker runs")});
            model.advance(0);
        }
        check(elapsed.elapsed() < 250, "destruction does not wait for background rendering");
    }
    {
        DanmakuSpriteLayout model;
        model.configure({1920, 1080}, 25, 100, 100, 1);
        QVariantList mixed;
        for (int i = 0; i < 60; ++i)
            mixed.append(entry(0, i % 3 == 0 ? 1 : i % 3 == 1 ? 4 : 5, "Same cached text"));
        model.setDensityLimit(20);
        model.load(mixed);
        check(pump(model, 0, [&] { return model.activeCount() == 20; }),
              "sprite density shares one budget across all modes");
        model.advance(4.1);
        check(model.activeCount() < 20, "density drops excess comments without delayed replay");
        model.setDensityLimit(0);
        model.reset(0);
        check(pump(model, 0, [&] { return model.activeCount() > 20; }),
              "sprite unlimited restores admission above configured cap");
        model.configure({1920, 1080}, 48, 100, 25, 1);
        model.reset(0);
        check(pump(model, 0, [&] { return !model.frame().isEmpty(); }), "quarter-screen large text prepared");
        bool inside = true;
        for (const auto &visual : model.frame())
            inside &= visual.position.y() >= 0 && visual.position.y() + visual.size.height() <= 270;
        check(inside, "sprite mixed modes stay fully within top quarter at large font size");
    }
    {
        DanmakuSpriteLayout model;
        model.configure({640, 360}, 25, 100, 100, 2);
        model.load({entry(0, 1, "close releases cached image")});
        check(pump(model, 0, [&] { return !model.frame().isEmpty(); }),
              "close fixture prepares a cached image");
        std::weak_ptr<const QImage> image;
        if (!model.frame().isEmpty()) image = model.frame().first().image;
        model.load({});
        check(model.cacheBytes() == 0 && model.cacheEntries() == 0 && image.expired(),
              "empty load releases cache and active image ownership");
        model.load({entry(0, 1, "obsolete preparation")});
        model.advance(0);
        model.load({});
        check(pump(model, 0, [&] { return !model.pendingPreparation(); })
                  && model.cacheBytes() == 0 && model.frame().isEmpty(),
              "in-flight preparation cannot repopulate cleared image cache");
    }
    // Test teardown keeps QGuiApplication alive until Qt font workers exit.
    QThreadPool::globalInstance()->waitForDone();
    return failures ? 1 : 0;
}
