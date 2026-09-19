#include "downloads/DownloadWorker.h"
#include "downloads/DownloadUtils.h"
#include "core/AppPaths.h"
#include "core/ApiErrors.h"
#include "core/BilibiliApiClient.h"
#include "core/PlayerApi.h"
#include "core/JsonHelpers.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrlQuery>

namespace {
struct Canceled {};
void checkCanceled(const std::shared_ptr<std::atomic_bool> &flag) {
    if (flag->load()) throw Canceled{};
}
QString cleanLine(QString value) {
    value.remove('\r'); value.remove('\n'); value.remove('\t');
    return value;
}
QString configQuoted(QString value) {
    value = cleanLine(value);
    value.replace('\\', "\\\\"); value.replace('"', "\\\"");
    return '"' + value + '"';
}
bool allowedUrl(const QString &text) {
    const QUrl url(text);
    return url.isValid() && !url.host().isEmpty()
        && (url.scheme() == "http" || url.scheme() == "https")
        && !text.contains('\r') && !text.contains('\n') && !text.contains('\t');
}
QByteArray process(const QString &tool, const QStringList &args, const QByteArray &input,
                   const std::shared_ptr<std::atomic_bool> &flag, bool capture = false,
                   const QString &overridePath = {},
                   const std::function<void(const QByteArray &)> &consumeOutput = {}) {
    checkCanceled(flag);
    const QString executable = overridePath.isEmpty() ? DownloadUtils::findTool(tool) : overridePath;
    if (executable.isEmpty()) throw std::runtime_error("Required download executable unavailable");
    QProcess child;
    auto env = QProcessEnvironment::systemEnvironment();
    for (const QString &key : env.keys()) {
        if (key.endsWith("_proxy", Qt::CaseInsensitive)) env.remove(key);
    }
    env.insert("NO_PROXY", "*");
#ifdef Q_OS_MACOS
    // Homebrew OpenSSL's compiled CA path is outside the relocatable app.
    // Use macOS's public roots only for our bundled aria2, retaining validation.
    const QString bundledAria = QCoreApplication::applicationDirPath() + "/aria2c";
    if (tool == "aria2c" && QCoreApplication::applicationDirPath().endsWith(".app/Contents/MacOS")
        && QFileInfo(executable).canonicalFilePath() == QFileInfo(bundledAria).canonicalFilePath()) {
        env.insert("SSL_CERT_FILE", "/etc/ssl/cert.pem");
        env.insert("SSL_CERT_DIR", "/etc/ssl/certs");
        env.insert("OPENSSL_MODULES", QCoreApplication::applicationDirPath() + "/../Frameworks/ossl-modules");
    }
#endif
    child.setProcessEnvironment(env);
    child.setStandardErrorFile(QProcess::nullDevice());
    if (!capture && !consumeOutput) child.setStandardOutputFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif
    child.start(executable, args);
    if (!child.waitForStarted(10000)) throw std::runtime_error("Download process failed to start");
    child.write(input);
    child.closeWriteChannel();
    QElapsedTimer timer;
    timer.start();
    QByteArray output;
    const auto drain = [&] {
        // Progress is consumed as bounded chunks, never accumulated or logged:
        // aria2's other output may contain signed media URLs.
        if (consumeOutput) {
            while (child.bytesAvailable() > 0) consumeOutput(child.read(8192));
        } else if (capture) output += child.readAllStandardOutput();
    };
    while (!child.waitForFinished(100)) {
        drain();
        if (flag->load() || timer.elapsed() > 24LL * 60 * 60 * 1000
            || output.size() > 64 * 1024 * 1024) {
            child.kill(); child.waitForFinished(5000);
            checkCanceled(flag);
            throw std::runtime_error("Download process exceeded resource limits");
        }
    }
    drain();
    checkCanceled(flag);
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
        throw std::runtime_error("Download process failed");
    if (output.size() > 64 * 1024 * 1024) throw std::runtime_error("Attachment too large");
    return output;
}
QByteArray curl(const QString &url, const QString &cookie,
                const std::shared_ptr<std::atomic_bool> &flag) {
    if (!allowedUrl(url)) throw std::runtime_error("Invalid attachment URL");
    QString config = "url = " + configQuoted(url) + '\n';
    config += "header = " + configQuoted("Cookie: " + cookie) + '\n';
    config += "user-agent = " + configQuoted(BilibiliApiClient::userAgent()) + '\n';
    config += "referer = " + configQuoted(BilibiliApiClient::referer()) + '\n';
    // -q MUST be first: do not load a user .curlrc or inherit proxy settings.
    return process("curl", {"-q", "--config", "-", "--fail", "--silent", "--location",
        "--compressed", "--noproxy", "*", "--proxy", "", "--proto", "=http,https",
        "--proto-redir", "=http,https", "--connect-timeout", "20", "--max-time", "180"},
        config.toUtf8(), flag, true);
}
void aria(const QStringList &urls, const QString &path, const QString &cookie,
          const std::shared_ptr<std::atomic_bool> &flag, const QString &overridePath = {},
          const std::function<void(int)> &progress = {}) {
    checkCanceled(flag);
    QStringList candidates;
    for (const auto &url : urls) if (allowedUrl(url)) candidates.append(url);
    if (candidates.isEmpty()) throw std::runtime_error("No usable DASH URL");
    const QFileInfo output(path);
    // Only our explicit completion receipt permits reusing a finished track.
    // A missing aria2 checkpoint alone can also mean a process died too early.
    QFile receipt(path + ".complete");
    if (DownloadUtils::readableMedia(path) && receipt.open(QIODevice::ReadOnly)
        && receipt.readAll().trimmed() == QByteArray::number(output.size())) {
        if (progress) progress(100);
        return;
    }
    receipt.close();
    if (progress) progress(-1); // No total is known until aria2 reports it.
    QByteArray pending;
    bool discardLine = false;
    QElapsedTimer progressTimer;
    progressTimer.start();
    int lastProgress = -1;
    const auto consume = [&](const QByteArray &chunk) {
        static const QRegularExpression readout(
            QStringLiteral(R"(\[#[0-9a-fA-F]+\s+\d+(?:B)?/(\d+)(?:B)?(?:\((\d+)%\))?)"));
        for (const char byte : chunk) {
            if (byte != '\r' && byte != '\n') {
                if (discardLine) continue;
                pending += byte;
                if (pending.size() >= 4096) { pending.clear(); discardLine = true; }
                continue;
            }
            if (discardLine) { discardLine = false; continue; }
            const auto match = readout.match(QString::fromLatin1(pending));
            pending.clear();
            if (!match.hasMatch() || match.captured(1).toULongLong() == 0
                || match.captured(2).isEmpty()) continue;
            // A resumed transfer can begin above zero. Never regress within a
            // stream, and reserve 100 for verified successful process exit.
            const int value = qBound(0, match.captured(2).toInt(), 99);
            if (value <= lastProgress || (lastProgress >= 0 && progressTimer.elapsed() < 200)) continue;
            lastProgress = value;
            progressTimer.restart();
            if (progress) progress(value);
        }
    };
    // URL and credentials are streamed to stdin, never argv, disk or log.
    QString input = candidates.join('\t') + '\n';
    input += "  dir=" + cleanLine(output.absolutePath()) + '\n';
    input += "  out=" + cleanLine(output.fileName()) + '\n';
    input += "  header=Cookie: " + cleanLine(cookie) + '\n';
    input += "  header=Referer: " + BilibiliApiClient::referer() + '\n';
    process("aria2c", {"--no-conf=true", "--input-file=-", "--all-proxy=", "--http-proxy=",
        "--https-proxy=", "--enable-rpc=false", "--max-concurrent-downloads=1",
        "--split=4", "--max-connection-per-server=4", "--continue=true",
        "--auto-file-renaming=false", "--allow-overwrite=false", "--file-allocation=none",
        "--connect-timeout=20", "--timeout=30", "--max-tries=3", "--retry-wait=2",
        "--console-log-level=error", "--summary-interval=1", "--download-result=hide",
        "--show-console-readout=true", "--truncate-console-readout=false", "--human-readable=false",
        "--enable-color=false", "--user-agent=" + BilibiliApiClient::userAgent()},
        input.toUtf8(), flag, false, overridePath, consume);
    if (!DownloadUtils::readableMedia(path)) throw std::runtime_error("DASH stream is empty");
    if (!QFileInfo::exists(receipt.fileName()) && receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        receipt.write(QByteArray::number(QFileInfo(path).size()));
    if (progress) progress(100);
}
QString unusedPath(const QString &path) {
    if (!QFileInfo::exists(path)) return path;
    const QFileInfo file(path);
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    QString target;
    do {
        target = file.dir().filePath(file.completeBaseName() + '_'
            + QDateTime::fromMSecsSinceEpoch(timestamp++).toString("yyyyMMdd_HHmmss_zzz")
            + '.' + file.suffix());
    } while (QFileInfo::exists(target));
    return target;
}
void writeNew(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || file.write(bytes) != bytes.size() || !file.flush())
        throw std::runtime_error("Cannot write download attachment");
}
}

QByteArray DownloadWorker::fetchAttachment(const QString &url, const QString &cookie,
    const std::shared_ptr<std::atomic_bool> &canceled) { return curl(url, cookie, canceled); }
void DownloadWorker::downloadStream(const QStringList &urls, const QString &path, const QString &cookie,
    const std::shared_ptr<std::atomic_bool> &canceled, const QString &aria2Path,
    const std::function<void(int)> &progress) {
    aria(urls, path, cookie, canceled, aria2Path, progress);
}
void DownloadWorker::mergeStreams(const QString &videoPath, const QString &audioPath,
    const QString &target, const std::shared_ptr<std::atomic_bool> &canceled, const QString &ffmpegPath) {
    if (QFileInfo::exists(target)) throw std::runtime_error("Refusing to replace existing media");
    // FFmpeg may leave partial output after cancellation/failure. Only publish
    // a finished merge; the owned temporary file is reclaimed on every exit.
    // A directory avoids holding a QTemporaryFile handle while FFmpeg opens it
    // on Windows. It is on the destination filesystem for the final rename.
    QTemporaryDir workspace(QFileInfo(target).dir().filePath(".bbhouse-merge-XXXXXX"));
    if (!workspace.isValid()) throw std::runtime_error("Cannot create merge temporary directory");
    const QString merged = workspace.filePath(QFileInfo(target).fileName());
    QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-n"};
    if (!videoPath.isEmpty()) args << "-i" << videoPath;
    if (!audioPath.isEmpty()) args << "-i" << audioPath;
    if (!videoPath.isEmpty()) args << "-map" << "0:v:0";
    if (!audioPath.isEmpty()) args << "-map" << (videoPath.isEmpty() ? "0:a:0" : "1:a:0");
    args << "-c" << "copy" << "-movflags" << "+faststart" << merged;
    process("ffmpeg", args, {}, canceled, false, ffmpegPath);
    if (!DownloadUtils::readableMedia(merged)) throw std::runtime_error("Empty merged media");
    if (!QFile::rename(merged, target)) throw std::runtime_error("Cannot publish merged media");
}

QVariantMap DownloadWorker::run(QVariantMap record,
                              const std::shared_ptr<std::atomic_bool> &canceled,
                              const Progress &progress, const QNetworkProxy &regionalProxy) {
    QString phase = Loc::get("解析下载信息");
    try {
        progress(phase, -1);
        checkCanceled(canceled);
        const QString cookie = QFileInfo::exists(AppPaths::cookiePath())
            ? BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath()) : QString();
        BilibiliApiClient directApi{QNetworkProxy(QNetworkProxy::NoProxy)};
        BilibiliApiClient scopedApi{regionalProxy};
        qint64 aid = record.value("oid").toLongLong();
        qint64 cid = record.value("cid").toLongLong();
        const qint64 epId = record.value("epId").toLongLong();
        const bool pgc = record.value("business").toString() == "pgc";
        BilibiliApiClient &api = pgc && record.value("regionalApiProxy").toBool() ? scopedApi : directApi;
        if (pgc && (cid <= 0 || aid <= 0)) {
            QUrl url("https://api.bilibili.com/pgc/view/web/season");
            QUrlQuery query; query.addQueryItem("ep_id", QString::number(epId)); url.setQuery(query);
            const QJsonObject result = api.get(url, cookie).root.value("result").toObject();
            for (const auto &item : result.value("episodes").toArray()) {
                const auto episode = item.toObject();
                if (jh::getInt64(episode, "id") != epId && jh::getInt64(episode, "ep_id") != epId) continue;
                cid = jh::getInt64(episode, "cid"); aid = jh::getInt64(episode, "aid"); break;
            }
        } else if (cid <= 0) {
            cid = PlayerApi::getFirstPage(api, aid, cookie).cid;
        }
        if (cid <= 0) throw std::runtime_error("No content identifier");
        record["oid"] = QString::number(aid); record["cid"] = QString::number(cid);
        const bool video = record.value("downloadVideo").toBool();
        const bool audio = video || record.value("downloadAudio").toBool();
        const QString base = record.value("basePath").toString();
        QStringList warnings;
        if ((video || audio) && !DownloadUtils::readableMedia(record.value("localPath").toString())) {
            PlayerApi::DashPlayInfo dash;
            if (pgc) {
                auto pgcInfo = PlayerApi::getPgcDashPlayUrl(api, epId, cid, cookie,
                    record.value("preferredQn", 80).toInt(), "avc");
                if (pgcInfo.entitlement.isPreview) throw std::runtime_error("Preview-only episode");
                dash = pgcInfo.dash;
            } else {
                dash = PlayerApi::getDashPlayUrl(api, aid, cid, cookie,
                    record.value("preferredQn", 80).toInt(), "avc");
            }
            record["selectedQn"] = dash.selectedQn;
            if (audio && !dash.hasAudio && !video) throw std::runtime_error("No audio stream");
            const bool hasAudio = audio && dash.hasAudio;
            if (audio && !dash.hasAudio) warnings.append(Loc::get("此视频没有音频流"));
            if (video) {
                phase = Loc::get("下载视频流");
                aria(dash.selectedVideoUrls, base + ".video.m4s", cookie, canceled,
                    record.value("aria2Path").toString(), [&](int value) {
                        progress(phase, value < 0 ? -1 : value * (hasAudio ? 70 : 90) / 100);
                    });
            }
            if (hasAudio) {
                phase = Loc::get("下载音频流");
                aria(dash.selectedAudioUrls, base + ".audio.m4s", cookie, canceled,
                    record.value("aria2Path").toString(), [&](int value) {
                        progress(phase, value < 0 ? -1 : (video ? 70 : 0) + value * (video ? 20 : 90) / 100);
                    });
            }
            phase = Loc::get("合并媒体"); progress(phase, -1);
            const QString target = unusedPath(base + (video ? ".mp4" : ".m4a"));
            mergeStreams(video ? base + ".video.m4s" : QString(),
                hasAudio ? base + ".audio.m4s" : QString(), target, canceled,
                record.value("ffmpegPath").toString());
            record["localPath"] = target;
        }
        const QString media = record.value("localPath").toString();
        const QString sidecarBase = media.isEmpty() ? base
            : QFileInfo(media).dir().filePath(QFileInfo(media).completeBaseName());
        if (record.value("downloadDanmaku").toBool()
            && !DownloadUtils::readableMedia(record.value("danmakuPath").toString())) {
            phase = Loc::get("下载弹幕"); progress(phase, -1);
            const QByteArray xml = DownloadUtils::danmakuXml(curl(
                QStringLiteral("https://comment.bilibili.com/%1.xml").arg(cid), cookie, canceled));
            const QString path = unusedPath(sidecarBase + ".xml");
            writeNew(path, xml); record["danmakuPath"] = path;
        }
        if (record.value("downloadSubtitles").toBool()) {
            phase = Loc::get("下载字幕"); progress(phase, -1);
            const auto tracks = aid > 0 ? PlayerApi::getSubtitleTracks(directApi, aid, cid, cookie)
                                       : QList<PlayerApi::SubtitleTrack>();
            QStringList subtitlePaths;
            for (const auto &track : tracks) {
                checkCanceled(canceled);
                const QString srt = DownloadUtils::subtitleToSrt(curl(track.url, cookie, canceled));
                if (srt.isEmpty()) continue;
                const QString suffix = subtitlePaths.isEmpty() ? ".srt"
                    : '.' + DownloadUtils::safeName(track.lan) + ".srt";
                const QString path = unusedPath(sidecarBase + suffix);
                writeNew(path, srt.toUtf8()); subtitlePaths.append(path);
                record["subtitlePaths"] = subtitlePaths;
            }
            record["subtitlePaths"] = subtitlePaths;
            if (subtitlePaths.isEmpty()) warnings.append(Loc::get("此视频没有可用字幕"));
        }
        checkCanceled(canceled);
        record["state"] = "completed"; record["progress"] = 100;
        if ((video || audio) && !DownloadUtils::cleanupCompleted(record)) {
            record["cleanupWarning"] = Loc::get("下载完成，但部分临时文件未能清理，请检查文件权限");
            warnings.append(record.value("cleanupWarning").toString());
        } else record.remove("cleanupWarning");
        record["message"] = warnings.isEmpty() ? Loc::get("下载完成") : warnings.join(QStringLiteral("；"));
    } catch (const Canceled &) {
        record["state"] = "canceled";
        record["message"] = Loc::get("已取消，已下载文件保留，可重试");
    } catch (const std::exception &) {
        // Child output and API exception details can contain signed URLs; never persist them.
        record["state"] = "failed";
        record["message"] = Loc::get("%1失败，请检查网络、账号权限、工具与目录后重试").arg(phase);
    }
    record.remove("aria2Path"); record.remove("ffmpegPath");
    record["updatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return record;
}
