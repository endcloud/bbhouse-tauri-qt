#ifndef SCREENSHOT_PATH_H
#define SCREENSHOT_PATH_H

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

namespace ScreenshotPath {
// A monotonic millisecond suffix keeps rapid requests distinct, even if the
// system clock moves backwards. Existing captures are never chosen again.
inline QString next(const QString &directory, QString title) {
    title.remove(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])")));
    title = title.trimmed().left(60);
    if (title.isEmpty()) title = QStringLiteral("screenshot");
    static QMutex mutex;
    static qint64 previous = 0;
    const QMutexLocker lock(&mutex);
    qint64 timestamp = qMax(QDateTime::currentMSecsSinceEpoch(), previous + 1);
    QString path;
    do {
        const auto stamp = QDateTime::fromMSecsSinceEpoch(timestamp).toUTC()
                               .toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
        path = QDir(directory).filePath(title + '_' + stamp + QStringLiteral(".png"));
        ++timestamp;
    } while (QFileInfo::exists(path) ||
             QFileInfo::exists(path.chopped(4) + QStringLiteral(".jpg")));
    previous = timestamp - 1;
    return path;
}
}

#endif
