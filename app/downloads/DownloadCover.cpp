#include "downloads/DownloadCover.h"
#include "downloads/DownloadUtils.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>

namespace {
constexpr qint64 MaxImageBytes = 2 * 1024 * 1024;
constexpr int TimeoutMs = 20000;
bool stopped(const std::shared_ptr<std::atomic_bool> &flag) {
    return flag && flag->load();
}

struct ProcessResult { bool finished = false; bool success = false; QByteArray metadata; };
ProcessResult run(const QString &tool, const QStringList &arguments, QElapsedTimer &elapsed,
                  const std::shared_ptr<std::atomic_bool> &canceled) {
    ProcessResult result;
    if (stopped(canceled) || elapsed.elapsed() >= TimeoutMs) return result;
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif
    process.setStandardOutputFile(QProcess::nullDevice());
    process.start(tool, QStringList{"-nostdin", "-hide_banner", "-nostats", "-max_alloc", "67108864"} + arguments);
    if (!process.waitForStarted(1000)) return result;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(50);
        const QByteArray output = process.readAllStandardError();
        if (result.metadata.size() < 128 * 1024)
            result.metadata.append(output.left(128 * 1024 - result.metadata.size()));
        if (stopped(canceled) || elapsed.elapsed() >= TimeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            return result;
        }
    }
    result.metadata.append(process.readAllStandardError().left(128 * 1024 - result.metadata.size()));
    result.finished = process.exitStatus() == QProcess::NormalExit;
    result.success = result.finished && process.exitCode() == 0;
    return result;
}

bool validCover(const QString &path) {
    const QFileInfo file(path);
    if (!file.isFile() || file.size() <= 0 || file.size() > MaxImageBytes) return false;
    QImageReader reader(path);
    const QSize size = reader.size();
    return size.width() > 0 && size.height() > 0 && size.width() <= 400 && size.height() <= 400
        && !reader.read().isNull();
}

QVariantMap coverRecord(const QString &path) {
    return {{"coverPath", path}, {"coverUrl", QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}};
}
}

QVariantMap DownloadCover::extract(const QString &mediaPath, const QString &cacheDirectory,
                                  const std::shared_ptr<std::atomic_bool> &canceled,
                                  const QString &ffmpegPath) {
    const QFileInfo media(mediaPath);
    if (stopped(canceled) || !media.isAbsolute() || !media.isFile() || !media.isReadable()
        || media.size() <= 0 || cacheDirectory.isEmpty()) return {};
    const QString canonical = media.canonicalFilePath();
    const QString cache = QFileInfo(cacheDirectory).absoluteFilePath();
    if (canonical.isEmpty() || !QDir().mkpath(cache)) return {};
    // Version plus canonical identity/stat fingerprint deduplicates repeated imports,
    // but a replaced/edited source receives new artwork rather than a stale cache hit.
    const QByteArray identity = QByteArray("cover-v1\0", 9) + canonical.toUtf8() + '\0'
        + QByteArray::number(media.size()) + '\0' + QByteArray::number(media.lastModified().toMSecsSinceEpoch());
    const QString target = QDir(cache).filePath(QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()) + ".jpg");
    if (validCover(target)) return coverRecord(target);
    const QString tool = ffmpegPath.isEmpty() ? DownloadUtils::findTool("ffmpeg") : ffmpegPath;
    if (tool.isEmpty()) return {};
    QTemporaryDir temporary(QDir(cache).filePath(".extract-XXXXXX"));
    if (!temporary.isValid()) return {};
    QElapsedTimer elapsed;
    elapsed.start();
    const QStringList input{"-protocol_whitelist", "file,pipe", "-max_pixels", "67108864",
                            "-probesize", "5000000", "-analyzeduration", "5000000", "-i", canonical};
    // FFmpeg reports embedded MP4 covers and Matroska JPEG/PNG attachments as
    // attached-pic video streams. Read the input header only (no media decode).
    // Numeric mapping also works with FFmpeg releases predating disp: specifiers.
    const ProcessResult probe = run(tool, QStringList{"-loglevel", "info"} + input, elapsed, canceled);
    if (!probe.finished || stopped(canceled)) return {};
    const QRegularExpression stream(QStringLiteral(
        "(?:^|\\n)  Stream #0:(\\d+)(?:\\[[^\\]]*\\])?(?:\\([^)]*\\))?: Video:([^\\r\\n]*)"));
    QStringList covers;
    QString fallback;
    auto matches = stream.globalMatch(QString::fromUtf8(probe.metadata));
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString index = "0:" + match.captured(1);
        if (match.captured(2).contains("(attached pic)")) {
            if (covers.size() < 2) covers.append(index);
        } else if (fallback.isEmpty()) {
            fallback = index;
        }
    }
    if (!fallback.isEmpty()) covers.append(fallback);
    const QString image = temporary.filePath("cover.jpg");
    for (const QString &mapping : covers) {
        const QStringList output{"-map", mapping, "-frames:v", "1", "-an", "-sn", "-dn",
            "-vf", "scale=w='min(400,iw)':h='min(400,ih)':force_original_aspect_ratio=decrease",
            "-threads", "1", "-q:v", "3", "-fs", QString::number(MaxImageBytes),
            "-update", "1", "-y", image};
        if (!run(tool, QStringList{"-loglevel", "error", "-threads", "1"} + input + output,
                 elapsed, canceled).success || !validCover(image)) continue;
        if (stopped(canceled)) return {};
        QFile source(image);
        if (!source.open(QIODevice::ReadOnly)) return {};
        const QByteArray bytes = source.read(MaxImageBytes + 1);
        if (bytes.isEmpty() || bytes.size() > MaxImageBytes) return {};
        QSaveFile destination(target);
        if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size()
            || stopped(canceled) || !destination.commit()) return {};
        return coverRecord(target);
    }
    return {};
}
