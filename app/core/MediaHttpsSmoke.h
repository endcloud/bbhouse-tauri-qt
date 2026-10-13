#ifndef MEDIA_HTTPS_SMOKE_H
#define MEDIA_HTTPS_SMOKE_H

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <memory>
#include "player/MpvClient.h"

namespace MediaHttpsSmoke {
// Public test-only CA and localhost key; never install them in a trust store.
inline constexpr char kCa[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIC8TCCAdmgAwIBAgIIan3aCqBcy/MwDQYJKoZIhvcNAQELBQAwJjEkMCIGA1UE
AxMbQkJIb3VzZSBNZWRpYSBIVFRQUyBUZXN0IENBMB4XDTIwMDEwMTAwMDAwMFoX
DTQwMDEwMTAwMDAwMFowJjEkMCIGA1UEAxMbQkJIb3VzZSBNZWRpYSBIVFRQUyBU
ZXN0IENBMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuhkHxrpD4w9Y
FWhJCTDGlpEwgXA24frrJgP4UQn/Th1bj9o39FqBeeMgjgg+XZ0SEsEG6cHz8t/K
NvaTb5oEIDRvYFVfpHPXOwvnJbF+1zMfopCmv6gTsiLpxdV6sm68iu1NSSR650qS
Y7GPUWlPgdi34L9SxPWNC+jB/yC0Tw34VLT/SP5JqTWpDFUb4UxVC7DK10H7PZO/
VhJaKZLFyHByzAcWryVOE7/wRIsjifrG/z6a23OcXgg+mU3KeeqIDlxG40cwugh7
5CJsqtxUd0b5KAGpd4eDUj7t19nT7SJxlchEN3GRstcJf8ekAOIzmudQ28BzK5/I
JfI8deqp+QIDAQABoyMwITAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIC
BDANBgkqhkiG9w0BAQsFAAOCAQEACvnvms1mGCRUWcWaEk73/wA0dURlU3EoLgCV
5KUtvAsrBc0gzoz7wCZtiIwAMSw7pr8QLT+By7ilsByHAVskaeulesZ/5mLYKCa8
0pBFuhfnjvJSwmW6mRNmogqIcooeNx9dTPYtIcZIg1wMQ4vb6nUYvHHm/kMRRMY0
Li0u7gDst4t340DP0AiCsiExHkvjePo9mkm7jGU4jbsPeXiuXlSwOWbzuOiPS7JS
lu2IUEazjDiH40P5SVqogN3QThs9VEt1R2lwLsTy7QfrV5nK1Ju/NCAN+C4DfBj+
NSAoQ/ISU9ci2Xm2OrCMmRzJmmjs4WU39Eei0lxA3pHHPJeEQg==
-----END CERTIFICATE-----
)PEM";
inline constexpr char kCertificate[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIC/DCCAeSgAwIBAgIEAQIDBDANBgkqhkiG9w0BAQsFADAmMSQwIgYDVQQDExtC
QkhvdXNlIE1lZGlhIEhUVFBTIFRlc3QgQ0EwHhcNMjEwMTAxMDAwMDAwWhcNMzkw
MTAxMDAwMDAwWjAUMRIwEAYDVQQDEwlsb2NhbGhvc3QwggEiMA0GCSqGSIb3DQEB
AQUAA4IBDwAwggEKAoIBAQDU/KHCVeChQRNVg3pQ9tUw+UEtS4m1l5t33RDB9kbG
ABlOse4a05VoKY0F4Nu0tIMK5DH0Me9HFFuJpcZvbPuejYEfmqqlBrOxH8xwyK1h
m7Qzd8Rx0r+8Y9OL0zkE9t0UTBXGDdtel3RzrvciyzPJh2fimTn4HyYTBS/Kudd3
+sqXCk/Tm89e70eg+oJY/8k88aZXeSyHi9xibLbxmawly/TkJzHjVCsPT8jE/dJ0
AC7vafwM+3RBqX9MrF3rSenC1ZOMMknKeKr4hBMApUQdTnrhmHk6/8eVQOafUJbZ
sWMk5MUKUvYEiuO/GWvhLiHm1pawlKSiRZuiCN3nV+spAgMBAAGjRDBCMAwGA1Ud
EwEB/wQCMAAwGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMBYGA1UdJQEB/wQM
MAoGCCsGAQUFBwMBMA0GCSqGSIb3DQEBCwUAA4IBAQAM2G6pBBzRGQn6RvGZMx4m
PPeMXGkED81UDJmCJ6wj0e0kJEkwPK4yT6ZZyLCw737aIVQ6j6aG8DdsyoTV6Hph
pcxM/1TRI1c4WJkvVQhDfCeEVxe693ihv6txoIKj74bAlB1utMMjxuKjKCq4SuUW
ytcRim50u8kQ0mIIFAjNqXE6sC31ceP9/I24uQ3/VzvhTC770CvZ0Jbgwdhg9/jP
z6drkpg9oFMal/Jebi865BPXM5COIGlCOQBwqgDvOS2ywU6kL4OtoXQl7TTIwR0v
sU9kVDMvzh80HE9Ux8O8DyIOUYhgJuBaicXdBckVbIiCs4PZC5BLV7Ucq0Av62Td
-----END CERTIFICATE-----
)PEM";
inline constexpr char kKey[] = R"PEM(-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA1PyhwlXgoUETVYN6UPbVMPlBLUuJtZebd90QwfZGxgAZTrHu
GtOVaCmNBeDbtLSDCuQx9DHvRxRbiaXGb2z7no2BH5qqpQazsR/McMitYZu0M3fE
cdK/vGPTi9M5BPbdFEwVxg3bXpd0c673IsszyYdn4pk5+B8mEwUvyrnXd/rKlwpP
05vPXu9HoPqCWP/JPPGmV3ksh4vcYmy28ZmsJcv05Ccx41QrD0/IxP3SdAAu72n8
DPt0Qal/TKxd60npwtWTjDJJyniq+IQTAKVEHU564Zh5Ov/HlUDmn1CW2bFjJOTF
ClL2BIrjvxlr4S4h5taWsJSkokWbogjd51frKQIDAQABAoIBAQCawka3crcZ9LMb
u8Ew+2nFgBe/1ddmP5cT7WmzxkN+s6uv5tHgZEV3vlleIBkEoPVnAJDbozz+JOiu
h+o8JLBgcOmWEUhVSR1gYn6mpOp2tjCmBSyUmFIBjXe1bfFMjiPuD6rykjcH8H0J
YeTpP0WN9fmiSTw58kg0IdysLYVHA3xfZnncychzTn/bZr9AzyYcKzrDeJppc8bH
wy+6BsIO8F3OBicIwO+XGBDQ45Z8m5XMYt07wwvvpi0PrabrhV/l8T/I0xCQKeB/
XrTEGnXD0fwLGuBtNPJCQvnPSImCW/WlmC54omHFA8nxfLpy5uRONarlbO7Q2Cwl
C/m1SJwRAoGBAPTpcrpHfzB+ex9nZ/Hiq0FE3LdfqFOb1iWtTv03zcoUdA7PprWR
ta+dsyFyu+C/u3xXz6NK2Y2z+C0dMlxfSEF+wP3lIFeiII/NJFj+ADsvr2Ot7PqP
gfT4fiEOHZXTS1djPe7r4+9vzWpZFq1eSdTP1/tDwuYFiy2lko6S5OIbAoGBAN6h
K1QSqVrVaBOdP8Lj9sgIKMP3xrWDyvU+Ck992kS9Ds3ardS501YMhobjiJY0lywh
am2Ux6bYbX61mezEpTHveqybduBQU+NL5ECbigmwqQEnojqyWAA0RLIoqGNCXTq7
v/lF2FvYjdPxcm8fQjCiekSpvWahrfPGW8f6ydwLAoGACCtJQmFkIq/1V2TRmMXg
xHqRiiRMrw1BpVwUiGm2GYl5HY1bmC7WCpeOBA2ZqrN6g3lCWcYDX7CjFI4mweID
vPuHYHCmFYY449MJ5VYDl/XNOdvxTaI/yXYIbv3Tis3Tc4TB6hhAMBpgla2HDh1Z
BRoxsBXgiF+i9tDRBKRtsgUCgYAgG8aP/ZEHVLgJ/CfyB8bT2rlmahnym+ttq2D5
TgFo4It+DQf9Ij/krTHJyo8serFC4B7lZwvUeGpyV+a3SzU4Gn3q3xEqEOvIYG2v
E7+43g3B6OFw7b/bZ65ws4R1rGCd0+5AGV63AGiUJMKIXE3Djetsc76pSrA39GfQ
U89BOQKBgQDBsHK37qVpCdwfp9EYBHLlwsqfX3GTGbkErRLKnV52Me65K+aekmSF
KOURBQ90jyKqP9t6gvNdhbOcl49TRRw67zWhk/ryuaUtowIbwkc8kekITiufAHs+
1uWWBXVgyFhHPGGlFaeHTmd+tWceDYW5t2FKlurPk0Aq7pJh/toNtg==
-----END RSA PRIVATE KEY-----
)PEM";

class Server final : public QTcpServer {
public:
    QByteArray body;
    int mediaRequests = 0;
    int audioRequests = 0;
protected:
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        socket->setLocalCertificate(QSslCertificate(QByteArray(kCertificate)));
        socket->setPrivateKey(QSslKey(QByteArray(kKey), QSsl::Rsa));
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        auto request = std::make_shared<QByteArray>();
        QObject::connect(socket, &QSslSocket::readyRead, socket, [this, socket, request] {
            request->append(socket->readAll());
            if (!request->contains("\r\n\r\n")) return;
            ++mediaRequests;
            if (request->startsWith("GET /audio.wav ")) ++audioRequests;
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        socket->startServerEncryption();
    }
};

class ProxyEnvironment final {
public:
    explicit ProxyEnvironment(quint16 port) {
        const QByteArray trap = "http://127.0.0.1:" + QByteArray::number(port);
        for (const QByteArray &key : {QByteArray("http_proxy"), QByteArray("HTTP_PROXY"),
                 QByteArray("https_proxy"), QByteArray("HTTPS_PROXY"), QByteArray("all_proxy"),
                 QByteArray("ALL_PROXY"), QByteArray("no_proxy"), QByteArray("NO_PROXY")}) {
            previous.insert(key, qgetenv(key.constData()));
            qputenv(key.constData(), key.toLower() == "no_proxy" ? QByteArray("") : trap);
        }
    }
    ~ProxyEnvironment() {
        for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
            if (it.value().isNull()) qunsetenv(it.key().constData());
            else qputenv(it.key().constData(), it.value());
        }
    }
private:
    QHash<QByteArray, QByteArray> previous;
};
}

inline bool runMediaHttpsSmoke(const QString &scratch) {
#ifndef Q_OS_WIN
    Q_UNUSED(scratch);
    return true;
#else
    auto fail = [] { qWarning("MEDIA_HTTPS_SMOKE_FAILED: certificate or media regression"); return false; };
    if (!QSslSocket::supportsSsl()) return fail();
    QTemporaryDir fixture(scratch + "/media-https-XXXXXX");
    if (!fixture.isValid()) return fail();
    QFile caFile(fixture.filePath("fixture-ca.pem"));
    if (!caFile.open(QIODevice::WriteOnly) || caFile.write(MediaHttpsSmoke::kCa) < 0) return fail();
    caFile.close();
    MediaHttpsSmoke::Server server;
    QDataStream wav(&server.body, QIODevice::WriteOnly);
    wav.setByteOrder(QDataStream::LittleEndian);
    wav.writeRawData("RIFF", 4); wav << quint32(36 + 32000);
    wav.writeRawData("WAVEfmt ", 8); wav << quint32(16) << quint16(1) << quint16(1)
        << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    wav.writeRawData("data", 4); wav << quint32(32000);
    wav.writeRawData(QByteArray(32000, '\0').constData(), 32000);
    QTcpServer proxy;
    if (!server.listen(QHostAddress::LocalHost) || !proxy.listen(QHostAddress::LocalHost)) return fail();
    int proxyRequests = 0;
    QObject::connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
        while (auto *socket = proxy.nextPendingConnection()) {
            ++proxyRequests; socket->close(); socket->deleteLater();
        }
    });
    MediaHttpsSmoke::ProxyEnvironment environment(proxy.serverPort());
    std::unique_ptr<MpvClient> client(MpvClient::create());
    if (!client || client->getPropertyString("options/tls-verify") != "yes") return fail();
    QFile defaultCa(client->getPropertyString("options/tls-ca-file"));
    if (!defaultCa.open(QIODevice::ReadOnly)
            || QSslCertificate::fromData(defaultCa.readAll()).isEmpty()) return fail();
    client->setPropertyString("ao", "null");
    client->setPropertyString("network-timeout", "2");
    bool loaded = false, failed = false;
    QObject::connect(client.get(), &MpvClient::fileLoaded, client.get(), [&] { loaded = true; });
    QObject::connect(client.get(), &MpvClient::playbackError, client.get(), [&](int, const QString &) { failed = true; });
    auto wait = [&] {
        QElapsedTimer timer; timer.start();
        while (!loaded && !failed && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        return loaded || failed;
    };
    const QString origin = QString("https://127.0.0.1:%1/").arg(server.serverPort());
    client->loadSingle(origin + "media.wav", 0);
    if (!wait() || loaded || !failed) return fail();
    client->stop();
    client->setPropertyString("tls-ca-file", caFile.fileName());
    loaded = failed = false;
    client->loadSingle(origin + "media.wav", 0);
    if (!wait() || !loaded || failed) return fail();
    loaded = failed = false;
    client->loadDashCandidates(origin + "media.wav", {origin + "audio.wav"}, 0);
    if (!wait() || !loaded || failed || server.audioRequests == 0 || proxyRequests != 0
            || client->getPropertyString("options/tls-verify") != "yes") return fail();
    qInfo("MEDIA_HTTPS_SMOKE_OK: verified TLS, rejected untrusted CA, HTTPS media/audio direct");
    return true;
#endif
}

#endif
