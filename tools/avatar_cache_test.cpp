#include "core/AvatarCache.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <memory>

namespace {
QByteArray png(const QColor &color) {
    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
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
QColor colorAt(const QUrl &source) {
    return QImage(source.toLocalFile()).pixelColor(0, 0);
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/avatar-cache-XXXXXX");
    if (!temp.isValid()) return 1;
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return 1;
    int requests = 0;
    bool sawCredentials = false;
    bool invalid = false;
    const auto red = png(Qt::red), blue = png(Qt::blue);
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
                QByteArray body = invalid ? QByteArray("not an image") : path == "/blue" ? blue : red;
                QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nSet-Cookie: fixture=secret\r\nContent-Length: "
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
    qint64 now = 1800000000000LL;
    AvatarCache cache(temp.path(), nullptr, [&] { return now; });
    int readyCount = 0;
    QUrl lastSource;
    QObject::connect(&cache, &AvatarCache::avatarReady, [&](const QString &, const QUrl &source) {
        ++readyCount;
        lastSource = source;
    });
    check(cache.resolve("100", url("/red")).isEmpty(), "first load returns placeholder without blocking");
    cache.resolve("100", url("/red"));
    cache.resolve("100", url("/red"));
    check(spinUntil([&] { return readyCount == 1; }) && requests == 1,
          "avatar strip and management consumers share one asynchronous request");
    const QUrl original = lastSource;
    check(original.isLocalFile() && colorAt(original) == QColor(Qt::red), "validated pixels saved to disk");
    now += AvatarCache::LifetimeMs - 1;
    check(cache.resolve("100", url("/blue")) == original && requests == 1,
          "same UP keeps seven-day cache even when URL changes");
    {
        AvatarCache reopened(temp.path(), nullptr, [&] { return now; });
        check(reopened.resolve("100", {}) == original, "process restart can load cached avatar without network URL");
    }
    ++now;
    check(cache.resolve("100", url("/blue")) == original, "exact seven-day boundary serves stale pixels while refreshing");
    cache.resolve("100", url("/blue"));
    check(spinUntil([&] { return readyCount == 2; }) && requests == 2
              && lastSource != original && colorAt(lastSource) == QColor(Qt::blue),
          "successful refresh updates pixels and busts decoded-image cache");
    const QUrl refreshed = lastSource;
    now += AvatarCache::LifetimeMs;
    invalid = true;
    cache.resolve("100", url("/red"));
    check(spinUntil([&] { return requests == 3; }), "expired avatar schedules a refresh");
    // A second independent request finishing proves the preceding small reply has been processed.
    cache.resolve("200", url("/slow"));
    check(spinUntil([&] { return requests == 4; }), "invalid-image fixture served");
    QElapsedTimer drain;
    drain.start();
    spinUntil([&] { return drain.elapsed() >= 120; });
    check(cache.resolve("100", url("/red")) == refreshed && readyCount == 2
              && colorAt(refreshed) == QColor(Qt::blue),
          "failed decoding retains stale disk pixels and successful timestamp");
    check(requests == 4, "failed refresh applies retry backoff");
    invalid = false;
    now += 5 * 60 * 1000;
    cache.resolve("100", url("/red"));
    check(spinUntil([&] { return readyCount == 3; }) && colorAt(lastSource) == QColor(Qt::red),
          "retry after backoff recovers stale avatar");
    cache.resolve("300", url("/slow"));
    check(spinUntil([&] { return requests == 6; }), "old URL request is in flight");
    cache.resolve("300", url("/blue"));
    check(spinUntil([&] { return readyCount == 4; }) && requests == 7
              && colorAt(lastSource) == QColor(Qt::blue),
          "late old-URL response cannot overwrite current avatar URL");
    const int beforeInvalidUrls = requests;
    check(cache.resolve("400", QUrl("file:///tmp/avatar.png")).isEmpty()
              && cache.resolve("401", QUrl("https://user:password@example.invalid/avatar.png")).isEmpty()
              && requests == beforeInvalidUrls, "non-http and embedded credential URLs are rejected");
    check(!sawCredentials, "requests send no cookies or authorization including cookies offered by image server");
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("avatarUrl", lastSource);
    QQmlComponent imageComponent(&engine);
    imageComponent.setData("import QtQuick\nImage { source: avatarUrl; asynchronous: true }", QUrl());
    std::unique_ptr<QObject> qmlImage(imageComponent.create());
    check(qmlImage && spinUntil([&] { return qmlImage->property("status").toInt() == 1; }),
          "Qt Quick loads revisioned local-file URL used by avatar bindings");
    // An unavailable server must not prevent an expired persisted image from being returned.
    server.close();
    now += AvatarCache::LifetimeMs;
    AvatarCache offline(temp.path(), nullptr, [&] { return now; });
    const QUrl offlineImage = offline.resolve("100", url("/red"));
    check(!offlineImage.isEmpty() && colorAt(offlineImage) == QColor(Qt::red),
          "expired persistent image remains available offline");
    return failures ? 1 : 0;
}
