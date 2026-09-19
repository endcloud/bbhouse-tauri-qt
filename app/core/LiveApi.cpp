#include "core/LiveApi.h"

#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

#include "core/ApiErrors.h"
#include "core/BilibiliApiClient.h"
#include "core/JsonHelpers.h"
#include "core/MediaUrlPolicy.h"

namespace {
constexpr int kFollowPageSize = 10;

QString positiveId(const QJsonObject &object, const QString &key) {
    const QString text = jh::getString(object, key);
    bool ok = false;
    const qulonglong id = text.toULongLong(&ok);
    return ok && id > 0 ? QString::number(id) : QString();
}

void checkRoot(const QJsonObject &root) {
    if (root.contains("code") && jh::getInt64(root, "code") != 0) {
        const int code = int(jh::getInt64(root, "code"));
        // 仅给出状态码，不将任意服务端错误原文（可能含媒体地址）送到日志。
        if (code == -101) throw ApiUnauthorizedError(Loc::get("登录已失效，请重新登录后刷新直播"));
        throw ApiError(code, Loc::get("直播接口请求失败（代码 %1）").arg(code));
    }
    if (!root.value("data").isObject())
        throw ApiError(-1, Loc::get("直播接口响应缺少有效数据"));
}

bool isHttpUrl(const QString &text) {
    for (const QChar ch : text) {
        if (ch.isSpace() || ch.unicode() < 0x20 || ch == QChar(0x7f)) return false;
    }
    const QUrl url(text, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
        && url.fragment().isEmpty()
        && (url.scheme() == "http" || url.scheme() == "https");
}

QString safeImage(const QString &value) {
    const QString normalized = jh::normalizeImageUrlLenient(value);
    return isHttpUrl(normalized) ? normalized : QString();
}

QString streamUrl(const QString &host, const QString &base, const QString &extra) {
    const QUrl origin(host, QUrl::StrictMode);
    if (!isHttpUrl(host) || !origin.query().isEmpty()
        || (!origin.path().isEmpty() && origin.path() != "/")
        || !base.startsWith('/') || base.startsWith("//")) return {};
    const QString url = host + base + extra;
    return isHttpUrl(url) ? url : QString();
}

struct Track {
    int rank = 0;
    int qn = 0;
    QList<int> acceptQn;
    QStringList urls;
};
}  // namespace

LiveFollowPage LiveApi::fetchFollowed(BilibiliApiClient &client, const QString &cookie, int page) {
    if (page < 1) throw ApiError(-1, Loc::get("直播列表页码无效"));
    QUrl url("https://api.live.bilibili.com/xlive/web-ucenter/user/following");
    QUrlQuery query;
    query.addQueryItem("page", QString::number(page));
    query.addQueryItem("page_size", QString::number(kFollowPageSize));
    query.addQueryItem("ignoreRecord", "1");
    query.addQueryItem("hit_ab", "true");
    url.setQuery(query);
    return parseFollowed(client.get(url, cookie).root, page);
}

LivePlayInfo LiveApi::fetchPlayInfo(BilibiliApiClient &client, const QString &cookie,
                                   const QString &roomId, int qn) {
    bool ok = false;
    const qulonglong id = roomId.toULongLong(&ok);
    if (!ok || id == 0) throw ApiError(-1, Loc::get("直播间编号无效"));
    QUrl url("https://api.live.bilibili.com/xlive/web-room/v2/index/getRoomPlayInfo");
    QUrlQuery query;
    query.addQueryItem("room_id", QString::number(id));
    query.addQueryItem("no_playurl", "0");
    query.addQueryItem("mask", "1");
    query.addQueryItem("qn", QString::number(qn > 0 ? qn : 10000));
    query.addQueryItem("platform", "web");
    query.addQueryItem("protocol", "0,1");
    query.addQueryItem("format", "0,1,2");
    query.addQueryItem("codec", "0,1");
    url.setQuery(query);
    return parsePlayInfo(client.get(url, cookie).root, qn);
}

LiveFollowPage LiveApi::parseFollowed(const QJsonObject &root, int page) {
    checkRoot(root);
    const QJsonObject data = root.value("data").toObject();
    if (!data.value("list").isArray())
        throw ApiError(-1, Loc::get("直播关注列表响应格式无效"));
    const QJsonArray list = data.value("list").toArray();
    LiveFollowPage result;
    QSet<QString> roomIds;
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        if (jh::getInt64(object, "live_status") != 1) continue;
        LiveRoom room;
        room.roomId = positiveId(object, "roomid");
        if (room.roomId.isEmpty()) room.roomId = positiveId(object, "room_id");
        room.mid = positiveId(object, "uid");
        if (room.roomId.isEmpty() || room.mid.isEmpty() || roomIds.contains(room.roomId)) continue;
        roomIds.insert(room.roomId);
        room.title = jh::getString(object, "title");
        room.uname = jh::getString(object, "uname");
        room.face = safeImage(jh::getString(object, "face"));
        room.cover = safeImage(jh::firstNonEmpty(jh::getString(object, "room_cover"),
            jh::getString(object, "cover_from_user"), jh::getString(object, "keyframe")));
        room.area = jh::firstNonEmpty(jh::getString(object, "area_name_v2"),
            jh::getString(object, "area_v2_name"), jh::getString(object, "area_name"));
        room.online = jh::getString(object, "text_small");
        result.rooms.append(room);
    }
    // 基于原始服务端页计算，不能用筛选后的在线人数判断尾页。
    if (data.contains("totalPage")) {
        result.hasMore = page < jh::getInt64(data, "totalPage");
    } else if (data.contains("count")) {
        const qint64 size = qMax<qint64>(1, jh::getInt64(data, "pageSize") > 0
            ? jh::getInt64(data, "pageSize") : kFollowPageSize);
        result.hasMore = qint64(page) * size < jh::getInt64(data, "count");
    } else {
        result.hasMore = list.size() >= kFollowPageSize;
    }
    return result;
}

LivePlayInfo LiveApi::parsePlayInfo(const QJsonObject &root, int preferredQn) {
    checkRoot(root);
    const QJsonObject data = root.value("data").toObject();
    LivePlayInfo result;
    result.roomId = positiveId(data, "room_id");
    result.liveStatus = int(jh::getInt64(data, "live_status"));
    if (jh::getBool(data, "is_locked") || jh::getBool(data, "is_hidden"))
        throw ApiError(-1, Loc::get("直播间当前不可访问"));
    if (jh::getBool(data, "encrypted") && !jh::getBool(data, "pwd_verified"))
        throw ApiError(-1, Loc::get("直播间需要密码，请前往网页验证"));
    if (result.liveStatus == 2)
        throw ApiError(-1, Loc::get("直播间正在轮播，暂不支持轮播播放"));
    if (result.liveStatus != 1)
        throw ApiError(-1, Loc::get("主播尚未开播或直播已结束"));
    if (result.roomId.isEmpty()) throw ApiError(-1, Loc::get("直播响应缺少有效房间编号"));
    const QJsonObject playurl = data.value("playurl_info").toObject().value("playurl").toObject();
    QMap<int, QString> descriptions;
    for (const QJsonValue &value : playurl.value("g_qn_desc").toArray()) {
        const QJsonObject object = value.toObject();
        descriptions.insert(int(jh::getInt64(object, "qn")), jh::getString(object, "desc"));
    }
    QList<Track> tracks;
    for (const QJsonValue &streamValue : playurl.value("stream").toArray()) {
        const QJsonObject stream = streamValue.toObject();
        const QString protocol = jh::getString(stream, "protocol_name");
        for (const QJsonValue &formatValue : stream.value("format").toArray()) {
            const QJsonObject format = formatValue.toObject();
            const QString name = jh::getString(format, "format_name");
            const int formatRank = protocol == "http_stream" && name == "flv" ? 0
                : protocol == "http_hls" && name == "fmp4" ? 10
                : protocol == "http_hls" && name == "ts" ? 20 : -1;
            if (formatRank < 0) continue;
            for (const QJsonValue &codecValue : format.value("codec").toArray()) {
                const QJsonObject codec = codecValue.toObject();
                const QString codecName = jh::getString(codec, "codec_name").toLower();
                const bool avc = codecName == "avc" || codecName.startsWith("avc1");
                const bool hevc = codecName == "hevc" || codecName.startsWith("hev1")
                    || codecName.startsWith("hvc1");
                if (!avc && !hevc) continue;
                Track track;
                track.rank = formatRank + (avc ? 0 : 30);
                track.qn = int(jh::getInt64(codec, "current_qn"));
                if (track.qn <= 0) continue;
                for (const QJsonValue &qnValue : codec.value("accept_qn").toArray()) {
                    const int qn = qnValue.toVariant().toInt();
                    if (qn > 0 && !track.acceptQn.contains(qn)) track.acceptQn.append(qn);
                }
                for (const QJsonValue &infoValue : codec.value("url_info").toArray()) {
                    const QJsonObject info = infoValue.toObject();
                    const QString url = streamUrl(jh::getString(info, "host"),
                        jh::getString(codec, "base_url"), jh::getString(info, "extra"));
                    if (!url.isEmpty() && !track.urls.contains(url)) track.urls.append(url);
                }
                if (!track.urls.isEmpty()) tracks.append(track);
            }
        }
    }
    if (tracks.isEmpty()) throw ApiError(-1, Loc::get("直播间没有可用的播放地址"));
    std::stable_sort(tracks.begin(), tracks.end(), [preferredQn](const Track &a, const Track &b) {
        if ((a.qn == preferredQn) != (b.qn == preferredQn)) return a.qn == preferredQn;
        return a.rank < b.rank;
    });
    const Track &selected = tracks.first();
    result.currentQn = selected.qn;
    QList<int> accepted = selected.acceptQn;
    if (!accepted.contains(selected.qn)) accepted.prepend(selected.qn);
    std::sort(accepted.begin(), accepted.end(), std::greater<int>());
    for (const int qn : accepted) {
        const QString label = descriptions.value(qn).isEmpty() ? QString::number(qn) : descriptions.value(qn);
        result.qualities.append(QVariantMap{{"qn", qn}, {"label", label}});
    }
    for (const Track &track : tracks) {
        if (track.qn != result.currentQn) continue;
        for (const QString &url : track.urls) {
            if (!result.urls.contains(url)) result.urls.append(url);
        }
    }
    result.urls = MediaUrlPolicy::ordered(result.urls);
    return result;
}
