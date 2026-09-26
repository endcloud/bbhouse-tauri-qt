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

### Requirement: 全平台应用图标
应用 SHALL 使用项目内用户提供的图标，PNG 资源 SHALL 内嵌到程序；Qt 应用图标、FluentUI 默认窗口图标及关于页 SHALL 使用同一品牌图形，macOS 选择透明圆角 PNG；PNG 和 ICNS 各尺寸 SHALL 保持约 80% 的居中主体与透明外围留白，使 Dock 中的视觉大小接近系统应用。无头任务入口 MUST NOT 为图标初始化 GUI。

#### Scenario: 运行时图标
- **WHEN** 用户从任意工作目录启动 GUI 并进入关于页
- **THEN** 应用及关于页读取内嵌图标，无需原始素材目录；现有主窗口隐藏标题图标的布局保留。

### Requirement: 平台图标分发
Windows 主程序及原生服务宿主 SHALL 嵌入多尺寸 ICO；macOS .app SHALL 包含 ICNS，Info.plist 的 CFBundleIconFile SHALL 指向它，打包验证 SHALL 检查文件完整及来源一致。Linux SHALL 提供 desktop 入口与 hicolor PNG 的 DesktopIntegration 安装组件，Qt desktop ID SHALL 匹配入口名。

#### Scenario: Windows 可执行文件
- **WHEN** 使用 MinGW 或 MSVC 构建并分发可执行文件
- **THEN** 图标已作为原生资源包含在 exe 中，不依赖外置 ICO。

#### Scenario: macOS 应用包
- **WHEN** 运行 macOS 打包脚本
- **THEN** 图标在签名前写入 Resources，并在原包及迁移检查中验证；缺失或不一致时阻止交付。

#### Scenario: Linux 桌面集成
- **WHEN** 安装 DesktopIntegration 组件且 bbhouse-qt 已在 PATH 中可运行
- **THEN** desktop 入口以 bbhouse-qt 启动，Icon 名称解析到安装的 hicolor 图标，并与 Qt desktop ID 对应。

#### Scenario: macOS Dock 视觉大小
- **WHEN** 用户在 Dock 中将本应用与系统应用并排显示
- **THEN** 图标主体不铺满画布；1024 像素母版主体为 824 像素、四边各 100 像素透明留白，ICNS 各尺寸保持相同比例。

### Requirement: 应用品牌与版本一致性
应用全局显示名称 SHALL 为 BBHouse，中英文主窗口、关于页及平台应用显示名 SHALL 保持一致。版本 SHALL 从 CMake 项目声明读取，当前为 2.0.2，并同步 Qt 应用版本、Windows 原生产品版本与 macOS 包版本。内部应用标识和现有数据路径 MUST 保持兼容，名称变化 MUST NOT 导致旧 Cookie、数据库或偏好失联。

#### Scenario: 显示名称和版本
- **WHEN** 用户启动任一语言版本并打开关于页
- **THEN** 主窗口及关于页显示 BBHouse，版本和复制信息显示 2.0.2，原有数据继续可读。

#### Scenario: 原生发行元数据
- **WHEN** 构建 Windows 程序或执行 macOS 打包
- **THEN** 原生产品名称为 BBHouse、版本为 2.0.2，macOS 应用包为 BBHouse.app；内部可执行文件名及 bundle ID 保持兼容。
