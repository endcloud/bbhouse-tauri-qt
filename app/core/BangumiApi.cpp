#include "core/BangumiApi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>
#include <stdexcept>

#include "core/ApiErrors.h"
#include "core/JsonHelpers.h"

namespace {
constexpr const char *kFollowListEndpoint =
        "https://api.bilibili.com/x/space/bangumi/follow/list";
constexpr const char *kPgcSeasonEndpoint = "https://api.bilibili.com/pgc/view/web/season";
}  // namespace

qint64 BangumiApi::tryGetSelfMid(const QString &cookie) {
    static const QRegularExpression pattern(QStringLiteral("DedeUserID=(\\d+)"));
    const QRegularExpressionMatch match = pattern.match(cookie);
    if (!match.hasMatch()) {
        throw ApiUnauthorizedError(Loc::get("账号未登录或 SESSDATA 已失效"));
    }

    return match.captured(1).toLongLong();
}

BangumiApi::FollowListPage BangumiApi::fetchFollowList(BilibiliApiClient &api,
                                                       const QString &cookie, int followType,
                                                       int pn, int ps) {
    if (followType != 1 && followType != 2) {
        throw std::invalid_argument("follow list 端点只接受 type 1(追番)或 2(追剧)");
    }

    QUrl url(kFollowListEndpoint);
    QUrlQuery query;
    query.addQueryItem("vmid", QString::number(tryGetSelfMid(cookie)));
    query.addQueryItem("type", QString::number(followType));
    query.addQueryItem("pn", QString::number(pn));
    query.addQueryItem("ps", QString::number(std::clamp(ps, 1, kMaxPageSize)));
    url.setQuery(query);
    const auto envelope = api.get(url, cookie);

    const QJsonObject data = jh::getChild(envelope.root, "data");
    FollowListPage page;
    const QJsonArray list = jh::getChildArray(data, "list");
    for (const QJsonValue &value : list) {
        if (value.isObject()) {
            page.items.append(parseSeason(value.toObject()));
        }
    }
    page.total = jh::getInt64(data, "total");
    return page;
}

BangumiApi::PgcSeasonDetail BangumiApi::fetchSeason(BilibiliApiClient &api,
                                                    const QString &cookie, qint64 seasonId) {
    QUrl url(kPgcSeasonEndpoint);
    QUrlQuery query;
    query.addQueryItem("season_id", QString::number(seasonId));
    url.setQuery(query);
    const auto envelope = api.get(url, cookie);

    // 注意:该端点数据在 `result` 信封(与 PGC playurl 端点同形态)。统一管线
    // checkEnvelope 只认 code 信封,数据体须取 root.value("result")。
    const QJsonObject result = jh::getChild(envelope.root, "result");
    PgcSeasonDetail detail;
    detail.seasonId = jh::getInt64(result, "season_id");
    detail.title = jh::getString(result, "title");
    detail.coverUrl = jh::normalizeImageUrlLenient(jh::getString(result, "cover"));
    const QJsonArray list = jh::getChildArray(result, "episodes");
    for (const QJsonValue &value : list) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject element = value.toObject();
        // 剧集 id 此处字段名为 `id`(即 playurl 侧的 ep_id);cid 是弹幕/播放路径所需,
        // duration 单位为毫秒。
        PgcSeasonEpisode episode;
        episode.epId = jh::getInt64(element, "id");
        episode.cid = jh::getInt64(element, "cid");
        episode.title = jh::getString(element, "title");
        episode.longTitle = jh::getString(element, "long_title");
        episode.coverUrl = jh::normalizeImageUrlLenient(jh::getString(element, "cover"));
        episode.durationSeconds = static_cast<int>(jh::getInt64(element, "duration") / 1000);
        episode.badge = jh::getString(element, "badge");
        detail.episodes.append(episode);
    }

    const QJsonObject progress = jh::getChild(jh::getChild(result, "user_status"), "progress");
    detail.lastEpId = jh::getInt64(progress, "last_ep_id");
    detail.lastEpIndex = jh::getString(progress, "last_ep_index");
    detail.lastTimeSeconds = static_cast<int>(jh::getInt64(progress, "last_time"));
    detail.rawJson = QString::fromUtf8(
            QJsonDocument(result).toJson(QJsonDocument::Compact));
    return detail;
}

BangumiApi::BangumiSeason BangumiApi::parseSeason(const QJsonObject &element) {
    const QJsonObject rating = jh::getChild(element, "rating");
    // 评分为 NaN = 条目无评分(jh::getDouble 对缺字段回 NaN)
    const double ratingScore = jh::getDouble(rating, "score");

    BangumiSeason season;
    season.seasonId = jh::getInt64(element, "season_id");
    season.seasonType = static_cast<int>(jh::getInt64(element, "season_type"));
    season.seasonTypeName = jh::getString(element, "season_type_name");
    season.title = jh::getString(element, "title");
    season.regional = isRegionalTitle(season.title);
    season.cover = jh::normalizeImageUrlLenient(jh::getString(element, "cover"));
    season.squareCover = jh::normalizeImageUrlLenient(jh::getString(element, "square_cover"));
    season.badge = jh::getString(element, "badge");
    season.totalCount = jh::getInt64(element, "total_count");
    season.isFinish = jh::getInt64(element, "is_finish") == 1;
    season.newEpIndexShow = jh::getString(jh::getChild(element, "new_ep"), "index_show");
    season.ratingScore = ratingScore;
    season.ratingCount = jh::getInt64(rating, "count");
    season.progressText = jh::getString(element, "progress");
    season.subtitle = jh::getString(element, "subtitle");
    season.evaluate = jh::getString(element, "evaluate");
    season.url = jh::getString(element, "url");
    season.rawJson = QString::fromUtf8(QJsonDocument(element).toJson(QJsonDocument::Compact));
    return season;
}

bool BangumiApi::isRegionalTitle(const QString &title) {
    static const QRegularExpression marker(
            QStringLiteral("[（(]\\s*[僅仅]限港澳[台臺]地[區区]\\s*[）)]"));
    return marker.match(title).hasMatch();
}

QList<BangumiApi::BangumiSeason> BangumiApi::scanRegionalFollowList(
        const std::function<FollowListPage(int)> &fetchPage) {
    QList<BangumiSeason> result;
    QSet<qint64> seen;
    for (int page = 1; page <= kMaxRegionalScanPages; ++page) {
        const FollowListPage response = fetchPage(page);
        if (response.total < 0 || response.total > qint64(kMaxRegionalScanPages) * kMaxPageSize)
            throw ApiError(-1, Loc::get("追番列表过大，无法完整加载港澳台番剧"));
        if (response.items.isEmpty() && (page - 1) * kMaxPageSize < response.total)
            throw ApiError(-1, Loc::get("追番列表不完整，请刷新重试"));
        for (BangumiSeason item : response.items) {
            if (item.seasonId <= 0 || seen.contains(item.seasonId)) continue;
            seen.insert(item.seasonId);
            item.regional = isRegionalTitle(item.title);
            if (item.regional) result.append(item);
        }
        if (qint64(page) * kMaxPageSize >= response.total) return result;
    }
    throw ApiError(-1, Loc::get("追番列表过大，无法完整加载港澳台番剧"));
}

BangumiApi::FollowListPage BangumiApi::regionalPage(const QList<BangumiSeason> &items,
                                                   const QString &query, int page) {
    QList<BangumiSeason> filtered;
    const QString needle = query.trimmed();
    for (const BangumiSeason &item : items) {
        if (needle.isEmpty() || item.title.contains(needle, Qt::CaseInsensitive))
            filtered.append(item);
    }
    FollowListPage result;
    result.total = filtered.size();
    const int lastPage = qMax(1, int((result.total + kMaxPageSize - 1) / kMaxPageSize));
    const int safePage = qBound(1, page, lastPage);
    result.items = filtered.mid((safePage - 1) * kMaxPageSize, kMaxPageSize);
    return result;
}
