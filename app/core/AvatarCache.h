#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <functional>

// Public image requests deliberately use a separate, credential-free network manager.
// One atomic PNG contains both pixels and its last successful refresh timestamp.
class AvatarCache : public QObject {
    Q_OBJECT
public:
    using Clock = std::function<qint64()>;
    static constexpr qint64 LifetimeMs = 7LL * 24 * 60 * 60 * 1000;
    explicit AvatarCache(const QString &directory, QObject *parent = nullptr, Clock clock = {});
    static AvatarCache *instance();
    Q_INVOKABLE void releaseMemoryCache();
    QUrl resolve(const QString &userId, const QUrl &remoteUrl);

signals:
    void avatarReady(const QString &userId, const QUrl &source);

private:
    struct Entry {
        QUrl source;
        qint64 refreshedAt = 0;
        qint64 retryAfter = 0;
    };
    QString imagePath(const QString &userId) const;
    Entry readEntry(const QString &userId) const;
    QString directory_;
    Clock clock_;
    QNetworkAccessManager network_;
    QStringList entryUse_;
    QHash<QString, Entry> entries_;
    QHash<QString, QUrl> latestUrls_;
    QSet<QString> pending_;
};
