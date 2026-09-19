#pragma once
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <memory>

namespace DownloadCover {
// Blocking bounded work: call only from a worker. Never modifies the source media.
// Prefers embedded artwork; an ordinary video frame is used when artwork is absent.
QVariantMap extract(const QString &mediaPath, const QString &cacheDirectory,
                    const std::shared_ptr<std::atomic_bool> &canceled,
                    const QString &ffmpegPath = {});
}
