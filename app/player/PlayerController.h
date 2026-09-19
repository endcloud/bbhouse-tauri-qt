#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include <functional>
#include <mutex>
#include <optional>

#include <QElapsedTimer>
#include <QObject>
#include <QNetworkProxy>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QThreadPool>
#include <QVariantList>
#include <QWindow>

#include "core/HeartbeatApi.h"
#include "core/HistoryStore.h"
#include "core/PlayerApi.h"
#include "player/DanmakuEngine.h"
#include "player/MpvClient.h"
#include "player/MpvVideoItem.h"
#include "player/SystemMediaControls.h"
#include "player/SubtitleTimeline.h"

// QML 桥接:播放窗口管线编排(契约见 openspec/specs/video-playback-window)。
// 线程约定同 HistoryController:数据层(PlayerApi/HistoryStore/HeartbeatApi)全部
// 阻塞式,一律经 QThreadPool 全局线程池执行,结果以 QueuedConnection 回投主线程
// 再发信号;mpv 内核调用(loadDash/属性/命令)仅限主线程(事件在其创建线程排空)。
//
// videoItem/danmakuItem 由 C++ 创建(QML 不能 new C++ 类型),窗口 QML 仅挂载
// (reparent);窗口关闭时 QML 先解除挂载,实例跨窗口复用。
class PlayerController : public QObject {
    Q_OBJECT
    // 内核就绪(MpvLib 可用且 client 创建成功;kernelDead 后置 false,下次起播重建)
    Q_PROPERTY(bool kernelAvailable READ kernelAvailable NOTIFY kernelAvailableChanged)
    // 会话播放列表(videoKey/title/subtitle/coverUrl/business/oid/kid/epId/cid/
    // duration/progress/linkUrl),窗口关闭即释放
    Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    // paused-for-cache(缓冲中);loading 为地址解析中(同样驱动缓冲指示)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // durl 单流回落模式(true 时 UI 以非阻塞提示说明)
    Q_PROPERTY(bool fallback READ fallback NOTIFY fallbackChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged)
    Q_PROPERTY(bool localMedia READ localMedia NOTIFY currentChanged)
    // 番剧剧集模式(PlaySeason 进入):playlist 即分集表,右侧播放列表面板隐藏,
    // 顶栏呈现选集菜单;普通起播入口(openWith)与关窗退出该模式
    Q_PROPERTY(bool seasonMode READ seasonMode NOTIFY seasonModeChanged)
    Q_PROPERTY(QString seasonTitle READ seasonTitle NOTIFY seasonTitleChanged)
    // 当前实际清晰度标签(如实显示接口返回的清晰度)
    Q_PROPERTY(QString qualityLabel READ qualityLabel NOTIFY qualitiesChanged)
    // 档位表 {qn,label,needVip,available}(available=响应内真实存在视频段)
    Q_PROPERTY(QVariantList qualities READ qualities NOTIFY qualitiesChanged)
    Q_PROPERTY(int currentQn READ currentQn NOTIFY qualitiesChanged)
    // 编码偏好 "avc"|"hev1"|"av1"(菜单选中态,持久化 App.Player.PreferCodec)
    Q_PROPERTY(QString preferCodec READ preferCodec NOTIFY preferCodecChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(bool keepSpeed READ keepSpeed NOTIFY keepSpeedChanged)
    Q_PROPERTY(int volumePercent READ volumePercent NOTIFY volumeChanged)
    Q_PROPERTY(bool danmakuOn READ danmakuOn NOTIFY danmakuOnChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY subtitleTracksChanged)
    Q_PROPERTY(int selectedSubtitle READ selectedSubtitle NOTIFY selectedSubtitleChanged)
    Q_PROPERTY(QString subtitleText READ subtitleText NOTIFY subtitleTextChanged)
    // C++ 创建、窗口 QML 仅挂载的视频项与弹幕项( CONSTANT:构造即创建,跨窗口复用)
    Q_PROPERTY(MpvVideoItem *videoItem READ videoItem CONSTANT)
    Q_PROPERTY(DanmakuEngine *danmakuItem READ danmakuItem CONSTANT)

   public:
    explicit PlayerController(QObject *parent = nullptr);
    ~PlayerController() override;

    bool kernelAvailable() const;
    QVariantList playlist() const;
    int currentIndex() const;
    bool paused() const;
    double position() const;
    double duration() const;
    bool seekable() const;
    bool buffering() const;
    bool loading() const;
    bool fallback() const;
    QString currentTitle() const;
    bool localMedia() const;
    bool seasonMode() const;
    QString seasonTitle() const;
    QString qualityLabel() const;
    QVariantList qualities() const;
    int currentQn() const;
    QString preferCodec() const;
    double speed() const;
    bool keepSpeed() const;
    int volumePercent() const;
    bool danmakuOn() const;
    QVariantList subtitleTracks() const { return subtitleTracks_; }
    int selectedSubtitle() const { return selectedSubtitle_; }
    QString subtitleText() const { return subtitleText_; }
    MpvVideoItem *videoItem() const { return videoItem_; }
    DanmakuEngine *danmakuItem() const { return danmakuItem_; }

    // 打开/复用窗口并入列起播。entries 每项 QVariantMap:videoKey/title/subtitle/
    // coverUrl/business/oid/kid/epId/cid/duration/progress(显式进度,秒,可缺省)/
    // linkUrl。按 videoKey 去重;已在列表的条目只切播不入列。调用方随后以
    // FluRouter.navigate("/player") 呈现窗口(FluWindowType.SingleTask 复用并聚焦)。
    Q_INVOKABLE void openWith(QVariantList entries);

    // 批量追加起播(合集"播放全部"等,special-follow-ui):先过滤不可播条目
    // (archive 需 oid>0;pgc 需 epId>0),再按 videoKey 保序去重,追加到
    // 当前播放列表(不清空既有列表;已在列表的条目不重复入列),随后定位到本批
    // 首项起播(已在列表则切播到其既有位置)。空集不开窗(调用方不导航);
    // 既有条目与追加互不扰动,失败不清列表。窗口呈现由调用方 navigate 复用。
    Q_INVOKABLE void playRange(QVariantList entries);

    // 进入番剧剧集模式起播(bangumi-ui):season 携带 title/lastEpId/
    // lastTimeSeconds;episodes 每项 QVariantMap:epId/cid/title/longTitle/
    // duration/badge。playlist 整表替换为分集(集键 pgc:{epId}:0,标题 long_title
    // 优先;仅最近观看分集携带 progress=last_time 供集内续播),从 startIndex
    // 起播,自动连播走下一集(末集停止,复用既有 playNext 语义)。
    Q_INVOKABLE void playSeason(QVariantMap season, QVariantList episodes, int startIndex);

    Q_INVOKABLE void togglePlayPause();
    // 未起播/时长未知时静默忽略;←→ 快捷键与进度条共用
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void setVolumePercent(int percent);
    Q_INVOKABLE void setSpeed(double value);
    // 倍速保持开关(规格 persist-playback-speed):开启即写穿 App.Player.Speed
    Q_INVOKABLE void setKeepSpeed(bool keep);
    // 保位换源(preferQn=0 表示接口默认最高)
    Q_INVOKABLE void setQuality(int qn);
    // 编码偏好 "avc"|"hev1"|"av1",持久化 App.Player.PreferCodec(默认 hev1 / H.265)
    Q_INVOKABLE void setPreferCodec(const QString &codec);
    Q_INVOKABLE void toggleDanmaku();
    Q_INVOKABLE void selectSubtitle(int index);
    Q_INVOKABLE void toggleSubtitle();
    Q_INVOKABLE void playByIndex(int index);
    Q_INVOKABLE void playNext();
    // 权益拒播确认移除等场景;移除当前项后停留无源状态,不自动续播
    Q_INVOKABLE void removeAt(int index);
    // mpv 纯视频帧 → 后台压缩到 <3 MiB → Screenshots，完成后提示最终路径。
    Q_INVOKABLE void screenshot();
    // QML 关窗序列:保存位置 → stop(即刻静音)→ 释放客户端引用;
    // QML 随后真正关窗(隐藏/销毁由 FluWindow closeListener 承担)
    Q_INVOKABLE void closeRequested();
    Q_INVOKABLE void attachMediaWindow(QWindow *window);

   signals:
    void kernelAvailableChanged();
    void playlistChanged();
    void currentChanged();
    void seasonModeChanged();
    void seasonTitleChanged();
    void pausedChanged();
    void positionChanged();
    void durationChanged();
    void seekableChanged();
    void bufferingChanged();
    void loadingChanged();
    void fallbackChanged(bool fallback);
    void qualitiesChanged();
    void preferCodecChanged();
    void speedChanged();
    void keepSpeedChanged();
    void volumeChanged();
    void danmakuOnChanged();
    void subtitleTracksChanged();
    void selectedSubtitleChanged();
    void subtitleTextChanged();
    // 解析/取流/cookie 等失败(非阻塞提示,不影响已注入列表)
    void errorOccurred(QString message);
    // PGC 权益拒播(试看 / 会员两套文案);QML 以模态确认后 removeAt 当前项
    void entitlementRejected(QString message);
    // 弹幕就绪(entries: time/type/fontSize/fontColor/level/message)
    void danmakuLoaded(QVariantList entries);
    void danmakuLoadFailed(QString message);
    // 内核中途死亡的一次性非阻塞提示(热替换重建为后续增强)
    void kernelRecoveredNotice(QString message);
    void screenshotSaved(QString path);
    void screenshotFailed(QString message);

   private:
    QThreadPool screenshotPool_; // Serial image encoding, independent of API work.
    QThreadPool localMediaPool_; // Local sidecar I/O and XML parsing; joined on destruction.
    QThreadPool subtitlePool_; // Subtitle HTTP/JSON work never blocks playback.
    // 一次起播/换源请求(线程池任务携带的不可变快照)
    struct ResolveRequest {
        int generation = 0;
        int preferredQn = 0;
        QString preferCodec;
        QString cookie;
        QNetworkProxy apiProxy{QNetworkProxy::NoProxy};
        QVariantMap entry;
        bool qualitySwitch = false;   // 保位换源(不触发停播/恢复点/心跳语义)
        double explicitStart = 0;     // 换源时的保位起点
        double sessionStart = -1;     // 主线程读取的会话恢复点快照(-1=无)
        double durationHint = 0;
        qint64 cid = 0;               // 解析得到的 cid(弹幕/心跳)
    };

    void startPlayback(const QVariantMap &entry, double explicitStart, bool qualitySwitch);
    void resolveEntry(const ResolveRequest &request);       // 线程池
    void startLocalPlayback(const ResolveRequest &request);
    void loadLocalDanmaku(const QVariantMap &entry);
    void resetSubtitles();
    void loadOnlineSubtitles(const ResolveRequest &request);
    void loadLocalSubtitles();
    void updateSubtitleText();

    // 任意工作线程调用;std::call_once 保证 store.initialize 仅执行一次
    void ensureStoreReady();

    void handleDashResolved(const ResolveRequest &request,
                            const PlayerApi::DashPlayInfo &info);
    void handleDurlResolved(const ResolveRequest &request, const QStringList &urls,
                            const QString &qualityLabel,
                            const QVariantList &qualities);
    void handleResolveFailed(const ResolveRequest &request, const QString &message);
    void tryNextVideoCandidate();
    void startDurlFallback();
    void loadDanmaku(qint64 cid, const QString &cookie);
    void applyNetworkHeaders(const QString &cookie);
    bool ensureKernel();
    double computeStartSeconds(const ResolveRequest &request);  // 线程池(含 SQLite 读取)
    static bool isResumable(double position, double duration);
    void flushPosition();                                   // 切播:落盘 + 结束上报
    void reportHeartbeat(const QVariantMap &entry, double position,
                         HeartbeatApi::PlayAction action, qint64 cid);
    void emitError(const QString &message);
    int indexOfKey(const QString &key) const;
    QVariantMap currentEntry() const;
    void syncSystemMedia();

    HistoryStore store_;
    std::once_flag storeInitFlag_;

    MpvVideoItem *videoItem_ = nullptr;
    DanmakuEngine *danmakuItem_ = nullptr;
    QPointer<MpvClient> client_;
    SystemMediaControls *systemMedia_ = nullptr;
    QMetaObject::Connection mediaWindowDestroyed_;
    QTimer mediaTimer_;
    quint64 mediaSession_ = 0;
    bool mediaReady_ = false;
    bool mediaWindowAttached_ = false;
    bool kernelAlive_ = false;        // 内核 SHUTDOWN 后置 false(handle 仍在,不再使用)
    bool kernelNoticeShown_ = false;  // 一次性提示


    QVariantList list_;
    int currentIndex_ = -1;
    QString currentKey_;
    QString currentTitle_;
    qint64 currentCid_ = 0;

    bool seasonMode_ = false;   // 番剧剧集模式(仅主线程)
    QString seasonTitle_;       // 剧集标题(顶栏选集菜单上下文)

    bool paused_ = true;
    double position_ = 0;
    double duration_ = 0;
    bool seekable_ = false;
    bool buffering_ = false;
    bool loading_ = false;
    bool fallback_ = false;
    QString qualityLabel_;
    QVariantList qualities_;
    int selectedQn_ = 0;

    double speed_ = 1.0;
    bool keepSpeed_ = false;
    int volume_ = 100;
    bool danmakuOn_ = true;
    QVariantList subtitleTracks_; // UI metadata only; signed URLs stay in native memory.
    QList<PlayerApi::SubtitleTrack> onlineSubtitleTracks_;
    SubtitleTimeline subtitleTimeline_;
    QString subtitleText_;
    QString subtitleCookie_;
    int selectedSubtitle_ = -1;
    quint64 subtitleEpoch_ = 0; // Changes only for new content, not quality switches.
    quint64 subtitleSelection_ = 0;
    bool subtitleCatalogRequested_ = false;
    int preferredQn_ = 80;  // 新会话默认 1080P，用户选择仍在会话内保持
    QString preferCodec_ = QStringLiteral("hev1");

    QTimer pollTimer_;           // 100ms 位置轮询(写穿 + 弹幕时基回填)
    QElapsedTimer saveClock_;    // 位置落库节流
    double lastSavedPos_ = 0;

    QTimer candidateWatchdog_;   // 候选链逐个尝试:8s 未 file-loaded 换下一候选
    bool waitingFileLoaded_ = false;
    bool waitingRenderReady_ = false;
    QStringList pendingVideoUrls_;
    QStringList pendingAudioUrls_;
    double pendingStart_ = 0;
    int candidateIndex_ = 0;
    ResolveRequest activeRequest_;

    int generation_ = 0;         // 递增使切播/关窗后的过期解析结果作废
    bool qualityResolving_ = false;
    bool playbackFailed_ = false;
    bool endedAwaitingSwitch_ = false;  // 自然播完已落 0,切播时不再重复落盘

    QHash<QString, double> sessionPositions_;  // 会话恢复点(仅主线程)
};

#endif  // PLAYER_CONTROLLER_H
