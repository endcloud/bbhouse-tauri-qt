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
public:
    explicit LiveController(QObject *parent = nullptr);
    QVariantList pool() const { return pool_; }
    bool busy() const { return busy_; }
    bool loaded() const { return loaded_; }
    bool hasMore() const { return hasMore_; }
    bool unauthorized() const { return unauthorized_; }
    QString error() const { return error_; }

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
};

#endif
