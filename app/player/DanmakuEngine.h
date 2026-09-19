#ifndef DANMAKU_ENGINE_H
#define DANMAKU_ENGINE_H

#include "player/DanmakuLayout.h"
#include "player/DanmakuSpriteLayout.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QQuickItem>
#include <QTimer>
#include <atomic>

// GUI-thread media clock and snapshots; QSG nodes live only on the render thread.
// Public controller/QML contract is unchanged; this item never receives input.
class DanmakuEngine : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool danmakuEnabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(int opacityPercent READ opacityPercent WRITE setOpacityPercent NOTIFY opacityPercentChanged)
    Q_PROPERTY(int speedPercent READ speedPercent WRITE setSpeedPercent NOTIFY speedPercentChanged)
    Q_PROPERTY(int areaPercent READ areaPercent WRITE setAreaPercent NOTIFY areaPercentChanged)
    Q_PROPERTY(QString implementation READ implementation WRITE setImplementation NOTIFY implementationChanged)
    Q_PROPERTY(int densityLimit READ densityLimit WRITE setDensityLimit NOTIFY densityLimitChanged)
    Q_PROPERTY(bool mergeSimilar READ mergeSimilar WRITE setMergeSimilar NOTIFY mergeSimilarChanged)
public:
    explicit DanmakuEngine(QQuickItem *parent = nullptr);
    bool enabled() const { return enabled_; }
    void setEnabled(bool value);
    int fontSize() const { return fontSize_; }
    void setFontSize(int value);
    int opacityPercent() const { return opacityPercent_; }
    void setOpacityPercent(int value);
    int speedPercent() const { return speedPercent_; }
    void setSpeedPercent(int value);
    int areaPercent() const { return areaPercent_; }
    void setAreaPercent(int value);
    QString implementation() const { return implementation_; }
    void setImplementation(const QString &value);
    int densityLimit() const { return densityLimit_; }
    void setDensityLimit(int value);
    bool mergeSimilar() const { return mergeSimilar_; }
    void setMergeSimilar(bool value);
    void beginTransition(double target);
    void completeTransition(double time, double speed, bool paused, bool playing);
    bool transitionPending() const { return transitionPending_; }
    Q_INVOKABLE void loadEntries(const QVariantList &entries);
    Q_INVOKABLE void reset(double time = -1);
    Q_INVOKABLE void setPlayback(double time, double speed, bool paused, bool playing);
    // Diagnostics for noninteractive scene-graph reuse/lifetime regression tests.
    quint64 nodeBuildCount() const { return nodeBuildCount_.load(); }
    int liveNodeCount() const { return liveNodeCount_.load(); }
    double mediaTime() const;

signals:
    void enabledChanged();
    void fontSizeChanged();
    void opacityPercentChanged();
    void speedPercentChanged();
    void areaPercentChanged();
    void implementationChanged();
    void densityLimitChanged();
    void mergeSimilarChanged();

protected:
    void geometryChange(const QRectF &next, const QRectF &previous) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void scheduleFrame();
    void attachWindow(QQuickWindow *window);
    bool animating() const;
    void applyEntries(const QVariantList &entries);
    void startMerge();
    DanmakuLayout layout_;
    DanmakuSpriteLayout sprites_;
    QString implementation_ = QStringLiteral("scene");
    QVariantList entries_;
    QVariantList originalEntries_;
    QVariantList mergedEntries_;
    QFutureWatcher<QVariantList> mergeWatcher_;
    quint64 sourceRevision_ = 0;
    quint64 mergingRevision_ = 0;
    bool mergedReady_ = false;
    bool mergeRunning_ = false;
    bool mergeSimilar_ = false;
    int densityLimit_ = 0;
    QVector<DanmakuSpriteLayout::Visual> spriteFrame_;
    bool transitionPending_ = false;
    double transitionTarget_ = 0;
    qint64 transitionStartedNs_ = 0;
    QVector<DanmakuLayout::Visual> frame_;
    QElapsedTimer clock_;
    QTimer wakeTimer_;
    QMetaObject::Connection swapConnection_;
    QMetaObject::Connection visibilityConnection_;
    QMetaObject::Connection animationConnection_;
    double anchorTime_ = 0;
    qint64 anchorNs_ = 0;
    double videoSpeed_ = 1;
    double clockCorrection_ = 0;
    double correctionSeconds_ = 0.25;
    bool hasAnchor_ = false;
    bool paused_ = true;
    bool playing_ = false;
    bool enabled_ = true;
    int fontSize_ = 25;
    int opacityPercent_ = 100;
    int speedPercent_ = 100;
    int areaPercent_ = 100;
    quint64 revision_ = 0;
    std::atomic<quint64> nodeBuildCount_{0};
    std::atomic<int> liveNodeCount_{0};
};
#endif
