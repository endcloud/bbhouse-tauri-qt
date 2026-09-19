# Proposal: fix-windows-qt-kit-selection

Windows 编译修复:CMake 按编译器挑选 Qt 套件,并补齐 Windows 的 libmpv 运行时产物。

## Why

项目双平台开发(Windows Qt 6.11.1 mingw / macOS Qt 6.11.2)。macOS 侧已由 `fix-macos-port-and-runtime-bugs` 打通;Windows 侧从 `f7846d6` 起把 Qt 前缀硬编码为 `C:/Qt/6.11.1/mingw_64`,由此产生两类 Windows 专属编译失败:

1. **编译器不是 MinGW**:MSVC 拿到 MinGW 版 Qt 后,`cl.exe` 直接崩在 Qt 头文件 —— 实测 `C:\Qt\6.11.1\mingw_64\include\QtCore\qcompilerdetection.h(1331): fatal error C1189: "Qt requires a C++17 compiler..."`,链接期还会去找不存在的 `.lib`。本机同时装有 VS 2026 Community 与 `C:/Qt/6.11.1/msvc2022_64`,踩中概率高。
2. **Qt 版本升级**:与 macOS 对齐到 6.11.2 后 `6.11.1` 目录不存在(或换机器),CMake 只会报"找不到 Qt6",与真实原因无关。

另有一处 Windows 与 macOS 的行为差:Windows 的 `libmpv-2.dll` 只在 exe 同级目录/系统路径搜索,而该 DLL 按 GPL 单独分发、不入库,导致 `player-runtime` 测试在 Windows 上必然加载失败(macOS 会命中 Homebrew 路径)。

## What Changes

1. `CMakeLists.txt`:调用方显式传入的 `CMAKE_PREFIX_PATH` / `Qt6_DIR` / `QT_QMAKE_EXECUTABLE`(Qt Creator kit 会传)优先;否则扫描 `C:/Qt/*`(macOS 为 `$HOME/Qt/*`),按编译器套件(GNU→`mingw_64`/`mingw`,MSVC 或 Clang→`msvc2026_64`/`msvc2022_64`/…)与版本倒序挑一个含 `Qt6Config.cmake` 的套件。
2. `CMakeLists.txt`:`find_package(Qt6)` 之后校验套件与编译器是否配套,不配套即 `FATAL_ERROR`,并给出"改用匹配套件 / 显式传 `-DCMAKE_PREFIX_PATH` / 删构建目录重新配置"三条处理办法,替代原先发生在 Qt 头文件里的隐晦报错。
3. `CMakeLists.txt`:MSVC 增加 `/utf-8`。源码含中文注释与字面量,MSVC 默认按 936 代码页读取会报 C4819 并可能误读中文。
4. `app/CMakeLists.txt`:Windows 下把 `3rd/mpv/libmpv-2.dll`(存在时)复制到 `bbhouse-qt` 与 `player-runtime-test` 的输出目录;缺失时 configure 阶段给一次警告,不阻断编译。
5. 新增 `CMakePresets.json`:固化 Windows MinGW(`C:/Qt/Tools/mingw1310_64` + `C:/Qt/6.11.1/mingw_64`)、Windows MSVC(`cl.exe` + `C:/Qt/6.11.1/msvc2022_64`,需 VS 开发者环境)与 macOS(`$HOME/Qt/6.11.2/macos`)三套 configure 预设(含构建期 `PATH`,并把 app 与四个测试目标一起构建的 build/test 预设)。预设里的显式前缀优先级最高,不会被自动探测覆盖。

## Impact

- MinGW 路径行为不变:仍挑 `C:/Qt/6.11.1/mingw_64`,Qt Creator kit 传入的前缀依旧优先,`bbhouse-qt`/测试目标与产物布局不变。
- 新增 `/utf-8` 只作用于 MSVC,不影响 MinGW/macOS。
- 不改变任何规格契约(无 spec delta)。

## 验证

- MinGW/Ninja(不传 Qt 前缀,验证自动挑套件):configure 打印 `自动选择 Qt 套件 C:/Qt/6.11.1/mingw_64(编译器 GNU)`;`bbhouse-qt` + 四个测试目标全部链接成功(357/357);`ctest` 四组 100% 通过(修复前 `player-runtime` 因缺 `libmpv-2.dll` 必失败)。
- MSVC(VS 2026 Community 19.51,不传前缀):`Qt6_DIR = C:/Qt/6.11.1/msvc2022_64`(修复前是 `mingw_64`,报 `qcompilerdetection.h(1331): fatal error C1189`);`bbhouse-qt` 333/333 链接成功,且不再出现 C4819 中文代码页告警。
- 报错路径:MSVC + `-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64` 命中新增的 `FATAL_ERROR`,输出编译器/Qt 套件与三条处理办法(编译未开始即失败,不再落到 Qt 头文件)。
- 未覆盖:macOS 侧未重跑(改动只在无显式前缀时生效,且 macOS 分支沿用原有 `$HOME/Qt/*/macos` 探测);`build/deploy` 与真实播放未重跑。
- 预设实测:`cmake --preset windows-mingw`(+`--build`/`ctest --preset`)= 配置通过、357/357、ctest 4/4(干净 shell,无需手工改 PATH);`windows-msvc` 在 vs2026 vcvars64 环境下同样 357/357 且 ctest 4/4;`--list-presets` 在 Windows 上只列 Windows 预设(macOS 预设受 condition 隐藏)。
