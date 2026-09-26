#include "preferences/AppPreferences.h"

#include <QSettings>
#include <QVariant>
#include <QHostAddress>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#include <memory>
#include <algorithm>

namespace {
bool savedBool(const QVariant &value, bool fallback) {
    const QString text = value.toString().trimmed().toLower();
    if (text == "true" || text == "1") return true;
    if (text == "false" || text == "0") return false;
    return fallback;
}

int savedInt(const QVariant &value, int fallback, int minimum, int maximum) {
    bool ok = false;
    const int number = value.toString().toInt(&ok);
    return ok && number >= minimum && number <= maximum ? number : fallback;
}

bool validProxyHost(const QString &host) {
    if (host.isEmpty() || host.size() > 253) return false;
    QHostAddress address;
    if (address.setAddress(host)) return true;
    // Accept a bare DNS name only: no URL, path, port or embedded credentials.
    const QString ascii = QString::fromLatin1(QUrl::toAce(host));
    static const QRegularExpression domain(
        QStringLiteral("^(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*\\.?$"));
    return domain.match(ascii).hasMatch();
}

bool validProxyType(const QString &type) {
    return type == "none" || type == "http" || type == "socks5";
}
constexpr auto proxyKey = "App.Network.RegionalProxy";
} // namespace

class AppPreferences::Private {
   public:
    std::unique_ptr<QSettings> settings;
    QString proxyPassword;
};

AppPreferences *AppPreferences::instance() {
    static AppPreferences inst;
    return &inst;
}

AppPreferences::AppPreferences(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>()) {
    // IniFormat 明确落盘位置,避免 Windows 注册表散落
    d->settings = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, "shizi",
                                              "bbhouse-qt");
}

AppPreferences::~AppPreferences() = default;

int AppPreferences::pageCacheMinutes() const {
    const int minutes = savedInt(d->settings->value("App.Memory.PageCacheMinutes", 5), 5, 1, 30);
    return minutes == 1 || minutes == 5 || minutes == 10 || minutes == 30 ? minutes : 5;
}

void AppPreferences::setPageCacheMinutes(int value) {
    if (value != 1 && value != 5 && value != 10 && value != 30) return;
    if (pageCacheMinutes() == value) return;
    d->settings->setValue("App.Memory.PageCacheMinutes", value);
    d->settings->sync();
    emit pageCacheMinutesChanged();
}

QString AppPreferences::language() const {
    return d->settings->value("app/language", "system").toString();
}

void AppPreferences::setLanguage(const QString &value) {
    if (language() == value) return;
    d->settings->setValue("app/language", value);
    emit languageChanged();
}

QString AppPreferences::theme() const {
    return d->settings->value("app/theme", "system").toString();
}

void AppPreferences::setTheme(const QString &value) {
    if (theme() == value) return;
    d->settings->setValue("app/theme", value);
    emit themeChanged();
}

QString AppPreferences::danmakuImplementation() const {
    const QString value = d->settings->value("App.Player.DanmakuImplementation", "sprite").toString();
    return value == "scene" ? value : QStringLiteral("sprite");
}

void AppPreferences::setDanmakuImplementation(const QString &value) {
    if (value != "scene" && value != "sprite") return;
    if (danmakuImplementation() == value) return;
    d->settings->setValue("App.Player.DanmakuImplementation", value);
    d->settings->sync();
    emit danmakuImplementationChanged();
}

bool AppPreferences::danmakuEnabled() const {
    return savedBool(d->settings->value("App.Player.DanmakuOn", true), true);
}

void AppPreferences::setDanmakuEnabled(bool value) {
    if (danmakuEnabled() == value) return;
    d->settings->setValue("App.Player.DanmakuOn", value);
    d->settings->sync();
    emit danmakuEnabledChanged();
}

bool AppPreferences::danmakuMergeSimilar() const {
    return savedBool(d->settings->value("App.Player.DanmakuMergeSimilar", true), true);
}

void AppPreferences::setDanmakuMergeSimilar(bool value) {
    if (danmakuMergeSimilar() == value) return;
    d->settings->setValue("App.Player.DanmakuMergeSimilar", value);
    d->settings->sync();
    emit danmakuMergeSimilarChanged();
}

int AppPreferences::danmakuOpacity() const {
    const int value = savedInt(d->settings->value("App.Player.DanmakuOpacity", 100), 100, 0, 100);
    return value;
}

void AppPreferences::setDanmakuOpacity(int value) {
    value = std::clamp(value, 0, 100);
    if (danmakuOpacity() == value) return;
    d->settings->setValue("App.Player.DanmakuOpacity", value);
    d->settings->sync();
    emit danmakuOpacityChanged();
}

int AppPreferences::danmakuFontSize() const {
    const int value = savedInt(d->settings->value("App.Player.DanmakuFontSize", 25), 25, 12, 48);
    return value;
}

void AppPreferences::setDanmakuFontSize(int value) {
    value = std::clamp(value, 12, 48);
    if (danmakuFontSize() == value) return;
    d->settings->setValue("App.Player.DanmakuFontSize", value);
    d->settings->sync();
    emit danmakuFontSizeChanged();
}

int AppPreferences::danmakuArea() const {
    const int value = savedInt(d->settings->value("App.Player.DanmakuArea", 25), 25, 25, 100);
    if (value != 25 && value != 50 && value != 100) return 25;
    return value;
}

void AppPreferences::setDanmakuArea(int value) {
    if (value != 25 && value != 50 && value != 100) return;
    if (danmakuArea() == value) return;
    d->settings->setValue("App.Player.DanmakuArea", value);
    d->settings->sync();
    emit danmakuAreaChanged();
}

int AppPreferences::danmakuDensity() const {
    const int value = savedInt(d->settings->value("App.Player.DanmakuDensity", 0), 0, 0, 100);
    if (value % 20 != 0) return 0;
    return value;
}

void AppPreferences::setDanmakuDensity(int value) {
    if (value < 0 || value > 100 || value % 20 != 0) return;
    if (danmakuDensity() == value) return;
    d->settings->setValue("App.Player.DanmakuDensity", value);
    d->settings->sync();
    emit danmakuDensityChanged();
}

QString AppPreferences::proxyType() const {
    const QString type = d->settings->value(proxyKey).toMap().value("type", "http").toString();
    return validProxyType(type) ? type : QStringLiteral("http");
}

QString AppPreferences::proxyHost() const {
    const QString host = d->settings->value(proxyKey).toMap().value("host", "localhost").toString().trimmed();
    return validProxyHost(host) ? host : QStringLiteral("localhost");
}

int AppPreferences::proxyPort() const {
    return savedInt(d->settings->value(proxyKey).toMap().value("port", 7890), 7890, 1, 65535);
}

QString AppPreferences::proxyUsername() const {
    return d->settings->value(proxyKey).toMap().value("username").toString().left(256);
}

QString AppPreferences::proxyPassword() const { return d->proxyPassword; }

QNetworkProxy AppPreferences::regionalProxy() const {
    Q_ASSERT(QThread::currentThread() == thread());
    if (proxyType() == "none") return QNetworkProxy(QNetworkProxy::NoProxy);
    return QNetworkProxy(proxyType() == "socks5" ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
                         proxyHost(), static_cast<quint16>(proxyPort()), proxyUsername(), proxyPassword());
}

bool AppPreferences::saveProxySettings(const QString &type, const QString &host, int port,
                                       const QString &username, const QString &password) {
    Q_ASSERT(QThread::currentThread() == thread());
    const QString normalizedHost = host.trimmed();
    if (!validProxyType(type) || !validProxyHost(normalizedHost) || port < 1 || port > 65535
        || username.size() > 256 || password.size() > 1024 || (!password.isEmpty() && username.isEmpty()))
        return false;
    const QVariantMap record{{"type", type}, {"host", normalizedHost}, {"port", port}, {"username", username}};
    if (proxyType() == type && proxyHost() == normalizedHost && proxyPort() == port
        && proxyUsername() == username && proxyPassword() == password)
        return true;
    const QVariant old = d->settings->value(proxyKey);
    d->settings->setValue(proxyKey, record);
    d->settings->sync();
    if (d->settings->status() != QSettings::NoError) {
        if (old.isValid()) d->settings->setValue(proxyKey, old);
        else d->settings->remove(proxyKey);
        d->settings->sync();
        return false;
    }
    d->proxyPassword = password;
    emit proxySettingsChanged();
    return true;
}

QVariant AppPreferences::value(const QString &key, const QVariant &fallback) const {
    return d->settings->value(key, fallback);
}

void AppPreferences::setValue(const QString &key, const QVariant &value) {
    if (key == "App.Memory.PageCacheMinutes") {
        setPageCacheMinutes(savedInt(value, -1, 1, 30));
        return;
    }

    // Proxy changes must go through the atomic validator, never the generic QML API.
    if (key.startsWith("App.Network.RegionalProxy")) return;
    if (key == "App.Player.DanmakuImplementation") {
        setDanmakuImplementation(value.toString());
        return;
    }
    if (key == "App.Player.DanmakuOn") {
        setDanmakuEnabled(savedBool(value, danmakuEnabled()));
        return;
    }
    if (key == "App.Player.DanmakuMergeSimilar") {
        setDanmakuMergeSimilar(savedBool(value, danmakuMergeSimilar()));
        return;
    }
    if (key == "App.Player.DanmakuOpacity") {
        bool ok = false;
        const int number = value.toString().toInt(&ok);
        if (ok) setDanmakuOpacity(number);
        return;
    }
    if (key == "App.Player.DanmakuFontSize") {
        bool ok = false;
        const int number = value.toString().toInt(&ok);
        if (ok) setDanmakuFontSize(number);
        return;
    }
    if (key == "App.Player.DanmakuArea") {
        bool ok = false;
        const int number = value.toString().toInt(&ok);
        if (ok) setDanmakuArea(number);
        return;
    }
    if (key == "App.Player.DanmakuDensity") {
        bool ok = false;
        const int number = value.toString().toInt(&ok);
        if (ok) setDanmakuDensity(number);
        return;
    }
    d->settings->setValue(key, value);
}
