#include "controllers/LoginController.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"
#include "core/CookieHelpers.h"
#include "core/LoginCookies.h"
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

LoginController::LoginController(QObject *parent) : QObject(parent) {
    cookiePath_ = AppPaths::cookiePath();
    QFile file(cookiePath_);
    if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024)
        needsLogin_ = !LoginCookies::isUsable(QString::fromUtf8(file.readAll()));
    // Passport needs response Set-Cookie headers, and has a separate async
    // session. Never inherit the request-scoped regional playback proxy.
    network_.setProxy(QNetworkProxy::NoProxy);
    pollTimer_.setSingleShot(true);
    pollTimer_.setInterval(2000);
    connect(&pollTimer_, &QTimer::timeout, this, &LoginController::poll);
}

void LoginController::cancel() {
    ++generation_;
    pollTimer_.stop();
    if (reply_) reply_->abort();
    reply_.clear();
    qrUrl_.clear(); qrKey_.clear(); status_.clear(); error_.clear();
    busy_ = false;
    emit stateChanged();
}
void LoginController::open() { cancel(); emit loginRequested(); }
void LoginController::fail(const QString &message) {
    pollTimer_.stop(); busy_ = false; error_ = message; status_.clear();
    emit stateChanged();
}
void LoginController::get(const QUrl &url, const QString &cookie,
                          std::function<void(QNetworkReply *)> done) {
    const int generation = generation_;
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("User-Agent", BilibiliApiClient::userAgent().toUtf8());
    request.setRawHeader("Referer", BilibiliApiClient::referer().toUtf8());
    request.setRawHeader("Origin", BilibiliApiClient::origin().toUtf8());
    if (!cookie.isEmpty()) request.setRawHeader("Cookie", cookie.toUtf8());
    auto *reply = network_.get(request);
    reply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, done] {
        reply->deleteLater();
        if (generation != generation_) return;
        reply_.clear();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || http != 200) {
            fail(tr("登录请求失败，请检查网络后重试。"));
            return;
        }
        done(reply);
    });
}
void LoginController::startQr() {
    cancel(); busy_ = true; status_ = tr("正在获取二维码…"); emit stateChanged();
    get(QUrl("https://passport.bilibili.com/x/passport-login/web/qrcode/generate"), {},
        [this](QNetworkReply *reply) {
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject data = root.value("data").toObject();
        const QUrl url(data.value("url").toString());
        if (root.value("code").toInt(-1) != 0 || data.value("qrcode_key").toString().isEmpty()
            || url.scheme() != "https" || url.host() != "passport.bilibili.com") {
            fail(tr("二维码获取失败，请重试。")); return;
        }
        qrUrl_ = url.toString(); qrKey_ = data.value("qrcode_key").toString();
        qrDeadline_ = QDateTime::currentMSecsSinceEpoch() + 180000;
        busy_ = false; status_ = tr("请使用哔哩哔哩手机客户端扫码"); emit stateChanged();
        pollTimer_.start();
    });
}
void LoginController::poll() {
    if (qrKey_.isEmpty()) return;
    if (QDateTime::currentMSecsSinceEpoch() >= qrDeadline_) {
        qrUrl_.clear(); qrKey_.clear(); fail(tr("二维码已失效，请刷新二维码。")); return;
    }
    QUrl url("https://passport.bilibili.com/x/passport-login/web/qrcode/poll");
    QUrlQuery query; query.addQueryItem("qrcode_key", qrKey_); url.setQuery(query);
    get(url, {}, [this](QNetworkReply *reply) {
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject data = root.value("data").toObject();
        if (root.value("code").toInt(-1) != 0) { fail(tr("扫码状态获取失败，请刷新二维码。")); return; }
        const int code = data.value("code").toInt(-1);
        if (code == 0) {
            QList<QByteArray> headers;
            for (const auto &pair : reply->rawHeaderPairs())
                if (pair.first.compare("Set-Cookie", Qt::CaseInsensitive) == 0) headers << pair.second;
            const QString cookie = LoginCookies::fromResponse(headers, QUrl(data.value("url").toString()));
            if (!LoginCookies::isUsable(cookie)) { fail(tr("登录响应中没有有效 Cookie，请重新扫码。")); return; }
            validateAndSave(cookie);
        } else if (code == 86038) {
            qrUrl_.clear(); qrKey_.clear(); fail(tr("二维码已失效，请刷新二维码。"));
        } else if (code == 86090 || code == 86101) {
            status_ = code == 86090 ? tr("已扫码，请在手机上确认登录") : tr("请使用哔哩哔哩手机客户端扫码");
            emit stateChanged(); pollTimer_.start();
        } else fail(tr("扫码状态获取失败，请刷新二维码。"));
    });
}
void LoginController::importText(const QString &text) {
    cancel();
    const QString cookie = LoginCookies::normalize(text);
    if (!LoginCookies::isUsable(cookie)) { fail(tr("Cookie 格式无效或缺少 SESSDATA。")); return; }
    validateAndSave(cookie);
}
void LoginController::importFile(const QUrl &url) {
    cancel();
    if (!url.isLocalFile()) { fail(tr("请选择本地 Cookie 文本文件。")); return; }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        fail(tr("Cookie 文件无法读取或超过 1 MiB。")); return;
    }
    importText(QString::fromUtf8(file.readAll()));
}
void LoginController::validateAndSave(const QString &cookie) {
    pollTimer_.stop(); busy_ = true; status_ = tr("正在验证登录状态…"); emit stateChanged();
    get(QUrl("https://api.bilibili.com/x/web-interface/nav"), cookie, [this, cookie](QNetworkReply *reply) {
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject data = root.value("data").toObject();
        if (root.value("code").toInt(-1) != 0 || !data.value("isLogin").toBool()) {
            fail(tr("Cookie 无效或已过期，请重新登录。")); return;
        }
        const qint64 verifiedMid = data.value("mid").toVariant().toLongLong();
        if (verifiedMid <= 0) { fail(tr("Cookie 无效或已过期，请重新登录。")); return; }
        const QString mid = CookieHelpers::tryGetCookieValue(cookie, "DedeUserID");
        if (!mid.isEmpty() && mid != QString::number(verifiedMid)) {
            fail(tr("Cookie 中的用户与登录账号不一致。")); return;
        }
        // The nav identity is authoritative; minimal browser imports may only
        // contain SESSDATA, while following/PGC queries require DedeUserID.
        finish(mid.isEmpty() ? cookie + "; DedeUserID=" + QString::number(verifiedMid) : cookie);
    });
}
void LoginController::finish(const QString &cookie) {
    if (!LoginCookies::save(cookiePath_, cookie)) {
        fail(tr("Cookie 保存失败，请检查目标目录的写入权限。")); return;
    }
    if (needsLogin_) { needsLogin_ = false; emit needsLoginChanged(); }
    cancel(); emit authenticated();
}
