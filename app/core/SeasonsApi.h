#ifndef SEASONS_API_H
#define SEASONS_API_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/BilibiliApiClient.h"

// 合集/系列域模型(契约同原 SeasonApi.cs)。UI 无关。
struct SeasonSummary {
    qint64 id = 0;
    bool isSeries = false;  // false = 合集(创作者中心合集),true = 系列(空间级列表)
    QString name;
    QString coverUrl;
    qint64 total = 0;  // 合集/系列内视频总数
};

// 合集/系列内的一个视频,面向播放列表构建
struct SeasonArchive {
    qint64 aid = 0;
    QString bvid;
    QString title;
    QString coverUrl;
    int durationSeconds = 0;  // 此端点以秒到达(与 arc/search 的 "MM:SS" 文本不同)
    qint64 pubTs = 0;         // 投稿时间戳
};

// 合并后的合集+系列列表单页。pageTotal 是响应的 page.total —— 文档标注它为
// "总页数",但实测是总条数(2026-09-03 验证:32 个合集的 up 第 1 页
// page_size=20 返回 20 条);单页返回条数少于请求条数时翻页结束。
struct SeasonsSeriesPage {
    QList<SeasonSummary> items;
    qint64 pageTotal = 0;
};

// 合集/系列内视频单页:累计条数到达 total 或某页返回空时翻页结束
struct SeasonArchivesPage {
    QList<SeasonArchive> archives;
    qint64 total = 0;
};

// UGC 合集/系列("合集和系列")域端点。端点参考:
// b3/bilibili-api-collect/docs/video/collection.md(无 WBI;期望浏览器 UA)。
// 合集(创作者中心合集)与系列(空间级列表)在同一次响应中返回,此处合并为
// 一个展示列表,以 SeasonSummary::isSeries 区分。
class SeasonsApi {
   public:
    // seasons/series list 端点的服务端 page_size 上限;更大的请求以
    // 空 data 的 -400 信封失败
    static constexpr int kMaxPageSize = 20;

    // 取 mid 的一个合并合集+系列页。pageSize 钳到 kMaxPageSize —— 端点拒绝
    // 任何超过 20 的值,以 data=null 的 -400 响应失败(文档说"默认 20"但没说
    // 它同时是上限;实测 2026-09-07:21/30 → data null,19/20 → 正常)。
    static SeasonsSeriesPage fetchSeasonsSeriesList(BilibiliApiClient &api, const QString &cookie,
                                                    qint64 mid, int pageNum, int pageSize);

    // 取合集(season)的一页视频。
    static SeasonArchivesPage fetchSeasonArchives(BilibiliApiClient &api, const QString &cookie,
                                                  qint64 mid, qint64 seasonId, int pageNum);

    // 取系列(series)的一页视频。参数名与合集端点不同(pn/ps)—— 在此归一化。
    static SeasonArchivesPage fetchSeriesArchives(BilibiliApiClient &api, const QString &cookie,
                                                  qint64 mid, qint64 seriesId, int pn);

    static SeasonArchivesPage parseArchives(const BilibiliApiClient::Envelope &envelope);
    static void appendSeasonList(QList<SeasonSummary> *items, const QJsonArray &list,
                                 bool isSeries);
    static qint64 parseTimestamp(const QJsonObject &element);
};

#endif  // SEASONS_API_H
