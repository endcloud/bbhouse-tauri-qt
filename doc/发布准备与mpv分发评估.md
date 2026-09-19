# 发布准备与 mpv 分发评估

日期：2026-09-19；OpenSpec：`prepare-public-release`。本次更新发布准备材料和构建流程，没有推送 tag、创建 GitHub Release 或改写用户配置。字体授权按用户决定保留为发布阻塞项，本变更不等同“现在即可合法发布所有文件”。

## 本次交付

- 正式仓库入口统一为 [endcloud/bbhouse-qt](https://github.com/endcloud/bbhouse-qt)，设置页和英文翻译移除 localhost 占位与“mpv 官方 Windows 构建/仅本机自用”的不准确描述。
- 增加 GPLv3 全文，明确本项目代码采用 GPL-3.0-only，补第三方归属及许可文本。涵盖 Qt、FluentUI、libqrencode、QCustomPlot、QHotkey、Chart.js/ChartJs2QML 及颜色库、mpv API、libmpv/FFmpeg、SQLite、zlib，以及 wiliwili、bililocal、pakku.js、Danmaku、DanmakuFrostMaster 和 API 文档参考。
- 完成持久化、Git 跟踪文件和本地可达历史审查，关闭原始 mpv 文本日志，API 错误脱敏后才展示/入库，新增忽略规则与只读审计工具；详见 [隐私审计](发布隐私审计.md)。
- 只修改读取缺省值，空配置读取不落盘；旧配置中的合法值（含旧默认值）完整保留。没有访问或改写本机真实 INI/数据库/Cookie。
- 新增 tag 自动构建，产物明确为 build-only；现有 Windows deploy 同步附带根 LICENSE、第三方说明和 `licenses/`。删除不参与链接的 `3rd/mpv/libmpv.dll.a`，保留官方 ISC API 头文件。

## 初始化预设

| 区域 | 新用户缺省值 |
| --- | --- |
| 外观与行为 | 主题、语言均跟随系统 |
| 区域代理 | HTTP，`localhost:7890`；用户原输入 `locahost` 按常用回环主机名修正 |
| 弹幕默认状态 | 开启 |
| 不透明度 / 字号 | 100% / 25px |
| 显示区域 / 密度 | 顶部 1/4 / 不限制（仍有资源预算与碰撞保护） |
| 合并相似弹幕 / 实现 | 开启 / 预缓存图像（sprite） |
| 点播新会话 | 1080P（qn=80）/ H.265（hev1）/ 1 倍速 |

配置没有迁移：已保存场景图文字、全屏弹幕区域、关闭合并、旧端口或其他自定义值仍保持；编码采用已有偏好优先，开启记忆速度且已保存速度时也不重置。清晰度是新会话默认，后续会话内选择保持。服务端不提供所选档位/编码时沿用已有降级，不绕过权益。代理仅作用于区域详情/PGC API，普通 API 和音视频继续直连。

## GitHub Actions

`.github/workflows/tag-build.yml` 对所有 tag push 和 `workflow_dispatch` 触发：

| 平台 | 编译环境 | 测试范围 |
| --- | --- | --- |
| Windows x64 | `windows-2022`、MSVC x64、Qt 6.8.3 `win64_msvc2022_64` | 编译所有测试；因没有提供已审计 libmpv，执行时排除 player-runtime/screenshot/live-player |
| macOS arm64 | `macos-15` 原生 arm64、AppleClang、Qt 6.8.3 `clang_64`（显式 arm64 构建） | Homebrew mpv；执行所有非交互测试，排除要求原生图形会话的 screenshot-macos |

Actions 按提交 SHA 固定；仅 `contents: read`，检出不保留推送凭据。`regression-tests` 聚合构建全部 `*-test` 目标。数据目录在 build 内；产物白名单不包含真实运行数据或测试日志，失败时只上传 CI 合成数据产生的回归日志。

产物 `bbhouse-qt-windows-x64-build` / `bbhouse-qt-macos-arm64-build` 保留 14 天。**它们不是可直接拷到干净机器上的安装包**：只含应用、FluentUI 模块、许可和构建说明，不带 Qt、libmpv/传递依赖和系统运行库，不做加载路径重定位、签名、公证，也不自动创建 GitHub Release。macOS 压缩包用 tar.gz 保留执行权限。

本机检查不能替代第一次真实 GitHub runner 运行。维护者在解决发布阻塞并准备好后再推送 tag；本次未触发远程流水线。

## mpv 是否需要随包分发

- **源码仓库不需要放 libmpv 二进制**。本项目只依赖公开 API 头文件并用 QLibrary 动态加载；没有必要把 DLL/dylib 提交进 Git。完整 mpv 源码也不必内嵌此仓库，可作为匹配发行版本的独立源码资产提供。
- **面向普通用户、开箱即播的 Release 应携带 libmpv 及其实际依赖**。Windows 通常把 x64 `libmpv-2.dll` 放在 exe 同级；是否还需其他 DLL 取决于所选构建。仅携带 mpv.exe 不能替代 libmpv。
- macOS 需要在 `.app/Contents/Frameworks` 放 arm64 libmpv 和依赖，修正 install names/RPATH、处理签名；本项目加载器已查找该位置，但当前开发构建还不是独立 `.app`。**只拷一个 Homebrew libmpv dylib 不够**。
- 也可明确要求用户自行安装兼容内核：macOS `brew install mpv`，Windows 手动安装 DLL。这样不属于“独立发行包”，不能承诺双击后马上可播；不捆绑也不应被用来绕开应用与 GPL 内核组合使用的许可分析。

## GPLv3 的兼容性和实际风险

mpv 0.41.0 上游 [Copyright](https://github.com/mpv-player/mpv/blob/v0.41.0/Copyright) 声明默认 GPL-2.0-or-later。该 **or-later** 允许选择 GPLv3，因此与本项目 GPL-3.0-only **通常兼容**；这不是 GPL-2.0-only 与 GPLv3 的不兼容情形。API 头文件的 ISC 许可单独保留，不代表内核许可变为 ISC。

最终结论取决于**实际二进制的完整依赖和构建参数**。本机 Homebrew mpv 为 0.41.0_9，FFmpeg 9.0.1_1 含 `--enable-gpl --enable-version3`，因此不能按纯 LGPL 套件描述。上游 `-Dgpl=false` 也不能保证 FFmpeg/其他库都满足 LGPL；不得选用了 `--enable-nonfree` 或不兼容组件后仍宣传可自由再分发。编码专利与第三方平台服务条款不由 GPL 自动解决。

GPLv3 二进制网络分发通常需要按 §6(d) 等适用方式提供对应源码，覆盖应用、修改后的内嵌库、所带 GPL/LGPL 依赖和许可证要求的构建材料，保留全部声明。提供源码时要匹配实际 tag/版本、补丁和配置；仅附项目主页或任意最新源代码不够。本项目本次补的许可证清单是基础材料，尚未生成一个完整、已核对的媒体运行库对应源码包。

当前已识别的发布风险：

1. **Segoe Fluent Icons：阻塞公开分发。** 仓库三份 `FluentIcons.ttf` 完全相同，name 表声明 Microsoft 版权和受限使用，无一般再分发授权。FluentUI MIT 不能覆盖这个字体。按用户决定本次保留，在取得授权或替换前，不应公开含该资源的源码/二进制。后续替换时还要考虑旧 Git 历史中的副本。
2. `noface.jpg` 默认头像缺少仓库内授权链，应核实或自制替换；代码许可不涵盖素材。
3. API 参考文档为 **CC BY-NC 4.0**，不是可随意再许可为 GPL 的软件。可以记录独立描述的接口事实；复制文档表达、截图或代码需核实原条款和用途。
4. Qt、FluentUI 子组件及具体媒体栈须逐项保留许可、提供必要对应源码。现有 QCustomPlot GPL-3.0+ 与本次 GPLv3 方向一致，libqrencode LGPL、QHotkey BSD、MIT 组件同样需保留声明。

## mpv 体积实测

日期为 2026-09-19，1 MiB = 1,048,576 字节。以下是**内核增量**，不含 Qt、主程序、签名、公证、许可证/源码包，也不承诺其他版本体积相同。

| 样本 | 未压缩 | 压缩/下载 | 范围 |
| --- | ---: | ---: | --- |
| 本机 arm64 libmpv 0.41.0_9 本体 | 4,495,136 B ≈ 4.3 MiB | 未单独测量 | 单一 dylib，不能脱离依赖运行 |
| 本机 libmpv 的 Homebrew dylib 闭包 | 63,950,576 B ≈ 61.0 MiB | ZIP deflate level 6：26,840,321 B ≈ 25.6 MiB | `otool -L` 递归、真实文件去重，共 48 个 dylib，不含系统库；不含运行时可选加载模块，非完成部署/签名后的包 |
| Windows 普通 x64 样本 libmpv-2.dll | 120,342,528 B ≈ 114.8 MiB | 整份开发归档 31,363,218 B ≈ 29.9 MiB | `mpv-dev-x86_64-20260903-git-69e63f425a.7z`；归档另含头文件和导入库，下载量不是仅 DLL 的压缩量 |
| 仓库保留的 mpv API 头文件 | 137,324 B ≈ 134.1 KiB | — | 不包含运行内核；已删除无用的 53,308 B 导入库 |

Windows 样本来自 [shinchiro/mpv-winbuild-cmake 20260903](https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20260903)，是第三方构建，不冒称 mpv 官方二进制。用于体积测量而非批准分发；本次没有执行该 DLL。下载归档 SHA-256：`fac135c68a35b7639e39d72c0c365104edbaebdea39a0dfdd8c36e8c8e80faef`。macOS 压缩只测量 dylib 数据，没有重定位或签名。

建议对用户的独立发行包捆绑合规内核，将对应源码作为同版本单独下载项，避免把巨大的 DLL 与完整依赖源码放进日常 Git 历史。最终发布包体积应在锁定平台依赖、部署 Qt、去除不需要的插件并完成签名后另行实测。

## 验证与待手测

本机 Qt 6.11.2 Release 主程序与 `regression-tests` 全部编译成功，CTest **32/32 通过**（含本机原生 macOS 截帧回归）；英文资源 541 条全部完成。`actionlint`、Python 语法、OpenSpec 严格校验和 diff 空白检查通过；编译产物收集脚本的 ZIP/tar 与数据排除已用隔离 fixture 检查。代理只执行构建、静态检查和非交互回归，不使用真实账号请求。

用户手测：使用隔离的新用户配置验证默认选项、1080P/H.265/1x、弹幕显示范围；已有账号配置应保持原样。中英设置 About 检查链接、换行及复制行。首次 GitHub tag 运行验证两个 runner 的编译、测试与产物；正式发行需另做干净 Windows/macOS 机器的依赖、播放、签名/公证验证。
