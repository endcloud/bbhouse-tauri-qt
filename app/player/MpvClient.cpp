#include "player/MpvClient.h"

#include <QThread>
#include <QDebug>
#include <QPointer>
#include <cmath>

#include <mpv/client.h>

#include "player/MpvLib.h"

namespace {
QString createError;
// 固定观察属性集(口径见 doc/wiliwili调研.md §3 与原 WinUI 项目)
const char *kObservedFlag[] = {"eof-reached", "pause", "seekable", "paused-for-cache"};
const char *kObservedDouble[] = {"duration"};
const char *kObservedString[] = {"hwdec-current"};
}  // namespace

MpvClient *MpvClient::create(QObject *parent) {
    MpvLib *lib = MpvLib::instance();
    lib->probe();
    if (!lib->available()) {
        createError = lib->errorMessage();
        return nullptr;
    }

    // 选项口径(对齐原 WinUI 项目 mpv 内核与 wiliwili 实证):
    // keep-open=always 播完只翻 eof-reached 不发 END_FILE;hr-seek 精确定位;
    // idle 复用实例;vo=libmpv 供 render API;hwdec=auto 硬解优先。
    mpv_handle *handle = lib->create();
    if (!handle) {
        createError = QStringLiteral("mpv_create failed");
        return nullptr;
    }

    auto setOpt = [&](const char *name, const char *value) {
        const int result = lib->setOptionString(handle, name, value);
        if (result < 0) qWarning() << "mpv option" << name << lib->errorString(result);
    };
    setOpt("vo", "libmpv");
#ifdef Q_OS_MACOS
    // libmpv's basic render API captures via the current decoded frame.
    // Native VideoToolbox surfaces cannot be converted by the screenshot
    // software scaler on macOS. Keep hardware decoding with CPU-readable
    // frames; do not switch decoders (or restart playback) on each capture.
    setOpt("hwdec", "auto-copy");
#else
    setOpt("hwdec", "auto-safe");
#endif
    setOpt("config", "no");
    // Media always connects directly, including FFmpeg's environment fallback.
    // API proxy credentials must never enter the media engine.
    setOpt("http-proxy", "");
    setOpt("stream-lavf-o", "http_proxy=");
    setOpt("terminal", "no");
    setOpt("keep-open", "always");
    setOpt("loop-file", "no");
    setOpt("idle", "yes");
    setOpt("hr-seek", "yes");
    setOpt("osd-level", "0");
    setOpt("input-default-bindings", "no");
    setOpt("input-vo-keyboard", "no");
    setOpt("input-cursor", "no");
    setOpt("audio-display", "no");
    setOpt("ytdl", "no");
    // Every new file starts without captions, including embedded default tracks.
    setOpt("sid", "no");
    setOpt("sub-auto", "no");
    setOpt("sub-visibility", "no");

    // Native mpv logs can contain HTTP Cookie headers and signed media URLs.
    // Do not enable its raw disk logger, including through inherited debug env.
    // Surface only structured error codes via our sanitized application events.
    setOpt("msg-level", "all=no");

    const int initResult = lib->initialize(handle);
    if (initResult < 0) {
        createError = QStringLiteral("mpv_initialize: ") + QString::fromUtf8(lib->errorString(initResult));
        qWarning().noquote() << createError;
        lib->terminateDestroy(handle);
        return nullptr;
    }

    auto *client = new MpvClient(parent);
    const auto destroy = lib->terminateDestroy;
    client->handle_ = std::shared_ptr<mpv_handle>(handle, [destroy](mpv_handle *value) { destroy(value); });
    createError.clear();
    lib->setWakeupCallback(handle, &MpvClient::onWakeup, client);

    for (const char *name : kObservedFlag)
        client->observeProperty(QString::fromUtf8(name), MPV_FORMAT_FLAG);
    for (const char *name : kObservedDouble)
        client->observeProperty(QString::fromUtf8(name), MPV_FORMAT_DOUBLE);
    for (const char *name : kObservedString)
        client->observeProperty(QString::fromUtf8(name), MPV_FORMAT_STRING);
    return client;
}

QString MpvClient::lastCreateError() { return createError; }

MpvClient::MpvClient(QObject *parent) : QObject(parent) {
    audioWatchdog_.setSingleShot(true);
    audioWatchdog_.setInterval(8000);
    connect(&audioWatchdog_, &QTimer::timeout, this, [this] {
        cancelAudioLoad();
        tryNextAudioCandidate(MPV_ERROR_LOADING_FAILED);
    });
}

MpvClient::~MpvClient() {
    MpvLib *lib = MpvLib::instance();
    if (handle_) {
        cancelAudioLoad();
        // Detach the callback before QObject teardown. The render thread owns
        // its own handle reference until mpv_render_context_free completes.
        lib->setWakeupCallback(handle_.get(), nullptr, nullptr);
        handle_.reset();
    }
}

void MpvClient::onWakeup(void *self) {
    // mpv 线程回调:转 queued 信号到创建线程排空
    QMetaObject::invokeMethod(static_cast<MpvClient *>(self), "takeEvents",
                              Qt::QueuedConnection);
}

void MpvClient::takeEvents() {
    MpvLib *lib = MpvLib::instance();
    if (!handle_ || !lib->waitEvent) return;
    while (true) {
        // client.h 仅提供类型(无链接依赖);调用经 MpvLib 函数指针
        mpv_event *event = lib->waitEvent(handle_.get(), 0);
        if (!event || event->event_id == MPV_EVENT_NONE) break;
        switch (event->event_id) {
            case MPV_EVENT_COMMAND_REPLY: {
                auto completion = pendingCommands_.take(event->reply_userdata);
                if (completion) completion(event->error);
                break;
            }
            case MPV_EVENT_START_FILE:
                if (event->data)
                    startedEntryId_ = static_cast<mpv_event_start_file *>(event->data)->playlist_entry_id;
                break;
            case MPV_EVENT_FILE_LOADED:
                if (requestedEntryId_ < 0 || startedEntryId_ != requestedEntryId_) break;
                // 外挂音轨补挂:audio-add 必须在主文件装载完成后发,否则被静默丢弃
                if (!pendingAudioUrls_.isEmpty()) {
                    emit audioLoadStarted();
                    tryNextAudioCandidate(MPV_ERROR_LOADING_FAILED);
                    break;
                }
                // A new controller can still hold its idle/paused state even
                // when mpv never changed its default pause=false. Reconcile at
                // each load too, including coalesced pause/resume notifications.
                handlePropertyChange(QStringLiteral("pause"));
                emit fileLoaded();
                break;
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto *prop = static_cast<mpv_event_property *>(event->data);
                if (prop && prop->name)
                    handlePropertyChange(QString::fromUtf8(prop->name));
                break;
            }
            case MPV_EVENT_PLAYBACK_RESTART:
                if (requestedEntryId_ >= 0 && startedEntryId_ == requestedEntryId_
                        && !getFlag("seeking"))
                    emit playbackRestarted(getPropertyDouble("playback-time"));
                break;
            case MPV_EVENT_END_FILE: {
                const auto *end = static_cast<mpv_event_end_file *>(event->data);
                if (end && end->playlist_entry_id == requestedEntryId_ &&
                    end->reason == MPV_END_FILE_REASON_ERROR) {
                    ++loadSerial_;
                    cancelAudioLoad();
                    requestedEntryId_ = -1;
                    pendingAudioUrls_.clear();
                    emit playbackError(end->error, QString::fromUtf8(lib->errorString(end->error)));
                }
                break;
            }
            case MPV_EVENT_SHUTDOWN:
                emit kernelDead();
                break;
            default:
                break;
        }
    }
}

void MpvClient::handlePropertyChange(const QString &name) {
    if (name == "eof-reached") {
        const bool eof = getFlag("eof-reached");
        if (eof && !lastEof_) emit ended();
        lastEof_ = eof;
    } else if (name == "pause") {
        // mpv already coalesces property events. Always deliver the first
        // snapshot; consumers deduplicate against their own UI state.
        emit pausedChanged(getFlag("pause"));
    } else if (name == "duration") {
        emit durationChanged(getPropertyDouble("duration"));
    } else if (name == "seekable") {
        emit seekableChanged(getFlag("seekable"));
    } else if (name == "paused-for-cache") {
        emit pausedForCacheChanged(getFlag("paused-for-cache"));
    } else if (name == "hwdec-current") {
        emit hwdecChanged(getPropertyString("hwdec-current"));
    }
}

void MpvClient::setOptionString(const QString &name, const QString &value) {
    if (!handle_) return;
    auto *lib = MpvLib::instance();
    const int result = lib->setOptionString(handle_.get(), name.toUtf8(), value.toUtf8());
    if (result < 0) qWarning() << "mpv option" << name << lib->errorString(result);
}

QString MpvClient::getPropertyString(const QString &name) const {
    if (!handle_) return {};
    char *value = nullptr;
    if (MpvLib::instance()->getProperty(handle_.get(), name.toUtf8(), mpvf::kString, &value) < 0)
        return {};
    QString out = QString::fromUtf8(value);
    MpvLib::instance()->freeFn(value);
    return out;
}

double MpvClient::getPropertyDouble(const QString &name) const {
    if (!handle_) return 0.0;
    double value = 0.0;
    if (MpvLib::instance()->getProperty(handle_.get(), name.toUtf8(), mpvf::kDouble, &value) < 0)
        return 0.0;
    return value;
}

bool MpvClient::getFlag(const QString &name) const {
    if (!handle_) return false;
    int value = 0;
    if (MpvLib::instance()->getProperty(handle_.get(), name.toUtf8(), mpvf::kFlag, &value) < 0)
        return false;
    return value != 0;
}

void MpvClient::setPropertyString(const QString &name, const QString &value) {
    if (!handle_) return;
    auto *lib = MpvLib::instance();
    const int result = lib->setPropertyStringFn(handle_.get(), name.toUtf8(), value.toUtf8());
    if (result < 0) qWarning() << "mpv property" << name << lib->errorString(result);
}

void MpvClient::observeProperty(const QString &name, mpv_format format) {
    if (!handle_) return;
    auto *lib = MpvLib::instance();
    const int result = lib->observeProperty(handle_.get(), 0, name.toUtf8(), format);
    if (result < 0) qWarning() << "mpv observe" << name << lib->errorString(result);
}

int MpvClient::command(const QStringList &args) {
    if (!handle_ || args.isEmpty()) return MPV_ERROR_UNINITIALIZED;
    QVector<QByteArray> bytes;
    bytes.reserve(args.size());
    for (const QString &arg : args) bytes.append(arg.toUtf8());
    QVector<const char *> pointers;
    pointers.reserve(bytes.size() + 1);
    for (QByteArray &b : bytes) pointers.append(b.data());
    pointers.append(nullptr);
    const int result = MpvLib::instance()->commandFn(handle_.get(), pointers.data());
    // Only the command name is logged: arguments may contain signed URLs.
    if (result < 0) qWarning() << "mpv command" << args.first() << MpvLib::instance()->errorString(result);
    return result;
}

quint64 MpvClient::commandAsync(const QStringList &args, QObject *context,
                              std::function<void(int)> completion) {
    const QPointer<QObject> receiver(context);
    auto deliver = [receiver, completion = std::move(completion)](int result) {
        if (!receiver) return;
        QMetaObject::invokeMethod(receiver, [completion, result] { completion(result); },
                                  Qt::QueuedConnection);
    };
    if (!handle_ || args.isEmpty()) {
        deliver(MPV_ERROR_UNINITIALIZED);
        return 0;
    }
    QVector<QByteArray> bytes;
    bytes.reserve(args.size());
    for (const auto &arg : args) bytes.append(arg.toUtf8());
    QVector<const char *> pointers;
    pointers.reserve(bytes.size() + 1);
    for (const auto &arg : bytes) pointers.append(arg.constData());
    pointers.append(nullptr);
    const quint64 id = ++nextCommandId_;
    pendingCommands_.insert(id, deliver);
    const int result = MpvLib::instance()->commandAsyncFn(handle_.get(), id, pointers.data());
    if (result < 0) {
        pendingCommands_.remove(id);
        deliver(result); // Submission failures do not produce COMMAND_REPLY.
    }
    return result < 0 ? 0 : id;
}

void MpvClient::cancelAudioLoad() {
    audioWatchdog_.stop();
    ++audioAttemptSerial_;
    if (!audioCommandId_) return;
    MpvLib::instance()->abortAsyncCommand(handle_.get(), audioCommandId_);
    pendingCommands_.remove(audioCommandId_);
    audioCommandId_ = 0;
}

void MpvClient::tryNextAudioCandidate(int previousError) {
    if (requestedEntryId_ < 0 || startedEntryId_ != requestedEntryId_) return;
    if (pendingAudioUrls_.isEmpty()) {
        emit playbackError(previousError,
                           QString::fromUtf8(MpvLib::instance()->errorString(previousError)));
        return;
    }
    const QString audioUrl = pendingAudioUrls_.takeFirst();
    const quint64 serial = loadSerial_;
    const qint64 entryId = requestedEntryId_;
    const quint64 attempt = ++audioAttemptSerial_;
    audioWatchdog_.start();
    audioCommandId_ = commandAsync({"audio-add", audioUrl, "select"}, this,
        [this, serial, entryId, attempt](int result) {
            if (serial != loadSerial_ || entryId != requestedEntryId_
                    || attempt != audioAttemptSerial_) return;
            audioCommandId_ = 0;
            audioWatchdog_.stop();
            if (result < 0) {
                tryNextAudioCandidate(result);
                return;
            }
            pendingAudioUrls_.clear();
            handlePropertyChange(QStringLiteral("pause"));
            emit fileLoaded();
        });
}

void MpvClient::loadFile(const QString &url, double startSeconds) {
    if (!handle_) return;
    const quint64 serial = ++loadSerial_;
    cancelAudioLoad();
    requestedEntryId_ = -1;
    setPropertyString("pause", "no");
    QStringList args = {"loadfile", url, "replace"};
    if (std::isfinite(startSeconds) && startSeconds > 0) {
        // mpv 0.38 / client API 2.3 inserted the playlist index before options.
        if (MpvLib::instance()->clientApiVersion() >= MPV_MAKE_VERSION(2, 3))
            args.append("-1");
        args.append("start=" + QString::number(startSeconds, 'f', 3));
    }
    lastEof_ = false;
    const int result = command(args);
    if (result >= 0) {
        int64_t id = -1;
        if (MpvLib::instance()->getProperty(handle_.get(), "playlist/0/id", MPV_FORMAT_INT64, &id) >= 0)
            requestedEntryId_ = id;
    }
    if (result < 0) {
        pendingAudioUrls_.clear();
        const QString message = QString::fromUtf8(MpvLib::instance()->errorString(result));
        QMetaObject::invokeMethod(this, [this, result, message, serial] {
            if (serial != loadSerial_) return;
            emit playbackError(result, message);
        }, Qt::QueuedConnection);
    }
}

void MpvClient::loadDash(const QString &videoUrl, const QString &audioUrl,
                         double startSeconds) {
    loadDashCandidates(videoUrl, audioUrl.isEmpty() ? QStringList() : QStringList{audioUrl},
                       startSeconds);
}

void MpvClient::loadDashCandidates(const QString &videoUrl, const QStringList &audioUrls,
                                  double startSeconds) {
    pendingAudioUrls_ = audioUrls;
    pendingAudioUrls_.removeAll(QString());
    pendingAudioUrls_.removeDuplicates();
    loadFile(videoUrl, startSeconds);
}

void MpvClient::loadSingle(const QString &url, double startSeconds) {
    pendingAudioUrls_.clear();
    loadFile(url, startSeconds);
}

void MpvClient::stop() {
    ++loadSerial_;
    cancelAudioLoad();
    requestedEntryId_ = -1;
    pendingAudioUrls_.clear();
    if (handle_) command({"stop"});
}

void MpvClient::setPaused(bool paused) {
    setPropertyString("pause", paused ? "yes" : "no");
}
