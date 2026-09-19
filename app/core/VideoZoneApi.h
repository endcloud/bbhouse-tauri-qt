#ifndef VIDEO_ZONE_API_H
#define VIDEO_ZONE_API_H

#include <QUrl>
#include <QString>

#include "core/BilibiliApiClient.h"

// 单个视频 view 载荷化简到分区定位字段(另带标题供调用方做健全性核对)。
// 契约同原 VideoViewInfo.cs record。
struct VideoViewInfo {
    qint64 tid = 0;
    qint64 tidV2 = 0;
    QString tname;     // 原值保留(空保持空):分区名来自 VideoZones,此处绝不猜测
    QString tnameV2;   // 同上
    QString title;
};

// 视频 view/分区端点。端点参考:
// b3/bilibili-api-collect/docs/video/info.md(x/web-interface/view,无 WBI)。
//
// 与文档的实测偏差(2026-09-03 对关注用户动态流验证):动态流 archive 载荷
// 完全不带分区 id;本端点虽返回数值 tid/tid_v2,但抽样视频(含长期存在的老
// 视频)的 tname/tname_v2 一律为空 —— 分区名必须经 VideoZones 内嵌表解析,
// 不能信响应。
class VideoZoneApi {
   public:
    // 按 bvid 取单个视频的分区定位。信封处理走统一管线(code=0;-101 → 登录
    // 失效;其他码如 -404(视频已删除)抛出并携带 code 与 message)。
    static VideoViewInfo fetchView(BilibiliApiClient &api, const QString &cookie,
                                   const QString &bvid);

    // 同一查询按 avid 键 —— 动态页按 aid 对视频出现去重,其分区补全队列带的
    // 就是 aid。
    static VideoViewInfo fetchViewByAid(BilibiliApiClient &api, const QString &cookie,
                                        qint64 aid);

    static VideoViewInfo fetchCore(BilibiliApiClient &api, const QString &cookie, const QUrl &url);
};

#endif  // VIDEO_ZONE_API_H
