#ifndef SPECIAL_FOLLOW_CONTROLLER_H
#define SPECIAL_FOLLOW_CONTROLLER_H

#include <atomic>
#include <memory>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QQueue>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "controllers/SpecialFollowStore.h"

// QML 桥接:特别关注页(special-follow-ui)。线程约定同 BangumiController
// (doc/qt-migration-notes.md):阻塞式 API 经 QThreadPool 执行,结果以
// QueuedConnection 回投主线程。
//
// 本地 UP 列表(快照文件 + 一次性种子导入)+ 单 UP 三档会话缓存:
// 投稿(ArcSearchApi 服务端 30/页 + 分区/排序)、合集(SeasonsApi 服务端 20/页,
// page_size 超限端点以 -400 拒绝)、专栏(ArticleApi 一次全量,客户端 30/页切片)。
// 每 UP 一份会话缓存,切头像/切页返回不重拉;失败保留旧卡(刷新保旧卡语义)。
class SpecialFollowController : public QObject {
    Q_OBJECT
    // UP 快照装载完成(含种子失败后的空表;页面骨架此后立即可交互)
    Q_PROPERTY(bool localFollowBusy READ localFollowBusy NOTIFY localFollowBusyChanged)
    Q_PROPERTY(bool upsReady READ upsReady NOTIFY upsReadyChanged)
    // 本地 UP 列表(mid/name/faceUrl/addedAt;呈现顺序即保存顺序)
    Q_PROPERTY(QVariantList ups READ ups NOTIFY upsChanged)
    Q_PROPERTY(qint64 currentMid READ currentMid NOTIFY currentMidChanged)
    // 最近一次失败为登录失效(-101);下一次成功或刷新即清除
    Q_PROPERTY(bool unauthorized READ unauthorized NOTIFY unauthorizedChanged)

    // ---- 投稿档(当前 UP;服务端 30/页) ----
    Q_PROPERTY(QVariantList arcItems READ arcItems NOTIFY arcItemsChanged)
    // 分区索引(响应 tlist;计数覆盖全部投稿)"全部分区"档由 QML 以 tid=0 呈现
    Q_PROPERTY(QVariantList arcPartitions READ arcPartitions NOTIFY arcItemsChanged)
    Q_PROPERTY(int arcPage READ arcPage NOTIFY arcInfoChanged)
    Q_PROPERTY(qint64 arcTotal READ arcTotal NOTIFY arcInfoChanged)
    // 排序 0=最新发布(pubdate) 1=最多播放(click) 2=最多收藏(stow);tid=0 全部
    Q_PROPERTY(int arcOrder READ arcOrder NOTIFY arcCriteriaChanged)
    Q_PROPERTY(qint64 arcTid READ arcTid NOTIFY arcCriteriaChanged)
    Q_PROPERTY(bool arcBusy READ arcBusy NOTIFY arcBusyChanged)

    // ---- 合集档(当前 UP;服务端 20/页,合集+系列合并列表) ----
    Q_PROPERTY(QVariantList seasonItems READ seasonItems NOTIFY seasonItemsChanged)
    Q_PROPERTY(int seasonPage READ seasonPage NOTIFY seasonInfoChanged)
    Q_PROPERTY(qint64 seasonTotal READ seasonTotal NOTIFY seasonInfoChanged)
    Q_PROPERTY(bool seasonBusy READ seasonBusy NOTIFY seasonBusyChanged)
    // 拉全合集视频的进行中指示(播放全部/展开共用)
    Q_PROPERTY(bool seasonVideosBusy READ seasonVideosBusy NOTIFY seasonVideosBusyChanged)

    // ---- 专栏档(当前 UP;一次全量,客户端 30/页切片) ----
    Q_PROPERTY(QVariantList articleItems READ articleItems NOTIFY articleInfoChanged)
    Q_PROPERTY(int articlePage READ articlePage NOTIFY articleInfoChanged)
    Q_PROPERTY(qint64 articleTotal READ articleTotal NOTIFY articleInfoChanged)
    Q_PROPERTY(bool articleBusy READ articleBusy NOTIFY articleBusyChanged)

    // ---- 管理弹层(浏览/搜索两模式 + 跨模式勾选池) ----
    Q_PROPERTY(bool manageBusy READ manageBusy NOTIFY manageBusyChanged)
    // 最近一次弹层拉取/搜索失败文案(空串=无;"加载更多"兼做重试)
    Q_PROPERTY(QString manageError READ manageError NOTIFY manageErrorChanged)
    // 浏览模式:已装载的各页关注成员(追加累积)
    Q_PROPERTY(QVariantList manageMembers READ manageMembers NOTIFY manageBrowseChanged)
    Q_PROPERTY(int manageBrowsePage READ manageBrowsePage NOTIFY manageBrowseChanged)
    Q_PROPERTY(qint64 manageBrowseTotal READ manageBrowseTotal NOTIFY manageBrowseChanged)
    // 搜索模式:关键词非空时生效;结果整体替换(每页)
    Q_PROPERTY(QVariantList manageSearchResults READ manageSearchResults
                       NOTIFY manageResultsChanged)
    Q_PROPERTY(int manageSearchPage READ manageSearchPage NOTIFY manageResultsChanged)
    Q_PROPERTY(qint64 manageSearchTotal READ manageSearchTotal NOTIFY manageResultsChanged)
    Q_PROPERTY(bool manageSearchMode READ manageSearchMode NOTIFY manageResultsChanged)
    // 页面档位记忆(0=投稿 1=合集 2=专栏;渲染释放时保留,重建后回显)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)
    // 搜索词记忆(渲染释放时保留,重建后回显)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)

   public:
    explicit SpecialFollowController(QObject *parent = nullptr);
    Q_INVOKABLE virtual void releasePageCache();

    bool localFollowBusy() const { return saveBusy_ || !saveQueue_.isEmpty(); }
    bool containsUp(qint64 mid) const;
    Q_INVOKABLE bool containsUp(const QString &mid) const { return containsUp(mid.toLongLong()); }
    // 仅装载本地列表，不从服务器播种；供空间页关注状态使用。
    Q_INVOKABLE void ensureLocalReady();
    void setUpFollowed(qint64 mid, const QString &name, const QString &face, bool followed);
    Q_INVOKABLE void setUpFollowed(const QString &mid, const QString &name, const QString &face, bool followed) {
        setUpFollowed(mid.toLongLong(), name, face, followed);
    }
    bool upsReady() const;
    QVariantList ups() const;
    qint64 currentMid() const;
    bool unauthorized() const;

    QVariantList arcItems() const;
    QVariantList arcPartitions() const;
    int arcPage() const;
    qint64 arcTotal() const;
    int arcOrder() const;
    qint64 arcTid() const;
    bool arcBusy() const;

    QVariantList seasonItems() const;
    int seasonPage() const;
    qint64 seasonTotal() const;
    bool seasonBusy() const;
    bool seasonVideosBusy() const;

    QVariantList articleItems() const;
    int articlePage() const;
    qint64 articleTotal() const;
    bool articleBusy() const;

    bool manageBusy() const;
    QString manageError() const;
    QVariantList manageMembers() const;
    int manageBrowsePage() const;
    qint64 manageBrowseTotal() const;
    QVariantList manageSearchResults() const;
    int manageSearchPage() const;
    qint64 manageSearchTotal() const;
    bool manageSearchMode() const;
    int currentTab() const { return currentTab_; }
    void setCurrentTab(int value);
    QString searchText() const { return searchText_; }
    void setSearchText(const QString &value);

    // 首次进入装载本地快照;文件不存在时以特别关注分组(tagid=-10)一次性种子
    // 导入。幂等:装载完成后重复调用零开销;失败以 seedFailed 提示,骨架可交互。
    Q_INVOKABLE void ensureReady();

    // 选中 UP(头像条点击);会话缓存命中零网络,未装载档由 QML 按当前档
    // ensureCurrentTabLoaded 触发首拉
    Q_INVOKABLE void selectUp(qint64 mid);

    // 当前 UP 当前档未装载时首拉(tab:0=投稿 1=合集 2=专栏;切头像/切档调用)
    Q_INVOKABLE void ensureCurrentTabLoaded(int tab);

    // ---- 投稿档 ----
    Q_INVOKABLE void loadArcPage(int page);
    // 切排序/分区:重置第一页并重新拉取(服务端 order/tid 参数)
    Q_INVOKABLE void setArcOrder(int order);
    Q_INVOKABLE void setArcPartition(qint64 tid);
    Q_INVOKABLE void refreshArc();  // 重拉当前页(保旧卡整体替换)

    // ---- 合集档 ----
    Q_INVOKABLE void loadSeasonsPage(int page);
    Q_INVOKABLE void refreshSeasons();
    // 循环翻页拉全该合集/系列的视频;完成经 seasonVideosReady(requestId, entries)
    // 回投,失败经 seasonVideosFailed。requestId 由 QML 生成用于匹配在途请求。
    Q_INVOKABLE void loadSeasonVideos(int requestId, bool isSeries, qint64 id);

    // ---- 专栏档 ----
    Q_INVOKABLE void loadArticles();       // 全量首拉(page 兜底 1)
    Q_INVOKABLE void setArticlePage(int page);  // 本地切片,零网络
    Q_INVOKABLE void refreshArticles();

    // ---- 管理弹层 ----
    // 打开:重置会话勾选池(本地成员全量预勾选)与两模式状态,拉浏览第 1 页
    Q_INVOKABLE void openManage();
    // 浏览模式载下一页 / 搜索模式载下一页(≤5 页);失败后重试同一动作
    Q_INVOKABLE void manageLoadMore();
    // 防抖到期后由 QML 调用;空词回到浏览模式(已装载页保留)
    Q_INVOKABLE void manageSearch(const QString &keyword);
    Q_INVOKABLE bool isMemberChecked(qint64 mid) const;
    // 勾选池变更(跨模式保持;未在本地列表的成员即时捕获昵称/头像)
    Q_INVOKABLE void setMemberChecked(qint64 mid, const QString &name,
                                      const QString &faceUrl, bool checked);
    // 合并式写回:已呈现成员按勾选池,未呈现的既有本地成员原样保留;完成后
    // emit manageSaved(本地列表与头像条即时刷新,选中回落规则同 selectUp)
    Q_INVOKABLE void saveManage();

   signals:
    void localFollowBusyChanged();
    void upsReadyChanged();
    void upsChanged();
    void currentMidChanged();
    void unauthorizedChanged();

    void arcItemsChanged();
    void arcInfoChanged();
    void arcCriteriaChanged();
    void arcBusyChanged();

    void seasonItemsChanged();
    void seasonInfoChanged();
    void seasonBusyChanged();
    void seasonVideosBusyChanged();

    void articleInfoChanged();
    void articleBusyChanged();

    void manageBusyChanged();
    void manageErrorChanged();
    void manageBrowseChanged();
    void manageResultsChanged();
    void currentTabChanged();
    void searchTextChanged();
    // 合并写回成功(弹层关闭由 QML 承担)
    void manageSaved();
    // 分档拉取失败(登录失效/网络等,message 本地化文案;旧卡片保留可刷新重试)
    void loadFailed(QString message);
    // 种子导入失败(空态引导;不阻塞骨架)
    void seedFailed(QString message);
    // 合集视频拉全完成/失败(requestId 匹配 QML 在途请求)
    void seasonVideosReady(int requestId, QVariantList entries);
    void seasonVideosFailed(QString message);

   private:
    friend class UserSpaceControllerTest;
    // 单 UP 会话缓存(仅主线程读写;QML 可见属性全部代理到 currentMid_ 的会话)
    struct ArcState {
        int page = 0;  // 0 = 未装载
        qint64 total = 0;
        int order = 0;      // ArcSortOrder 序数
        qint64 tid = 0;     // 0 = 全部分区
        QVariantList items;
        QVariantList partitions;
    };
    struct SeasonsState {
        int page = 0;
        qint64 total = 0;
        QVariantList items;
    };
    struct ArticlesState {
        int page = 0;          // 本地切片当前页;0 = 未装载
        QVariantList items;    // 全量(端点一次返回)
    };
    struct UpSession {
        ArcState arc;
        SeasonsState seasons;
        ArticlesState articles;
    };

    UpSession *session(qint64 mid);
    void emitTabReset();  // 切头像后刷新全部档位可见属性

    virtual void startArcFetch(qint64 mid, int page);
    void finishArcFetch(qint64 mid, int page, int order, qint64 tid,
                        const QVariantList &items, const QVariantList &partitions,
                        qint64 total, const QString &error, bool unauthorized);
    virtual void startSeasonsFetch(qint64 mid, int page);
    void finishSeasonsFetch(qint64 mid, int page, const QVariantList &items,
                            qint64 total, const QString &error, bool unauthorized);
    virtual void startArticlesFetch(qint64 mid);
    void finishArticlesFetch(qint64 mid, const QVariantList &items,
                             const QString &error, bool unauthorized);
    void startSeedOrLoad();
    void finishUps(const QList<SpecialFollowStore::Entry> &members);

    void startManageFetch(bool searchMode, int page);
    void finishManageFetch(bool searchMode, int page, const QVariantList &users,
                           qint64 total, const QString &error, bool unauthorized);
    void processSaveQueue();
    void drainPendingArc();
    void drainPendingSeasons();
    void drainPendingArticles();
    struct SaveOperation {
        bool management = false;
        qint64 mid = 0;
        QString name;
        QString face;
        bool followed = false;
        QHash<qint64, SpecialFollowStore::Entry> checked;
        QSet<qint64> presented;
    };
    QQueue<SaveOperation> saveQueue_;
    bool saveBusy_ = false;
    qint64 pendingArcMid_ = 0;
    int pendingArcPage_ = 1;
    qint64 pendingSeasonsMid_ = 0;
    int pendingSeasonsPage_ = 1;
    qint64 pendingArticlesMid_ = 0;
    void applyMidFallback();

    std::shared_ptr<SpecialFollowStore> store_ = std::make_shared<SpecialFollowStore>();
    QVariantList ups_;  // 当前本地列表的 QVariantList 投影(仅主线程)
    bool upsReady_ = false;
    qint64 currentMid_ = 0;
    bool unauthorized_ = false;

    quint64 pageGeneration_ = 0;
    QList<qint64> sessionUse_;
    QHash<qint64, UpSession> sessions_;  // mid → 三档会话缓存(仅主线程)

    std::atomic_bool arcBusy_{false};
    std::atomic_bool seasonsBusy_{false};
    std::atomic_bool articlesBusy_{false};
    std::atomic_bool seasonVideosBusy_{false};
    std::atomic_bool seedBusy_{false};
    std::atomic_bool manageBusy_{false};

    // 管理弹层会话状态(仅主线程;openManage 重置)
    QVariantList manageBrowseItems_;
    int manageBrowsePage_ = 0;
    qint64 manageBrowseTotal_ = 0;
    QVariantList manageSearchItems_;
    int manageSearchPage_ = 0;
    qint64 manageSearchTotal_ = 0;
    QString manageKeyword_;
    bool manageSearchMode_ = false;
    QString manageError_;
    QSet<qint64> presentedMids_;                 // 本次弹层会话呈现过的成员
    QHash<qint64, SpecialFollowStore::Entry> checkedInfo_;  // 勾选池(勾选时捕获资料)
    int currentTab_ = 0;        // 主线程,纯 UI 状态(渲染释放前记忆用)
    QString searchText_;        // 主线程,纯 UI 状态(渲染释放前记忆用)
};

#endif  // SPECIAL_FOLLOW_CONTROLLER_H
