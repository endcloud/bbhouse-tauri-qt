#include "player/SystemMediaControls.h"
#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <limits>
#include <thread>

struct FakeBackend final : SystemMediaBackend {
    SystemMediaState state;
    quintptr window = 0;
    void setWindow(quintptr id) override { window = id; }
    void update(const SystemMediaState &value) override { state = value; }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool value, const char *description) {
        if (!value) { qCritical() << description; ++failures; }
    };
    SystemMediaCallback nativeCallback;
    FakeBackend *native = nullptr;
    auto controls = std::make_unique<SystemMediaControls>(nullptr, [&](SystemMediaCallback callback) {
        nativeCallback = std::move(callback);
        auto backend = std::make_unique<FakeBackend>();
        native = backend.get();
        return backend;
    });
    int delivered = 0;
    double seek = -1;
    QObject::connect(controls.get(), &SystemMediaControls::commandRequested,
                     &app, [&](SystemMediaCommand command, double value) {
        check(QThread::currentThread() == app.thread(), "Native command must run on GUI thread");
        ++delivered;
        if (command == SystemMediaCommand::Seek) seek = value;
    });
    SystemMediaState state;
    state.session = 1;
    state.active = true;
    state.paused = true;
    state.title = QStringLiteral("Offline video");
    state.duration = 120;
    state.canSeek = true;
    state.canNext = true;
    controls->setWindow(123);
    controls->update(state);
    check(native->window == 123 && native->state.title == state.title, "Metadata/window forwarded");
    std::thread worker([&] { nativeCallback(SystemMediaCommand::Play, 0, 1); });
    worker.join();
    check(delivered == 0, "Worker command is queued");
    QCoreApplication::processEvents();
    check(delivered == 1, "Play delivered");
    nativeCallback(SystemMediaCommand::Next, 0, 1);
    state.session = 2;
    controls->update(state);
    QCoreApplication::processEvents();
    check(delivered == 1, "Old session command rejected after switch");
    nativeCallback(SystemMediaCommand::Pause, 0, 2);
    nativeCallback(SystemMediaCommand::Previous, 0, 2);
    nativeCallback(SystemMediaCommand::Seek, std::numeric_limits<double>::quiet_NaN(), 2);
    QCoreApplication::processEvents();
    check(delivered == 1, "Disabled and invalid commands rejected");
    nativeCallback(SystemMediaCommand::Seek, 200, 2);
    QCoreApplication::processEvents();
    check(delivered == 2 && seek == 120, "Seek clamps to duration");
    state.paused = false;
    controls->update(state);
    nativeCallback(SystemMediaCommand::Play, 0, 2);
    nativeCallback(SystemMediaCommand::Pause, 0, 2);
    QCoreApplication::processEvents();
    check(delivered == 3, "Absolute play/pause are idempotent");
    nativeCallback(SystemMediaCommand::Next, 0, 2);
    controls->clear();
    QCoreApplication::processEvents();
    check(delivered == 3 && !native->state.active && native->state.title.isEmpty(), "Close clears metadata and queued commands");
    state.session = 3;
    state.duration = std::numeric_limits<double>::infinity();
    state.position = -4;
    state.rate = std::numeric_limits<double>::quiet_NaN();
    controls->update(state);
    check(native->state.duration == 0 && native->state.position == 0 && native->state.rate == 1
          && !native->state.canSeek, "Invalid timeline values sanitized");
    nativeCallback(SystemMediaCommand::Next, 0, 3);
    controls.reset();
    std::thread late([&] { nativeCallback(SystemMediaCommand::Play, 0, 3); });
    late.join();
    QCoreApplication::processEvents();
    check(delivered == 3, "Destruction safely cancels pending and late native callbacks");
    if (!failures) qInfo() << "System media regression passed";
    return failures ? 1 : 0;
}
