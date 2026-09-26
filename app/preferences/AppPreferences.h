#ifndef APP_PREFERENCES_H
#define APP_PREFERENCES_H

#include <QObject>
#include <QVariant>
#include <QNetworkProxy>
#include <memory>

// 应用偏好持久化(QSettings IniFormat:%APPDATA%/shizi/bbhouse-qt.ini)。
// 对齐原 WinUI 项目的 AppPreferences 语义:主题/语言即时写穿,存储值即末次状态。
class AppPreferences : public QObject {
    Q_OBJECT
    Q_PROPERTY(int pageCacheMinutes READ pageCacheMinutes WRITE setPageCacheMinutes NOTIFY pageCacheMinutesChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString danmakuImplementation READ danmakuImplementation WRITE setDanmakuImplementation NOTIFY danmakuImplementationChanged)
    Q_PROPERTY(bool danmakuEnabled READ danmakuEnabled WRITE setDanmakuEnabled NOTIFY danmakuEnabledChanged)
    Q_PROPERTY(int danmakuOpacity READ danmakuOpacity WRITE setDanmakuOpacity NOTIFY danmakuOpacityChanged)
    Q_PROPERTY(int danmakuFontSize READ danmakuFontSize WRITE setDanmakuFontSize NOTIFY danmakuFontSizeChanged)
    Q_PROPERTY(int danmakuArea READ danmakuArea WRITE setDanmakuArea NOTIFY danmakuAreaChanged)
    Q_PROPERTY(int danmakuDensity READ danmakuDensity WRITE setDanmakuDensity NOTIFY danmakuDensityChanged)
    Q_PROPERTY(bool danmakuMergeSimilar READ danmakuMergeSimilar WRITE setDanmakuMergeSimilar NOTIFY danmakuMergeSimilarChanged)
    Q_PROPERTY(QString proxyType READ proxyType NOTIFY proxySettingsChanged)
    Q_PROPERTY(QString proxyHost READ proxyHost NOTIFY proxySettingsChanged)
    Q_PROPERTY(int proxyPort READ proxyPort NOTIFY proxySettingsChanged)
    Q_PROPERTY(QString proxyUsername READ proxyUsername NOTIFY proxySettingsChanged)
    Q_PROPERTY(QString proxyPassword READ proxyPassword NOTIFY proxySettingsChanged)
   public:
    static AppPreferences *instance();
    ~AppPreferences() override;

    int pageCacheMinutes() const;
    void setPageCacheMinutes(int value);

    // "system" | "zh_CN" | "en_US"
    QString language() const;
    void setLanguage(const QString &value);

    // "system" | "light" | "dark"
    QString theme() const;
    void setTheme(const QString &value);

    QString danmakuImplementation() const;
    void setDanmakuImplementation(const QString &value);

    bool danmakuEnabled() const;
    void setDanmakuEnabled(bool value);
    int danmakuOpacity() const;
    void setDanmakuOpacity(int value);
    int danmakuFontSize() const;
    void setDanmakuFontSize(int value);
    int danmakuArea() const;
    void setDanmakuArea(int value);
    int danmakuDensity() const;
    void setDanmakuDensity(int value);
    bool danmakuMergeSimilar() const;
    void setDanmakuMergeSimilar(bool value);

    QString proxyType() const;
    QString proxyHost() const;
    int proxyPort() const;
    QString proxyUsername() const;
    QString proxyPassword() const;
    // Call on the preferences object's thread; pass the value snapshot to API workers.
    QNetworkProxy regionalProxy() const;
    // The password is session-only. Other fields persist as one validated record.
    Q_INVOKABLE bool saveProxySettings(const QString &type, const QString &host, int port,
                                      const QString &username, const QString &password);

    // 通用键值存取(后继变更复用:倍速保持、弹幕开关等)
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);

   signals:
    void pageCacheMinutesChanged();
    void languageChanged();
    void themeChanged();
    void danmakuImplementationChanged();
    void danmakuEnabledChanged();
    void danmakuOpacityChanged();
    void danmakuFontSizeChanged();
    void danmakuAreaChanged();
    void danmakuDensityChanged();
    void danmakuMergeSimilarChanged();
    void proxySettingsChanged();

   private:
    explicit AppPreferences(QObject *parent = nullptr);
    class Private;
    std::unique_ptr<Private> d;
};

#endif  // APP_PREFERENCES_H
