#include "player/LivePlayerController.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QThreadPool>
#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"

namespace {
constexpr int kCandidateTimeoutMs = 12000;
constexpr int kBufferTimeoutMs = 20000;
}

LivePlayerController::LivePlayerController(QObject *parent, Resolver resolver,
                                         ClientFactory clientFactory)
    : QObject(parent), resolver_(std::move(resolver)),
      clientFactory_(std::move(clientFactory)) {
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
