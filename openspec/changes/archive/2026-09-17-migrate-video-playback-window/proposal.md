# Proposal: migrate-video-playback-window

## Why

播放窗口是本项目核心能力(契约见 `video-playback-window` 规格,22 需求)。原 WinUI 项目经多轮迭代收敛为"mpv 内核 + 自绘控制面板 + 会话播放列表"形态,本变更按其终态一次性迁移,跳过中间迭代(内置传输控件/DASH 之前的 durl 时代结构等)。PGC 权益判定与番剧剧集模式的需求同属该能力规格,PlayerApi 已含 PGC 端点,一并纳入本变更实现。

## What Changes

- `app/player/PlayerController.h/.cpp`:播放管线编排
  - 内核生命周期:MpvClient 创建(缺库降级标志)/销毁;kernelDead → 非阻塞提示(热替换重建列为后续增强)
  - 起播管线:cookie 读取 →(archive)PlayerApi::getFirstPage + getDashPlayUrl → loadDash(视频主文件+音频外挂轨,鉴权头注入);durl 回落(getPlayUrl fnval=1);(pgc)epid+cid 同传,epid 单独也支持;权益字段透传 UI
  - 续播三级:显式进度(条目携带) > 本地 playback_positions(>5s 且距片尾>10s) > 云端 progress > 0;自然播完落 0
  - 位置写穿:位置轮询 100ms;每 5s 且位移>1s 节流落库;切播/关窗 flush
  - 会话播放列表:QVariantList 增删切换;播完自动连播(末项停止);折叠由 QML 承担
  - 清晰度:档位表(from formats,needVip 标记)+保位换源;编码偏好(avc/hevc/av1)持久化
  - 倍速 0.5–3.0;保持开关(规格 persist-playback-speed:App.Player.KeepSpeed/Speed)
  - 弹幕接线:getDanmakuXml→DanmakuParser→DanmakuEngine.loadEntries;开关持久化 App.Player.DanmakuOn(默认开)
  - 截图:mpv screenshot-to-file(按规格 ≤5MB 约束后续精化)
- `app/qml/PlayerWindow.qml`:独立 FluWindow(1200×720)
  - MpvVideoItem 全区铺底 + 透明覆盖层:顶栏(标题/清晰度/弹幕开关/倍速/截图/关闭)+ 底栏(播放暂停/进度条/时间/音量/全屏);3s 无指针自动隐藏,移动/点击呼出,点击画面切换播放暂停
  - 右侧会话播放列表 300px 可折叠;点击切播
  - 键盘:Space/←→/↑↓/F/Esc/D;缓冲指示 FluProgressRing(paused-for-cache)
  - 全屏:visibility=FullScreen;Esc/F 退出
  - 权益拒播:is_preview/!has_paid → FluContentDialog 提示(试看/会员文案),移出列表
- Main.qml 路由增加 "/player";单实例语义:PlayerController 持有现有窗口复用
- 无规格 delta(实现既有能力规格;持久化键名见 tasks)

## Impact

- Affected specs: 无修改
- Affected code: app/player/PlayerController.*、app/qml/PlayerWindow.qml、Main.qml、CMake、translations
- 明确延后(记录于 tasks 勾选区):相邻条目预解析、内核热替换原地重建、CC 字幕、番剧剧集模式(待番剧页)、标题栏主题自适应细节
