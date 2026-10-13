## ADDED Requirements

### Requirement: Back navigation input
应用 SHALL 将鼠标侧键 Back 与系统 Back 键转换为统一的返回事件。个人空间打开时，主窗口 SHALL 弹出统一路由栈；播放器窗口 SHALL 退出全屏或关闭窗口。

#### Scenario: Side button in personal space
- **WHEN** 个人空间可见且用户按下鼠标侧键 Back 或系统 Back 键
- **THEN** 主窗口返回进入个人空间前的路由，并保持该页的导航状态。

### Requirement: Xbox player controls
Windows SHALL 支持首个 XInput 手柄的按键边沿事件。视频播放器 SHALL 支持 A 播放/暂停、B 返回、Start 全屏、方向键调整音量/进度、肩键切换/跳转播放；直播播放器 SHALL 支持 A、B、Start 和方向键音量。

#### Scenario: Controller playback
- **WHEN** 用户在播放器窗口按下映射的 Xbox 按键
- **THEN** 播放器执行对应控制且控制面板重新显示。
