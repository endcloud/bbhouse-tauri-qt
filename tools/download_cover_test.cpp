#include "downloads/DownloadCover.h"
#include "downloads/DownloadUtils.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QDebug>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/download-cover-fixture-XXXXXX");
    if (!temp.isValid()) return 1;
    int failures = 0;
    const auto check = [&](bool ok, const char *name) {
        qInfo() << (ok ? "PASS" : "FAIL") << name;
        failures += !ok;
    };
    const QString tool = DownloadUtils::findTool("ffmpeg");
    if (tool.isEmpty()) {
        qInfo() << "SKIP real cover integration: FFmpeg is required";
        return 77;
    }
    const auto ffmpeg = [&](const QStringList &args) {
        QProcess process;
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.start(tool, QStringList{"-nostdin", "-hide_banner", "-loglevel", "error", "-y"} + args);
        return process.waitForFinished(20000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    const QString cache = temp.filePath("缓存 封面");
    const QString plain = temp.filePath("中文 视频.mp4");
    const QString attached = temp.filePath("元数据封面.mp4");
    const QString mkv = temp.filePath("附件 封面.mkv");
    const QString artwork = temp.filePath("blue.jpg");
    const QString audio = temp.filePath("audio.m4a");
    QImage blue(800, 500, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    const bool generated = blue.save(artwork, "JPEG")
        && ffmpeg({"-f", "lavfi", "-i", "color=c=red:s=640x360:r=5", "-t", "0.6", "-c:v", "mpeg4", "-an", plain})
        && ffmpeg({"-i", plain, "-i", artwork, "-map", "0:v", "-map", "1:v", "-c", "copy",
                   "-disposition:v:1", "attached_pic", attached})
        && ffmpeg({"-i", plain, "-c", "copy", "-attach", artwork, "-metadata:s:t", "mimetype=image/jpeg", mkv})
        && ffmpeg({"-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100", "-t", "0.2", "-c:a", "aac", audio});
    check(generated, "generate local media with distinguishable frame and embedded artwork");
    if (!generated) return 1;
    const auto canceled = std::make_shared<std::atomic_bool>(false);
    const auto isColor = [&](const QVariantMap &record, bool expectBlue) {
        const QString path = record.value("coverPath").toString();
        QImage image(path);
        if (image.isNull() || image.width() > 400 || image.height() > 400) return false;
        const QColor pixel = image.pixelColor(image.width() / 2, image.height() / 2);
        return QUrl(record.value("coverUrl").toString()).toLocalFile() == path
            && (expectBlue ? pixel.blue() > 200 && pixel.red() < 40 : pixel.red() > 200 && pixel.blue() < 40);
    };
    const auto embedded = DownloadCover::extract(attached, cache, canceled, tool);
    check(isColor(embedded, true), "embedded MP4 artwork takes precedence over video frame and stays within 400 pixels");
    check(isColor(DownloadCover::extract(mkv, cache, canceled, tool), true), "Matroska image attachment is used as cover");
    const auto fallback = DownloadCover::extract(plain, cache, canceled, tool);
    check(isColor(fallback, false), "Chinese local path and first-frame fallback work without embedded cover");
    check(DownloadCover::extract(audio, cache, canceled, tool).isEmpty(), "audio without artwork has no fabricated cover");
    check(DownloadCover::extract(attached, cache, canceled, temp.filePath("missing-ffmpeg")) == embedded,
          "same source fingerprint reuses cached cover without running FFmpeg");
    QFile modified(plain);
    const bool touched = modified.open(QIODevice::ReadWrite)
        && modified.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2), QFileDevice::FileModificationTime);
    modified.close();
    const auto refreshed = DownloadCover::extract(plain, cache, canceled, tool);
    check(touched && isColor(refreshed, false) && refreshed.value("coverPath") != fallback.value("coverPath"),
          "changed media stat gets a new cache identity");
    const QString broken = temp.filePath("broken.mp4");
    QFile invalid(broken);
    if (!invalid.open(QIODevice::WriteOnly)) return 1;
    invalid.write("not a media file");
    invalid.close();
    const int countBefore = QDir(cache).entryList(QDir::Files).size();
    check(DownloadCover::extract(broken, cache, canceled, tool).isEmpty(), "unreadable media returns no cover");
    check(DownloadCover::extract(broken, cache, canceled, temp.filePath("missing-ffmpeg")).isEmpty(),
          "missing configured FFmpeg fails gracefully");
    canceled->store(true);
    check(DownloadCover::extract(attached, cache, canceled, tool).isEmpty(), "canceled work does not publish cached artwork");
    check(QDir(cache).entryList(QDir::Files).size() == countBefore
          && QDir(cache).entryList({".extract-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
          "failed and canceled extraction leave no temporary output");
    return failures ? 1 : 0;
}
