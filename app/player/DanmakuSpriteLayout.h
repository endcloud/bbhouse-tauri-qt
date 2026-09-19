#ifndef DANMAKU_SPRITE_LAYOUT_H
#define DANMAKU_SPRITE_LAYOUT_H

#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QVariantList>
#include <QVector>
#include <memory>

// GUI-thread model. Workers own only copied font/text and a shared result mailbox;
// no QObject/QSG access, callbacks into the model, or blocking destructor.
class DanmakuSpriteLayout {
public:
    struct Visual {
        quint64 id = 0;
        std::shared_ptr<const QImage> image;
        QPointF position;
        QSizeF size; // logical pixels; image storage is scaled by devicePixelRatio
    };
    static constexpr int MaxPreparationTasks = 4;
    static constexpr int MaxActive = 128;
    static constexpr int MaxCacheEntries = 256;
    static constexpr qint64 MaxCacheBytes = 16 * 1024 * 1024;
    static constexpr qint64 MaxImageBytes = 2 * 1024 * 1024;
    static constexpr double PreparationWindow = 2.0;
    static constexpr double MaximumLateness = 0.25;

    DanmakuSpriteLayout();
    ~DanmakuSpriteLayout();
    DanmakuSpriteLayout(const DanmakuSpriteLayout &) = delete;
    DanmakuSpriteLayout &operator=(const DanmakuSpriteLayout &) = delete;

    void load(const QVariantList &entries);
    void configure(QSizeF viewport, int pixels, int speedPercent, int areaPercent, qreal dpr);
    void setDensityLimit(int value);
    // Explicit seek: first eligible entry is at/after time, never the past 12 s.
    void reset(double time);
    const QVector<Visual> &advance(double time);
    const QVector<Visual> &frame() const;
    // Next media-time wakeup includes prefetch/retry; callers should poll pending
    // preparation even while paused (same media time), until pending work drains.
    double nextTime() const;
    quint64 generation() const;
    int pendingPreparation() const;
    quint64 preparedCount() const;
    int activeCount() const;
    int laneCount() const;
    qint64 cacheBytes() const;
    int cacheEntries() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif
