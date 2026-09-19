#ifndef BANGUMI_API_H
#define BANGUMI_API_H

#include <functional>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include "core/BilibiliApiClient.h"

// 追番追剧(Bangumi)域端点(统一管线之上)。端点参考:
// b3/bilibili-api-collect/docs/user/space.md §查询用户追番(追剧)明细 与 docs/bangumi/*。
// 无 WBI;必须携带管线共享的浏览器 UA/Referer 头(与关系域端点同一风控姿态)。
//
// 与文档的线上偏差(2026-09-07 对作者账号实测):type 参数只接受 1(追番)和 2(追剧)
// —— 3..6 一律以空 data 的 -400 信封拒绝。每个桶在服务端混合多种 season 类型
// (桶 1:番剧 season_type=1 + 国创 season_type=4;桶 2:电影=2/纪录片=3/电视剧=5/
// 综艺=7 ……),因此 UI 的四个标签页是对这两桶的客户端过滤 —— 不存在服务端按类型查询。
class BangumiApi {
   public:
    // follow list 端点文档标注的服务端页大小上限(定义域 1-30)。
    static constexpr int kMaxPageSize = 30;
    static constexpr int kMaxRegionalScanPages = 500;

    // 追番页展示的一季(番剧/国创/影视/纪录片/综艺…)。
    struct BangumiSeason {
        qint64 seasonId = 0;
        int seasonType = 0;
        QString seasonTypeName;
        QString title;
        bool regional = false;
        QString cover;
        QString squareCover;
        QString badge;
        qint64 totalCount = 0;
        bool isFinish = false;
        QString newEpIndexShow;
        double ratingScore = 0.0;  // NaN = 条目无评分
        qint64 ratingCount = 0;
        QString progressText;
        QString subtitle;
        QString evaluate;
        QString url;
        QString rawJson;
    };

    // follow 桶的一页 + 桶内总数(翻页上界)。
    struct FollowListPage {
        QList<BangumiSeason> items;
        qint64 total = 0;
    };

    // 单集可播放条目:epid/cid 对可直接定位播放流,无需再查;时长归一为秒。
    struct PgcSeasonEpisode {
        QString coverUrl;
        qint64 epId = 0;
        qint64 cid = 0;
        QString title;
        QString longTitle;
        int durationSeconds = 0;
        QString badge;
    };

    // 单部 PGC 剧集的完整剧集表,含账号观看位置。
    struct PgcSeasonDetail {
        QString coverUrl;
        qint64 seasonId = 0;
        QString title;
        QList<PgcSeasonEpisode> episodes;
        qint64 lastEpId = 0;
        QString lastEpIndex;  // user_status.progress.last_ep_index(规格要求原值)
        int lastTimeSeconds = 0;
        QString rawJson;  // result 体原文(规格要求保留供上层扩展)
    };

    // 从 cookie 字符串提取账号 mid(DedeUserID)。follow list 需要 vmid;解析 cookie
    // 让校验保持离线,并与 nav 端点的报告一致。缺失时抛登录失效语义错误。
    static qint64 tryGetSelfMid(const QString &cookie);

    // 拉取 followType 桶的一页(1=追番,2=追剧 —— 仅有的服务端接受值;见类注释偏差)。
    static FollowListPage fetchFollowList(BilibiliApiClient &api, const QString &cookie,
                                          int followType, int pn, int ps);

    // 按 season_id 拉取整季剧集表。载荷位于 `result` 信封(与 PGC playurl 端点同形态)。
    // 账号观看位置(user_status.progress)定位续播分集;从未观看的剧集两者皆缺。
    static PgcSeasonDetail fetchSeason(BilibiliApiClient &api, const QString &cookie,
                                       qint64 seasonId);

    // 只认明确的地区限制标记，简繁体与全半角括号均兼容。
    static bool isRegionalTitle(const QString &title);
    // 从追番(type=1)完整扫描，成功后才交付快照；跨页去重，失败不返回部分列表。
    // fetchPage 参数同时允许离线 fixture 验证分页扫描行为。
    static QList<BangumiSeason> scanRegionalFollowList(
            const std::function<FollowListPage(int)> &fetchPage);
    // 港澳台先全量搜索再本地分页，返回过滤后总数。
    static FollowListPage regionalPage(const QList<BangumiSeason> &items,
                                       const QString &query, int page);

    static BangumiSeason parseSeason(const QJsonObject &element);
};

#endif  // BANGUMI_API_H
