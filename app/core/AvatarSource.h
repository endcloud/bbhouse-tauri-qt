#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// Per-image binding; all bindings share AvatarCache's disk storage and in-flight requests.
class AvatarSource : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString userId READ userId WRITE setUserId NOTIFY userIdChanged)
    Q_PROPERTY(QUrl remoteUrl READ remoteUrl WRITE setRemoteUrl NOTIFY remoteUrlChanged)
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
public:
    explicit AvatarSource(QObject *parent = nullptr);
    QString userId() const { return userId_; }
    QUrl remoteUrl() const { return remoteUrl_; }
    QUrl source() const { return source_; }
    void setUserId(const QString &value);
    void setRemoteUrl(const QUrl &value);
signals:
    void userIdChanged();
    void remoteUrlChanged();
    void sourceChanged();
private:
    void scheduleRefresh();
    void refresh();
    void setSource(const QUrl &source);
    QString userId_;
    QUrl remoteUrl_;
    QUrl source_;
    QTimer timer_;
    bool refreshScheduled_ = false;
};
