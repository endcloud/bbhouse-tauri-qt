#ifndef BANGUMI_CONTROLLER_H
#define BANGUMI_CONTROLLER_H

#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/BangumiApi.h"

// QML 桥接:追番追剧页(bangumi-ui)。线程约定同 WatchlaterController
// (doc/qt-migration-notes.md):阻塞式 API 经 QThreadPool 执行,结果以
// QueuedConnection 回投主线程。
//
// 常规两桶语义(见 BangumiApi 类注释):服务端 type 仅区分 1(追番)与 2(追剧),
// 桶内混合多种 season_type,四档(番剧/国创/影视/纪录片)是 QML 侧对桶页的
// 客户端投影 —— 同桶切档零网络。桶数据记忆当前页(页码/条目/总数):番剧与
// 国创共享桶 1 页码,影视与纪录片共享桶 2 页码;换桶未装载时拉该桶第 1 页。
// 港澳台为独立桶 3：直连扫描全部追番后，搜索并本地分页；地区详情单独代理。
// 页数据不落库。失败保留既有桶页(刷新保旧卡)。
class BangumiController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // 最近一次失败为登录失效(-101);下一次成功或刷新即清除
    Q_PROPERTY(bool unauthorized READ unauthorized NOTIFY unauthorizedChanged)
    // 当前桶当前页全量条目(响应原序、season_id 去重;未按档位过滤,
    // QML 按四档 + 搜索词做纯内存投影)
    Q_PROPERTY(QVariantList pageItems READ pageItems NOTIFY pageItemsChanged)
    // 当前桶页码(1 起)与桶内总数(翻页上界)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY pageInfoChanged)
    Q_PROPERTY(qint64 currentTotal READ currentTotal NOTIFY pageInfoChanged)
    Q_PROPERTY(int currentBucket READ currentBucket NOTIFY currentBucketChanged)
   public:
    explicit BangumiController(QObject *parent = nullptr);

    bool busy() const;
    bool unauthorized() const;
    QVariantList pageItems() const;
    int currentPage() const;
    qint64 currentTotal() const;
    int currentBucket() const;

    // 换桶(1=追番,2=追剧,3=港澳台;其余值忽略):目标桶已记忆页码 → 纯切换零网络;
    // 未装载 → 拉第 1 页(忙碌期间排队,在途请求完成后自动补拉)
    Q_INVOKABLE void setBucket(int bucket);
    // 当前桶内标准服务端分页(page 从 1 起,30 条/页)
    Q_INVOKABLE void loadPage(int page);
    // 重拉当前桶当前页(保旧卡整体替换;并重读当前生效凭据源)
    Q_INVOKABLE void refresh();
    // 当前桶未装载时拉第 1 页(页面 Component.onCompleted 调用;切页返回不重拉)
    Q_INVOKABLE void ensureCurrentBucketLoaded();

    // 剧集详情(season → 全分集 + 云端观看位置):命中会话缓存同步返回该表;
    // 未命中起线程池拉取并回空表,完成后经 seasonDetailReady 回投(失败
    // errorOccurred,不影响页面浏览)。
    Q_INVOKABLE QVariantMap seasonDetail(qint64 seasonId, bool regional = false);
    Q_INVOKABLE void setRegionalSearch(const QString &query);

   signals:
    void busyChanged();
    void unauthorizedChanged();
    void pageItemsChanged();
    void pageInfoChanged();
    void currentBucketChanged();
    // seasonDetail 未命中缓存时的异步结果(seasonId/title/episodes[epId,cid,
    // title,longTitle,duration,badge]/lastEpId/lastTimeSeconds)
    void seasonDetailReady(QVariantMap detail);
    // 分页拉取失败:登录失效/网络等(message 本地化文案;旧卡片保留可重试)
    void loadFailed(QString message);
    // 剧集详情拉取失败(仅提示,不影响页面浏览与分页状态)
    void errorOccurred(QString message);

   private:
    friend class RegionalBangumiTest;
    struct BucketState {
        int page = 0;        // 0 = 未装载
        qint64 total = 0;
        QVariantList items;  // 当前页全量条目
    };

    virtual void startFetch(int bucket, int page);
    void finishFetch(int bucket, int page, const QVariantList &items, qint64 total,
                     const QString &error, bool unauthorized);
    virtual void startSeasonFetch();
    void projectRegionalPage(int page);
    static QVariantMap toItemMap(const BangumiApi::BangumiSeason &season);

    struct SeasonRequest {
        qint64 id = 0;
        bool regional = false;
        quint64 generation = 0;
        QNetworkProxy proxy;
    };
    void finishSeasonFetch(const SeasonRequest &request, const QVariantMap &detail,
                           const QString &error);
    BucketState buckets_[3];     // [0]=追番 [1]=追剧 [2]=港澳台；仅主线程
    QList<BangumiApi::BangumiSeason> regionalItems_;
    QString regionalSearch_;
    int currentBucket_ = 1;
    QHash<QString, QVariantMap> seasonCache_;  // regional + seasonId，代理变更时清空
    bool unauthorized_ = false;
    bool busy_ = false;
    bool seasonBusy_ = false;
    quint64 seasonGeneration_ = 0;
    std::optional<SeasonRequest> queuedSeason_;
    SeasonRequest latestSeason_;
};

#endif  // BANGUMI_CONTROLLER_H
