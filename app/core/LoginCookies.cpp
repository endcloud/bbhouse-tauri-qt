#include "core/LoginCookies.h"
#include "core/CookieHelpers.h"
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>

QString LoginCookies::normalize(const QString &input) {
    QString text = input.trimmed();
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    if (text.startsWith("Cookie:", Qt::CaseInsensitive)) text = text.mid(7).trimmed();
    if (text.size() > 1024 * 1024) return {};
    // Accept browser request headers and one key=value per line. Reject all
    // remaining controls so imported text cannot inject another HTTP header.
    text.replace(QRegularExpression("[\\r\\n]+"), ";");
    QStringList result;
    for (const QString &part : text.split(';', Qt::SkipEmptyParts)) {
        const QString pair = part.trimmed();
        if (pair.isEmpty()) continue;
        const int equal = pair.indexOf('=');
        if (equal <= 0) return {};
        const QString key = pair.left(equal).trimmed();
        const QString value = pair.mid(equal + 1).trimmed();
        static const QRegularExpression keyPattern("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$");
        if (!keyPattern.match(key).hasMatch()) return {};
        for (QChar c : value)
            if (c.unicode() < 0x20 || c.unicode() >= 0x7f) return {};
        result.append(key + '=' + value);
    }
    return result.join("; ");
}

bool LoginCookies::isUsable(const QString &cookie) {
    const QString clean = normalize(cookie);
    return !clean.isEmpty() && !CookieHelpers::tryGetCookieValue(clean, "SESSDATA").isEmpty();
}

QString LoginCookies::fromResponse(const QList<QByteArray> &setCookies, const QUrl &url) {
    QMap<QString, QString> values;
    for (const QByteArray &header : setCookies) {
        for (const auto &cookie : QNetworkCookie::parseCookies(header)) {
            const QString domain = cookie.domain().toLower();
            if (!domain.isEmpty() && domain != "bilibili.com" && domain != ".bilibili.com"
                    && !domain.endsWith(".bilibili.com")) continue;
            values.insert(QString::fromLatin1(cookie.name()), QString::fromLatin1(cookie.value()));
        }
    }
    auto render = [&values] {
        QStringList parts;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (!it.value().isEmpty()) parts << it.key() + '=' + it.value();
        return normalize(parts.join("; "));
    };
    QString candidate = render();
    if (isUsable(candidate)) return candidate;
    // Old response URL carries credential fields, never visit this URL or log it.
    if (url.scheme() != "https" || (url.host() != "passport.biligame.com"
                                   && url.host() != "passport.bilibili.com")) return {};
    const QUrlQuery query(url);
    for (const QString &name : {QString("SESSDATA"), QString("bili_jct"), QString("DedeUserID"),
                                QString("DedeUserID__ckMd5"), QString("sid")}) {
        const QString value = query.queryItemValue(name, QUrl::FullyDecoded);
        if (!value.isEmpty()) values.insert(name, value);
    }
    candidate = render();
    return isUsable(candidate) ? candidate : QString();
}

bool LoginCookies::save(const QString &path, const QString &cookie) {
    const QString clean = normalize(cookie);
    if (!isUsable(clean) || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) return false;
    const QByteArray data = clean.toUtf8() + '\n';
    if (file.write(data) != data.size()) return false;
    return file.commit();
}
