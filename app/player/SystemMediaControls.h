#pragma once

#include "player/SystemMediaBackend.h"
#include <QObject>

// All public methods and signals are GUI-thread only. Native callbacks are
// marshalled through a lifetime gate and checked against the current session.
class SystemMediaControls : public QObject {
    Q_OBJECT
public:
    using Factory = std::function<std::unique_ptr<SystemMediaBackend>(SystemMediaCallback)>;
    explicit SystemMediaControls(QObject *parent = nullptr, Factory factory = createSystemMediaBackend);
    ~SystemMediaControls() override;
    void setWindow(quintptr windowId);
    void update(const SystemMediaState &state);
    void clear();

signals:
    void commandRequested(SystemMediaCommand command, double position);

private:
    struct CallbackGate;
    void dispatch(SystemMediaCommand command, double position, quint64 session);
    std::shared_ptr<CallbackGate> gate_;
    std::unique_ptr<SystemMediaBackend> backend_;
    SystemMediaState state_;
};
