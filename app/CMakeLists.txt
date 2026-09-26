qt_add_executable(bbhouse-qt
        main.cpp
        controllers/LoginController.h controllers/LoginController.cpp
        core/LoginCookies.h core/LoginCookies.cpp
        downloads/DownloadController.h downloads/DownloadController.cpp
        downloads/DownloadCover.h downloads/DownloadCover.cpp
        downloads/DownloadStore.h downloads/DownloadStore.cpp
        downloads/DownloadWorker.h downloads/DownloadWorker.cpp
        downloads/DownloadUtils.h downloads/DownloadUtils.cpp
        core/ApiErrors.h
        core/LiveApi.h core/LiveApi.cpp
        core/PopularApi.h core/PopularApi.cpp
        controllers/PopularController.h controllers/PopularController.cpp
        controllers/LiveController.h controllers/LiveController.cpp
        player/LivePlayerController.h player/LivePlayerController.cpp
        core/AppPaths.h core/AppPaths.cpp
        core/UserProfileApi.h core/UserProfileApi.cpp
        core/CardAuthor.h
        core/AvatarCache.h core/AvatarCache.cpp
        core/AvatarSource.h core/AvatarSource.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/BangumiApi.h core/BangumiApi.cpp
        core/DynamicApi.h core/DynamicApi.cpp
        core/DynamicFeedModels.h
        core/HistoryApi.h core/HistoryApi.cpp
        core/HistoryModels.h
        core/HistoryStore.h core/HistoryStore.cpp
        core/HistorySyncRunner.h core/HistorySyncRunner.cpp
        core/HistoryScheduler.h core/HistoryScheduler.cpp
        core/HistoryServiceEntry.h core/HistoryServiceEntry.cpp
        core/HistoryServiceWorker.h core/HistoryServiceWorker.cpp core/HistoryServiceSchedule.h
        controllers/HistoryServiceController.h controllers/HistoryServiceController.cpp
        core/DanmakuParser.h core/DanmakuParser.cpp
        core/JsonHelpers.h
        core/ToviewApi.h core/ToviewApi.cpp
        core/PlayerApi.h core/PlayerApi.cpp
        core/WbiSigner.h core/WbiSigner.cpp
        core/CookieHelpers.h
        core/RelationApi.h core/RelationApi.cpp
        core/SeasonsApi.h core/SeasonsApi.cpp
        core/ArcSearchApi.h core/ArcSearchApi.cpp
        core/ArticleApi.h core/ArticleApi.cpp
        core/VideoZones.h core/VideoZones.cpp
        core/VideoZoneApi.h core/VideoZoneApi.cpp
        core/HeartbeatApi.h core/HeartbeatApi.cpp
        controllers/DynamicCardModel.h controllers/DynamicCardModel.cpp
        controllers/DynamicsController.h controllers/DynamicsController.cpp
        controllers/AppController.h controllers/AppController.cpp
        controllers/BangumiController.h controllers/BangumiController.cpp
        controllers/HistoryController.h controllers/HistoryController.cpp
        controllers/OnlineHistoryController.h controllers/OnlineHistoryController.cpp
        controllers/SpecialFollowStore.h controllers/SpecialFollowStore.cpp
        controllers/SpecialFollowController.h controllers/SpecialFollowController.cpp
        controllers/UserSpaceController.h controllers/UserSpaceController.cpp
        controllers/WatchlaterController.h controllers/WatchlaterController.cpp
        preferences/AppPreferences.h preferences/AppPreferences.cpp
        player/MpvClient.h player/MpvClient.cpp
        player/MpvLib.h player/MpvLib.cpp
        player/MpvVideoItem.h player/MpvVideoItem.cpp
        player/DanmakuFilter.h player/DanmakuFilter.cpp
        player/DanmakuEngine.h player/DanmakuEngine.cpp
        player/DanmakuSpriteLayout.h player/DanmakuSpriteLayout.cpp
        player/DanmakuLayout.h player/DanmakuLayout.cpp
        player/SubtitleTimeline.h
        player/PlayerController.h player/PlayerController.cpp
        player/SystemMediaBackend.h
        player/SystemMediaControls.h player/SystemMediaControls.cpp
        player/ScreenshotPath.h
        player/ScreenshotWriter.h player/ScreenshotWriter.cpp
)

# Native executable icons must not depend on the compiler working directory.
if(WIN32)
    enable_language(RC)
    set(BBHOUSE_WINDOWS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.ico")
    set(BBHOUSE_FILE_DESCRIPTION "BBHouse")
    set(BBHOUSE_EXECUTABLE_FILENAME "BBHouse.exe")
    configure_file(resources/icons/icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc" PROPERTIES
        OBJECT_DEPENDS "${BBHOUSE_WINDOWS_ICON}")
    target_sources(bbhouse-qt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app-icon.rc")
    set_target_properties(bbhouse-qt PROPERTIES OUTPUT_NAME "BBHouse")
    set(BBHOUSE_FILE_DESCRIPTION "BBHouse History Service")
    set(BBHOUSE_EXECUTABLE_FILENAME "bbhouse-history-service.exe")
    configure_file(resources/icons/icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/service-icon.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/service-icon.rc" PROPERTIES
        OBJECT_DEPENDS "${BBHOUSE_WINDOWS_ICON}")
endif()

# Native SCM/UAC host deliberately has no Qt imports: it must start outside
# Qt Creator's PATH. The host supplies captured runtime paths to the Qt worker.
if(WIN32)
    add_executable(bbhouse-history-service windows/HistoryServiceHost.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/service-icon.rc")
    target_compile_definitions(bbhouse-history-service PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(bbhouse-history-service PRIVATE advapi32 shell32 ole32 oleaut32 taskschd uuid)
    set_target_properties(bbhouse-history-service PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
    if(MINGW)
        target_link_options(bbhouse-history-service PRIVATE -static -static-libgcc -static-libstdc++)
    elseif(MSVC)
        set_property(TARGET bbhouse-history-service PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_dependencies(bbhouse-qt bbhouse-history-service)
    target_link_libraries(bbhouse-qt PRIVATE shell32)
endif()

# 原生系统媒体控制；Windows 用 WinRT ABI，同时兼容 MinGW/MSVC。
if(APPLE)
    enable_language(OBJCXX)
    target_sources(bbhouse-qt PRIVATE player/SystemMediaBackend_mac.mm
        player/SystemMediaArtwork.h player/SystemMediaArtwork.cpp)
    set_source_files_properties(player/SystemMediaBackend_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(bbhouse-qt PRIVATE "-framework Foundation" "-framework MediaPlayer" "-framework AppKit")
elseif(WIN32)
    target_sources(bbhouse-qt PRIVATE player/SystemMediaBackend_win.cpp)
    target_link_libraries(bbhouse-qt PRIVATE runtimeobject ole32 uuid)
else()
    target_sources(bbhouse-qt PRIVATE player/SystemMediaBackend_stub.cpp)
endif()

set_source_files_properties(qml/js/Format.js PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)

# 发布包双击不应弹控制台:显式声明 GUI 子系统(否则 MinGW 默认按 CUI 链接,
# 只会出现一个黑框且看不到任何 QML 报错,Qt Creator 里则不易察觉)
set_target_properties(bbhouse-qt PROPERTIES WIN32_EXECUTABLE TRUE)

qt_add_qml_module(bbhouse-qt
        URI bbhouse
        VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        QML_FILES
        qml/Main.qml
        qml/LoginWindow.qml
        qml/pages/LoginPage.qml
        qml/MainWindow.qml
        qml/LivePlayerWindow.qml
        qml/HistoryServiceWindow.qml
        qml/pages/LivePage.qml
        qml/pages/PopularPage.qml
        qml/PlayerWindow.qml
        qml/controls/PlayerControls.qml
        qml/controls/PlayerControlButton.qml
        qml/controls/ExpiringPageLoader.qml
        qml/controls/PageSearchBox.qml
        qml/controls/CoverPreviewOverlay.qml
        qml/controls/HistoryCard.qml
        qml/controls/DownloadDialog.qml
        qml/controls/SeasonCard.qml
        qml/js/Format.js
        qml/pages/BangumiPage.qml
        qml/pages/DynamicsPage.qml
        qml/pages/LocalHistoryPage.qml
        qml/pages/OnlineHistoryPage.qml
        qml/pages/PlaceholderPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/AboutPage.qml
        qml/pages/DownloadsPage.qml
        qml/pages/SpecialFollowPage.qml
        qml/pages/UserSpacePage.qml
        qml/pages/WatchlaterPage.qml
)

# Shared with the real About page regression so embedded image URLs resolve there.
function(bbhouse_add_icon_resources target)
    qt_add_resources(${target} "app_icons"
        PREFIX "/icons"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
        FILES resources/icons/bbhouse-icon-1024.png resources/icons/bbhouse-icon-1024-mac.png)
endfunction()
bbhouse_add_icon_resources(bbhouse-qt)
if(WIN32)
    qt_add_resources(bbhouse-qt "deployment_video_fixture"
        PREFIX "/deployment"
        BASE "${CMAKE_SOURCE_DIR}/tools/fixtures"
        FILES "${CMAKE_SOURCE_DIR}/tools/fixtures/screenshot-red-h264.mp4")
endif()

# Desktop integration only; Linux runtime/dependency packaging is distro-specific.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(FILES resources/linux/io.github.endcloud.bbhouse-qt.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications" COMPONENT DesktopIntegration)
    install(FILES resources/icons/bbhouse-icon-1024.png
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/1024x1024/apps"
        RENAME io.github.endcloud.bbhouse-qt.png COMPONENT DesktopIntegration)
endif()

# 随程序打包的默认头像，离线/缺失头像无需外部文件。
qt_add_resources(bbhouse-qt "app_images"
        PREFIX "/"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        FILES resources/images/noface.jpg
)

# 导入帮助直接嵌入可执行文件，开发运行及各平台发行包共用同一份文档。
# MinGW Makefiles cannot reliably resolve non-ASCII file dependencies on Windows.
# Stage the document under an ASCII filename while preserving its public resource URL.
set(BBHOUSE_COOKIE_HELP "${CMAKE_CURRENT_BINARY_DIR}/help/cookie-import-help.md")
configure_file("${CMAKE_SOURCE_DIR}/doc/Cookie导入帮助.md" "${BBHOUSE_COOKIE_HELP}" COPYONLY)
set_source_files_properties("${BBHOUSE_COOKIE_HELP}" PROPERTIES QT_RESOURCE_ALIAS "Cookie导入帮助.md")
qt_add_resources(bbhouse-qt "app_help"
        PREFIX "/help"
        FILES "${BBHOUSE_COOKIE_HELP}"
)

# i18n: en-US 翻译(lrelease 产物嵌入资源 :/i18n/)
find_program(QT_LRELEASE NAMES lrelease HINTS
        "${Qt6_DIR}/../../../bin" "${Qt6_DIR}/../../../libexec/qt6" REQUIRED)
set(BBHOUSE_TS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/translations/bbhouse_en_US.ts)
set(BBHOUSE_QM_FILE ${CMAKE_CURRENT_BINARY_DIR}/bbhouse_en_US.qm)
add_custom_command(
        OUTPUT ${BBHOUSE_QM_FILE}
        COMMAND ${QT_LRELEASE} ${BBHOUSE_TS_FILE} -qm ${BBHOUSE_QM_FILE}
        DEPENDS ${BBHOUSE_TS_FILE}
        COMMENT "lrelease bbhouse_en_US"
)
set_source_files_properties(${BBHOUSE_QM_FILE} PROPERTIES QT_RESOURCE_ALIAS "bbhouse_en_US.qm")
qt_add_resources(bbhouse-qt "app_translations"
        PREFIX "/i18n"
        FILES ${BBHOUSE_QM_FILE}
)

target_include_directories(bbhouse-qt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/3rd/mpv/include
)

target_link_libraries(bbhouse-qt PRIVATE
        Qt6::Quick
        Qt6::Qml
        Qt6::Widgets
        Qt6::Network
        Qt6::Sql
        Qt6::Concurrent
        fluentuiplugin
)

# 弹幕解压用 zlib:Windows 走 Qt 私有 QtZlib(头文件直引免链接),其余平台链系统 zlib
if (NOT WIN32)
        find_package(ZLIB REQUIRED)
        target_link_libraries(bbhouse-qt PRIVATE ZLIB::ZLIB)
endif()

# exe 对 fluentuiplugin.dll 的硬链接依赖:复制一份到 exe 同级目录供 Windows 加载器查找
add_custom_command(TARGET bbhouse-qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:fluentuiplugin> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        COMMENT "Copy fluentuiplugin.dll next to exe"
)

# libmpv 运行时:内核用 QLibrary 动态加载。macOS 会去 Homebrew 前缀找 dylib,
# Windows 只在 exe 同级目录/系统搜索路径里找 libmpv-2.dll,因此这里把仓库里的
# DLL 复制到各目标输出目录——否则 exe 与 player-runtime 测试都会静默加载失败。
# 运行库不入源码库；分发须核对实际构建的许可与对应源码。缺失不阻断编译。
set(BBHOUSE_MPV_DLL "${CMAKE_SOURCE_DIR}/3rd/mpv/libmpv-2.dll")
function(bbhouse_copy_mpv_dll target)
    if (WIN32 AND EXISTS "${BBHOUSE_MPV_DLL}")
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${BBHOUSE_MPV_DLL}" "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Copy libmpv-2.dll next to $<TARGET_FILE_NAME:${target}>"
        )
    endif ()
endfunction()

if (WIN32 AND NOT EXISTS "${BBHOUSE_MPV_DLL}")
    message(WARNING "3rd/mpv/libmpv-2.dll 不存在:播放器与 player-runtime 测试无法加载 libmpv;"
            "请下载与架构匹配的 libmpv-2.dll 放到 3rd/mpv/(仓库不提交该文件)。")
endif ()

bbhouse_copy_mpv_dll(bbhouse-qt)

# 无头自测:存储层(退出码 0 = 通过)
add_executable(store-test EXCLUDE_FROM_ALL
        ../tools/store_test.cpp
        core/HistoryStore.h core/HistoryStore.cpp
        core/HistoryModels.h
        core/DanmakuParser.h core/DanmakuParser.cpp
        core/JsonHelpers.h
)
target_include_directories(store-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(store-test PRIVATE Qt6::Core Qt6::Sql)
set_target_properties(store-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# 无头探针:API 管线冒烟(nav+wbi+历史首页,共 2 个 GET)
add_executable(api-probe EXCLUDE_FROM_ALL
        ../tools/api_probe.cpp
        core/ApiErrors.h
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/HistoryApi.h core/HistoryApi.cpp
        core/HistoryModels.h
        core/DanmakuParser.h core/DanmakuParser.cpp
        core/JsonHelpers.h
        core/PlayerApi.h core/PlayerApi.cpp
        core/WbiSigner.h core/WbiSigner.cpp
)
target_include_directories(api-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(api-probe PRIVATE Qt6::Core Qt6::Network)
if (NOT WIN32)
        # PlayerApi 弹幕解压依赖(见上方 bbhouse-qt 同款说明)
        target_link_libraries(api-probe PRIVATE ZLIB::ZLIB)
endif()
set_target_properties(api-probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# 交付目录 build/deploy/:exe + Qt 运行时 + FluentUI 模块 + libmpv-2.dll
if(WIN32)
find_program(WINDEPLOYQT_EXE NAMES windeployqt HINTS "${Qt6_DIR}/../../../bin")
# MinGW 运行时:windeployqt 配 --no-compiler-runtime 不会拷 libstdc++/libgcc/libwinpthread,
# 缺了 exe 在本机直接起不来(开发时靠 Qt Creator 的 PATH 掩盖)。从编译器目录显式补齐。
set(BBHOUSE_GCC_RUNTIME_DLLS "")
if (MINGW)
    get_filename_component(BBHOUSE_GCC_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach (_gcc_dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libstdc++-6.dll libwinpthread-1.dll)
        if (EXISTS "${BBHOUSE_GCC_BIN}/${_gcc_dll}")
            list(APPEND BBHOUSE_GCC_RUNTIME_DLLS "${BBHOUSE_GCC_BIN}/${_gcc_dll}")
        endif ()
    endforeach ()
endif ()
set(_bbhouse_deploy_runtime_cmd "")
if (BBHOUSE_GCC_RUNTIME_DLLS)
    set(_bbhouse_deploy_runtime_cmd
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${BBHOUSE_GCC_RUNTIME_DLLS} ${CMAKE_BINARY_DIR}/deploy/)
endif ()
add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/deploy
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/deploy
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:bbhouse-qt> ${CMAKE_BINARY_DIR}/deploy/
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:bbhouse-history-service> ${CMAKE_BINARY_DIR}/deploy/
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/FluentUI ${CMAKE_BINARY_DIR}/deploy/FluentUI
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fluentuiplugin.dll ${CMAKE_BINARY_DIR}/deploy/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/3rd/mpv/libmpv-2.dll ${CMAKE_BINARY_DIR}/deploy/
        # fluentuiplugin.dll 的导入表里有 Qt6PrintSupport(windeployqt 只扫 exe,漏掉插件依赖),
        # 缺了它 QML 侧 "Cannot load library .../FluentUI/fluentuiplugin.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::PrintSupport> ${CMAKE_BINARY_DIR}/deploy/
        ${_bbhouse_deploy_runtime_cmd}
        COMMAND ${WINDEPLOYQT_EXE} --no-compiler-runtime --no-system-d3d-compiler
                --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/qml
                ${CMAKE_BINARY_DIR}/deploy/$<TARGET_FILE_NAME:bbhouse-qt>
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
                "${CMAKE_BINARY_DIR}/deploy/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/licenses" "${CMAKE_BINARY_DIR}/deploy/licenses"
        DEPENDS bbhouse-qt
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Deploy runnable directory to build/deploy"
)

endif()

add_executable(api-regression-test EXCLUDE_FROM_ALL
        ../tools/api_regression_test.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/PlayerApi.h core/PlayerApi.cpp
        core/WbiSigner.h core/WbiSigner.cpp)
target_include_directories(api-regression-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(api-regression-test PRIVATE Qt6::Core Qt6::Network)
if(NOT WIN32)
    target_link_libraries(api-regression-test PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME store COMMAND store-test)
add_test(NAME api-regression COMMAND api-regression-test)

add_executable(playback-entry-test EXCLUDE_FROM_ALL ../tools/playback_entry_test.cpp)
target_include_directories(playback-entry-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(playback-entry-test PRIVATE Qt6::Core Qt6::Qml)
add_test(NAME playback-entry COMMAND playback-entry-test)

add_executable(player-runtime-test EXCLUDE_FROM_ALL
        ../tools/player_runtime_test.cpp
        player/MpvLib.h player/MpvLib.cpp
        player/MpvClient.h player/MpvClient.cpp
        player/DanmakuFilter.h player/DanmakuFilter.cpp
        player/DanmakuEngine.h player/DanmakuEngine.cpp
        player/DanmakuSpriteLayout.h player/DanmakuSpriteLayout.cpp
        player/DanmakuLayout.h player/DanmakuLayout.cpp)
target_include_directories(player-runtime-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/3rd/mpv/include)
target_link_libraries(player-runtime-test PRIVATE Qt6::Quick Qt6::Gui Qt6::Core Qt6::Network)
add_test(NAME player-runtime COMMAND player-runtime-test)
set_tests_properties(player-runtime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# 可选离屏绘制微基准，不计作播放帧率，也不加入有耗时阈值的 CTest。
add_executable(danmaku-paint-bench EXCLUDE_FROM_ALL ../tools/danmaku_paint_bench.cpp)
target_link_libraries(danmaku-paint-bench PRIVATE Qt6::Gui Qt6::Core)
bbhouse_copy_mpv_dll(player-runtime-test)

add_executable(danmaku-layout-test EXCLUDE_FROM_ALL ../tools/danmaku_layout_test.cpp
        player/DanmakuLayout.h player/DanmakuLayout.cpp)
target_include_directories(danmaku-layout-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(danmaku-layout-test PRIVATE Qt6::Gui)
add_test(NAME danmaku-layout COMMAND danmaku-layout-test)
set_tests_properties(danmaku-layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(screenshot-test EXCLUDE_FROM_ALL ../tools/screenshot_test.cpp
        ../tools/screenshot_compression_test.cpp
        player/ScreenshotWriter.h player/ScreenshotWriter.cpp
        player/MpvClient.h player/MpvClient.cpp player/MpvLib.h player/MpvLib.cpp)
target_include_directories(screenshot-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/3rd/mpv/include)
target_link_libraries(screenshot-test PRIVATE Qt6::Gui)
add_test(NAME screenshot COMMAND screenshot-test)
set_tests_properties(screenshot PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
bbhouse_copy_mpv_dll(screenshot-test)
if(APPLE)
    target_sources(screenshot-test PRIVATE ../tools/screenshot_macos_test.cpp)
    target_link_libraries(screenshot-test PRIVATE Qt6::OpenGL)
    configure_file(../tools/fixtures/screenshot-red-h264.mp4
                   ${CMAKE_CURRENT_BINARY_DIR}/screenshot-red-h264.mp4 COPYONLY)
    target_compile_definitions(screenshot-test PRIVATE
        BBHOUSE_SCREENSHOT_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/screenshot-red-h264.mp4")
    add_test(NAME screenshot-macos COMMAND screenshot-test --macos-opengl)
    set_tests_properties(screenshot-macos PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa" TIMEOUT 30)
endif()

add_executable(danmaku-scene-test EXCLUDE_FROM_ALL ../tools/danmaku_scene_test.cpp
        player/DanmakuFilter.h player/DanmakuFilter.cpp
        player/DanmakuEngine.h player/DanmakuEngine.cpp
        player/DanmakuSpriteLayout.h player/DanmakuSpriteLayout.cpp
        player/DanmakuLayout.h player/DanmakuLayout.cpp)
target_include_directories(danmaku-scene-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(danmaku-scene-test PRIVATE Qt6::Quick Qt6::OpenGL)
add_test(NAME danmaku-scene COMMAND danmaku-scene-test)
set_tests_properties(danmaku-scene PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

add_executable(danmaku-sprite-test EXCLUDE_FROM_ALL ../tools/danmaku_sprite_test.cpp
        player/DanmakuSpriteLayout.h player/DanmakuSpriteLayout.cpp
        player/DanmakuLayout.h player/DanmakuLayout.cpp)
target_include_directories(danmaku-sprite-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(danmaku-sprite-test PRIVATE Qt6::Gui)
add_test(NAME danmaku-sprite COMMAND danmaku-sprite-test)
set_tests_properties(danmaku-sprite PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(preferences-test EXCLUDE_FROM_ALL ../tools/preferences_test.cpp
        preferences/AppPreferences.h preferences/AppPreferences.cpp)
target_include_directories(preferences-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(preferences-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME preferences COMMAND preferences-test)

add_executable(danmaku-filter-test EXCLUDE_FROM_ALL ../tools/danmaku_filter_test.cpp
        player/DanmakuFilter.h player/DanmakuFilter.cpp)
target_include_directories(danmaku-filter-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(danmaku-filter-test PRIVATE Qt6::Core)
add_test(NAME danmaku-filter COMMAND danmaku-filter-test)

add_executable(dynamics-model-test EXCLUDE_FROM_ALL ../tools/dynamics_model_test.cpp
        controllers/DynamicCardModel.h controllers/DynamicCardModel.cpp)
target_include_directories(dynamics-model-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dynamics-model-test PRIVATE Qt6::Quick)
add_test(NAME dynamics-model COMMAND dynamics-model-test)
set_tests_properties(dynamics-model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

add_executable(dynamics-controller-test EXCLUDE_FROM_ALL ../tools/dynamics_controller_test.cpp
        controllers/DynamicCardModel.h controllers/DynamicCardModel.cpp
        controllers/DynamicsController.h controllers/DynamicsController.cpp
        core/DynamicApi.h core/DynamicApi.cpp core/VideoZoneApi.h core/VideoZoneApi.cpp
        core/VideoZones.h core/VideoZones.cpp core/AppPaths.h core/AppPaths.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp)
target_include_directories(dynamics-controller-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dynamics-controller-test PRIVATE Qt6::Qml Qt6::Network)
add_test(NAME dynamics-controller COMMAND dynamics-controller-test)

add_executable(dynamics-page-test EXCLUDE_FROM_ALL ../tools/dynamics_page_test.cpp
        controllers/DynamicCardModel.h controllers/DynamicCardModel.cpp
        controllers/DynamicsController.h controllers/DynamicsController.cpp
        core/DynamicApi.h core/DynamicApi.cpp core/VideoZoneApi.h core/VideoZoneApi.cpp
        core/VideoZones.h core/VideoZones.cpp core/AppPaths.h core/AppPaths.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp)
target_include_directories(dynamics-page-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dynamics-page-test PRIVATE Qt6::Quick Qt6::Widgets Qt6::Network fluentuiplugin)
target_compile_definitions(dynamics-page-test PRIVATE DYNAMICS_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME dynamics-page COMMAND dynamics-page-test)

add_executable(online-history-page-test EXCLUDE_FROM_ALL ../tools/online_history_page_test.cpp)
target_link_libraries(online-history-page-test PRIVATE Qt6::Quick Qt6::Widgets fluentuiplugin)
target_compile_definitions(online-history-page-test PRIVATE ONLINE_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME online-history-page COMMAND online-history-page-test)

add_executable(page-state-restore-test EXCLUDE_FROM_ALL ../tools/page_state_restore_test.cpp)
target_link_libraries(page-state-restore-test PRIVATE Qt6::Quick Qt6::Widgets fluentuiplugin)
target_compile_definitions(page-state-restore-test PRIVATE PAGE_STATE_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME page-state-restore COMMAND page-state-restore-test)

add_executable(special-follow-page-test EXCLUDE_FROM_ALL ../tools/special_follow_page_test.cpp
        controllers/SpecialFollowController.h controllers/SpecialFollowController.cpp
        controllers/SpecialFollowStore.h controllers/SpecialFollowStore.cpp
        core/ArcSearchApi.h core/ArcSearchApi.cpp
        core/ArticleApi.h core/ArticleApi.cpp core/SeasonsApi.h core/SeasonsApi.cpp
        core/RelationApi.h core/RelationApi.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/AppPaths.h core/AppPaths.cpp core/WbiSigner.h core/WbiSigner.cpp)
target_include_directories(special-follow-page-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(special-follow-page-test PRIVATE Qt6::Quick Qt6::Widgets Qt6::Network fluentuiplugin)
target_compile_definitions(special-follow-page-test PRIVATE SPECIAL_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME special-follow-page COMMAND special-follow-page-test)

add_executable(online-history-controller-test EXCLUDE_FROM_ALL ../tools/online_history_controller_test.cpp
        controllers/OnlineHistoryController.h controllers/OnlineHistoryController.cpp
        core/HistoryApi.h core/HistoryApi.cpp core/AppPaths.h core/AppPaths.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp)
target_include_directories(online-history-controller-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(online-history-controller-test PRIVATE Qt6::Qml Qt6::Network)
add_test(NAME online-history-controller COMMAND online-history-controller-test)

# 离屏信号/状态回归，不代替页面 UI/UX 手测。
add_executable(page-search-test EXCLUDE_FROM_ALL ../tools/page_search_test.cpp)
target_link_libraries(page-search-test PRIVATE Qt6::Quick Qt6::Widgets fluentuiplugin)
target_compile_definitions(page-search-test PRIVATE
        SEARCHBOX_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml/controls/PageSearchBox.qml")
add_test(NAME page-search COMMAND page-search-test)

# 头像缓存离线 HTTP fixture，数据仅写入测试临时目录。
add_executable(avatar-cache-test EXCLUDE_FROM_ALL ../tools/avatar_cache_test.cpp
        core/AvatarCache.h core/AvatarCache.cpp core/AvatarSource.h core/AvatarSource.cpp
        core/AppPaths.h core/AppPaths.cpp)
target_include_directories(avatar-cache-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(avatar-cache-test PRIVATE Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick)
add_test(NAME avatar-cache COMMAND avatar-cache-test)
set_tests_properties(avatar-cache PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# 假原生后端验证命令派发/会话失效/状态，不占用真实系统媒体控制。
add_executable(system-media-test EXCLUDE_FROM_ALL ../tools/system_media_test.cpp
        player/SystemMediaBackend.h player/SystemMediaControls.h player/SystemMediaControls.cpp
        player/SystemMediaBackend_stub.cpp)
target_include_directories(system-media-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(system-media-test PRIVATE Qt6::Core)
add_test(NAME system-media COMMAND system-media-test)

# 本地 HTTP fixture 验证封面异步加载、切播和关闭后旧回应失效。
add_executable(system-media-artwork-test EXCLUDE_FROM_ALL ../tools/system_media_artwork_test.cpp
        player/SystemMediaArtwork.h player/SystemMediaArtwork.cpp)
target_include_directories(system-media-artwork-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(system-media-artwork-test PRIVATE Qt6::Gui Qt6::Network)
add_test(NAME system-media-artwork COMMAND system-media-artwork-test)
set_tests_properties(system-media-artwork PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(user-space-test EXCLUDE_FROM_ALL ../tools/user_space_test.cpp
        controllers/UserSpaceController.h controllers/UserSpaceController.cpp
        controllers/SpecialFollowController.h controllers/SpecialFollowController.cpp
        controllers/SpecialFollowStore.h controllers/SpecialFollowStore.cpp
        core/UserProfileApi.h core/UserProfileApi.cpp
        core/ArcSearchApi.h core/ArcSearchApi.cpp
        core/ArticleApi.h core/ArticleApi.cpp
        core/SeasonsApi.h core/SeasonsApi.cpp
        core/RelationApi.h core/RelationApi.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/AppPaths.h core/AppPaths.cpp
        core/WbiSigner.h core/WbiSigner.cpp)
target_include_directories(user-space-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(user-space-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME user-space COMMAND user-space-test)

add_executable(card-author-test EXCLUDE_FROM_ALL ../tools/card_author_test.cpp)
target_link_libraries(card-author-test PRIVATE Qt6::Quick Qt6::Widgets fluentuiplugin)
target_compile_definitions(card-author-test PRIVATE
        CARD_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml/controls/HistoryCard.qml")
add_test(NAME card-author COMMAND card-author-test)

add_executable(api-proxy-test EXCLUDE_FROM_ALL ../tools/api_proxy_test.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp)
target_include_directories(api-proxy-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(api-proxy-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME api-proxy COMMAND api-proxy-test)
set_tests_properties(api-proxy PROPERTIES TIMEOUT 15)

add_executable(regional-bangumi-test EXCLUDE_FROM_ALL ../tools/regional_bangumi_test.cpp
        controllers/BangumiController.h controllers/BangumiController.cpp
        core/BangumiApi.h core/BangumiApi.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/AppPaths.h core/AppPaths.cpp
        preferences/AppPreferences.h preferences/AppPreferences.cpp)
target_include_directories(regional-bangumi-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(regional-bangumi-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME regional-bangumi COMMAND regional-bangumi-test)

# 直播解析与分页会话回归：离线数据，不访问真实用户记录。
add_executable(live-api-test EXCLUDE_FROM_ALL ../tools/live_api_test.cpp
        core/LiveApi.h core/LiveApi.cpp core/BilibiliApiClient.h core/BilibiliApiClient.cpp)
target_include_directories(live-api-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(live-api-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME live-api COMMAND live-api-test)

add_executable(live-controller-test EXCLUDE_FROM_ALL ../tools/live_controller_test.cpp
        controllers/LiveController.h controllers/LiveController.cpp
        core/LiveApi.h core/LiveApi.cpp core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/AppPaths.h core/AppPaths.cpp)
target_include_directories(live-controller-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(live-controller-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME live-controller COMMAND live-controller-test)

add_executable(live-player-test EXCLUDE_FROM_ALL ../tools/live_player_test.cpp
        player/LivePlayerController.h player/LivePlayerController.cpp
        player/MpvVideoItem.h player/MpvVideoItem.cpp
        player/MpvClient.h player/MpvClient.cpp player/MpvLib.h player/MpvLib.cpp
        core/LiveApi.h core/LiveApi.cpp core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/AppPaths.h core/AppPaths.cpp)
target_include_directories(live-player-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/3rd/mpv/include)
target_link_libraries(live-player-test PRIVATE Qt6::Quick Qt6::OpenGL Qt6::Network)
add_test(NAME live-player COMMAND live-player-test)
set_tests_properties(live-player PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
bbhouse_copy_mpv_dll(live-player-test)

add_executable(popular-api-test EXCLUDE_FROM_ALL ../tools/popular_api_test.cpp
        core/PopularApi.h core/PopularApi.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp core/WbiSigner.h core/WbiSigner.cpp)
target_include_directories(popular-api-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(popular-api-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME popular-api COMMAND popular-api-test)

add_executable(popular-controller-test EXCLUDE_FROM_ALL ../tools/popular_controller_test.cpp
        controllers/PopularController.h controllers/PopularController.cpp
        core/PopularApi.h core/PopularApi.cpp core/AppPaths.h core/AppPaths.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp core/WbiSigner.h core/WbiSigner.cpp)
target_include_directories(popular-controller-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(popular-controller-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME popular-controller COMMAND popular-controller-test)

add_executable(popular-page-test EXCLUDE_FROM_ALL ../tools/popular_page_test.cpp
        controllers/PopularController.h controllers/PopularController.cpp
        core/PopularApi.h core/PopularApi.cpp core/AppPaths.h core/AppPaths.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp core/WbiSigner.h core/WbiSigner.cpp)
target_include_directories(popular-page-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(popular-page-test PRIVATE Qt6::Quick Qt6::Widgets Qt6::Network fluentuiplugin)
target_compile_definitions(popular-page-test PRIVATE POPULAR_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME popular-page COMMAND popular-page-test)

add_executable(player-controls-test EXCLUDE_FROM_ALL ../tools/player_controls_test.cpp)
target_link_libraries(player-controls-test PRIVATE Qt6::Quick Qt6::Widgets fluentuiplugin)
target_compile_definitions(player-controls-test PRIVATE PLAYER_CONTROLS_QML="${CMAKE_CURRENT_SOURCE_DIR}/qml/controls/PlayerControls.qml")
add_test(NAME player-controls COMMAND player-controls-test)

# 本地历史/调度回归：仅离线 fixture、隔离数据库和命令替身。
add_executable(history-scheduler-test EXCLUDE_FROM_ALL ../tools/history_scheduler_test.cpp
        core/HistoryScheduler.h core/HistoryScheduler.cpp)
target_include_directories(history-scheduler-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(history-scheduler-test PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(history-scheduler-test PRIVATE shell32 ole32)
endif()
add_test(NAME history-scheduler COMMAND history-scheduler-test)

add_executable(history-sync-test EXCLUDE_FROM_ALL ../tools/history_sync_test.cpp
        core/HistorySyncRunner.h core/HistorySyncRunner.cpp core/HistoryStore.h core/HistoryStore.cpp
        core/HistoryApi.h core/HistoryApi.cpp core/BilibiliApiClient.h core/BilibiliApiClient.cpp)
target_include_directories(history-sync-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(history-sync-test PRIVATE Qt6::Core Qt6::Network Qt6::Sql)
add_test(NAME history-sync COMMAND history-sync-test)

add_executable(history-service-entry-test EXCLUDE_FROM_ALL ../tools/history_service_entry_test.cpp
        core/HistoryScheduler.h core/HistoryScheduler.cpp core/HistoryStore.h core/HistoryStore.cpp)
target_include_directories(history-service-entry-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(history-service-entry-test PRIVATE Qt6::Core Qt6::Sql)
if(WIN32)
    target_link_libraries(history-service-entry-test PRIVATE shell32 ole32)
endif()
target_compile_definitions(history-service-entry-test PRIVATE HISTORY_APP_PATH="$<TARGET_FILE:bbhouse-qt>")
add_dependencies(history-service-entry-test bbhouse-qt)
add_test(NAME history-service-entry COMMAND history-service-entry-test)

add_executable(history-controller-test EXCLUDE_FROM_ALL ../tools/history_controller_test.cpp
        controllers/HistoryController.h controllers/HistoryController.cpp
        core/HistorySyncRunner.h core/HistorySyncRunner.cpp core/HistoryStore.h core/HistoryStore.cpp
        core/HistoryApi.h core/HistoryApi.cpp core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/AppPaths.h core/AppPaths.cpp)
target_include_directories(history-controller-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(history-controller-test PRIVATE Qt6::Core Qt6::Network Qt6::Sql)
add_test(NAME history-controller COMMAND history-controller-test)

add_executable(history-service-worker-test EXCLUDE_FROM_ALL ../tools/history_service_worker_test.cpp
        core/HistoryServiceWorker.h core/HistoryServiceWorker.cpp core/HistoryServiceSchedule.h
        core/HistoryScheduler.h core/HistoryScheduler.cpp
        core/HistorySyncRunner.h core/HistorySyncRunner.cpp core/HistoryStore.h core/HistoryStore.cpp
        core/HistoryApi.h core/HistoryApi.cpp core/BilibiliApiClient.h core/BilibiliApiClient.cpp)
target_include_directories(history-service-worker-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(history-service-worker-test PRIVATE Qt6::Core Qt6::Network Qt6::Sql)
if(WIN32)
    target_link_libraries(history-service-worker-test PRIVATE shell32 ole32)
endif()
add_test(NAME history-service-worker COMMAND history-service-worker-test)

# 本地播放：隔离 Cookie/数据路径，真实 libmpv，不访问在线 API。
add_executable(local-player-test EXCLUDE_FROM_ALL ../tools/local_player_test.cpp
        player/PlayerController.h player/PlayerController.cpp
        player/MpvVideoItem.h player/MpvVideoItem.cpp
        player/MpvClient.h player/MpvClient.cpp player/MpvLib.h player/MpvLib.cpp
        player/DanmakuEngine.h player/DanmakuEngine.cpp
        player/DanmakuFilter.h player/DanmakuFilter.cpp
        player/DanmakuLayout.h player/DanmakuLayout.cpp
        player/DanmakuSpriteLayout.h player/DanmakuSpriteLayout.cpp
        player/ScreenshotWriter.h player/ScreenshotWriter.cpp
        player/SystemMediaControls.h player/SystemMediaControls.cpp player/SystemMediaBackend_stub.cpp
        preferences/AppPreferences.h preferences/AppPreferences.cpp
        core/HistoryStore.h core/HistoryStore.cpp
        core/PlayerApi.h core/PlayerApi.cpp core/HeartbeatApi.h core/HeartbeatApi.cpp
        core/BilibiliApiClient.h core/BilibiliApiClient.cpp core/WbiSigner.h core/WbiSigner.cpp
        core/DanmakuParser.h core/DanmakuParser.cpp)
target_include_directories(local-player-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/3rd/mpv/include)
target_link_libraries(local-player-test PRIVATE Qt6::Quick Qt6::OpenGL Qt6::Network Qt6::Sql)
if(NOT WIN32)
    target_link_libraries(local-player-test PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME local-player COMMAND local-player-test)
set_tests_properties(local-player PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "requires-libmpv")
bbhouse_copy_mpv_dll(local-player-test)
if(APPLE OR WIN32)
    target_sources(local-player-test PRIVATE ../tools/local_player_render_test.cpp)
    configure_file(../tools/fixtures/screenshot-red-h264.mp4
                   ${CMAKE_CURRENT_BINARY_DIR}/screenshot-red-h264.mp4 COPYONLY)
    target_compile_definitions(local-player-test PRIVATE
        BBHOUSE_LOCAL_VIDEO_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/screenshot-red-h264.mp4")
    if(APPLE)
        add_test(NAME local-player-macos COMMAND local-player-test --macos-opengl)
        set_tests_properties(local-player-macos PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=cocoa" LABELS "requires-libmpv;requires-gui" TIMEOUT 30)
    else()
        add_test(NAME local-player-windows COMMAND local-player-test --windows-opengl)
        set_tests_properties(local-player-windows PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=rhi" LABELS "requires-libmpv;requires-gui" TIMEOUT 30)
    endif()
endif()

add_executable(download-page-test EXCLUDE_FROM_ALL ../tools/download_page_test.cpp)
target_link_libraries(download-page-test PRIVATE Qt6::Quick Qt6::Widgets Qt6::Network fluentuiplugin)
target_compile_definitions(download-page-test PRIVATE DOWNLOAD_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml")
add_test(NAME download-page COMMAND download-page-test)

add_executable(download-test EXCLUDE_FROM_ALL ../tools/download_test.cpp ../tools/download_management_test.cpp
        downloads/DownloadController.h downloads/DownloadController.cpp
        downloads/DownloadCover.h downloads/DownloadCover.cpp
        downloads/DownloadStore.h downloads/DownloadStore.cpp
        downloads/DownloadUtils.h downloads/DownloadUtils.cpp
        downloads/DownloadWorker.h downloads/DownloadWorker.cpp
        core/AppPaths.h core/AppPaths.cpp core/BilibiliApiClient.h core/BilibiliApiClient.cpp
        core/PlayerApi.h core/PlayerApi.cpp core/WbiSigner.h core/WbiSigner.cpp
        preferences/AppPreferences.h preferences/AppPreferences.cpp)
target_include_directories(download-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(download-test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Concurrent)
if(NOT WIN32)
    target_link_libraries(download-test PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME download COMMAND download-test)

add_executable(download-cover-test EXCLUDE_FROM_ALL ../tools/download_cover_test.cpp
        downloads/DownloadCover.h downloads/DownloadCover.cpp
        downloads/DownloadUtils.h downloads/DownloadUtils.cpp)
target_include_directories(download-cover-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(download-cover-test PRIVATE Qt6::Core Qt6::Gui)
if(NOT WIN32)
    target_link_libraries(download-cover-test PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME download-cover COMMAND download-cover-test)
set_tests_properties(download-cover PROPERTIES SKIP_RETURN_CODE 77)

add_executable(login-cookie-test EXCLUDE_FROM_ALL ../tools/login_cookie_test.cpp core/LoginCookies.cpp)
target_include_directories(login-cookie-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(login-cookie-test PRIVATE Qt6::Core Qt6::Network)
add_test(NAME login-cookie COMMAND login-cookie-test)

add_executable(onboarding-page-test EXCLUDE_FROM_ALL ../tools/onboarding_page_test.cpp)
bbhouse_add_icon_resources(onboarding-page-test)
target_link_libraries(onboarding-page-test PRIVATE Qt6::Quick Qt6::Widgets fluentuiplugin)
target_compile_definitions(onboarding-page-test PRIVATE ONBOARDING_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml/pages")
add_test(NAME onboarding-page COMMAND onboarding-page-test)

add_executable(login-page-test EXCLUDE_FROM_ALL ../tools/login_page_test.cpp
        controllers/LoginController.h controllers/LoginController.cpp core/LoginCookies.cpp)
target_include_directories(login-page-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(login-page-test PRIVATE LOGIN_QML_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_link_libraries(login-page-test PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Network)
add_dependencies(login-page-test fluentuiplugin)
add_test(NAME login-page COMMAND login-page-test)

# Exercise the real QML Loader lifetime without network or user data.
add_executable(page-lifecycle-test EXCLUDE_FROM_ALL ../tools/page_lifecycle_test.cpp)
target_link_libraries(page-lifecycle-test PRIVATE Qt6::Quick)
target_compile_definitions(page-lifecycle-test PRIVATE
        PAGE_LOADER_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml/controls/ExpiringPageLoader.qml")
add_test(NAME page-lifecycle COMMAND page-lifecycle-test)

add_executable(fluent-ownership-test EXCLUDE_FROM_ALL ../tools/fluent_ownership_test.cpp
        ../3rd/FluentUI/FluTreeModel.h ../3rd/FluentUI/FluTreeModel.cpp)
target_include_directories(fluent-ownership-test PRIVATE ../3rd/FluentUI)
target_link_libraries(fluent-ownership-test PRIVATE Qt6::Core Qt6::Qml)
add_test(NAME fluent-ownership COMMAND fluent-ownership-test)

# CI 构建所有已登记的回归程序，避免新增测试后漏改各平台工作流。
include(SystemMediaWindowsTest.cmake)
add_custom_target(regression-tests)
get_property(_bbhouse_app_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach (_bbhouse_test_target IN LISTS _bbhouse_app_targets)
    if (_bbhouse_test_target MATCHES "-test$")
        add_dependencies(regression-tests ${_bbhouse_test_target})
    endif ()
endforeach ()

# 编译不需要 libmpv，但这些回归必须加载真实内核。缺内核的 CI 要显式排除，
# 不应把缺依赖返回成功而伪装成运行通过。
set_tests_properties(player-runtime screenshot live-player PROPERTIES LABELS "requires-libmpv")
if (APPLE)
    set_tests_properties(screenshot-macos PROPERTIES LABELS "requires-libmpv;requires-gui")
endif ()

# 应用显示版本与项目/发布包保持一致。
target_compile_definitions(bbhouse-qt PRIVATE BBHOUSE_APP_VERSION="${PROJECT_VERSION}")
