## Why
macOS 播放器帧截图返回 `error running command`，Windows 正常。本地 H.264 与真实 OpenGL/libmpv 复现确认：VideoToolbox 硬件帧进入截图的软件转换路径，libswscale 不支持该格式，导致 PNG 写入失败。既有无 GPU 的 PNG fixture 未覆盖此路径。

## What Changes
- macOS 使用可回读帧的自动硬解模式，保留硬件解码，保证截图获得可编码的普通像素帧。
- 保持异步纯视频截图、独立文件名与真实落盘反馈；Windows 保留既有设置。
- 增加本地 H.264、真实 macOS OpenGL 渲染与截图回归，检查字幕隔离、暂停状态和失败结果。
- 根据用户复测反馈，为所有平台截图增加严格小于 3 MiB 的落盘限制；小 PNG 保持原样，超限转 JPEG 并按需降低质量、等比缩小。

## Capabilities
### New Capabilities
无。
### Modified Capabilities
- `video-playback-window`: macOS 硬解帧截图兼容。

## Impact
mpv 初始化选项、macOS 离线回归与交付文档；不访问在线媒体或用户数据库。
