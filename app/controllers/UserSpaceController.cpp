#include "controllers/UserSpaceController.h"
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include "core/BilibiliApiClient.h"

UserSpaceController::UserSpaceController(QObject *parent) : SpecialFollowController(parent) {}

void UserSpaceController::openSpace(const QString &midText, const QString &name, const QString &face) {
    bool valid = false;
    const qint64 mid = midText.toLongLong(&valid);
    if (!valid || mid <= 0) return;
    const bool changed = currentMid() != mid;
    const bool reloadProfile = changed || profile_.mid != mid;
    if (reloadProfile) {
        profile_ = {};
        profile_.mid = mid;
        profile_.name = name;
        profile_.faceUrl = face;
        profileError_.clear();
        selectUp(mid);
        emit profileChanged();
    }
    if (reloadProfile || (!profileBusy_ && profile_.name.isEmpty())) refreshProfile();
}

void UserSpaceController::refreshProfile() {
    const qint64 mid = currentMid();
    if (mid <= 0) return;
    const quint64 generation = ++profileGeneration_;
    profileBusy_ = true;
    profileError_.clear();
    emit profileBusyChanged();
    emit profileChanged();
    using Result = QPair<UserProfile, QString>;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation, mid] {
        const Result result = watcher->result();
        watcher->deleteLater();
        finishProfile(generation, mid, result.first, result.second);
    });
    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();
    watcher->setFuture(promise->future());
    // 任务不捕获控制器，销毁或切换路由不会被晚到回应访问。
    QThreadPool::globalInstance()->start([promise, mid] {
        Result result;
        try { result.first = UserProfileApi::fetch(*BilibiliApiClient::instance(), mid); }
        catch (const std::exception &error) { result.second = QString::fromUtf8(error.what()); }
        promise->addResult(result);
        promise->finish();
    });
}

void UserSpaceController::finishProfile(quint64 generation, qint64 mid,
                                      const UserProfile &profile, const QString &error) {
    if (generation != profileGeneration_ || mid != currentMid()) return;
    profileBusy_ = false;
    profileError_ = error;
    if (error.isEmpty()) {
        if (!profile.name.isEmpty()) profile_.name = profile.name;
        if (!profile.faceUrl.isEmpty()) profile_.faceUrl = profile.faceUrl;
        profile_.sign = profile.sign;
        profile_.archiveCount = profile.archiveCount;
    }
    emit profileChanged();
    emit profileBusyChanged();
}

void UserSpaceController::releasePageCache() {
    SpecialFollowController::releasePageCache();
    ++profileGeneration_;
    profile_ = {};
    profileError_.clear();
    profileBusy_ = false;
    emit profileChanged();
    emit profileBusyChanged();
}
