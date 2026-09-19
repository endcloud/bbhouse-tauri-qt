## Why

动态刷新图标混在分类行，特别关注头像跨次打开重复下载，mpv 播放未接入操作系统媒体控制。用户要求刷新移至标题文字右侧、头像缓存七天，并支持 macOS 与 Windows 系统媒体控制。

## What Changes

- 动态刷新图标移到页面标题文字右侧，间距 12px，加载指示位于按钮之后。
- 特别关注页头像使用持久化缓存，成功获取后七天内复用，过期异步更新并保留旧图作为失败回退。
- 播放器连接 macOS Now Playing/Remote Command 与 Windows SMTC，发布媒体标题、播放状态与时间，并响应系统播放/暂停、上一项/下一项和可用的定位操作。
- 使用用户提供的 noface.jpg 作为头像默认/加载失败占位，缓存和加载机制保持原样。
- 补齐 macOS 媒体控制中心封面，异步加载且过滤切播后的旧回应。
- 增加离线回归与双平台手测说明。

## Capabilities

### New Capabilities
- `system-media-controls`: 原生系统媒体会话与播放器双向同步。

### Modified Capabilities
- `dynamics-ui`: 刷新操作移到标题带。
- `special-follow-ui`: 特别关注头像七天缓存。

## Impact

影响动态 QML 布局、特别关注头像加载服务、PlayerController 与平台构建链接；不变更 B 站 API 或用户数据库结构。
