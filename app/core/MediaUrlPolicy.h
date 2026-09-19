#ifndef MEDIA_URL_POLICY_H
#define MEDIA_URL_POLICY_H

#include <QSet>
#include <QStringList>
#include <QUrl>
#include <algorithm>

// 只重排接口实际下发的 URL，不改写主机/查询参数，也不输出带签名的地址。
namespace MediaUrlPolicy {
inline bool valid(const QString &text) {
    for (const QChar ch : text) {
        if (ch.isSpace() || ch.unicode() < 0x20 || ch == QChar(0x7f)) return false;
    }
    const QUrl url(text, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
        && url.fragment().isEmpty() && (url.scheme() == "https" || url.scheme() == "http");
}

inline int priority(const QString &text) {
    QString host = QUrl(text).host().toLower();
    if (host.endsWith('.')) host.chop(1);
    const auto under = [&host](const QString &domain) {
        return host == domain || host.endsWith('.' + domain);
    };
    // szbdyd 也是 PCDN；不能只识别 mcdn，更不能检查整个 URL 的查询参数。
    if (host.contains("mcdn") || host.contains("pcdn") || under("szbdyd.com")) return 2;

    // 文档中已知的云厂商 UPOS 节点。未知节点保留为普通候选，不凭端口推断 P2P。
    if (host.startsWith("upos-") && (under("bilivideo.com") || under("bilivideo.cn")
                                     || under("akamaized.net"))) {
        const QString label = host.section('.', 0, 0);
        for (const char *provider : {"cos", "hw", "oss", "ali", "kodo", "ks3", "bos", "zos", "akam"}) {
            if (label.contains(QStringLiteral("-mirror") + provider)
                || label.contains(QStringLiteral("-estg") + provider)) return 0;
        }
    }
    return 1;
}

inline QStringList ordered(const QStringList &urls) {
    QStringList result;
    QSet<QString> seen;
    for (const QString &url : urls) {
        if (valid(url) && !seen.contains(url)) {
            seen.insert(url);
            result.append(url);
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        return priority(a) < priority(b);
    });
    return result;
}
} // namespace MediaUrlPolicy

#endif // MEDIA_URL_POLICY_H
