#include "preferences/AppPreferences.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QProcess>
#include <QDebug>
#include <QFile>

namespace {
QString snapshot(AppPreferences *prefs) {
    return QStringList{prefs->danmakuImplementation(), QString::number(prefs->danmakuEnabled()),
                       QString::number(prefs->danmakuOpacity()), QString::number(prefs->danmakuFontSize()),
                       QString::number(prefs->danmakuArea()), QString::number(prefs->danmakuDensity()),
                       QString::number(prefs->danmakuMergeSimilar())}.join(',');
}
QString proxySnapshot(AppPreferences *prefs) {
    return QStringList{prefs->proxyType(), prefs->proxyHost(), QString::number(prefs->proxyPort()),
                       prefs->proxyUsername()}.join(',');
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 5) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromLocal8Bit(argv[1]));
        auto *prefs = AppPreferences::instance();
        return snapshot(prefs) == QString::fromLatin1(argv[2])
            && proxySnapshot(prefs) == QString::fromUtf8(argv[3])
            && prefs->theme() + "," + prefs->language() == QString::fromUtf8(argv[4])
            && prefs->proxyPassword().isEmpty() ? 0 : 1;
    }
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/preferences-XXXXXX");
    if (!temp.isValid()) return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QSettings saved(QSettings::IniFormat, QSettings::UserScope, "shizi", "bbhouse-qt");
    int failures = 0;
    auto check = [&](bool ok, const char *name) { qInfo() << (ok ? "PASS" : "FAIL") << name; failures += !ok; };
    auto startup = [&](const QString &danmaku, const QString &proxy, const QString &appearance) {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {temp.path(), danmaku, proxy, appearance});
        return child.waitForFinished(5000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
    };
    check(startup("sprite,1,100,25,25,0,1", "http,localhost,7890,", "system,system"),
          "fresh settings use release presets");
    saved.sync();
    check(saved.allKeys().isEmpty(), "reading fresh defaults does not populate or rewrite settings");

    // Explicit old defaults are still user choices, not candidates for migration.
    saved.setValue("app/theme", "dark");
    saved.setValue("app/language", "en_US");
    saved.setValue("App.Player.DanmakuImplementation", "scene");
    saved.setValue("App.Player.DanmakuOn", false);
    saved.setValue("App.Player.DanmakuOpacity", 75);
    saved.setValue("App.Player.DanmakuFontSize", 32);
    saved.setValue("App.Player.DanmakuArea", 100);
    saved.setValue("App.Player.DanmakuDensity", 40);
    saved.setValue("App.Player.DanmakuMergeSimilar", false);
    saved.setValue("App.Network.RegionalProxy", QVariantMap{{"type", "none"},
                   {"host", "localhost"}, {"port", 7897}});
    saved.sync();
    auto settingsBytes = [&] {
        QFile file(saved.fileName());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const auto beforeStartup = settingsBytes();
    check(startup("scene,0,75,32,100,40,0", "none,localhost,7897,", "dark,en_US"),
          "existing preferences including old defaults survive a fresh process");
    check(!beforeStartup.isEmpty() && settingsBytes() == beforeStartup,
          "startup leaves existing settings file byte-for-byte unchanged");
    saved.clear();
    // Read corrupted/obsolete persisted values without silently rewriting preferences on startup.
    saved.setValue("App.Player.DanmakuImplementation", "removed-mode");
    saved.setValue("App.Player.DanmakuOn", "invalid");
    saved.setValue("App.Player.DanmakuOpacity", -5);
    saved.setValue("App.Player.DanmakuFontSize", "large");
    saved.setValue("App.Player.DanmakuArea", 60);
    saved.setValue("App.Player.DanmakuDensity", 30);
    saved.setValue("App.Player.DanmakuMergeSimilar", "invalid");
    saved.setValue("App.Network.RegionalProxy", QVariantMap{{"type", "unsupported"},
                   {"host", "http://not-a-bare-host/path"}, {"port", 70000}});
    saved.sync();
    auto *prefs = AppPreferences::instance();
    int implementation = 0, enabled = 0, opacity = 0, fontSize = 0, area = 0, density = 0, merge = 0;
    auto persisted = [&] {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {temp.path(), snapshot(prefs), proxySnapshot(prefs), prefs->theme() + "," + prefs->language()});
        return child.waitForFinished(5000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
    };
    QObject::connect(prefs, &AppPreferences::danmakuImplementationChanged, [&] { ++implementation; });
    QObject::connect(prefs, &AppPreferences::danmakuEnabledChanged, [&] { ++enabled; });
    QObject::connect(prefs, &AppPreferences::danmakuOpacityChanged, [&] { ++opacity; });
    QObject::connect(prefs, &AppPreferences::danmakuFontSizeChanged, [&] { ++fontSize; });
    QObject::connect(prefs, &AppPreferences::danmakuAreaChanged, [&] { ++area; });
    QObject::connect(prefs, &AppPreferences::danmakuDensityChanged, [&] { ++density; });
    QObject::connect(prefs, &AppPreferences::danmakuMergeSimilarChanged, [&] { ++merge; });
    check(snapshot(prefs) == "sprite,1,100,25,25,0,1", "invalid saved values use safe defaults");
    saved.sync();
    check(saved.value("App.Player.DanmakuOpacity").toInt() == -5
              && saved.value("App.Player.DanmakuOn").toString() == "invalid",
          "initial reads do not write preferences");
    check(persisted(), "fresh process uses the same fallback defaults");

    prefs->setDanmakuImplementation("unknown");
    check(implementation == 0 && prefs->danmakuImplementation() == "sprite", "invalid implementation write ignored");
    for (const QString &mode : {QStringLiteral("scene"), QStringLiteral("sprite"), QStringLiteral("scene")}) {
        prefs->setDanmakuImplementation(mode);
        const int emitted = implementation;
        prefs->setDanmakuImplementation(mode);
        check(implementation == emitted && prefs->danmakuImplementation() == mode, "one notification per implementation selection");
        check(persisted(), "fresh process reads selected implementation");
    }

    prefs->setDanmakuEnabled(false);
    prefs->setDanmakuOpacity(-10);
    prefs->setDanmakuFontSize(100);
    prefs->setDanmakuArea(100);
    prefs->setDanmakuDensity(40);
    prefs->setDanmakuMergeSimilar(false);
    check(snapshot(prefs) == "scene,0,0,48,100,40,0", "typed writes apply and numeric sliders clamp");
    check(enabled == 1 && opacity == 1 && fontSize == 1 && area == 1 && density == 1 && merge == 1,
          "each setting emits its own change notification");
    check(persisted(), "all typed settings persist immediately across processes");
    prefs->setDanmakuEnabled(false);
    prefs->setDanmakuOpacity(-20);
    prefs->setDanmakuFontSize(80);
    prefs->setDanmakuArea(100);
    prefs->setDanmakuDensity(40);
    prefs->setDanmakuMergeSimilar(false);
    check(enabled == 1 && opacity == 1 && fontSize == 1 && area == 1 && density == 1 && merge == 1,
          "identical effective settings do not emit duplicate notifications");
    prefs->setDanmakuArea(75);
    prefs->setDanmakuDensity(-1);
    prefs->setDanmakuDensity(19);
    prefs->setDanmakuDensity(120);
    check(area == 1 && density == 1, "invalid discrete choices are ignored");

    prefs->setValue("App.Player.DanmakuImplementation", "sprite");
    prefs->setValue("App.Player.DanmakuOn", "true");
    prefs->setValue("App.Player.DanmakuOpacity", 120);
    prefs->setValue("App.Player.DanmakuFontSize", 0);
    prefs->setValue("App.Player.DanmakuArea", 50);
    prefs->setValue("App.Player.DanmakuDensity", 100);
    prefs->setValue("App.Player.DanmakuMergeSimilar", true);
    check(snapshot(prefs) == "sprite,1,100,12,50,100,1", "generic API routes through typed validation");
    check(implementation == 4 && enabled == 2 && opacity == 2 && fontSize == 2 && area == 2 && density == 2 && merge == 2,
          "legacy writes notify typed bindings");
    check(persisted(), "generic writes also persist immediately");
    prefs->setValue("App.Player.DanmakuOn", "invalid");
    prefs->setValue("App.Player.DanmakuOpacity", "invalid");
    prefs->setValue("App.Player.DanmakuFontSize", 15.5);
    prefs->setValue("App.Player.DanmakuArea", 40);
    prefs->setValue("App.Player.DanmakuDensity", 30);
    prefs->setValue("App.Player.DanmakuMergeSimilar", "invalid");
    check(snapshot(prefs) == "sprite,1,100,12,50,100,1", "malformed generic writes preserve current settings");
    check(enabled == 2 && opacity == 2 && fontSize == 2 && area == 2 && density == 2 && merge == 2,
          "rejected generic writes emit no notifications");
    for (const int choice : {25, 50, 100}) prefs->setDanmakuArea(choice);
    for (const int choice : {0, 20, 40, 60, 80, 100}) prefs->setDanmakuDensity(choice);
    prefs->setDanmakuDensity(0);
    prefs->setDanmakuFontSize(25);
    check(prefs->danmakuArea() == 100 && prefs->danmakuDensity() == 0 && persisted(),
          "all discrete choices and unlimited density remain persistable");

    int proxyChanges = 0;
    QObject::connect(prefs, &AppPreferences::proxySettingsChanged, [&] { ++proxyChanges; });
    check(proxySnapshot(prefs) == "http,localhost,7890,", "invalid saved proxy uses local defaults");
    const auto defaultProxy = prefs->regionalProxy();
    check(defaultProxy.type() == QNetworkProxy::HttpProxy && defaultProxy.hostName() == "localhost"
              && defaultProxy.port() == 7890, "default proxy snapshot is explicit HTTP endpoint");
    check(prefs->saveProxySettings("socks5", " 127.0.0.1 ", 1080, "fixture-user", "fixture-password")
              && proxyChanges == 1 && persisted(), "proxy record persists together; password is session-only");
    const auto snapshotProxy = prefs->regionalProxy();
    check(snapshotProxy.type() == QNetworkProxy::Socks5Proxy && snapshotProxy.hostName() == "127.0.0.1"
              && snapshotProxy.port() == 1080 && snapshotProxy.user() == "fixture-user"
              && snapshotProxy.password() == "fixture-password", "snapshot carries configured authentication");
    QFile ini(saved.fileName());
    check(ini.open(QIODevice::ReadOnly) && !ini.readAll().contains("fixture-password"),
          "proxy password is never written to preferences file");
    check(prefs->saveProxySettings("socks5", "127.0.0.1", 1080, "fixture-user", "fixture-password")
              && proxyChanges == 1, "identical proxy save does not notify twice");
    const QString beforeProxy = proxySnapshot(prefs);
    for (const QString &host : {QString(), QStringLiteral("https://localhost"), QStringLiteral("localhost:7897"),
                               QStringLiteral("user@localhost"), QStringLiteral("local host"), QStringLiteral("localhost/path")})
        check(!prefs->saveProxySettings("http", host, 7897, {}, {}), "non-host proxy input is rejected");
    check(!prefs->saveProxySettings("https", "localhost", 7897, {}, {})
              && !prefs->saveProxySettings("http", "localhost", 0, {}, {})
              && !prefs->saveProxySettings("http", "localhost", 65536, {}, {})
              && !prefs->saveProxySettings("http", "localhost", 7897, {}, "orphan-password")
              && proxySnapshot(prefs) == beforeProxy && prefs->proxyPassword() == "fixture-password"
              && proxyChanges == 1 && persisted(), "invalid record leaves all old proxy values intact");
    prefs->setValue("App.Network.RegionalProxy", QVariantMap{{"type", "none"}});
    check(proxySnapshot(prefs) == beforeProxy && proxyChanges == 1, "generic setter cannot bypass proxy validation");
    check(prefs->saveProxySettings("http", "::1", 1, {}, {}) && persisted()
              && prefs->regionalProxy().hostName() == "::1" && prefs->proxyPassword().isEmpty(),
          "IPv6 host and minimum port accepted; authentication can be cleared");
    check(prefs->saveProxySettings("none", "localhost", 65535, {}, {}) && persisted()
              && prefs->regionalProxy().type() == QNetworkProxy::NoProxy,
          "disabled configuration creates explicit direct proxy, including maximum port");
    check(snapshotProxy.type() == QNetworkProxy::Socks5Proxy && snapshotProxy.port() == 1080,
          "existing request snapshot is independent of later settings edits");
    return failures ? 1 : 0;
}
