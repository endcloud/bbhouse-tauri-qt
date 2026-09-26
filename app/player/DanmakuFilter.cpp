#include "player/DanmakuFilter.h"

#include <QHash>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace {
constexpr double WindowSeconds = 10;
constexpr int MaxActiveGroups = 1024;
constexpr int MaxCandidates = 32;
constexpr int MaxFuzzyLength = 128;
constexpr int MaxInputLength = 512;

bool cjk(QChar ch) {
    return ch.unicode() >= 0x2e80 && ch.unicode() <= 0x9fff;
}

QString comparisonText(const QString &source) {
    QString text = source.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
    // Keep word boundaries in Latin text, but ignore spacing between CJK glyphs.
    for (qsizetype i = text.size() - 2; i > 0; --i)
        if (text[i] == u' ' && cjk(text[i - 1]) && cjk(text[i + 1])) text.remove(i, 1);
    qsizetype end = text.size();
    while (end > 0 && (text[end - 1].isPunct() || text[end - 1].isSpace())) --end;
    // Punctuation-only reactions remain distinct ("???" must not equal "!!!").
    if (end > 0) text.truncate(end);
    // Two conventional numeric reactions are repetition, not numeric quantities.
    if (text.size() >= 3 && std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c == u'6'; }))
        return QStringLiteral("666");
    if (text.size() >= 3 && text.front() == u'2'
            && std::all_of(text.cbegin() + 1, text.cend(), [](QChar c) { return c == u'3'; }))
        return QStringLiteral("233");
    return text;
}

QString digits(const QString &text) {
    QString result;
    for (QChar ch : text) if (ch.isDigit()) result += ch;
    return result;
}

// Banded Levenshtein on Unicode scalar values. At most five cells per source
// character are visited; no unbounded quadratic matrix or shared scratch state.
bool nearby(const QList<uint> &a, const QList<uint> &b) {
    const int shorter = int(qMin(a.size(), b.size()));
    const int longer = int(qMax(a.size(), b.size()));
    if (shorter < 6 || longer > MaxFuzzyLength) return false;
    const int limit = shorter >= 12 ? 2 : 1;
    if (longer - shorter > limit) return false;
    constexpr int Far = MaxFuzzyLength + 1;
    std::array<int, MaxFuzzyLength + 1> previous, current;
    previous.fill(Far);
    for (int j = 0; j <= qMin(int(b.size()), limit); ++j) previous[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        current.fill(Far);
        if (i <= limit) current[0] = i;
        int minimum = Far;
        for (int j = qMax(1, i - limit); j <= qMin(int(b.size()), i + limit); ++j) {
            current[j] = qMin(previous[j] + 1,
                              qMin(current[j - 1] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])));
            minimum = qMin(minimum, current[j]);
        }
        if (minimum > limit) return false;
        previous.swap(current);
    }
    return previous[b.size()] <= limit;
}

struct Input {
    QVariant value;
    double time;
    bool valid;
};
struct Group {
    qsizetype output;
    double time;
    int type;
    int count = 1;
    QString text;
    QString numeric;
    QList<uint> scalars;
};
}

QVariantList DanmakuFilter::mergeSimilar(const QVariantList &entries) {
    QList<Input> sorted;
    sorted.reserve(entries.size());
    for (const QVariant &value : entries) {
        const QVariantMap map = value.toMap();
        bool converted = false;
        const double time = map.value(QStringLiteral("time")).toDouble(&converted);
        sorted.append({value, time, converted && std::isfinite(time) && time >= 0});
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const Input &a, const Input &b) {
        if (a.valid != b.valid) return a.valid;
        return a.valid && a.time < b.time;
    });

    QVariantList result;
    result.reserve(entries.size());
    std::deque<Group> active;
    QHash<QString, qsizetype> exact;
    auto key = [](int type, const QString &text) { return QString::number(type) + u':' + text; };
    auto retire = [&] {
        const auto &group = active.front();
        exact.remove(key(group.type, group.text));
        active.pop_front();
    };
    for (const Input &input : sorted) {
        QVariantMap map = input.value.toMap();
        const int type = map.value(QStringLiteral("type")).toInt();
        const QString message = map.value(QStringLiteral("message")).toString();
        if (!input.valid || type < 1 || type > 5 || message.isEmpty() || message.size() > MaxInputLength) {
            result.append(input.value);
            continue;
        }
        while (!active.empty() && input.time - active.front().time > WindowSeconds) retire();
        const QString normalized = comparisonText(message);
        if (normalized.isEmpty()) {
            result.append(input.value);
            continue;
        }
        const QString exactKey = key(type, normalized);
        auto found = exact.constFind(exactKey);
        Group *match = nullptr;
        if (found != exact.cend()) {
            // The deque holds no more than 1024 groups; output indices increase.
            auto group = std::lower_bound(active.begin(), active.end(), *found,
                [](const Group &candidate, qsizetype output) { return candidate.output < output; });
            if (group != active.end() && group->output == *found) match = &*group;
        }
        const auto scalars = normalized.toUcs4();
        const QString numeric = digits(normalized);
        if (!match && scalars.size() >= 6 && scalars.size() <= MaxFuzzyLength) {
            int examined = 0;
            for (auto group = active.rbegin(); group != active.rend() && examined < MaxCandidates; ++group, ++examined) {
                if (group->type != type || group->numeric != numeric) continue;
                if (nearby(scalars, group->scalars)) { match = &*group; break; }
            }
        }
        if (match) {
            ++match->count;
            QVariantMap representative = result[match->output].toMap();
            representative[QStringLiteral("mergeCount")] = match->count;
            // Render the first message verbatim; normalization is comparison-only.
            representative[QStringLiteral("message")] = representative.value(QStringLiteral("mergeOriginalMessage")).toString()
                    + QStringLiteral(" ×%1").arg(match->count);
            result[match->output] = representative;
            continue;
        }
        if (active.size() >= MaxActiveGroups) retire();
        map[QStringLiteral("mergeCount")] = 1;
        map[QStringLiteral("mergeOriginalMessage")] = message;
        const qsizetype output = result.size();
        result.append(map);
        active.push_back({output, input.time, type, 1, normalized, numeric, scalars});
        exact.insert(exactKey, output);
    }
    // Dense identical input can collapse to just a handful of comments. Do not
    // retain the full original reservation in the cached merged result.
    result.squeeze();
    return result;
}
