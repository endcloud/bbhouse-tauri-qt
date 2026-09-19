# Tasks: fix-windows-qt-kit-selection

- [x] 1.1 CMake:按编译器套件 + 版本倒序自动挑 Qt(GNU→mingw*,MSVC/Clang→msvc*;含 macOS $HOME/Qt/*)
- [x] 1.2 CMake:显式前缀/kit 优先,不让自动探测覆盖调用方
- [x] 1.3 CMake:套件与编译器不配套时 FATAL_ERROR + 三条处理办法
- [x] 1.4 CMake:MSVC 加 /utf-8(消除中文注释/字面量的 C4819 误读)
- [x] 2.1 app:Windows 复制 libmpv-2.dll 到 exe 与 player-runtime-test 输出目录;缺失时警告
- [x] 3.1 MinGW 全新构建(不传前缀,验证自动选 mingw_64)+ ctest 四组
- [x] 3.2 MSVC 全新构建(不传前缀,验证自动选 msvc2022_64 且不出现 C1189)
- [x] 3.3 强制不匹配(MSVC + mingw 前缀)时命中新的 FATAL_ERROR 文案
- [x] 3.4 README Windows 构建段落更新
- [x] 4.1 CMakePresets.json:windows-mingw / windows-msvc / macos 三套配置预设(含构建期 PATH 与 build/test 预设)
- [x] 4.2 预设实测:windows-mingw 干净 shell 配置+357/357 构建+ctest 4/4;windows-msvc 开发者终端配置+357/357 构建+ctest 4/4
