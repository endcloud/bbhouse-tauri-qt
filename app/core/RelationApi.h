#ifndef RELATION_API_H
#define RELATION_API_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/BilibiliApiClient.h"

// 关注关系域模型(契约同原 RelationApi.cs)。UI 无关。
struct FollowUser {
    qint64 mid = 0;
    QString name;      // 响应字段 uname
    QString faceUrl;   // 已归一化 https
    QString sign;      // 个性签名
    bool isSpecial = false;  // 响应字段 special != 0(特别关注标志)
};

// 一页关注用户 + 账号关注总数(分页上界)
struct FollowingsPage {
    QList<FollowUser> users;
    qint64 total = 0;
};

// 关注关系域端点。端点参考:b3/bilibili-api-collect/docs/user/relation.md
// (Cookie 鉴权,无 WBI)。followings 端点对非浏览器客户端有风控(-352),
// 因此走管线共享的浏览器 UA/Referer 头。
class RelationApi {
   public:
    // 取 vmid 的一页关注列表(本人账号全量深度;他人账号服务端截断到 100 条)。
    // 传 ps/pn 向前翻页。
    static FollowingsPage fetchFollowings(BilibiliApiClient &api, const QString &cookie,
                                          qint64 vmid, int pn, int ps);

    // 对 vmid 的关注列表做服务端昵称搜索。响应形状与 fetchFollowings 一致。
    // 分页服务端封顶 5 页(超过返回 code 22007),pn 保持 <= 5。
    // order/order_type 镜像 web 端查询形状(文档未记载,服务端无害)。
    // 与文档的实测偏差(2026-09-03 验证):响应 total 是"匹配数"(文档声称是
    // 关注总数);缺省 name 返回空列表而非未过滤列表 —— 调用方把空词当
    // "浏览模式",改走 fetchFollowings。
    static FollowingsPage fetchFollowingsSearch(BilibiliApiClient &api, const QString &cookie,
                                                qint64 vmid, const QString &name, int pn, int ps);

    // 取特别关注分组(tagid=-10)的一页:完整用户对象带头像 —— 与
    // /relation/tag/special 只回裸 mid 不同。翻页直到某页条数少于 ps。
    static QList<FollowUser> fetchSpecialTag(BilibiliApiClient &api, const QString &cookie,
                                             int pn, int ps);

    static FollowingsPage parseFollowings(const QJsonObject &root);
    // /x/relation/tag 的用户数组直接挂在 data 下(无 list 包装、无 total)。
    static QList<FollowUser> parseUserArray(const QJsonObject &root);
    static FollowUser parseUser(const QJsonObject &element);
};

#endif  // RELATION_API_H
