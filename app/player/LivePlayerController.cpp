#include "player/LivePlayerController.h"

#include "player/FullscreenWindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFutureWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QPromise>
#include <QRegularExpression>
#include <QThreadPool>
#include <QThread>
#include <QElapsedTimer>
#include <QtEndian>
#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"
#include "player/ScreenshotPath.h"
#include "player/ScreenshotWriter.h"

namespace {
constexpr int kCandidateTimeoutMs = 12000;
constexpr int kBufferTimeoutMs = 20000;
}

LivePlayerController::LivePlayerController(QObject *parent, Resolver resolver,
                                         ClientFactory clientFactory,
                                         RecordingPathFactory recordingPathFactory)
    : QObject(parent), resolver_(std::move(resolver)),
      clientFactory_(std::move(clientFactory)),
      recordingPathFactory_(std::move(recordingPathFactory)) {
    screenshotPool_.setMaxThreadCount(1);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &LivePlayerController::closeRequested);
    if (!resolver_) resolver_ = [](const QString &roomId, int qn) {
        const QString cookie = BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
        return LiveApi::fetchPlayInfo(*BilibiliApiClient::instance(), cookie, roomId, qn);
    };
    if (!clientFactory_) clientFactory_ = [](QObject *owner) { return MpvClient::create(owner); };
    videoItem_ = new MpvVideoItem();
    watchdog_.setSingleShot(true);
    connect(&watchdog_, &QTimer::timeout, this, &LivePlayerController::queueNextCandidate);
    connect(videoItem_, &MpvVideoItem::renderError, this, [this](const QString &) {
        if (mediaActive_) fail(Loc::get("直播画面初始化失败，请重新连接"));
    });
}

LivePlayerController::~LivePlayerController() {
    closeRequested();
    delete videoItem_;
}

namespace {
QString liveSafeName(QString value) {
    value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), "_");
    value = value.trimmed().left(80);
    while (value.endsWith('.') || value.endsWith(' ')) value.chop(1);
    if (value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral(".."))
        value = QStringLiteral("up");
    if (QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                            QRegularExpression::CaseInsensitiveOption).match(value).hasMatch())
        value.prepend('_');
    return value;
}
struct LiveSessionPaths {
    QString stem;
    QString directory;
};
LiveSessionPaths liveSessionPaths(const QString &root, const QString &author) {
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString safeAuthor = liveSafeName(author);
    if (safeAuthor == QStringLiteral("live") && author.trimmed().isEmpty()) safeAuthor = QStringLiteral("up");
    // Keep both the directory and the media filename short and stable. The
    // live title is display metadata and must not become part of the path.
    const QString stem = QStringLiteral("live_%1_%2").arg(safeAuthor.left(48), stamp);
    const QString directory = QDir(root).filePath(QStringLiteral("live/") + stem);
    QDir().mkpath(directory);
    return {stem, directory};
}
QString liveRecordingPath(const QString &root, const QString &author) {
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString safeAuthor = liveSafeName(author);
    const QString stem = QStringLiteral("live_%1_%2").arg(safeAuthor.left(48), stamp);
    const QString directory = QDir(root).filePath(QStringLiteral("live"));
    QString path = QDir(directory).filePath(stem + QStringLiteral(".mp4"));
    for (int suffix = 1; QFileInfo::exists(path); ++suffix)
        path = QDir(directory).filePath(stem + QStringLiteral("_") + QString::number(suffix)
                                        + QStringLiteral(".mp4"));
    return path;
}
QString configuredDownloadDirectory() {
    QSettings settings(QDir(AppPaths::dataDir()).filePath(QStringLiteral("downloads.ini")),
                       QSettings::IniFormat);
    QString path = settings.value(QStringLiteral("download/directory")).toString();
    if (path.isEmpty()) path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (path.isEmpty()) path = QDir::home().filePath(QStringLiteral("Downloads"));
    return path;
}

QString recordingDirectoryError(const QString &directory) {
    const QFileInfo info(directory);
    if (!info.exists() || !info.isDir()) {
        return Loc::get("无法创建录制目录");
    }

    // QFileInfo::isWritable() is not reliable for all Windows directory
    // providers. An actual temporary file checks the same write path mpv will
    // use, and is removed immediately after the probe.
    QTemporaryFile probe(QDir(directory).filePath(QStringLiteral(".bbhouse-record-XXXXXX")));
    probe.setAutoRemove(true);
    if (!probe.open()) {
        return Loc::get("录制目录不可写：%1").arg(probe.errorString());
    }
    return {};
}

bool isCompleteMp4(QFile &file, qint64 fileSize) {
    qint64 offset = 0;
    bool hasFileType = false;
    bool hasMovie = false;
    bool hasMediaData = false;
    while (offset < fileSize) {
        if (fileSize - offset < 8 || !file.seek(offset)) return false;
        const QByteArray base = file.read(8);
        if (base.size() != 8) return false;
        quint64 boxSize = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(base.constData()));
        const QByteArray type = base.mid(4, 4);
        quint64 headerSize = 8;
        if (boxSize == 1) {
            const QByteArray extended = file.read(8);
            if (extended.size() != 8) return false;
            boxSize = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(extended.constData()));
            headerSize = 16;
        } else if (boxSize == 0) {
            boxSize = static_cast<quint64>(fileSize - offset);
        }
        if (boxSize < headerSize || boxSize > static_cast<quint64>(fileSize - offset)) return false;
        if (type == QByteArrayLiteral("ftyp")) hasFileType = true;
        else if (type == QByteArrayLiteral("moov")) hasMovie = boxSize > headerSize;
        else if (type == QByteArrayLiteral("mdat")) hasMediaData |= boxSize > headerSize;
        offset += static_cast<qint64>(boxSize);
    }
    return offset == fileSize && hasFileType && hasMovie && hasMediaData;
}

QString recordingIntegrityError(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return Loc::get("录制文件未生成");
    if (info.size() < 16) return Loc::get("录制文件为空或过短");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Loc::get("录制文件无法读取：%1").arg(file.errorString());
    }
    const QByteArray header = file.read(12);
    if (header.size() < 12) return Loc::get("录制文件头不完整");
    if (header.left(4) == QByteArrayLiteral("RIFF")
        && header.mid(8, 4) == QByteArrayLiteral("WAVE")) {
        const quint32 riffSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(header.constData() + 4));
        if (static_cast<quint64>(riffSize) + 8 != static_cast<quint64>(info.size()))
            return Loc::get("WAV 录制文件长度标记不完整");
        return {};
    }
    if (!isCompleteMp4(file, info.size())) return Loc::get("MP4 文件结构不完整或缺少索引/媒体数据");
    return {};
}
}

QString LivePlayerController::statusText() const {
    if (!errorMessage_.isEmpty()) return errorMessage_;
    if (roomId_.isEmpty()) return Loc::get("尚未选择直播间");
    if (loading_) return Loc::get("正在连接直播…");
    if (paused_) return Loc::get("直播已暂停，重新连接可回到最新画面");
    if (buffering_) return Loc::get("直播缓冲中…");
    return Loc::get("直播中");
}

QString LivePlayerController::qualityLabel() const {
    for (const QVariant &value : qualities_) {
        const QVariantMap quality = value.toMap();
        if (quality.value("qn").toInt() == currentQn_) return quality.value("label").toString();
    }
    return currentQn_ > 0 ? QString::number(currentQn_) : Loc::get("自动");
}

void LivePlayerController::openRoom(QVariantMap room) {
    const QString id = room.value("roomId").toString().trimmed();
    static const QRegularExpression validId(QStringLiteral("^[1-9][0-9]{0,19}$"));
    if (!validId.match(id).hasMatch()) {
        closeRequested();
        fail(Loc::get("直播间 ID 无效"));
        return;
    }
    roomId_ = id;
    title_ = room.value("title").toString();
    authorName_ = room.value("authorName", room.value("uname")).toString();
    preferredQn_ = 10000;
    currentQn_ = 0;
    qualities_.clear();
    emit roomChanged();
    emit qualitiesChanged();
    resolve();
}

void LivePlayerController::retry() {
    if (!roomId_.isEmpty()) resolve();
}

void LivePlayerController::setQuality(int qn) {
    if (roomId_.isEmpty() || qn <= 0) return;
    bool available = false;
    for (const QVariant &value : qualities_)
        if (value.toMap().value("qn").toInt() == qn) available = true;
    if (!available || (qn == currentQn_ && errorMessage_.isEmpty())) return;
    preferredQn_ = qn;
    resolve();
}

void LivePlayerController::resolve() {
    finishRecording(true);
    const quint64 generation = ++generation_;
    ++candidateSerial_;
    watchdog_.stop();
    mediaActive_ = false;
    if (client_) client_->stop();
    candidates_.clear();
    candidateIndex_ = -1;
    loading_ = true;
    buffering_ = false;
    paused_ = true;
    errorMessage_.clear();
    emit stateChanged();
    const auto resolver = resolver_;
    const QString roomId = roomId_;
    const int qn = preferredQn_;
    const QPointer<LivePlayerController> guard(this);
    // Only immutable snapshots enter the worker. Delivery is queued through the
    // application, so destroying the controller while HTTP runs is harmless.
    QThreadPool::globalInstance()->start([guard, resolver, roomId, qn, generation] {
        LivePlayInfo info;
        QString error;
        try {
            info = resolver(roomId, qn);
        } catch (const ApiError &exception) {
            error = exception.message();
        } catch (const std::exception &) {
            // Backend exceptions can contain a signed media URL: never surface it.
            error = Loc::get("直播地址解析失败，请重新连接");
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, generation, info, error] {
            if (guard) guard->acceptResolved(generation, info, error);
        }, Qt::QueuedConnection);
    });
}

void LivePlayerController::acceptResolved(quint64 generation,
                                        const LivePlayInfo &info,
                                        const QString &error) {
    if (generation != generation_ || roomId_.isEmpty()) return;
    if (!error.isEmpty()) { fail(error); return; }
    if (info.liveStatus != 1) { fail(Loc::get("主播已下播，请稍后刷新")); return; }
    if (info.urls.isEmpty()) { fail(Loc::get("暂无可用直播地址，请重新连接")); return; }
    qualities_ = info.qualities;
    currentQn_ = info.currentQn;
    emit qualitiesChanged();
    // API ordering preserves AVC FLV -> HLS and each CDN, with one finite pass.
    candidates_ = info.urls;
    candidates_.removeDuplicates();
    candidateIndex_ = -1;
    if (!ensureKernel()) { fail(Loc::get("直播播放内核不可用，请检查 libmpv")); return; }
    nextCandidate();
}

bool LivePlayerController::ensureKernel() {
    if (client_) return true;
    client_ = clientFactory_(this);
    if (!client_) return false;
    videoItem_->setClient(client_);
    // Public CDN requests carry only UA/Referer, never Cookie or API proxy data.
    client_->setPropertyString("http-header-fields", "Referer: https://live.bilibili.com/");
    client_->setPropertyString("user-agent", BilibiliApiClient::userAgent());
    client_->setPropertyString("http-proxy", "");
    client_->setPropertyString("stream-lavf-o", "http_proxy=");
    client_->setPropertyString("speed", "1");
    client_->setPropertyString("volume", QString::number(volume_));
    client_->setPropertyString("cache", "yes");
    client_->setPropertyString("cache-pause-initial", "yes");
    client_->setPropertyString("demuxer-max-bytes", "33554432");
    connect(client_, &MpvClient::fileLoaded, this, [this] {
        if (!mediaActive_) return;
        loading_ = false;
        // Keep the startup timeout until actual decoded playback restarts.
        emit stateChanged();
    });
    connect(client_, &MpvClient::playbackRestarted, this, [this](double) {
        if (!mediaActive_) return;
        playbackStarted_ = true;
        watchdog_.stop();
        loading_ = false;
        buffering_ = false;
        emit stateChanged();
    });
    connect(client_, &MpvClient::playbackError, this, [this](int, const QString &) {
        if (mediaActive_) queueNextCandidate();
    });
    connect(client_, &MpvClient::ended, this, [this] {
        if (!mediaActive_) return;
        // EOF does not distinguish broadcaster offline from a severed CDN.
        fail(Loc::get("直播流已结束或中断，请重新连接确认直播状态"));
    });
    connect(client_, &MpvClient::pausedChanged, this, [this](bool paused) {
        if (!mediaActive_ || paused_ == paused) return;
        paused_ = paused;
        if (paused_) watchdog_.stop();
        else if (buffering_) watchdog_.start(kBufferTimeoutMs);
        else if (!playbackStarted_) watchdog_.start(kCandidateTimeoutMs);
        emit stateChanged();
    });
    connect(client_, &MpvClient::pausedForCacheChanged, this, [this](bool buffering) {
        if (!mediaActive_ || buffering_ == buffering) return;
        buffering_ = buffering;
        if (buffering_ && !paused_) watchdog_.start(kBufferTimeoutMs);
        else if (playbackStarted_) watchdog_.stop();
        emit stateChanged();
    });
    connect(client_, &MpvClient::kernelDead, this, [this] {
        fail(Loc::get("直播播放内核已停止，请重新连接"));
        releaseKernel();
    });
    return true;
}

void LivePlayerController::queueNextCandidate() {
    if (!mediaActive_) return;
    const quint64 generation = generation_;
    const quint64 candidate = candidateSerial_;
    QTimer::singleShot(0, this, [this, generation, candidate] {
        if (generation == generation_ && candidate == candidateSerial_ && mediaActive_)
            nextCandidate();
    });
}

void LivePlayerController::nextCandidate() {
    finishRecording(true);
    watchdog_.stop();
    ++candidateSerial_;
    if (++candidateIndex_ >= candidates_.size()) {
        fail(Loc::get("所有直播线路连接失败，请重新连接以获取最新地址"));
        return;
    }
    mediaActive_ = true;
    playbackStarted_ = false;
    loading_ = true;
    buffering_ = false;
    paused_ = false;
    emit stateChanged();
    watchdog_.start(kCandidateTimeoutMs);
    client_->loadSingle(candidates_.at(candidateIndex_), 0);
}

void LivePlayerController::fail(const QString &message) {
    finishRecording(true);
    watchdog_.stop();
    ++candidateSerial_;
    mediaActive_ = false;
    loading_ = false;
    buffering_ = false;
    paused_ = true;
    errorMessage_ = message;
    if (client_) {
        client_->setPaused(true);
        client_->stop();
    }
    emit stateChanged();
}

void LivePlayerController::togglePlayPause() {
    if (!client_ || !mediaActive_ || loading_) return;
    paused_ = !paused_;
    client_->setPaused(paused_);
    if (paused_) watchdog_.stop();
    else if (buffering_) watchdog_.start(kBufferTimeoutMs);
    else if (!playbackStarted_) watchdog_.start(kCandidateTimeoutMs);
    emit stateChanged();
}

void LivePlayerController::setVolumePercent(int percent) {
    percent = qBound(0, percent, 100);
    if (volume_ == percent) return;
    volume_ = percent;
    if (client_) client_->setPropertyString("volume", QString::number(volume_));
    emit volumeChanged();
}

void LivePlayerController::releaseKernel() {
    videoItem_->setClient(nullptr);
    if (client_) {
        disconnect(client_, nullptr, this, nullptr);
        client_->deleteLater();
        client_ = nullptr;
    }
}

void LivePlayerController::closeRequested() {
    finishRecording(false);
    ++generation_;
    ++candidateSerial_;
    watchdog_.stop();
    mediaActive_ = false;
    if (client_) {
        client_->setPaused(true);
        client_->stop();
    }
    releaseKernel();
    roomId_.clear();
    title_.clear();
    authorName_.clear();
    errorMessage_.clear();
    candidates_.clear();
    qualities_.clear();
    currentQn_ = 0;
    candidateIndex_ = -1;
    loading_ = buffering_ = false;
    paused_ = true;
    emit roomChanged();
    emit qualitiesChanged();
    emit stateChanged();
}

void LivePlayerController::toggleRecording() {
    if (!client_ || !mediaActive_ || loading_ || roomId_.isEmpty()) return;
    if (recording_) {
        finishRecording(true);
        return;
    }
    const QString root = configuredDownloadDirectory();
    const QString path = recordingPathFactory_ ? recordingPathFactory_(root, authorName_)
                                               : liveRecordingPath(root, authorName_);
    if (path.isEmpty()) {
        emit recordingFailed(Loc::get("直播录制启动失败：无法生成目标文件路径"));
        return;
    }
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        emit recordingFailed(Loc::get("直播录制启动失败：无法创建录制目录\n目录：%1")
                                 .arg(QDir::toNativeSeparators(directory)));
        return;
    }
    const QString directoryError = recordingDirectoryError(directory);
    if (!directoryError.isEmpty()) {
        emit recordingFailed(Loc::get("直播录制启动失败：%1\n目录：%2")
                                 .arg(directoryError, QDir::toNativeSeparators(directory)));
        return;
    }
    // mpv exposes recording as the runtime stream-record property. Use
    // forward slashes for the value passed into mpv; display the native path
    // in diagnostics and save notifications.
    const int result = client_->setPropertyString(QStringLiteral("stream-record"),
                                                   QDir::fromNativeSeparators(path));
    if (result < 0) {
        emit recordingFailed(Loc::get("直播录制启动失败：mpv stream-record 返回错误码 %1（%2）\n目录：%3\n目标文件：%4")
                                 .arg(result)
                                 .arg(MpvClient::errorString(result))
                                 .arg(QDir::toNativeSeparators(directory))
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }
    recordingPath_ = path;
    recording_ = true;
    emit recordingChanged();
}

bool LivePlayerController::finishRecording(bool notify) {
    if (!recording_) return true;
    const QString path = recordingPath_;
    const int result = client_ ? client_->setPropertyString(QStringLiteral("stream-record"), QString())
                               : MPV_ERROR_UNINITIALIZED;
    recording_ = false;
    recordingPath_.clear();
    emit recordingChanged();

    // stream-record updates are consumed by mpv's demuxer thread. A successful
    // property write does not mean the muxer has already flushed its trailer.
    const QPointer<LivePlayerController> guard(this);
    screenshotPool_.start([guard, path, result, notify] {
        QString error;
        if (result < 0) {
            error = Loc::get("mpv 返回错误码 %1（%2）")
                        .arg(result).arg(MpvClient::errorString(result));
        } else {
            QElapsedTimer timer;
            timer.start();
            do {
                QThread::msleep(50);
                error = recordingIntegrityError(path);
            } while (!error.isEmpty() && timer.elapsed() < 5000);
        }
        if (!error.isEmpty())
            qWarning().noquote() << "直播录制收尾失败：" << error
                                 << "文件：" << QDir::toNativeSeparators(path);
        if (notify) QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, path, error] {
            if (!guard) return;
            if (error.isEmpty()) emit guard->recordingSaved(path);
            else emit guard->recordingFailed(Loc::get("直播录制文件校验失败：%1\n文件：%2")
                                                 .arg(error, QDir::toNativeSeparators(path)));
        }, Qt::QueuedConnection);
    });
    return result >= 0;
}

void LivePlayerController::screenshot() {
    if (!client_ || !mediaActive_) {
        emit screenshotFailed(Loc::get("当前没有可截图的直播画面"));
        return;
    }
    // Screenshots retain their short live_<anchor>_<timestamp> session layout.
    const QString root = AppPaths::dataDir() + QStringLiteral("/Screenshots");
    const LiveSessionPaths session = liveSessionPaths(root, authorName_);
    const QString path = ScreenshotPath::next(session.directory, session.stem);
    auto capture = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/bbhouse-live-capture-XXXXXX"));
    if (!capture->isValid()) {
        emit screenshotFailed(Loc::get("截图失败"));
        return;
    }
    const QString rawPath = capture->filePath(QStringLiteral("frame.png"));
    client_->commandAsync({QStringLiteral("screenshot-to-file"), rawPath, QStringLiteral("video")}, this,
                          [this, capture, rawPath, path](int result) {
        if (result < 0 || !QFileInfo::exists(rawPath)) {
            emit screenshotFailed(Loc::get("截图失败"));
            return;
        }
        auto promise = std::make_shared<QPromise<ScreenshotWriter::Result>>();
        promise->start();
        auto *watcher = new QFutureWatcher<ScreenshotWriter::Result>(this);
        connect(watcher, &QFutureWatcher<ScreenshotWriter::Result>::finished, this,
                [this, watcher, capture] {
            const auto saved = watcher->result();
            watcher->deleteLater();
            if (saved.error.isEmpty()) emit screenshotSaved(saved.path);
            else emit screenshotFailed(Loc::get("截图失败") + QStringLiteral(": ") + saved.error);
        });
        watcher->setFuture(promise->future());
        screenshotPool_.start([promise, rawPath, path]() mutable {
            promise->addResult(ScreenshotWriter::save(rawPath, path));
            promise->finish();
        });
    });
}

void LivePlayerController::setFullscreenWindow(QWindow *window, bool fullscreen) {
    PlayerFullscreen::setBorderless(window, fullscreen);
}
