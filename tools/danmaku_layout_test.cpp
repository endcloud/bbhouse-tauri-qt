#include "player/DanmakuLayout.h"
#include <QGuiApplication>
#include <QFontInfo>
#include <QElapsedTimer>
#include <QDebug>
#include <cmath>
#include <limits>

static QVariantMap entry(double t, int type, QString message = QStringLiteral("测试弹幕 Test")) {
    return {{"time", t}, {"type", type}, {"message", message}, {"fontSize", 25}};
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *label) { qInfo() << (ok ? "PASS" : "FAIL") << label; if (!ok) ++failures; };
    DanmakuLayout model;
    model.configure({640, 360}, 25, 100, 100);
    model.load({entry(0, 5)});
    auto frame = model.advance(1);
    check(frame.size() == 1 && frame[0].position.y() == 0 && frame[0].text->lineAt(0).y() >= 2,
          "first lane reserves outline padding and full text baseline");
    const auto layout = frame[0].text;
    const auto count = model.preparedCount();
    check(model.advance(1)[0].text == layout && model.preparedCount() == count, "paused frame reuses layout");
    model.advance(1.2);
    model.advance(1.19);
    check(model.preparedCount() == count, "small media clock correction does not reshape text");
    model.configure({640, 360}, 25, 100, 0);
    check(model.advance(1.3).isEmpty(), "zero area hides all lanes");
    model.configure({640, 10}, 25, 100, 100);
    check(model.advance(1.3).isEmpty(), "short viewport clips no partial first row");
    model.configure({640, 360}, 25, 100, 100);
    model.load({entry(0, 1), entry(1, 5)});
    model.reset(2);
    model.advance(2);
    check(model.advance(5.5).size() == 1, "expired center preserves older scroll");
    const auto left = model.advance(6)[0].position.x();
    check(model.advance(7)[0].position.x() < left, "scroll follows media time");
    model.reset(1);
    check(!model.advance(1).isEmpty(), "backward seek reconstructs active window");
    model.reset(50);
    check(model.advance(50).isEmpty(), "forward seek discards old text");
    model.load({entry(51, 4), entry(51, 5)});
    auto centers = model.advance(51);
    check(centers.size() == 2 && centers[0].position.y() > centers[1].position.y(), "top and bottom lanes differ");
    model.load({entry(-1, 1), entry(std::numeric_limits<double>::quiet_NaN(), 1), entry(0, 7), entry(0, 1, "")});
    model.reset(0);
    check(model.advance(0).isEmpty(), "invalid timestamps and unsupported/empty entries rejected");
    QVariantList dense;
    for (int i = 0; i < 20000; ++i) dense.append(entry(0, 1));
    model.load(dense);
    model.reset(1);
    const auto before = model.preparedCount();
    QElapsedTimer timer; timer.start();
    model.advance(1);
    qInfo() << "Dense 20000-entry admission ms:" << timer.nsecsElapsed() / 1e6;
    check(model.activeCount() <= 256 && model.preparedCount() - before <= 64, "burst admission has bounded preparation and nodes");
    const auto prepared = model.preparedCount();
    for (int i = 0; i < 500; ++i) model.advance(1 + i * .001);
    check(model.preparedCount() == prepared, "500 steady frames perform no new text layout");
    model.load({});
    check(model.activeCount() == 0 && model.frame().isEmpty(), "replace releases all active resources");
    {
        DanmakuLayout limited;
        limited.configure({1920, 1080}, 25, 100, 100);
        QVariantList mixed;
        for (int i = 0; i < 60; ++i) mixed.append(entry(0, i % 3 == 0 ? 1 : i % 3 == 1 ? 4 : 5));
        limited.setDensityLimit(20);
        limited.load(mixed);
        check(limited.advance(0).size() == 20, "density shares one 20-entry budget across all modes");
        check(limited.advance(4.1).size() < 20, "excess entries are dropped rather than replayed after expiry");
        limited.setDensityLimit(0);
        limited.reset(0);
        check(limited.advance(0).size() > 20, "unlimited restores lane admission beyond user cap");
        limited.configure({1920, 1080}, 48, 100, 25);
        limited.reset(0);
        bool inside = !limited.advance(0).isEmpty();
        for (const auto &visual : limited.frame())
            inside &= visual.position.y() >= 0 && visual.position.y() + visual.size.height() <= 270;
        check(inside, "large text in mixed modes stays fully inside top quarter");
    }
#ifdef Q_OS_MACOS
    check(QFontInfo(DanmakuLayout::platformFont(25)).family() == "PingFang SC", "macOS uses PingFang SC");
#endif
    return failures ? 1 : 0;
}
