#include "core/BilibiliApiClient.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <exception>
#include <optional>

#include "core/ApiErrors.h"

namespace {
constexpr int kTimeoutMs = 30000;

QNetworkRequest buildRequest(const QUrl &url, const QString &cookie) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", BilibiliApiClient::userAgent().toUtf8());
    request.setRawHeader("Referer", BilibiliApiClient::referer().toUtf8());
    request.setRawHeader("Origin", BilibiliApiClient::origin().toUtf8());
    if (!cookie.isEmpty()) {
        request.setRawHeader("Cookie", cookie.toUtf8());
    }
    return request;
}

QByteArray readAll(QNetworkAccessManager *nam, const QNetworkRequest &request,
                   const QByteArray &body, bool formPost) {
    QNetworkReply *reply = formPost ? nam->post(request, body) : nam->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(kTimeoutMs);
    loop.exec();

    if (!timeout.isActive()) {
        // 超时触发:abort 后仍未 finished 的兜底
        reply->abort();
        reply->deleteLater();
        throw ApiError(0, Loc::get("请求超时"), 0);
    }
    timeout.stop();

    const QByteArray bytes = reply->readAll();
    const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString reason =
            reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    const QString errorText = reply->errorString();
    const auto networkError = reply->error();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError && status == 0) {
        // 传输层失败(未达 HTTP)
        throw ApiError(0, Loc::get("网络请求失败") + ": " + errorText, 0);
    }
    if (status < 200 || status >= 300) {
        throw ApiError(0,
                       Loc::get("HTTP %1 %2").arg(QString::number(status), reason),
                       status);
    }
    return bytes;
}
}  // namespace

BilibiliApiClient *BilibiliApiClient::instance() {
    static BilibiliApiClient inst;
    return &inst;
}

BilibiliApiClient::BilibiliApiClient() {
    thread_ = new QThread(this);
    thread_->setObjectName("bilibili-api");
    moveToThread(thread_);
    thread_->start();
}

BilibiliApiClient::BilibiliApiClient(const QNetworkProxy &proxy)
    : proxy_(proxy.type() == QNetworkProxy::DefaultProxy
                 ? QNetworkProxy(QNetworkProxy::NoProxy) : proxy) {}

QNetworkAccessManager *BilibiliApiClient::networkManager() {
    if (!nam_) {
        nam_ = new QNetworkAccessManager(this);
        nam_->setProxy(proxy_);
    }
    return nam_;
}

BilibiliApiClient::~BilibiliApiClient() {
    if (!thread_) return;
    thread_->quit();
    thread_->wait(3000);
}

BilibiliApiClient::Envelope BilibiliApiClient::get(const QUrl &url,
                                                   const QString &cookie) {
    if (!thread_ || QThread::currentThread() == thread_) return getInternal(url, cookie);
    std::optional<Envelope> out;
    std::exception_ptr err;
    QMetaObject::invokeMethod(
            this,
            [&, this] {
                try {
                    out = getInternal(url, cookie);
                } catch (...) {
                    err = std::current_exception();
                }
            },
            Qt::BlockingQueuedConnection);
    if (err) std::rethrow_exception(err);
    return *out;
}

BilibiliApiClient::Envelope BilibiliApiClient::postForm(
        const QUrl &url, const QMap<QString, QString> &form, const QString &cookie) {
    if (!thread_ || QThread::currentThread() == thread_) return postFormInternal(url, form, cookie);
    std::optional<Envelope> out;
    std::exception_ptr err;
    QMetaObject::invokeMethod(
            this,
            [&, this] {
                try {
                    out = postFormInternal(url, form, cookie);
                } catch (...) {
                    err = std::current_exception();
                }
            },
            Qt::BlockingQueuedConnection);
    if (err) std::rethrow_exception(err);
    return *out;
}

QByteArray BilibiliApiClient::getBytes(const QUrl &url, const QString &cookie) {
    if (!thread_ || QThread::currentThread() == thread_) return getBytesInternal(url, cookie);
    QByteArray out;
    std::exception_ptr err;
    QMetaObject::invokeMethod(
            this,
            [&, this] {
                try {
                    out = getBytesInternal(url, cookie);
                } catch (...) {
                    err = std::current_exception();
                }
            },
            Qt::BlockingQueuedConnection);
    if (err) std::rethrow_exception(err);
    return out;
}

BilibiliApiClient::Envelope BilibiliApiClient::getInternal(const QUrl &url,
                                                           const QString &cookie) {
    networkManager();
    const QByteArray bytes = readAll(nam_, buildRequest(url, cookie), {}, false);
    return {bytes, checkEnvelope(bytes)};
}

BilibiliApiClient::Envelope BilibiliApiClient::postFormInternal(
        const QUrl &url, const QMap<QString, QString> &form, const QString &cookie) {
    networkManager();
    QUrlQuery query;
    for (auto it = form.constBegin(); it != form.constEnd(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }
    QNetworkRequest request = buildRequest(url, cookie);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");
    const QByteArray bytes =
            readAll(nam_, request, query.toString(QUrl::FullyEncoded).toUtf8(), true);
    return {bytes, checkEnvelope(bytes)};
}

QByteArray BilibiliApiClient::getBytesInternal(const QUrl &url,
                                               const QString &cookie) {
    networkManager();
    return readAll(nam_, buildRequest(url, cookie), {}, false);
}

QString BilibiliApiClient::readCookieFromFile(const QString &cookiePath) {
    QFile file(cookiePath);
    if (!file.exists()) {
        throw ApiError(0, Loc::get("未找到 bilibili.cookie.txt"), 0);
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw ApiError(0, Loc::get("未找到 bilibili.cookie.txt"), 0);
    }
    const QString cookie = QString::fromUtf8(file.readAll()).trimmed();
    if (cookie.isEmpty()) {
        throw ApiError(0, Loc::get("cookie 文件内容为空"), 0);
    }
    if (!cookie.contains("SESSDATA=", Qt::CaseInsensitive)) {
        throw ApiError(0, Loc::get("cookie 文件中没有找到 SESSDATA"), 0);
    }
    return cookie;
}

QJsonObject BilibiliApiClient::checkEnvelope(const QByteArray &json) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        throw ApiError(0, Loc::get("响应解析失败"), 0);
    }
    const QJsonObject root = doc.object();
    const int code = root.value("code").isDouble()
                             ? static_cast<int>(root.value("code").toDouble())
                             : -1;
    const QString message = root.value("message").isString()
                                    ? root.value("message").toString()
                                    : QString();

    if (code == -101) {
        throw ApiUnauthorizedError(Loc::get("登录失效: 账号未登录或 SESSDATA 已失效"));
    }
    if (code != 0) {
        throw ApiError(code,
                       Loc::get("API 错误 code=%1: %2").arg(QString::number(code), message),
                       0);
    }
    return root;
}
