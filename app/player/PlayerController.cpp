#include "core/PlaybackEntry.h"
#include "player/PlayerController.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QUrl>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QPromise>
#include <QSet>
#include <QTemporaryDir>
#include <QThreadPool>

#include "core/ApiErrors.h"
#include "core/AppPaths.h"
#include "core/BilibiliApiClient.h"
#include "core/DanmakuParser.h"
#include "core/HeartbeatApi.h"
#include "core/PlayerApi.h"
#include "preferences/AppPreferences.h"
#include "player/MpvLib.h"
#include "player/ScreenshotPath.h"
#include "player/ScreenshotWriter.h"

namespace {
// 持久化键名(规格 persist-playback-speed / 弹幕开关跨窗口保持)
constexpr const char *kKeyKeepSpeed = "App.Player.KeepSpeed";
constexpr const char *kKeySpeed = "App.Player.Speed";
constexpr const char *kKeyPreferCodec = "App.Player.PreferCodec";

constexpr int kPollIntervalMs = 100;        // 位置轮询(弹幕时基 100ms 级)
constexpr qint64 kSaveIntervalMs = 5000;    // 位置落库节流:每 5s
constexpr double kSaveMinDelta = 1.0;       // 且位移 >1s
constexpr int kCandidateTimeoutMs = 8000;   // 单候选 8s 未 file-loaded 换下一候选

}  // namespace

PlayerController::PlayerController(QObject *parent)
    : QObject(parent), store_(AppPaths::dbPath()) {
    workerPool_.setMaxThreadCount(4);
    onlineDanmaku_.loaded = [this](QVariantList entries) { emit danmakuLoaded(entries); };
    onlineDanmaku_.failed = [this](QString message) { emit danmakuLoadFailed(message); };
    screenshotPool_.setMaxThreadCount(1);
    localMediaPool_.setMaxThreadCount(1);
    subtitlePool_.setMaxThreadCount(2);
    connect(this, &PlayerController::positionChanged, this, &PlayerController::updateSubtitleText);
    AppPreferences *prefs = AppPreferences::instance();
    danmakuOn_ = prefs->danmakuEnabled();
    keepSpeed_ = prefs->value(QString::fromLatin1(kKeyKeepSpeed), false).toBool();
    // 保持开关关闭 = 纯会话级语义:恒为默认 1x,不读已存倍速
    speed_ = keepSpeed_
                     ? prefs->value(QString::fromLatin1(kKeySpeed), 1.0).toDouble()
                     : 1.0;
    preferCodec_ = prefs->value(QString::fromLatin1(kKeyPreferCodec), "hev1").toString();

    systemMedia_ = new SystemMediaControls(this);
    connect(systemMedia_, &SystemMediaControls::commandRequested, this,
            [this](SystemMediaCommand command, double position) {
        switch (command) {
        case SystemMediaCommand::Play:
        case SystemMediaCommand::Pause:
        case SystemMediaCommand::Toggle: togglePlayPause(); break;
        case SystemMediaCommand::Previous: playByIndex(currentIndex_ - 1); break;
        case SystemMediaCommand::Next: playNext(); break;
        case SystemMediaCommand::Seek: seek(position); break;
        case SystemMediaCommand::Stop:
            // Keep the playlist/window available; native Stop pauses at the current position.
            if (!paused_) togglePlayPause();
            break;
        }
    });
    for (auto signal : {&PlayerController::pausedChanged, &PlayerController::durationChanged,
                        &PlayerController::seekableChanged, &PlayerController::bufferingChanged,
                        &PlayerController::loadingChanged, &PlayerController::speedChanged,
                        &PlayerController::playlistChanged, &PlayerController::currentChanged,
                        &PlayerController::kernelAvailableChanged})
        connect(this, signal, this, &PlayerController::syncSystemMedia);
    connect(this, &PlayerController::errorOccurred, this, &PlayerController::syncSystemMedia);
    mediaTimer_.setInterval(1000);
    connect(&mediaTimer_, &QTimer::timeout, this, &PlayerController::syncSystemMedia);

    // 视频项与弹幕项:C++ 创建,窗口 QML 仅挂载(reparent),跨窗口复用
    videoItem_ = new MpvVideoItem();
    connect(videoItem_, &MpvVideoItem::renderReadyChanged, this, [this] {
        if (!waitingRenderReady_ || !videoItem_->renderReady()) return;
        waitingRenderReady_ = false;
        tryNextVideoCandidate();
    });
    connect(videoItem_, &MpvVideoItem::renderError, this, [this](const QString &message) {
        if (!client_ || currentKey_.isEmpty()) return;
        client_->stop();
        playbackFailed_ = true;
        loading_ = false;
        buffering_ = false;
        waitingFileLoaded_ = false;
        waitingRenderReady_ = false;
        candidateWatchdog_.stop();
        emit loadingChanged();
        emit bufferingChanged();
        emitError(Loc::get("视频渲染初始化失败: %1").arg(message));
    });
    danmakuItem_ = new DanmakuEngine();
    danmakuItem_->setEnabled(danmakuOn_);
    danmakuItem_->setImplementation(prefs->danmakuImplementation());
    danmakuItem_->setOpacityPercent(prefs->danmakuOpacity());
    danmakuItem_->setFontSize(prefs->danmakuFontSize());
    danmakuItem_->setAreaPercent(prefs->danmakuArea());
    danmakuItem_->setDensityLimit(prefs->danmakuDensity());
    danmakuItem_->setMergeSimilar(prefs->danmakuMergeSimilar());
    connect(prefs, &AppPreferences::danmakuEnabledChanged, this, [this, prefs] {
        danmakuOn_ = prefs->danmakuEnabled();
        danmakuItem_->setEnabled(danmakuOn_);
        emit danmakuOnChanged();
    });
    connect(prefs, &AppPreferences::danmakuOpacityChanged, this, [this, prefs] {
        danmakuItem_->setOpacityPercent(prefs->danmakuOpacity());
    });
    connect(prefs, &AppPreferences::danmakuFontSizeChanged, this, [this, prefs] {
        danmakuItem_->setFontSize(prefs->danmakuFontSize());
    });
    connect(prefs, &AppPreferences::danmakuAreaChanged, this, [this, prefs] {
        danmakuItem_->setAreaPercent(prefs->danmakuArea());
    });
    connect(prefs, &AppPreferences::danmakuDensityChanged, this, [this, prefs] {
        danmakuItem_->setDensityLimit(prefs->danmakuDensity());
    });
    connect(prefs, &AppPreferences::danmakuMergeSimilarChanged, this, [this, prefs] {
        danmakuItem_->setMergeSimilar(prefs->danmakuMergeSimilar());
    });
    connect(prefs, &AppPreferences::danmakuImplementationChanged, this, [this, prefs] {
        danmakuItem_->setImplementation(prefs->danmakuImplementation());
    });

    // 位置轮询:位置变化驱动进度条/时间文本 + 弹幕时基回填 + 位置写穿
    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, [this] {
        if (!client_ || !kernelAlive_ || currentKey_.isEmpty() || (loading_ && !qualityResolving_) || playbackFailed_) return;
        const double pos = client_->getPropertyDouble("playback-time");
        if (pos >= 0 && !qFuzzyCompare(pos + 1.0, position_ + 1.0)) {
            position_ = pos;
            emit positionChanged();
        }
        if (danmakuItem_) {
            danmakuItem_->setPlayback(position_, speed_, paused_, !paused_ && !buffering_);
        }
        // 位置写穿:每 5s 且 |Δ|>1s 节流落库 + 会话恢复点更新
        if (!localMedia() && duration_ > 0 && qAbs(position_ - lastSavedPos_) > kSaveMinDelta &&
            saveClock_.elapsed() >= kSaveIntervalMs) {
            lastSavedPos_ = position_;
            saveClock_.restart();
            sessionPositions_.insert(currentKey_, position_);
            const QString key = currentKey_;
            const double p = position_;
            const double d = duration_;
            workerPool_.start([this, key, p, d] {
                ensureStoreReady();
                store_.savePlaybackPosition(key, p, d);
            });
        }
    });

    // 错误事件立即换候选；连接无响应时由看门狗兜底，最终回落单流。
    candidateWatchdog_.setSingleShot(true);
    connect(&candidateWatchdog_, &QTimer::timeout, this, [this] {
        if (!waitingFileLoaded_) return;
        tryNextVideoCandidate();
    });

    saveClock_.start();
}

PlayerController::~PlayerController() {
    pollTimer_.stop();
    candidateWatchdog_.stop();
    mediaTimer_.stop();
    workerPool_.waitForDone();
    localMediaPool_.waitForDone();
    subtitlePool_.waitForDone();
    delete systemMedia_;
    systemMedia_ = nullptr;
    delete videoItem_;
    delete danmakuItem_;
}

void PlayerController::attachMediaWindow(QWindow *window) {
    disconnect(mediaWindowDestroyed_);
    mediaWindowAttached_ = window != nullptr;
    systemMedia_->setWindow(window ? static_cast<quintptr>(window->winId()) : 0);
    if (window) {
        mediaWindowDestroyed_ = connect(window, &QObject::destroyed, this, [this] {
            mediaWindowAttached_ = false;
            ++mediaSession_;
            mediaTimer_.stop();
            if (systemMedia_) {
                systemMedia_->clear();
                systemMedia_->setWindow(0);
            }
        });
    }
    syncSystemMedia();
}

void PlayerController::syncSystemMedia() {
    if (!systemMedia_) return;
    SystemMediaState state;
    state.session = mediaSession_;
    state.active = mediaWindowAttached_ && mediaReady_ && kernelAvailable()
        && !currentKey_.isEmpty() && !playbackFailed_;
    state.title = currentTitle_;
    state.artist = currentEntry().value("authorName").toString();
    state.coverUrl = currentEntry().value("coverUrl").toString();
    if (state.artist.isEmpty() && seasonMode_) state.artist = seasonTitle_;
    state.paused = paused_;
    state.buffering = buffering_ || loading_;
    state.canSeek = seekable_ && !loading_;
    state.canPrevious = currentIndex_ > 0;
    state.canNext = currentIndex_ >= 0 && currentIndex_ + 1 < list_.size();
    state.position = position_;
    state.duration = duration_;
    state.rate = speed_;
    systemMedia_->update(state);
    if (state.active && !mediaTimer_.isActive()) mediaTimer_.start();
    if (!state.active) mediaTimer_.stop();
}

bool PlayerController::kernelAvailable() const {
    return client_ && kernelAlive_ && client_->isValid();
}

QVariantList PlayerController::playlist() const { return list_; }

int PlayerController::currentIndex() const { return currentIndex_; }

bool PlayerController::paused() const { return paused_; }

double PlayerController::position() const { return position_; }

double PlayerController::duration() const { return duration_; }

bool PlayerController::seekable() const { return seekable_; }

bool PlayerController::buffering() const { return buffering_; }

bool PlayerController::loading() const { return loading_; }

bool PlayerController::fallback() const { return fallback_; }

QString PlayerController::currentTitle() const { return currentTitle_; }
bool PlayerController::localMedia() const { return PlaybackEntry::isLocal(currentEntry()); }

bool PlayerController::seasonMode() const { return seasonMode_; }

QString PlayerController::seasonTitle() const { return seasonTitle_; }

QString PlayerController::qualityLabel() const { return qualityLabel_; }

QVariantList PlayerController::qualities() const { return qualities_; }

int PlayerController::currentQn() const { return selectedQn_; }

QString PlayerController::preferCodec() const { return preferCodec_; }

double PlayerController::speed() const { return speed_; }

bool PlayerController::keepSpeed() const { return keepSpeed_; }

int PlayerController::volumePercent() const { return volume_; }

bool PlayerController::danmakuOn() const { return danmakuOn_; }

void PlayerController::ensureStoreReady() {
    std::call_once(storeInitFlag_, [this] {
        store_.initialize();
    });
}

void PlayerController::emitError(const QString &message) {
    QMetaObject::invokeMethod(
            this, [this, message] { emit errorOccurred(message); },
            Qt::QueuedConnection);
}

int PlayerController::indexOfKey(const QString &key) const {
    for (int i = 0; i < list_.size(); ++i) {
        if (list_.at(i).toMap().value("videoKey").toString() == key) return i;
    }
    return -1;
}

QVariantMap PlayerController::currentEntry() const {
    return currentIndex_ >= 0 && currentIndex_ < list_.size()
                   ? list_.at(currentIndex_).toMap()
                   : QVariantMap();
}

void PlayerController::openWith(QVariantList entries) {
    if (entries.isEmpty()) return;
    // 普通起播入口:退出剧集模式,恢复播放列表面板(剧集分集列表让位给新会话列表)
    if (seasonMode_) {
        if (currentIndex_ >= 0) flushPosition();
        currentIndex_ = -1;
        currentKey_.clear();
        currentCid_ = 0;
        seasonMode_ = false;
        seasonTitle_.clear();
        emit seasonModeChanged();
        emit seasonTitleChanged();
        list_.clear();
    }
    int target = -1;
    QVariantList merged = list_;
    for (const QVariant &value : entries) {
        const QVariantMap entry = PlaybackEntry::normalize(value.toMap());
        const QString key = entry.value("videoKey").toString();
        if (key.isEmpty()) continue;
        if (!PlaybackEntry::playable(entry)) {
            if (PlaybackEntry::isLocal(entry)) emitError(Loc::get("本地媒体文件不存在、为空或无法读取"));
            continue;
        }
        int existing = -1;
        for (int i = 0; i < merged.size(); ++i)
            if (merged.at(i).toMap().value("videoKey").toString() == key) { existing = i; break; }
        if (existing >= 0) {
            target = existing;  // 已在列表:去重,只切播
            continue;
        }
        merged.append(entry);
        target = merged.size() - 1;
    }
    if (target < 0) return;
    const bool changed = merged.size() != list_.size();
    list_ = merged;
    if (changed) emit playlistChanged();
    playByIndex(target);
}

void PlayerController::playRange(QVariantList entries) {
    // 批量起播入列(合集"播放全部"):过滤不可播 → 按 videoKey 保序去重 →
    // 追加到既有列表(不清空)→ 定位本批首项起播;空集不开窗
    QVariantList cleaned;
    QSet<QString> seen;
    for (const QVariant &value : entries) {
        const QVariantMap entry = PlaybackEntry::normalize(value.toMap());
        if (!PlaybackEntry::playable(entry)) continue;
        const QString key = entry.value("videoKey").toString();
        if (key.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        cleaned.append(entry);
    }
    if (cleaned.isEmpty()) return;
    // 剧集模式下播放列表是分集表,与合流语义冲突:退出并释放(同 openWith)
    if (seasonMode_) {
        if (currentIndex_ >= 0) flushPosition();
        currentIndex_ = -1;
        currentKey_.clear();
        currentCid_ = 0;
        seasonMode_ = false;
        seasonTitle_.clear();
        emit seasonModeChanged();
        emit seasonTitleChanged();
        list_.clear();
        currentIndex_ = -1;
    }
    // 本批首项的落点:已在列表用其既有位置(spec"起播定位到该合集首集"),
    // 否则为追加后的新位置
    const QString firstKey = cleaned.first().toMap().value("videoKey").toString();
    QVariantList merged = list_;
    for (const QVariant &value : cleaned) {
        const QString key = value.toMap().value("videoKey").toString();
        if (indexOfKey(key) >= 0) continue;  // 已在列表:不重复入列
        merged.append(value);
    }
    int target = -1;
    for (int i = 0; i < merged.size(); ++i) {
        if (merged.at(i).toMap().value("videoKey").toString() == firstKey) {
            target = i;
            break;
        }
    }
    if (target < 0) return;
    const bool changed = merged.size() != list_.size();
    list_ = merged;
    if (changed) emit playlistChanged();
    playByIndex(target);  // 点击当前条目时 playByIndex 早退,不重播
}

void PlayerController::playSeason(QVariantMap season, QVariantList episodes, int startIndex) {
    if (episodes.isEmpty()) return;
    const qint64 lastEpId = season.value("lastEpId").toLongLong();
    const int lastTime = season.value("lastTimeSeconds").toInt();
    QVariantList entries;
    entries.reserve(episodes.size());
    for (const QVariant &value : episodes) {
        const QVariantMap episode = value.toMap();
        const qint64 epId = episode.value("epId").toLongLong();
        if (epId <= 0) continue;
        QVariantMap entry;
        // 剧集分集键与播放器免分 P 形态同构:pgc:{ep_id}:0
        entry.insert("videoKey", QStringLiteral("pgc:%1:0").arg(epId));
        const QString longTitle = episode.value("longTitle").toString();
        entry.insert("title", longTitle.isEmpty() ? episode.value("title").toString()
                                                  : longTitle);
        entry.insert("subtitle", season.value("title").toString());
        const QString episodeCover = episode.value("coverUrl").toString();
        entry.insert("coverUrl", episodeCover.isEmpty() ? season.value("coverUrl").toString()
                                                       : episodeCover);
        entry.insert("business", QStringLiteral("pgc"));
        entry.insert("regionalApiProxy", season.value("regional").toBool());
        entry.insert("epId", epId);
        entry.insert("cid", episode.value("cid").toLongLong());
        entry.insert("duration", episode.value("duration").toDouble());
        // 云端观看位置:仅最近观看分集携带 last_time,集内定位沿用既有续播规则
        entry.insert("progress", epId == lastEpId ? static_cast<double>(lastTime) : 0.0);
        entries.append(entry);
    }
    if (entries.isEmpty()) return;
    // 旧条目收尾后重置选择态(playByIndex 对相同索引会早退,须先清)
    if (currentIndex_ >= 0) flushPosition();
    endedAwaitingSwitch_ = false;
    currentIndex_ = -1;
    currentKey_.clear();
    currentCid_ = 0;
    // 进入剧集模式:playlist 即分集表(整表替换,非合流)
    seasonMode_ = true;
    seasonTitle_ = season.value("title").toString();
    emit seasonModeChanged();
    emit seasonTitleChanged();
    list_ = entries;
    emit playlistChanged();
    playByIndex(qBound(0, startIndex, static_cast<int>(entries.size()) - 1));
}

bool PlayerController::isResumable(double position, double duration) {
    return position > 5 && (duration <= 0 || duration - position > 10);
}

double PlayerController::computeStartSeconds(const ResolveRequest &request) {
    const double duration = request.durationHint;
    // 三级续播:会话恢复点 > 本地持久化 > 云端进度
    if (request.sessionStart >= 0) {
        return isResumable(request.sessionStart, duration) ? request.sessionStart : 0.0;
    }
    const std::optional<double> local =
            store_.getPlaybackPosition(request.entry.value("videoKey").toString());
    // 本地播放位置回退 5 秒起播(距片尾 >10s 才跳,否则从头)
    if (local) return isResumable(*local, duration) ? qMax(0.0, *local - 5.0) : 0.0;
    const double progress = request.entry.value("progress", 0).toDouble();
    return isResumable(progress, duration) ? progress : 0.0;
}

void PlayerController::startPlayback(const QVariantMap &entry, double explicitStart,
                                     bool qualitySwitch) {
    generation_++;
    if (!qualitySwitch) {
        resetSubtitles();
        onlineDanmaku_.reset();
    }
    waitingRenderReady_ = false;
    waitingFileLoaded_ = false;
    candidateWatchdog_.stop();
    pendingVideoUrls_.clear();
    qualityResolving_ = qualitySwitch && kernelAvailable() && !playbackFailed_;
    playbackFailed_ = false;
    ResolveRequest request;
    request.generation = generation_;
    request.preferredQn = preferredQn_;
    request.preferCodec = preferCodec_;
    request.entry = entry;
    if (entry.value("business").toString() == QLatin1String("pgc") &&
        entry.value("regionalApiProxy").toBool()) {
        request.apiProxy = AppPreferences::instance()->regionalProxy();
    }
    request.qualitySwitch = qualitySwitch;
    request.explicitStart = explicitStart;
    request.durationHint = entry.value("duration").toDouble();
    // 会话恢复点在主线程读取快照(哈希仅主线程读写)
    const QString key = entry.value("videoKey").toString();
    request.sessionStart =
            sessionPositions_.contains(key) ? sessionPositions_.value(key) : -1.0;
    if (PlaybackEntry::isLocal(entry)) {
        startLocalPlayback(request);
        return;
    }
    workerPool_.start([this, request] { resolveEntry(request); });
}

void PlayerController::startLocalPlayback(const ResolveRequest &request) {
    // This branch deliberately precedes all Cookie, API and HistoryStore access.
    qualityResolving_ = false;
    fallback_ = false;
    emit fallbackChanged(false);
    const QString path = request.entry.value("localPath").toString();
    if (!PlaybackEntry::readableLocalFile(path)) {
        handleResolveFailed(request, Loc::get("本地媒体文件不存在、为空或无法读取"));
        return;
    }
    if (!ensureKernel()) {
        handleResolveFailed(request, Loc::get("播放内核不可用: %1").arg(MpvClient::lastCreateError()));
        return;
    }
    // A reused online client must not retain credentials for local playback.
    client_->setPropertyString("http-header-fields", QString());
    client_->setPropertyString("sub-auto", "no");
    activeRequest_ = request;
    pendingVideoUrls_ = {QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)};
    pendingAudioUrls_.clear();
    pendingStart_ = request.explicitStart;
    candidateIndex_ = 0;
    currentCid_ = 0;
    selectedQn_ = 0;
    qualities_.clear();
    qualityLabel_ = Loc::get("本地媒体");
    emit qualitiesChanged();
    tryNextVideoCandidate();
    loadLocalDanmaku(request.entry);
}

void PlayerController::loadLocalDanmaku(const QVariantMap &entry) {
    const QFileInfo media(entry.value("localPath").toString());
    const QString sibling = media.dir().filePath(media.completeBaseName() + ".xml");
    const QString specified = entry.value("danmakuPath").toString();
    const int generation = generation_;
    localMediaPool_.start([this, sibling, specified, generation] {
        const QString path = PlaybackEntry::readableLocalFile(sibling) ? sibling : specified;
        if (!PlaybackEntry::readableLocalFile(path) ||
            QFileInfo(path).suffix().compare("xml", Qt::CaseInsensitive) != 0) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return;
        const auto parsed = DanmakuParser::parseXml(QString::fromUtf8(file.readAll()));
        QVariantList entries;
        entries.reserve(parsed.size());
        for (const auto &item : parsed) {
            entries.append(QVariantMap{{"time", item.time}, {"type", item.type},
                {"fontSize", item.fontSize}, {"fontColor", item.fontColor},
                {"level", item.level}, {"message", item.message}});
        }
        QMetaObject::invokeMethod(this, [this, generation, entries] {
            if (generation != generation_) return;
            emit danmakuLoaded(entries);
        }, Qt::QueuedConnection);
    });
}

void PlayerController::resetSubtitles() {
    ++subtitleEpoch_;
    ++subtitleSelection_;
    subtitleCatalogRequested_ = false;
    subtitleTracks_.clear();
    onlineSubtitleTracks_.clear();
    subtitleCookie_.clear();
    subtitleTimeline_ = {};
    selectedSubtitle_ = -1;
    if (client_ && kernelAlive_) {
        client_->setPropertyString("sub-visibility", "no");
        client_->setPropertyString("sid", "no");
    }
    emit subtitleTracksChanged();
    emit selectedSubtitleChanged();
    updateSubtitleText();
}

void PlayerController::updateSubtitleText() {
    const QString text = selectedSubtitle_ >= 0 && !localMedia()
            ? subtitleTimeline_.textAt(position_) : QString();
    if (text == subtitleText_) return;
    subtitleText_ = text;
    emit subtitleTextChanged();
}

void PlayerController::loadOnlineSubtitles(const ResolveRequest &request) {
    if (subtitleCatalogRequested_) return;
    subtitleCatalogRequested_ = true;
    const quint64 epoch = subtitleEpoch_;
    subtitleCookie_ = request.cookie;
    subtitlePool_.start([this, request, epoch] {
        QList<PlayerApi::SubtitleTrack> tracks;
        try {
            // Subtitle info and caption files remain direct even for regional PGC.
            BilibiliApiClient api(QNetworkProxy::NoProxy);
            const bool pgc = request.entry.value("business").toString() == "pgc";
            tracks = PlayerApi::getSubtitleTracks(api,
                    pgc ? 0 : request.entry.value("oid").toLongLong(), request.cid,
                    request.cookie, pgc ? request.entry.value("epId").toLongLong() : 0);
        } catch (const std::exception &) {
            // Optional captions never turn a successful playback into an error.
        }
        QMetaObject::invokeMethod(this, [this, epoch, tracks] {
            if (epoch != subtitleEpoch_) return;
            onlineSubtitleTracks_ = tracks;
            subtitleTracks_.clear();
            for (const auto &track : tracks) {
                QString label = track.lanDoc.isEmpty() ? track.lan : track.lanDoc;
                if (label.isEmpty()) label = Loc::get("字幕");
                if (track.isAi && !label.contains("AI", Qt::CaseInsensitive)) label += " (AI)";
                subtitleTracks_.append(QVariantMap{{"label", label}, {"isAi", track.isAi}});
            }
            emit subtitleTracksChanged();
        }, Qt::QueuedConnection);
    });
}

void PlayerController::loadLocalSubtitles() {
    if (!client_ || !kernelAlive_) return;
    // Embedded tracks are already known after file-loaded. Sidecars are only
    // attached on selection, so opening a local file never enables subtitles.
    subtitleTracks_.clear();
    const int count = qMin(1000, static_cast<int>(client_->getPropertyDouble("track-list/count")));
    for (int i = 0; i < count; ++i) {
        const QString prefix = QStringLiteral("track-list/%1/").arg(i);
        if (client_->getPropertyString(prefix + "type") != "sub") continue;
        const QString id = client_->getPropertyString(prefix + "id");
        QString label = client_->getPropertyString(prefix + "title");
        const QString lang = client_->getPropertyString(prefix + "lang");
        if (label.isEmpty()) label = lang.isEmpty() ? Loc::get("内嵌字幕 %1").arg(id) : lang;
        subtitleTracks_.append(QVariantMap{{"label", label}, {"mpvId", id}});
    }
    emit subtitleTracksChanged();
    const auto entry = activeRequest_.entry;
    const quint64 epoch = subtitleEpoch_;
    localMediaPool_.start([this, entry, epoch] {
        QStringList paths = entry.value("subtitlePaths").toStringList();
        paths.append(entry.value("subtitlePath").toString());
        const QFileInfo media(entry.value("localPath").toString());
        for (const QFileInfo &candidate : media.dir().entryInfoList(QDir::Files | QDir::Readable)) {
            const QString base = candidate.completeBaseName();
            if (base == media.completeBaseName() || base.startsWith(media.completeBaseName() + '.'))
                paths.append(candidate.absoluteFilePath());
        }
        QVariantList sidecars;
        QSet<QString> seen;
        for (const auto &path : paths) {
            if (!PlaybackEntry::readableLocalFile(path)) continue;
            const QFileInfo file(path);
            const QString suffix = file.suffix().toLower();
            if (!QStringList{"srt", "ass", "ssa", "vtt", "sub"}.contains(suffix)) continue;
            const QString canonical = file.canonicalFilePath();
            if (seen.contains(canonical)) continue;
            seen.insert(canonical);
            sidecars.append(QVariantMap{{"label", file.fileName()}, {"path", canonical}});
        }
        QMetaObject::invokeMethod(this, [this, epoch, sidecars] {
            if (epoch != subtitleEpoch_) return;
            subtitleTracks_.append(sidecars);
            emit subtitleTracksChanged();
        }, Qt::QueuedConnection);
    });
}

void PlayerController::toggleSubtitle() {
    selectSubtitle(selectedSubtitle_ >= 0 || subtitleTracks_.isEmpty() ? -1 : 0);
}

void PlayerController::selectSubtitle(int index) {
    if (index < -1 || index >= subtitleTracks_.size()) return;
    const quint64 selection = ++subtitleSelection_;
    const quint64 epoch = subtitleEpoch_;
    selectedSubtitle_ = index;
    subtitleTimeline_ = {};
    if (client_ && kernelAlive_) {
        client_->setPropertyString("sub-visibility", "no");
        client_->setPropertyString("sid", "no");
    }
    emit selectedSubtitleChanged();
    updateSubtitleText();
    if (index < 0) return;
    if (localMedia()) {
        if (!client_ || !kernelAlive_) return;
        const QVariantMap track = subtitleTracks_.at(index).toMap();
        const QString id = track.value("mpvId").toString();
        if (!id.isEmpty()) {
            client_->setPropertyString("sid", id);
            client_->setPropertyString("sub-visibility", "yes");
            return;
        }
        const QString path = track.value("path").toString();
        QPointer<MpvClient> client = client_;
        // "cached" attaches without changing selection. Even an outdated
        // asynchronous completion cannot activate a track on the next file.
        client_->commandAsync({"sub-add", QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded), "cached"},
                this, [this, client, epoch, selection, index, path](int result) {
            if (epoch != subtitleEpoch_ || selection != subtitleSelection_ || client != client_) return;
            QString loadedId;
            const int count = qMin(1000, static_cast<int>(client->getPropertyDouble("track-list/count")));
            for (int i = 0; result >= 0 && i < count; ++i) {
                const QString prefix = QStringLiteral("track-list/%1/").arg(i);
                const QString filename = client->getPropertyString(prefix + "external-filename");
                if (filename == path || QUrl(filename).toLocalFile() == path)
                    loadedId = client->getPropertyString(prefix + "id");
            }
            if (loadedId.isEmpty()) {
                selectSubtitle(-1);
                emitError(Loc::get("本地字幕加载失败"));
                return;
            }
            auto track = subtitleTracks_[index].toMap();
            track.insert("mpvId", loadedId);
            subtitleTracks_[index] = track;
            client->setPropertyString("sid", loadedId);
            client->setPropertyString("sub-visibility", "yes");
        });
        return;
    }
    if (index >= onlineSubtitleTracks_.size()) return;
    const QString url = onlineSubtitleTracks_.at(index).url;
    const QString cookie = subtitleCookie_;
    subtitlePool_.start([this, epoch, selection, url, cookie] {
        SubtitleTimeline timeline;
        bool loaded = false;
        try {
            BilibiliApiClient api(QNetworkProxy::NoProxy);
            timeline = SubtitleTimeline::fromJson(PlayerApi::getSubtitleJson(api, url, cookie).toUtf8());
            loaded = true;
        } catch (const std::exception &) {}
        QMetaObject::invokeMethod(this, [this, epoch, selection, timeline, loaded] {
            if (epoch != subtitleEpoch_ || selection != subtitleSelection_) return;
            if (!loaded) {
                selectSubtitle(-1);
                emitError(Loc::get("字幕加载失败"));
                return;
            }
            subtitleTimeline_ = timeline;
            updateSubtitleText();
        }, Qt::QueuedConnection);
    });
}

void PlayerController::resolveEntry(const ResolveRequest &request) {
    ensureStoreReady();
    // 起始位置判定含阻塞 SQLite 调用,仅在池线程执行
    ResolveRequest resolved = request;
    if (!resolved.qualitySwitch) {
        resolved.explicitStart = computeStartSeconds(resolved);
    }
    try {
        resolved.cookie = BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
    } catch (const std::exception &e) {
        handleResolveFailed(resolved, QString::fromUtf8(e.what()));
        return;
    }

    const QString business = resolved.entry.value("business").toString();
    std::unique_ptr<BilibiliApiClient> regionalClient;
    if (business == QLatin1String("pgc") && resolved.entry.value("regionalApiProxy").toBool())
        regionalClient = std::make_unique<BilibiliApiClient>(resolved.apiProxy);
    BilibiliApiClient &api = regionalClient ? *regionalClient : *BilibiliApiClient::instance();
    if (business == QLatin1String("pgc")) {
        // PGC 免分 P:ep_id + cid 同传(历史条目);仅 epid(动态)省略 cid,
        // 弹幕 cid 经 season 接口换算,失败不阻塞播放
        const qint64 epId = resolved.entry.value("epId").toLongLong();
        qint64 cid = resolved.entry.value("cid").toLongLong();
        qint64 danmakuCid = cid;
        if (cid <= 0) {
            try {
                danmakuCid = PlayerApi::getPgcEpisodeCid(api, epId, resolved.cookie);
            } catch (const std::exception &) {
                danmakuCid = 0;  // 换算失败:无弹幕,绝不视作播放失败
            }
        }
        resolved.cid = danmakuCid;
        try {
            const PlayerApi::PgcDashInfo info =
                    PlayerApi::getPgcDashPlayUrl(api, epId, cid, resolved.cookie, resolved.preferredQn,
                                                 resolved.preferCodec);
            // 权益判定在装载前:试看切片 / 未付费内容拒绝播放(解析错误走 catch 回落,
            // 不触发拒播序列)
            if (info.entitlement.isPreview) {
                const QString text = Loc::get(info.entitlement.isPreview
                                                      ? "《%1》当前仅提供试看片段,开通大会员后可观看完整内容"
                                                      : "《%1》为会员付费内容,开通大会员后可观看")
                                             .arg(resolved.entry.value("title").toString());
                QMetaObject::invokeMethod(
                        this,
                        [this, request, text] {
                            if (request.generation != generation_) return;
                            playbackFailed_ = true;
                            loading_ = false;
                            emit loadingChanged();
                            if (client_ && kernelAlive_) client_->setPaused(true);
                            emit entitlementRejected(text);
                        },
                        Qt::QueuedConnection);
                return;
            }
            QMetaObject::invokeMethod(
                    this,
                    [this, resolved, info] {
                        if (resolved.generation != generation_) return;
                        handleDashResolved(resolved, info.dash);
                    },
                    Qt::QueuedConnection);
        } catch (const std::exception &e) {
            // DASH 解析失败 → PGC durl 变体回落
            try {
                const PlayerApi::PgcPlayUrl play =
                        PlayerApi::getPgcPlayUrl(api, epId, cid, resolved.cookie, 64);
                if (play.entitlement.isPreview) {
                    const QString text =
                            Loc::get(play.entitlement.isPreview
                                             ? "《%1》当前仅提供试看片段,开通大会员后可观看完整内容"
                                             : "《%1》为会员付费内容,开通大会员后可观看")
                                    .arg(resolved.entry.value("title").toString());
                    QMetaObject::invokeMethod(
                            this,
                            [this, request, text] {
                                if (request.generation != generation_) return;
                                loading_ = false;
                                emit loadingChanged();
                                if (client_ && kernelAlive_) client_->setPaused(true);
                                emit entitlementRejected(text);
                            },
                            Qt::QueuedConnection);
                    return;
                }
                const QStringList urls = play.playUrl.urls;
                const QString label = play.playUrl.qualityLabel;
                QVariantList qualityList;
                for (const PlayerApi::QualityOption &option : play.playUrl.availableQualities) {
                    QVariantMap map;
                    map.insert("qn", option.qn);
                    map.insert("label", option.label);
                    map.insert("needVip", false);
                    map.insert("available", true);
                    qualityList.append(map);
                }
                QMetaObject::invokeMethod(
                        this,
                        [this, resolved, urls, label, qualityList] {
                            if (resolved.generation != generation_) return;
                            handleDurlResolved(resolved, urls, label, qualityList);
                        },
                        Qt::QueuedConnection);
            } catch (const std::exception &e2) {
                handleResolveFailed(resolved, QString::fromUtf8(e2.what()));
            }
        }
        return;
    }

    // archive:aid → 分 P 1 的 cid → DASH
    const qint64 aid = resolved.entry.value("oid").toLongLong();
    qint64 cid = resolved.entry.value("cid").toLongLong();
    if (cid <= 0) {
        try {
            cid = PlayerApi::getFirstPage(api, aid, resolved.cookie).cid;
        } catch (const std::exception &e) {
            handleResolveFailed(resolved, QString::fromUtf8(e.what()));
            return;
        }
        if (cid <= 0) {
            handleResolveFailed(resolved, Loc::get("未获取到视频分 P 信息"));
            return;
        }
    }
    resolved.cid = cid;
    try {
        const PlayerApi::DashPlayInfo info = PlayerApi::getDashPlayUrl(
                api, aid, cid, resolved.cookie, resolved.preferredQn, resolved.preferCodec);
        QMetaObject::invokeMethod(
                this,
                [this, resolved, info] {
                    if (resolved.generation != generation_) return;
                    handleDashResolved(resolved, info);
                },
                Qt::QueuedConnection);
    } catch (const std::exception &) {
        // DASH 解析失败 → MP4 渐进式(fnval=1 durl)回落,不向用户抛阻断错误
        try {
            const PlayerApi::PlayUrl play = PlayerApi::getPlayUrl(api, aid, cid, resolved.cookie, 64);
            QVariantList qualityList;
            for (const PlayerApi::QualityOption &option : play.availableQualities) {
                QVariantMap map;
                map.insert("qn", option.qn);
                map.insert("label", option.label);
                map.insert("needVip", false);
                map.insert("available", true);
                qualityList.append(map);
            }
            QMetaObject::invokeMethod(
                    this,
                    [this, resolved, play, qualityList] {
                        if (resolved.generation != generation_) return;
                        handleDurlResolved(resolved, play.urls, play.qualityLabel, qualityList);
                    },
                    Qt::QueuedConnection);
        } catch (const std::exception &e2) {
            handleResolveFailed(resolved, QString::fromUtf8(e2.what()));
        }
    }
}

bool PlayerController::ensureKernel() {
    if (client_ && kernelAlive_ && client_->isValid()) return true;
    if (client_) {
        if (videoItem_) videoItem_->setClient(nullptr);
        disconnect(client_, nullptr, this, nullptr);
        client_->stop();
        client_->deleteLater();
        client_ = nullptr;
    }
    kernelNoticeShown_ = false;
    client_ = MpvClient::create(this);
    kernelAlive_ = client_ != nullptr;
    emit kernelAvailableChanged();
    if (!client_) return false;
    if (videoItem_) videoItem_->setClient(client_);
    // 跨内核恢复:音量/倍速保持(内核崩溃重建与关窗重建共用此路径)
    client_->setPropertyString("volume", QString::number(volume_));
    client_->setPropertyString("speed", QString::number(speed_));
    connect(client_, &MpvClient::playbackRestarted, this, [this](double position) {
        if (danmakuItem_ && !playbackFailed_ && !currentKey_.isEmpty())
            danmakuItem_->completeTransition(position, speed_, paused_, !paused_ && !buffering_);
    });
    connect(client_, &MpvClient::fileLoaded, this, [this] {
        if (!waitingFileLoaded_ || activeRequest_.generation != generation_) return;
        playbackFailed_ = false;
        mediaReady_ = true;
        waitingFileLoaded_ = false;
        candidateWatchdog_.stop();
        if (loading_) {
            loading_ = false;
            emit loadingChanged();
        }
        if (localMedia()) loadLocalSubtitles();
        syncSystemMedia();
    });
    connect(client_, &MpvClient::audioLoadStarted, this, [this] {
        // 视频已就绪，交由内核逐个音轨的 8 秒超时推进，避免重载同一坏音轨。
        if (waitingFileLoaded_ && activeRequest_.generation == generation_)
            candidateWatchdog_.stop();
    });
    connect(client_, &MpvClient::playbackError, this, [this](int, const QString &message) {
        if (currentKey_.isEmpty() || activeRequest_.generation != generation_) return;
        if (waitingFileLoaded_) {
            candidateWatchdog_.stop();
            // 下一事件循环再试，避免在 mpv 事件排空栈内递归装载。
            const int generation = generation_;
            const int candidate = candidateIndex_;
            QTimer::singleShot(0, this, [this, generation, candidate] {
                if (generation == generation_ && candidate == candidateIndex_ && waitingFileLoaded_) tryNextVideoCandidate();
            });
        } else {
            playbackFailed_ = true;
            loading_ = false;
            buffering_ = false;
            emit loadingChanged();
            emit bufferingChanged();
            emitError(Loc::get("媒体装载失败: %1").arg(message));
        }
    });
    connect(client_, &MpvClient::ended, this, [this] {
        if (loading_ || playbackFailed_ || currentKey_.isEmpty()) return;
        // 自然播完:会话恢复点清除 + 持久化落 0(从头);随后自动连播
        const QString key = currentKey_;
        const double dur = duration_;
        sessionPositions_.remove(key);
        endedAwaitingSwitch_ = true;
        if (!localMedia() && !key.isEmpty()) {
            workerPool_.start([this, key, dur] {
                ensureStoreReady();
                store_.savePlaybackPosition(key, 0, dur);
            });
        }
        if (currentIndex_ + 1 < list_.size()) {
            playByIndex(currentIndex_ + 1);
        } else {
            paused_ = true;
            position_ = duration_;
            if (client_) client_->setPaused(true);
            emit pausedChanged();
            emit positionChanged();
        }
        // 末项:停止在片尾(keep-open 冻结),不循环
    });
    connect(client_, &MpvClient::pausedChanged, this, [this](bool paused) {
        if (paused_ == paused) return;
        paused_ = paused;
        if (danmakuItem_) danmakuItem_->setPlayback(position_, speed_, paused_, !paused_ && !buffering_);
        emit pausedChanged();
    });
    connect(client_, &MpvClient::durationChanged, this, [this](double seconds) {
        if (seconds <= 0) return;
        duration_ = seconds;
        emit durationChanged();
    });
    connect(client_, &MpvClient::seekableChanged, this, [this](bool seekable) {
        seekable_ = seekable;
        emit seekableChanged();
    });
    connect(client_, &MpvClient::pausedForCacheChanged, this, [this](bool buffering) {
        if (buffering_ == buffering) return;
        buffering_ = buffering;
        if (danmakuItem_) danmakuItem_->setPlayback(position_, speed_, paused_, !paused_ && !buffering_);
        emit bufferingChanged();
    });
    connect(client_, &MpvClient::kernelDead, this, [this] {
        // 内核自行终止:一次性非阻塞提示;原地热替换重建为后续增强,
        // 本次仅置标志,下次起播 ensureKernel 重建
        kernelAlive_ = false;
        playbackFailed_ = true;
        waitingRenderReady_ = false;
        emit kernelAvailableChanged();
        waitingFileLoaded_ = false;
        candidateWatchdog_.stop();
        loading_ = false;
        buffering_ = false;
        emit loadingChanged();
        emit bufferingChanged();
        if (!kernelNoticeShown_) {
            kernelNoticeShown_ = true;
            emit kernelRecoveredNotice(
                    Loc::get("播放内核发生异常,播放已停止;重新起播后将自动重建内核"));
        }
    });
    return true;
}

void PlayerController::applyNetworkHeaders(const QString &cookie) {
    if (!client_) return;
    // 防盗链头经内核网络层注入,视频/音频两条流统一生效(Cookie + Referer + UA)
    client_->setPropertyString(
            "http-header-fields",
            QStringLiteral("Referer: %1,Cookie: %2").arg(BilibiliApiClient::referer(), cookie));
    client_->setPropertyString("user-agent", BilibiliApiClient::userAgent());
}

void PlayerController::handleDashResolved(const ResolveRequest &request,
                                          const PlayerApi::DashPlayInfo &info) {
    if (!ensureKernel()) {
        loading_ = false;
        emit loadingChanged();
        playbackFailed_ = true;
        emitError(Loc::get("播放内核不可用: %1").arg(MpvClient::lastCreateError()));
        return;
    }
    qualityResolving_ = false;
    fallback_ = false;
    emit fallbackChanged(false);
    applyNetworkHeaders(request.cookie);
    activeRequest_ = request;
    pendingVideoUrls_ = info.selectedVideoUrls;
    pendingAudioUrls_ = info.selectedAudioUrls;
    pendingStart_ = request.explicitStart;
    candidateIndex_ = 0;
    currentCid_ = request.cid;
    waitingFileLoaded_ = false;
    tryNextVideoCandidate();
    // 档位表(available = 响应内真实存在视频段;needVip 仅作展示标记)
    QVariantList qualityList;
    QString label;
    for (const PlayerApi::DashFormat &format : info.formats) {
        QVariantMap map;
        map.insert("qn", format.quality);
        map.insert("label", format.label);
        map.insert("needVip", format.needVip);
        map.insert("available", format.available);
        qualityList.append(map);
        if (format.quality == info.selectedQn) label = format.label;
    }
    if (label.isEmpty() && info.selectedQn > 0) {
        // 段中存在而目录缺失的档位:合成菜单行标签 "qP" 形式
        label = QStringLiteral("%1P").arg(info.selectedQn);
    }
    qualities_ = qualityList;
    selectedQn_ = info.selectedQn;
    qualityLabel_ = label;
    emit qualitiesChanged();
    loadDanmaku(request.cid, request.cookie);
    loadOnlineSubtitles(request);
    // 切换时新条目以起始位置"开始播放"上报(fire-and-forget)
    if (!request.qualitySwitch) {
        reportHeartbeat(request.entry, request.explicitStart, HeartbeatApi::Start,
                        request.cid);
    }
}

void PlayerController::tryNextVideoCandidate() {
    if (!client_ || !kernelAlive_ || activeRequest_.generation != generation_) return;
    // libmpv disables the video track if loadfile wins the race against
    // mpv_render_context_create. Local files can load before QML mounts the
    // item. Start both the load and candidate timeout only after GL is ready.
    if (!videoItem_->renderReady()) {
        waitingRenderReady_ = true;
        return;
    }
    waitingRenderReady_ = false;
    if (candidateIndex_ >= pendingVideoUrls_.size()) {
        if (fallback_ || localMedia()) {
            // 单流全部候选也未能装载:停止并报错(不无限重试)
            waitingFileLoaded_ = false;
            candidateWatchdog_.stop();
            client_->stop();
            buffering_ = false;
            loading_ = false;
            emit bufferingChanged();
            emit loadingChanged();
            playbackFailed_ = true;
            emitError(Loc::get("媒体装载失败，请重试或选择其他视频"));
            return;
        }
        waitingFileLoaded_ = false;
        candidateWatchdog_.stop();
        // 全部视频候选被拒 → durl 单流回落(再次解析,看门狗语义继续生效)
        startDurlFallback();
        return;
    }
    waitingFileLoaded_ = true;
    if (danmakuItem_) danmakuItem_->beginTransition(pendingStart_);
    if (fallback_ || localMedia())
        client_->loadSingle(pendingVideoUrls_.at(candidateIndex_), pendingStart_);
    else
        client_->loadDashCandidates(pendingVideoUrls_.at(candidateIndex_), pendingAudioUrls_,
                                    pendingStart_);
    candidateWatchdog_.start(kCandidateTimeoutMs);
    candidateIndex_++;
}

void PlayerController::startDurlFallback() {
    if (localMedia()) return;
    const ResolveRequest request = activeRequest_;
    workerPool_.start([this, request] {
        const QString business = request.entry.value("business").toString();
        std::unique_ptr<BilibiliApiClient> regionalClient;
        if (business == QLatin1String("pgc") && request.entry.value("regionalApiProxy").toBool())
            regionalClient = std::make_unique<BilibiliApiClient>(request.apiProxy);
        BilibiliApiClient &api = regionalClient ? *regionalClient : *BilibiliApiClient::instance();
        try {
            if (business == QLatin1String("pgc")) {
                const PlayerApi::PgcPlayUrl play = PlayerApi::getPgcPlayUrl(
                        api, request.entry.value("epId").toLongLong(),
                        request.entry.value("cid").toLongLong(), request.cookie, 64);
                if (play.entitlement.isPreview) {
                    handleResolveFailed(request, Loc::get("当前内容仅提供试看片段"));
                    return;
                }
                QVariantList qualityList;
                for (const PlayerApi::QualityOption &option : play.playUrl.availableQualities) {
                    QVariantMap map;
                    map.insert("qn", option.qn);
                    map.insert("label", option.label);
                    map.insert("needVip", false);
                    map.insert("available", true);
                    qualityList.append(map);
                }
                QMetaObject::invokeMethod(
                        this,
                        [this, request, play, qualityList] {
                            if (request.generation != generation_) return;
                            handleDurlResolved(request, play.playUrl.urls,
                                               play.playUrl.qualityLabel, qualityList);
                        },
                        Qt::QueuedConnection);
            } else {
                const PlayerApi::PlayUrl play = PlayerApi::getPlayUrl(
                        api, request.entry.value("oid").toLongLong(),
                        request.cid, request.cookie, 64);
                QVariantList qualityList;
                for (const PlayerApi::QualityOption &option : play.availableQualities) {
                    QVariantMap map;
                    map.insert("qn", option.qn);
                    map.insert("label", option.label);
                    map.insert("needVip", false);
                    map.insert("available", true);
                    qualityList.append(map);
                }
                QMetaObject::invokeMethod(
                        this,
                        [this, request, play, qualityList] {
                            if (request.generation != generation_) return;
                            handleDurlResolved(request, play.urls, play.qualityLabel,
                                               qualityList);
                        },
                        Qt::QueuedConnection);
            }
        } catch (const std::exception &e) {
            handleResolveFailed(request, QString::fromUtf8(e.what()));
        }
    });
}

void PlayerController::handleDurlResolved(const ResolveRequest &request,
                                          const QStringList &urls, const QString &qualityLabel,
                                          const QVariantList &qualities) {
    if (urls.isEmpty()) {
        handleResolveFailed(request, Loc::get("播放地址解析失败"));
        return;
    }
    if (!ensureKernel()) {
        loading_ = false;
        emit loadingChanged();
        playbackFailed_ = true;
        emitError(Loc::get("播放内核不可用: %1").arg(MpvClient::lastCreateError()));
        return;
    }
    qualityResolving_ = false;
    if (!fallback_) {
        fallback_ = true;
        emit fallbackChanged(true);
    }
    applyNetworkHeaders(request.cookie);
    activeRequest_ = request;
    pendingVideoUrls_ = urls;
    pendingAudioUrls_.clear();
    pendingStart_ = request.explicitStart;
    candidateIndex_ = 0;
    currentCid_ = request.cid;
    waitingFileLoaded_ = false;
    tryNextVideoCandidate();
    qualities_ = qualities;
    selectedQn_ = 0;
    qualityLabel_ = qualityLabel;
    emit qualitiesChanged();
    loadDanmaku(request.cid, request.cookie);
    loadOnlineSubtitles(request);
}

void PlayerController::handleResolveFailed(const ResolveRequest &request,
                                           const QString &message) {
    // 解析失败不影响已注入列表:停留无源,用户可改播其他条目
    QMetaObject::invokeMethod(
            this,
            [this, request, message] {
                if (request.generation != generation_) return;
                // 保位解析失败时旧媒体仍可继续，不能把整个播放会话标成失败。
                playbackFailed_ = !(request.qualitySwitch && qualityResolving_ && kernelAvailable());
                qualityResolving_ = false;
                waitingFileLoaded_ = false;
                candidateWatchdog_.stop();
                loading_ = false;
                buffering_ = false;
                emit loadingChanged();
                emit bufferingChanged();
                emit errorOccurred(message);
            },
            Qt::QueuedConnection);
}

void PlayerController::loadDanmaku(qint64 cid, const QString &cookie) {
    onlineDanmaku_.request(cid, cookie);
}

void PlayerController::flushPosition() {
    // 切播/关窗收尾:会话位置落盘 + 旧条目"结束播放"上报(均 fire-and-forget)
    const QString key = currentKey_;
    const QVariantMap entry = currentEntry();
    if (key.isEmpty() || entry.isEmpty() || PlaybackEntry::isLocal(entry)) return;
    const bool hasSession = duration_ > 0 && !loading_ && !playbackFailed_;
    const double pos = position_;
    if (hasSession) sessionPositions_.insert(key, pos);
    const double dur = duration_;
    if (hasSession && pos > 0 && dur > 0 && !endedAwaitingSwitch_) {
        workerPool_.start([this, key, pos, dur] {
            ensureStoreReady();
            store_.savePlaybackPosition(key, pos, dur);
        });
    }
    reportHeartbeat(entry, endedAwaitingSwitch_ ? dur : pos, HeartbeatApi::End, currentCid_);
}

void PlayerController::reportHeartbeat(const QVariantMap &entry, double position,
                                       HeartbeatApi::PlayAction action, qint64 cid) {
    if (PlaybackEntry::isLocal(entry)) return;
    HeartbeatApi::HeartbeatReport report;
    const QString business = entry.value("business").toString();
    if (business == QLatin1String("pgc")) {
        report.epId = entry.value("epId").toLongLong();
    } else {
        report.aid = entry.value("oid").toLongLong();
    }
    report.cid = cid;
    report.playedSeconds = position;
    report.action = action;
    QThreadPool::globalInstance()->start([report] {
        try {
            const QString cookie = BilibiliApiClient::readCookieFromFile(AppPaths::cookiePath());
            HeartbeatApi::report(*BilibiliApiClient::instance(), cookie, report);
        } catch (const std::exception &) {
            // 尽力而为:失败静默,不影响切播与播放时序
        }
    });
}

void PlayerController::playByIndex(int index) {
    if (index < 0 || index >= list_.size()) return;
    if (index == currentIndex_ && !playbackFailed_ && kernelAlive_) return;
    if (index == currentIndex_ && loading_) return;
    ++mediaSession_;
    mediaReady_ = false;
    syncSystemMedia();
    const QVariantMap entry = list_.at(index).toMap();
    // 旧条目收尾:落盘 + 结束上报
    if (currentIndex_ >= 0) flushPosition();
    endedAwaitingSwitch_ = false;

    currentIndex_ = index;
    currentKey_ = entry.value("videoKey").toString();
    currentTitle_ = entry.value("title").toString();
    emit currentChanged();
    // 条目切换立即停播旧视频:暂停冻结画面(暂停即无声),新流装载后 loadDash
    // 显式恢复播放;不存在解析期间旧视频继续播放的窗口
    if (client_ && kernelAlive_) client_->setPaused(true);
    if (!paused_) {
        paused_ = true;
        emit pausedChanged();
    }
    position_ = 0;
    lastSavedPos_ = 0;
    emit positionChanged();
    const double hint = entry.value("duration").toDouble();
    if (!qFuzzyCompare(duration_, hint)) {
        duration_ = hint;
        emit durationChanged();
    }
    seekable_ = false;
    emit seekableChanged();
    if (buffering_) {
        buffering_ = false;
        emit bufferingChanged();
    }
    loading_ = true;
    emit loadingChanged();
    qualities_.clear();
    qualityLabel_.clear();
    emit qualitiesChanged();
    if (danmakuItem_) {
        danmakuItem_->loadEntries({});
        danmakuItem_->setPlayback(0, speed_, true, false);
    }
    pollTimer_.start(kPollIntervalMs);
    startPlayback(entry, 0, false);
}

void PlayerController::playNext() {
    if (currentIndex_ + 1 < list_.size()) playByIndex(currentIndex_ + 1);
}

void PlayerController::removeAt(int index) {
    if (index < 0 || index >= list_.size()) return;
    list_.removeAt(index);
    emit playlistChanged();
    if (index == currentIndex_) {
        ++mediaSession_;
        mediaReady_ = false;
        syncSystemMedia();
        generation_++;
        resetSubtitles();
        onlineDanmaku_.reset();
        waitingFileLoaded_ = false;
        candidateWatchdog_.stop();
        // 权益拒播等场景:停留无源状态,不自动续播下一项
        currentIndex_ = -1;
        currentKey_.clear();
        currentTitle_.clear();
        currentCid_ = 0;
        emit currentChanged();
        if (client_ && kernelAlive_) client_->setPaused(true);
        loading_ = false;
        buffering_ = false;
        seekable_ = false;
        position_ = 0;
        duration_ = 0;
        emit loadingChanged();
        emit bufferingChanged();
        emit seekableChanged();
        emit positionChanged();
        emit durationChanged();
        if (danmakuItem_) {
            danmakuItem_->loadEntries({});
            danmakuItem_->setPlayback(0, speed_, true, false);
        }
    } else if (index < currentIndex_) {
        currentIndex_--;
        emit currentChanged();
    }
}

void PlayerController::togglePlayPause() {
    if (!client_ || !kernelAlive_ || currentKey_.isEmpty()) return;
    if (paused_ && endedAwaitingSwitch_) {
        seek(0);
        endedAwaitingSwitch_ = false;
    }
    paused_ = !paused_;
    client_->setPaused(paused_);
    emit pausedChanged();
}

void PlayerController::seek(double seconds) {
    // 未起播或时长未知时静默忽略
    if (!client_ || !kernelAlive_ || !seekable_) return;
    const double target = qBound(0.0, seconds, duration_ > 0 ? duration_ : seconds);
    const int result = client_->command({"seek", QString::number(target, 'f', 3), "absolute+exact"});
    if (result < 0) return;
    if (duration_ <= 0 || target < duration_) endedAwaitingSwitch_ = false;
    position_ = target;
    emit positionChanged();
    syncSystemMedia();
    if (danmakuItem_) {
        danmakuItem_->beginTransition(target);
        danmakuItem_->setPlayback(target, speed_, paused_, !paused_ && !buffering_);
        danmakuItem_->reset(target); // Explicit seek also resets corrections smaller than 0.5s.
    }
}

void PlayerController::setVolumePercent(int percent) {
    const int value = qBound(0, percent, 100);
    if (volume_ == value) return;
    volume_ = value;
    if (client_ && kernelAlive_) client_->setPropertyString("volume", QString::number(value));
    emit volumeChanged();
}

void PlayerController::setSpeed(double value) {
    const double clamped = qBound(0.25, value, 3.0);
    if (qFuzzyCompare(speed_, clamped)) return;
    speed_ = clamped;
    if (client_ && kernelAlive_) {
        client_->setPropertyString("speed", QString::number(speed_));
    }
    emit speedChanged();
    if (keepSpeed_) {
        AppPreferences::instance()->setValue(QString::fromLatin1(kKeySpeed), speed_);
    }
}

void PlayerController::setKeepSpeed(bool keep) {
    if (keepSpeed_ == keep) return;
    keepSpeed_ = keep;
    emit keepSpeedChanged();
    AppPreferences *prefs = AppPreferences::instance();
    prefs->setValue(QString::fromLatin1(kKeyKeepSpeed), keep);
    // 关闭保持 = 清除已存倍速,恢复默认 1x 的纯会话级语义
    prefs->setValue(QString::fromLatin1(kKeySpeed), keep ? speed_ : 1.0);
}

void PlayerController::setQuality(int qn) {
    if (localMedia()) return;
    preferredQn_ = qn;
    if (currentIndex_ < 0) return;
    // 保位换源:不属于条目切换,不触发停播序列/恢复点/心跳;从当前播放位置继续
    const QVariantMap entry = currentEntry();
    if (entry.isEmpty()) return;
    loading_ = true;
    emit loadingChanged();
    startPlayback(entry, position_, true);
}

void PlayerController::setPreferCodec(const QString &codec) {
    if (codec != QLatin1String("avc") && codec != QLatin1String("hev1") &&
        codec != QLatin1String("av1")) {
        return;
    }
    preferCodec_ = codec;
    AppPreferences::instance()->setValue(QString::fromLatin1(kKeyPreferCodec), codec);
    emit preferCodecChanged();
    setQuality(preferredQn_);  // 保位换源以新编码偏好重解析
}

void PlayerController::toggleDanmaku() {
    AppPreferences::instance()->setDanmakuEnabled(!danmakuOn_);
}

void PlayerController::screenshot() {
    if (!client_ || !kernelAlive_ || currentKey_.isEmpty()) {
        emit screenshotFailed(Loc::get("当前没有可截图的播放画面"));
        return;
    }
    const QString dir = AppPaths::dataDir() + QStringLiteral("/Screenshots");
    if (!QDir().mkpath(dir)) {
        emit screenshotFailed(Loc::get("截图失败"));
        return;
    }
    const QString path = ScreenshotPath::next(dir, currentTitle_);
    auto capture = std::make_shared<QTemporaryDir>(dir + QStringLiteral("/.capture-XXXXXX"));
    if (!capture->isValid()) {
        emit screenshotFailed(Loc::get("截图失败") + QStringLiteral(": ") + capture->errorString());
        return;
    }
    const QString rawPath = capture->filePath(QStringLiteral("frame.png"));
    // video excludes mpv subtitles/OSD; the separate QML danmaku and controls
    // never enter this command. The raw capture is temporary, not a final file.
    client_->commandAsync({"screenshot-to-file", rawPath, "video"}, this,
                          [this, path, rawPath, capture](int result) {
        if (result < 0) {
            const QString detail = QStringLiteral(": ") +
                QString::fromUtf8(MpvLib::instance()->errorString(result));
            emit screenshotFailed(Loc::get("截图失败") + detail);
            return;
        }
        auto promise = std::make_shared<QPromise<ScreenshotWriter::Result>>();
        promise->start();
        auto *watcher = new QFutureWatcher<ScreenshotWriter::Result>(this);
        connect(watcher, &QFutureWatcher<ScreenshotWriter::Result>::finished, this, [this, watcher] {
            const auto saved = watcher->result();
            watcher->deleteLater();
            if (!saved.path.isEmpty())
                emit screenshotSaved(Loc::get("截图已保存到 %1").arg(saved.path));
            else
                emit screenshotFailed(Loc::get("截图失败") + QStringLiteral(": ") + saved.error);
        });
        watcher->setFuture(promise->future());
        // No controller access from the worker. Its owned pool joins at teardown;
        // deleting the watcher suppresses delivery, while staging still cleans up.
        screenshotPool_.start([promise, capture, rawPath, path]() mutable {
            const auto saved = ScreenshotWriter::save(rawPath, path);
            capture.reset();
            promise->addResult(saved);
            promise->finish();
        });
    });
}

void PlayerController::closeRequested() {
    waitingRenderReady_ = false;
    ++mediaSession_;
    mediaReady_ = false;
    mediaWindowAttached_ = false;
    systemMedia_->clear();
    systemMedia_->setWindow(0);
    mediaTimer_.stop();
    // 关窗序列:保存位置、立即停播、释放会话；渲染器持有最后的内核引用。
    // 停止必须先于重量级资源释放,窗口随 FluRouter 立即从屏幕消失。
    generation_++;  // 挂起的解析/弹幕结果作废
    resetSubtitles();
    onlineDanmaku_.reset();
    qualityResolving_ = false;
    pollTimer_.stop();
    candidateWatchdog_.stop();
    waitingFileLoaded_ = false;
    if (currentIndex_ >= 0) flushPosition();
    if (client_ && kernelAlive_) {
        client_->setPaused(true);
        client_->stop();
    }
    activeRequest_ = {};
    pendingVideoUrls_.clear();
    pendingAudioUrls_.clear();
    qualities_.clear();
    qualityLabel_.clear();
    emit qualitiesChanged();
    // 会话播放列表为会话内存态:窗口关闭即释放(已落盘位置跨会话生效)
    sessionPositions_.clear();
    list_.clear();
    list_.squeeze();
    currentIndex_ = -1;
    currentKey_.clear();
    currentTitle_.clear();
    currentCid_ = 0;
    endedAwaitingSwitch_ = false;
    // 剧集模式随窗口会话结束退出(下次普通起播直接呈现播放列表面板)
    seasonMode_ = false;
    seasonTitle_.clear();
    emit playlistChanged();
    emit currentChanged();
    emit seasonModeChanged();
    emit seasonTitleChanged();
    position_ = 0;
    duration_ = 0;
    seekable_ = false;
    buffering_ = false;
    loading_ = false;
    emit positionChanged();
    emit durationChanged();
    emit seekableChanged();
    emit bufferingChanged();
    emit loadingChanged();
    if (danmakuItem_) {
        danmakuItem_->loadEntries({});
        danmakuItem_->setPlayback(0, speed_, true, false);
    }
    if (videoItem_) videoItem_->setClient(nullptr);
    if (client_) {
        disconnect(client_, nullptr, this, nullptr);
        client_->deleteLater();
        client_ = nullptr;
    }
    kernelAlive_ = false;
    emit kernelAvailableChanged();
}
