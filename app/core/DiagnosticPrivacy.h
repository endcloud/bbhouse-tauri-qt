#ifndef DIAGNOSTIC_PRIVACY_H
#define DIAGNOSTIC_PRIVACY_H

#include <QRegularExpression>
#include <QString>

// API errors are shown in the UI and may be persisted in history sync_runs.
// Qt and remote servers can echo request URLs or headers in their messages.
inline QString privateDiagnostic(QString message) {
    static const QRegularExpression headers(
        QStringLiteral(R"((?:Cookie|Set-Cookie|Authorization|Proxy-Authorization)["']?\s*:\s*[^\r\n]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression credentials(
        QStringLiteral(R"(\b(SESSDATA|bili_jct|DedeUserID|DedeUserID__ckMd5|access_token|refresh_token)["']?\s*[=:]\s*["']?[^\s;,"'<>]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression urls(
        QStringLiteral(R"(\b(?:https?|socks5?)://[^\s<>"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    message.replace(headers, QStringLiteral("[redacted header]"));
    message.replace(credentials, QStringLiteral("\\1=[redacted]"));
    message.replace(urls, QStringLiteral("[redacted URL]"));
    return message;
}

#endif
