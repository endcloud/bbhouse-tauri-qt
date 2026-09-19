#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "preferences/AppPreferences.h"

// QML 桥接:外观主题(亮/暗/跟随系统)与语言偏好 + 应用信息。
// theme 即时生效的口径:applyTheme 写穿 AppPreferences,QML 侧(FluTheme.darkMode)
// 绑定 AppController.theme 完成切换(0/1/2 枚举见 3rd/FluentUI/Def.h FluThemeType)。
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    // 封面 CDN 转码后缀:按本机 Qt 图像插件能力选择(.avif/.webp/空=回源 jpeg)
    Q_PROPERTY(QString imageTranscodeSuffix READ imageTranscodeSuffix CONSTANT)
    // 本机 Qt 可解码的图像格式清单(小写扩展名),供 QML 侧对 webp 等
    // 不支持格式追加 CDN 转格式后缀
    Q_PROPERTY(QStringList decodableImageFormats READ decodableImageFormats CONSTANT)
   public:
    explicit AppController(QObject *parent = nullptr);

    // "system" | "light" | "dark"
    QString theme() const;
    void setTheme(const QString &value);

    // "system" | "zh_CN" | "en_US"(语言切换重启生效,见 ui-localization 规格)
    QString language() const;
    void setLanguage(const QString &value);

    QString appVersion() const;

    QString imageTranscodeSuffix() const;

    QStringList decodableImageFormats() const;

    // QML 入口:持久化 + 经 themeChanged 驱动 FluTheme.darkMode 即时生效
    Q_INVOKABLE void applyTheme(const QString &theme);

    Q_INVOKABLE void openUserSpace(const QString &mid, const QString &name,
                                   const QString &faceUrl);

   signals:
    void userSpaceRequested(QString mid, QString name, QString faceUrl);
    void themeChanged();
    void languageChanged();

   private:
    static bool isValidTheme(const QString &value);

    AppPreferences *preferences_;
};

#endif  // APP_CONTROLLER_H
