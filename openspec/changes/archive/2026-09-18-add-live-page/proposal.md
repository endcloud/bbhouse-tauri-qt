## Why

Qt 客户端尚无关注直播入口。用户需要浏览已关注且正在直播的房间，并直接在客户端播放；参考 Tauri 客户端及 DPlayer 的直播处理，同时沿用现有原生播放器架构。

## What Changes

- 新增直播导航、缓存页面、关注开播卡片、搜索、刷新、分页与错误/空态。
- 接入 B 站关注直播与房间取流 API；仅显示正在直播的房间，保留真实清晰度及可用线路。
- 新增独立直播窗口与 libmpv 控制器，支持播放暂停、音量、清晰度、线路回退、重新连接、全屏和关闭清理。
- 直播不使用点播进度、续播、弹幕 XML 或观看心跳；接口与流均直连。

## Capabilities

### New Capabilities
- `live-ui`: 已关注开播列表及直播播放。

### Modified Capabilities
- `app-navigation-shell`: 接入直播缓存路由与独立搜索。

## Impact

涉及 app/core、controllers、player、QML、翻译、构建与离线回归。参考仓库只读，不新增 WebView/DPlayer 运行依赖。直播弹幕和互动不在此次列表与播放功能范围。
