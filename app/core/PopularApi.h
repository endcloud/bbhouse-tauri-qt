#ifndef POPULAR_API_H
#define POPULAR_API_H

#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include <QVariantList>

class BilibiliApiClient;

struct PopularPage {
    QVariantList items;
    bool hasMore = false;
    QString description;
};

// 阻塞接口：在控制器的后台任务调用；始终复用显式直连的共享 API 管线。
// 卡片 ID 均为十进制字符串；PGC 榜单可能仅返回 seasonId，交由番剧详情选择剧集。
class PopularApi {
public:
    static PopularPage fetchPopular(BilibiliApiClient &client, const QString &cookie, int page, int pageSize = 20);
    static QVariantList fetchWeeklyPeriods(BilibiliApiClient &client, const QString &cookie);
    static PopularPage fetchWeekly(BilibiliApiClient &client, const QString &cookie, int number);
    static PopularPage fetchPrecious(BilibiliApiClient &client, const QString &cookie);
    static PopularPage fetchRanking(BilibiliApiClient &client, const QString &cookie, int rid);
    static QVariantList fetchMusicPeriods(BilibiliApiClient &client, const QString &cookie);
    static PopularPage fetchMusic(BilibiliApiClient &client, const QString &cookie, const QString &listId);

    // 请求构造与解析是纯函数，供离线契约回归使用。requestUrl 不包含 WBI 签名。
    enum class Kind { Popular, WeeklyPeriods, Weekly, Precious, Ranking, MusicPeriods, Music };
    static QUrl requestUrl(Kind kind, int selection = 0, int pageSize = 20, const QString &listId = {});
    static int rankingSeasonType(int rid);
    static PopularPage parsePopular(const QJsonObject &root, int pageSize = 20);
    static QVariantList parseWeeklyPeriods(const QJsonObject &root);
    static PopularPage parseWeekly(const QJsonObject &root, int expectedNumber = 0);
    static PopularPage parsePrecious(const QJsonObject &root);
    static PopularPage parseRanking(const QJsonObject &root, int rid);
    static QVariantList parseMusicPeriods(const QJsonObject &root);
    static PopularPage parseMusic(const QJsonObject &root);
};
#endif
