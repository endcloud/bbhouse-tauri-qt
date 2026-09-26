## Why

> 2026-09-21审查限定：以下对播放器“不是主因”的表述仅为原页面优化提案的历史判断，不是完整内存剖析结论。本机Arc 140T离线4K Main10/OpenGL探针已在无页面/弹幕/网络缓存时复现GiB级占用。见[审查报告](../../../doc/内存优化审查与高画质内存分析.md)；播放器仍不在本change的实现范围，后续优化需另行提案。

Windows 11 上空闲即占用约 1.5GB、播放中逼近 2GB，远超 Qt Quick 应用应有水平,不如同类 WebView 方案。代码审查定位到两处根因:(1)`MainWindow.qml` 的页面路由让每个访问过的页面永久保留完整 QML 树("首访创建、切走保留、重选不重建"),多页面叠加后常驻内存持续累积;(2)在线历史与动态两个瀑布流页面用自绘 `Flow+Repeater` 直接铺开整个已加载条目池(动态页规格甚至明文要求"MUST NOT 做虚拟滚动或窗口化"),条目池又只增不减,深度滚动或长会话后单页即可驻留成百上千个完整卡片 QQuickItem 树。页面优化只处理渲染驻留和池边界；单 FBO、有限弹幕 CPU 缓存不能排除播放器高内存，真实杜比实测和媒体修复见独立 reduce-player-memory-churn。

## What Changes

- 在线历史、动态、本地历史三个瀑布流页面改为同构的内联虚拟化网格(复用流行页已验证的"按实测最大卡片高度定行高 + GridView 虚拟化"方案),仅在屏与缓冲区内的卡片保留为活动 QQuickItem,离屏卡片被回收。
- 动态页规格中"MUST NOT 做虚拟滚动或窗口化"的历史约束(WinUI 原型迁移遗留,非本项目主动决策)予以移除,改为要求虚拟化渲染且不得因虚拟化丢失"分区补查增量呈现"等既有局部更新行为。
- 在线历史/动态/流行(综合热门档)三个无限滚动条目池增加内存安全上限(远高于正常浏览量,仅托底极端长会话),超限后丢弃最早追加的条目;不改变到底判定、去重、搜索投影、刷新等现有行为的正常路径。
- `MainWindow.qml` 页面路由改为:非当前页面的 `Loader.active` 在切走后释放(销毁该页 QML 渲染树,包括卡片与已解码封面),仅当前页保持渲染;已由控制器单例持有的数据(条目池、游标、筛选/分类/分区选择等)不受影响、无需重新拉取。识别到目前仅由页面本地 QML 属性持有、控制器未镜像的状态(如滚动位置、部分页签选择),迁移为控制器可查询的状态或页面级持久记忆,以满足各页规格中"切走再返回 MUST 保持"的既有要求,不引入重新拉取或状态丢失。
- 番剧竖版海报封面（`SeasonCard.qml`）沿用 FluImage 的显示尺寸 × min(DPR, 2) 解码上限，修正 DPR>2 无上限覆盖;不改变请求的 CDN 地址与展示图片本身(实现细节,不涉及规格文字)。

## Capabilities

### New Capabilities
(无)

### Modified Capabilities
- `history-browser-ui`:"瀑布流卡片布局"由"卡片放入当前最短列"的逐列 masonry 改为虚拟化网格(统一行高取已测最大卡片高度),本地历史页渲染同步切换,分页与搜索行为不变。
- `online-history-ui`:渲染方式随 history-browser-ui 同步改为虚拟化网格(与本地历史保持一致);无限滚动条目池新增内存安全上限说明。
- `dynamics-ui`:移除"瀑布流 MUST 平铺、MUST NOT 虚拟化/窗口化"的既有要求与对应场景,改为虚拟化网格渲染要求,并保留"分区补查增量呈现"等既有局部更新契约;加载池新增内存安全上限说明。
- `popular-ui`:综合热门档的会话内累计条目池新增内存安全上限说明,渲染方式(已是虚拟化 GridView)不变。
- `app-navigation-shell`:新增"页面渲染内容可在切走后释放、数据状态由控制器保留"的显式约束,明确切页返回时不得重新拉取或丢失游标/筛选/滚动等既有规格要求,同时允许实现释放非当前页的渲染树以控制内存。

## Impact

- QML:三页各自内联虚拟化网格，复用布局模式;`OnlineHistoryPage.qml`、`DynamicsPage.qml`、`LocalHistoryPage.qml`、`MainWindow.qml`、`SeasonCard.qml` 改动。
- C++:`OnlineHistoryController`(QVariantList 池上限 + QML 稳定 ListModel 投影)、`DynamicsController`(池上限)、`PopularController`(综合热门池上限);动态沿用已有 `DynamicCardModel`，在线/本地历史沿用 JS ListModel。
- 不涉及数据库 schema、网络协议或已发布 API 变更;不影响 mpv 播放内核与弹幕引擎(媒体问题由独立 change 处理，不能据此排除其内存成本)。
- 验证:CMake 构建 0 error;现有 CTest(danmaku-scene/danmaku-sprite/dynamics-model 等)保持通过;新增覆盖虚拟化网格与页面渲染释放/恢复的测试;其余为用户手测(滚动流畅度、切页状态保持、内存占用前后对比)。
