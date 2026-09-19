## Context

PlayerController 初始 paused=true；MpvClient 的 lastPaused=false 将首次收到的默认 pause=false 吞掉，首次起播没有状态更新。后续切换通常经过 true→false 才恢复正常。

## Decisions

- mpv 观察通知及 file-loaded 状态快照都向上转发 pause；由 PlayerController 的现有相等判断去重。不得硬编码起播为播放，保留内核真实暂停状态。
- 列表使用锚定的固定封面与剩余文字区，显示纯文本并限制行数，避免自然尺寸或富文本越出条目。
- Qt6 FluentUI 的三个标准窗口按钮从固定 30px 改为标题栏实际高度；主窗口 48px 标题栏原先产生的上下各 9px 留白消失。保持 40px 宽度与原图标，不改变 macOS 原生标题栏或 Windows 最大化边框补偿。

## Risks / Trade-offs

窗口原生缩放/最大化与用户平台效果仍需手测；自动回归覆盖首次通知与播放/暂停变化，不把 macOS 构建当成 Windows 视觉验收。
