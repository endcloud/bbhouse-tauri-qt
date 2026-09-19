## Why

当前应用只支持在线播放，无法保存 DASH 音视频、弹幕和字幕，也没有可持久化、可检测的本地媒体库。需要在既有 Qt/QML 架构内提供下载和导入入口。

## What Changes

- 新建下载队列、独立 SQLite 下载记录库；保存基础资料、资源选择、任务状态和本地路径。
- aria2 下载 DASH 视频/音频，FFmpeg 无损封装或合并；系统 curl 下载 XML 弹幕和字幕 JSON，字幕转为 SRT。
- 设置新增下载二级区域，包含默认资源、下载目录和工具路径；默认使用系统用户下载目录。
- 主导航末尾新增下载管理，下载中与媒体库沿用卡片瀑布流，右上提供导入。
- 视频卡片右键及播放器截图按钮左侧提供下载入口。
- 内置播放器支持本地视频/音频和同名 XML 弹幕，不依赖 Cookie 或产生在线心跳。

## Capabilities

### New Capabilities
- `download-manager`: 下载任务、附件转换、独立数据库、设置、页面和本地播放。

### Modified Capabilities

## Impact

新增 Qt Core/Sql 下载服务及 QML 页面；复用 PlayerApi 和 libmpv。增加 aria2c、FFmpeg、系统 curl 运行时依赖。数据库沿用工程嵌入式 SQLite 技术栈，不引入服务端数据库。UI/UX 与真实在线下载由用户手测，代理完成离线回归与构建。
