#include "player/MpvVideoItem.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QQuickOpenGLUtils>

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include "player/MpvClient.h"
#include "player/MpvLib.h"

namespace {

// mpv 的 GL 符号解析回调(GL 线程,当前上下文已就绪)
void *getProcAddress(void *, const char *name) {
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context) return nullptr;
    QByteArray byteName(name);
    return reinterpret_cast<void *>(context->getProcAddress(byteName));
}

void onRenderUpdate(void *that) {
    // mpv 渲染线程回调 → 排队触发 QML 重绘
    emit static_cast<MpvRenderNotifier *>(that)->updateRequested();
}

// 渲染器:渲染上下文在 render() 中惰性创建(而非仅在 createFramebufferObject),
// 因为 FBO 的创建时机(窗口首帧)通常早于 mpv client 就绪(起播网络解析),
// 只挂在 FBO 创建点会导致"视频 EOF/黑屏但有声";synchronize 跟踪 client 指针
// 变化(内核重建/关窗置空),render 线程内先释放旧上下文再按新 handle 重建,
// mpv_render_context_free 始终在 GL 上下文当前的渲染线程执行。
class MpvRenderer : public QQuickFramebufferObject::Renderer {
   public:
    explicit MpvRenderer(MpvVideoItem *item) : notifier_(item->notifier()) {}
    ~MpvRenderer() override { freeRenderContext(); }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        return new QOpenGLFramebufferObject(size);
    }

    void synchronize(QQuickFramebufferObject *item) override {
        // 主线程 → 渲染线程同步点:抓取最新 client 指针
        auto *videoItem = static_cast<MpvVideoItem *>(item);
        MpvClient *client = videoItem ? videoItem->client() : nullptr;
        auto handle = client ? client->sharedHandle() : std::shared_ptr<mpv_handle>{};
        if (handle != pendingHandle_ || videoItem->clientRevision() != clientRevision_) {
            pendingHandle_ = std::move(handle);
            clientRevision_ = videoItem->clientRevision();
            clientDirty_ = true;
        }
    }

    void render() override {
        MpvLib *lib = MpvLib::instance();
        QOpenGLFramebufferObject *fbo = framebufferObject();

        if (clientDirty_) {
            freeRenderContext();
            renderHandle_ = pendingHandle_;
            createFailed_ = false;
            renderFailed_ = false;
            clientDirty_ = false;
        }
        if (!renderContext_ && !createFailed_ && renderHandle_ &&
            lib->available()) {
            mpv_opengl_init_params glInit{};
            glInit.get_proc_address = getProcAddress;
            glInit.get_proc_address_ctx = nullptr;
            mpv_render_param params[] = {
                    {MPV_RENDER_PARAM_API_TYPE,
                     const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                    {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                    {MPV_RENDER_PARAM_INVALID, nullptr}};
            const int result = lib->renderContextCreate(&renderContext_, renderHandle_.get(), params);
            if (result < 0) {
                renderContext_ = nullptr;
                createFailed_ = true;
                const QString message = QString::fromUtf8(lib->errorString(result));
                qWarning() << "mpv render context create failed:" << message;
                emit notifier_->failed(clientRevision_, message);
            } else {
                lib->renderContextSetUpdateCallback(renderContext_, onRenderUpdate,
                                                    notifier_.get());
                emit notifier_->initialized(clientRevision_);
            }
        }

        if (renderContext_ && fbo && lib->renderContextRender) {
            mpv_opengl_fbo mpvFbo{static_cast<int>(fbo->handle()), fbo->size().width(),
                                  fbo->size().height(), 0};
            // 输出到 Qt Quick 采样的离屏纹理，不是 OpenGL 默认帧缓冲。
            // 此处额外翻转会使最终视频上下颠倒；Qt 项保持 mirrorVertically=false。
            int flipY = 0;
            mpv_render_param params[] = {
                    {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
                    {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                    {MPV_RENDER_PARAM_INVALID, nullptr}};
            lib->renderContextUpdate(renderContext_);
            const int result = lib->renderContextRender(renderContext_, params);
            if (result < 0 && !renderFailed_) {
                renderFailed_ = true;
                emit notifier_->failed(clientRevision_, QString::fromUtf8(lib->errorString(result)));
            }
            // report_swap describes an actual display swap, not this offscreen draw.
            // Qt owns the window swap, so do not feed mpv a fabricated timestamp.
        } else {
            // 无内核/未装载:黑底
            if (fbo) fbo->bind();
            if (QOpenGLContext *ctx = QOpenGLContext::currentContext()) {
                ctx->functions()->glDisable(GL_SCISSOR_TEST);
                ctx->functions()->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                ctx->functions()->glClearColor(0.f, 0.f, 0.f, 1.f);
                ctx->functions()->glClear(GL_COLOR_BUFFER_BIT);
            }
        }
        QQuickOpenGLUtils::resetOpenGLState();
    }

   private:
    void freeRenderContext() {
        MpvLib *lib = MpvLib::instance();
        if (renderContext_ && lib->renderContextFree) {
            lib->renderContextSetUpdateCallback(renderContext_, nullptr, nullptr);
            lib->renderContextFree(renderContext_);
        }
        renderContext_ = nullptr;
        renderHandle_.reset();
    }

    std::shared_ptr<MpvRenderNotifier> notifier_;
    std::shared_ptr<mpv_handle> pendingHandle_;
    std::shared_ptr<mpv_handle> renderHandle_;
    bool createFailed_ = false;
    bool renderFailed_ = false;
    bool clientDirty_ = false;
    quint64 clientRevision_ = 0;
    mpv_render_context *renderContext_ = nullptr;
};

}  // namespace

MpvVideoItem::MpvVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    notifier_ = std::shared_ptr<MpvRenderNotifier>(new MpvRenderNotifier,
        [](MpvRenderNotifier *value) { value->deleteLater(); });
    connect(notifier_.get(), &MpvRenderNotifier::updateRequested, this,
            &MpvVideoItem::update, Qt::QueuedConnection);
    connect(notifier_.get(), &MpvRenderNotifier::initialized, this, [this](quint64 revision) {
        if (!client_ || revision != clientRevision_ || renderReady_) return;
        renderReady_ = true;
        emit renderReadyChanged();
    }, Qt::QueuedConnection);
    connect(notifier_.get(), &MpvRenderNotifier::failed, this,
            [this](quint64 revision, const QString &message) {
        if (!client_ || revision != clientRevision_) return;
        renderReady_ = false;
        emit renderReadyChanged();
        emit renderError(message);
    }, Qt::QueuedConnection);
    setMirrorVertically(false);
}

MpvVideoItem::~MpvVideoItem() = default;

void MpvVideoItem::setClient(MpvClient *client) {
    if (client_ == client) return;
    disconnect(destroyedConnection_);
    client_ = client;
    ++clientRevision_;
    renderReady_ = false;
    if (client) destroyedConnection_ = connect(client, &QObject::destroyed, this, [this] {
        client_ = nullptr;
        ++clientRevision_;
        renderReady_ = false;
        emit renderReadyChanged();
        emit clientChanged();
        update();
    });
    emit renderReadyChanged();
    emit clientChanged();
    update();
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const {
    return new MpvRenderer(const_cast<MpvVideoItem *>(this));
}
