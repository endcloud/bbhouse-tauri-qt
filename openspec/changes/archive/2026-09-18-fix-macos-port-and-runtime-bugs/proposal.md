# Proposal: fix-macos-port-and-runtime-bugs

macOS(Qt 6.11.2,`/Users/ziyu/Qt`)移植修复 + 已报告运行 bug 修复 + FluentUI 引用完善。

## Why

项目原为 Windows(mingw)开发;本次在 macOS 上构建暴露平台耦合错误,同时修复两个已报告的运行 bug(特别关注页进入 fc、动态页封面全灭),并按约定核对 bilibili-API-collect 文档一致性。

## What Changes

### 编译/平台(macOS)

1. `PlayerApi.cpp`:`QtZlib/zlib.h` 为 Windows Qt 私有头,macOS 不存在 → 平台分派,非 Win 走系统 `zlib.h`;CMake 非 WIN32 链 `ZLIB::ZLIB`(bbhouse-qt 与 api-probe 均链)。
2. `main.cpp`:`qputenv("QT_QPA_PLATFORM", "windows:darkmode=2")` 在 macOS 上直接导致平台插件加载失败 → 加 `Q_OS_WIN` 守卫。
3. `MpvLib::probe()`:仅尝试 `libmpv-2.dll` → 按平台分派:macOS 依次尝试 应用同级/`/opt/homebrew/lib`/`/usr/local/lib` 的 `libmpv(.2).dylib`(brew mpv 提供),Linux `libmpv.so.2`;符号解析收敛到 `library_`。
4. FluentUI(Qt6/imports):`FluAcrylic`/`FluClip` 依赖 `Qt5Compat.GraphicalEffects`(本机 Qt 未装该模块,且属遗留兼容层)→ 全部替换为 Qt6 原生 `QtQuick.Effects.MultiEffect`(FastBlur→blur,OpacityMask→mask)。

### 运行 bug

5. **动态页封面无法加载**:DynamicApi 使用 `normalizeImageUrlStrict`(仅收 https/协议相对),而动态流封面多为 `http://i*.hdslb.com` → 全部置空。修复:统一改用 `normalizeImageUrlLenient`(http→https 升级),与其余 10 个域一致;删除已无引用的 strict 函数。
6. **特别关注页进入 fc**:已由 macOS 崩溃报告定位真凶 —— `FluRectangle::paint` 直接索引 `_radius[1..3]`,而 QML 给单值 `radius: 20` 时 QList 长度为 1,Qt 官方构建(force-asserts)触发 `QList::operator[]` 断言 → SIGABRT。FluClip 继承 FluRectangle,特别关注页头像条是唯一单值 radius 使用者;无 cookie 时头像条为空不触发绘制,与"无 cookie 正常"现象吻合。修复:paint 内将 radius 归一化到 4 角(缺位补末值)。
7. **avif/webp 解码失败警告**:本机 Qt 仅带 gif/ico/jpeg/svg 插件(无 qtimageformats),B 站封面转码 `.avif` 与头像 `.webp` 均不可解码。修复:AppController 新增 `imageTranscodeSuffix`(avif>webp>空)与 `decodableImageFormats` 两个能力探测属性;Format.js 新增 `ensureDecodableImageUrl`(不可解码容器经 CDN `@.jpg` 纯转格式),`buildThumbnailUrl` 增加格式后缀参数;卡片/头像/文集/合集封面全部接入。实测 CDN `xxx.webp@.jpg` 返回 200 image/jpeg。
8. **FluImage 错误重试按钮必抛 ReferenceError**(引用不存在的 id `image`):修复并改为分帧清空-回写,保证同 URL 强制重载。
9. **FluImage 内存风险**:大图(4K 封面)全量解码。新增默认 `asynchronous: true` + `sourceSize` 钳制(显示尺寸 × min(dpr,2))。
10. **特别关注页主内容区缺 FluScrollBar**(其余各页均有):补齐。

### 核对(不改码)

11. bilibili-API-collect 全端点核对:13 组端点参数/方法/解析路径与文档一致(详见项目记忆索引)。

### 第二轮(真实 cookie 实测驱动)

12. **macOS 原生标题栏**:沉浸式自绘标题栏在 macOS 下汉堡按钮与红绿灯冲突、无原生窗口按钮。修复:macOS 下 `FluApp.useSystemAppBar = true`(Main.qml 在首个窗口创建前设置),汉堡/搜索框下沉到内容区顶部工具行(高 44,仅系统栏模式显示);原生标题栏同时带来系统圆角与原生窗口按钮。Windows 行为不变。
13. **mpv 渲染上下文永远缺失**("出时长后无响应/黑屏"根因):原实现只在 `createFramebufferObject`(窗口首帧)创建 render context,而此时 mpv client 通常尚未就绪(起播需网络解析)→ 永不重试 → mpv 报 `vo/libmpv: No render context set`、视频轨 EOF。修复:[MpvVideoItem](file:///Users/ziyu/Documents/code_trae/bbhouse-qt/app/player/MpvVideoItem.cpp) 改为 render() 内惰性创建 + synchronize 跟踪 client 变化(内核重建/关窗置空时先释放旧上下文,GL 线程内)。
14. **libmpv 非 C locale 警告**:`QApplication` 按系统区域设置 LC_NUMERIC,mpv 官方要求 C locale,否则数字解析未定义。main() 中 `setlocale(LC_NUMERIC, "C")`。
15. **macOS GL 上下文仅 2.1**:mpv render 需要 3.2+ Core;main() 首处设置 `QSurfaceFormat 3.2 CoreProfile`(仅 macOS)。实测 4.1 Metal + videotoolbox 硬解生效。
16. **DASH 外挂音轨丢失**("有声无影/有影无声"的另一面):`audio-add` 在 loadfile 装载窗口期内被 mpv 静默丢弃。修复:延至 `MPV_EVENT_FILE_LOADED` 时补挂(pendingAudioUrl_)。
17. **loadfile 参数错位**:`"replace,start=N"` 整串被当作 flags(第 3 参为 index),续播起始位置从未生效。修复:flags=replace、index=-1、options="start=N" 分列。

## Impact

- 不影响 Windows 已有行为:zlib/QtZlib 分支不变;`windows:darkmode=2` 保留;MultiEffect 在两平台同版本 Qt 均可用(≥6.5);FluRectangle 归一化对完整 4 值调用零影响;图像格式后缀探测在 Windows(有 qtimageformats)仍选 avif;标题栏仅 macOS 切系统栏;GL 格式设置仅 macOS;locale/audio-add/loadfile 修复为跨平台正确性修复,Windows 同受益。
- 不改变任何 API 语义/规格契约(无 spec delta)。

## 验证

- macOS:`cmake --build build` 0 error;`store-test` 18/18 PASS;全页冒烟 0 QML 错误;libmpv 自 `/opt/homebrew/lib` 加载成功。
- 真实 cookie 实测:特别关注页种子导入 164 UP + 头像条/投稿卡渲染 40s 零断言零崩溃零解码错误(修复前同场景 SIGABRT)。
- 播放实测(BBHOUSE_SMOKE_PLAY_AID 冒烟起播链):BV1DyY26eEtr(原"出时长无响应")VO: [libmpv] 1920x1080 videotoolbox 硬解 + 外挂 AAC 音轨挂接 + AO coreaudio 出声;BV1N1tC6zE7B(原"API 404")API 全链(pagelist/wbi-playurl/durl 回落/弹幕 xml)curl+WBI 复核均可达,内核装载正常。
- UI/UX 手测项见交付说明增补清单。
