# Proposal: harden-playback-and-layout

## Why

前次 macOS 移植已可构建，但用户仍报告起播内核缺失/API 404、特别关注头像锯齿以及页面间距问题。需以本机 Qt 6.11.2 和当前代码重新验证，补齐真实故障处理与可维护的参考索引。

## What Changes

- 审核全部页面及实际使用的 FluentUI 组件，修复尺寸约束、边距和圆形头像遮罩。
- 核对 B 站端点、ID 传递和播放解析，建立仓库内持久化接口索引。
- 对照 wiliwili 审核 mpv 生命周期、渲染与弹幕，修复起播与错误恢复。
- 以 macOS Qt 构建及适量无头回归验证；UI/UX 和 Windows 视觉效果由用户手测。

## Impact

涉及 app、已内置 FluentUI、构建配置和文档；不修改仓库外的参考项目，不提交凭据或运行日志。

## 手测反馈补修

用户确认整体基本通过、播放器工作正常，剩余问题为视频上下颠倒。mpv 输出到 Qt Quick 的离屏 FBO，不应套用默认 framebuffer 的 Y 翻转；将 MPV_RENDER_PARAM_FLIP_Y 设为 0，保持 Qt 项不镜像。
