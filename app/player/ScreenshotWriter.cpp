#include "ScreenshotWriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryFile>

namespace ScreenshotWriter {
namespace {
QString message(const char *text) {
    return QCoreApplication::translate("ScreenshotWriter", text);
}

Result publish(const QByteArray &bytes, const QString &target) {
    // Stage in the destination directory so publishing stays on one filesystem.
    // QFile::rename refuses existing targets, including races after this check.
    if (QFileInfo::exists(target)) return {{}, message("截图文件已存在，未覆盖")};
    QTemporaryFile staging(QFileInfo(target).absolutePath() + "/.screenshot-XXXXXX");
    if (!staging.open()) return {{}, staging.errorString()};
    if (staging.write(bytes) != bytes.size() || !staging.flush())
        return {{}, staging.errorString()};
    staging.close();
    if (!staging.rename(target)) return {{}, staging.errorString()};
    staging.setAutoRemove(false);
    return {target, {}};
}
}

Result save(const QString &sourcePng, const QString &targetPng, qint64 maxBytes) {
    if (maxBytes <= 0) return {{}, message("截图大小上限必须大于零")};
    QImageReader reader(sourcePng, "png");
    const QImage original = reader.read();
    if (original.isNull()) return {{}, reader.errorString()};

    QFile source(sourcePng);
    if (!source.open(QIODevice::ReadOnly)) return {{}, source.errorString()};
    if (source.size() < maxBytes) {
        const QByteArray bytes = source.readAll();
        if (source.error() != QFile::NoError) return {{}, source.errorString()};
        // Source is a caller-owned private staging file, immutable during save.
        return publish(bytes, targetPng);
    }
    source.close();

    const QFileInfo targetInfo(targetPng);
    const QString targetJpeg = targetInfo.dir().filePath(targetInfo.completeBaseName() + ".jpg");
    QImage frame = original;
    for (;;) {
        // Exhaust quality reductions before reducing resolution. Keep quality
        // at least 50; high-entropy frames can then shrink proportionally.
        for (const int quality : {95, 90, 85, 80, 75, 70, 65, 60, 55, 50}) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            if (!buffer.open(QIODevice::WriteOnly)) return {{}, buffer.errorString()};
            QImageWriter writer(&buffer, "jpeg");
            writer.setQuality(quality);
            writer.setOptimizedWrite(true);
            if (!writer.write(frame)) return {{}, writer.errorString()};
            if (bytes.size() < maxBytes) return publish(bytes, targetJpeg);
        }
        if (frame.width() == 1 && frame.height() == 1)
            return {{}, message("无法在指定大小上限内保存截图")};
        const QSize bounds(qMax(1, frame.width() * 4 / 5), qMax(1, frame.height() * 4 / 5));
        frame = original.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (frame.isNull()) return {{}, message("截图缩放失败")};
    }
}
}
