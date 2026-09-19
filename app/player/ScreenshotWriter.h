#ifndef SCREENSHOT_WRITER_H
#define SCREENSHOT_WRITER_H

#include <QString>

namespace ScreenshotWriter {
inline constexpr qint64 MaxBytes = 3 * 1024 * 1024;

struct Result {
    QString path;
    QString error;
};

// Synchronous: run outside the GUI thread. The caller owns sourcePng.
// Retains small PNGs, otherwise returns a JPEG path. Never overwrites a file.
// A successful output is strictly smaller than maxBytes.
Result save(const QString &sourcePng, const QString &targetPng, qint64 maxBytes = MaxBytes);
}

#endif
