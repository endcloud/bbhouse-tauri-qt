## Why

用户反馈弹幕卡顿、字形和平滑度差，以及顶部截图包含弹幕。对照 bililocal 并完成离屏评估后，用户授权按方案重构。

## What Changes

- 将 QQuickPaintedItem 整层逐帧文字路径绘制改为 QQuickItem/QSGTextNode，稳定帧复用文字节点。
- 拆分媒体时钟、车道调度与文字排版，设置微软雅黑/苹方默认字体，处理 DPR/场景图资源重建。
- 随 Qt Quick 帧节奏更新；为空时等待入场，限制密集突发准备量；平滑时钟回校，显式 seek 立即重定位。
- 纯视频帧异步截图，唯一文件名与真实命令结果回报。
- 增加模型、软件/GL 场景图、真实 mpv 截图回归及性能记录。

## Capabilities

### Modified Capabilities

- `video-playback-window`: 弹幕文字资源复用、平台字体、纯视频帧截图。

## Impact

新增 DanmakuLayout，重写 DanmakuEngine 呈现层；扩展 MpvClient 异步命令与截图链路；保留视频渲染/API/UI 功能。最低 Qt 6.7。本机已用 Qt 6.11.2 验证，真实播放与跨屏观感待用户手测。
