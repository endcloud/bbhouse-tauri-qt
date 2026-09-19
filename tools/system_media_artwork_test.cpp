#include "player/SystemMediaArtwork.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <memory>

namespace {
QByteArray png(const QColor &color) {
    QImage image(800, 450, QImage::Format_RGBA8888);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
bool spinUntil(const std::function<bool()> &ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 3000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}
void drain(int milliseconds = 120) {
    QElapsedTimer timer;
    timer.start();
    spinUntil([&] { return timer.elapsed() >= milliseconds; });
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return 1;
    const auto red = png(Qt::red), blue = png(Qt::blue);
    int requests = 0;
    bool sawCredentials = false;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            auto request = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                socket->setProperty("handled", true);
                ++requests;
                sawCredentials |= request->toLower().contains("cookie:")
                    || request->toLower().contains("authorization:");
                const auto path = request->split(' ').value(1);
                const QByteArray body = path == "/invalid" ? QByteArray("not an image")
                    : path == "/large" ? QByteArray(8 * 1024 * 1024 + 1, 'x')
                    : path == "/blue" ? blue : red;
                const QByteArray status = path == "/missing" ? "404 Not Found" : "200 OK";
                const QByteArray response = "HTTP/1.1 " + status
                    + "\r\nContent-Type: image/png\r\nSet-Cookie: fixture=secret\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                QTimer::singleShot(path == "/slow" ? 80 : 1, socket, [socket, response] {
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            });
        }
    });
    auto url = [&](const char *path) {
        return QUrl(QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path));
    };
    int failures = 0;
    auto check = [&](bool value, const char *name) {
        qInfo() << (value ? "PASS" : "FAIL") << name;
        failures += !value;
    };
    SystemMediaArtwork cover;
    int changed = 0;
    QObject::connect(&cover, &SystemMediaArtwork::imageChanged, [&] {
        ++changed;
        check(QThread::currentThread() == app.thread(), "decoded artwork delivered on owning thread");
    });
    cover.setSource(1, url("/red"));
    check(cover.image().isNull(), "initial download is asynchronous");
    cover.setSource(1, url("/red"));
    check(spinUntil([&] { return !cover.image().isNull(); }) && requests == 1,
          "duplicate source coalesces during download");
    check(cover.image().size() == QSize(512, 288) && cover.image().pixelColor(0, 0) == QColor(Qt::red),
          "image is decoded and bounded to native artwork size");
    const auto cacheKey = cover.image().cacheKey();
    for (int i = 0; i < 10; ++i) cover.setSource(1, url("/red"));
    drain();
    check(requests == 1 && cover.image().cacheKey() == cacheKey && changed == 1,
          "progress updates keep artwork without redownload or repeated decoding");

    cover.setSource(2, url("/invalid"));
    check(cover.image().isNull(), "track change clears previous artwork immediately");
    check(spinUntil([&] { return requests == 2; }), "invalid image received");
    drain();
    cover.setSource(2, url("/invalid"));
    drain();
    check(cover.image().isNull() && requests == 2, "invalid image stays empty without retry on every tick");

    cover.setSource(3, url("/slow"));
    check(spinUntil([&] { return requests == 3; }), "delayed cover request starts");
    cover.setSource(4, url("/blue"));
    check(spinUntil([&] { return !cover.image().isNull(); }), "new track cover arrives");
    drain();
    check(cover.image().pixelColor(0, 0) == QColor(Qt::blue), "late response cannot overwrite new track");
    cover.setSource(4, {});
    check(cover.image().isNull(), "missing cover clears current artwork");

    const int beforeClose = requests;
    cover.setSource(5, url("/slow"));
    check(spinUntil([&] { return requests > beforeClose; }), "close fixture request starts");
    cover.setSource(0, {});
    drain();
    check(cover.image().isNull(), "closing session invalidates pending cover");
    cover.setSource(6, url("/missing"));
    drain();
    check(cover.image().isNull(), "HTTP failure cannot publish image body");
    cover.setSource(7, url("/large"));
    drain();
    check(cover.image().isNull(), "oversized download is discarded");

    const int beforeInvalid = requests;
    cover.setSource(8, QUrl("file:///does-not-exist.png"));
    QUrl credentialUrl = url("/red");
    credentialUrl.setUserName("forbidden");
    credentialUrl.setPassword("forbidden");
    cover.setSource(9, credentialUrl);
    drain();
    check(requests == beforeInvalid && cover.image().isNull(), "only credential-free HTTP image URLs accepted");
    check(!sawCredentials, "dedicated downloader never sends API credentials or saved cookies");
    {
        auto pending = std::make_unique<SystemMediaArtwork>();
        pending->setSource(10, url("/slow"));
        check(spinUntil([&] { return requests > beforeInvalid; }), "destruction fixture starts");
    }
    drain();
    QThreadPool::globalInstance()->waitForDone();
    if (!failures) qInfo() << "System media artwork regression passed";
    return failures ? 1 : 0;
}
