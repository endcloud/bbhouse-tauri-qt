# macOS Release 应用与 DMG 交付

更新：2026-09-19；OpenSpec：`package-macos-release`。本次为当前 Mac 的本地验收包，没有上传 GitHub Release，也没有改变先前保留的字体授权阻塞决定。

## 本次产物

目录：`build/release/bbhouse-qt-macos-arm64_20260919_161057/`。

| 文件 | 用途 | 体积 |
| --- | --- | ---: |
| `bbhouse-qt.app` | 可复制到 Applications 的独立应用，版本 1.0.1 | 176,110,925 字节，约 168.0 MiB（常规文件逻辑总量，不重复计符号链接） |
| `bbhouse-qt-1.0.1-macos-arm64.dmg` | 只读压缩 HFS+ 磁盘映像，包含应用、Applications 快捷入口及说明 | 69,671,328 字节，约 66.4 MiB |
| `使用说明.md` / `SHA256SUMS` | 安装说明与 DMG 校验值 | — |

DMG SHA-256：`8ca197255164f519856e96c77511f94c0c7cfbb4b7dce4a36ed2cf96edc362b1`。

**适用平台：Apple Silicon / arm64，macOS 27.0 或更新版本。** 本机 Homebrew libmpv 和部分媒体依赖的 Mach-O 最低系统版本为 27.0，因此 Info.plist 如实记录该下限；更早系统需要重新选择/编译兼容媒体依赖，不能只修改 plist 绕过。

本机没有有效 Developer ID 签名身份，本包使用 ad-hoc 签名并通过完整签名校验，**未做 Apple 公证**。当前机器本地试用与公开下载后的 Gatekeeper 行为不同；此交付不宣称可在所有机器上免提示打开。

本次附带 aria2 1.37.0、FFmpeg 9.0.1、curl 8.22.0 和 51 个 Homebrew formula 的版本/许可材料。新包全部验证成功后，已清理 6 个旧版及失败中间包目录；`build/release/` 仅保留此次交付，构建缓存和工具环境保留。

## 安装与凭据

打开 DMG，将左侧 `bbhouse-qt.app` 拖入右侧 Applications。应用包含 Qt、FluentUI、QML/图片/SQLite 插件、libmpv、aria2c、FFmpeg、curl 及其动态依赖，运行不需要安装 Qt SDK 或 Homebrew。

账号数据不随包附带。可在首次登录窗口导入 Cookie，或手动将主 Cookie 文件命名为 `bilibili.cookie.txt`，放到：

```text
~/Library/Application Support/shizi/bbhouse-qt/
```

调试使用的 `bilibili.cookie.normal.txt` 也放在同一目录。若显式指定 `BBHOUSE_DATA_DIR`，则使用该目录。`.app` 不再向上搜索 `/Applications`，也无需修改签名包内部；开发形态裸可执行程序继续按原逻辑查找仓库根 Cookie。没有自动搬运或读取本机真实凭据，已有用户设置、历史数据库和定时任务均保持不变。

已注册的历史定时任务固化了程序路径；只有用户决定改为使用安装后的应用执行服务时，才在应用内重新保存计划。此次未改动任何真实系统任务。

## 打包实现

入口为 `scripts/package-macos.py`，Finder 布局配置为 `scripts/macos/dmg-settings.py`。

- 构建 Release 主程序和所有回归程序，运行 CTest。使用现有 Qt 6.11.2 / AppleClang 与 arm64 工具链。
- 创建标准 `.app/Contents/{MacOS,Frameworks,Resources,PlugIns}`，由 macdeployqt 分析所有应用 QML 和内嵌 FluentUI 模块，部署所需 Qt 框架及插件。
- SQL 驱动只保留 QSQLITE，避免携带本应用不用、依赖外部数据库客户端的 Mimer/ODBC/PostgreSQL 插件。
- 将 aria2c、FFmpeg 和 Homebrew curl 置于 Contents/MacOS，应用默认优先使用包内工具，用户显式配置路径仍优先。递归解析 libmpv 和三个工具的非系统动态依赖，按真实文件去重；Qt universal 二进制裁剪为 arm64，重写加载路径为包内相对路径，移除开发机 RPATH。任何未解析依赖或架构缺失导致打包失败。
- 包内全部 Mach-O 检查架构、最低系统版本和依赖闭包；逐项 ad-hoc 签名，最后签 framework 与 app，再做 `codesign --verify --deep --strict`。
- 附项目许可、第三方说明、Qt SPDX、Homebrew 依赖许可证及实际使用的 formula 版本与构建配方；清单去掉本机缓存路径。不包含 Cookie、INI、观看库、缓存或用户截图。
- dmgbuild 直接生成 Finder 图标布局，应用与 Applications 在两侧，说明在下方；不通过 Finder UI 自动化操作。DMG 完成后校验、只读挂载，再从映像复制应用到新的目录复测。

所有交付写入项目 `build/release/` 下的时间戳目录，不覆盖同名产物；传入 `--clean-old` 时，在新包及 DMG 全部检查通过后清理旧的 macOS 交付目录，保留构建缓存和工具环境。临时安装、媒体 fixture、挂载点、失败中间包都在本项目 build 内管理；不改写 Qt/Homebrew 原始安装文件。

## 重建

本机 Homebrew 没有 `dmgbuild` formula；按维护要求先尝试 brew 后，已用 Homebrew Python 在项目 build 下创建独立工具环境：

```sh
/opt/homebrew/bin/python3.14 -m venv build/macos-package-tools
build/macos-package-tools/bin/python -m pip install dmgbuild==1.6.7
python3 scripts/package-macos.py --clean-old
```

需先具备 Homebrew 的 `mpv`、`aria2`、`ffmpeg`、`curl`（缺失时 `brew install mpv aria2 ffmpeg curl`）；curl 使用 keg-only 的 `opt/curl/bin/curl`，不复制系统 curl。打包拒绝 FFmpeg `--enable-nonfree` 构建，并要求 curl 具有 AppleSecTrust 系统证书验证支持。

默认脚本完成构建、CTest、打包和包自检；已有经过验证的 Release 构建可用 `--skip-build`。可通过 `--qt-prefix`、`--build-dir` 和 `--dmgbuild` 指定路径，构建/临时目录必须位于本项目 build 下。

## 验证结果

- 本机 Release 应用和完整测试目标构建通过，CTest **41/41 通过**。
- 包内 **174 个 Mach-O 文件全部为 arm64**；全部非系统依赖指向包内实际文件，无开发 Qt/Homebrew 绝对加载路径。
- `.app` 及 DMG 内的应用均通过严格代码签名检查，DMG `hdiutil verify` 通过。
- `--deployment-smoke-test --scratch-dir <项目内临时目录>` 在正常控制器/账号初始化之前分流，隔离 QSettings 和数据目录，下载控制器使用空的临时数据库，阻止 QML 网络请求。
- 冒烟额外使用 macOS sandbox 禁止网络，以及读取 `/opt/homebrew`、`/usr/local`、开发 Qt 和 build/bin。验证 `dladdr` 实际加载包内 libmpv，播放合成 PCM WAV 至正常 EOF；验证 QSQLITE 创建/写入/查询、JPEG/WebP 插件、资源图片解码，以及实际 SettingsPage.qml 无错误装载。
- 三个包内工具执行版本检查；FFmpeg 将合成 WAV 编码 AAC/M4A、无损转封装并解码，curl 使用 file 协议精确复制 fixture。独立沙箱只允许本机临时 HTTP 端口，验证 aria2c/curl 下载字节一致，继续拒绝 Homebrew 读取。
- 从只读 DMG 复制到新路径后的相同冒烟通过，证明运行不依赖原交付路径。使用原生 Cocoa 插件，但没有启动账号首页、真实请求或 UI 操作。
- OpenSpec 严格校验、Python 语法和 diff 检查通过。Git 隐私扫描命中两项（同一登录测试文件的工作树与历史版本）；复核均为以 fixture 命名的合成测试值，未发现真实凭据。打包不包含这些测试文件或任何用户配置/数据库。

以上不替代用户拖拽安装、完整界面、真实账号在线播放、硬解/音画同步及跨显示器手测。

## 保留的公开分发边界

当前字体、默认头像授权和完整依赖对应源码的公开发行义务见 [开源归属](../THIRD_PARTY_NOTICES.md) 与 [发布评估](发布准备与mpv分发评估.md)。本次把运行依赖带齐并不等同完成这些事项；用户此前决定保留字体并记录阻塞项，仍然有效。

## 应用图标（2026-09-19）

打包脚本现将 `app/resources/icons/icon.icns` 复制至 `Contents/Resources/icon.icns`，并设置 `CFBundleIconFile`；图标在签名前纳入包，并参与原包与迁移检查。此次重打包已包含最新的 macOS Dock 标准留白图标，详见[全平台应用图标](全平台应用图标.md)。

## 下载工具与 HTTPS 信任

内置 aria2 使用 OpenSSL；启动时其子进程明确读取 macOS 公共 CA 文件 `/etc/ssl/cert.pem`，不依赖 Homebrew 的证书路径，也不关闭证书校验。自定义工具路径保留原行为。内置 curl 使用 AppleSecTrust 验证系统证书。工具版本输出、Homebrew 构建配方及依赖版本位于 `Contents/Resources`。

aria2 还通过运行时加载 OpenSSL legacy provider，静态 Mach-O 列表无法自动发现。包内额外部署 `Contents/Frameworks/ossl-modules/legacy.dylib`，仅内置 aria2 子进程显式设置 `OPENSSL_MODULES`；provider 同样参与依赖闭包、签名与隔离检查。
