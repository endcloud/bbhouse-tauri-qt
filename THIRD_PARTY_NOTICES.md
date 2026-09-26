# 开源归属与第三方说明

项目仓库：[endcloud/bbhouse-tauri-qt](https://github.com/endcloud/bbhouse-tauri-qt)。本项目原创代码以 **GPL-3.0-only** 发布，完整文本见 [LICENSE](LICENSE)。第三方文件保留各自的版权和许可证；根许可证不授予我们不拥有的字体、图像、商标或平台内容权利。

**正式公开分发仍被图标字体授权问题阻塞**：用户于 2026-09-19 决定本次保留现有字体，仅记录阻塞项。加入 GPL 文本不表示整个仓库及其二进制已经完成许可清理。

## 直接依赖与内嵌组件

| 项目 | 来源与许可 | 本项目中的作用和分发注意事项 |
| --- | --- | --- |
| Qt 6 | [Qt](https://www.qt.io/licensing/open-source-lgpl-obligations)，按模块为 LGPL-3.0 / GPL-2.0 / GPL-3.0 或商业许可，另有第三方组件 | Core/Gui/Quick/Qml/Widgets/Network/Sql/PrintSupport/OpenGL 等。GPLv3 应用可使用兼容的开源分支；发行必须核对实际 SDK、模块、插件及其第三方清单，不能只附应用 LICENSE。 |
| FluentUI 1.7.7 | [zhuzichu520/FluentUI](https://github.com/zhuzichu520/FluentUI)，Copyright © 2023 zhuzichu，MIT | 内嵌于 `3rd/FluentUI`，本项目修改了部分控件和构建逻辑，原 [MIT License](licenses/FluentUI-MIT.txt) 已补齐。其子组件和字体不自动继承 MIT。 |
| libqrencode | [fukuchi/libqrencode](https://github.com/fukuchi/libqrencode)，LGPL-2.1-or-later | FluentUI 的 `qrcode/`，源码版权归 Kentaro Fukuchi 等，原文件头保留；[LGPL 文本](licenses/LGPL-2.1.txt)。 |
| QHotkey | [Skycoder42/QHotkey](https://github.com/Skycoder42/QHotkey)，BSD-3-Clause | FluentUI 的 `qhotkey/` 全局热键实现，Copyright © 2016 Felix Barz；[许可](licenses/QHotkey-BSD-3-Clause.txt)。 |
| QCustomPlot 2.1.1 | [QCustomPlot](https://www.qcustomplot.com/)，GPL-3.0-or-later（另有商业授权） | FluentUI 的 `qmlcustomplot/`，Copyright © 2011–2022 Emanuel Eichhammer。这里采用源码声明的 GPL 分支，不能把整个插件说成纯 MIT。 |
| Chart.js 2.9.4 / ChartJs2QML | [Chart.js](https://github.com/chartjs/Chart.js)、[Elypson/ChartJs2QML](https://github.com/Elypson/ChartJs2QML)，MIT | FluentUI 的 `JS/Chart.js`，保留 Chart.js Contributors、ChartJs2QML contributors / Michael A. Voelkel 版权，含 color、color-convert、color-name、color-string 的 MIT 代码；对应许可见 `licenses/`。 |
| mpv API 头文件 | [mpv-player/mpv](https://github.com/mpv-player/mpv)，ISC | `3rd/mpv/include/mpv/`，Copyright © 2017–2018 the mpv developers；各头文件声明完整保留，[文本](licenses/mpv-headers-ISC.txt)。头文件的 ISC 许可不代表内核也是 ISC。 |
| libmpv | [mpv Copyright](https://github.com/mpv-player/mpv/blob/v0.41.0/Copyright)，默认 GPL-2.0-or-later，满足条件的 LGPL 构建另计 | 运行时通过 QLibrary 加载，没有静态链接 mpv。开发机用 Homebrew 或用户提供的 DLL。单独二进制包应捆绑兼容内核和传递依赖，或明确要求安装；动态加载不豁免许可证义务。 |
| FFmpeg 及媒体依赖 | [FFmpeg legal](https://ffmpeg.org/legal.html)，LGPL/GPL 取决于构建选项及依赖 | libmpv 的传递依赖，也是下载模块的无损合并和本地封面提取工具。macOS 独立应用附带 Homebrew 可执行程序；Windows Release 脚本附带指定的 ffmpeg.exe，本轮为 [Gyan](https://www.gyan.dev/ffmpeg/builds/) 9.0.1 essentials 静态构建。本机构建启用 `--enable-gpl --enable-version3`，按 GPLv3 分支；根 LICENSE 提供 GPLv3 文本。不可用“libmpv 为 LGPL”推断整个媒体栈为 LGPL，禁止把 `--enable-nonfree` 构建作为可自由再分发包。 |
| aria2 | [aria2](https://github.com/aria2/aria2)，GPL-2.0-or-later（含 OpenSSL 例外，见上游 COPYING） | 下载模块通过外部 aria2c 进程下载 DASH；源码仓库不提交可执行程序。macOS 独立应用捆绑 aria2c 及其非系统动态依赖；Windows Release 脚本附带用户指定的静态 x64 程序，本轮为 1.37.0；保留用户自定义路径。版权 Tatsuhiro Tsujikawa 等；[GPLv2 文本](licenses/GPL-2.0.txt)，可选择后续版本。 |
| Vulkan Loader | [KhronosGroup/Vulkan-Loader](https://github.com/KhronosGroup/Vulkan-Loader)，Apache-2.0 及更宽松许可 | 本轮 Windows libmpv 的必需依赖，Windows Release 附带 1.4.341.0 x64 Loader；复用本机已签名 Vulkan Runtime 文件，非 Windows 自带依赖。[对应版本许可](licenses/Vulkan-Loader-Apache-2.0.txt)。显卡驱动仍由系统提供。 |
| curl | [curl license](https://curl.se/docs/copyright.html)，curl 许可 | 调用 curl 下载弹幕及字幕响应；macOS 独立应用附带 Homebrew curl/libcurl 及依赖，启用 AppleSecTrust 系统证书验证，不复制 Apple 系统可执行文件；Windows 使用系统自带 curl，未内嵌 libcurl。 |
| SQLite | [SQLite copyright](https://sqlite.org/copyright.html)，public domain | 由 Qt QSQLITE 驱动提供本地存储；发行仍应保留 Qt SQL 插件相关许可。 |
| zlib | [madler/zlib](https://github.com/madler/zlib)，zlib 许可 | macOS 使用系统库，Windows 使用 Qt 的 zlib；[许可文本](licenses/zlib.txt)。 |

`licenses/` 补充文件来自上游许可原文或本机对应依赖安装件，不替代未来发行包对**实际所带每个库**的许可证、版本和完整对应源码清单。

## 近期参考项目

这些项目用于架构、算法、API 和交互研究；并不意味着把它们的完整运行库捆绑在本项目里。

| 项目 | 许可 | 参考范围 |
| --- | --- | --- |
| [wiliwili](https://github.com/xfangfang/wiliwili) | GPLv3 | mpv 生命周期、DASH、WBI、弹幕调度和 B 站接口；见 [调研](doc/wiliwili调研.md) 与 [运行时审查](doc/player-runtime-review.md)。早期文档曾提出移植建议，不应据此把所有条目都描述为实际复制。 |
| [bililocal](https://github.com/ancientlysine/bililocal) | GPLv3 | 弹幕准备/绘制分离、图片缓存、纹理和在屏队列；见 [评估](doc/bililocal弹幕重构评估.md)。未搬入旧 Qt5/OpenGL 后端。 |
| [pakku.js](https://github.com/xmcp/pakku.js) | GPLv3 | 时间窗、文本规范化、候选缓存及数量标记；见 [参考说明](doc/pakku弹幕合并参考.md)。当前为 Qt 独立实现，未引入上游代码、WASM 或词典。 |
| [Danmaku](https://github.com/weizhenye/Danmaku) | MIT，Copyright © 2014 Zhenye Wei | DPR 缓存画布、seek 游标、暂停恢复等设计；[许可](licenses/Danmaku-MIT.txt)。 |
| [DanmakuFrostMaster](https://github.com/cotaku/DanmakuFrostMaster) | MIT，Copyright © 2021 cotaku | 文字预渲染与活跃弹幕调度；[许可](licenses/DanmakuFrostMaster-MIT.txt)。两项参考见 [预缓存图像说明](doc/弹幕实现切换与预缓存图像.md)。 |
| [bilibili-API-collect（本地参考 fork）](https://github.com/pskdje/bilibili-API-collect) | 文档 CC BY-NC 4.0 | 按接口域查阅端点、参数、响应事实，见 [索引](doc/bilibili-api-index.md)。这是有限制商业用途的文档许可，**不是 GPL 兼容软件许可证**；不要将原文、截图、示例代码整份复制后按 GPL 再授权。如需再分发受保护表达，应按原条款署名并核实用途/另获授权。 |

## 字体和图像的授权边界

三个路径中的 `Font/FluentIcons.ttf`（`3rd/FluentUI/FluentUI/`、`Qt5/imports/FluentUI/`、`Qt6/imports/FluentUI/`）经读取字体 name 表，实际均为 **Segoe Fluent Icons**，版权为 Microsoft Corporation。字体内许可仅允许原产品许可范围内的使用、受限制的嵌入与临时打印下载，并写明 “Any other use is prohibited.”。这不构成随 GPL 项目再分发授权；参见 [Microsoft 字体再分发 FAQ](https://learn.microsoft.com/en-us/typography/fonts/font-faq)。

**发布阻塞项：在公开含这些字体的 Git 仓库、源码压缩包或二进制前，获得明确再分发授权或替换为许可清楚的开源图标并完成 UI 手测。** 只在最新版本移除也不会清掉旧提交里的字体，应另行安排历史处理；本次不重写历史。

`app/resources/images/noface.jpg` 是既有默认头像素材，不能由代码 GPL 声明推断图片版权授权。仓库未附其授权链，正式发行前应核实或替换为自制头像。哔哩哔哩品牌、封面、头像、视频、弹幕等在线内容的权利属于相应权利人；访问接口不会转移这些权利。

## 二进制发行所需材料

分发应用、Qt、FluentUI 插件、libmpv/FFmpeg 时，应保留版权和许可证，提供与二进制**确切版本及修改**相对应的可机器读取源码、构建脚本/配置，覆盖许可证要求的传递依赖；网络分发可采用 GPLv3 §6(d) 方式提供等价的源码获取入口。一个项目主页链接或任意最新版本源码不能代替对应源码。若选择 LGPL 路径，还应满足替换/重新链接与修改调试等相应条款。

具体内核体积、动态库部署边界、未完成事项和来源见 [发布准备与 mpv 分发评估](doc/发布准备与mpv分发评估.md)。
