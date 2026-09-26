cmake_minimum_required(VERSION 3.21)

project(bbhouse-qt VERSION 2.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 所有可执行文件与 DLL 统一输出到 build/bin,FluentUI QML 模块输出到 build/bin/FluentUI
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 源码含中文注释与字面量,MSVC 默认按本地代码页(936)读取会报 C4819 并可能把中文读错
if (MSVC)
    add_compile_options(/utf-8)
endif()

# 本机默认 Qt 路径:调用方显式传入(kit 的 CMAKE_PREFIX_PATH / Qt6_DIR / QT_QMAKE_EXECUTABLE)
# 优先,否则在 C:/Qt(Windows)或 $HOME/Qt(macOS)下自动挑一个套件。
# 必须按编译器挑套件:Windows 的 C:/Qt 可能同时装了 mingw_64 与 msvc2022_64,
# 给 MSVC 用 MinGW 版 Qt 会在 Qt 头文件里报 C1189(__cplusplus),链接期也找不到 .lib;
# 反之 MinGW 用 MSVC 版 Qt 会在链接期找不到符号。按版本倒序挑,升级 Qt 无需改 CMake。
if (NOT CMAKE_PREFIX_PATH AND NOT Qt6_DIR AND NOT QT_DIR AND NOT QT_QMAKE_EXECUTABLE)
    set(_bbhouse_qt_found FALSE)
    if (WIN32)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(_bbhouse_qt_kits mingw_64 mingw)
        elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(_bbhouse_qt_kits msvc2026_64 msvc2022_64 msvc2019_64 msvc_64)
        else ()
            set(_bbhouse_qt_kits mingw_64 msvc2022_64)
        endif ()
        file(GLOB _bbhouse_qt_roots LIST_DIRECTORIES TRUE "C:/Qt/*")
    elseif (APPLE)
        set(_bbhouse_qt_kits macos)
        file(GLOB _bbhouse_qt_roots LIST_DIRECTORIES TRUE "$ENV{HOME}/Qt/*")
    else ()
        set(_bbhouse_qt_roots "")
    endif ()
    list(SORT _bbhouse_qt_roots COMPARE NATURAL ORDER DESCENDING)
    foreach (_bbhouse_qt_root IN LISTS _bbhouse_qt_roots)
        foreach (_bbhouse_qt_kit IN LISTS _bbhouse_qt_kits)
            if (EXISTS "${_bbhouse_qt_root}/${_bbhouse_qt_kit}/lib/cmake/Qt6/Qt6Config.cmake")
                list(APPEND CMAKE_PREFIX_PATH "${_bbhouse_qt_root}/${_bbhouse_qt_kit}")
                message(STATUS "bbhouse-qt: 自动选择 Qt 套件 ${_bbhouse_qt_root}/${_bbhouse_qt_kit}"
                        "(编译器 ${CMAKE_CXX_COMPILER_ID})")
                set(_bbhouse_qt_found TRUE)
                break()
            endif ()
        endforeach ()
        if (_bbhouse_qt_found)
            break()
        endif ()
    endforeach ()
    unset(_bbhouse_qt_found)
endif ()

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Quick Qml Widgets Network Sql PrintSupport OpenGL Concurrent)

# 套件与编译器不配套时给出可操作的报错,而不是让错误发生在 Qt 头文件/链接期
if (WIN32 AND DEFINED Qt6_DIR)
    string(TOLOWER "${Qt6_DIR}" _bbhouse_qt6_dir_lower)
    if (_bbhouse_qt6_dir_lower MATCHES "/mingw")
        set(_bbhouse_qt6_kit "MinGW")
    elseif (_bbhouse_qt6_dir_lower MATCHES "/msvc")
        set(_bbhouse_qt6_kit "MSVC")
    else ()
        set(_bbhouse_qt6_kit "")
    endif ()
    if (_bbhouse_qt6_kit STREQUAL "MinGW" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Qt 套件与编译器不配套:\n"
                "  编译器 : ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER})\n"
                "  Qt     : MinGW 版 (${Qt6_DIR})\n"
                "MinGW 编译出的 Qt 只能配 MinGW 编译器,MSVC(cl.exe)必须配 MSVC 版 Qt。\n"
                "处理办法(任选其一):\n"
                "  1) 在 Qt Creator 里改用与编译器一致的套件(本机推荐 \"Desktop Qt 6.11.1 MinGW 64-bit\");\n"
                "  2) 显式传 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64(或对应的 msvc* 目录);\n"
                "  3) 删除构建目录重新配置,让 CMake 按编译器自动挑套件。")
    elseif (_bbhouse_qt6_kit STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Qt 套件与编译器不配套:\n"
                "  编译器 : ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER})\n"
                "  Qt     : MSVC 版 (${Qt6_DIR})\n"
                "MSVC 编译出的 Qt 只能配 MSVC(cl.exe),MinGW 编译器必须配 MinGW 版 Qt。\n"
                "处理办法(任选其一):\n"
                "  1) 在 Qt Creator 里改用与编译器一致的套件(本机推荐 \"Desktop Qt 6.11.1 MinGW 64-bit\");\n"
                "  2) 显式传 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64;\n"
                "  3) 删除构建目录重新配置,让 CMake 按编译器自动挑套件。")
    endif ()
endif ()

if (Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()
include(CTest)

# FluentUI QML 模块输出重定向到本仓库构建目录(不写入 Qt 安装目录)
set(FLUENTUI_QML_PLUGIN_DIRECTORY "${CMAKE_BINARY_DIR}/bin/FluentUI" CACHE PATH "Path to FluentUI plugin")
add_subdirectory(3rd/FluentUI)

# FluentUI 插件 DLL 与 qmldir 同目录,供 QML 引擎按模块目录解析插件
set_target_properties(fluentuiplugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/FluentUI
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/FluentUI
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/FluentUI
)

add_subdirectory(app)
