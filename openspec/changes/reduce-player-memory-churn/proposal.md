## Why

用户授权依据 2026-09-21 内存审查修复，并提供 BV1omoHYzEST 杜比用例。真实杜比片源已在无页面/弹幕/网络缓存的独立 OpenGL 管线重现约 1.7 GiB 私有提交量。这个主峰值需要继续研究解码与渲染互操作；当前可安全消除同内容换档重复下载、解析、上传弹幕的瞬时开销，并补齐白名单运行时诊断。

## What Changes

- 同内容、同 cid 和同账号上下文换清晰度时，复用已加载或正在加载的弹幕；使用独立内容代际隔离切集、关窗和晚到结果。
- 成功的空弹幕也复用；失败允许后续换档重试；只保留当前内容标识，不建立长期弹幕列表缓存。
- 增加默认关闭的结构化诊断，每 5 秒输出固定白名单中的解码方式、格式、尺寸、缓存、掉帧和 Windows 进程内存计数，不输出 Cookie、地址、标题、原生日志。
- 恢复新libmpv版本的媒体直连契约：关闭会读取环境代理的curl后端，兼容旧版并使用已显式直连的libavformat；保留真实HTTP/音轨候选回归。
- 扩展隔离工具，按显式 BV/凭据路径只读取得有限媒体前缀，记录真实杜比属性和 A/B 数据。
- 本 change 不切换 D3D11 呈现架构，不强制软解，不降低中间颜色精度/缩放滤镜，不承诺消除全部 1 GiB 增量。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `video-playback-window`：同内容弹幕资源复用与安全播放诊断。

## Impact

影响 PlayerController 在线弹幕请求生命周期、MpvClient 可选诊断、离线媒体测试与 tools/memory-review。字幕目录已通过 subtitleEpoch/subtitleCatalogRequested 复用，无需再次重写。播放器画质和解码默认参数保持原契约；真实原生 UI、HDR/DV 观感和连续换档由用户验收。
