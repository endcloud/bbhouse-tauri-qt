## Decisions

- 全屏边框处理限定在播放器窗口，通过 Windows 原生窗口样式保存/恢复，避免修改 FluentUI 全局窗口行为。
- 分 P 列表来自 `/x/player/pagelist`，只在 archive 视频会话中展示；PGC、直播和本地媒体不改变现有播放路径。
- 下载身份优先使用 `videoKey`，旧记录按业务和媒体 ID 兼容匹配；视频任务视为已包含音频。
- 更新检查只接受 HTTP 200、合法 JSON、较高整数 `code` 和 http/https URL，不阻断启动。
- 输入桥只在 Windows 使用 XInput 轮询按键边沿；应用级 Back/鼠标侧键发出统一返回信号，播放器窗口自行消费播放控制，避免引入额外 QML Gamepad 运行时依赖。

## Validation

直播录制使用原流压缩包封装，不进行二次编码。清空 `stream-record` 后 mpv 解复用线程异步写尾，后台最多等待 5 秒校验 MP4 顶层 box 长度、`ftyp/moov/mdat`；该校验不等于逐帧解码。切流、候选线路回退和应用正常退出均走统一收尾；异常杀进程或断电不在正常关闭保证范围内。离线回归需使用仍有未预读包的夹具，并可通过 `BBHOUSE_LIVE_TEST_FFMPEG` 启用 H.264/AAC MP4 真录制与双轨解码验证。

构建 `bbhouse-qt` 及相关离线测试；运行 `special-follow-page`、`online-history-controller`、`download`、`api-regression`、`live-player` 和 `player-runtime` 测试。Windows 全屏边框、手柄/鼠标侧键、直播录制及更新弹窗由用户在目标系统手测确认。
