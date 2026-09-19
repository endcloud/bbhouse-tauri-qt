#include "core/HistoryApi.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QUrlQuery>

#include "core/JsonHelpers.h"

namespace {
constexpr const char *kEndpoint = "https://api.bilibili.com/x/web-interface/history/cursor";
}  // namespace

BilibiliHistoryPage HistoryApi::fetchPage(BilibiliApiClient &api, const QString &cookie,
                                          const HistoryCursor &cursor, int pageSize) {
    const auto envelope = api.get(buildUri(cursor, pageSize), cookie);
    BilibiliHistoryPage page;
    page.rawJson = envelope.raw;

    const QJsonObject data = jh::getChild(envelope.root, "data");
    if (data.isEmpty()) return page;

    page.cursor = parseCursor(data);
    const QJsonArray list = jh::getChildArray(data, "list");
    for (const QJsonValue &value : list) {
        if (value.isObject()) page.items.append(parseItem(value.toObject()));
    }
    return page;
}

QUrl HistoryApi::buildUri(const HistoryCursor &cursor, int pageSize) {
    QUrl url(kEndpoint);
    QUrlQuery query;
    query.addQueryItem("ps", QString::number(pageSize));
    query.addQueryItem("type", "all");
    if (cursor.pageSize > 0 || cursor.max != 0 || cursor.viewAt != 0 ||
        !cursor.business.isEmpty()) {
        query.addQueryItem("max", QString::number(cursor.max));
        query.addQueryItem("view_at", QString::number(cursor.viewAt));
        if (!cursor.business.trimmed().isEmpty()) {
            query.addQueryItem("business", cursor.business);
        }
    }
    url.setQuery(query);
    return url;
}

HistoryCursor HistoryApi::parseCursor(const QJsonObject &data) {
    const QJsonObject cursor = jh::getChild(data, "cursor");
    if (cursor.isEmpty()) return {};
    HistoryCursor out;
    out.max = jh::getInt64(cursor, "max");
    out.viewAt = jh::getInt64(cursor, "view_at");
    out.business = jh::getString(cursor, "business");
    out.pageSize = static_cast<int>(jh::getInt64(cursor, "ps"));
    return out;
}

HistoryItem HistoryApi::parseItem(const QJsonObject &element) {
    const QJsonObject history = jh::getChild(element, "history");
    const QString business = jh::getString(history, "business");
    const qint64 oid = jh::getInt64(history, "oid");
    const QString bvid = jh::getString(history, "bvid");
    const QString uri = jh::getString(element, "uri");
    const qint64 kid = jh::getInt64(element, "kid");

    HistoryItem item;
    // API 解析即产出 video_key(与 store 建键同式):在线页去重依赖它
    item.videoKey = business + ":" + QString::number(oid) + ":" + QString::number(kid);
    item.kid = kid;
    item.oid = oid;
    item.business = business;
    item.title = jh::getString(element, "title");
    item.subtitle = jh::firstNonEmpty(jh::getString(element, "long_title"),
                                      jh::getString(element, "show_title"),
                                      jh::getString(history, "part"));
    item.coverUrl =
            jh::normalizeImageUrlLenient(findCoverUrl(element));
    item.authorName = jh::getString(element, "author_name");
    item.authorMid = jh::getInt64(element, "author_mid");
    item.viewAt = jh::getInt64(element, "view_at");
    item.progress = static_cast<int>(jh::getInt64(element, "progress"));
    item.duration = static_cast<int>(jh::getInt64(element, "duration"));
    item.badge = jh::getString(element, "badge");
    item.linkUrl = buildLinkUrl(uri, business, bvid, oid, kid);
    item.rawJson = QString::fromUtf8(QJsonDocument(element).toJson(QJsonDocument::Compact));
    return item;
}

QString HistoryApi::buildLinkUrl(const QString &uri, const QString &business,
                                 const QString &bvid, qint64 oid, qint64 kid) {
    if (!uri.trimmed().isEmpty()) return uri;

    if (business == "archive" && !bvid.trimmed().isEmpty()) {
        return "https://www.bilibili.com/video/" + bvid;
    }
    if (business == "article" && (oid > 0 || kid > 0)) {
        return "https://www.bilibili.com/read/cv" + QString::number(oid > 0 ? oid : kid);
    }
    if (business == "live" && (oid > 0 || kid > 0)) {
        return "https://live.bilibili.com/" + QString::number(oid > 0 ? oid : kid);
    }
    if (business == "pgc" && (oid > 0 || kid > 0)) {
        return "https://www.bilibili.com/bangumi/play/ss" +
               QString::number(kid > 0 ? kid : oid);
    }
    return "https://www.bilibili.com/";
}

QString HistoryApi::findCoverUrl(const QJsonObject &element) {
    const QString cover = jh::getString(element, "cover");
    if (!cover.trimmed().isEmpty()) return cover;

    const QJsonArray covers = jh::getChildArray(element, "covers");
    for (const QJsonValue &value : covers) {
        if (value.isString() && !value.toString().trimmed().isEmpty())
            return value.toString();
    }
    return QString();
}
