#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace DownloadUtils {
QString safeName(QString value);
QString findTool(const QString &name);
QString subtitleToSrt(const QByteArray &json);
QByteArray danmakuXml(const QByteArray &bytes);
bool readableMedia(const QString &path);
QString siblingDanmaku(const QString &path);
QVariantMap safeMetadata(const QVariantMap &entry);
QString reserveBase(const QString &directory, const QString &title);
// Only completed download records with a readable final media file may be cleaned.
bool cleanupCompleted(const QVariantMap &record);
}
