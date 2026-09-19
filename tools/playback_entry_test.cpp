#include <QCoreApplication>
#include <QJsonDocument>
#include <QJSEngine>
#include <cstdio>
#include "core/PlaybackEntry.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    auto entry = PlaybackEntry::normalize({{"business", "pgc"}, {"rawJson",
        R"({"history":{"oid":170001,"epid":5751673,"cid":41394834610}})"}});
    check(entry.value("cid").toLongLong() == 41394834610LL, "history 64-bit cid");
    check(entry.value("epId").toLongLong() == 5751673, "history episode identity");
    check(PlaybackEntry::playable(entry), "history PGC playable");
    auto dynamic = PlaybackEntry::normalize({{"business", "pgc"}, {"rawJson",
        R"({"modules":{"module_dynamic":{"major":{"pgc":{"epid":85046}}}}})"}});
    check(dynamic.value("epId").toLongLong() == 85046, "dynamic PGC ep-only");
    check(PlaybackEntry::playable(dynamic), "ep-only PGC playable");
    check(dynamic.value("videoKey").toString() == "pgc:85046:0", "stable episode key");
    auto link = PlaybackEntry::normalize({{"business", "pgc"}, {"linkUrl", "https://www.bilibili.com/bangumi/play/ep85046"}});
    check(link.value("epId").toLongLong() == 85046, "episode URL recovery");
    check(!PlaybackEntry::playable({{"business", "live"}, {"oid", 170001}}), "live excluded");
    auto ugc = PlaybackEntry::normalize({{"business", "archive"}, {"oid", 114000000000000LL}, {"videoKey", "archive:original:0"}});
    check(ugc.value("oid").toLongLong() == 114000000000000LL, "large aid survives normalization");
    check(ugc.value("videoKey").toString() == "archive:original:0", "stored key preserved");
    QJSEngine js;
    check(js.evaluate("Number(114000000000000)").toNumber() == 114000000000000.0, "JS numeric aid precision");
    std::printf("PLAYBACK_ENTRY_%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
