# Windows 可运行 Release 包（2026-09-19）

当前版本已更新为 **BBHouse 2.0.2**，Windows 启动文件为 `BBHouse.exe`，最新路径与验证见 [2.0.2 发布记录](BBHouse2.0.2发布.md)。以下保留早期包的历史记录。

后续修复：用户发现下述旧包首帧后闪退，已定位 Windows SMTC 的 Release 优化问题；新版包和包含实际视频的验证见 [起播闪退修复](WindowsRelease起播闪退修复.md)。本文原始 6 项 CTest/音频冒烟结果不能作为视频起播通过的证据。

OpenSpec：`package-windows-release`。目标目录：`D:\release\bbhouse-qt[源码提交号]`。使用 Qt 6.11.1 MinGW x64、MinGW 13.1.0、Ninja Release，目录可整体移动；启动 `bbhouse-qt.exe`，无需安装 Qt 或开发工具。

本次实际交付：`D:\release\bbhouse-qt[af874cc]`，**232 个文件、310125144 字节（295.76 MiB）**。最终路径的隔离部署、PE 依赖及外部工具检查通过，231 条文件 SHA-256 均已核验；源码提交为 `af874cc`，后续文档提交仅记录结果。

## 包含内容与体积

- 主程序、Windows 原生历史服务宿主、Qt/FluentUI、libmpv、Vulkan Loader、SQLite/JPEG/WebP/Schannel 插件及必需 QML 模块。
- `tools/aria2c.exe` 1.37.0、`tools/ffmpeg.exe` 9.0.1 essentials。保留下载、MP4/M4A 合并、媒体解码及封面提取；不附 ffplay/ffprobe。
- Windows 10/11 自带的 curl、系统组件和显卡驱动由系统提供；不把 Qt SDK 或本机额外安装的 Vulkan 当成系统依赖。
- 去除未用控件主题、调试插件、Qt 翻译、额外 SQL/图片插件、SVG/Lottie、软件 OpenGL 回退、开发元数据和重复 FluentUI 文件，仅对自有 EXE/DLL 的暂存副本 strip。保留许可证和 Qt 动态库替换结构。

暂存实测 **295.7 MiB**，57 个 x64 PE 文件。原 deploy 294.1 MiB 尚不含下载工具和 Vulkan；若补相同依赖而不裁剪约为 399.2 MiB，本次裁剪节省 **103.5 MiB**。此外选用 98.1 MiB 的 FFmpeg essentials，较本机 214.9 MiB 的 full 构建再省约 116.8 MiB。最终目录附少量说明和哈希清单，准确字节数以打包脚本输出为准。

这是保留功能和运行稳定性的精简包，未用 UPX，也未重编删功能版 Qt/libmpv；不宣称理论上的最小体积。无软件 OpenGL 回退，需要正常显卡驱动。

## 依赖来源

| 文件 | 本次来源 / 版本 | SHA-256 |
|---|---|---|
| `libmpv-2.dll` | 项目已有运行库，未替换 | `7469d0493ed908ca4d3ea46681b930ccc29a159f430e4375a0305bba94f3deba` |
| `tools/ffmpeg.exe` | Gyan 9.0.1 essentials 静态构建 | `72a489eccd008c2ec2c0a5856c5c75bc3d8bbfa90166c4566865c246445e6aa3` |
| `tools/aria2c.exe` | 本机已有 aria2 1.37.0 静态 x64 文件 | `be2099c214f63a3cb4954b09a0becd6e2e34660b886d4c898d260febfe9d70c2` |
| `vulkan-1.dll` | 本机 Vulkan Runtime 1.4.341.0，Microsoft Windows Hardware Compatibility Publisher 有效签名 | `f6863eaeaa36d3670b735951e4270f9116204f3554157c21890cffe8fc17abd5` |

FFmpeg 归档来自 [Gyan Release essentials](https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip)，归档 SHA-256 为 `fec81ae03971d9dd4be3ebe02e263bd2ec1d789483f931bdba5f5715e65da2e9`。该 URL 会随版本更新；复现本轮应使用上述版本/哈希，不将未来下载误认为相同文件。对应许可、来源与现有公开分发授权事项见 `THIRD_PARTY_NOTICES.md` 及 `licenses/`。本次是本地交付目录，不是远程公开发布。

## 生成方式

```powershell
pwsh -File scripts/package-release.ps1 -StageOnly -FfmpegPath <静态essentials版ffmpeg.exe>
# 完成验证并提交源码，再输出；缺省 aria2 来自 PATH，Vulkan 来自已安装 Runtime。
pwsh -File scripts/package-release.ps1 -OutDir D:\release -FfmpegPath <静态essentials版ffmpeg.exe>
```

也可显式指定 `-Aria2Path`、`-VulkanPath`；`-SkipBuild` 仅供刚完成 Release deploy 后使用。正式输出要求 Git 工作区已提交，默认目录名取该源码提交号。同名目录追加 `yyyyMMdd_HHmmss_fff`，`-Force` 不再删除旧包。

`qt.conf` 固定插件和 QML 路径。FluentUI 内嵌的 `Qt.labs.qmlmodels` 不能被只扫描应用目录的 windeployqt 发现，因此脚本显式补入其 DLL 和 QML 插件；不得再次随未用 labs 模块一并删掉。Vulkan 是 libmpv 的 PE 硬依赖，即使本机 System32 有该文件也必须随包携带。

## 验证结果与边界

- Release 主程序、服务宿主及相关测试目标构建通过。
- 本轮 Release CTest **6/6**：store、history-service-entry、download、download-cover、onboarding-page、login-cookie。
- 精简暂存包隔离入口通过：实际包内 libmpv、本地 PCM 解码播放、SQLite 读写、JPEG/WebP、内嵌图标与中文帮助、Schannel、Qt/QML/FluentUI、Shapes/Effects/qmlmodels 及设置页。
- 57 个 EXE/DLL 的 PE32+ x64 普通/延迟导入检查通过，非系统依赖均在包内。明确系统白名单排除 Vulkan 和另装的 VC++ 运行库，避免本机安装掩盖缺失。
- 包内 FFmpeg 合并 MP4、提取 M4A、缩放 JPEG 封面，以及 aria2/系统 curl 的本机回环 HTTP 下载与内容一致性验证通过。
- 打包脚本在最终发布目录再次执行相同检查，处理带方括号路径，并生成 `使用说明.md`、`依赖清单.md` 和 `SHA256SUMS`。

验证使用项目 build 内临时数据、系统 PATH 和隔离配置，不读取真实 Cookie/用户数据库，不创建可见窗口，不注册真实服务，不访问账号 API。UI、GPU 画面和在线业务仍由用户手测。上轮 Debug 全量 CTest 的 5 项运行失败仍未在本任务修复，详情见[Windows 构建复验](Windows原生服务与UAC.md#windows-构建占用排查)；本轮定向验证不能替代全量通过。
