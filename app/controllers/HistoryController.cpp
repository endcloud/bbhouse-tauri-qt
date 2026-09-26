#include "core/CardAuthor.h"
#include "core/PlaybackEntry.h"
#include "controllers/HistoryController.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>

#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/HistorySyncRunner.h"

namespace {
constexpr int kPageSize = 30;
// 封面下载请求头口径对齐原 CoverPreviewOverlay(B 站图床校验 Referer)
constexpr const char *kCoverReferer = "https://www.bilibili.com/";
constexpr const char *kCoverUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
}  // namespace

HistoryController::~HistoryController() {
    cancelSync();
    workerPool_.waitForDone();
}

HistoryController::HistoryController(QObject *parent)
    : QObject(parent), store_(AppPaths::dbPath()) {
    workerPool_.setMaxThreadCount(2);
    // 启动即预热:后台线程初始化 SQLite(建表/迁移),UI 侧经 ready 呈现就绪态
    workerPool_.start([this] {
        try {
            ensureStoreReady();
            QMetaObject::invokeMethod(this, [this] { emit readyChanged(); }, Qt::QueuedConnection);
        } catch (const std::exception &e) {
            const QString message = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this, [this, message] {
                if (storeReady_.load()) return;
                loadError_ = message;
                emit loadingChanged();
                emit loadFailed(message);
            }, Qt::QueuedConnection);
        }
    });
}

bool HistoryController::ready() const { return storeReady_.load(); }

bool HistoryController::syncing() const { return syncing_.load(); }

QString HistoryController::syncStatus() const { return syncStatus_; }

int HistoryController::videoCount() const { return videoCount_; }

int HistoryController::recordCount() const { return recordCount_; }

void HistoryController::setSearchText(const QString &value) {
    if (searchText_ == value) return;
    searchText_ = value;
    emit searchTextChanged();
}

void HistoryController::ensureStoreReady() {
    if (storeReady_.load()) return;
    // MinGW 的 once_flag 异常重试在本机线程池上阻塞。用 RAII 锁保护
    // 成功状态，失败时释放锁，后续 loadPage 可以重新初始化。
    std::lock_guard<std::mutex> lock(storeInitMutex_);
    if (storeReady_.load()) return;
    store_.initialize();
    storeReady_.store(true);
}

QVariantList HistoryController::toVariantList(const QList<HistoryItem> &items) {
    QVariantList list;
    list.reserve(items.size());
    for (const HistoryItem &item : items) {
        QVariantMap map;
        map.insert("videoKey", item.videoKey);
        map.insert("title", item.title);
        map.insert("subtitle", item.subtitle);
        map.insert("coverUrl", item.coverUrl);
        map.insert("authorName", item.authorName);
        map.insert("authorMid", QString::number(item.authorMid));
        map.insert("faceUrl", historyAuthorFaceUrl(item));
        map.insert("viewAt", item.viewAt);
        map.insert("progress", item.progress);
        map.insert("duration", item.duration);
        map.insert("badge", item.badge);
        map.insert("linkUrl", item.linkUrl);
        map.insert("viewCount", item.viewCount);
        map.insert("locallyRecorded", true);
        map.insert("rawJson", item.rawJson);
        map.insert("business", item.business);
        map.insert("oid", item.oid);
        map.insert("kid", item.kid);
        // 全部观看事件的 viewAt(Unix 秒,新→旧):多次观看角标 tooltip 逐行列出
        QVariantList viewRecords;
        QVariantList recordedViews;
        viewRecords.reserve(item.viewRecords.size());
        for (const ViewRecord &record : item.viewRecords) {
            viewRecords.append(record.viewAt);
            recordedViews.append(QVariantMap{{"viewAt", record.viewAt}, {"progress", record.progress}});
        }
        map.insert("viewRecords", viewRecords);
        map.insert("recordedViews", recordedViews);
        list.append(PlaybackEntry::normalize(map));
    }
    return list;
}

void HistoryController::loadPage(int page) {
    if (page < 1) return;
    if (lastRequestedPage_ != page) {
        lastRequestedPage_ = page;
        emit lastRequestedPageChanged();
    }
    const quint64 generation = ++loadGeneration_;
    loading_ = true;
    loadError_.clear();
    emit loadingChanged();
    workerPool_.start([this, page, generation] {
        try {
            ensureStoreReady();
            const int total = store_.count();
            const int actualPage = qMin(page, qMax(1, (total + kPageSize - 1) / kPageSize));
            const auto list = toVariantList(store_.loadItems((actualPage - 1) * kPageSize, kPageSize));
            const int records = store_.countRecords();
            QMetaObject::invokeMethod(this, [this, actualPage, list, total, records, generation] {
                if (generation != loadGeneration_) return;
                loading_ = false;
                loadError_.clear();
                videoCount_ = total;
                recordCount_ = records;
                emit readyChanged();
                emit loadingChanged();
                emit countsChanged();
                emit pageLoaded(actualPage, list, total);
            }, Qt::QueuedConnection);
        } catch (const std::exception &e) {
            const QString message = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this, [this, message, generation] {
                if (generation != loadGeneration_) return;
                loading_ = false;
                loadError_ = message;
                emit loadingChanged();
                emit loadFailed(message);
            }, Qt::QueuedConnection);
        }
    });
}

void HistoryController::startSync() {
    // exchange 兜底:QML 侧按钮禁用失效时避免重复同步
    if (syncing_.exchange(true)) return;
    cancelFlag_.store(false);

    syncStatus_ = Loc::get("开始同步");
    emit syncingChanged(true);
    emit syncStatusChanged(syncStatus_);

    workerPool_.start([this] {
        const auto progress = [this](const QString &text) {
            // Runner 在池线程回调,状态文案回投主线程
            QMetaObject::invokeMethod(
                    this,
                    [this, text] {
                        syncStatus_ = text;
                        emit syncStatusChanged(text);
                    },
                    Qt::QueuedConnection);
        };

        HistorySyncRunner::Request request;
        request.cookiePath = AppPaths::cookiePath();
        request.exportPath = AppPaths::exportPath();
        request.source = SyncSource::Manual;

        try {
            ensureStoreReady();
            HistorySyncRunner runner(store_);
            const SyncResult result = runner.run(request, progress, cancelFlag_);

            // Runner 正常返回;区分"取消"与"完成"两种收尾口径
            const QString summary =
                    cancelFlag_.load()
                            ? Loc::get("同步已取消:共 %1 页,读取 %2 条,新增 %3 条,已存在 %4 条")
                                      .arg(QString::number(result.pages),
                                           QString::number(result.seen),
                                           QString::number(result.inserted),
                                           QString::number(result.updated))
                            : Loc::get("同步完成:共 %1 页,读取 %2 条,新增 %3 条,已存在 %4 条")
                                      .arg(QString::number(result.pages),
                                           QString::number(result.seen),
                                           QString::number(result.inserted),
                                           QString::number(result.updated));
            QMetaObject::invokeMethod(
                    this,
                    [this, summary] {
                        syncing_.store(false);
                        syncStatus_ = summary;
                        emit syncingChanged(false);
                        emit syncStatusChanged(summary);
                        emit syncFinished(summary);
                    },
                    Qt::QueuedConnection);
        } catch (const std::exception &e) {
            // cookie 缺失/为空/无 SESSDATA、登录失效、接口错误统一走失败口径
            const QString message = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(
                    this,
                    [this, message] {
                        syncing_.store(false);
                        syncStatus_ = message;
                        emit syncingChanged(false);
                        emit syncStatusChanged(message);
                        emit syncFailed(message);
                    },
                    Qt::QueuedConnection);
        }
    });
}

void HistoryController::cancelSync() { cancelFlag_.store(true); }

void HistoryController::coverDownload(const QString &url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        emit coverDownloadFinished(QString());
        return;
    }
    workerPool_.start([this, trimmed] {
        QString savedPath;
        // NAM 需运行中的事件循环:池线程内起 QEventLoop 等待 finished
        QNetworkAccessManager manager;
        QNetworkRequest request{QUrl(trimmed)};
        request.setRawHeader("Referer", kCoverReferer);
        request.setRawHeader("User-Agent", kCoverUserAgent);
        request.setTransferTimeout(15000);
        QNetworkReply *reply = manager.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray payload = reply->readAll();
            QDir targetDir(
                    QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
                    QStringLiteral("/bilibili_cover"));
            QString fileName = QUrl(trimmed).fileName();
            if (fileName.isEmpty()) fileName = QStringLiteral("bilibili_cover.jpg");
            if (!fileName.contains(u'.')) fileName += QStringLiteral(".jpg");
            if (!payload.isEmpty() && targetDir.mkpath(QStringLiteral("."))) {
                QFile file(targetDir.filePath(fileName));
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(payload);
                    file.close();
                    savedPath = targetDir.filePath(fileName);
                }
            }
        }
        reply->deleteLater();
        QMetaObject::invokeMethod(
                this,
                [this, savedPath] { emit coverDownloadFinished(savedPath); },
                Qt::QueuedConnection);
    });
}
