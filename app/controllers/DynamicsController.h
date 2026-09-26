#ifndef DYNAMICS_CONTROLLER_H
#define DYNAMICS_CONTROLLER_H

#include <atomic>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include "core/DynamicFeedModels.h"
#include "controllers/DynamicCardModel.h"

// QML 桥接:关注动态流(DynamicApi feed/all offset 翻页,只读,不落库)。
// 线程约定同 OnlineHistoryController(doc/qt-migration-notes.md):阻塞式 API 经
// QThreadPool 全局线程池执行,结果以 QueuedConnection 回投主线程再发信号。
//
// 池+投影:加载池存全量卡片视图(QVariantMap,含 duplicateCount 等视图字段),
// 分类∩分区∩搜索的投影由 reproject 在主线程算出；cardModel 增量驱动卡片，
// itemsChanged 供空态/续载判定，zoneNames 独立更新分区按钮（切档回顶）。
// 六档筛选值与分区/搜索词由 QML 写入(档位持久化在 QML 侧)。
//
// 续载状态机:单轮 loadMore 在工作线程循环拉取,直至当前筛选下新增 >= 24 卡 /
// has_more=false / 单轮 6 页上限;busy 为刷新与续载互斥的单一闸门,generation 使
// 刷新后在途回应作废。刷新保留既有池(不闪空白),成功一轮后整体重建。
//
// aid 去重:视频类条目按 av 号合并(转发条目取原动态 av 号),卡片内容取组内
// 发布最早条目(并列取先入池),池按 pubTs 降序稳定排序保持动态流自然位置;
// 非视频按动态 id 独立呈现。出现次数与记录(时间/作者/投稿或转发)随卡片下发,
// 供角标 tooltip。
//
// 分区补查:视频档下 zoneName 未解析的唯一视频经 250ms 串行队列逐个
// VideoZoneApi::fetchViewByAid,分区名经 VideoZones 静态表解析(顶级分区),
// QHash 会话缓存(刷新不清,同一 av 至多一次请求);失败按"未知分区"参与
// 筛选,无重试风暴。待解析清空前 zoneGateActive 为真,QML 据此暂缓空投影
// "未找到匹配"终态与自动续拉判定。
class DynamicsController : public QObject {
    Q_OBJECT
    // 任一页请求进行中(首页/续载共用;QML 据此禁用刷新与防重复续载)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // 云端到底(has_more=false;刷新复位)
    Q_PROPERTY(bool ended READ ended NOTIFY endedChanged)
    // 最近一次失败为登录失效(-101);下一次成功或刷新即清除
    Q_PROPERTY(bool unauthorized READ unauthorized NOTIFY unauthorizedChanged)
    // 加载池全量(去重后;QML 消费"已载 N 条"与分区名集合)
    Q_PROPERTY(QVariantList pool READ pool NOTIFY poolChanged)
    // 当前筛选(分类∩分区∩搜索)下的投影;筛选/池/分区解析变化即重算
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QAbstractItemModel *cardModel READ cardModel CONSTANT)
    Q_PROPERTY(QStringList zoneNames READ zoneNames NOTIFY zoneNamesChanged)
    // 六档分类筛选:"video"/"pgc"/"live"/"article"/"post"/"other"(QML 写入)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter
                       NOTIFY categoryFilterChanged)
    // 视频分区筛选("" = 全部分区;离开视频档清空由 QML 承担)
    Q_PROPERTY(QString zoneFilter READ zoneFilter WRITE setZoneFilter NOTIFY
                       zoneFilterChanged)
    // 搜索词(标题/UP 主不分大小写子串;QML 注入,仅内存投影零网络)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY
                       searchTextChanged)
    // 分区解析未决(视频档下仍有待补查视频);QML 据此门控空匹配终态
    Q_PROPERTY(bool zoneGateActive READ zoneGateActive NOTIFY zoneGateActiveChanged)
    // 页面渲染释放前记忆的滚动位置,重建后据此恢复(不参与业务逻辑)
    Q_PROPERTY(qreal scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollOffsetChanged)
    Q_PROPERTY(QVariantMap scrollAnchor MEMBER scrollAnchor_ NOTIFY scrollAnchorChanged)
   public:
    explicit DynamicsController(QObject *parent = nullptr);
    Q_INVOKABLE void releasePageCache();

    bool busy() const;
    bool ended() const;
    bool unauthorized() const;
    QVariantList pool() const;
    QVariantList items() const;
    QAbstractItemModel *cardModel() { return &cardModel_; }
    QStringList zoneNames() const { return zoneNames_; }
    QString categoryFilter() const;
    QString zoneFilter() const;
    QString searchText() const;
    bool zoneGateActive() const;

    void setCategoryFilter(const QString &value);
    void setZoneFilter(const QString &value);
    void setSearchText(const QString &value);
    qreal scrollOffset() const;
    void setScrollOffset(qreal value);

    // 重置 offset 与去重分组重拉首页(既有池保留至新数据到达,筛选档位不变;
    // busy 期间重复触发在此兜底)
    Q_INVOKABLE void refresh();
    // 自保留 offset 续载(触底 / 空投影自动续链;互斥与到底态在此兜底)
    Q_INVOKABLE void loadMore();

   signals:
    void busyChanged();
    void endedChanged();
    void unauthorizedChanged();
    void poolChanged();
    void itemsAboutToChange();
    void itemsChanged();
    void zoneNamesChanged();
    void categoryFilterChanged();
    void zoneFilterChanged();
    void searchTextChanged();
    void zoneGateActiveChanged();
    void scrollOffsetChanged();
    void scrollAnchorChanged();
    // 登录失效/网络失败等(message 为本地化文案,QML 经 InfoBar 呈现;已加载内容保留)
    void loadFailed(QString message);

   private:
    friend class DynamicsControllerTest; // Offline feed/view fixtures; no live API requests.
    // 同一 av 号的出现记录(角标 tooltip:时间/作者/投稿或转发)
    struct Occurrence {
        qint64 pubTs = 0;
        QString authorName;
        bool isForward = false;
    };

    // 工作线程循环携带的筛选快照(主线程读取,循环期间不随 UI 变化)
    struct FilterSnapshot {
        QString category;
        QString zone;
        QString search;  // 已转小写
    };

    static QString categoryToString(DynamicCategory category);
    // 工作线程侧的条目匹配(续载计数):新拉条目分区未解析时对分区档乐观计入,
    // 解析完成后由主线程投影精确过滤
    static bool matchesFeedItem(const DynamicFeedItem &item, const FilterSnapshot &filter);

    int findPoolIndexByAid(qint64 aid) const;
    void startLoad();
    void finishLoad(int generation, const QList<DynamicFeedItem> &collected,
                    const QString &nextOffset, bool hasMore, const QString &error,
                    bool unauthorized);
    void mergeItems(const QList<DynamicFeedItem> &collected);
    void decorateDuplicates();
    void reproject();
    void updateZoneNames();
    bool matchesFilter(const QVariantMap &item, const FilterSnapshot &filter) const;
    void queueZoneLookups();
    void updateZoneGate();
    void pumpZoneQueue();
    void applyZoneResult(qint64 aid, const QString &zoneName);
    void setEnded();
    // 按首次追加顺序淘汰,与展示用 pubTs 排序及代表条目替换解耦。
    void trimPool();
    static QVariantMap toItemMap(const DynamicFeedItem &item);

    int generation_ = 0;         // 刷新递增,使在途回应作废(主线程读写)
    bool ended_ = false;         // 主线程
    bool unauthorized_ = false;  // 主线程
    bool refreshPending_ = false;  // 刷新后未成功重建池(成功一轮即整体替换)
    QString offset_;             // 下一页 offset(空 = 首页;主线程)
    quint64 nextPoolOrder_ = 0;
    QVariantMap scrollAnchor_;
    QVariantList pool_;          // 去重后全量池(pubTs 降序;主线程)
    QVariantList items_;         // 当前筛选投影(主线程)
    DynamicCardModel cardModel_;
    QStringList zoneNames_;
    QSet<QString> poolIds_;      // 动态 id 去重(非视频条目独立呈现)
    QSet<qint64> poolAids_;      // 已入池 av 号(代表条目在池中唯一)
    QHash<qint64, QList<Occurrence>> aidOccurrences_;  // av 号 → 出现记录(入池序)

    QString categoryFilter_ = QStringLiteral("video");
    QString zoneFilter_;
    QString searchText_;   // 原始输入(展示用)
    QString searchLower_;  // 预转小写,投影与续载计数共用

    // 分区补查:会话缓存 + 250ms 串行队列(仅视频档排空;非视频档不发请求)
    QHash<qint64, QString> zoneCache_;
    QList<qint64> pendingZoneAids_;
    quint64 zoneGeneration_ = 0;
    bool zoneInFlight_ = false;
    bool zoneGateActive_ = false;
    QTimer zoneTimer_;

    qreal scrollOffset_ = 0;  // 页面渲染释放前记忆的滚动位置(主线程,纯 UI 状态)

    std::atomic_bool busy_{false};
};

#endif  // DYNAMICS_CONTROLLER_H
