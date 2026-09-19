# qt-app-scaffold Specification

## Purpose
TBD - created by archiving change setup-qt-project-skeleton. Update Purpose after archive.

## Requirements

### Requirement: 构建体系与仓库结构

系统 SHALL 以 CMake(≥3.21)+ Ninja 构建一个 Qt 6 桌面工程:根 `CMakeLists.txt` 聚合 `app/` 主目标与 `3rd/FluentUI` 子目录;所有第三方库 MUST 位于 `./3rd` 下。构建产物 MUST 包含可启动的 `bbhouse-qt.exe`。

#### Scenario: 全新机器配置构建

- **WHEN** 以 `-DCMAKE_PREFIX_PATH=<Qt>/mingw_64 -G Ninja` 配置并 `cmake --build build`
- **THEN** FluentUI 子目录与主工程全部编译成功(0 error),`build` 下产出 `bbhouse-qt.exe`

### Requirement: FluentUI 组件库集成

工程 SHALL 把 `./3rd/FluentUI` 的 C++ 源码编译为 QML 模块(URI `FluentUI`),其输出目录 MUST 位于本仓库构建目录内(不得写入 Qt 安装目录)。应用启动时 MUST 注册该模块的 QML import 路径,使任何 QML 文件的 `import FluentUI` 可解析。

#### Scenario: QML 引用 FluentUI 控件

- **WHEN** 主 QML 文件声明 `import FluentUI` 并实例化 `FluWindow`/`FluText`
- **THEN** offscreen 冒烟运行加载无 QML 错误(stderr 无 "module not installed" 或组件创建失败)

### Requirement: 应用入口与 FluApp 初始化

`main.cpp` SHALL 初始化 `FluApp`(`FluApp::init` / 注册 FluentUI 单例)、设置应用信息,并以主 QML 文件启动 QML 引擎。窗口默认尺寸、最小尺寸常量 MUST 集中定义,供后续 app-navigation-shell 使用。

#### Scenario: 启动进入主窗口

- **WHEN** 用户双击 exe 启动应用
- **THEN** 出现 FluentUI 风格主窗口,进程不崩溃

### Requirement: i18n 双语机制

全部用户可见文案 MUST 经 `qsTr()` 包裹且以 zh-CN 作为中性源语言;en-US 文案 MUST 经 Qt `.ts` 翻译文件维护,应用启动时按"语言偏好(跟随系统/简体中文/English)"加载对应 `.qm`(跟随系统时按系统语言链匹配)。语言偏好 MUST 持久化,本骨架阶段提供偏好存取接口(设置页在后继变更实现)。

#### Scenario: 英文系统自动切换

- **WHEN** 系统语言为 en-US 且语言偏好为"跟随系统"
- **THEN** 启动后界面文案加载 en-US 翻译

### Requirement: mpv 链接与部署形态

工程 SHALL 链接 `./3rd/mpv` 下的 libmpv(MinGW 导入库 `libmpv.dll.a`),运行时从 exe 同级目录加载 `libmpv-2.dll`。交付形态 MUST 为"可运行目录":exe + Qt 运行时 + FluentUI 模块 + libmpv-2.dll。

#### Scenario: 播放内核可用性探测

- **WHEN** 应用启动并查询 mpv 动态库加载结果
- **THEN** 在交付目录形态下 libmpv-2.dll 加载成功(探测接口供播放器变更复用)
