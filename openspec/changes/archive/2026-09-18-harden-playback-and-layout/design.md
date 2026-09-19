## Context

参见 proposal.md。基线已含 macOS Qt 6.11.2 移植；源码仍残留 Windows 路径与若干原型常量，规格与实现存在偏差。参考源实际在 /Users/ziyu/Documents/b3；仓库中的 b3 是失效的 Windows 链接，b3的替身为 Finder 别名文件。

## Goals / Non-Goals

**Goals:** 保持现有 Qt/QML 架构，以可复现回归修复起播、生命周期、数据标识和布局，留下后续维护索引。

**Non-Goals:** 不代替用户进行 UI/UX 手测，不引入播放器框架替换，不实施尚未迁移的爬虫服务/本地记录功能。

## Decisions

- mpv 格式常量直接取官方 client.h，避免重复定义产生 ABI 错误；渲染释放以持有资源生命周期保证顺序，替代固定延时推测。
- 播放入口统一归一化历史 rawJson 与业务 ID，QML 中 ID 使用 double/var 避免 32 位 int 溢出。进度恢复按规格优先本地持久化。
- 每轮解析捕获清晰度/编码快照；切换时取消候选 watchdog 并通过 generation 丢弃旧结果。
- 保留 Qt6 MultiEffect，将头像 mask 恢复连续 alpha；各页只由一层负责外边距，内部布局使用实际内容宽度。
- API 索引写入 doc，供后续会话持续检索；离线 fixture 测试与联网只读探针分离。

## Risks / Trade-offs

- B 站接口可能风控或内容失效 → 区分 HTTP、业务码与媒体失败，保留上下文；不以一次成功推断全部内容可播。
- macOS 构建不证明 Windows 效果 → 提供跨平台手测矩阵。
- 多模块并行修复 → 文件责任分离，主代理统一构建、回归和提交。
