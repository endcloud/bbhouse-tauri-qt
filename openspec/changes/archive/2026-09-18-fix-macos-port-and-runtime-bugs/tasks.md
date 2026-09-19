# Tasks: fix-macos-port-and-runtime-bugs

- [x] 1.1 编译修复:zlib 平台分派 + CMake ZLIB 链接(app/api-probe)
- [x] 1.2 编译修复:main.cpp QT_QPA_PLATFORM 加 Q_OS_WIN 守卫
- [x] 1.3 MpvLib macOS/Linux dylib/so 候选路径加载
- [x] 1.4 FluAcrylic/FluClip → QtQuick.Effects MultiEffect(去 Qt5Compat 依赖)
- [x] 2.1 动态页封面:DynamicApi 统一 lenient 归一化,删除 strict
- [x] 2.2 特别关注页 fc:定位为 FluRectangle::paint radius 列表越界(单值 radius),paint 内归一化到 4 角;真实 cookie 164 UP 实测零崩溃
- [x] 2.6 avif/webp 不可解码:AppController 能力探测属性 + Format.ensureDecodableImageUrl + 缩略图格式后缀全链接入;实测零解码错误
- [x] 5.1 macOS 原生标题栏(FluApp.useSystemAppBar)+ 内容区工具行(汉堡/搜索下沉)
- [x] 5.2 mpv render context 惰性创建(MpvVideoItem render 内 + synchronize client 跟踪)
- [x] 5.3 setlocale(LC_NUMERIC,"C") + macOS GL 3.2 CoreProfile(实测 4.1 Metal)
- [x] 5.4 DASH 外挂音轨延至 file-loaded 补挂;loadfile start 参数归位 options
- [x] 5.5 冒烟起播钩子(BBHOUSE_SMOKE_PLAY_AID)+ mpv log-file 调试开关(BBHOUSE_MPV_LOG)
- [x] 2.3 FluImage:重试按钮 ReferenceError 修复 + 分帧重载
- [x] 2.4 FluImage:默认异步 + sourceSize 解码钳制
- [x] 2.5 特别关注页主内容区补 FluScrollBar
- [x] 3.1 bilibili-API-collect 13 组端点核对 + 记忆索引
- [x] 4.1 构建 0 error + store-test 18/18 + 全页冒烟 0 错误
- [x] 4.2 用户手测(特别关注页 fc 复核/动态封面/头像圆角裁切/图片加载)

归档验收记录（2026-09-18）：用户明确反馈“手测完成，完全 pass”，并确认除两个保留迁移项外其余 change 已实现。该勾选记录用户验收，不代表本机重新执行 Windows 或跨屏手测。
