#ifndef LIVE_PLAYER_CONTROLLER_H
#define LIVE_PLAYER_CONTROLLER_H

#include <functional>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QThreadPool>
#include <QWindow>

#include "core/LiveApi.h"
#include "player/MpvVideoItem.h"

// Independent, ephemeral live session: no history, heartbeat, seek or API proxy.
class LivePlayerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString roomId READ roomId NOTIFY roomChanged)
    Q_PROPERTY(QString title READ title NOTIFY roomChanged)
    Q_PROPERTY(QString authorName READ authorName NOTIFY roomChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QVariantList qualities READ qualities NOTIFY qualitiesChanged)
    Q_PROPERTY(int currentQn READ currentQn NOTIFY qualitiesChanged)
    Q_PROPERTY(QString qualityLabel READ qualityLabel NOTIFY qualitiesChanged)
    Q_PROPERTY(int volumePercent READ volumePercent NOTIFY volumeChanged)
    Q_PROPERTY(MpvVideoItem *videoItem READ videoItem CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)

public:
    using Resolver = std::function<LivePlayInfo(const QString &, int)>;
    using ClientFactory = std::function<MpvClient *(QObject *)>;
    using RecordingPathFactory = std::function<QString(const QString &, const QString &)>;
    explicit LivePlayerController(QObject *parent = nullptr, Resolver resolver = {},
                                  ClientFactory clientFactory = {},
                                  RecordingPathFactory recordingPathFactory = {});
    ~LivePlayerController() override;

    QString roomId() const { return roomId_; }
    QString title() const { return title_; }
    QString authorName() const { return authorName_; }
    bool loading() const { return loading_; }
    bool buffering() const { return buffering_; }
    bool paused() const { return paused_; }
    QString errorMessage() const { return errorMessage_; }
    QString statusText() const;
    QVariantList qualities() const { return qualities_; }
    int currentQn() const { return currentQn_; }
    QString qualityLabel() const;
    int volumePercent() const { return volume_; }
    MpvVideoItem *videoItem() const { return videoItem_; }
    bool recording() const { return recording_; }

    // roomId crosses QML as a decimal string (never a QML 32-bit int).
    Q_INVOKABLE void openRoom(QVariantMap room);
    Q_INVOKABLE void retry(); // Fresh URLs also return a paused session to live edge.
    Q_INVOKABLE void setQuality(int qn);
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void setVolumePercent(int percent);
    Q_INVOKABLE void closeRequested();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void screenshot();
    Q_INVOKABLE void setFullscreenWindow(QWindow *window, bool fullscreen);

signals:
    void roomChanged();
    void stateChanged();
    void qualitiesChanged();
    void volumeChanged();
    void recordingChanged();
    void recordingSaved(QString path);
    void recordingFailed(QString message);
    void screenshotSaved(QString path);
    void screenshotFailed(QString message);

private:
    void resolve();
    void acceptResolved(quint64 generation, const LivePlayInfo &info,
                        const QString &error);
    bool ensureKernel();
    void nextCandidate();
    void queueNextCandidate();
    void fail(const QString &message);
    void releaseKernel();
    bool finishRecording(bool notify);

    Resolver resolver_;
    ClientFactory clientFactory_;
    RecordingPathFactory recordingPathFactory_;
    MpvVideoItem *videoItem_ = nullptr;
    QPointer<MpvClient> client_;
    QTimer watchdog_;
    quint64 generation_ = 0;
    quint64 candidateSerial_ = 0;
    QString roomId_, title_, authorName_, errorMessage_;
    QStringList candidates_;
    QVariantList qualities_;
    int candidateIndex_ = -1;
    int preferredQn_ = 10000;
    int currentQn_ = 0;
    int volume_ = 100;
    bool loading_ = false;
    bool buffering_ = false;
    bool paused_ = true;
    bool mediaActive_ = false;
    bool playbackStarted_ = false;
    bool recording_ = false;
    QString recordingPath_;
    QThreadPool screenshotPool_;
};

#endif
