// Real LoginPage + production controller, fixture transport/path, no real credentials or API.
#include "controllers/LoginController.h"
#include "core/AppPaths.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <memory>
#include <cstring>

static QString fixtureCookiePath;
QString AppPaths::cookiePath() { return fixtureCookiePath; }

class FixtureReply final : public QNetworkReply {
public:
    explicit FixtureReply(const QByteArray &body) : body_(body) { open(QIODevice::ReadOnly); setFinished(true); }
    void abort() override {}
    qint64 readData(char *data, qint64 size) override {
        const qint64 count = qMin(size, qint64(body_.size()) - offset_);
        if (count <= 0) return -1;
        memcpy(data, body_.constData() + offset_, size_t(count)); offset_ += count; return count;
    }
private:
    QByteArray body_;
    qint64 offset_ = 0;
};
class FixtureLogin final : public LoginController {
public:
    int requests = 0;
    bool validSession = true;
    QString endpoint;
protected:
    void get(const QUrl &url, const QString &, std::function<void(QNetworkReply *)> done) override {
        ++requests; endpoint = url.path();
        QJsonObject body{{"code", validSession ? 0 : -101},
            {"data", QJsonObject{{"isLogin", validSession}, {"mid", 123456789012LL}}}};
        FixtureReply reply(QJsonDocument(body).toJson());
        done(&reply);
    }
};
static QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems()) if (auto *found = visualChild(child, name)) return found;
    return nullptr;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    QTemporaryDir fixture(QDir::currentPath() + "/login-page-XXXXXX");
    if (!fixture.isValid()) return 1;
    fixtureCookiePath = fixture.path() + "/bilibili.cookie.txt";
    FixtureLogin controller;
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.rootContext()->setContextProperty("LoginController", &controller);
    int warnings = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        warnings += errors.size();
        for (const auto &error : errors) qWarning().noquote() << error.toString();
    });
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(LOGIN_QML_ROOT) + "/pages/LoginPage.qml"));
    std::unique_ptr<QObject> object(component.create());
    auto *page = qobject_cast<QQuickItem *>(object.get());
    if (!page) { qCritical() << component.errors(); return 1; }
    QQuickWindow window;
    window.resize(640, 720);
    page->setParentItem(window.contentItem());
    page->setSize(QSizeF(640, 720));
    window.show();
    auto settle = [] { QEventLoop loop; QTimer::singleShot(80, &loop, &QEventLoop::quit); loop.exec(); };
    settle();
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        qInfo() << (ok ? "PASS" : "FAIL") << name;
        if (!ok) ++failures;
    };
    check(controller.needsLogin() && controller.requests == 0, "first launch loads without credentials or network");
    auto *input = visualChild(page, "loginCookieInput");
    auto *importButton = visualChild(page, "loginImportButton");
    auto *skipButton = visualChild(page, "loginSkipButton");
    check(input && importButton && skipButton, "login entries instantiate");
    if (!input || !importButton || !skipButton) return 1;
    check(!importButton->isEnabled(), "empty import is disabled");
    QMetaObject::invokeMethod(skipButton, "clicked");
    check(controller.requests == 0 && controller.needsLogin(), "skip does not authenticate or contact API");
    int changed = 0, authenticated = 0;
    QObject::connect(&controller, &LoginController::needsLoginChanged, [&] { ++changed; });
    QObject::connect(&controller, &LoginController::authenticated, [&] { ++authenticated; });
    input->setProperty("text", "SESSDATA=fixture");
    settle();
    check(importButton->isEnabled(), "pasted cookie enables import");
    QMetaObject::invokeMethod(importButton, "clicked");
    check(input->property("text").toString().isEmpty(), "credential input cleared after import");
    check(!controller.needsLogin() && changed == 1 && authenticated == 1,
          "successful save updates startup state and emits authentication");
    check(controller.endpoint == "/x/web-interface/nav", "imports require nav validation");
    auto saved = [&] { QFile file(fixtureCookiePath); if (!file.open(QIODevice::ReadOnly)) return QByteArray(); return file.readAll(); };
    const QByteArray initial = saved();
    check(initial.contains("DedeUserID=123456789012"), "minimal imports gain verified long user ID");
    controller.importText("SESSDATA=replacement; DedeUserID=999");
    check(saved() == initial && !controller.error().isEmpty(), "identity mismatch preserves prior credentials");
    controller.validSession = false;
    controller.importText("SESSDATA=expired");
    check(saved() == initial && authenticated == 1, "expired session cannot overwrite credentials");
    FixtureLogin restarted;
    check(!restarted.needsLogin() && restarted.requests == 0, "existing credential bypasses onboarding without network");
    page->setSize(QSizeF(480, 500)); settle();
    check(warnings == 0, "login page has no QML binding or property errors");
    object.reset();
    return failures ? 1 : 0;
}
