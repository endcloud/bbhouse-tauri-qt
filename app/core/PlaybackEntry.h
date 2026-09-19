#ifndef PLAYBACK_ENTRY_H
#define PLAYBACK_ENTRY_H

#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantMap>
#include "core/JsonHelpers.h"

// 所有页面共用的播放标识恢复。历史表保留 rawJson，无需迁移 SQLite schema。
namespace PlaybackEntry {
inline bool isLocal(const QVariantMap &entry) {
    return entry.value("business").toString() == QLatin1String("local")
        || entry.value("isLocal").toBool();
}
inline bool readableLocalFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isFile() || !info.isReadable() || info.size() <= 0)
        return false;
    QFile file(path);
    return file.open(QIODevice::ReadOnly);
}
inline QVariantMap normalize(QVariantMap entry) {
    if (isLocal(entry)) {
        entry.insert("business", QStringLiteral("local"));
        entry.insert("isLocal", true);
        return entry;
    }
    const QJsonObject raw = QJsonDocument::fromJson(entry.value("rawJson").toString().toUtf8()).object();
    const QJsonObject history = raw.value("history").toObject();
    const QString business = entry.value("business").toString();
    auto fillId = [&](const char *key, qint64 value) {
        if (entry.value(key).toLongLong() <= 0 && value > 0) entry.insert(key, value);
    };
    fillId("oid", jh::getInt64(history, "oid"));
    fillId("cid", jh::getInt64(history, "cid"));
    fillId("cid", jh::getInt64(raw, "cid"));
    fillId("epId", jh::getInt64(history, "epid"));
    fillId("epId", jh::getInt64(raw, "ep_id"));
    if (business == QLatin1String("pgc")) {
        QJsonObject dynamic = raw;
        if (raw.value("orig").isObject()) dynamic = raw.value("orig").toObject();
        const QJsonObject pgc = dynamic.value("modules").toObject()
            .value("module_dynamic").toObject().value("major").toObject().value("pgc").toObject();
        fillId("epId", jh::getInt64(pgc, "epid"));
        const auto match = QRegularExpression(QStringLiteral("/ep(\\d+)")).match(entry.value("linkUrl").toString());
        if (match.hasMatch()) fillId("epId", match.captured(1).toLongLong());
    }
    // 保留已有持久化键；动态等未生成键的入口补上稳定业务键。
    if (entry.value("videoKey").toString().isEmpty()) {
        const qint64 id = entry.value(business == QLatin1String("pgc") ? "epId" : "oid").toLongLong();
        if (id > 0) entry.insert("videoKey", QStringLiteral("%1:%2:0").arg(business).arg(id));
    }
    return entry;
}
inline bool playable(const QVariantMap &entry) {
    if (entry.value("invalid").toBool()) return false;
    if (isLocal(entry)) return readableLocalFile(entry.value("localPath").toString());
    const QString business = entry.value("business").toString();
    return (business == QLatin1String("archive") && entry.value("oid").toLongLong() > 0)
        || (business == QLatin1String("pgc") && entry.value("epId").toLongLong() > 0);
}
}
#endif
