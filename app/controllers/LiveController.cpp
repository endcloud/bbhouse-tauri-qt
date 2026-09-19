#include "controllers/LiveController.h"

#include <memory>

#include <QFutureWatcher>
#include <QPromise>
#include <QSet>
#include <QThreadPool>

#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"

namespace {
struct FetchResult {
    LiveFollowPage page;
    QString error;
    bool unauthorized = false;
};
}

LiveController::LiveController(QObject *parent) : QObject(parent) {}

void LiveController::ensureLoaded() {
    if (!loaded_ && !busy_) beginFetch(true);
}

void LiveController::refresh() {
    // 刷新可取代仍在途的加载；旧代数的完成回调不能解除新请求的 busy。
    beginFetch(true);
}

void LiveController::loadMore() {
    if (busy_ || !hasMore_) return;
    beginFetch(!loaded_);
}

void LiveController::beginFetch(bool replace) {
    ++generation_;
    replace_ = replace;
    pendingPage_ = replace ? 1 : nextPage_;
    scannedPages_ = 0;
    cookieCaptured_ = false;
    requestCookie_.clear();
    if (!error_.isEmpty()) {
        error_.clear();
        emit errorChanged();
    }
    if (unauthorized_) {
        unauthorized_ = false;
        emit unauthorizedChanged();
    }
    if (!busy_) {
        busy_ = true;
        emit busyChanged();
    }
    startFetch(generation_, pendingPage_);
}

void LiveController::startFetch(quint64 generation, int page) {
    // 每次刷新/续载只读取一次；跳过未开播页时继续使用同一 Cookie 快照。
    if (!cookieCaptured_) {
        try {
            requestCookie_ = BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            cookieCaptured_ = true;
        } catch (const ApiUnauthorizedError &e) {
            finishFetch(generation, page, {}, QString::fromUtf8(e.what()), true);
            return;
        } catch (const std::exception &e) {
            finishFetch(generation, page, {}, QString::fromUtf8(e.what()), false);
            return;
        }
    }
    const QString cookie = requestCookie_;
    auto *watcher = new QFutureWatcher<FetchResult>(this);
    connect(watcher, &QFutureWatcher<FetchResult>::finished, this,
            [this, watcher, generation, page] {
        const auto result = watcher->result();
        watcher->deleteLater();
        finishFetch(generation, page, result.page, result.error, result.unauthorized);
    });
    auto promise = std::make_shared<QPromise<FetchResult>>();
    promise->start();
    watcher->setFuture(promise->future());
    // worker 不持有控制器；页面关闭后 QObject 自动断开 watcher 的接收连接。
    QThreadPool::globalInstance()->start([promise, cookie, page] {
        FetchResult result;
        try {
            result.page = LiveApi::fetchFollowed(*BilibiliApiClient::instance(), cookie, page);
        } catch (const ApiUnauthorizedError &e) {
            result.unauthorized = true;
            result.error = QString::fromUtf8(e.what());
        } catch (const std::exception &e) {
            result.error = QString::fromUtf8(e.what());
        }
        promise->addResult(result);
        promise->finish();
    });
}

void LiveController::finishFetch(quint64 generation, int page, const LiveFollowPage &result,
                               const QString &error, bool unauthorized) {
    if (generation != generation_ || !busy_ || page != pendingPage_) return;
    QString failure = error;
    QVariantList additions;
    if (failure.isEmpty()) {
        QSet<QString> seen;
        if (!replace_) {
            for (const auto &value : pool_) seen.insert(value.toMap().value("roomId").toString());
        }
        for (const auto &room : result.rooms) {
            if (room.roomId.isEmpty() || seen.contains(room.roomId)) continue;
            seen.insert(room.roomId);
            additions.append(toItemMap(room));
        }
        ++scannedPages_;
        // 过滤空页或全部重复页不代表结束；继续寻找可追加的开播房间。
        if (additions.isEmpty() && result.hasMore) {
            if (scannedPages_ < kMaxPagesPerOperation) {
                pendingPage_ = page + 1;
                startFetch(generation, pendingPage_);
                return;
            }
            failure = tr("关注直播分页超过安全上限，请刷新重试");
        }
    }
    busy_ = false;
    requestCookie_.clear();
    cookieCaptured_ = false;
    if (!failure.isEmpty()) {
        error_ = failure;
        unauthorized_ = unauthorized;
        emit errorChanged();
        emit unauthorizedChanged();
        emit busyChanged();
        return;
    }
    if (replace_) pool_ = additions;
    else pool_.append(additions);
    nextPage_ = page + 1;
    hasMore_ = result.hasMore;
    if (!loaded_) {
        loaded_ = true;
        emit loadedChanged();
    }
    emit poolChanged();
    emit hasMoreChanged();
    emit busyChanged();
}

QVariantMap LiveController::toItemMap(const LiveRoom &room) {
    return {{"roomId", room.roomId}, {"mid", room.mid}, {"title", room.title},
            {"uname", room.uname}, {"face", room.face}, {"cover", room.cover},
            {"area", room.area}, {"online", room.online}};
}
