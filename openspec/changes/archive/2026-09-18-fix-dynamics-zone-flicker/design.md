## Context

根因链为 applyZoneResult → reproject → itemsChanged → Repeater QVariantList model 重置，网络本身不是重绘来源。当前 Qt 实现使用 view 的 aid 参数（视频 ID），无需改成 bvid。

## Decisions

- 保留原始 pool / items 快照契约，额外提供 CONSTANT cardModel 给卡片 Repeater。模型按稳定身份做局部通知，字段变更不 reset；筛选新命中只插入新增卡片。
- 模型身份优先为视频 aid，非视频为动态 id，保证去重组代表条目换为更早投稿时仍复用同一张视频卡。
- 分区集合单独维护，现有分区顺序不因另一条视频解析完成而重排；新增分区追加，消失分区移除。QML 使用 ListModel 保留已有按钮实例，“全部分区”为独立常驻按钮。
- 移动卡片时显式重新计算瀑布流位置；分区字段更新不触发布局清空。刷新成功先替换池再发布，不发空投影中间态。
- 增量模型和 QML Repeater 用离线 fixture 验证委托身份，真实 API 与实际 UI 观感由用户手测。

## Risks / Trade-offs

新分区使筛选行自然换行时内容区高度仍会变化，这属于正常布局；既有卡片不被重建。删除不再匹配的卡片和新增匹配卡片属于必要结构变更，不保留隐藏委托。
