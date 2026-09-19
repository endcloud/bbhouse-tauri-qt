#ifndef DYNAMIC_FEED_MODELS_H
#define DYNAMIC_FEED_MODELS_H

#include <QList>
#include <QString>

// 动态域模型(契约同原 DynamicFeedItem.cs)。UI 无关。

// 单条动态的内容分类:由条目的条目级 type + 生效 major type 在客户端判定
// (服务端的 type 过滤参数已废弃,且无法表达"其他")。
enum class DynamicCategory {
    Video,
    Pgc,
    Live,
    Article,
    Post,
    Other
};

// 关注用户动态 feed 的单条条目,精简为卡片所需字段。有意与 HistoryItem 分开:
// 那是持久化契约(video_key / 观看进度语义),动态从不入库。
struct DynamicFeedItem {
    QString id;  // 动态 id(id_str),条目唯一,用于去重

    DynamicCategory category = DynamicCategory::Other;

    QString majorType;  // 分类来源的 major 类型(MAJOR_TYPE_*),条目无 major 对象时为空

    QString title;

    QString summary;  // 次行:视频简介、专栏摘要或动态文本

    QString coverUrl;
    QString authorName;
    QString authorFaceUrl;
    qint64 authorMid = 0;

    qint64 pubTs = 0;  // 发布时间戳(UNIX 秒)

    int durationSeconds = 0;  // 总时长(秒);未知(非视频条目)为 0

    qint64 aid = 0;  // 视频类条目的 av 号;其余为 0

    QString linkUrl;

    bool isUnavailable = false;  // 条目(或转发的原动态)失效/已删除

    bool isForward = false;  // 条目为转发(分类来自原动态)

    QString rawJson;
};

// 动态 feed 的一页:条目 + 翻页信号(offset = 末条 id,has_more)。
struct DynamicFeedPage {
    QList<DynamicFeedItem> items;
    QString offset;
    bool hasMore = false;
    QString rawJson;
};

#endif  // DYNAMIC_FEED_MODELS_H
