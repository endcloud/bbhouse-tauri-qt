#include "core/AvatarSource.h"
#include "core/AvatarCache.h"

AvatarSource::AvatarSource(QObject *parent) : QObject(parent) {
    connect(AvatarCache::instance(), &AvatarCache::avatarReady, this,
            [this](const QString &userId, const QUrl &source) {
                if (userId == userId_) setSource(source);
            });
    // Cached pages can stay alive for days; periodically re-check the successful-fetch TTL.
    timer_.setInterval(60 * 60 * 1000);
    connect(&timer_, &QTimer::timeout, this, &AvatarSource::refresh);
    timer_.start();
}

void AvatarSource::setUserId(const QString &value) {
    if (userId_ == value) return;
    userId_ = value;
    setSource({});
    emit userIdChanged();
    scheduleRefresh();
}

void AvatarSource::setRemoteUrl(const QUrl &value) {
    if (remoteUrl_ == value) return;
    remoteUrl_ = value;
    emit remoteUrlChanged();
    scheduleRefresh();
}

void AvatarSource::scheduleRefresh() {
    if (refreshScheduled_) return;
    refreshScheduled_ = true;
    // Coalesce model changes so a recycled delegate never requests its old URL for a new UP.
    QTimer::singleShot(0, this, [this] {
        refreshScheduled_ = false;
        refresh();
    });
}

void AvatarSource::refresh() { setSource(AvatarCache::instance()->resolve(userId_, remoteUrl_)); }

void AvatarSource::setSource(const QUrl &source) {
    if (source_ == source) return;
    source_ = source;
    emit sourceChanged();
}
