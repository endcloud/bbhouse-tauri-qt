#ifndef ARC_SEARCH_API_H
#define ARC_SEARCH_API_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/BilibiliApiClient.h"

// 空间投稿端点的排序口径(order 参数)
enum class ArcSortOrder {
    Pubdate,
    Click,
    Stow
};

// 空间投稿域模型(契约同原 ArcSearchApi.cs)。UI 无关。
struct ArcVideo {
    qint64 aid = 0;
    QString bvid;
    QString title;
    QString coverUrl;
    qint64 created = 0;       // 投稿时间戳
    qint64 play = 0;          // 播放数
    qint64 review = 0;        // 弹幕数(响应字段 video_review)
    QString lengthText;       // 时长文本 "MM:SS"
    int durationSeconds = 0;  // 同一文本解析出的秒值
};

// tlist 的一个分区档位(计数覆盖全部投稿,与当前 order/tid 过滤无关)
struct ArcPartition {
    qint64 tid = 0;
    QString name;
    qint64 count = 0;
};

// 投稿单页 + 分区索引 + 投稿总数
struct ArcSearchPage {
    QList<ArcVideo> videos;
    QList<ArcPartition> partitions;
    qint64 total = 0;
};

// 空间投稿("投稿")端点。端点参考:b3/bilibili-api-collect/docs/user/space.md
// (GET /x/space/wbi/arc/search)—— 统一层目前唯一必须 WBI 签名的端点,经
// WbiSigner 签名后再并参。响应除视频列表外自带分区索引(tlist),调用方免费
// 拿到分区筛选档位 —— 无需逐视频查询。
class ArcSearchApi {
   public:
    static constexpr int kPageSize = 30;

    static ArcSearchPage fetch(BilibiliApiClient &api, const QString &cookie, qint64 mid,
                               ArcSortOrder order, qint64 tid, int pn);

    static ArcSearchPage parse(const QJsonObject &root);
};

#endif  // ARC_SEARCH_API_H
