#include "controllers/AppController.h"

#include <QCoreApplication>
#include <QImageReader>

AppController::AppController(QObject *parent)
    : QObject(parent), preferences_(AppPreferences::instance()) {
    // 偏好层信号透传给 QML 属性通知
    connect(preferences_, &AppPreferences::themeChanged, this,
            &AppController::themeChanged);
    connect(preferences_, &AppPreferences::languageChanged, this,
            &AppController::languageChanged);
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
