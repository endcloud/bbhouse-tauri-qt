#ifndef MPV_LIB_H
#define MPV_LIB_H

#include <QObject>
#include <QPointer>
#include <QLibrary>
#include <mpv/client.h>

typedef struct mpv_handle mpv_handle;
typedef struct mpv_render_context mpv_render_context;
typedef struct mpv_render_param mpv_render_param;
typedef struct mpv_event mpv_event;

// libmpv-2.dll 动态加载与符号解析(纯动态,无链接期硬依赖;缺库优雅降级)。
// 加载顺序:exe 同级目录 → 系统搜索路径。解析失败任一核心符号即 available=false。
class MpvLib : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString libraryPath READ libraryPath CONSTANT)

   public:
    static MpvLib *instance();

    // 探测并加载 libmpv-2.dll,解析全部符号。幂等。
    Q_INVOKABLE bool probe();

    bool available() const { return available_; }
    QString libraryPath() const { return libraryPath_; }
    QString errorMessage() const { return errorMessage_; }

    // ---- client API ----
    mpv_handle *(*create)() = nullptr;
    int (*initialize)(mpv_handle *) = nullptr;
    void (*terminateDestroy)(mpv_handle *) = nullptr;
    void (*setWakeupCallback)(mpv_handle *, void (*)(void *), void *) = nullptr;
    int (*setOptionString)(mpv_handle *, const char *, const char *) = nullptr;
    int (*setPropertyStringFn)(mpv_handle *, const char *, const char *) = nullptr;
    int (*getProperty)(mpv_handle *, const char *, mpv_format, void *) = nullptr;
    int (*observeProperty)(mpv_handle *, uint64_t, const char *, mpv_format) = nullptr;
    int (*commandFn)(mpv_handle *, const char **) = nullptr;
    int (*commandAsyncFn)(mpv_handle *, uint64_t, const char **) = nullptr;
    void (*abortAsyncCommand)(mpv_handle *, uint64_t) = nullptr;
    void (*freeFn)(void *) = nullptr;
    const char *(*errorString)(int) = nullptr;
    mpv_event *(*waitEvent)(mpv_handle *, double) = nullptr;
    unsigned long (*clientApiVersion)() = nullptr;

    // ---- render API(GL)----
    int (*renderContextCreate)(mpv_render_context **, mpv_handle *,
                               mpv_render_param *) = nullptr;
    void (*renderContextFree)(mpv_render_context *) = nullptr;
    int (*renderContextRender)(mpv_render_context *, mpv_render_param *) = nullptr;
    void (*renderContextSetUpdateCallback)(mpv_render_context *, void (*)(void *), void *) = nullptr;
    uint64_t (*renderContextUpdate)(mpv_render_context *) = nullptr;
    void (*renderContextReportSwap)(mpv_render_context *) = nullptr;

   private:
    explicit MpvLib(QObject *parent = nullptr);
    template <typename Fn>
    Fn resolve(QLibrary *lib, const char *name);

    QPointer<QLibrary> library_;
    bool available_ = false;
    QString libraryPath_;
    QString errorMessage_;
};

// Alias names retained for existing callers; ABI values always come from client.h.
namespace mpvf {
inline constexpr mpv_format kFlag = MPV_FORMAT_FLAG;
inline constexpr mpv_format kString = MPV_FORMAT_STRING;
inline constexpr mpv_format kDouble = MPV_FORMAT_DOUBLE;
}  // namespace mpvf

#endif  // MPV_LIB_H
