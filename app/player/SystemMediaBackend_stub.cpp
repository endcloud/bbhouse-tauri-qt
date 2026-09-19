#include "player/SystemMediaBackend.h"

namespace {
class StubBackend final : public SystemMediaBackend {
    void setWindow(quintptr) override {}
    void update(const SystemMediaState &) override {}
};
}
std::unique_ptr<SystemMediaBackend> createSystemMediaBackend(SystemMediaCallback) {
    return std::make_unique<StubBackend>();
}
