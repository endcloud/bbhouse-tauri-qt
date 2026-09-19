#include "player/DanmakuEngine.h"
#include "player/DanmakuFilter.h"

#include <QQuickWindow>
#include <QSGTextNode>
#include <QSGOpacityNode>
#include <QSGImageNode>
#include <QHash>
#include <QSet>
#include <QMatrix4x4>
#include <QPromise>
#include <QThreadPool>
#include <cmath>

namespace {
class TextRoot : public QSGOpacityNode {
public:
    quint64 generation = 0;
    quint64 revision = 0;
    QHash<quint64, QSGTextNode *> nodes;
};
class SpriteRoot : public QSGOpacityNode {
public:
    quint64 generation = 0;
    quint64 revision = 0;
    QHash<quint64, QSGTransformNode *> nodes;
};
bool darkColor(const QColor &color) {
    return color.red() * 299 + color.green() * 587 + color.blue() * 114 < 60000;
}
}

DanmakuEngine::DanmakuEngine(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setClip(true);
    setAcceptHoverEvents(false);
    setAcceptedMouseButtons(Qt::NoButton);
    clock_.start();
    wakeTimer_.setSingleShot(true);
    wakeTimer_.setTimerType(Qt::PreciseTimer);
    connect(&wakeTimer_, &QTimer::timeout, this, &DanmakuEngine::scheduleFrame);
    connect(this, &QQuickItem::windowChanged, this, &DanmakuEngine::attachWindow);
    connect(this, &QQuickItem::visibleChanged, this, &DanmakuEngine::scheduleFrame);
    connect(&mergeWatcher_, &QFutureWatcher<QVariantList>::finished, this, [this] {
        mergeRunning_ = false;
        if (mergingRevision_ == sourceRevision_) {
            mergedEntries_ = mergeWatcher_.result();
            mergedReady_ = true;
            if (mergeSimilar_) applyEntries(mergedEntries_);
        }
        // Coalesce replacements: a running worker owns no item/controller, and
        // at most the newest source will be processed after it completes.
        startMerge();
    });
    attachWindow(window());
}

void DanmakuEngine::attachWindow(QQuickWindow *target) {
    disconnect(swapConnection_);
    disconnect(visibilityConnection_);
    disconnect(animationConnection_);
    ++revision_;
    if (target) {
        // frameSwapped can originate on the render thread. Only the queued GUI
        // callback touches the clock/model; no cross-thread item mutation.
        swapConnection_ = connect(target, &QQuickWindow::frameSwapped, this, [this] {
            if (animating() && (implementation_ == "sprite" ? !spriteFrame_.isEmpty() : !frame_.isEmpty()))
                scheduleFrame();
        }, Qt::QueuedConnection);
        animationConnection_ = connect(target, &QQuickWindow::afterAnimating, this, [this] {
            // Sample the sprite clock just before synchronization, instead of
            // retaining the preceding swap callback's position for a full frame.
            if (implementation_ == "sprite" && animating()) polish();
        });
        visibilityConnection_ = connect(target, &QWindow::visibleChanged, this,
                                        &DanmakuEngine::scheduleFrame);
    }
    scheduleFrame();
}

bool DanmakuEngine::animating() const {
    return enabled_ && opacityPercent_ > 0 && areaPercent_ > 0 && width() > 0
        && (implementation_ == "sprite" ? sprites_.laneCount() > 0 && !transitionPending_
                                         : layout_.laneCount() > 0) && playing_ && !paused_
        && isVisible() && window() && window()->isVisible() && window()->isExposed();
}

void DanmakuEngine::scheduleFrame() {
    wakeTimer_.stop();
    polish();
    update();
}

double DanmakuEngine::mediaTime() const {
    if (implementation_ == "sprite" && transitionPending_) return transitionTarget_;
    if (!playing_ || paused_) return anchorTime_;
    const double elapsed = (clock_.nsecsElapsed() - anchorNs_) / 1e9;
    return anchorTime_ + elapsed * videoSpeed_
        + clockCorrection_ * qMin(1., elapsed / correctionSeconds_);
}

void DanmakuEngine::setPlayback(double time, double speed, bool paused, bool playing) {
    if (!std::isfinite(time) || time < 0) return;
    if (implementation_ == "sprite" && transitionPending_) {
        paused_ = paused;
        playing_ = playing;
        if (std::isfinite(speed) && speed > 0) videoSpeed_ = speed;
        scheduleFrame();
        return;
    }
    const double predicted = mediaTime();
    const bool sprite = implementation_ == "sprite";
    const bool seeked = hasAnchor_ && std::abs(time - predicted) > (sprite ? 2.0 : 0.5);
    const double nextSpeed = std::isfinite(speed) && speed > 0 ? speed : videoSpeed_;
    const bool continuous = hasAnchor_ && playing_ && !paused_ && playing && !paused
                            && !seeked && qFuzzyCompare(nextSpeed, videoSpeed_);
    anchorTime_ = continuous ? predicted : time;
    clockCorrection_ = continuous ? time - predicted : 0;
    if (sprite) {
        // Per-sprite travel follows one uninterrupted monotonic timeline, like
        // the reference animation clocks. Media samples do not modulate velocity.
        anchorTime_ = hasAnchor_ && !seeked ? predicted : time;
        clockCorrection_ = 0;
    }
    // Decoder position can advance in video-frame steps. Apply small corrections
    // gradually, never moving text backwards or resetting its node every poll.
    correctionSeconds_ = qMax(0.25, 2 * std::abs(clockCorrection_) / nextSpeed);
    anchorNs_ = clock_.nsecsElapsed();
    hasAnchor_ = true;
    videoSpeed_ = nextSpeed;
    paused_ = paused;
    playing_ = playing;
    if (seeked) {
        if (sprite) sprites_.reset(time);
        else layout_.reset(time);
    }
    scheduleFrame();
}

void DanmakuEngine::loadEntries(const QVariantList &entries) {
    originalEntries_ = entries;
    ++sourceRevision_;
    mergedEntries_.clear();
    mergedReady_ = false;
    if (entries.isEmpty()) transitionPending_ = false;
    applyEntries(mergeSimilar_ ? QVariantList() : entries);
    startMerge();
}

void DanmakuEngine::applyEntries(const QVariantList &entries) {
    entries_ = entries;
    frame_.clear();
    spriteFrame_.clear();
    if (implementation_ == "sprite") sprites_.load(entries);
    else layout_.load(entries);
    reset();
}

void DanmakuEngine::startMerge() {
    // finished may already be queued, so keep the explicit in-flight revision
    // until that notification has consumed the previous result.
    if (!mergeSimilar_ || mergedReady_ || originalEntries_.isEmpty() || mergeRunning_) return;
    mergeRunning_ = true;
    mergingRevision_ = sourceRevision_;
    const auto input = originalEntries_;
    auto promise = std::make_shared<QPromise<QVariantList>>();
    promise->start();
    mergeWatcher_.setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, input] {
        promise->addResult(DanmakuFilter::mergeSimilar(input));
        promise->finish();
    });
}

void DanmakuEngine::setMergeSimilar(bool value) {
    if (mergeSimilar_ == value) return;
    mergeSimilar_ = value;
    applyEntries(value ? (mergedReady_ ? mergedEntries_ : QVariantList()) : originalEntries_);
    startMerge();
    emit mergeSimilarChanged();
}

void DanmakuEngine::setDensityLimit(int value) {
    value = qBound(0, value, 100);
    if (densityLimit_ == value) return;
    densityLimit_ = value;
    layout_.setDensityLimit(value);
    sprites_.setDensityLimit(value);
    reset();
    emit densityLimitChanged();
}

void DanmakuEngine::setImplementation(const QString &value) {
    if ((value != "scene" && value != "sprite") || implementation_ == value) return;
    const double time = mediaTime();
    implementation_ = value;
    frame_.clear();
    spriteFrame_.clear();
    layout_.load({});
    sprites_.load({});
    if (value == "sprite") sprites_.load(entries_);
    else layout_.load(entries_);
    ++revision_;
    reset(time);
    emit implementationChanged();
}

void DanmakuEngine::beginTransition(double target) {
    if (!std::isfinite(target) || target < 0) return;
    transitionPending_ = true;
    transitionTarget_ = target;
    transitionStartedNs_ = clock_.nsecsElapsed();
    if (implementation_ == "sprite") reset(target);
}

void DanmakuEngine::completeTransition(double time, double speed, bool paused, bool playing) {
    if (!transitionPending_ || !std::isfinite(time) || time < 0) return;
    // A queued restart from an earlier seek must not release the newer target.
    // The event may wait in the GUI queue while playback has already resumed.
    // Allow that elapsed progress, including at higher playback rates.
    const double elapsed = (clock_.nsecsElapsed() - transitionStartedNs_) / 1e9;
    const double rate = std::isfinite(speed) && speed > 0 ? speed : videoSpeed_;
    if (time < transitionTarget_ - 0.75 || time > transitionTarget_ + elapsed * rate + 0.75) return;
    transitionPending_ = false;
    if (implementation_ == "sprite") {
        anchorTime_ = time;
        anchorNs_ = clock_.nsecsElapsed();
        clockCorrection_ = 0;
        paused_ = paused;
        playing_ = playing;
        hasAnchor_ = true;
        if (std::isfinite(speed) && speed > 0) videoSpeed_ = speed;
        // Keep prepared target resources; only the animation anchor is released.
        scheduleFrame();
    }
}

void DanmakuEngine::reset(double time) {
    if (std::isfinite(time) && time >= 0) {
        anchorTime_ = time;
        anchorNs_ = clock_.nsecsElapsed();
        clockCorrection_ = 0;
    }
    if (implementation_ == "sprite") {
        sprites_.configure(size(), fontSize_, speedPercent_, areaPercent_,
                           window() ? window()->effectiveDevicePixelRatio() : 1.0);
        sprites_.reset(mediaTime());
        spriteFrame_.clear();
    } else {
        layout_.configure(size(), fontSize_, speedPercent_, areaPercent_);
        layout_.reset(mediaTime());
    }
    scheduleFrame();
}

void DanmakuEngine::setEnabled(bool value) {
    if (enabled_ == value) return;
    enabled_ = value;
    reset();
    emit enabledChanged();
}
void DanmakuEngine::setFontSize(int value) {
    value = qBound(10, value, 96);
    if (fontSize_ == value) return;
    fontSize_ = value;
    reset();
    emit fontSizeChanged();
}
void DanmakuEngine::setOpacityPercent(int value) {
    value = qBound(0, value, 100);
    if (opacityPercent_ == value) return;
    opacityPercent_ = value;
    scheduleFrame();
    emit opacityPercentChanged();
}
void DanmakuEngine::setSpeedPercent(int value) {
    value = qBound(10, value, 500);
    if (speedPercent_ == value) return;
    speedPercent_ = value;
    reset();
    emit speedPercentChanged();
}
void DanmakuEngine::setAreaPercent(int value) {
    value = qBound(0, value, 100);
    if (areaPercent_ == value) return;
    areaPercent_ = value;
    reset();
    emit areaPercentChanged();
}

void DanmakuEngine::geometryChange(const QRectF &next, const QRectF &previous) {
    QQuickItem::geometryChange(next, previous);
    if (next.size() != previous.size()) reset();
}
void DanmakuEngine::itemChange(ItemChange change, const ItemChangeData &data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemDevicePixelRatioHasChanged) {
        // Qt owns glyph atlases; rebuilding our nodes lets it select the new
        // window DPR without retaining a stale native-rendering glyph run.
        ++revision_;
        scheduleFrame();
    }
}

void DanmakuEngine::updatePolish() {
    const double time = mediaTime();
    if (implementation_ == "sprite") {
        sprites_.configure(size(), fontSize_, speedPercent_, areaPercent_,
                           window() ? window()->effectiveDevicePixelRatio() : 1.0);
        if (enabled_ && opacityPercent_ > 0 && isVisible()) {
            const auto &frame = sprites_.advance(time);
            spriteFrame_ = transitionPending_ ? QVector<DanmakuSpriteLayout::Visual>() : frame;
            if (sprites_.pendingPreparation() > 0) wakeTimer_.start(8);
            else if (animating() && spriteFrame_.isEmpty() && std::isfinite(sprites_.nextTime())) {
                wakeTimer_.start(int(qBound(8., std::ceil((sprites_.nextTime() - time) / videoSpeed_ * 1000.), 60000.)));
            }
        } else spriteFrame_.clear();
        update();
        return;
    }
    layout_.configure(size(), fontSize_, speedPercent_, areaPercent_);
    if (enabled_ && opacityPercent_ > 0 && isVisible()) frame_ = layout_.advance(time);
    else frame_.clear();
    if (animating() && frame_.isEmpty() && std::isfinite(layout_.nextTime())) {
        const double delay = qMax(1., std::ceil((layout_.nextTime() - time) / videoSpeed_ * 1000.));
        wakeTimer_.start(int(qMin(delay, 60000.)));
    }
    update();
}

QSGNode *DanmakuEngine::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    // Qt blocks the GUI thread during synchronization. No layout preparation,
    // media clock changes or QQuickItem access occurs later in render().
    if (implementation_ == "sprite") {
        auto *root = dynamic_cast<SpriteRoot *>(oldNode);
        if (!root || root->generation != sprites_.generation() || root->revision != revision_) {
            delete oldNode;
            root = new SpriteRoot;
            root->generation = sprites_.generation();
            root->revision = revision_;
        }
        root->setOpacity(opacityPercent_ / 100.);
        QSet<quint64> present;
        for (const auto &visual : spriteFrame_) present.insert(visual.id);
        for (auto it = root->nodes.begin(); it != root->nodes.end();) {
            if (present.contains(it.key())) { ++it; continue; }
            root->removeChildNode(it.value());
            delete it.value();
            it = root->nodes.erase(it);
        }
        int uploaded = 0;
        bool uploadDeferred = false;
        for (const auto &visual : spriteFrame_) {
            auto *node = root->nodes.value(visual.id, nullptr);
            if (!node) {
                if (uploaded >= 4) { uploadDeferred = true; continue; }
                auto *texture = window()->createTextureFromImage(*visual.image, QQuickWindow::TextureCanUseAtlas);
                if (!texture) continue;
                auto *image = window()->createImageNode();
                if (!image) { delete texture; continue; }
                image->setOwnsTexture(true);
                image->setTexture(texture);
                image->setFiltering(QSGTexture::Linear);
                image->setRect(QRectF(QPointF(0, 0), visual.size));
                node = new QSGTransformNode;
                node->appendChildNode(image);
                root->appendChildNode(node);
                root->nodes.insert(visual.id, node);
                ++uploaded;
                ++nodeBuildCount_;
            }
            QMatrix4x4 matrix;
            matrix.translate(float(visual.position.x()), float(visual.position.y()));
            node->setMatrix(matrix);
        }
        // A paused frame still needs to drain the bounded upload queue. The
        // queued context call cannot outlive this item or mutate it on render.
        if (uploadDeferred)
            QMetaObject::invokeMethod(this, &DanmakuEngine::scheduleFrame, Qt::QueuedConnection);
        liveNodeCount_.store(root->nodes.size());
        return root;
    }
    auto *root = dynamic_cast<TextRoot *>(oldNode);
    if (!root || root->generation != layout_.generation() || root->revision != revision_) {
        delete oldNode;
        root = new TextRoot;
        root->generation = layout_.generation();
        root->revision = revision_;
    }
    root->setOpacity(opacityPercent_ / 100.);
    QSet<quint64> present;
    for (const auto &visual : frame_) present.insert(visual.id);
    for (auto it = root->nodes.begin(); it != root->nodes.end();) {
        if (present.contains(it.key())) { ++it; continue; }
        root->removeChildNode(it.value());
        delete it.value();
        it = root->nodes.erase(it);
    }
    for (const auto &visual : frame_) {
        auto *node = root->nodes.value(visual.id, nullptr);
        if (!node) {
            node = window()->createTextNode();
            if (!node) continue;
            node->setRenderType(QSGTextNode::QtRendering);
            node->setColor(visual.color);
            node->setTextStyle(QSGTextNode::Outline);
            node->setStyleColor(darkColor(visual.color) ? Qt::white : Qt::black);
            node->addTextLayout(QPointF(0, 0), visual.text.get());
            root->appendChildNode(node);
            root->nodes.insert(visual.id, node);
            ++nodeBuildCount_;
        }
        QMatrix4x4 matrix;
        matrix.translate(float(visual.position.x()), float(visual.position.y()));
        node->setMatrix(matrix);
    }
    liveNodeCount_.store(root->nodes.size());
    return root;
}
