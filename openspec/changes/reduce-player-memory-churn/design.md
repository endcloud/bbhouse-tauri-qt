## Context

此前 loadDanmaku 将视频 loadfile 的 generation 当作弹幕生命周期。每次换档都会请求 XML；若旧请求仍在路上，结果因 generation 改变被丢弃，再启动新请求。字幕已有独立内容 epoch，审查报告对此需补充澄清。

## Goals / Non-Goals

目标：消除同内容重复弹幕工作，保留切播隔离与失败重试，使真实媒体内存具有不泄漏凭据的可观察数据。

非目标：凭小样本更换渲染架构、压缩画质、宣称所有高画质资源均为泄漏。

## Decisions

1. OnlineDanmakuLoader 独立于视频 generation。cid 与 Cookie 的 SHA-256 摘要共同标识当前请求，pending 和成功状态均去重；不保留额外解析结果副本。
2. 新内容、移除当前条目、关窗立即 reset，递增 epoch 并丢弃排队任务。worker 使用单线程专用池，析构等待完成；晚到结果在主线程检查 epoch。原始 XML/解析中间结构在排队交付前离开作用域。
3. 失败清除去重标识但仍以 epoch 隔离，下一次用户换档可重试；同内容首次尚未完成的请求允许跨画质 generation 交付。
4. 诊断通过 `BBHOUSE_PLAYER_DIAGNOSTICS=1` 显式启用，仅固定白名单内字段。PrivateUsage 是私有提交量，WorkingSetSize 是整个工作集，不能等同于任务管理器“进程”页的活动私有工作集，也不能与显存相加。
5. 真实用例通过 API GET 和带 Range 的 CDN GET 读取前缀，不启动产品播放控制器，不发心跳、不接触历史库、不保存签名直链。探针仅播放本地前缀，网络缓存关闭。

## Risks / Trade-offs

- 弹幕复用意味着同一内容会话内不会仅因切清晰度而刷新新发送弹幕，这与清晰度操作职责一致；重新打开内容会重新加载。
- 未中断正在执行的 HTTP，请求受现有超时限制；析构等待专用池完成以避免使用已析构对象。
- 单次离屏掉帧不能代表最终屏幕体验；真实 BV 已确认 DV profile 8/RPU，但不构成 HDR 输出色准验收。

## Validation

离线真实 worker/队列/XML parser 测试覆盖 pending 去重、成功去重、切内容隔离、账号变化、失败重试和空结果；既有原生视频回归保留。杜比探针对照同 handle/context 的 1080P→杜比→1080P；完整数值写入交付报告。

## 同轮媒体直连回归修复

真实本地HTTP测试确认当前libmpv curl后端会在空http-proxy时继续采用环境代理；仅有stream-lavf-o无法约束该后端。初始化时在支持curl-enabled的版本关闭该后端，使用已配置http_proxy为空的libavformat。旧版MPV_ERROR_OPTION_NOT_FOUND兼容跳过；未修改系统/进程代理。保留原本“媒体必须直连”的能力契约，真实音轨回退/超时/取消与代理陷阱回归验证。
