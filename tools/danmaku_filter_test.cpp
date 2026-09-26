#include "player/DanmakuFilter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <future>
#include <limits>

namespace {
QVariantMap entry(double time, QString message, int type = 1) {
    return {{"time", time}, {"type", type}, {"message", message}, {"fontSize", 25}, {"fontColor", 0xffffff}};
}
int count(const QVariant &value) { return value.toMap().value("mergeCount").toInt(); }
QString message(const QVariant &value) { return value.toMap().value("message").toString(); }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) ++failures;
    };
    auto first = entry(1, QStringLiteral("好耶！"));
    first["fontColor"] = 0xff0000;
    first["fontSize"] = 32;
    const QVariantList source{entry(3, QStringLiteral("好耶")), first, entry(2, QStringLiteral("好耶!!!"))};
    const auto merged = DanmakuFilter::mergeSimilar(source);
    check(merged.size() == 1 && count(merged[0]) == 3 && message(merged[0]) == QStringLiteral("好耶！ ×3"),
          "punctuation-normalized duplicates show a visible total");
    check(merged[0].toMap().value("time").toDouble() == 1
              && merged[0].toMap().value("fontColor").toUInt() == 0xff0000
              && merged[0].toMap().value("fontSize").toInt() == 32,
          "unsorted input retains earliest timestamp and style");
    check(source[1].toMap() == first && !source[0].toMap().contains("mergeCount"),
          "processing never mutates raw input");
    auto result = DanmakuFilter::mergeSimilar({entry(0, "same"), entry(10, "same"), entry(10.001, "same"), entry(20, "same")});
    check(result.size() == 2 && count(result[0]) == 2 && count(result[1]) == 2,
          "ten-second inclusive window is anchored to first entry, not extended by peers");
    result = DanmakuFilter::mergeSimilar({entry(0, "same", 1), entry(1, "same", 4), entry(2, "same", 5), entry(3, "same", 2)});
    check(result.size() == 4, "different display modes never merge");
    result = DanmakuFilter::mergeSimilar({entry(0, "ＡＢＣ  test！"), entry(1, "abc   TEST"),
                                       entry(2, QStringLiteral("你 好 世 界")), entry(3, QStringLiteral("你好世界"))});
    check(result.size() == 2 && count(result[0]) == 2 && count(result[1]) == 2,
          "Unicode width, case and CJK spacing normalize consistently");
    result = DanmakuFilter::mergeSimilar({entry(0, "!!!"), entry(1, "???"), entry(2, "a b"), entry(3, "ab")});
    check(result.size() == 4 && message(result[0]) == "!!!" && message(result[1]) == "???",
          "punctuation-only reactions and short Latin word boundaries remain distinct");
    result = DanmakuFilter::mergeSimilar({entry(0, QStringLiteral("很好")), entry(1, QStringLiteral("很差")),
                                       entry(2, QStringLiteral("我喜欢")), entry(3, QStringLiteral("我不喜欢"))});
    check(result.size() == 4, "short text requires exact normalized equality");
    result = DanmakuFilter::mergeSimilar({entry(0, QStringLiteral("今天这段视频真精彩")), entry(1, QStringLiteral("今天这段视频太精彩"))});
    check(result.size() == 1 && count(result[0]) == 2, "longer text tolerates one scalar substitution");
    result = DanmakuFilter::mergeSimilar({entry(0, "abcdefghijklm"), entry(1, "abcdXefghijklmY")});
    check(result.size() == 1 && count(result[0]) == 2, "long text tolerates two insertions");
    result = DanmakuFilter::mergeSimilar({entry(0, "abcdefghijkl"), entry(1, "lkjihgfedcba"),
                                       entry(2, QStringLiteral("这个视频在2025年发布")), entry(3, QStringLiteral("这个视频在2026年发布"))});
    check(result.size() == 4, "word order and differing numbers are not fuzzy duplicates");
    result = DanmakuFilter::mergeSimilar({entry(0, "666"), entry(1, "6666666"), entry(2, "233"), entry(3, "233333"),
                                       entry(4, "1000"), entry(5, "10000")});
    check(result.size() == 4 && count(result[0]) == 2 && count(result[1]) == 2,
          "conventional numeric reactions collapse without collapsing arbitrary numbers");
    const QString emoji = QString::fromUcs4(U"🙂🙂🙂");
    result = DanmakuFilter::mergeSimilar({entry(0, emoji), entry(1, QString::fromUcs4(U"🙂🙂🙃"))});
    check(result.size() == 2, "short-text safety counts Unicode scalars, not UTF-16 halves");
    result = DanmakuFilter::mergeSimilar({entry(0, "abcdef"), entry(1, "abcdeg"), entry(2, "abcdhg")});
    check(result.size() == 2 && count(result[0]) == 2, "fuzzy grouping compares the representative and cannot drift transitively");
    const QString longText(513, u'a');
    const QVariantList invalid{entry(-1, "a"), entry(std::numeric_limits<double>::quiet_NaN(), "a"),
                               entry(0, "a", 7), entry(0, ""), entry(0, longText), entry(1, longText),
                               QVariantMap{{"message", "missing time"}, {"type", 1}}};
    result = DanmakuFilter::mergeSimilar(invalid);
    check(result.size() == invalid.size(), "invalid, unsupported and oversized entries pass through without merging");
    QVariantList dense;
    for (int i = 0; i < 20000; ++i) dense.append(entry(0, "same"));
    QElapsedTimer timer;
    timer.start();
    result = DanmakuFilter::mergeSimilar(dense);
    qInfo() << "20,000 duplicate entries, ms:" << timer.elapsed();
    check(result.size() == 1 && count(result[0]) == 20000, "dense exact duplicates keep one group and correct count");
    check(result.capacity() <= 8, "merged cache releases original 20,000-entry reservation");
    dense.clear();
    for (int i = 0; i < 20000; ++i) dense.append(entry(0, QStringLiteral("unique entry number %1").arg(i)));
    timer.restart();
    result = DanmakuFilter::mergeSimilar(dense);
    qInfo() << "20,000 unique entries, ms:" << timer.elapsed();
    check(result.size() == dense.size(), "dense distinct entries are preserved after candidate-cache eviction");
    dense.clear();
    for (int i = 0; i < 4000; ++i) {
        QString text(110, u'a');
        int code = i;
        for (int j = 0; j < 6; ++j) {
            text += QString(3, QChar(u'b' + code % 20));
            code /= 20;
        }
        dense.append(entry(0, text));
    }
    timer.restart();
    result = DanmakuFilter::mergeSimilar(dense);
    qInfo() << "4,000 long common-prefix fuzzy candidates, ms:" << timer.elapsed();
    check(result.size() == dense.size(), "bounded fuzzy comparisons preserve dense long near-misses");
    auto worker = std::async(std::launch::async, [&source] { return DanmakuFilter::mergeSimilar(source); });
    const auto concurrent = DanmakuFilter::mergeSimilar(source);
    check(worker.get() == concurrent && concurrent == merged, "parallel calls have deterministic output and no shared mutable state");
    return failures ? 1 : 0;
}
