#ifndef HISTORY_CONTROLLER_H
#define HISTORY_CONTROLLER_H

#include <atomic>
#include <mutex>

#include <QObject>
#include <QThreadPool>
#include <QString>
#include <QVariantList>

#include "core/HistoryStore.h"

// QML 桥接:本地历史分页装载 + 同步编排(HistorySyncRunner 驱动)。
// 数据层为阻塞式 API,一律经控制器所属 QThreadPool 执行,结果以
// QueuedConnection 回投主线程再发信号(约定见 doc/qt-migration-notes.md)。
class HistoryController : public QObject {
    Q_OBJECT
    // 后台 store 初始化完成(启动即在线程池预热)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadingChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString syncStatus READ syncStatus NOTIFY syncStatusChanged)
    // 本地库计数(状态栏"共 N 视频 / M 观看记录";随每次 pageLoaded 刷新)
    Q_PROPERTY(int videoCount READ videoCount NOTIFY countsChanged)
    Q_PROPERTY(int recordCount READ recordCount NOTIFY countsChanged)
    // 最近一次请求的页码(loadPage 调用时即更新,不等加载完成);页面渲染
    // 释放/重建间据此恢复,避免重建时误回第一页(不参与业务逻辑)
    Q_PROPERTY(int lastRequestedPage READ lastRequestedPage NOTIFY lastRequestedPageChanged)
    // 已提交的搜索词(标题栏搜索投影);页面渲染释放/重建间由此保留,零网络请求
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
   public:
    explicit HistoryController(QObject *parent = nullptr);
    ~HistoryController() override;

    bool ready() const;
    bool loading() const { return loading_; }
    QString loadError() const { return loadError_; }
    bool syncing() const;
    QString syncStatus() const;
    int videoCount() const;
    int recordCount() const;
    int lastRequestedPage() const { return lastRequestedPage_; }
    QString searchText() const { return searchText_; }
    void setSearchText(const QString &value);

    // page 从 1 起,30 条/页;完成后 pageLoaded(page, items, total)(主线程)
    Q_INVOKABLE void loadPage(int page);

    // cookie 缺失/为空/无 SESSDATA 等预检失败经 syncFailed 报给 UI
    Q_INVOKABLE void startSync();
    Q_INVOKABLE void cancelSync();

    // 封面原图下载(封面预览右键"下载原图"):线程池 GET,带 Referer 与浏览器
    // UA,保存到 系统图片目录/bilibili_cover/<URL 文件名>;成败均回
    // coverDownloadFinished(savedPath)(空串 = 失败,主线程)
    Q_INVOKABLE void coverDownload(const QString &url);

   signals:
    void readyChanged();
    void loadingChanged();
    void loadFailed(QString message);
    void syncingChanged(bool syncing);
    void syncStatusChanged(QString status);
    void syncFinished(QString resultSummaryText);
    void syncFailed(QString errorMessage);
    void pageLoaded(int page, QVariantList items, int total);
    void countsChanged();
    void coverDownloadFinished(QString savedPath);
    void lastRequestedPageChanged();
    void searchTextChanged();

   private:
    QThreadPool workerPool_;
    // 任意工作线程调用;成功后跳过，异常后允许下一次请求重试。
    void ensureStoreReady();
    static QVariantList toVariantList(const QList<HistoryItem> &items);

    bool loading_ = false;
    QString loadError_;
    quint64 loadGeneration_ = 0;
    HistoryStore store_;
    std::mutex storeInitMutex_;
    std::atomic_bool storeReady_{false};
    std::atomic_bool cancelFlag_{false};
    std::atomic_bool syncing_{false};
    QString syncStatus_;  // 仅主线程读写
    int videoCount_ = 0;  // 仅主线程读写(queued 回投后赋值)
    int recordCount_ = 0;
    int lastRequestedPage_ = 1;  // 主线程,纯 UI 状态(渲染释放前记忆用)
    QString searchText_;         // 主线程,纯 UI 状态(渲染释放前记忆用)
};

#endif  // HISTORY_CONTROLLER_H
