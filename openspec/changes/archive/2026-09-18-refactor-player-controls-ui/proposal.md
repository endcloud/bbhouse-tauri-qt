## Why

用户要求参考前版 Tauri 的 PlayerControls.vue / Video.vue，以 YouTube 式底部面板重构 Qt 播放器 UI/UX，保留已经手测通过的播放功能。

## What Changes

- 全宽进度条置于底部操作行上方，深色渐变、圆形按钮与文字胶囊、向上菜单。
- 使用 FluentUI 按钮、滑块、菜单与滚动条；保留现有清晰度/编码/倍速保持/弹幕/截图/选集/列表/全屏功能。
- 暂停常驻、悬停预览时间、音量悬停展开及静音恢复，窄窗双行适配。
- 不增加无后端的字幕、PiP 等假入口，不修改播放/API/渲染内核。

## Capabilities

### Modified Capabilities

- `video-playback-window`: 控制栏布局、显隐、键盘与指针交互。

## Impact

仅 QML 展示层、资源注册、翻译和文档；原播放控制器接口保持。
