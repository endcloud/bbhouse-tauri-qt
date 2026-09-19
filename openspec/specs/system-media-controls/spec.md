# system-media-controls Specification

## Purpose
TBD - created by archiving change add-system-media-controls-and-avatar-cache. Update Purpose after archive.

## Requirements

### Requirement: 系统媒体会话

播放器 MUST 在 macOS 与 Windows 播放时建立原生系统媒体会话，发布当前标题、播放/暂停状态、时长及进度，倍速改变 MUST 同步。系统播放、暂停及切换播放状态命令 MUST 调用既有播放器控制；可用的上一项/下一项和进度定位 MUST 与当前会话列表及可定位状态一致。原生回调 MUST 安全转发到 Qt 主线程，不直接跨线程操纵 mpv。

#### Scenario: 系统播放暂停

- **WHEN** 媒体已加载且用户从系统媒体面板或系统转发的媒体键暂停或继续
- **THEN** 播放器与系统显示的状态同步，重复暂停或继续不会误切换成反向状态

#### Scenario: 进度与列表控制

- **WHEN** 系统提供定位或上一项/下一项操作且当前播放器允许该操作
- **THEN** 调用现有定位或列表切播逻辑，越界操作不生效，标题与时间切换到当前媒体

### Requirement: 媒体会话生命周期与平台兼容

媒体会话 MUST 随播放窗口关闭、最终失败或内核终止释放/失活并清除旧信息；切播 MUST NOT 向新条目应用旧命令。macOS MUST 使用系统 Now Playing/Remote Command 接口，Windows MUST 使用 SMTC 桌面互操作并兼容项目支持的 MinGW/MSVC。其他平台 MUST 能安全构建无操作后端；原生初始化失败 MUST NOT 阻断应用内播放，且 MUST 记录不含敏感信息的诊断。

#### Scenario: 关闭播放器

- **WHEN** 用户关闭播放窗口
- **THEN** 系统会话失活，旧媒体信息清除，后续旧回调不再触发播放

#### Scenario: 原生接口不可用

- **WHEN** 原生媒体会话初始化失败
- **THEN** 应用内播放继续正常工作，日志提供原因且不包含 Cookie 或签名媒体地址

### Requirement: macOS 媒体封面

macOS 系统媒体会话 MUST 使用当前播放条目的封面，异步获取并发布为 Now Playing artwork，不阻塞播放与主线程。会话切换、关闭或失活 MUST 清除旧封面并使旧请求失效；同一会话的常规进度更新 MUST NOT 重复下载封面。图片请求 MUST 不携带 B 站凭据，并校验响应与图片。无封面或加载失败时 MUST 保留文字和媒体控制且 MUST NOT 显示上一条封面。

#### Scenario: 封面发布与进度更新

- **WHEN** 当前媒体包含有效封面且异步请求成功
- **THEN** 系统媒体中心呈现该封面，后续播放进度更新仍保留封面并不重复下载

#### Scenario: 切播和关闭期间的旧封面

- **WHEN** 封面下载尚未完成时切换到新媒体或关闭播放器
- **THEN** 旧下载结果不会覆盖新条目的封面，也不会重新激活已关闭的媒体会话
