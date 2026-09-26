#ifndef MPV_CLIENT_H
#define MPV_CLIENT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <functional>
#include <QHash>
#include <QVariantMap>
#include <mpv/client.h>

typedef struct mpv_handle mpv_handle;
typedef struct mpv_render_context mpv_render_context;

// 进程内 libmpv 内核封装(契约见 openspec/specs/video-playback-window "MPV 播放内核"):
// vo=libmpv + render API(GL FBO),DASH 视频直链为主文件、音频直链为外挂音轨,
// 硬解默认开启;keep-open=always 播完只翻 eof-reached 不发 END_FILE(自动连播以
// eof 翻转驱动);pause 跨 loadfile 保持,装载前需显式清除。
// 事件经 wakeup 回调转 Queued 信号,在创建线程(主线程)排空。
class MpvClient : public QObject {
    Q_OBJECT
   public:
    // 动态加载 libmpv 并创建+初始化内核;失败返回 nullptr，由调用方展示具体错误。
    static MpvClient *create(QObject *parent = nullptr);
    ~MpvClient() override;

    bool isValid() const { return handle_ != nullptr; }
    mpv_handle *handle() const { return handle_.get(); }
    std::shared_ptr<mpv_handle> sharedHandle() const { return handle_; }
    static QString lastCreateError();

    // 选项/属性
    void setOptionString(const QString &name, const QString &value);
    QString getPropertyString(const QString &name) const;
    double getPropertyDouble(const QString &name) const;
    bool getFlag(const QString &name) const;
    // Explicit allowlist only: never exports paths, headers, track titles or metadata.
    QVariantMap diagnosticSnapshot() const;
    void setPropertyString(const QString &name, const QString &value);
    void observeProperty(const QString &name, mpv_format format);

    // 命令(argv 语义:各元素即参数,无再解析,防 URL 裸逗号截断)
    int command(const QStringList &args);
    // Completion is always queued to context, including submission errors.
    // Returns the mpv request ID (zero for submission failure).
    // Deleting context or this client cancels pending completion delivery.
    quint64 commandAsync(const QStringList &args, QObject *context,
                      std::function<void(int)> completion);

    // 装载 DASH:视频直链为主文件(replace,支持 start=N 秒),音频直链为外挂音轨
    // (audio-add 延至 file-loaded 后异步发出，完成后才通知 fileLoaded；切播可取消)
    void loadDash(const QString &videoUrl, const QString &audioUrl, double startSeconds);
    void loadDashCandidates(const QString &videoUrl, const QStringList &audioUrls,
                            double startSeconds);
    // 装载单流直链(durl 回落)
    void loadSingle(const QString &url, double startSeconds);
    void stop();
    void setPaused(bool paused);

   signals:
    // 内核自行终止或不可恢复错误(热替换协议的触发点)
    void kernelDead();

   public slots:
    // 排空事件并转发为具体信号(主线程调用)
    void takeEvents();

   signals:
    void fileLoaded();
    // 主文件已就绪；音轨候选由内部的逐候选看门狗处理。
    void audioLoadStarted();
    void playbackRestarted(double position);
    void playbackError(int errorCode, QString message);
    void ended();  // eof-reached 翻转(keep-open 语义,非 END_FILE)
    void pausedChanged(bool paused);
    void durationChanged(double seconds);
    void seekableChanged(bool seekable);
    void pausedForCacheChanged(bool buffering);
    void hwdecChanged(QString method);

   private:
    explicit MpvClient(QObject *parent);
    static void onWakeup(void *self);
    void handlePropertyChange(const QString &name);

    std::shared_ptr<mpv_handle> handle_;
    quint64 nextCommandId_ = 0;
    QHash<quint64, std::function<void(int)>> pendingCommands_;
    void loadFile(const QString &url, double startSeconds);
    void cancelAudioLoad();
    void tryNextAudioCandidate(int previousError);
    quint64 audioCommandId_ = 0;
    quint64 audioAttemptSerial_ = 0;
    QTimer audioWatchdog_;
    QStringList pendingAudioUrls_;  // 按 API 优先级，在 file-loaded 后逐一异步补挂
    qint64 requestedEntryId_ = -1;
    qint64 startedEntryId_ = -1;
    quint64 loadSerial_ = 0;
    bool lastEof_ = false;
};

#endif  // MPV_CLIENT_H
