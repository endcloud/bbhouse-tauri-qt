## Context

现有 Qt Quick 使用 FluentUI 与动态 libmpv，点播控制器绑定 aid/cid、播放列表、历史及心跳。直播房间使用独立 roomId 与无固定时长的 FLV/HLS 单流，不能伪造为 archive 条目。

## Goals / Non-Goals

目标：关注开播分页、独立搜索与可恢复状态，原生直播播放及有限线路回退。用户确认本次先完成列表和播放，不纳入实时弹幕。非目标：直播聊天、礼物、录制、回看、点播历史与 WebView 嵌入。

## Decisions

- `LiveApi` 共用 BilibiliApiClient 的 Cookie/信封校验/超时和显式直连，解析关注分页及房间 play_info 多层 stream/format/codec/url_info，保留服务端实际清晰度。
- `LiveController` 异步抓取分页，跨页按房间去重、过滤非开播；刷新失败保留旧列表，搜索只投影已加载条目并保留显式加载更多入口。
- 独立 `LivePlayerController` / `LivePlayerWindow`，复用 MpvClient/MpvVideoItem。每次重连重新读取凭据与取流，代数隔离过期请求；有限候选回退，超时和结束呈现可重试状态。
- mpv 通过 FFmpeg 原生支持 HTTP FLV/HLS，无需浏览器 MSE/flv.js。优先 AVC 与普通 HTTP FLV，再使用兼容 HLS；不更改点播内核选项。
- 不将 Cookie、签名流地址、真实关注列表打印或持久化；API 与媒体不继承区域代理。

- 用户首版手测基本 pass 后，直播窗口改为标准播放器同款顶部/底部渐变覆盖层，复用白色控制按钮，3 秒空闲自动隐藏，并在菜单、焦点、拖动、悬停与暂停/缓冲/错误时保持可见。视频占满内容区，中央失败提示独立于自动隐藏层。
- 主导航仅调整显示项目顺序为动态、番剧、直播、特别关注、稍后再看、在线历史、本地历史，设置和原分隔线位置语义保留；内部缓存路由键与 Loader 一一对应不变。

## Risks / Trade-offs

关注 API 与清晰度受登录状态、风控和房间实时状态影响，错误必须显式反馈。有限重试避免无限请求；原生实时播放延迟与 GPU 音画需要用户手测。首次交付无直播弹幕。

## Validation

离线 fixture 验证分页、状态过滤、长 ID、流格式/清晰度/候选；控制器验证旧回应、失败保旧、关闭及换房隔离。使用真实 libmpv 的本地流回归、CMake 构建、CTest 与无头 QML 装载；UI/真实直播由用户手测后归档。
