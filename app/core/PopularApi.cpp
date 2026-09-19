#include "core/PopularApi.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>
#include <limits>

#include "core/ApiErrors.h"
#include "core/BilibiliApiClient.h"
#include "core/JsonHelpers.h"
#include "core/WbiSigner.h"

namespace {
[[noreturn]] void malformed() {
    throw ApiError(-1, Loc::get("流行接口响应格式无效"));
}

QString id(const QJsonObject &object, const QString &key) {
    const qint64 number = jh::getInt64(object, key);
    return number > 0 ? QString::number(number) : QString();
}

QJsonObject dataObject(const QJsonObject &root, bool allowResult = false) {
    // 不能把缺失/错误 code 当作成功，也不能把失败响应解析成空列表。
    const QJsonValue codeValue = root.value("code");
    if (!codeValue.isDouble() && !codeValue.isString()) malformed();
    bool validCode = false;
    const int code = jh::getString(root, "code").toInt(&validCode);
    if (!validCode) malformed();
    if (code == -101) throw ApiUnauthorizedError(Loc::get("登录已失效，请重新登录后刷新流行"));
    if (code != 0) throw ApiError(code, Loc::get("流行接口请求失败（代码 %1）").arg(code));
    const QJsonValue data = root.value("data");
    if (data.isObject()) return data.toObject();
    if (allowResult && root.value("result").isObject()) return root.value("result").toObject();
    malformed();
}

QJsonArray listArray(const QJsonObject &data) {
    if (!data.value("list").isArray()) malformed();
    return data.value("list").toArray();
}

QVariantMap baseCard(const QJsonObject &raw) {
    return {{"title", jh::getString(raw, "title")}, {"business", "archive"},
        {"aid", QString()}, {"oid", QString()}, {"cid", QString()}, {"epId", QString()},
        {"seasonId", QString()}, {"bvid", QString()}, {"authorMid", QString()},
        {"authorName", QString()}, {"faceUrl", QString()}, {"authorFace", QString()},
        {"duration", qint64(0)}, {"playCount", qint64(0)}, {"danmakuCount", qint64(0)},
        {"likeCount", qint64(0)}, {"viewCount", 0}, {"progress", 0},
        {"coverUrl", QString()}, {"linkUrl", QString()}, {"badge", QString()},
        {"invalid", false}};
}

QVariantMap videoCard(const QJsonObject &raw) {
    QVariantMap card = baseCard(raw);
    const QString aid = id(raw, "aid");
    const QString bvid = jh::getString(raw, "bvid");
    if (aid.isEmpty()) malformed();
    const QJsonObject owner = raw.value("owner").toObject();
    const QJsonObject stat = raw.value("stat").toObject();
    const QString face = jh::normalizeImageUrlLenient(jh::getString(owner, "face"));
    card.insert("aid", aid);
    card.insert("oid", aid);
    card.insert("cid", id(raw, "cid"));
    card.insert("bvid", bvid);
    card.insert("videoKey", "archive:" + aid + ":0");
    card.insert("coverUrl", jh::normalizeImageUrlLenient(jh::getString(raw, "pic")));
    card.insert("authorMid", id(owner, "mid"));
    card.insert("authorName", jh::getString(owner, "name"));
    card.insert("faceUrl", face);
    card.insert("authorFace", face);
    card.insert("duration", jh::getInt64(raw, "duration"));
    card.insert("playCount", jh::getInt64(stat, "view"));
    card.insert("danmakuCount", jh::getInt64(stat, "danmaku"));
    card.insert("likeCount", jh::getInt64(stat, "like"));
    card.insert("pubdate", jh::getInt64(raw, "pubdate"));
    card.insert("description", jh::getString(raw, "desc"));
    card.insert("linkUrl", "https://www.bilibili.com/video/" + (bvid.isEmpty() ? "av" + aid : bvid));
    const QJsonValue reason = raw.value("rcmd_reason");
    card.insert("badge", reason.isString() ? reason.toString() : jh::getString(reason.toObject(), "content"));
    card.insert("subtitle", card.value("badge"));
    card.insert("invalid", jh::getInt64(raw, "state") < 0);
    return card;
}

QVariantMap pgcCard(const QJsonObject &raw) {
    QVariantMap card = baseCard(raw);
    const QString season = id(raw, "season_id");
    const QJsonObject ep = raw.value("new_ep").toObject();
    QString episode = jh::firstNonEmpty(id(ep, "id"), id(ep, "ep_id"), id(raw, "ep_id"));
    if (episode.isEmpty()) {
        const auto match = QRegularExpression("/ep([0-9]+)").match(jh::getString(raw, "url"));
        if (match.hasMatch()) episode = match.captured(1);
    }
    if (season.isEmpty() && episode.isEmpty()) malformed();
    const QJsonObject stat = raw.value("stat").toObject();
    card.insert("business", "pgc");
    card.insert("seasonId", season);
    card.insert("epId", episode);
    card.insert("videoKey", episode.isEmpty() ? "season:" + season : "pgc:" + episode + ":0");
    card.insert("coverUrl", jh::normalizeImageUrlLenient(jh::firstNonEmpty(
        jh::getString(raw, "ss_horizontal_cover"), jh::getString(raw, "cover"), jh::getString(ep, "cover"))));
    card.insert("playCount", jh::getInt64(stat, "view"));
    card.insert("danmakuCount", jh::getInt64(stat, "danmaku"));
    card.insert("badge", jh::getString(raw, "badge"));
    card.insert("description", jh::firstNonEmpty(jh::getString(ep, "index_show"), jh::getString(raw, "desc")));
    card.insert("subtitle", card.value("description"));
    card.insert("linkUrl", "https://www.bilibili.com/bangumi/play/" +
        (episode.isEmpty() ? "ss" + season : "ep" + episode));
    return card;
}

PopularPage videoList(const QJsonObject &data, bool pgc = false, bool ranked = false) {
    PopularPage page;
    page.description = jh::firstNonEmpty(jh::getString(data, "note"), jh::getString(data, "explain"));
    int rank = 0;
    for (const auto &value : listArray(data)) {
        if (!value.isObject()) malformed();
        const QJsonObject raw = value.toObject();
        QVariantMap card = pgc ? pgcCard(raw) : videoCard(raw);
        ++rank;
        if (ranked) card.insert("rank", jh::getInt64(raw, "rank") > 0 ? jh::getInt64(raw, "rank") : rank);
        page.items.append(card);
    }
    return page;
}

QJsonObject get(BilibiliApiClient &client, const QString &cookie, QUrl url, bool signedRequest = true) {
    // 匿名 nav 返回 -101；公开浏览不应为获取签名而先强制登录。
    // 榜单与目录不需 WBI；热门/选期/必刷仅在已有 Cookie 时签名。
    if (signedRequest && !cookie.trimmed().isEmpty()) {
        QUrlQuery query(url);
        QMap<QString, QString> params;
        for (const auto &pair : query.queryItems()) params.insert(pair.first, pair.second);
        const auto signature = WbiSigner::sign(client, cookie, params);
        query.addQueryItem("w_rid", signature.first);
        query.addQueryItem("wts", signature.second);
        url.setQuery(query);
    }
    return client.get(url, cookie).root;
}
}  // namespace

int PopularApi::rankingSeasonType(int rid) {
    switch (rid) {
    case 13: return 1;
    case 167: return 4;
    case 177: return 3;
    case 23: return 2;
    case 11: return 5;
    default: return 0;
    }
}

QUrl PopularApi::requestUrl(Kind kind, int selection, int pageSize, const QString &listId) {
    QString path;
    QUrlQuery query;
    switch (kind) {
    case Kind::Popular:
        if (selection < 1 || pageSize < 1 || pageSize > 50) throw ApiError(-1, Loc::get("流行请求参数无效"));
        path = "/x/web-interface/popular";
        query.addQueryItem("pn", QString::number(selection));
        query.addQueryItem("ps", QString::number(pageSize));
        break;
    case Kind::WeeklyPeriods: path = "/x/web-interface/popular/series/list"; break;
    case Kind::Weekly:
        if (selection < 1) throw ApiError(-1, Loc::get("流行请求参数无效"));
        path = "/x/web-interface/popular/series/one";
        query.addQueryItem("number", QString::number(selection));
        break;
    case Kind::Precious: path = "/x/web-interface/popular/precious"; break;
    case Kind::Ranking:
        if (selection < 0) throw ApiError(-1, Loc::get("流行请求参数无效"));
        if (const int seasonType = rankingSeasonType(selection)) {
            path = "/pgc/season/rank/web/list";
            query.addQueryItem("season_type", QString::number(seasonType));
            query.addQueryItem("day", "3");
        } else {
            path = "/x/web-interface/ranking/v2";
            query.addQueryItem("rid", QString::number(selection));
            query.addQueryItem("type", "all");
        }
        break;
    case Kind::MusicPeriods:
        path = "/x/copyright-music-publicity/toplist/all_period";
        query.addQueryItem("list_type", "1");
        break;
    case Kind::Music:
        if (listId.toLongLong() <= 0) throw ApiError(-1, Loc::get("流行请求参数无效"));
        path = "/x/copyright-music-publicity/toplist/music_list";
        query.addQueryItem("list_id", listId);
        break;
    }
    QUrl url("https://api.bilibili.com" + path);
    url.setQuery(query);
    return url;
}

PopularPage PopularApi::fetchPopular(BilibiliApiClient &client, const QString &cookie, int page, int pageSize) {
    return parsePopular(get(client, cookie, requestUrl(Kind::Popular, page, pageSize)), pageSize);
}
QVariantList PopularApi::fetchWeeklyPeriods(BilibiliApiClient &client, const QString &cookie) {
    return parseWeeklyPeriods(get(client, cookie, requestUrl(Kind::WeeklyPeriods), false));
}
PopularPage PopularApi::fetchWeekly(BilibiliApiClient &client, const QString &cookie, int number) {
    return parseWeekly(get(client, cookie, requestUrl(Kind::Weekly, number)), number);
}
PopularPage PopularApi::fetchPrecious(BilibiliApiClient &client, const QString &cookie) {
    return parsePrecious(get(client, cookie, requestUrl(Kind::Precious)));
}
PopularPage PopularApi::fetchRanking(BilibiliApiClient &client, const QString &cookie, int rid) {
    return parseRanking(get(client, cookie, requestUrl(Kind::Ranking, rid), false), rid);
}
QVariantList PopularApi::fetchMusicPeriods(BilibiliApiClient &client, const QString &cookie) {
    return parseMusicPeriods(get(client, cookie, requestUrl(Kind::MusicPeriods), false));
}
PopularPage PopularApi::fetchMusic(BilibiliApiClient &client, const QString &cookie, const QString &listId) {
    return parseMusic(get(client, cookie, requestUrl(Kind::Music, 0, 20, listId), false));
}

PopularPage PopularApi::parsePopular(const QJsonObject &root, int pageSize) {
    const QJsonObject data = dataObject(root);
    PopularPage page = videoList(data);
    if (data.contains("no_more")) {
        const QJsonValue noMore = data.value("no_more");
        if (!noMore.isBool() && !noMore.isDouble()) malformed();
        page.hasMore = !jh::getBool(data, "no_more");
    } else page.hasMore = listArray(data).size() >= pageSize;
    return page;
}

QVariantList PopularApi::parseWeeklyPeriods(const QJsonObject &root) {
    QVariantList periods;
    QSet<int> seen;
    for (const auto &value : listArray(dataObject(root))) {
        if (!value.isObject()) malformed();
        const auto raw = value.toObject();
        const qint64 number = jh::getInt64(raw, "number");
        if (number <= 0 || number > std::numeric_limits<int>::max()) malformed();
        if (seen.contains(int(number))) continue;
        seen.insert(int(number));
        const QString name = jh::getString(raw, "name");
        const QString label = Loc::get("第 %1 期").arg(number)
            + (name.isEmpty() ? QString() : " · " + name);
        periods.append(QVariantMap{{"number", int(number)}, {"label", label},
            {"subject", jh::getString(raw, "subject")}});
    }
    std::sort(periods.begin(), periods.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("number").toInt() > b.toMap().value("number").toInt();
    });
    return periods;
}

PopularPage PopularApi::parseWeekly(const QJsonObject &root, int expectedNumber) {
    const auto data = dataObject(root);
    if (!data.value("config").isObject()) malformed();
    const auto config = data.value("config").toObject();
    if (expectedNumber > 0 && jh::getInt64(config, "number") != expectedNumber)
        throw ApiError(-1, Loc::get("每周必看返回期数与请求不一致"));
    PopularPage page = videoList(data);
    page.description = jh::firstNonEmpty(jh::getString(config, "subject"), jh::getString(config, "name"));
    return page;
}
PopularPage PopularApi::parsePrecious(const QJsonObject &root) { return videoList(dataObject(root)); }
PopularPage PopularApi::parseRanking(const QJsonObject &root, int rid) {
    return videoList(dataObject(root, rankingSeasonType(rid) > 0), rankingSeasonType(rid) > 0, true);
}

QVariantList PopularApi::parseMusicPeriods(const QJsonObject &root) {
    const auto data = dataObject(root);
    if (!data.value("list").isObject()) malformed();
    const auto years = data.value("list").toObject();
    QVariantList periods;
    QSet<QString> seen;
    for (auto year = years.constBegin(); year != years.constEnd(); ++year) {
        if (!year.value().isArray()) malformed();
        for (const auto &value : year.value().toArray()) {
            if (!value.isObject()) malformed();
            const auto raw = value.toObject();
            const QString periodId = id(raw, "ID");
            if (periodId.isEmpty()) malformed();
            if (seen.contains(periodId)) continue;
            seen.insert(periodId);
            const qint64 number = jh::getInt64(raw, "priod");
            periods.append(QVariantMap{{"id", periodId}, {"number", number}, {"year", year.key()},
                {"label", year.key() + " · " + Loc::get("第 %1 期").arg(number)},
                {"publishTime", jh::getInt64(raw, "publish_time")}});
        }
    }
    std::sort(periods.begin(), periods.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        const auto leftTime = left.value("publishTime").toLongLong(), rightTime = right.value("publishTime").toLongLong();
        return leftTime != rightTime ? leftTime > rightTime : left.value("id").toLongLong() > right.value("id").toLongLong();
    });
    return periods;
}

PopularPage PopularApi::parseMusic(const QJsonObject &root) {
    PopularPage page;
    for (const auto &value : listArray(dataObject(root))) {
        if (!value.isObject()) malformed();
        const auto raw = value.toObject();
        // creation_* 和 mv_* 必须成组选择，避免混合两条稿件的编号、封面、作者。
        const bool creation = !id(raw, "creation_aid").isEmpty();
        const QString prefix = creation ? "creation_" : "mv_";
        const QString aid = id(raw, prefix + "aid");
        if (aid.isEmpty()) continue;  // 音频榜含仅音频条目；播放器不支持该媒体。
        const QString singer = jh::getString(raw, "singer");
        QString title = jh::getString(raw, "music_title");
        if (!singer.isEmpty()) title += " - " + singer;
        QJsonObject video{{"aid", aid}, {"bvid", jh::getString(raw, prefix + "bvid")},
            {"pic", jh::getString(raw, prefix + "cover")}, {"title", title},
            {"desc", jh::getString(raw, "album")}};
        if (creation) {
            video.insert("owner", QJsonObject{{"mid", id(raw, "creation_up")},
                {"name", jh::firstNonEmpty(jh::getString(raw, "creation_nickname"), singer)}});
            video.insert("duration", raw.value("creation_duration"));
            video.insert("stat", QJsonObject{{"view", raw.value("creation_play")}});
        } else video.insert("owner", QJsonObject{{"name", singer}});
        auto card = videoCard(video);
        card.insert("rank", jh::getInt64(raw, "rank"));
        card.insert("badge", creation ? jh::getString(raw, "creation_reason") : QString());
        page.items.append(card);
    }
    return page;
}
