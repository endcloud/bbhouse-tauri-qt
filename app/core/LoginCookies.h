#ifndef LOGIN_COOKIES_H
#define LOGIN_COOKIES_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

// Pure credential conversion + atomic persistence, shared by login and offline tests.
namespace LoginCookies {
QString normalize(const QString &input);
bool isUsable(const QString &cookie);
QString fromResponse(const QList<QByteArray> &setCookies, const QUrl &crossDomainUrl);
bool save(const QString &path, const QString &cookie);
}
#endif
