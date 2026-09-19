#include "core/CardAuthor.h"
#include "controllers/WatchlaterController.h"

#include <QMetaObject>
#include <QThreadPool>

#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"
#include "core/ToviewApi.h"

WatchlaterController::WatchlaterController(QObject *parent) : QObject(parent) {}

bool WatchlaterController::busy() const { return busy_.load(); }

bool WatchlaterController::unauthorized() const { return unauthorized_; }

bool WatchlaterController::loaded() const { return loaded_; }

QVariantList WatchlaterController::pool() const { return pool_; }

void WatchlaterController::refresh() {
    // busy 单闸门:请求期间重复触发在此兜底
    if (busy_.exchange(true)) return;
    emit busyChanged();

    QThreadPool::globalInstance()->start([this] {
        QList<HistoryItem> items;
        QString error;
        bool unauthorized = false;
        try {
            const QString cookie =
                    BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            items = ToviewApi::fetchAll(*BilibiliApiClient::instance(), cookie);
        } catch (const ApiUnauthorizedError &e) {
            unauthorized = true;
            error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(
                this,
                [this, items, error, unauthorized] {
                    busy_.store(false);
                    emit busyChanged();
                    if (!error.isEmpty()) {
                        // 失败保留既有池(刷新保旧卡语义)
                        unauthorized_ = unauthorized;
                        emit unauthorizedChanged();
                        emit loadFailed(error);
                        return;
                    }
                    unauthorized_ = false;
                    emit unauthorizedChanged();
                    // 整体替换:QML 侧经 poolChanged 绑定原子切换,不闪空白
                    pool_.clear();
                    pool_.reserve(items.size());
                    for (const HistoryItem &item : items) {
                        pool_.append(toItemMap(item));
                    }
                    if (!loaded_) {
                        loaded_ = true;
                        emit loadedChanged();
                    }
                    emit poolChanged();
                },
                Qt::QueuedConnection);
    });
}

QVariantMap WatchlaterController::toItemMap(const HistoryItem &item) {
    QVariantMap map;
    // 键名与 HistoryController::toVariantList 一致(卡片视图模型同构)
    map.insert("videoKey", item.videoKey);
    map.insert("title", item.title);
    map.insert("subtitle", item.subtitle);
    map.insert("coverUrl", item.coverUrl);
    map.insert("authorName", item.authorName);
    map.insert("authorMid", QString::number(item.authorMid));
    map.insert("faceUrl", historyAuthorFaceUrl(item));
    map.insert("viewAt", item.viewAt);  // = add_at(添加时间,降序原序)
    map.insert("progress", item.progress);  // 负值 = 已看完,卡片按无进度渲染
    map.insert("duration", item.duration);
    map.insert("badge", item.badge);
    map.insert("linkUrl", item.linkUrl);
    map.insert("viewCount", item.viewCount);
    map.insert("rawJson", item.rawJson);
    map.insert("business", item.business);
    map.insert("oid", item.oid);
    map.insert("kid", item.kid);
    // PGC 分集定位(epid+cid 成对才可直起播;普通稿件为 0)
    qint64 epId = 0;
    qint64 cid = 0;
    ToviewApi::tryParsePgcLocation(item.rawJson, &epId, &cid);
    map.insert("epId", epId);
    map.insert("cid", cid);
    // 失效稿件标记(state<0);标题占位与禁播呈现由 UI 层按本地化处理
    map.insert("invalid", ToviewApi::isInvalidEntry(item.rawJson));
    return map;
}
