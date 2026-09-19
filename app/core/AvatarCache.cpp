#include "core/AvatarCache.h"
#include "core/AppPaths.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QNetworkReply>
#include <QSaveFile>
#include <memory>

namespace {
constexpr qsizetype MaxDownloadBytes = 4 * 1024 * 1024;
constexpr qint64 RetryDelayMs = 5 * 60 * 1000;
const QString TimestampKey = QStringLiteral("bbhouse-refreshedAt");

QUrl localSource(const QString &path, qint64 timestamp) {
    QUrl url = QUrl::fromLocalFile(path);
    // Qt Quick keys its decoded-image cache by the complete URL; a successful
    // refresh must invalidate those pixels even though the disk filename is stable.
    url.setQuery(QStringLiteral("v=%1").arg(timestamp));
    return url;
}

bool reasonableSize(const QSize &size) {
    return size.isValid() && size.width() <= 4096 && size.height() <= 4096;
}
}

AvatarCache::AvatarCache(const QString &directory, QObject *parent, Clock clock)
    : QObject(parent), directory_(directory), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return QDateTime::currentMSecsSinceEpoch(); };
    QDir().mkpath(directory_);
}

AvatarCache *AvatarCache::instance() {
    static auto *cache = new AvatarCache(AppPaths::dataDir() + "/avatar-cache", qApp);
    return cache;
}

QString AvatarCache::imagePath(const QString &userId) const {
    const auto key = QCryptographicHash::hash(userId.toUtf8(), QCryptographicHash::Sha256).toHex();
    return directory_ + "/" + QString::fromLatin1(key) + ".png";
}

AvatarCache::Entry AvatarCache::readEntry(const QString &userId) const {
    QImageReader reader(imagePath(userId), "png");
    if (!reasonableSize(reader.size())) return {};
    const QImage image = reader.read();
    if (image.isNull()) return {};
    const qint64 timestamp = image.text(TimestampKey).toLongLong();
    if (timestamp <= 0) return {};
    return {localSource(imagePath(userId), timestamp), timestamp, 0};
}

QUrl AvatarCache::resolve(const QString &userId, const QUrl &remoteUrl) {
    if (userId.isEmpty() || userId == "0") return {};
    if (!entries_.contains(userId)) entries_.insert(userId, readEntry(userId));
    const Entry entry = entries_.value(userId);
    const qint64 now = clock_();
    const bool fresh = !entry.source.isEmpty() && now >= entry.refreshedAt
                       && now - entry.refreshedAt < LifetimeMs;
    QUrl url = remoteUrl;
    if (url.scheme().isEmpty() && !url.host().isEmpty()) url.setScheme("https");
    const bool validUrl = (url.scheme() == "https" || url.scheme() == "http")
                          && !url.host().isEmpty() && url.userInfo().isEmpty();
    if (validUrl) latestUrls_.insert(userId, url);
    if (fresh || pending_.contains(userId) || now < entry.retryAfter || !validUrl)
        return entry.source;

    pending_.insert(userId);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = network_.get(request);
    reply->setReadBufferSize(MaxDownloadBytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll());
        if (bytes->size() > MaxDownloadBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, userId, url] {
        bytes->append(reply->readAll());
        const bool validResponse = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && bytes->size() <= MaxDownloadBytes;
        reply->deleteLater();
        pending_.remove(userId);
        if (latestUrls_.value(userId) != url) {
            // The same UP's refreshed metadata arrived while this request was pending.
            // Discard old pixels and fetch the latest known URL without extending the TTL.
            resolve(userId, latestUrls_.value(userId));
            return;
        }
        auto &entry = entries_[userId];
        entry.retryAfter = clock_() + RetryDelayMs;
        if (!validResponse) return;
        QBuffer buffer(bytes.get());
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        if (!reasonableSize(reader.size())) return;
        reader.setScaledSize(reader.size().scaled(256, 256, Qt::KeepAspectRatio));
        QImage image = reader.read();
        if (image.isNull()) return;
        const qint64 refreshedAt = clock_();
        image.setText(TimestampKey, QString::number(refreshedAt));
        QSaveFile file(imagePath(userId));
        if (!file.open(QIODevice::WriteOnly)) return;
        QImageWriter writer(&file, "png");
        if (!writer.write(image) || !file.commit()) return;
        entry = {localSource(imagePath(userId), refreshedAt), refreshedAt, 0};
        emit avatarReady(userId, entry.source);
    });
    return entry.source;
}
