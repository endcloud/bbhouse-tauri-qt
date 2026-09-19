#include "player/MpvLib.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QLibrary>

MpvLib *MpvLib::instance() {
    static MpvLib inst;
    return &inst;
}

MpvLib::MpvLib(QObject *parent) : QObject(parent) {}

template <typename Fn>
Fn MpvLib::resolve(QLibrary *lib, const char *name) {
    // QFunctionPointer(void(*)()) → 目标函数指针,同为函数指针间转换
    return reinterpret_cast<Fn>(lib->resolve(name));
}

bool MpvLib::probe() {
    if (library_) return available_;

    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QStringList candidates = {appDir + "/libmpv-2.dll", "libmpv-2.dll", "mpv-2.dll"};
#elif defined(Q_OS_MACOS)
    const QStringList candidates = {
        appDir + "/../Frameworks/libmpv.2.dylib", appDir + "/../Frameworks/libmpv.dylib",
        appDir + "/libmpv.2.dylib", appDir + "/libmpv.dylib",
        "/opt/homebrew/lib/libmpv.2.dylib", "/opt/homebrew/lib/libmpv.dylib",
        "/usr/local/lib/libmpv.2.dylib", "/usr/local/lib/libmpv.dylib",
        "libmpv.2.dylib", "libmpv.dylib"};
#else
    const QStringList candidates = {appDir + "/libmpv.so.2", "libmpv.so.2", "libmpv.so.1"};
#endif
    QStringList errors;
    for (const QString &candidate : candidates) {
        auto *candidateLib = new QLibrary(candidate, this);
        // Shared handles may survive the UI controller until GL cleanup finishes.
        candidateLib->setLoadHints(QLibrary::PreventUnloadHint);
        if (candidateLib->load()) {
            library_ = candidateLib;
            break;
        }
        errors.append(candidate + ": " + candidateLib->errorString());
        delete candidateLib;
    }
    if (!library_) {
        errorMessage_ = errors.join('\n');
        qWarning().noquote() << "libmpv load failed:" << errorMessage_;
        return false;
    }

    QLibrary *lib = library_;

    create = resolve<decltype(create)>(lib, "mpv_create");
    initialize = resolve<decltype(initialize)>(lib, "mpv_initialize");
    terminateDestroy = resolve<decltype(terminateDestroy)>(lib, "mpv_terminate_destroy");
    setWakeupCallback = resolve<decltype(setWakeupCallback)>(lib, "mpv_set_wakeup_callback");
    setOptionString = resolve<decltype(setOptionString)>(lib, "mpv_set_option_string");
    setPropertyStringFn = resolve<decltype(setPropertyStringFn)>(lib, "mpv_set_property_string");
    getProperty = resolve<decltype(getProperty)>(lib, "mpv_get_property");
    observeProperty = resolve<decltype(observeProperty)>(lib, "mpv_observe_property");
    commandFn = resolve<decltype(commandFn)>(lib, "mpv_command");
    commandAsyncFn = resolve<decltype(commandAsyncFn)>(lib, "mpv_command_async");
    abortAsyncCommand = resolve<decltype(abortAsyncCommand)>(lib, "mpv_abort_async_command");
    freeFn = resolve<decltype(freeFn)>(lib, "mpv_free");
    errorString = resolve<decltype(errorString)>(lib, "mpv_error_string");
    waitEvent = resolve<decltype(waitEvent)>(lib, "mpv_wait_event");
    clientApiVersion = resolve<decltype(clientApiVersion)>(lib, "mpv_client_api_version");

    renderContextCreate = resolve<decltype(renderContextCreate)>(lib, "mpv_render_context_create");
    renderContextFree = resolve<decltype(renderContextFree)>(lib, "mpv_render_context_free");
    renderContextRender = resolve<decltype(renderContextRender)>(lib, "mpv_render_context_render");
    renderContextSetUpdateCallback =
            resolve<decltype(renderContextSetUpdateCallback)>(lib, "mpv_render_context_set_update_callback");
    renderContextUpdate = resolve<decltype(renderContextUpdate)>(lib, "mpv_render_context_update");
    renderContextReportSwap = resolve<decltype(renderContextReportSwap)>(lib, "mpv_render_context_report_swap");

    QStringList missing;
#define REQUIRE_MPV(symbol) if (!symbol) missing.append(QStringLiteral(#symbol))
    REQUIRE_MPV(create); REQUIRE_MPV(initialize); REQUIRE_MPV(terminateDestroy);
    REQUIRE_MPV(setWakeupCallback); REQUIRE_MPV(setOptionString);
    REQUIRE_MPV(setPropertyStringFn); REQUIRE_MPV(getProperty); REQUIRE_MPV(observeProperty);
    REQUIRE_MPV(commandAsyncFn); REQUIRE_MPV(abortAsyncCommand); REQUIRE_MPV(commandFn); REQUIRE_MPV(freeFn); REQUIRE_MPV(errorString);
    REQUIRE_MPV(waitEvent); REQUIRE_MPV(clientApiVersion); REQUIRE_MPV(renderContextCreate);
    REQUIRE_MPV(renderContextFree); REQUIRE_MPV(renderContextRender);
    REQUIRE_MPV(renderContextSetUpdateCallback); REQUIRE_MPV(renderContextUpdate);
    REQUIRE_MPV(renderContextReportSwap);
#undef REQUIRE_MPV
    available_ = missing.isEmpty();
    if (!available_) {
        errorMessage_ = QStringLiteral("libmpv missing symbols: ") + missing.join(", ");
        qWarning().noquote() << errorMessage_;
        return false;
    }
    errorMessage_.clear();
    libraryPath_ = QFileInfo(lib->fileName()).absoluteFilePath();
    qDebug() << "libmpv loaded:" << libraryPath_;
    return true;
}
