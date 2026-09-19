#include <QGuiApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include "player/MpvClient.h"
#include "player/MpvLib.h"
#include "player/ScreenshotPath.h"

// Synthetic 2-second, 128x72 red H.264 fixture; no network or user media.
// Generated once with:
// ffmpeg -f lavfi -i color=c=red:s=128x72:d=2:r=24 -c:v libx264
//        -pix_fmt yuv420p screenshot-red-h264.mp4
// PNG/vo=null cannot exercise the macOS hardware-frame screenshot regression.
int runMacScreenshotTest(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
        failures += !ok;
        return ok;
    };
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/mac-screenshot-XXXXXX");
    if (!check(temp.isValid(), "fixtures remain under build")) return 1;

    QOpenGLContext gl;
    gl.setFormat(format);
    if (!check(gl.create(), "native OpenGL context created")) return 1;
    QOffscreenSurface surface;
    surface.setFormat(gl.format());
    surface.create();
    if (!check(surface.isValid() && gl.makeCurrent(&surface),
               "offscreen OpenGL surface current without a visible window")) return 1;
    QOpenGLFramebufferObject fbo(128, 72);
    if (!check(fbo.isValid(), "video framebuffer created")) return 1;
    std::unique_ptr<MpvClient> client(MpvClient::create());
    if (!check(bool(client), "real mpv initialized with production decoder options")) return 1;
    auto *lib = MpvLib::instance();
    mpv_render_context *render = nullptr;
    mpv_opengl_init_params init{};
    init.get_proc_address = [](void *, const char *name) -> void * {
        return reinterpret_cast<void *>(QOpenGLContext::currentContext()->getProcAddress(name));
    };
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (!check(lib->renderContextCreate(&render, client->handle(), params) >= 0,
               "libmpv OpenGL renderer initialized")) return 1;
    // Destroy the render context before the client and while GL is current.
    const auto freeRender = [lib](mpv_render_context *value) { lib->renderContextFree(value); };
    std::unique_ptr<mpv_render_context, decltype(freeRender)> renderOwner(render, freeRender);
    int frames = 0;
    auto until = [&](const std::function<bool()> &predicate) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (lib->renderContextUpdate(render) & MPV_RENDER_UPDATE_FRAME) {
                mpv_opengl_fbo target{int(fbo.handle()), 128, 72, 0};
                int flip = 0;
                mpv_render_param draw[] = {
                    {MPV_RENDER_PARAM_OPENGL_FBO, &target},
                    {MPV_RENDER_PARAM_FLIP_Y, &flip},
                    {MPV_RENDER_PARAM_INVALID, nullptr}};
                lib->renderContextRender(render, draw);
                ++frames;
            }
            QThread::msleep(2);
        }
        return predicate();
    };
    client->setPropertyString("ao", "null");
    const QString input = temp.filePath(QStringLiteral("本地 视频.mp4"));
    check(QFile::copy(QStringLiteral(BBHOUSE_SCREENSHOT_FIXTURE), input), "copy H.264 fixture");
    client->loadSingle(input, 0);
    check(until([&] { return frames >= 3 && client->getPropertyDouble("video-out-params/w") == 128; }),
          "H.264 frames decoded and rendered through libmpv OpenGL");
    std::cout << "INFO hwdec-current=" << client->getPropertyString("hwdec-current").toStdString()
              << std::endl;

    const QString subtitles = temp.filePath("overlay.srt");
    QFile subtitleFile(subtitles);
    check(subtitleFile.open(QIODevice::WriteOnly), "create subtitle exclusion fixture");
    subtitleFile.write("1\n00:00:00,000 --> 00:10:00,000\nMUST NOT APPEAR\n");
    subtitleFile.close();
    check(client->command({"sub-add", subtitles, "select"}) >= 0, "select subtitle track");

    auto verifyImage = [&](const QString &path) {
        QImage image(path);
        bool red = image.size() == QSize(128, 72);
        for (int y = 0; red && y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                // Allow H.264/YUV rounding; white subtitle pixels fail this check.
                if (pixel.red() < 240 || pixel.green() > 15 || pixel.blue() > 15) {
                    red = false;
                    break;
                }
            }
        }
        check(red, "saved PNG contains only the decoded red video frame");
    };
    int completed = 0;
    QSet<QString> paths;
    for (int i = 0; i < 2; ++i) {
        const QString path = ScreenshotPath::next(temp.path(), QStringLiteral("中文 连续截图"));
        client->commandAsync({"screenshot-to-file", path, "video"}, &app, [&, path](int result) {
            check(result >= 0, "hardware-decoded playing frame screenshot succeeds");
            verifyImage(path);
            paths.insert(path);
            ++completed;
        });
    }
    check(until([&] { return completed == 2; }) && paths.size() == 2,
          "concurrent captures complete with distinct Unicode filenames");
    check(!client->getFlag("pause"), "playing screenshot does not pause playback");

    client->setPaused(true);
    check(until([&] { return client->getFlag("pause"); }), "pause before capture");
    const double pausedPosition = client->getPropertyDouble("time-pos");
    const QString pausedPath = ScreenshotPath::next(temp.path(), "paused");
    client->commandAsync({"screenshot-to-file", pausedPath, "video"}, &app, [&](int result) {
        check(result >= 0, "paused hardware-decoded frame screenshot succeeds");
        verifyImage(pausedPath);
        ++completed;
    });
    check(until([&] { return completed == 3; }), "paused screenshot completes");
    const QString invalidPath = temp.filePath("missing/frame.png");
    client->commandAsync({"screenshot-to-file", invalidPath, "video"}, &app, [&](int result) {
        check(result < 0 && !QFile::exists(invalidPath), "write failure is reported without a fake file");
        ++completed;
    });
    check(until([&] { return completed == 4; }), "write failure completes");
    check(client->getFlag("pause") &&
              std::abs(client->getPropertyDouble("time-pos") - pausedPosition) < 0.01,
          "successful and failed captures preserve paused position");
    client->stop();
    return failures ? 1 : 0;
}
