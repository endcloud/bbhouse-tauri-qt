#include "core/RelationApi.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include "core/JsonHelpers.h"

namespace {
constexpr const char *kFollowingsEndpoint = "https://api.bilibili.com/x/relation/followings";
constexpr const char *kFollowingsSearchEndpoint =
        "https://api.bilibili.com/x/relation/followings/search";
constexpr const char *kTagEndpoint = "https://api.bilibili.com/x/relation/tag";
}  // namespace

FollowingsPage RelationApi::fetchFollowings(BilibiliApiClient &api, const QString &cookie,
                                            qint64 vmid, int pn, int ps) {
    QUrl url(kFollowingsEndpoint);
    QUrlQuery query;
    query.addQueryItem("vmid", QString::number(vmid));
    query.addQueryItem("ps", QString::number(ps));
    query.addQueryItem("pn", QString::number(pn));
    url.setQuery(query);

    const BilibiliApiClient::Envelope envelope = api.get(url, cookie);
    return parseFollowings(envelope.root);
}

FollowingsPage RelationApi::fetchFollowingsSearch(BilibiliApiClient &api, const QString &cookie,
                                                  qint64 vmid, const QString &name, int pn,
                                                  int ps) {
    QUrl url(kFollowingsSearchEndpoint);
    QUrlQuery query;
    query.addQueryItem("vmid", QString::number(vmid));
    query.addQueryItem("name", name);
    query.addQueryItem("order", "desc");
    query.addQueryItem("order_type", "");  // 空值镜像 web 端查询形状
    query.addQueryItem("ps", QString::number(ps));
    query.addQueryItem("pn", QString::number(pn));
    url.setQuery(query);

    const BilibiliApiClient::Envelope envelope = api.get(url, cookie);
    return parseFollowings(envelope.root);
}

QList<FollowUser> RelationApi::fetchSpecialTag(BilibiliApiClient &api, const QString &cookie,
                                               int pn, int ps) {
    QUrl url(kTagEndpoint);
    QUrlQuery query;
    query.addQueryItem("tagid", "-10");
    query.addQueryItem("ps", QString::number(ps));
    query.addQueryItem("pn", QString::number(pn));
    url.setQuery(query);

    const BilibiliApiClient::Envelope envelope = api.get(url, cookie);
    return parseUserArray(envelope.root);
}

FollowingsPage RelationApi::parseFollowings(const QJsonObject &root) {
    FollowingsPage page;
    const QJsonObject data = jh::getChild(root, "data");
    const QJsonArray list = jh::getChildArray(data, "list");
    for (const QJsonValue &value : list) {
        if (value.isObject()) page.users.append(parseUser(value.toObject()));
    }
    page.total = jh::getInt64(data, "total");
    return page;
}

QList<FollowUser> RelationApi::parseUserArray(const QJsonObject &root) {
    QList<FollowUser> users;
    const QJsonArray data = jh::getChildArray(root, "data");
    for (const QJsonValue &value : data) {
        if (value.isObject()) users.append(parseUser(value.toObject()));
    }
    return users;
}

FollowUser RelationApi::parseUser(const QJsonObject &element) {
    FollowUser user;
    user.mid = jh::getInt64(element, "mid");
    user.name = jh::getString(element, "uname");
    user.faceUrl = jh::normalizeImageUrlLenient(jh::getString(element, "face"));
    user.sign = jh::getString(element, "sign");
    user.isSpecial = jh::getInt64(element, "special") != 0;
    return user;
}
