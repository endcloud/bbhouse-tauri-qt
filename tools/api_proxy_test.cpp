#include "core/BilibiliApiClient.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QThread>
#include <future>
#include <memory>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

// Both peers are local fixtures. The proxy deliberately resolves a fake host,
// so a misplaced proxy cannot accidentally succeed through real DNS/network.
class Fixture {
public:
    QTcpServer server;
    int requests = 0;
    QByteArray requestLine;
    bool requireAuthentication = false;
    bool authenticated = false;
    bool sawProxyAuthorization = false;
    Fixture() {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                auto bytes = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    if (!bytes->contains("\r\n\r\n")) return;
                    ++requests;
                    requestLine = bytes->left(bytes->indexOf("\r\n"));
                    sawProxyAuthorization = bytes->toLower().contains("proxy-authorization:");
                    const QByteArray expected = "proxy-authorization: basic " + QByteArray("fixture-user:fixture-secret").toBase64().toLower();
                    if (requireAuthentication && !bytes->toLower().contains(expected)) {
                        socket->write("HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm=fixture\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    if (requireAuthentication) authenticated = true;
                    const QByteArray body = "{\"code\":0,\"data\":{\"ok\":true}}";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Fixture direct, proxy, trap;
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        qInfo() << (ok ? "PASS" : "FAIL") << name;
        failures += !ok;
    };
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", trap.server.serverPort()));
    const auto directUrl = QUrl(QString("http://127.0.0.1:%1/api").arg(direct.server.serverPort()));
    const auto regionalUrl = QUrl("http://regional-fixture.invalid/pgc/view/web/season");
    try {
        BilibiliApiClient scoped(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy.server.serverPort()));
        check(scoped.get(regionalUrl, {}).root.value("code").toInt() == 0, "scoped HTTP proxy returns API envelope");
        check(proxy.requests == 1 && proxy.requestLine.startsWith("GET http://regional-fixture.invalid/"),
              "only regional API uses explicit proxy absolute target");
        BilibiliApiClient noProxy{QNetworkProxy(QNetworkProxy::NoProxy)};
        check(noProxy.get(directUrl, {}).root.value("code").toInt() == 0 && direct.requests == 1,
              "direct client ignores application proxy");
        auto *shared = BilibiliApiClient::instance();
        auto ordinary = std::async(std::launch::async, [shared, directUrl] { return shared->get(directUrl, {}); });
        while (ordinary.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        check(ordinary.get().root.value("code").toInt() == 0 && direct.requests == 2,
              "ordinary shared API client remains direct after regional request");
        check(scoped.get(regionalUrl, {}).root.value("code").toInt() == 0 && proxy.requests == 2,
              "ordinary request cannot mutate scoped proxy routing");
        BilibiliApiClient defaultProxy{QNetworkProxy(QNetworkProxy::DefaultProxy)};
        check(defaultProxy.get(directUrl, {}).root.value("code").toInt() == 0 && direct.requests == 3,
              "default proxy sentinel never inherits system/application proxy");
        Fixture authProxy;
        authProxy.requireAuthentication = true;
        BilibiliApiClient authenticatedClient{QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1",
                                                         authProxy.server.serverPort(), "fixture-user", "fixture-secret")};
        check(authenticatedClient.get(regionalUrl, {}).root.value("code").toInt() == 0 && authProxy.authenticated,
              "optional credentials answer proxy authentication challenge");
        check(!direct.sawProxyAuthorization, "proxy credentials never leak to ordinary origin requests");
        check(trap.requests == 0, "global proxy trap sees no traffic");
    } catch (const std::exception &) {
        // Never print URLs, credentials or raw API response on failure.
        check(false, "fixture request unexpectedly failed");
    }
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
    return failures ? 1 : 0;
}
