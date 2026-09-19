#include "player/SystemMediaControls.h"
#include <QMetaObject>
#include <cmath>
#include <mutex>

struct SystemMediaControls::CallbackGate {
    std::mutex mutex;
    SystemMediaControls *target = nullptr;
};

SystemMediaControls::SystemMediaControls(QObject *parent, Factory factory)
    : QObject(parent), gate_(std::make_shared<CallbackGate>()) {
    gate_->target = this;
    const auto gate = gate_;
    backend_ = factory([gate](SystemMediaCommand command, double position, quint64 session) {
        // QObject cannot disappear between the lifetime check and invokeMethod.
        std::lock_guard<std::mutex> lock(gate->mutex);
        if (auto *target = gate->target) {
            QMetaObject::invokeMethod(target, [target, command, position, session] {
                target->dispatch(command, position, session);
            }, Qt::QueuedConnection);
        }
    });
}

SystemMediaControls::~SystemMediaControls() {
    {
        std::lock_guard<std::mutex> lock(gate_->mutex);
        gate_->target = nullptr;
    }
    backend_.reset();
}

void SystemMediaControls::setWindow(quintptr windowId) { backend_->setWindow(windowId); }

void SystemMediaControls::update(const SystemMediaState &state) {
    state_ = state;
    state_.duration = std::isfinite(state.duration) ? qMax(0.0, state.duration) : 0;
    state_.position = std::isfinite(state.position) ? qMax(0.0, state.position) : 0;
    if (state_.duration > 0) state_.position = qMin(state_.position, state_.duration);
    state_.rate = std::isfinite(state.rate) && state.rate > 0 ? state.rate : 1;
    state_.canSeek = state.canSeek && state_.duration > 0;
    backend_->update(state_);
}

void SystemMediaControls::clear() { update(SystemMediaState{}); }

void SystemMediaControls::dispatch(SystemMediaCommand command, double position, quint64 session) {
    if (!state_.active || session != state_.session) return;
    if (command == SystemMediaCommand::Previous && !state_.canPrevious) return;
    if (command == SystemMediaCommand::Next && !state_.canNext) return;
    if (command == SystemMediaCommand::Seek) {
        if (!state_.canSeek || !std::isfinite(position)) return;
        position = qBound(0.0, position, state_.duration);
    }
    // Native Play/Pause are absolute actions, never toggle in the wrong direction.
    if (command == SystemMediaCommand::Play && !state_.paused) return;
    if (command == SystemMediaCommand::Pause && state_.paused) return;
    emit commandRequested(command, position);
}
