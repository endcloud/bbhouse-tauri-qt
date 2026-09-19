#include "player/SystemMediaArtwork.h"

#include <QBuffer>
#include <QFutureWatcher>
#include <QImageReader>
#include <QNetworkReply>
#include <QPromise>
#include <QThreadPool>
#include <memory>

namespace {
constexpr qsizetype MaxDownloadBytes = 8 * 1024 * 1024;

QImage decodeCover(QByteArray data) {
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    // Bound both allocation and decoder work, even for untrusted image bytes.
    if (!size.isValid() || size.width() > 8192 || size.height() > 8192
        || qint64(size.width()) * size.height() > 32 * 1024 * 1024) return {};
    reader.setAutoTransform(true);
    reader.setScaledSize(size.scaled(512, 512, Qt::KeepAspectRatio));
    const QImage decoded = reader.read();
    if (decoded.isNull()) return {};
    return decoded.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBA8888);
}
}

SystemMediaArtwork::SystemMediaArtwork(QObject *parent) : QObject(parent) {}

SystemMediaArtwork::~SystemMediaArtwork() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
}

void SystemMediaArtwork::setSource(quint64 session, const QUrl &source) {
    QUrl url = source;
    if (url.scheme().isEmpty() && !url.host().isEmpty()) url.setScheme("https");
    if ((url.scheme() != "https" && url.scheme() != "http")
        || url.host().isEmpty() || !url.userInfo().isEmpty() || session == 0) url = QUrl{};
    if (session_ == session && source_ == url) return;
    session_ = session;
    source_ = url;
    const quint64 generation = ++generation_;
    if (reply_) {
        reply_->abort();
        reply_ = nullptr;
    }
    if (!image_.isNull()) {
        image_ = {};
        emit imageChanged();
    }
    if (url.isEmpty()) return;

    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = network_.get(request);
    reply_ = reply;
    reply->setReadBufferSize(MaxDownloadBytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll());
        if (bytes->size() > MaxDownloadBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, generation] {
        if (reply->isOpen()) bytes->append(reply->readAll());
        const bool valid = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && bytes->size() <= MaxDownloadBytes;
        reply->deleteLater();
        if (reply_ == reply) reply_ = nullptr;
        if (generation != generation_ || !valid) return;

        auto promise = std::make_shared<QPromise<QImage>>();
        promise->start();
        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation] {
            const QImage image = watcher->result();
            watcher->deleteLater();
            if (generation != generation_ || image.isNull()) return;
            image_ = image;
            emit imageChanged();
        });
        watcher->setFuture(promise->future());
        // The worker owns only bytes/promise; destroying the loader never leaves
        // a worker with access to a QObject or native Now Playing state.
        QThreadPool::globalInstance()->start([promise, bytes] {
            promise->addResult(decodeCover(*bytes));
            promise->finish();
        });
    });
}
