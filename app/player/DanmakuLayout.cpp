#include "player/DanmakuLayout.h"

#include <QFontMetricsF>
#include <QTextOption>
#include <algorithm>
#include <cmath>
#include <limits>

QFont DanmakuLayout::platformFont(int pixels) {
    QFont font;
#ifdef Q_OS_WIN
    font.setFamilies({QStringLiteral("Microsoft YaHei"), QStringLiteral("Microsoft YaHei UI"),
                      QStringLiteral("Noto Sans CJK SC"), QStringLiteral("sans-serif")});
#elif defined(Q_OS_MACOS)
    font.setFamilies({QStringLiteral("PingFang SC"), QStringLiteral("Heiti SC"),
                      QStringLiteral("sans-serif")});
#else
    font.setFamilies({QStringLiteral("Noto Sans CJK SC"), QStringLiteral("WenQuanYi Micro Hei"),
                      QStringLiteral("sans-serif")});
#endif
    font.setPixelSize(pixels);
    font.setWeight(QFont::Medium);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

void DanmakuLayout::load(const QVariantList &entries) {
    entries_.clear();
    if (entries.isEmpty()) entries_.squeeze();
    maxScale_ = 1;
    for (const auto &value : entries) {
        const auto map = value.toMap();
        const double time = map.value("time").toDouble();
        const int type = map.value("type").toInt();
        double scale = map.value("fontSize", 25).toDouble() / 25.;
        const QString message = map.value("message").toString();
        if (!std::isfinite(time) || time < 0 || type < 1 || type > 5 || message.isEmpty()) continue;
        if (!std::isfinite(scale)) scale = 1;
        scale = qBound(0.4, scale, 4.0);
        maxScale_ = qMax(maxScale_, scale);
        entries_.append({time, type, scale,
                         QColor::fromRgb(map.value("fontColor", 0xffffff).toUInt()), message});
    }
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const Entry &a, const Entry &b) { return a.time < b.time; });
    reset(time_);
}

void DanmakuLayout::configure(QSizeF viewport, int pixels, int speedPercent, int areaPercent) {
    pixels = qBound(10, pixels, 96);
    speedPercent = qBound(10, speedPercent, 500);
    areaPercent = qBound(0, areaPercent, 100);
    if (viewport == viewport_ && pixels == pixels_ && speedPercent == speedPercent_
            && areaPercent == areaPercent_) return;
    viewport_ = viewport;
    pixels_ = pixels;
    font_ = platformFont(pixels);
    speedPercent_ = speedPercent;
    areaPercent_ = areaPercent;
    reset(time_);
}

void DanmakuLayout::reset(double time) {
    time_ = qMax(0., time);
    ++generation_;
    active_.clear();
    frame_.clear();
    QFont largest = font_;
    largest.setPixelSize(qMax(10, qRound(pixels_ * maxScale_)));
    lineHeight_ = qMax(pixels_ * 1.2 * maxScale_, QFontMetricsF(largest).height() + 4.);
    const int lanes = qBound(0, int(viewport_.height() * areaPercent_ / 100. / lineHeight_), 128);
    scrollLines_.fill({0, 0}, lanes);
    centerLines_.fill(0, lanes);
    const double oldest = time_ - 0.12 * speedPercent_;
    next_ = int(std::lower_bound(entries_.cbegin(), entries_.cend(), oldest,
        [](const Entry &entry, double t) { return entry.time < t; }) - entries_.cbegin());
}

void DanmakuLayout::setDensityLimit(int value) {
    value = qBound(0, value, 256);
    if (densityLimit_ == value) return;
    densityLimit_ = value;
    reset(time_);
}

const QVector<DanmakuLayout::Visual> &DanmakuLayout::advance(double time) {
    if (!std::isfinite(time) || time < 0) return frame_;
    if (time < time_ - 0.5) reset(time);
    // Small decoder-clock corrections must not rebuild all text every poll.
    time = qMax(time, time_);
    time_ = time;
    active_.erase(std::remove_if(active_.begin(), active_.end(),
        [time](const Active &item) { return time >= item.end; }), active_.end());
    frame_.clear();
    if (scrollLines_.isEmpty() || viewport_.width() <= 0) return frame_;
    const double cross = 0.12 * speedPercent_;
    const double center = 0.04 * speedPercent_;
    // Bound new text preparation per frame. Dense bursts discard excess due
    // entries, rather than delaying them and piling up a stale work queue.
    int preparedThisFrame = 0;
    while (next_ < entries_.size() && entries_[next_].time <= time) {
        const int id = next_++;
        const auto &entry = entries_[id];
        const bool centered = entry.type == 4 || entry.type == 5;
        const double end = entry.time + (centered ? center : cross);
        if (time >= end || active_.size() >= (densityLimit_ > 0 ? densityLimit_ : 256)) continue;
        if (preparedThisFrame >= 64) {
            next_ = int(std::upper_bound(entries_.cbegin() + next_, entries_.cend(), time,
                [](double t, const Entry &e) { return t < e.time; }) - entries_.cbegin());
            break;
        }
        // Reject occupied lanes before shaping, the common dense-stream case.
        bool freeLane = false;
        for (int lane = 0; lane < scrollLines_.size(); ++lane) {
            if (entry.time >= (centered ? centerLines_[lane] : scrollLines_[lane].first)) {
                freeLane = true;
                break;
            }
        }
        if (!freeLane) continue;
        QFont font = font_;
        font.setPixelSize(qMax(10, qRound(pixels_ * entry.scale)));
        // Cap pathological payloads; normal Bilibili comment lengths are much smaller.
        auto layout = std::make_shared<QTextLayout>(entry.message.left(1024), font);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        layout->setTextOption(option);
        layout->setCacheEnabled(true);
        layout->beginLayout();
        auto line = layout->createLine();
        if (line.isValid()) {
            line.setLineWidth(1e6);
            line.setPosition(QPointF(2, 2));
        }
        layout->endLayout();
        ++preparedThisFrame;
        ++preparedCount_;
        if (!line.isValid()) continue;
        const QSizeF size(line.naturalTextWidth() + 4., line.height() + 4.);
        const double velocity = (viewport_.width() + size.width()) / cross;
        int chosen = -1;
        for (int k = 0; k < scrollLines_.size(); ++k) {
            const int lane = entry.type == 4 ? scrollLines_.size() - k - 1 : k;
            if (centered) {
                if (entry.time < centerLines_[lane]) continue;
                centerLines_[lane] = end;
            } else {
                if (entry.time < scrollLines_[lane].first ||
                    entry.time + viewport_.width() / velocity < scrollLines_[lane].second) continue;
                scrollLines_[lane] = {entry.time + size.width() / velocity, end};
            }
            chosen = lane;
            break;
        }
        if (chosen < 0) continue;
        Visual visual{quint64(id), layout, entry.color, QPointF(0, chosen * lineHeight_), size};
        active_.append({visual, entry.time, end, velocity, centered});
    }
    frame_.reserve(active_.size());
    for (const auto &item : active_) {
        Visual visual = item.visual;
        visual.position.setX(item.centered ? (viewport_.width() - visual.size.width()) / 2.
            : viewport_.width() - item.velocity * (time - item.start));
        frame_.append(std::move(visual));
    }
    return frame_;
}

double DanmakuLayout::nextTime() const {
    return next_ < entries_.size() ? entries_[next_].time : std::numeric_limits<double>::infinity();
}
