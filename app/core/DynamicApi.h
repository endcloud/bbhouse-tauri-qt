#ifndef DYNAMIC_API_H
#define DYNAMIC_API_H

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include "core/BilibiliApiClient.h"
#include "core/DynamicFeedModels.h"

// 动态域端点(统一管线之上)。端点参考:
// b3/bilibili-api-collect/docs/dynamic/all.md(feed/all,offset 翻页,Cookie 鉴权,无
// WBI)。docs/dynamic/dynamic_enum.md 为 major 类型表。特别关注页曾试接 feed/space
// (docs/dynamic/space.md)后又移除:即使已登录,携带 dm_img 的请求仍概率性命中 412
// 风控,重试无法稳定消除(2026-09-07)。
//
// 分类在客户端判定:端点的 type 过滤参数文档已标注废弃("新版无"),且没有"其他"桶。
class DynamicApi {
   public:
    // 拉取一页动态;翻页传上一响应的 offset,空串请求第一页。
    static DynamicFeedPage fetchFeed(BilibiliApiClient &api, const QString &cookie,
                                     const QString &offset);

    static QUrl buildFeedUri(const QString &offset);
    static DynamicFeedPage parseFeed(const QJsonObject &root, const QString &rawJson);
    static DynamicFeedItem parseItem(const QJsonObject &element);
    static DynamicCategory mapCategory(const QString &itemType, const QString &majorType);
    // 把 API 的人类时长文本(h:mm:ss 或 m:ss)解析为秒;缺席或无法识别返回 0。
    static int parseDurationText(const QString &text);

   private:
    // 从生效 major 对象填充展示字段。所有读取皆容错 —— 缺字段得到 空/0 而非抛错。
    static void fillContent(DynamicFeedItem &item, const QJsonObject &major,
                            const QString &majorType);
    static void fillFromLiveRcmdContent(DynamicFeedItem &item, const QString &content);
    static QJsonObject findMajor(const QJsonObject &item);
    static QString getDescText(const QJsonObject &item);
    static QString getAuthorName(const QJsonObject &item);
    static qint64 getAuthorMid(const QJsonObject &item);
    static qint64 getPubTs(const QJsonObject &item);
    static QString normalizeLinkUrl(const QString &url);
};

#endif  // DYNAMIC_API_H
