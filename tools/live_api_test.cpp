#include "core/LiveApi.h"
#include "core/ApiErrors.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <functional>

namespace {
QJsonObject room(const QString &id, int status) {
    return {{"roomid", id}, {"uid", "9007199254740993"}, {"live_status", status},
        {"title", "Fixture title"}, {"uname", "Fixture author"}, {"face", "//img.example.test/a.jpg"},
        {"room_cover", "http://img.example.test/c.jpg"}, {"area_name_v2", "游戏"}, {"text_small", "1.2万"}};
}

QJsonObject codec(const QString &name, int qn, const QString &base, const QJsonArray &urls) {
    return {{"codec_name", name}, {"current_qn", qn}, {"accept_qn", QJsonArray{10000, 400, 150}},
        {"base_url", base}, {"url_info", urls}};
}

QJsonObject host(const QString &value, const QString &extra = "?fixture=1") {
    return {{"host", value}, {"extra", extra}};
}

QJsonObject stream(const QString &protocol, const QString &format, const QJsonArray &codecs) {
    return {{"protocol_name", protocol}, {"format", QJsonArray{QJsonObject{
        {"format_name", format}, {"codec", codecs}}}}};
}

QJsonObject playback(const QJsonArray &streams, int status = 1) {
    return {{"code", 0}, {"data", QJsonObject{{"room_id", "3000000001"}, {"live_status", status},
        {"playurl_info", QJsonObject{{"playurl", QJsonObject{
            {"g_qn_desc", QJsonArray{QJsonObject{{"qn", 10000}, {"desc", "原画"}},
                QJsonObject{{"qn", 400}, {"desc", "蓝光"}}, QJsonObject{{"qn", 150}, {"desc", "高清"}},
                QJsonObject{{"qn", 30000}, {"desc", "未获授权档位"}}}}, {"stream", streams}}}}}}}};
}
}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *name) {
        qInfo() << (ok ? "PASS" : "FAIL") << name;
        failures += !ok;
    };
    const auto rejects = [&](const std::function<void()> &action, const char *name) {
        bool rejected = false;
        try { action(); } catch (const ApiError &) { rejected = true; }
        check(rejected, name);
    };

    QJsonObject followed{{"code", 0}, {"data", QJsonObject{{"totalPage", 2}, {"list", QJsonArray{
        room("3000000001", 1), room("2", 0), room("3", 2), room("3000000001", 1), room("4", 1)}}}}};
    const LiveFollowPage first = LiveApi::parseFollowed(followed, 1);
    check(first.rooms.size() == 2 && first.hasMore, "followed list filters offline and carousel, deduplicates rooms");
    check(first.rooms.first().roomId == "3000000001" && first.rooms.first().mid == "9007199254740993",
        "room and author identifiers remain lossless strings across QML boundary");
    check(first.rooms.first().cover == "https://img.example.test/c.jpg"
        && first.rooms.first().face == "https://img.example.test/a.jpg" && first.rooms.first().online == "1.2万",
        "followed card metadata normalizes image schemes and preserves audience text");
    check(!LiveApi::parseFollowed(followed, 2).hasMore, "server final page ends pagination");
    QJsonObject emptyLive{{"data", QJsonObject{{"totalPage", 3}, {"list", QJsonArray{room("2", 0)}}}}};
    check(LiveApi::parseFollowed(emptyLive, 1).rooms.isEmpty() && LiveApi::parseFollowed(emptyLive, 1).hasMore,
        "page without live rooms does not truncate subsequent pages");
    QJsonObject countFallback{{"data", QJsonObject{{"count", 21}, {"pageSize", 10}, {"list", QJsonArray{}}}}};
    check(LiveApi::parseFollowed(countFallback, 2).hasMore && !LiveApi::parseFollowed(countFallback, 3).hasMore,
        "count metadata supports pagination without totalPage");
    QJsonObject empty{{"data", QJsonObject{{"totalPage", 0}, {"list", QJsonArray{}}}}};
    check(!LiveApi::parseFollowed(empty).hasMore, "empty follow list succeeds");
    rejects([] { LiveApi::parseFollowed({}); }, "malformed follow response is an error instead of an empty success");
    bool unauthorized = false;
    try { LiveApi::parseFollowed({{"code", -101}}); } catch (const ApiUnauthorizedError &) { unauthorized = true; }
    check(unauthorized, "login expiry preserves unauthorized error type");

    const QJsonObject flv = stream("http_stream", "flv", QJsonArray{
        codec("hevc", 10000, "/hevc.flv", QJsonArray{host("https://hevc.example.test")}),
        codec("avc", 10000, "/avc.flv", QJsonArray{host("https://mcdn.example.test"),
            host("https://cdn.example.test"), host("https://cdn.example.test")})});
    const QJsonObject hls = stream("http_hls", "fmp4", QJsonArray{
        codec("avc", 10000, "/avc.m3u8", QJsonArray{host("https://hls.example.test")})});
    const LivePlayInfo playable = LiveApi::parsePlayInfo(playback(QJsonArray{hls, flv}));
    check(playable.roomId == "3000000001" && playable.currentQn == 10000 && playable.liveStatus == 1,
        "playback retains canonical room and actual quality");
    check(playable.urls.size() == 4 && playable.urls.first() == "https://cdn.example.test/avc.flv?fixture=1"
        && playable.urls.at(1).contains("hls") && playable.urls.at(2).contains("hevc")
        && playable.urls.last().contains("mcdn"),
        "all ordinary CDN formats precede P2P; format preference and deduplication remain");
    check(playable.qualities.size() == 3 && playable.qualities.first().toMap().value("qn").toInt() == 10000
        && playable.qualities.first().toMap().value("label").toString() == "原画",
        "quality choices use accepted IDs and descriptions, exclude advertised but unavailable quality");
    check(LiveApi::parsePlayInfo(playback(QJsonArray{hls})).urls.first().contains(".m3u8"),
        "HLS-only room remains playable");
    const QJsonObject peerFlv = stream("http_stream", "flv", QJsonArray{
        codec("avc", 10000, "/peer.flv", QJsonArray{host("https://node.szbdyd.com"),
            host("https://node.pcdn.example.test")})});
    const QJsonObject cloudHls = stream("http_hls", "fmp4", QJsonArray{
        codec("avc", 10000, "/cloud.m3u8", QJsonArray{host("https://upos-sz-mirrorcos.bilivideo.com")})});
    const auto crossTrack = LiveApi::parsePlayInfo(playback(QJsonArray{peerFlv, flv, cloudHls}));
    check(crossTrack.urls.size() == 6 && crossTrack.urls.first().contains("upos-sz-mirrorcos")
        && crossTrack.urls.at(1).contains("cdn.example.test") && crossTrack.urls.at(2).contains("hevc")
        && crossTrack.urls.at(3).contains("szbdyd") && crossTrack.urls.at(4).contains("pcdn")
        && crossTrack.urls.last().contains("mcdn"),
        "same-quality cloud HLS precedes ordinary FLV and all PCDN tracks");
    const auto peerOnly = LiveApi::parsePlayInfo(playback(QJsonArray{peerFlv}));
    check(peerOnly.urls.size() == 2 && peerOnly.urls.first().contains("szbdyd"),
        "P2P-only live room keeps ordered fallback candidates");
    const QJsonObject lower = stream("http_stream", "flv", QJsonArray{
        codec("avc", 150, "/lower.flv", QJsonArray{host("https://lower.example.test")})});
    const LivePlayInfo requested = LiveApi::parsePlayInfo(playback(QJsonArray{lower, hls}), 10000);
    check(requested.currentQn == 10000 && requested.urls.size() == 1 && requested.urls.first().contains("hls"),
        "requested quality wins over format preference and candidates never mix qualities");
    const LivePlayInfo downgraded = LiveApi::parsePlayInfo(playback(QJsonArray{lower}), 10000);
    check(downgraded.currentQn == 150, "server quality downgrade reports actual quality");
    const QJsonObject malicious = stream("http_stream", "flv", QJsonArray{
        codec("avc", 10000, "/video.flv", QJsonArray{host("file:///tmp"), host("javascript:alert(1)"),
            host("https://user:pass@example.test"), host("https://cdn.example.test", "?x=1\r\nHeader:value"),
            host("https://cdn.example.test", "#fragment"), host("https://cdn.example.test/prefix")})});
    rejects([&] { LiveApi::parsePlayInfo(playback(QJsonArray{malicious})); },
        "unsafe schemes, credentials, control characters and malformed origins cannot become media URLs");
    const QJsonObject missingPath = stream("http_stream", "flv", QJsonArray{
        codec("avc", 10000, "", QJsonArray{host("https://cdn.example.test")})});
    rejects([&] { LiveApi::parsePlayInfo(playback(QJsonArray{missingPath})); }, "missing stream path cannot play CDN root");
    rejects([&] { LiveApi::parsePlayInfo(playback(QJsonArray{flv}, 0)); }, "offline room is rejected before loading media");
    rejects([&] { LiveApi::parsePlayInfo(playback(QJsonArray{flv}, 2)); }, "carousel is not treated as a live room");
    rejects([&] { LiveApi::parsePlayInfo(playback({})); }, "missing playable tracks produces explicit failure");
    QJsonObject locked = playback(QJsonArray{flv});
    QJsonObject lockedData = locked.value("data").toObject();
    lockedData.insert("is_locked", true);
    locked.insert("data", lockedData);
    rejects([&] { LiveApi::parsePlayInfo(locked); }, "locked room is rejected even if a stale URL exists");
    lockedData.remove("is_locked");
    lockedData.insert("encrypted", true);
    locked.insert("data", lockedData);
    rejects([&] { LiveApi::parsePlayInfo(locked); }, "unverified password room is rejected");
    return failures == 0 ? 0 : 1;
}
