#include "core/LoginCookies.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool result, const char *name) {
        if (!result) { qWarning() << "FAIL" << name; ++failures; }
    };
    check(LoginCookies::normalize("Cookie: SESSDATA=fixture;\r\nbili_jct=csrf")
                  == "SESSDATA=fixture; bili_jct=csrf", "normalize header and newlines");
    check(!LoginCookies::isUsable("foo=SESSDATA=spoof"), "require session cookie key");
    check(!LoginCookies::isUsable("SESSDATA="), "reject empty session");
    check(!LoginCookies::isUsable("SESSDATA=fixture\r\nInjected: unsafe"), "reject header injection");
    check(LoginCookies::normalize(QString("SESSDATA=fi") + QChar(1) + "xture").isEmpty(), "reject control character");
    const auto headerCookie = LoginCookies::fromResponse({
        "SESSDATA=test%2Cvalue; Domain=.bilibili.com; Path=/; HttpOnly",
        "bili_jct=csrf; Domain=.bilibili.com; Path=/", "SESSDATA=evil; Domain=evil.test"}, {});
    check(headerCookie.contains("SESSDATA=test%2Cvalue"), "preserve encoded header and exclude other domain");
    check(!headerCookie.contains("HttpOnly") && !headerCookie.contains("Path="), "strip cookie attributes");
    const QUrl legacy("https://passport.biligame.com/crossDomain?SESSDATA=test%252Cvalue&bili_jct=csrf&gourl=untrusted");
    check(LoginCookies::fromResponse({}, legacy) == "SESSDATA=test%2Cvalue; bili_jct=csrf", "legacy URL decode once and allowlist");
    check(LoginCookies::fromResponse({}, QUrl("https://evil.test/?SESSDATA=fixture")).isEmpty(), "reject unrelated fallback host");
    check(LoginCookies::fromResponse({}, QUrl("https://passport.biligame.com/?ticket=fixture")).isEmpty(), "ticket is not credential");
    check(LoginCookies::fromResponse({"SESSDATA=header; Domain=.bilibili.com"}, legacy)
          == "SESSDATA=header", "prefer response headers");

    // All fixtures stay under the test working directory (project build tree).
    QTemporaryDir dir(QDir::currentPath() + "/login-test-XXXXXX");
    check(dir.isValid(), "create isolated test directory");
    const QString path = dir.path() + "/nested/bilibili.cookie.txt";
    check(LoginCookies::save(path, "SESSDATA=original"), "persist initial credential");
    check(!LoginCookies::save(path, "SESSDATA="), "reject invalid replacement");
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read fixture credential");
    check(file.readAll() == "SESSDATA=original\n", "failed save preserves old credential");
    file.close();
    check(LoginCookies::save(path, "SESSDATA=replacement; bili_jct=csrf"), "replace credential atomically");
    check(file.open(QIODevice::ReadOnly), "read replacement");
    check(file.readAll() == "SESSDATA=replacement; bili_jct=csrf\n", "replacement format matches client reader");
#ifndef Q_OS_WIN
    check(!(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)), "credential private permissions");
#endif
    file.close();
    check(!LoginCookies::save(dir.path(), "SESSDATA=fixture"), "write failure returns false");
    if (failures == 0) qInfo() << "login cookie regression passed";
    return failures ? 1 : 0;
}
