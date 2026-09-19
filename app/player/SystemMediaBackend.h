#pragma once

#include <QString>
#include <QtGlobal>
#include <functional>
#include <memory>

enum class SystemMediaCommand { Play, Pause, Toggle, Previous, Next, Seek, Stop };

struct SystemMediaState {
    quint64 session = 0;
    bool active = false;
    bool paused = true;
    bool buffering = false;
    bool canSeek = false;
    bool canPrevious = false;
    bool canNext = false;
    QString title;
    QString artist;
    QString coverUrl;
    double position = 0;
    double duration = 0;
    double rate = 1;
};

// Callback may run on a native worker thread. It carries the session captured
// when the command arrived; the Qt bridge rejects queued commands after switching.
using SystemMediaCallback = std::function<void(SystemMediaCommand, double, quint64)>;
class SystemMediaBackend {
public:
    virtual ~SystemMediaBackend() = default;
    virtual void setWindow(quintptr windowId) = 0;
    virtual void update(const SystemMediaState &state) = 0;
};
std::unique_ptr<SystemMediaBackend> createSystemMediaBackend(SystemMediaCallback callback);
