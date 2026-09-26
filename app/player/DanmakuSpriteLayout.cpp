#include "player/DanmakuSpriteLayout.h"
#include "player/DanmakuLayout.h"

#include <QFontMetricsF>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSet>
#include <QTextLayout>
#include <QTextOption>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int MaxScannedPerFrame = 128;
struct Preparation {
    QString key;
    quint64 epoch;
    std::shared_ptr<const QImage> image;
};
struct Mailbox {
    QMutex mutex;
    quint64 epoch = 0;
    bool alive = true;
    int running = 0; // includes cancelled generations until their workers exit
    QVector<Preparation> ready;
};

// CPU-only, thread-local shaping/painting. Follows the pre-rendered text surface
// approach of DanmakuFrostMaster and Danmaku's canvas renderer, using public Qt.
std::shared_ptr<const QImage> rasterize(const QString &text, QFont font,
                                       QColor color, qreal dpr) {
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(option);
    layout.beginLayout();
    auto line = layout.createLine();
    if (line.isValid()) line.setLineWidth(1e6);
    layout.endLayout();
    if (!line.isValid()) return {};
    const qreal padding = 3;
    const QSize physical(qCeil((line.naturalTextWidth() + padding * 2) * dpr),
                         qCeil((line.height() + padding * 2) * dpr));
    if (physical.isEmpty() || physical.width() > 4096 || physical.height() > 1024 ||
        qint64(physical.width()) * physical.height() * 4 > DanmakuSpriteLayout::MaxImageBytes)
        return {};
    auto image = std::make_shared<QImage>(physical, QImage::Format_ARGB32_Premultiplied);
    if (image->isNull()) return {};
    image->setDevicePixelRatio(dpr);
    image->fill(Qt::transparent);
    QPainter painter(image.get());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    // Render outline as offset text coverage, then normal text. Unlike glyph
    // paths, QTextLayout retains font fallback and color-font rendering.
    const QColor outline = qGray(color.rgb()) < 60 ? Qt::white : Qt::black;
    painter.setPen(outline);
    const QPointF origin(padding, padding);
    for (const QPointF &offset : {QPointF(-1, 0), QPointF(1, 0), QPointF(0, -1), QPointF(0, 1)})
        layout.draw(&painter, origin + offset);
    painter.setPen(color);
    layout.draw(&painter, origin);
    return image;
}
}

struct DanmakuSpriteLayout::Private {
    struct Entry { double time; int type; double scale; QColor color; QString text; };
    struct Active { Visual visual; double start; double end; double velocity; bool centered; };
    struct Cached { std::shared_ptr<const QImage> image; qint64 bytes; quint64 used; };
    QVector<Entry> entries;
    QVector<Active> active;
    QVector<Visual> frame;
    QVector<int> waiting;
    QVector<QPair<double, double>> scroll;
    QVector<double> center;
    QHash<QString, Cached> cache;
    QSet<QString> pendingKeys;
    QSet<QString> failedKeys;
    std::shared_ptr<Mailbox> mailbox = std::make_shared<Mailbox>();
    QSizeF viewport;
    int pixels = 25, speedPercent = 100, areaPercent = 100;
    int densityLimit = 0;
    qreal dpr = 1;
    double maxScale = 1, lineHeight = 36, time = 0;
    int next = 0, prefetch = 0;
    quint64 generation = 0, prepared = 0, usage = 0;
    qint64 bytes = 0;

    QString key(const Entry &entry) const {
        return QString::number(qMax(10, qRound(pixels * entry.scale))) + '/' +
            QString::number(dpr, 'g', 12) + '/' + QString::number(entry.color.rgba()) + '/' + entry.text;
    }
    int lower(double value) const {
        return int(std::lower_bound(entries.cbegin(), entries.cend(), value,
            [](const Entry &entry, double t) { return entry.time < t; }) - entries.cbegin());
    }
    int upper(double value) const {
        return int(std::upper_bound(entries.cbegin(), entries.cend(), value,
            [](double t, const Entry &entry) { return t < entry.time; }) - entries.cbegin());
    }
    void invalidate() {
        QMutexLocker lock(&mailbox->mutex);
        mailbox->epoch = ++generation;
        mailbox->ready.clear();
        pendingKeys.clear();
        failedKeys.clear();
    }
    void fail(const QString &key) {
        if (failedKeys.size() >= MaxCacheEntries) failedKeys.clear();
        failedKeys.insert(key);
    }
    bool makeSpace(qint64 required) {
        while (bytes + required > MaxCacheBytes || cache.size() >= MaxCacheEntries) {
            auto oldest = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                // Active/frame images stay inside the same accounting budget.
                if (it->image.use_count() != 1) continue;
                if (oldest == cache.end() || it->used < oldest->used) oldest = it;
            }
            if (oldest == cache.end()) return false;
            bytes -= oldest->bytes;
            cache.erase(oldest);
        }
        return true;
    }
    void collect() {
        QVector<Preparation> completed;
        {
            QMutexLocker lock(&mailbox->mutex);
            completed.swap(mailbox->ready);
        }
        for (auto &result : completed) {
            pendingKeys.remove(result.key);
            if (result.epoch != generation) continue;
            if (!result.image) { fail(result.key); continue; }
            const qint64 cost = result.image->sizeInBytes();
            if (!makeSpace(cost)) { fail(result.key); continue; }
            cache.insert(result.key, {std::move(result.image), cost, ++usage});
            bytes += cost;
            ++prepared;
        }
    }
    bool prepare(const Entry &entry) {
        const QString cacheKey = key(entry);
        if (cache.contains(cacheKey) || pendingKeys.contains(cacheKey) || failedKeys.contains(cacheKey))
            return true;
        const auto state = mailbox;
        const quint64 epoch = generation;
        {
            QMutexLocker lock(&state->mutex);
            // Completed-but-uncollected images count too; repeated advance/reset
            // cannot queue unlimited work while another generation is running.
            if (state->running + state->ready.size() >= MaxPreparationTasks) return false;
            ++state->running;
        }
        pendingKeys.insert(cacheKey);
        const QFont font = DanmakuLayout::platformFont(qMax(10, qRound(pixels * entry.scale)));
        const QString text = entry.text;
        const QColor color = entry.color;
        const qreal ratio = dpr;
        QThreadPool::globalInstance()->start([state, epoch, cacheKey, font, text, color, ratio] {
            {
                QMutexLocker lock(&state->mutex);
                if (!state->alive || state->epoch != epoch) { --state->running; return; }
            }
            auto image = rasterize(text, font, color, ratio);
            QMutexLocker lock(&state->mutex);
            --state->running;
            if (state->alive && state->epoch == epoch)
                state->ready.append({cacheKey, epoch, std::move(image)});
        });
        return true;
    }
    bool admit(int id, const std::shared_ptr<const QImage> &image) {
        const auto &entry = entries[id];
        const QSizeF size = image->deviceIndependentSize();
        if (size.height() > lineHeight || active.size() >= (densityLimit > 0 ? densityLimit : MaxActive)) return false;
        const bool centered = entry.type == 4 || entry.type == 5;
        const double duration = (centered ? .04 : .12) * speedPercent;
        const double velocity = (viewport.width() + size.width()) / duration;
        for (int k = 0; k < scroll.size(); ++k) {
            const int lane = entry.type == 4 ? scroll.size() - k - 1 : k;
            if (centered) {
                if (time < center[lane]) continue;
                center[lane] = time + duration;
            } else {
                // Both tests use actual admission time. A slightly late prepared
                // image enters at the right edge, never halfway across the screen.
                if (time < scroll[lane].first || time + viewport.width() / velocity < scroll[lane].second)
                    continue;
                scroll[lane] = {time + size.width() / velocity, time + duration};
            }
            Visual visual{quint64(id), image, {0, lane * lineHeight}, size};
            active.append({visual, time, time + duration, velocity, centered});
            return true;
        }
        return false;
    }
};

DanmakuSpriteLayout::DanmakuSpriteLayout() : d(std::make_unique<Private>()) {}
DanmakuSpriteLayout::~DanmakuSpriteLayout() {
    QMutexLocker lock(&d->mailbox->mutex);
    d->mailbox->alive = false;
    d->mailbox->ready.clear();
    // The mailbox outlives us if a worker is running; no waitForDone() here.
}

void DanmakuSpriteLayout::load(const QVariantList &entries) {
    d->entries.clear();
    d->maxScale = 1;
    for (const QVariant &value : entries) {
        const auto map = value.toMap();
        const double time = map.value("time").toDouble();
        const int type = map.value("type").toInt();
        QString text = map.value("message").toString();
        const int merged = map.value("mergeCount", 1).toInt();
        if (text.size() > 256 && merged > 1 && map.contains("mergeOriginalMessage")) {
            // Preserve the count badge when the existing texture text budget
            // truncates a long representative. Never cut a Unicode surrogate.
            const QString suffix = QStringLiteral("… ×%1").arg(merged);
            text = map.value("mergeOriginalMessage").toString().left(256 - suffix.size());
            if (!text.isEmpty() && text.back().isHighSurrogate()) text.chop(1);
            text += suffix;
        } else text = text.left(256);
        if (!text.isEmpty() && text.back().isHighSurrogate()) text.chop(1);
        text.replace('\n', ' ');
        text.replace('\r', ' ');
        double scale = map.value("fontSize", 25).toDouble() / 25.;
        if (!std::isfinite(time) || time < 0 || type < 1 || type > 5 || text.trimmed().isEmpty()) continue;
        scale = std::isfinite(scale) ? qBound(.4, scale, 4.) : 1.;
        d->maxScale = qMax(d->maxScale, scale);
        d->entries.append({time, type, scale, QColor::fromRgb(map.value("fontColor", 0xffffff).toUInt()), text});
    }
    std::stable_sort(d->entries.begin(), d->entries.end(),
        [](const Private::Entry &a, const Private::Entry &b) { return a.time < b.time; });
    reset(d->time);
    if (entries.isEmpty()) {
        // An empty load marks a closed video or renderer switch, unlike seek.
        // The persistent engine must not retain the previous video's rasters.
        d->cache.clear();
        d->bytes = 0;
        d->entries.squeeze();
    }
}

void DanmakuSpriteLayout::configure(QSizeF viewport, int pixels, int speedPercent,
                                   int areaPercent, qreal dpr) {
    if (!std::isfinite(viewport.width()) || !std::isfinite(viewport.height())) viewport = {};
    viewport = {qMax(0., viewport.width()), qMax(0., viewport.height())};
    pixels = qBound(10, pixels, 96);
    speedPercent = qBound(10, speedPercent, 500);
    areaPercent = qBound(0, areaPercent, 100);
    dpr = std::isfinite(dpr) ? qBound(qreal(.5), dpr, qreal(4)) : 1.;
    if (d->viewport == viewport && d->pixels == pixels && d->speedPercent == speedPercent &&
        d->areaPercent == areaPercent && qFuzzyCompare(d->dpr, dpr)) return;
    const bool textChanged = pixels != d->pixels || !qFuzzyCompare(d->dpr, dpr);
    d->viewport = viewport;
    d->pixels = pixels;
    d->speedPercent = speedPercent;
    d->areaPercent = areaPercent;
    d->dpr = dpr;
    reset(d->time);
    if (textChanged) { d->cache.clear(); d->bytes = 0; }
}

void DanmakuSpriteLayout::reset(double time) {
    if (!std::isfinite(time) || time < 0) time = 0;
    d->time = time;
    d->invalidate();
    d->active.clear();
    d->frame.clear();
    d->waiting.clear();
    const QFont largest = DanmakuLayout::platformFont(qMax(10, qRound(d->pixels * d->maxScale)));
    d->lineHeight = QFontMetricsF(largest).height() + 7.;
    const int lanes = qBound(0, int(d->viewport.height() * d->areaPercent / 100. / d->lineHeight), 128);
    d->scroll.fill({0, 0}, lanes);
    d->center.fill(0, lanes);
    d->next = d->prefetch = d->lower(time);
}

void DanmakuSpriteLayout::setDensityLimit(int value) {
    value = qBound(0, value, MaxActive);
    if (d->densityLimit == value) return;
    d->densityLimit = value;
    reset(d->time);
}

const QVector<DanmakuSpriteLayout::Visual> &DanmakuSpriteLayout::advance(double time) {
    if (!std::isfinite(time) || time < 0) return d->frame;
    if (time < d->time - .5) reset(time);
    d->time = qMax(time, d->time);
    d->frame.clear();
    d->active.erase(std::remove_if(d->active.begin(), d->active.end(),
        [this](const Private::Active &a) { return d->time >= a.end; }), d->active.end());
    d->collect();
    if (d->scroll.isEmpty() || d->viewport.width() <= 0) return d->frame;

    // Jump over missed entries in O(log n), instead of replaying a backlog.
    d->next = qMax(d->next, d->lower(d->time - MaximumLateness));
    int scanned = 0;
    while (d->next < d->entries.size() && d->entries[d->next].time <= d->time) {
        if (++scanned > MaxScannedPerFrame || d->waiting.size() >= MaxScannedPerFrame) {
            d->next = d->upper(d->time);
            break;
        }
        d->waiting.append(d->next++);
    }
    for (auto it = d->waiting.begin(); it != d->waiting.end();) {
        // Drop excess due entries before preparing more text; never queue them
        // until a slot eventually opens, which would replay stale comments.
        if (d->active.size() >= (d->densityLimit > 0 ? d->densityLimit : MaxActive)) {
            d->waiting.clear();
            break;
        }
        const auto &entry = d->entries[*it];
        const QString key = d->key(entry);
        if (d->time - entry.time > MaximumLateness || d->failedKeys.contains(key)) {
            it = d->waiting.erase(it);
            continue;
        }
        auto cached = d->cache.find(key);
        if (cached != d->cache.end()) {
            cached->used = ++d->usage;
            d->admit(*it, cached->image);
            it = d->waiting.erase(it);
        } else {
            d->prepare(entry);
            ++it;
        }
    }
    d->prefetch = qMax(d->prefetch, d->next);
    scanned = 0;
    while (d->prefetch < d->entries.size() &&
           d->entries[d->prefetch].time <= d->time + PreparationWindow && ++scanned <= MaxScannedPerFrame) {
        if (!d->prepare(d->entries[d->prefetch])) break;
        ++d->prefetch;
    }
    d->frame.reserve(d->active.size());
    for (const auto &active : d->active) {
        auto visual = active.visual;
        visual.position.setX(active.centered ? (d->viewport.width() - visual.size.width()) / 2.
            : d->viewport.width() - active.velocity * (d->time - active.start));
        d->frame.append(std::move(visual));
    }
    return d->frame;
}

const QVector<DanmakuSpriteLayout::Visual> &DanmakuSpriteLayout::frame() const { return d->frame; }
double DanmakuSpriteLayout::nextTime() const {
    if (d->scroll.isEmpty() || d->viewport.width() <= 0) return std::numeric_limits<double>::infinity();
    if (!d->waiting.isEmpty() || pendingPreparation() > 0) return d->time + .016;
    double next = d->next < d->entries.size() ? d->entries[d->next].time : std::numeric_limits<double>::infinity();
    if (d->prefetch < d->entries.size())
        next = qMin(next, d->entries[d->prefetch].time - PreparationWindow);
    return qMax(d->time + .016, next);
}
quint64 DanmakuSpriteLayout::generation() const { return d->generation; }
int DanmakuSpriteLayout::pendingPreparation() const {
    QMutexLocker lock(&d->mailbox->mutex);
    return d->mailbox->running + d->mailbox->ready.size();
}
quint64 DanmakuSpriteLayout::preparedCount() const { return d->prepared; }
int DanmakuSpriteLayout::activeCount() const { return d->active.size(); }
int DanmakuSpriteLayout::laneCount() const { return d->scroll.size(); }
qint64 DanmakuSpriteLayout::cacheBytes() const { return d->bytes; }
int DanmakuSpriteLayout::cacheEntries() const { return d->cache.size(); }
