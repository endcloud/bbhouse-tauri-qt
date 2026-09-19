#ifndef PLAYER_API_H
#define PLAYER_API_H

#include <QList>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "core/BilibiliApiClient.h"

// 播放域端点(统一管线之上)。移植自 C# BilibiliPlayerClient,端点参考:
// b3/bilibili-api-collect docs/video/info.md、docs/video/videostream_url.md
// (wbi playurl,DASH + durl)、docs/bangumi/videostream_url.md(PGC playurl +
// 权益字段)、docs/video/player.md(字幕 v2)与弹幕存档
// comment.bilibili.com/{cid}.xml。信封 code 校验(-101 → ApiUnauthorizedError、
// 非零 → ApiError)与 HTTP 状态检查由统一管线承担(对应 C# 侧逐点 throw 的
// Ply_NotLoggedIn / Ply_*ApiCode / Ply_HttpStatus 文案)。
class PlayerApi {
   public:
    // pagelist 元素(分 P)。
    struct VideoPage {
        qint64 cid = 0;
        int page = 0;
        QString part;
    };

    // 清晰度档位(accept_quality 与 accept_description 按下标对齐的条目)。
    struct QualityOption {
        int qn = 0;
        QString label;
    };

    // MP4 渐进式播放地址(fnval=1 durl 路径)。
    struct PlayUrl {
        QString url;                        // urls 首项，兼容单地址调用方
        QStringList urls;                  // 同一 durl 分段的完整有序主备链
        int quality = 0;
        QString qualityLabel;
        QList<QualityOption> availableQualities;
    };

    // support_formats 条目:清晰度 id、显示名、VIP 门槛,以及与 dash 视频段交叉
    // 校验后的「该档位在本响应里是否真的存在可播视频段」。
    struct DashFormat {
        int quality = 0;
        QString label;
        bool needVip = false;
        bool available = true;
    };

    // 解析完成的 DASH 播放。候选链按已知公有云、其他普通 CDN、mcdn/PCDN
    // 排序，保留全部备选用于失败回退。除 C# 原有的所选段
    // 信息外,按移植要求保留全部段的候选链(信息只增不减)。
    struct DashPlayInfo {
        QList<DashFormat> formats;           // 档位表(needVip / available)
        int selectedQn = 0;                  // 所选清晰度(C# VideoQuality)
        QString selectedCodec;               // 所选编码(C# VideoCodec)
        QStringList selectedVideoUrls;       // 所选视频段候选链(C# VideoUrls)
        QStringList selectedAudioUrls;       // 所选音频段候选链(C# AudioUrls,取最高规格段;无音频为空)
        QList<QStringList> videoCandidates;  // 每个视频段(清晰度×编码)的候选链
        QList<int> videoQualities;           // 与 videoCandidates 按下标对齐
        QList<QString> videoCodecs;          // 与 videoCandidates 按下标对齐
        QList<QStringList> audioCandidates;  // 每个音频段的主 + backup 排序候选链
        bool hasAudio = false;
    };

    // 字幕轨(/x/player/wbi/v2;AI 轨需登录,lan 以 "ai…" 开头)。
    struct SubtitleTrack {
        QString lan;
        QString lanDoc;
        QString url;
        bool isAi = false;
    };

    // PGC playurl 响应(result 信封)中的权益快照:is_preview=1 表示下发的是试看
    // 切片;has_paid 反映对该季的付费;vip_type 是 PGC 端点视角的账号 VIP 类型。
    struct PgcEntitlement {
        bool isPreview = false;
        bool hasPaid = false;
        qint64 vipType = 0;
    };

    // PGC DASH 播放 + 权益字段。
    struct PgcDashInfo {
        DashPlayInfo dash;
        PgcEntitlement entitlement;
    };

    // PGC MP4 durl 播放 + 权益字段。
    struct PgcPlayUrl {
        PlayUrl playUrl;
        PgcEntitlement entitlement;
    };

    // 分 P 1 的 cid —— v1 只播分 P 1。
    static VideoPage getFirstPage(BilibiliApiClient &api, qint64 aid, const QString &cookie);

    // MP4 渐进式播放地址(fnval=1 durl 路径)。先试 wbi/pc 形态,再回退跳过 referer
    // 握手的 html5 形态。durl 路径不总能满足高 qn;qn≠64 全部失败时以 64 重试一次
    // —— 保证清晰度选择永远不能把播放搞挂。
    static PlayUrl getPlayUrl(BilibiliApiClient &api, qint64 aid, qint64 cid,
                              const QString &cookie, int qn = 64);

    // DASH 解析:fnval 载入全部 DASH 相关位(dash/HDR/杜比/8K/AV1 =
    // 16|128|256|512|1024|2048)。响应把视频段与音频段分开,由 mpv 内核按主文件 +
    // 外挂音轨播放。响应无可用 dash 载荷时抛错,由调用方回退 durl 管线。
    static DashPlayInfo getDashPlayUrl(BilibiliApiClient &api, qint64 aid, qint64 cid,
                                       const QString &cookie, int preferQn,
                                       const QString &preferCodec);

    // PGC(番剧/电影)DASH 解析:分集由 ep_id + cid 直接定位(取自历史条目,无
    // pagelist 往返),端点无需 WBI 签名,载荷位于 result 信封。dash 对象与主站
    // 端点同构,档位/编码选择共用。响应同时携带权益字段(is_preview / has_paid /
    // vip_type),供拒绝播放流程消费。
    static PgcDashInfo getPgcDashPlayUrl(BilibiliApiClient &api, qint64 epId, qint64 cid,
                                         const QString &cookie, int preferQn,
                                         const QString &preferCodec);

    // PGC MP4 durl 变体(内核回退路径);同端点 fnval=1。
    static PgcPlayUrl getPgcPlayUrl(BilibiliApiClient &api, qint64 epId, qint64 cid,
                                    const QString &cookie, int qn);

    // 从 season API 解析分集 cid(动态 feed 条目只有 epid)。匹配不到返回 0 ——
    // 调用方视作「无弹幕」,绝不视作播放失败。session 级缓存由调用方负责。
    static qint64 getPgcEpisodeCid(BilibiliApiClient &api, qint64 epId, const QString &cookie);

    // 从 PGC playurl result 体提取权益快照。is_preview / has_paid 是 JSON 布尔
    // (与 need_vip 同为布尔/数字双形态,读法一致)。
    static PgcEntitlement parsePgcEntitlement(const QJsonObject &result);

    // nav 端点的账号 VIP 状态(驱动清晰度菜单的 VIP 门控)。
    static bool getVipStatus(BilibiliApiClient &api, const QString &cookie);

    // 字幕轨列表(AI 轨需登录)。
    static QList<SubtitleTrack> getSubtitleTracks(BilibiliApiClient &api, qint64 aid,
                                                  qint64 cid, const QString &cookie,
                                                  qint64 epId = 0);
    static QList<SubtitleTrack> parseSubtitleTracks(const QJsonObject &data);

    // 拉取字幕轨 JSON({body:[{from,to,content}]})原文。
    static QString getSubtitleJson(BilibiliApiClient &api, const QString &url,
                                   const QString &cookie);

    // 拉取并解压某 cid 的全量 XML 弹幕存档(gzip/zlib/raw 自适应)。
    static QString getDanmakuXml(BilibiliApiClient &api, qint64 cid, const QString &cookie);

    // 纯响应解析，供缓存载荷与离线回归复用；输入是已解包的 data/result。
    static PlayUrl parseDurlFromData(const QJsonObject &data);
    static DashPlayInfo selectDash(const QJsonObject &data, const QJsonObject &dash,
                                   int preferQn, const QString &preferCodec);
    static qint64 parsePgcEpisodeCid(const QJsonObject &result, qint64 epId);

   private:
    // dash.video / dash.audio 数组元素(解析中间态)。
    struct DashSegment {
        int quality = 0;
        QString codec;
        QString baseUrl;
        QStringList backupUrls;
    };

    // getPlayUrl 的单次形态尝试(pc → html5,均带 high_quality=1)。
    static PlayUrl getPlayUrlCore(BilibiliApiClient &api, qint64 aid, qint64 cid,
                                  const QString &cookie, int qn);

    static QList<QualityOption> parseQualityOptions(const QJsonObject &data);

    // PGC playurl 共用请求,返回 result 信封体。
    static QJsonObject getPgcPlayUrlResult(BilibiliApiClient &api, qint64 epId, qint64 cid,
                                           const QString &cookie, const QString &fnval,
                                           int qn);

    static QStringList orderByStableCdn(const QString &baseUrl, const QStringList &backupUrls);
    static QList<DashSegment> parseDashSegments(const QJsonObject &dash, const QString &property);
    static QList<DashFormat> parseDashFormats(const QJsonObject &data);

    static QString decompressDanmaku(const QByteArray &bytes);
};

#endif  // PLAYER_API_H
