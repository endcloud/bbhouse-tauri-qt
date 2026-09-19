#ifndef LIVE_API_H
#define LIVE_API_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

class BilibiliApiClient;

struct LiveRoom {
    QString roomId;
    QString mid;
    QString title;
    QString uname;
    QString face;
    QString cover;
    QString area;
    QString online;
};

struct LiveFollowPage {
    QList<LiveRoom> rooms;
    bool hasMore = false;
};

struct LivePlayInfo {
    QString roomId;
    int liveStatus = 0;
    int currentQn = 0;
    QVariantList qualities;  // QVariantMap {qn: int, label: QString}
    QStringList urls;       // 同一实际清晰度的完整音视频流；只在内存中使用
};

// 直播 API 独立于 UGC/PGC；仅使用直连的共享 BilibiliApiClient。
class LiveApi {
public:
    // 关注接口每页最多 10 人；服务端页包含未开播用户，rooms 仅保留 live_status=1。
    // 因此 rooms 为空时仍需检查 hasMore，不能提前结束全量加载。
    static LiveFollowPage fetchFollowed(BilibiliApiClient &client, const QString &cookie, int page);
    static LivePlayInfo fetchPlayInfo(BilibiliApiClient &client, const QString &cookie,
                                      const QString &roomId, int qn = 10000);
    static LiveFollowPage parseFollowed(const QJsonObject &root, int page = 1);
    // 离线解析完整 API 信封；未开播、轮播、锁定和不可播放响应抛 ApiError。
    static LivePlayInfo parsePlayInfo(const QJsonObject &root, int preferredQn = 10000);
};

#endif
