// Offline synthetic-fixture measurement. No product code, visible window,
// credentials, network media, raw mpv logger, or user configuration.
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QThread>
#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

QJsonObject memory() {
    PROCESS_MEMORY_COUNTERS_EX value{};
    value.cb = sizeof(value);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&value), sizeof(value)))
        return {{"error", "GetProcessMemoryInfo failed"}};
    return {{"private_mib", double(value.PrivateUsage) / 1048576.},
            {"working_set_mib", double(value.WorkingSetSize) / 1048576.}};
}

void *getProc(void *context, const char *name) {
    return reinterpret_cast<void *>(static_cast<QOpenGLContext *>(context)->getProcAddress(name));
}

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    for (const QString &name : {"dll", "fixture", "next-fixture"}) cli.addOption({name, name, "path"});
    cli.addOption({"hwdec", "Decoder setting for this probe only", "mode", "auto-safe"});
    cli.addOption({"threads", "Software decoder thread limit", "count", "0"});
    cli.addOption({"hw-threads", "Hardware decoder thread limit (probe only)", "count", "4"});
    cli.addOption({"extra", "Hardware decoder extra frames", "count", "auto"});
    cli.addOption({"direct-render", "Decoder direct rendering", "value", "auto"});
    cli.addOption({"fbo-format", "Intermediate rendering format", "value", "auto"});
    cli.addOption({"scale", "Upscaling filter", "value", "lanczos"});
    cli.addOption({"dscale", "Downscaling filter", "value", "hermite"});
    cli.addOption({"seconds", "Sampling duration per fixture", "value", "6"});
    cli.addOption({"width", "Offscreen output width", "value", "1920"});
    cli.addOption({"height", "Offscreen output height", "value", "1080"});
    cli.process(app);
    QStringList fixtures{cli.value("fixture")};
    if (cli.isSet("next-fixture")) fixtures << cli.value("next-fixture") << cli.value("fixture");
    for (QString &fixture : fixtures) {
        const QFileInfo info(fixture);
        if (!info.isFile() || info.absoluteFilePath().startsWith("//") || info.absoluteFilePath().startsWith("\\\\")) return 2;
        fixture = info.absoluteFilePath();
    }
    QLibrary lib(cli.value("dll"));
    if (!lib.load()) return 3;
#define LOAD(name) const auto p_##name = reinterpret_cast<decltype(&name)>(lib.resolve(#name)); if (!p_##name) return 4;
    LOAD(mpv_create) LOAD(mpv_initialize) LOAD(mpv_set_option_string)
    LOAD(mpv_get_property_string) LOAD(mpv_command) LOAD(mpv_free) LOAD(mpv_terminate_destroy)
    LOAD(mpv_render_context_create) LOAD(mpv_render_context_render) LOAD(mpv_render_context_free)
    QJsonObject output{{"mode", "native-offscreen-opengl"}, {"baseline", memory()},
                       {"hwdec_option", cli.value("hwdec")}, {"threads_option", cli.value("threads")},
                       {"hw_threads_option", cli.value("hw-threads")},
                       {"extra_frames_option", cli.value("extra")},
                       {"direct_render_option", cli.value("direct-render")},
                       {"fbo_format_option", cli.value("fbo-format")},
                       {"scale_option", cli.value("scale")}, {"dscale_option", cli.value("dscale")},
                       {"output_width", cli.value("width").toInt()},
                       {"output_height", cli.value("height").toInt()}};
    QOpenGLContext gl;
    gl.setFormat(format);
    if (!gl.create()) return 5;
    QOffscreenSurface surface;
    surface.setFormat(gl.format());
    surface.create();
    if (!surface.isValid() || !gl.makeCurrent(&surface)) return 6;
    const auto functions = gl.functions();
    output["gl_renderer"] = QString::fromLatin1(reinterpret_cast<const char *>(functions->glGetString(GL_RENDERER)));
    output["gl_version"] = QString::fromLatin1(reinterpret_cast<const char *>(functions->glGetString(GL_VERSION)));
    QOpenGLFramebufferObject fbo(QSize(cli.value("width").toInt(), cli.value("height").toInt()));
    output["after_gl"] = memory();
    mpv_handle *handle = p_mpv_create();
    if (!handle) return 7;
    auto option = [&](const char *name, const char *value) { return p_mpv_set_option_string(handle, name, value) >= 0; };
    for (const auto pair : {std::pair{"config", "no"}, {"load-scripts", "no"}, {"terminal", "no"},
         {"msg-level", "all=no"}, {"vo", "libmpv"}, {"ao", "null"}, {"idle", "yes"},
         {"keep-open", "always"}, {"ytdl", "no"}, {"cache", "no"}, {"sub-auto", "no"},
         {"audio-file-auto", "no"}, {"access-references", "no"}, {"http-proxy", ""},
         {"stream-lavf-o", "http_proxy="}}) if (!option(pair.first, pair.second)) return 8;
    if (!option("hwdec", cli.value("hwdec").toUtf8()) ||
        !option("vd-lavc-threads", cli.value("threads").toUtf8()) ||
        !option("hwdec-threads", cli.value("hw-threads").toUtf8()) ||
        !option("hwdec-extra-frames", cli.value("extra").toUtf8()) ||
        !option("vd-lavc-dr", cli.value("direct-render").toUtf8()) ||
        !option("fbo-format", cli.value("fbo-format").toUtf8()) ||
        !option("scale", cli.value("scale").toUtf8()) ||
        !option("dscale", cli.value("dscale").toUtf8())) return 8;
    if (p_mpv_initialize(handle) < 0) return 9;
    auto property = [&](const char *key) {
        char *data = p_mpv_get_property_string(handle, key);
        const QString value = data ? QString::fromUtf8(data) : QString();
        if (data) p_mpv_free(data);
        return value;
    };
    mpv_opengl_init_params init{getProc, &gl};
    mpv_render_param initParams[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                                    {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context *render = nullptr;
    if (p_mpv_render_context_create(&render, handle, initParams) < 0) return 10;
    output["initialized"] = memory();
    QJsonArray phases;
    int phaseIndex = 0;
    for (const QString &fixture : fixtures) {
        const QByteArray path = fixture.toUtf8();
        const char *load[] = {"loadfile", path.constData(), "replace", nullptr};
        if (p_mpv_command(handle, load) < 0) return 11;
        QJsonArray samples;
        QElapsedTimer timer;
        timer.start();
        qint64 nextSample = 0;
        while (timer.elapsed() < cli.value("seconds").toDouble() * 1000.) {
            QCoreApplication::processEvents();
            gl.makeCurrent(&surface);
            mpv_opengl_fbo target{int(fbo.handle()), fbo.width(), fbo.height(), 0};
            int flip = 0;
            mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &target},
                                        {MPV_RENDER_PARAM_FLIP_Y, &flip}, {MPV_RENDER_PARAM_INVALID, nullptr}};
            if (p_mpv_render_context_render(render, params) < 0) return 12;
            if (timer.elapsed() >= nextSample) {
                QJsonObject sample = memory();
                sample["elapsed_ms"] = timer.elapsed();
                sample["hwdec"] = property("hwdec-current");
                sample["playback_time"] = property("time-pos");
                samples.append(sample);
                nextSample = timer.elapsed() + 100;
            }
            QThread::msleep(5);
        }
        QJsonObject properties;
        for (const char *name : {"mpv-version", "ffmpeg-version", "hwdec-current", "current-vo", "video-codec",
             "video-params/w", "video-params/h", "video-params/pixelformat", "video-params/hw-pixelformat",
             "video-params/primaries", "video-params/gamma", "estimated-vf-fps", "decoder-frame-drop-count",
             "frame-drop-count", "options/vd-lavc-threads", "options/hwdec-extra-frames",
             "options/vd-lavc-dr", "options/fbo-format", "options/scale", "options/dscale", "options/hwdec-threads",
             "video-params/colormatrix", "video-params/sig-peak", "video-params/dolby-vision-profile",
             "video-dec-params/pixelformat", "video-dec-params/primaries", "video-dec-params/gamma"}) properties[name] = property(name);
        phases.append(QJsonObject{{"phase", ++phaseIndex}, {"properties", properties}, {"samples", samples}});
    }
    output["phases"] = phases;
    const char *stop[] = {"stop", nullptr};
    p_mpv_command(handle, stop);
    QThread::msleep(200);
    output["after_stop_render_context_alive"] = memory();
    gl.makeCurrent(&surface);
    p_mpv_render_context_free(render);
    p_mpv_terminate_destroy(handle);
    // The QOpenGLContext and FBO still exist here; this is NOT a process-wide
    // or graphics-driver teardown measurement.
    output["after_mpv_destroy_gl_alive"] = memory();
    std::cout << QJsonDocument(output).toJson(QJsonDocument::Indented).constData();
    return 0;
}
