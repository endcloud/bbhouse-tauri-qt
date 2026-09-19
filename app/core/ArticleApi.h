#ifndef ARTICLE_API_H
#define ARTICLE_API_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/BilibiliApiClient.h"

// 专栏文集域模型(契约同原 ArticleApi.cs)。UI 无关。
struct ArticleListEntry {
    qint64 id = 0;
    QString name;
    QString coverUrl;
    qint64 updateTime = 0;     // 最近更新时间戳
    qint64 words = 0;          // 字数
    qint64 read = 0;           // 阅读数
    qint64 articlesCount = 0;  // 文章数
};

// up 的全部文集 + 总数
struct ArticleListsPage {
    QList<ArticleListEntry> lists;
    qint64 total = 0;
};

// 专栏文集("专栏文集")端点。端点参考:
// b3/bilibili-api-collect/docs/user/space.md(GET /x/article/up/lists,
// Cookie 鉴权,无 WBI)。端点一次返回全量 —— 不存在服务端分页参数
// (文档只列 mid/sort);调用方本地按每页 30 分页。sort 保持默认 0
// (最近更新优先)—— 页面不提供文集排序。
class ArticleApi {
   public:
    static ArticleListsPage fetchArticleLists(BilibiliApiClient &api, const QString &cookie,
                                              qint64 mid);

    static ArticleListsPage parsePage(const QJsonObject &root);
    static ArticleListEntry parseEntry(const QJsonObject &element);
};

#endif  // ARTICLE_API_H
