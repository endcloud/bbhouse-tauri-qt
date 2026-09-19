#ifndef SUBTITLE_TIMELINE_H
#define SUBTITLE_TIMELINE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// In-memory Bilibili captions. No signed URL or subtitle body is persisted.
// Prefix maximum end times allow overlapping cues and efficient random seeks.
class SubtitleTimeline {
public:
    static SubtitleTimeline fromJson(const QByteArray &json) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(json, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()
                || !document.object().value("body").isArray())
            throw std::runtime_error("Invalid subtitle JSON");
        SubtitleTimeline result;
        for (const auto &value : document.object().value("body").toArray()) {
            const auto item = value.toObject();
            const double from = item.value("from").toDouble(-1);
            const double to = item.value("to").toDouble(-1);
            const QString text = item.value("content").toString().trimmed();
            if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to <= from || text.isEmpty()) continue;
            result.cues_.append({from, to, 0, text});
        }
        std::stable_sort(result.cues_.begin(), result.cues_.end(),
                         [](const Cue &a, const Cue &b) { return a.from < b.from; });
        double lastEnd = 0;
        for (auto &cue : result.cues_) cue.maxEnd = lastEnd = std::max(lastEnd, cue.to);
        return result;
    }

    QString textAt(double position) const {
        const auto end = std::upper_bound(cues_.cbegin(), cues_.cend(), position,
                                         [](double p, const Cue &cue) { return p < cue.from; });
        QStringList lines;
        auto it = end;
        while (it != cues_.cbegin()) {
            --it;
            if (it->maxEnd <= position) break;
            if (position < it->to) lines.prepend(it->text);
        }
        return lines.join('\n');
    }
private:
    struct Cue { double from; double to; double maxEnd; QString text; };
    QVector<Cue> cues_;
};

#endif
