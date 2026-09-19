#include "core/VideoZoneApi.h"

#include <QJsonObject>
#include <QUrlQuery>

#include "core/JsonHelpers.h"

namespace {
constexpr const char *kViewEndpoint = "https://api.bilibili.com/x/web-interface/view";
}  // namespace

VideoViewInfo VideoZoneApi::fetchView(BilibiliApiClient &api, const QString &cookie,
                                      const QString &bvid) {
    QUrl url(kViewEndpoint);
    QUrlQuery query;
    query.addQueryItem("bvid", bvid);
    url.setQuery(query);
    return fetchCore(api, cookie, url);
}

VideoViewInfo VideoZoneApi::fetchViewByAid(BilibiliApiClient &api, const QString &cookie,
                                           qint64 aid) {
    QUrl url(kViewEndpoint);
    QUrlQuery query;
    query.addQueryItem("aid", QString::number(aid));
    url.setQuery(query);
    return fetchCore(api, cookie, url);
}

VideoViewInfo VideoZoneApi::fetchCore(BilibiliApiClient &api, const QString &cookie,
                                      const QUrl &url) {
    const BilibiliApiClient::Envelope envelope = api.get(url, cookie);

    const QJsonObject data = jh::getChild(envelope.root, "data");
    VideoViewInfo info;
    info.tid = jh::getInt64(data, "tid");
    info.tidV2 = jh::getInt64(data, "tid_v2");
    // 原值保留(空保持空):分区名来自 VideoZones,此处绝不猜测
    info.tname = jh::getString(data, "tname");
    info.tnameV2 = jh::getString(data, "tname_v2");
    info.title = jh::getString(data, "title");
    return info;
}
