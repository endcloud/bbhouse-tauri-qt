#ifndef WATCHLATER_CONTROLLER_H
#define WATCHLATER_CONTROLLER_H

#include <atomic>

#include <QObject>
#include <QString>
#include <QVariantList>

#include "core/HistoryModels.h"

// QML 桥接:稍后再看(ToviewApi 单次全量,只读,不落库)。
// 线程约定同 HistoryController(doc/qt-migration-notes.md):阻塞式 API 经
// QThreadPool 执行,结果以 QueuedConnection 回投主线程。
//
// 全量语义:端点一次性返回完整快照,无游标/续载;刷新期间保留既有池,
// 新数据到达时整体替换(不闪空白);失败保留旧池。每条目附 invalid(失效
// 标记,state<0)与 epId/cid(PGC 分集定位,与播放器免分 P 形态同构),
// 占位标题等 UI 呈现由 QML 侧按本地化文案处理。
class WatchlaterController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // 最近一次失败为登录失效(-101);下一次成功或刷新即清除
    Q_PROPERTY(bool unauthorized READ unauthorized NOTIFY unauthorizedChanged)
    // 已完成至少一次全量装载(此前为未装载态;刷新不改变该标志)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    // 全量池(响应原序 = add_at 降序;QML 按当前搜索词做纯内存投影)
    Q_PROPERTY(QVariantList pool READ pool NOTIFY poolChanged)
    // 搜索词记忆(渲染释放时保留,重建后回显)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    // 页码记忆(渲染释放时保留,重建后回显)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY pageIndexChanged)
   public:
    explicit WatchlaterController(QObject *parent = nullptr);
    Q_INVOKABLE void releasePageCache();

    bool busy() const;
    bool unauthorized() const;
    bool loaded() const;
    QVariantList pool() const;
    QString searchText() const { return searchText_; }
    void setSearchText(const QString &value);
    int pageIndex() const { return pageIndex_; }
    void setPageIndex(int value);

    // 重新发起一次全量请求(busy 期间重复触发在此兜底);既有池保留至替换
    Q_INVOKABLE void refresh();

   signals:
    void busyChanged();
    void unauthorizedChanged();
    void loadedChanged();
    void poolChanged();
    void searchTextChanged();
    void pageIndexChanged();
    // 登录失效/网络失败等(message 为本地化文案,QML 经 InfoBar 呈现;旧卡片保留)
    void loadFailed(QString message);

   private:
    static QVariantMap toItemMap(const HistoryItem &item);

    quint64 generation_ = 0;
    QVariantList pool_;          // 全量池(主线程;刷新期间不动,成功后整体替换)
    bool unauthorized_ = false;  // 主线程
    bool loaded_ = false;        // 主线程
    QString searchText_;         // 主线程,纯 UI 状态(渲染释放前记忆用)
    int pageIndex_ = 1;          // 主线程,纯 UI 状态(渲染释放前记忆用)
    std::atomic_bool busy_{false};
};

#endif  // WATCHLATER_CONTROLLER_H
