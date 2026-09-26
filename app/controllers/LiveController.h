#ifndef LIVE_CONTROLLER_H
#define LIVE_CONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariantList>

#include "core/LiveApi.h"

// 关注直播列表的会话缓存；刷新仅在新快照成功时替换旧卡片。
class LiveController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList pool READ pool NOTIFY poolChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(bool unauthorized READ unauthorized NOTIFY unauthorizedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // 搜索词记忆(渲染释放时保留,重建后回显)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    // 滚动位置记忆(渲染释放时保留,重建后回显;规格明确要求恢复)
    Q_PROPERTY(double scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollOffsetChanged)
public:
    explicit LiveController(QObject *parent = nullptr);
    Q_INVOKABLE void releasePageCache();
    QVariantList pool() const { return pool_; }
    bool busy() const { return busy_; }
    bool loaded() const { return loaded_; }
    bool hasMore() const { return hasMore_; }
    bool unauthorized() const { return unauthorized_; }
    QString error() const { return error_; }
    QString searchText() const { return searchText_; }
    void setSearchText(const QString &value);
    double scrollOffset() const { return scrollOffset_; }
    void setScrollOffset(double value);

    Q_INVOKABLE void ensureLoaded();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore();

signals:
    void poolChanged();
    void busyChanged();
    void loadedChanged();
    void hasMoreChanged();
    void unauthorizedChanged();
    void errorChanged();
    void searchTextChanged();
    void scrollOffsetChanged();

private:
    friend class LiveControllerTest;
    static constexpr int kMaxPagesPerOperation = 100;
    void beginFetch(bool replace);
    virtual void startFetch(quint64 generation, int page);
    void finishFetch(quint64 generation, int page, const LiveFollowPage &result,
                     const QString &error, bool unauthorized);
    static QVariantMap toItemMap(const LiveRoom &room);

    QVariantList pool_;
    QString error_;
    QString requestCookie_;
    bool cookieCaptured_ = false;
    bool busy_ = false;
    bool loaded_ = false;
    bool hasMore_ = true;
    bool unauthorized_ = false;
    bool replace_ = false;
    int nextPage_ = 1;
    int pendingPage_ = 1;
    int scannedPages_ = 0;
    quint64 generation_ = 0;
    QString searchText_;      // 主线程,纯 UI 状态(渲染释放前记忆用)
    double scrollOffset_ = 0; // 主线程,纯 UI 状态(渲染释放前记忆用)
};

#endif
