#include "controllers/AppController.h"

#include <QCoreApplication>
#include <QEvent>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#include <QTimer>
#include <windows.h>
#include <xinput.h>
#endif

#ifdef Q_OS_WIN
class AppController::GamepadPoller : public QObject {
public:
    explicit GamepadPoller(AppController *owner) : QObject(owner), owner_(owner) {
        timer_.setInterval(16);
        connect(&timer_, &QTimer::timeout, this, &GamepadPoller::poll);
        timer_.start();
    }

private:
    void poll() {
        XINPUT_STATE state{};
        if (XInputGetState(0, &state) != ERROR_SUCCESS) {
            previous_ = 0;
            return;
        }
        const WORD changed = state.Gamepad.wButtons & ~previous_;
        previous_ = state.Gamepad.wButtons;
        auto emitButton = [this](int button) { emit owner_->gamepadButtonPressed(button); };
        if (changed & XINPUT_GAMEPAD_A) emitButton(0);
        if (changed & XINPUT_GAMEPAD_B) emitButton(1);
        if (changed & XINPUT_GAMEPAD_X) emitButton(2);
        if (changed & XINPUT_GAMEPAD_Y) emitButton(3);
        if (changed & XINPUT_GAMEPAD_START) emitButton(4);
        if (changed & XINPUT_GAMEPAD_BACK) emitButton(5);
        if (changed & XINPUT_GAMEPAD_DPAD_UP) emitButton(6);
        if (changed & XINPUT_GAMEPAD_DPAD_DOWN) emitButton(7);
        if (changed & XINPUT_GAMEPAD_DPAD_LEFT) emitButton(8);
        if (changed & XINPUT_GAMEPAD_DPAD_RIGHT) emitButton(9);
        if (changed & XINPUT_GAMEPAD_LEFT_SHOULDER) emitButton(10);
        if (changed & XINPUT_GAMEPAD_RIGHT_SHOULDER) emitButton(11);
    }

    AppController *owner_;
    QTimer timer_;
    WORD previous_ = 0;
};
#endif

AppController::AppController(QObject *parent)
    : QObject(parent), preferences_(AppPreferences::instance()) {
    if (auto *application = QCoreApplication::instance())
        application->installEventFilter(this);
#ifdef Q_OS_WIN
    gamepadPoller_ = new GamepadPoller(this);
#endif
    // 偏好层信号透传给 QML 属性通知
    connect(preferences_, &AppPreferences::themeChanged, this,
            &AppController::themeChanged);
    connect(preferences_, &AppPreferences::languageChanged, this,
            &AppController::languageChanged);
    QTimer::singleShot(0, this, &AppController::checkForUpdates);
}

bool AppController::eventFilter(QObject *, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::BackButton) {
            emit backRequested();
            return false;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Back) {
            emit backRequested();
            return false;
        }
    }
    return false;
}

QString AppController::theme() const { return preferences_->theme(); }

void AppController::setTheme(const QString &value) { applyTheme(value); }

QString AppController::language() const { return preferences_->language(); }

void AppController::setLanguage(const QString &value) {
    preferences_->setLanguage(value);
}

QString AppController::appVersion() const {
    return QCoreApplication::applicationVersion();
}

int AppController::versionCode() const {
    return BBHOUSE_VERSION_CODE;
}

void AppController::checkForUpdates() {
    QNetworkRequest request(QUrl(QStringLiteral("https://cos.endcloud.cn/bbhouse-qt/version.json")));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BBHouse/%1").arg(appVersion()));
    auto *reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray payload = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError
                        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        if (!ok) return;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return;
        const QJsonObject object = document.object();
        const int code = object.value(QStringLiteral("code")).toInt(0);
        const QUrl url(object.value(QStringLiteral("url")).toString().trimmed());
        if (code <= versionCode() || !url.isValid()
            || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))) {
            return;
        }
        remoteVersionCode_ = code;
        updateUrl_ = url.toString();
        updateAvailable_ = true;
        emit updateAvailableChanged();
    });
}

QString AppController::imageTranscodeSuffix() const {
    // 按本机 Qt 图像插件能力一次性选定:优先 avif(体积最小),其次 webp,
    // 均无则空串(CDN 无扩展名回源 jpeg,Qt 基座必支持)
    static const QString suffix = [] {
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        if (formats.contains("avif")) return QStringLiteral(".avif");
        if (formats.contains("webp")) return QStringLiteral(".webp");
        return QString();
    }();
    return suffix;
}

QStringList AppController::decodableImageFormats() const {
    static const QStringList formats = [] {
        QStringList out;
        const QList<QByteArray> supported = QImageReader::supportedImageFormats();
        out.reserve(supported.size());
        for (const QByteArray &format : supported) {
            out << QString::fromLatin1(format).toLower();
        }
        return out;
    }();
    return formats;
}

void AppController::applyTheme(const QString &theme) {
    if (!isValidTheme(theme)) return;
    preferences_->setTheme(theme);
}

bool AppController::isValidTheme(const QString &value) {
    return value == QLatin1String("system") || value == QLatin1String("light") ||
           value == QLatin1String("dark");
}

void AppController::openUserSpace(const QString &mid, const QString &name,
                                  const QString &faceUrl) {
    bool ok = false;
    const qint64 value = mid.toLongLong(&ok);
    if (!ok || value <= 0) return;
    emit userSpaceRequested(QString::number(value), name, faceUrl);
}
