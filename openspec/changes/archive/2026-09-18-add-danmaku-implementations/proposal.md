## Why

用户手测确认现有场景图实现功能正常，但起播和 seek 仍有抽动，要求保留它并增加可选实现，参考 DanmakuFrostMaster 与前端 Danmaku。

## What Changes

- 设置新增持久化的弹幕实现选择：场景图文字（现有，默认）、预缓存图像（新增）；切换立即应用，保留播放状态。
- 新实现借鉴预生成文字位图、分离调度与动画、前向 seek 清队列，采用 Qt 图片纹理节点呈现。
- 处理起播/seek 尚未完成时的旧媒体时间样本，等实际播放重新开始后启用新实现的动画。
- 增加实现切换、缓存失效与 seek 的非交互回归；UI/UX 由用户手测。

## Capabilities

### Modified Capabilities

- `video-playback-window`: 可切换弹幕实现及新实现调度。

## Impact

AppPreferences、设置页及翻译、DanmakuEngine、独立图像调度器、mpv 播放同步事件和回归测试。外部参考仓库只读，现有文字实现保留，纯帧截图行为保持。
