#include "core/PopularApi.h"
#include "core/ApiErrors.h"
#include "core/PlaybackEntry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <functional>

namespace {
QJsonObject envelope(const QJsonArray &list) {
    return {{"code", 0}, {"data", QJsonObject{{"list", list}}}};
}
QJsonObject video() {
    // 整数 JSON 输入覆盖 Qt 的 64 位保存能力，而不仅仅验证字符串输入。
    return QJsonDocument::fromJson(R"({"aid":9007199254740993,"cid":3000000001,"bvid":"BV1fixture",
      "title":"Fixture","pic":"//img.example.test/cover.jpg","duration":123,
      "owner":{"mid":9007199254740995,"name":"UP","face":"http://img.example.test/face.jpg"},
      "stat":{"view":8765432101,"danmaku":40,"like":50},"rcmd_reason":{"content":"推荐"}})").object();
}
}
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
    using Kind = PopularApi::Kind;
    const QUrl popular = PopularApi::requestUrl(Kind::Popular, 3, 20);
    check(popular.path() == "/x/web-interface/popular" && QUrlQuery(popular).queryItemValue("pn") == "3"
        && QUrlQuery(popular).queryItemValue("ps") == "20", "popular request uses correct page and size");
    check(PopularApi::requestUrl(Kind::WeeklyPeriods).path().endsWith("series/list"), "weekly directory has separate endpoint");
    const auto weekly = PopularApi::requestUrl(Kind::Weekly, 7);
    check(weekly.path().endsWith("series/one") && QUrlQuery(weekly).queryItemValue("number") == "7",
        "historical weekly issue is explicitly selected rather than implicit latest");
    rejects([] { PopularApi::requestUrl(Kind::Weekly, 0); }, "weekly missing selection rejected");
    rejects([] { PopularApi::requestUrl(Kind::Popular, 0); }, "invalid pagination rejected");
    const QList<QPair<int, int>> pgcTypes{{13, 1}, {167, 4}, {177, 3}, {23, 2}, {11, 5}};
    for (const auto &type : pgcTypes) {
        const QUrl request = PopularApi::requestUrl(Kind::Ranking, type.first);
        check(request.path() == "/pgc/season/rank/web/list" && QUrlQuery(request).queryItemValue("season_type") == QString::number(type.second)
            && QUrlQuery(request).queryItemValue("day") == "3" && !QUrlQuery(request).hasQueryItem("rid"),
            "PGC categories route to dedicated endpoint and correct season type");
    }
    const QUrl rank = PopularApi::requestUrl(Kind::Ranking, 36);
    check(rank.path().endsWith("ranking/v2") && QUrlQuery(rank).queryItemValue("rid") == "36"
        && QUrlQuery(rank).queryItemValue("type") == "all", "UGC ranking retains rid and type");
    check(QUrlQuery(PopularApi::requestUrl(Kind::MusicPeriods)).queryItemValue("list_type") == "1"
        && QUrlQuery(PopularApi::requestUrl(Kind::Music, 0, 20, "9007199254740993")).queryItemValue("list_id") == "9007199254740993",
        "music uses hot chart and lossless issue ID");
    QJsonObject root = envelope({video()});
    auto data = root.value("data").toObject();
    data.insert("no_more", true);
    root.insert("data", data);
    const auto lastPage = PopularApi::parsePopular(root, 1);
    const auto card = lastPage.items.first().toMap();
    check(!lastPage.hasMore, "server no_more overrides full-page heuristic");
    data.insert("no_more", false); root.insert("data", data);
    check(PopularApi::parsePopular(root, 20).hasMore, "short page with server continuation remains pageable");
    check(card.value("aid").toString() == "9007199254740993" && card.value("cid").toString() == "3000000001"
        && card.value("authorMid").toString() == "9007199254740995" && card.value("aid").metaType().id() == QMetaType::QString,
        "numeric JSON identifiers survive as exact strings across QML");
    check(card.value("coverUrl").toString().startsWith("https://") && card.value("faceUrl").toString().startsWith("https://")
        && card.value("playCount").toLongLong() == 8765432101LL && card.value("viewCount").toInt() == 0,
        "card metadata separates play count from personal history views");
    check(PlaybackEntry::playable(card) && card.value("videoKey") == "archive:9007199254740993:0", "card is directly compatible with playback entry");
    const auto periods = PopularApi::parseWeeklyPeriods(envelope({QJsonObject{{"number", 1}, {"name", "2019 第一期"}},
        QJsonObject{{"number", 77}, {"subject", "Theme"}, {"name", "09.11 - 09.17"}}, QJsonObject{{"number", 1}}}));
    check(periods.size() == 2 && periods.first().toMap().value("number") == 77
        && periods.first().toMap().value("subject") == "Theme"
        && periods.first().toMap().value("label").toString().contains("77")
        && periods.first().toMap().value("label").toString().contains("09.11 - 09.17"),
        "weekly directory sorts, deduplicates, and shows issue number alongside dates");
    data = envelope({video()}).value("data").toObject();
    data.insert("config", QJsonObject{{"number", 7}, {"subject", "Chosen issue"}});
    root = {{"code", 0}, {"data", data}};
    check(PopularApi::parseWeekly(root, 7).description == "Chosen issue", "weekly chosen issue metadata parsed");
    rejects([&] { PopularApi::parseWeekly(root, 8); }, "mismatched weekly server issue rejected");
    QJsonObject pgc{{"season_id", "3000000009"}, {"title", "Series"}, {"cover", "//img.example.test/poster"},
        {"ss_horizontal_cover", "//img.example.test/landscape"}, {"new_ep", QJsonObject{{"index_show", "更新至第 3 话"}}},
        {"stat", QJsonObject{{"view", 100}}}, {"badge", "会员"}};
    const auto seasonCard = PopularApi::parseRanking(envelope({pgc}), 13).items.first().toMap();
    check(seasonCard.value("seasonId") == "3000000009" && seasonCard.value("epId").toString().isEmpty()
        && !seasonCard.value("invalid").toBool() && seasonCard.value("videoKey") == "season:3000000009"
        && seasonCard.value("coverUrl").toString().endsWith("landscape"), "season-only PGC card preserves detail-navigation identity");
    pgc.insert("new_ep", QJsonObject{{"id", "3000000008"}});
    const auto episode = PopularApi::parseRanking({{"code", 0}, {"result", QJsonObject{{"list", QJsonArray{pgc}}}}}, 13).items.first().toMap();
    check(PlaybackEntry::playable(episode), "PGC result envelope and explicit episode support native playback");
    const QJsonObject musicYears{
        {"2025", QJsonArray{QJsonObject{{"ID", "200"}, {"priod", 9}, {"publish_time", 300}}}},
        {"2024", QJsonArray{QJsonObject{{"ID", "100"}, {"priod", 90}, {"publish_time", 100}}}}};
    const auto musicPeriods = PopularApi::parseMusicPeriods(
        {{"code", 0}, {"data", QJsonObject{{"list", musicYears}}}});
    check(musicPeriods.first().toMap().value("id") == "200" && musicPeriods.first().toMap().value("publishTime") == 300,
        "music periods ordered by publication time independent of year-key order");
    QJsonObject music{{"music_title", "Song"}, {"singer", "Singer"}, {"creation_aid", "3000000001"},
        {"creation_bvid", "BVcreation"}, {"creation_cover", "//img.example.test/creation"},
        {"creation_up", "9007199254740993"}, {"creation_nickname", "Creator"}, {"creation_duration", 100},
        {"mv_aid", "3000000002"}, {"mv_bvid", "BVmv"}, {"mv_cover", "//img.example.test/mv"}};
    auto songs = PopularApi::parseMusic(envelope({music})).items;
    check(songs.first().toMap().value("aid") == "3000000001" && songs.first().toMap().value("authorName") == "Creator",
        "music prefers associated creation and preserves author");
    music.insert("creation_aid", 0);
    songs = PopularApi::parseMusic(envelope({music, QJsonObject{{"music_title", "Audio only"}}})).items;
    check(songs.size() == 1 && songs.first().toMap().value("aid") == "3000000002"
        && songs.first().toMap().value("bvid") == "BVmv" && songs.first().toMap().value("authorMid").toString().isEmpty()
        && songs.first().toMap().value("duration") == 0, "music fallback uses MV fields together and omits unsupported audio-only items");
    rejects([] { PopularApi::parsePopular({}); }, "missing envelope rejected");
    rejects([] { PopularApi::parsePopular({{"code", -352}}); }, "risk-control errors not empty success");
    rejects([] { PopularApi::parsePopular({{"code", 0}, {"data", QJsonObject{}}}); }, "missing list rejected");
    rejects([] { PopularApi::parsePopular(envelope({17})); }, "nonobject list entries rejected");
    rejects([] { PopularApi::parsePopular(envelope({QJsonObject{{"title", "bad"}}})); }, "malformed identity rejected");
    rejects([] { PopularApi::parseMusicPeriods({{"code", 0}, {"data", QJsonObject{{"list", QJsonArray{}}}}}); },
        "malformed music period directory rejected");
    bool unauthorized = false;
    try { PopularApi::parsePrecious({{"code", -101}}); } catch (const ApiUnauthorizedError &) { unauthorized = true; }
    check(unauthorized, "expired login preserves semantic error type");
    check(PopularApi::parsePopular(envelope({})).items.isEmpty() && PopularApi::parseMusic(envelope({})).items.isEmpty(),
        "valid empty lists remain successful");
    return failures ? 1 : 0;
}
