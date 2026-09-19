#ifndef ONLINE_HISTORY_CONTROLLER_H
#define ONLINE_HISTORY_CONTROLLER_H

#include <atomic>

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

#include "core/HistoryModels.h"

// QML 桥接:在线历史(云端 cursor 观看历史,只读,不落库)。
// 线程约定同 HistoryController(doc/qt-migration-notes.md):数据层阻塞式,
// 经 QThreadPool 全局线程池执行,结果以 QueuedConnection 回投主线程再发信号。
//
// 续载状态机:游标翻页 + video_key 去重池;终止信号与 HistorySyncRunner 同口径
// (空页 / 游标缺 business / 游标与上页相同防死循环)。generation 使刷新后
// 在途回应作废;busy 为刷新与续载互斥的单一闸门。
class OnlineHistoryController : public QObject {
    Q_OBJECT
    // 任一页请求进行中(首页/续载共用;QML 据此禁用刷新与防重复续载)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // 云端到底(终止信号命中后续载停止;刷新复位)
    Q_PROPERTY(bool ended READ ended NOTIFY endedChanged)
    // 最近一次失败为登录失效(-101);下一次成功或刷新即清除
    Q_PROPERTY(bool unauthorized READ unauthorized NOTIFY unauthorizedChanged)
    // 已加载去重池(video_key 唯一;QML 按当前搜索词做纯内存投影)
    Q_PROPERTY(QVariantList pool READ pool NOTIFY poolChanged)
   public:
    explicit OnlineHistoryController(QObject *parent = nullptr);

    bool busy() const;
    bool ended() const;
    bool unauthorized() const;
    QVariantList pool() const;

    // 重置游标与池,重新拉首页(工具栏刷新按钮)
    Q_INVOKABLE void refresh();
    // 以保留游标续载一页(触底阈值 / 空投影自动续链;互斥与到底态在此兜底)
    Q_INVOKABLE void loadMore();

   signals:
    void busyChanged();
    void endedChanged();
    void unauthorizedChanged();
    void poolChanged();
    // 登录失效/网络失败等(message 为本地化文案,QML 经 InfoBar 呈现;已加载内容保留)
    void loadFailed(QString message);

   private:
    void startFetch(int generation);
    void finishFetch(int generation, const QList<HistoryItem> &items,
                     const HistoryCursor &nextCursor, bool hasCursor, const QString &error,
                     bool unauthorized);
    void setEnded();
    static QVariantMap toItemMap(const HistoryItem &item);
    static QString cursorKey(const HistoryCursor &cursor);

    int generation_ = 0;         // 刷新递增,使在途回应作废(主线程读写)
    bool ended_ = false;         // 主线程
    bool unauthorized_ = false;  // 主线程
    bool hasCursor_ = false;     // 是否已持有上一页游标(主线程)
    HistoryCursor cursor_;       // 下一次请求游标(主线程)
    QString lastCursorKey_;      // 上一页返回游标键(主线程)
    QVariantList pool_;          // 去重后全量池(主线程)
    QSet<QString> poolKeys_;
    std::atomic_bool busy_{false};
};

#endif  // ONLINE_HISTORY_CONTROLLER_H
