#include "core/UserProfileApi.h"
#include <QUrlQuery>
#include "core/ApiErrors.h"
#include "core/BilibiliApiClient.h"
#include "core/JsonHelpers.h"

UserProfile UserProfileApi::fetch(BilibiliApiClient &api, qint64 mid) {
    QUrl url(QStringLiteral("https://api.bilibili.com/x/web-interface/card"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("mid"), QString::number(mid));
    url.setQuery(query);
    UserProfile result = parse(api.get(url, {}).root);
    if (result.mid != mid) throw ApiError(-1, Loc::get("用户资料加载失败"));
    return result;
}

UserProfile UserProfileApi::parse(const QJsonObject &root) {
    const auto data = jh::getChild(root, "data");
    const auto card = jh::getChild(data, "card");
    UserProfile result;
    result.mid = jh::getInt64(card, "mid");
    result.name = jh::getString(card, "name");
    result.faceUrl = jh::normalizeImageUrlLenient(jh::getString(card, "face"));
    result.sign = jh::getString(card, "sign");
    result.archiveCount = qMax<qint64>(0, jh::getInt64(data, "archive_count"));
    return result;
}
