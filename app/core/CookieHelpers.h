#ifndef COOKIE_HELPERS_H
#define COOKIE_HELPERS_H

#include <QString>
#include <QStringList>

// cookie 串键值提取(bilibili.cookie.txt 原文)。与原 C# 正则
// (?:^|;\s*)name=([^;]+)(忽略大小写)同口径:按 ';' 分段、键名容忍两侧空白、
// 值非空才算命中;缺席返回空串。HeartbeatApi 的 bili_jct 与后续 BangumiApi
// 的 DedeUserID 提取都走这里。
namespace CookieHelpers {

inline QString tryGetCookieValue(const QString &cookie, const QString &name) {
    const QStringList segments = cookie.split(';');
    for (const QString &segment : segments) {
        const int equals = segment.indexOf('=');
        if (equals <= 0) continue;
        const QString key = segment.left(equals).trimmed();
        if (key.compare(name, Qt::CaseInsensitive) != 0) continue;
        const QString value = segment.mid(equals + 1);
        if (value.isEmpty()) continue;  // 原正则 [^;]+ 要求值非空
        return value;
    }
    return QString();
}

}  // namespace CookieHelpers

#endif  // COOKIE_HELPERS_H
