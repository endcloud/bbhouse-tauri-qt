#ifndef HEARTBEAT_API_H
#define HEARTBEAT_API_H

#include <QString>
#include <QUrl>

#include "core/BilibiliApiClient.h"

// 播放进度心跳上报(video/report.md: 上报视频播放心跳 web 端)。
// POST /x/click-interface/web/heartbeat,仅 Cookie 鉴权;播放器窗口在每次条目
// 切换时,对离场条目发一条"播放结束"上报、对进场条目发一条"播放开始"上报。
// 端点的参数计算被文档标注为推测,因此 realtime 与 played_time 同值
// ("把所有已播时长设为同值"),web 播放器私有的可省字段
// (refer_url/spmid/session/…)一律省略。
//
// 上报是尽力而为(fire-and-forget):调用方吞掉失败;管线仍会校验 HTTP 状态
// 与信封(code=0、无 data 体视为成功)。
class HeartbeatApi {
   public:
    // play_type:1 = 播放开始,4 = 播放结束
    enum PlayAction {
        Start = 1,
        End = 4,
    };

    // 一次进度上报。UGC:Aid>0(可带 Cid);PGC:EpId>0(可带 Cid)。
    struct HeartbeatReport {
        qint64 aid = 0;     // UGC avid(PGC 条目为 0)
        qint64 cid = 0;     // 视频 cid(已知时用于标识分 P/剧集)
        qint64 epId = 0;    // PGC epid(UGC 条目为 0)
        double playedSeconds = 0.0;  // 进度秒数(切换时刻的位置)
        PlayAction action = Start;   // 1 = 播放开始,4 = 播放结束
    };

    static void report(BilibiliApiClient &api, const QString &cookie,
                       const HeartbeatReport &reportData);

    // endpoint 覆盖生产端点 —— 无头请求形状探针指向本地监听器而非真实站点
    static void report(BilibiliApiClient &api, const QString &cookie,
                       const HeartbeatReport &reportData, const QUrl &endpoint);

    // 提取 bili_jct cookie 值(心跳 csrf token)。与原 BangumiApi.TryGetSelfMid
    // 的 DedeUserID 提取同型;缺席返回空串。
    static QString tryGetBiliJct(const QString &cookie);
};

#endif  // HEARTBEAT_API_H
