#pragma once

#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

// One current cover only, independent of the authenticated Bilibili API client.
// Public methods and imageChanged run on the owning thread; decoding is pooled.
class SystemMediaArtwork final : public QObject {
    Q_OBJECT
public:
    explicit SystemMediaArtwork(QObject *parent = nullptr);
    ~SystemMediaArtwork() override;
    void setSource(quint64 session, const QUrl &url);
    const QImage &image() const { return image_; }

signals:
    void imageChanged();

private:
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    quint64 session_ = 0;
    quint64 generation_ = 0;
    QUrl source_;
    QImage image_;
};
