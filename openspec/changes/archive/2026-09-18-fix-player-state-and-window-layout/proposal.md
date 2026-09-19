## Why

用户手测发现新播放窗口首次起播时播放/暂停图标与实际播放不一致，播放列表长文字越界，以及主窗口自绘标题栏窗口按钮顶部留空。

## What Changes

- 修复初始 pause 属性同步，确保新内核默认播放状态也能通知控制器，并在媒体装载时重新确认。
- 播放列表明确封面和文字区边界，标题最多两行、说明一行省略，按纯文本显示。
- 主窗口标准窗口按钮顶对齐，保留系统标题栏和已有拖拽/窗口操作。

## Capabilities

### Modified Capabilities

- `video-playback-window`: 首次播放状态与列表文本边界。
- `app-navigation-shell`: 自绘标题栏窗口按钮顶边对齐。

## Impact

涉及 MpvClient 状态通知、PlayerWindow 列表布局和 MainWindow 标题栏；本机 Qt 构建、现有回归与无头装载验证，实际 UI/UX 交由用户手测。
