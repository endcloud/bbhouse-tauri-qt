## Why

播放器窗口在 Windows 全屏切换时偶发保留 Fluent 原生边框并闪烁；特别关注页码、下载重复任务、清晰度记忆、历史搜索和个人空间路由也存在交互不一致。直播与视频播放还缺少分 P、录制、帧截图及启动更新提醒能力。

## What Changes

- Windows 播放器和直播播放器进入显示器全屏时临时移除原生窗口边框，退出后恢复原窗口状态，并增加视频分 P 选择。
- 修复特别关注分页选中态，头像选中态更明显，默认按加入时间倒序；个人空间使用主窗口统一路由返回按钮。
- 下载按视频身份去重，视频已含音轨时抛弃重复音频任务；主动选择的点播清晰度持久化，缺失时回落到最近较低档，默认 1080P。
- 历史搜索改用官方 history/search 接口；直播增加压缩录制和帧截图。
- 鼠标侧键/系统 Back 键统一触发路由返回；Windows Xbox/XInput 按键映射到播放器常用控制。
- 启动异步检查 version.json，远端版本码更高时提供更新弹窗。

## Capabilities

### Modified Capabilities
- `video-playback-window`
- `special-follow-ui`
- `download-manager`
- `online-history-ui`
- `user-space-ui`
- `live-ui`

### New Capabilities
- `application-update-check`

## Impact

影响 Qt/QML 播放窗口、播放 API、下载与历史控制器、特别关注/个人空间页面及启动网络请求。凭据、媒体签名地址和用户数据库格式不变；Windows 全屏、直播录制及真实在线更新弹窗仍需用户手测。
