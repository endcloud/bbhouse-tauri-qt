#ifndef MPV_VIDEO_ITEM_H
#define MPV_VIDEO_ITEM_H

#include <QQuickFramebufferObject>
#include <QPointer>

#include "player/MpvClient.h"

// This dispatcher outlives the QML item when the scene graph retires later.
// Qt disconnects its queued item connections automatically on item destruction.
class MpvRenderNotifier : public QObject {
    Q_OBJECT
   signals:
    void updateRequested();
    void initialized(quint64 revision);
    void failed(quint64 revision, QString message);
};

// QQuick 视频项:mpv render API(GL)直接渲染进 QQuickFramebufferObject 的 FBO。
// 要求 QQuickWindow 使用 OpenGL RHI(main.cpp 已强制)。
// 控制面板/弹幕等 QML 元素自然叠加其上(替代原 WinUI 的 wid+透明岛三明治)。
class MpvVideoItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(MpvClient *client READ client WRITE setClient NOTIFY clientChanged)

   public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);
    ~MpvVideoItem() override;

    MpvClient *client() const { return client_.data(); }
    std::shared_ptr<MpvRenderNotifier> notifier() const { return notifier_; }
    void setClient(MpvClient *client);
    // Main-thread state; renderer notifications carry the binding revision so
    // a retired kernel cannot release a new kernel's pending media request.
    bool renderReady() const { return renderReady_; }
    quint64 clientRevision() const { return clientRevision_; }

    Renderer *createRenderer() const override;

   signals:
    void clientChanged();
    void renderReadyChanged();
    void renderError(QString message);

   private:
    QPointer<MpvClient> client_;
    quint64 clientRevision_ = 0;
    bool renderReady_ = false;
    QMetaObject::Connection destroyedConnection_;
    std::shared_ptr<MpvRenderNotifier> notifier_;
};

#endif  // MPV_VIDEO_ITEM_H
