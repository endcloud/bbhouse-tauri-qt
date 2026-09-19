#pragma once
#include <QVariantMap>
#include <QStringList>
#include <QNetworkProxy>
#include <atomic>
#include <functional>
#include <memory>

namespace DownloadWorker {
using Progress = std::function<void(const QString &, int)>;
// Percent is -1 while the amount of work is unknown. Callbacks execute on the
// calling worker thread; UI consumers must queue updates onto their own thread.
// Shared production primitives are exposed for loopback-only process integration tests.
QByteArray fetchAttachment(const QString &url, const QString &cookie,
                          const std::shared_ptr<std::atomic_bool> &canceled);
void downloadStream(const QStringList &urls, const QString &path, const QString &cookie,
                    const std::shared_ptr<std::atomic_bool> &canceled,
                    const QString &aria2Path = {},
                    const std::function<void(int)> &progress = {});
void mergeStreams(const QString &videoPath, const QString &audioPath, const QString &target,
                  const std::shared_ptr<std::atomic_bool> &canceled,
                  const QString &ffmpegPath = {});
QVariantMap run(QVariantMap record, const std::shared_ptr<std::atomic_bool> &canceled,
                const Progress &progress, const QNetworkProxy &regionalProxy = QNetworkProxy(QNetworkProxy::NoProxy));
}
