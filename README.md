# bbhouse-qt

当前版本：**1.0.1**。新增首次 Cookie 导入、独立关于页及默认关闭的 CC 字幕，见[交付与手测](doc/登录初始化与关于及CC字幕.md)。本轮已于 2026-09-19 用户确认并同步 [OpenSpec 归档](openspec/changes/archive/2026-09-19-add-login-about-and-cc-subtitles/)，扫码入口保持隐藏。

仓库：[https://github.com/endcloud/bbhouse-tauri-qt](https://github.com/endcloud/bbhouse-tauri-qt)。原 Tauri 版本保留在 [`tauri` 分支](https://github.com/endcloud/bbhouse-tauri-qt/tree/tauri)，Qt 源码迁移记录见 [发布与归档](doc/Qt源码发布与Tauri归档.md)。本项目代码采用 [GPLv3](LICENSE)，第三方组件保留原许可，见 [开源归属与参考](THIRD_PARTY_NOTICES.md)。

**发布前阻塞项**：内嵌 Segoe Fluent Icons 尚未确认再分发授权，本次按维护者决定保留；默认头像素材授权也待核实。当前不能把整个仓库/编译产物当作已完成许可清理的发行版。见 [发布准备与 mpv 分发评估](doc/发布准备与mpv分发评估.md) 和 [持久化隐私审计](doc/发布隐私审计.md)。

Bilibili.History 的 Qt6 + QML 桌面重实现，使用内嵌 FluentUI、动态 libmpv 与本地 SQLite。

已实现导航、历史、动态、稍后再看、番剧、特别关注、个人空间、直播、流行和播放窗口。本地历史 SQLite 持久同步已实现；macOS 使用 LaunchAgent，Windows 已改为 Windows Service + UAC，见[服务迁移与手测](doc/Windows原生服务与UAC.md)。Windows 原生服务与媒体 CDN 优先级两项变更已于 2026-09-19 用户手测通过并同步归档，详见[CDN 交付记录](doc/Windows任务注册与CDN优先级修复.md)。原“待播队列/关窗归档”不在本轮重定范围内，仍未迁移。当前 macOS 修复及用户手测清单见 [交付记录](doc/运行修复与手测清单.md)。

## 下载与本地媒体库

新增下载管理、独立下载记录数据库及本地导入播放。默认下载视频＋音频并合并为 MP4，同时保存 XML 弹幕与可用 SRT 字幕；需安装 aria2、FFmpeg，附件调用系统 curl。用法、依赖和手测清单见[下载管理与本地媒体库](doc/下载管理与本地媒体库.md)。

本轮已于 2026-09-19 用户验收并同步归档；[主规格](openspec/specs/download-manager/spec.md)、[归档记录](openspec/changes/archive/2026-09-19-add-download-manager/)已更新。Windows 原生验证边界继续保留在交付文档。

## macOS 构建

最低 Qt 6.7（使用公开 QSGTextNode）；本机 Qt 6.11.2 已在 `/Users/ziyu/Qt/`，libmpv 已由 Homebrew 安装。其他机器缺少内核时先 `brew install mpv`。

```sh
export PATH="/Users/ziyu/Qt/Tools/CMake/CMake.app/Contents/bin:/Users/ziyu/Qt/Tools/Ninja:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/Users/ziyu/Qt/6.11.2/macos -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bbhouse-qt store-test api-regression-test playback-entry-test player-runtime-test player-controls-test danmaku-layout-test danmaku-scene-test screenshot-test danmaku-sprite-test preferences-test danmaku-filter-test dynamics-model-test dynamics-controller-test page-search-test avatar-cache-test system-media-test system-media-artwork-test user-space-test card-author-test api-proxy-test regional-bangumi-test live-api-test live-controller-test live-player-test popular-api-test popular-controller-test popular-page-test history-scheduler-test history-sync-test history-service-entry-test history-service-worker-test history-controller-test -j 6
ctest --test-dir build --output-on-failure
./build/bin/bbhouse-qt
```

`build/bin/bbhouse-qt` 是本机开发运行产物，需当前 Qt 和 Homebrew 运行库；尚不是可拷贝到任意 Mac 的独立应用包。`deploy` 目标与 `scripts/package.sh` 为 Windows 专用。macOS 独立应用和拖拽 DMG 已提供 `python3 scripts/package-macos.py`，本机交付适用于 arm64 / macOS 27+，详见 [应用与 DMG 打包](doc/macOS应用与DMG打包.md)。

## Windows 构建

Qt 套件必须与编译器配套(`mingw_64` 只配 MinGW,`msvc2022_64` 只配 MSVC/`cl.exe`)。未显式传入 Qt 前缀时,CMake 会按编译器扫描 `C:/Qt/*` 自动挑一个套件(版本倒序),套件与编译器不配套会直接报错并给出处理办法。

```sh
# MinGW(Qt Creator kit: Desktop Qt 6.11.1 MinGW 64-bit)
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

测试目标(`store-test`/`api-regression-test`/`playback-entry-test`/`player-runtime-test`)是 `EXCLUDE_FROM_ALL`,需用 `--target` 显式构建后再 `ctest --test-dir build --output-on-failure`。`player-runtime` 依赖 `libmpv-2.dll`,Windows 下 CMake 会自动把它复制到测试 exe 同级目录。

运行/打包:把兼容架构的 `libmpv-2.dll` 放到 `3rd/mpv/`(运行库不入源码仓库；分发义务见开源说明),`deploy` 目标会打包到 `build/deploy/`;缺少该 DLL 时 configure 阶段有警告。CMake 不链接 mpv,程序运行时动态解析官方 API。

## 一键预设(CMakePresets.json)

预设把两套 Windows 工具链与 macOS 固化下来,并已把编译器/Qt 的 `bin` 加进构建期 PATH(MinGW 的 `as.exe`、测试用的 Qt DLL 都靠它);`windows-mingw` 等同 Qt Creator 的「Desktop Qt 6.11.1 MinGW 64-bit」套件。构建预设会一起构建 app 与全部回归测试目标。

```powershell
# Windows MinGW(本机默认,任何终端都能跑;cmake 用 Qt 自带那份,系统 PATH 里没有)
& C:\Qt\Tools\CMake_64\bin\cmake.exe --preset windows-mingw
& C:\Qt\Tools\CMake_64\bin\cmake.exe --build --preset windows-mingw
& C:\Qt\Tools\CMake_64\bin\ctest.exe  --preset windows-mingw

# Windows MSVC:cl 需要 VS 的 INCLUDE/LIB,所以要在开发者终端里执行
cmd /c ""C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && C:\Qt\Tools\CMake_64\bin\cmake.exe --preset windows-msvc && C:\Qt\Tools\CMake_64\bin\cmake.exe --build --preset windows-msvc"
```

macOS 上 `cmake --preset macos` 即用 Qt 6.11.2 + Homebrew libmpv;打包 Windows 交付目录仍用 `cmake --build --preset windows-mingw --target deploy`。换机器或升级 Qt 时只改 `CMakePresets.json` 里的 `CMAKE_PREFIX_PATH`/`CMAKE_*_COMPILER`(不传前缀时 CMake 也会按编译器自动挑套件)。

## Windows Release 包

```powershell
pwsh -File scripts/package-release.ps1              # 输出 D:\release\bbhouse-qt[<短提交号>]
pwsh -File scripts/package-release.ps1 -OutDir E:\rel -Force   # 换目录/覆盖同名包
```

脚本做四步:配置并构建 Release → `deploy` 目标(含 MinGW 运行时、`Qt6PrintSupport` 等 windeployqt 漏掉的依赖)→ 复制到 `build/release-stage` 后按清单精简(未用 QML 样式/插件/翻译/软件 OpenGL 回退,约 280MB→209MB)→ 清空 PATH 做自包含冒烟,并在发布目录复跑一次。冒烟用默认平台启动并强制 Qt 日志走 stderr,校验**窗口真的出现**且日志无 QML 报错(TypeError/模块缺失等),数据隔离在 `build/smoke-data`;失败直接报错,不会产出跑不起来的包。

软件包为 GUI 子系统(`WIN32_EXECUTABLE`),双击不会弹控制台;调试时可用 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 从终端看日志。

## Tag 自动编译

[GitHub Actions 工作流](.github/workflows/tag-build.yml) 在推送任意 tag 时触发，也可从 Actions 手动执行。Windows 使用 Windows 2022 / MSVC x64，macOS 使用 macOS 15 / 原生 arm64；CI 固定公开 Qt 6.8.3 SDK（满足项目最低 Qt 6.7），不使用开发机路径。

```sh
git tag v1.0.1
git push origin v1.0.1
```

上面是维护者准备发布后的操作示例，本次没有推送 tag。两端编译应用与所有回归程序；Windows 未提供已审计 libmpv，运行测试时明确排除 `player-runtime`、`screenshot`、`live-player`；macOS 安装 Homebrew mpv，排除需要原生图形会话的 `screenshot-macos`。本地可用 `cmake --build build --target bbhouse-qt regression-tests` 构建完整测试集。

成功后保存 14 天的 `bbhouse-qt-windows-x64-build` / `bbhouse-qt-macos-arm64-build` 为 **build-only** 产物，不含 Qt/libmpv/系统运行库，未重定位、签名或公证，不能直接作为独立安装包，也不自动创建 GitHub Release。首次远程 tag 构建仍需验证。现有 Windows 本地 `deploy` 会附许可证和归属文件，但完整对应源码、素材授权等发行义务仍需维护者完成。

## 新用户预设

只在相应设置尚未保存时生效，不重写旧配置：主题和语言跟随系统；区域 HTTP 代理 `localhost:7890`；弹幕开启、100% 不透明度、25px、顶部 1/4、密度不限制、相似合并开启、预缓存图像；点播新会话优先 1080P / H.265 / 1 倍速。已保存编码和记忆倍速照常保留；清晰度仍受服务端可用流和账号权益约束。代理仍仅用于区域 API，普通 API 与媒体直连。

## 应用图标

Windows ICO、macOS ICNS、Qt 运行时 PNG 与 Linux desktop/hicolor 图标已接入；构建使用仓库内素材。平台接线、Linux 安装及复测说明见[全平台应用图标](doc/全平台应用图标.md)。

## 凭据与维护

首次没有格式有效的 Cookie 时显示登录初始化窗口，可导入 Cookie 文件或粘贴文本，操作见 [Cookie-Editor 导入帮助](doc/Cookie导入帮助.md)。扫码入口暂时隐藏。服务端验证成功后原子保存为当前读取路径的 `bilibili.cookie.txt`；设置末尾可“重新登录”。稍后登录直接进入下载管理/本地媒体库。

macOS `.app` 从用户数据目录 `~/Library/Application Support/shizi/bbhouse-qt/` 读取 `bilibili.cookie.txt`，不将凭据放入签名包；裸程序开发时从仓库根读取 `bilibili.cookie.txt`（需含 `SESSDATA=`，已忽略，不入库）。播放器每次起播重新读取凭据，修正文件后可以重试同一条目。`BBHOUSE_DATA_DIR` 可将回归测试数据库隔离到项目 build 内。

- [特别关注名称与卡片作者修复](doc/特别关注名称与卡片作者修复.md)
- [标准播放器列表与光标显隐调整](doc/播放器列表与光标显隐调整.md)
- [多集选集与封面加载优化](doc/多集选集与封面加载优化.md)
- [macOS 硬解帧截图修复](doc/macOS帧截图修复.md)
- [本地历史与原生定时服务](doc/本地历史与原生定时服务.md)
- [Windows 原生服务与 UAC](doc/Windows原生服务与UAC.md)
- [Windows 任务注册与 CDN 优先级修复](doc/Windows任务注册与CDN优先级修复.md)
- [流行页面与每周必看选期](doc/流行页面与每周必看选期.md)
- [直播页面与原生播放手测](doc/直播页面与播放手测.md)
- [直播接口与内核调研](doc/直播接口与内核调研.md)
- [区域番剧与个人空间归档记录](doc/区域番剧与个人空间归档记录.md)
- [OpenSpec 积压清理与遗留规格差异](doc/OpenSpec积压清理与遗留差异.md)
- [港澳台番剧与代理设置](doc/港澳台番剧与代理设置.md)
- [视频卡片作者入口与个人空间](doc/个人空间与卡片作者导航.md)
- [系统媒体控制与头像缓存](doc/系统媒体控制与头像缓存.md)
- [导航初始选中与动态分类调整](doc/导航初始选中与动态分类调整.md)
- [页面搜索、卡片交互与稍后再看分页修复](doc/页面搜索与卡片交互分页修复.md)
- [弹幕显示设置与相似合并](doc/弹幕显示设置与相似合并.md)
- [弹幕实现切换与预缓存图像手测](doc/弹幕实现切换与预缓存图像.md)
- [弹幕场景图重构与手测](doc/弹幕场景图重构与手测.md)
- [播放器控制面板与手测清单](doc/播放器控制面板重构与手测.md)
- [B 站接口目录与端点索引](doc/bilibili-api-index.md)
- [页面与 FluentUI 审查](doc/ui-layout-review.md)
- [mpv 与弹幕审查](doc/player-runtime-review.md)
- [Qt 迁移约定](doc/qt-migration-notes.md)
- [OpenSpec](openspec/)
