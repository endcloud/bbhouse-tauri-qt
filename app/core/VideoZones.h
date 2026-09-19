#ifndef VIDEO_ZONES_H
#define VIDEO_ZONES_H

#include <QString>

// 静态分区表条目:名称 + 所属顶级分区号与名称(契约同原 VideoZones.cs 的 ZoneEntry)。
struct VideoZoneEntry {
    QString name;
    qint64 topTid = 0;
    QString topName;
};

// 静态视频分区(分区)表:数字分区 id → 显示名 + 所属顶级分区。
// 动态流载荷完全不带分区 id,view 端点只回数字 id(tname/tname_v2 抽样恒空,
// 2026-09-03 验证),名称只能来自这份内嵌表,而非任何运行时分区 API
// (x/web-interface/zone 是 IP 归属地,不是分区列表)。
//
// 来源:b3/bilibili-api-collect/docs/video/video_zone.md(v1,键 = tid)与
// video_zone_v2.md(v2,键 = tid_v2),原表 2026-09-03 由一次性解析器生成。
class VideoZones {
   public:
    // v1 表查询:键 = tid
    static bool findV1(qint64 tid, VideoZoneEntry *entryOut);
    // v2 表查询:键 = tid_v2
    static bool findV2(qint64 tidV2, VideoZoneEntry *entryOut);

    // 单视频分区解析:v1 tid 优先,v1 表没有的 id 回落 tid_v2 查 v2 表。
    // 两者都未命中返回 false("未知分区"语义,不抛错),输出参数置空。
    static bool tryResolve(qint64 tid, qint64 tidV2, QString *nameOut, QString *topNameOut);
};

#endif  // VIDEO_ZONES_H
