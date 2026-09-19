# Tasks: migrate-video-playback-window

## 1. PlayerController

- [ ] 1.1 内核创建/销毁/降级标志 + kernelDead 非阻塞提示信号
- [ ] 1.2 起播管线:archive/PGC 分流,DASH 双直链 + durl 回落,鉴权头注入,候选链逐个尝试
- [ ] 1.3 续播三级 + >5s/距尾>10s 规则 + 播完落 0
- [ ] 1.4 位置写穿(5s/1s 节流 + 切播/关窗 flush)
- [ ] 1.5 会话播放列表(去重/切播/自动连播) + 清晰度保位换源 + 编码偏好持久化
- [ ] 1.6 倍速档位 + 保持开关;弹幕装载接线 + 开关持久化;权益判定信号

## 2. PlayerWindow.qml

- [ ] 2.1 FluWindow + MpvVideoItem 铺底 + 透明覆盖层(顶栏/底栏自绘)
- [ ] 2.2 控制栏呼出/自动隐藏/画面点击暂停;按钮 32px 命中区
- [ ] 2.3 播放列表面板(折叠/切播/当前高亮/瞄准)
- [ ] 2.4 键盘快捷键 + 缓冲指示 + 全屏切换
- [ ] 2.5 权益拒播 FluContentDialog + 移出列表
- [ ] 2.6 单实例复用 + 关窗清理序列(flush→stop→terminate→destroy)

## 3. 验证

- [ ] 3.1 CMake 注册 + 构建 0 error
- [ ] 3.2 冒烟:BBHOUSE_SMOKE_NAV 覆盖(player 窗口仅在交互后打开,冒烟验证窗口 QML 可装载)
- [ ] 3.3 validate + 归档 + commit
