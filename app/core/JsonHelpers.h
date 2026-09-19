#ifndef JSON_HELPERS_H
#define JSON_HELPERS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <cmath>

// 容错 JSON 读取(对齐原 JsonElementHelpers):缺字段/类型不符一律回退 空串/0/false/NaN,
// 不抛异常。NaN 语义 = 字段缺席(如未评分)。
namespace jh {

inline QString getString(const QJsonObject &obj, const QString &key) {
    const QJsonValue v = obj.value(key);
    if (v.isString()) return v.toString();
    if (v.isDouble()) {
        // 数字按原始文本语义返回(qint64 范围内无损)
        return QString::number(v.toVariant().toLongLong());
    }
    return QString();
}

inline qint64 getInt64(const QJsonObject &obj, const QString &key) {
    const QJsonValue v = obj.value(key);
    if (v.isDouble()) return v.toVariant().toLongLong();
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().toLongLong(&ok);
        if (ok) return n;
    }
    return 0;
}

inline double getDouble(const QJsonObject &obj, const QString &key) {
    const QJsonValue v = obj.value(key);
    if (v.isDouble()) return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok) return d;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

inline bool getBool(const QJsonObject &obj, const QString &key) {
    const QJsonValue v = obj.value(key);
    if (v.isBool()) return v.toBool();
    if (v.isDouble()) return v.toVariant().toLongLong() != 0;
    if (v.isString()) {
        const QString s = v.toString();
        return s.compare("true", Qt::CaseInsensitive) == 0;
    }
    return false;
}

inline QJsonObject getChild(const QJsonObject &obj, const QString &key) {
    const QJsonValue v = obj.value(key);
    return v.isObject() ? v.toObject() : QJsonObject();
}

inline QJsonArray getChildArray(const QJsonObject &obj, const QString &key) {
    const QJsonValue v = obj.value(key);
    return v.isArray() ? v.toArray() : QJsonArray();
}

// 变参便捷包装:首个非空(按 trimmed 判定)
inline QString firstNonEmpty(const QString &a = {}, const QString &b = {},
                             const QString &c = {}, const QString &d = {},
                             const QString &e = {}) {
    if (!a.trimmed().isEmpty()) return a;
    if (!b.trimmed().isEmpty()) return b;
    if (!c.trimmed().isEmpty()) return c;
    if (!d.trimmed().isEmpty()) return d;
    if (!e.trimmed().isEmpty()) return e;
    return QString();
}

inline QString firstArrayString(const QJsonObject &obj, const QString &key) {
    const QJsonArray arr = getChildArray(obj, key);
    for (const QJsonValue &entry : arr) {
        if (entry.isString() && !entry.toString().trimmed().isEmpty())
            return entry.toString();
    }
    return QString();
}

// 数组内对象逐个尝试候选字段名(文档未展开的字段形态)
inline QString firstArrayObjectString(const QJsonObject &obj, const QString &key,
                                      std::initializer_list<const char *> candidates) {
    const QJsonArray arr = getChildArray(obj, key);
    for (const QJsonValue &entry : arr) {
        if (!entry.isObject()) continue;
        for (const char *candidate : candidates) {
            const QString value = getString(entry.toObject(), candidate);
            if (!value.trimmed().isEmpty()) return value;
        }
    }
    return QString();
}

// 图片 URL 宽松归一化:协议相对/明文 http 升级 https(B 站图床全支持 https)
inline QString normalizeImageUrlLenient(const QString &url) {
    if (url.trimmed().isEmpty()) return QString();
    if (url.startsWith("//")) return "https:" + url;
    if (url.startsWith("http://", Qt::CaseInsensitive))
        return "https://" + url.mid(7);
    return url;
}

}  // namespace jh

#endif  // JSON_HELPERS_H
