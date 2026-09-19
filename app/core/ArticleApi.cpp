#include "core/ArticleApi.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include "core/JsonHelpers.h"

namespace {
constexpr const char *kArticleListsEndpoint = "https://api.bilibili.com/x/article/up/lists";
}  // namespace

ArticleListsPage ArticleApi::fetchArticleLists(BilibiliApiClient &api, const QString &cookie,
                                               qint64 mid) {
    QUrl url(kArticleListsEndpoint);
    QUrlQuery query;
    query.addQueryItem("mid", QString::number(mid));
    url.setQuery(query);

    const BilibiliApiClient::Envelope envelope = api.get(url, cookie);
    return parsePage(envelope.root);
}

ArticleListsPage ArticleApi::parsePage(const QJsonObject &root) {
    const QJsonObject data = jh::getChild(root, "data");

    ArticleListsPage page;
    const QJsonArray array = jh::getChildArray(data, "lists");
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        page.lists.append(parseEntry(value.toObject()));
    }

    page.total = jh::getInt64(data, "total");
    return page;
}

ArticleListEntry ArticleApi::parseEntry(const QJsonObject &element) {
    ArticleListEntry entry;
    entry.id = jh::getInt64(element, "id");
    entry.name = jh::getString(element, "name");
    entry.coverUrl = jh::normalizeImageUrlLenient(jh::getString(element, "image_url"));
    entry.updateTime = jh::getInt64(element, "update_time");
    entry.words = jh::getInt64(element, "words");
    entry.read = jh::getInt64(element, "read");
    entry.articlesCount = jh::getInt64(element, "articles_count");
    return entry;
}
