## Context

代码审查（非猜测，逐文件读码确认）定位到的当前状态：

- `app/qml/MainWindow.qml`：`property var visitedPages: []` 只增不减，每个页面对应一个 `Loader { active: window.visitedPages.indexOf(key) !== -1 }`，全部 Loader 位于同一 `StackLayout` 内，首访后永久 `active: true`。文件自身注释："首访创建、切走保留、重选不重建"。
- `OnlineHistoryPage.qml` / `DynamicsPage.qml`：自绘 masonry（`Flickable > Item > Repeater`），`Repeater.model` 绑定到控制器的 `pool`（`OnlineHistoryController::pool_` 为 `QVariantList`，只 `append`；`DynamicsController::pool_` 同样只增，且 `reproject()` 把整个去重过滤后的 `items_` 灌进 `DynamicCardModel`，即 `Repeater` 渲染的仍是全量投影）。`Repeater` 不做回收，每个 model 条目对应一棵常驻 `HistoryCard` 子树（约 30–50 个 QQuickItem + 一张已缓存封面）。
- `dynamics-ui` 规格现状明文要求 "瀑布流 MUST 平铺全部已加载动态，MUST NOT 做虚拟滚动或窗口化"——经 `git log` 核实此句来自仓库初始化提交（WinUI 原型迁移基线），不是本项目针对某个已知虚拟化缺陷做出的主动决策。
- 同一代码库中已有两个成功的虚拟化先例可直接复用经验：
  - `PopularPage.qml`（`popular-ui`）：`GridView` + 单调递增的 `measuredCardHeight`（"同一选择只增高，避免短卡片进入缓存导致整表反复收缩"）+ 每个 cell 内 `HistoryCard` 顶部居中显示，允许矮卡片下方留白。
  - `DynamicCardModel`（`app/controllers/DynamicCardModel.h/.cpp`）：已经是一个正确实现的增量 `QAbstractListModel`（`setItems()` 按稳定 key 做 insert/remove/move/dataChanged diff），目前喂给的是非虚拟化的 `Repeater`，但模型本身已具备驱动虚拟化视图所需的增量语义，为满足 `dynamics-ui` 的"分区补查增量呈现"要求（局部更新不重建卡片）。
- 原播放器局部审查仅核对单 FBO 与 `DanmakuSpriteLayout` 硬上限 256 张/16MiB/单张 2MiB；`DanmakuEngine::updatePaintNode` 逐帧对 `present`/`root->nodes` 做差集回收离屏节点，媒体优化由独立change处理。

## Goals / Non-Goals

**Goals:**
- 让"在线历史""动态""本地历史"三个瀑布流页面的活动 QQuickItem 数量与视口无关（虚拟化），而非与已加载条目总数成正比。
- 让非当前页面不再无限期持有渲染内容，同时不违反各页面规格中"切走再返回 MUST 保持列表/游标/筛选/搜索"的既有约束。
- 为长会话下持续增长的条目池设置内存安全上限，不改变正常浏览路径下的可观察行为。

**Non-Goals:**
- `local-records-ui`（"本地记录"待播/历史页）在当前代码库未实现（`MainWindow.qml` 无对应导航项/Loader，`openspec/changes/archive/2026-09-18-migrate-local-records-ui/proposal.md` 明确标注该范围未完成）——不在本次渲染释放任务范围内，无需处理。
- `MainWindow.qml` 的 `space_loader`（个人空间二级页，`UserSpacePage`）沿用现状不变——同属"首访创建、不销毁"模式，但为单页浏览入口而非主要瀑布流页面，不是内存的主要来源，不在本次范围内。
- 不改动 `WatchlaterPage`/`SpecialFollowPage`/`BangumiPage` 的渲染方式——它们已通过服务端分页/替换式加载将活动卡片数天然限制在约 30–40 个以内，转虚拟化收益小、风险与工作量不成比例。
- 不改动 mpv 播放内核、弹幕引擎、下载管理器——审查未发现问题。
- 不改变任何网络协议、数据库 schema 或云端 API 调用频率/参数。
- 不追求真正的逐卡片 masonry 像素级紧密排布（该取舍在 `popular-ui` 已被接受）。

## Decisions

### 1. 逐页内联 `GridView`，复用 PopularPage 已验证的方案，不新增共享控件

`PopularPage.qml`（`popular` 综合热门档除外的其它档、以及 `popular` 档）已经用 `GridView` + 列宽/列数计算（`Math.max(1, Math.floor(width/316))`）+ 单调递增 `measuredCardHeight`（同一"选择"内只增高）+ `cacheBuffer: cellHeight` + delegate 内 `HistoryCard` 顶部居中，成功解决了虚拟化网格下变高度卡片的问题。`LocalHistoryPage`、`OnlineHistoryPage`、`DynamicsPage` 直接按该模式各自内联实现（不抽取新的共享 `.qml` 控件）：三处 `GridView` 结构相近但列宽、delegate 内 `HistoryCard` 的信号接线（`onAuthorClicked`/`onCoverClicked`/`onSeasonPlayRequested` 等）逐页不同，与现有 masonry 实现"各页各自内联、结构相似"的既有代码风格一致（`OnlineHistoryPage.qml`/`LocalHistoryPage.qml` 当前的 masonry 算法本就是各自内联的重复实现，不是共享组件）。

备选方案 A："抽取 `app/qml/controls/CardGridView.qml` 共享控件，通过 `Component` 属性或 `Loader` 注入每页的卡片内容与信号接线"——被否决：QML 里跨文件注入既要转发 `cardData` 又要转发差异化的信号（各页 `HistoryCard` 的 `onXxx` 处理器都不同），需要额外的 `Loader`/`Connections` 转发层，复杂度和维护成本超过直接内联三份相似代码；且与本仓库现有约定（相似瀑布流逻辑本就允许逐页内联，参见 `OnlineHistoryPage.qml`/`LocalHistoryPage.qml` 注释"与 XX 页同构"）相悖。
备选方案 B："保留自绘 masonry，仅对不可见区域做手工 delegate 复用"——被否决：重新发明 `GridView` 已提供的能力。

### 2. 条目池数据源：动态页绑定既有 C++ 模型，在线/本地历史沿用 PopularPage 的 JS 稳定投影模式

`DynamicsController::cardModel()` 已是正确实现的增量 `QAbstractListModel`（`DynamicCardModel::setItems` 按稳定 key 做 insert/remove/move/dataChanged diff），`DynamicsPage.qml` 的 `GridView.model` 直接绑定它即可，无需改动 `DynamicsController` 的数据结构。

`OnlineHistoryController`/`HistoryController`（本地历史）目前以 `QVariantList` 暴露条目池/当前页，两者形状与 `PopularController.pool` 一致；`PopularPage.qml` 已用纯 QML/JS 方案解决了"从 `QVariantList` 得到一个虚拟化友好、增量更新、保留 `contentY` 的模型"问题（`ListModel { dynamicRoles: true }` + `syncCards()` 按 `videoKey` 逐项 diff，见 `PopularPage.qml` 现有实现）。`OnlineHistoryPage.qml`/`LocalHistoryPage.qml` 复用同一 JS 模式，不需要新增 C++ 模型类。

备选方案："为 `OnlineHistoryController` 新增一个与 `DynamicCardModel` 同构的 C++ 增量模型类"——被否决：`PopularPage` 已证明纯 QML/JS 的 `ListModel + syncCards()` diff 方案足以达到同等效果（稳定 key 增量更新、`contentY` 保留），额外引入一个 C++ 模型类是不必要的重复实现，增加代码量与维护面而无实质收益。

### 3. 条目池内存安全上限：达到上限后丢弃最早追加的条目

在 `OnlineHistoryController::pool_`、`DynamicsController::pool_`、`PopularController` 综合热门累加器分别加一个远高于正常浏览量的上限（初定 2000 条，按会话观察可调）。超限后从最旧端裁剪。这是纯粹的极端会话托底，不是常规路径——虚拟化已经把"活动 QQuickItem 数量"与池大小解耦，池本身只是若干 `QVariantMap`/轻量 model 行，2000 条量级仍在几十 MB 内；上限只防御"应用开一整天、持续无限滚动"的病态场景。

已知权衡：若真的触发裁剪且用户随后向上滚动，会发现最早的条目消失（内容边界收缩）——这与"已加载列表 MUST 保持"存在字面张力，但仅在远超正常使用量的边界场景触发；已在对应 spec delta 的"超长会话池上限"场景中显式承认并限定适用范围。裁剪只发生在追加新条目之后。视图在模型变化前记忆稳定条目 key 与行内相对偏移，布局后恢复仍保留的可见条目；锚点已淘汰时落在保留窗口起点。硬上限与无限保留正在浏览的已淘汰条目无法同时满足，此边界必须显式记录并手测。

### 4. 非活动页面渲染释放:数据与渲染分离,而非整体销毁重建

`MainWindow.qml` 的 `Loader.active` 从"曾访问过即永久 true"改为"仅当前选中页为 true,其余为 false"。这会销毁非当前页的 QML 项树(含 `HistoryCard`/`GridView` delegate 与已解码封面),但不影响:

- 已经活在控制器单例(`OnlineHistoryController`/`DynamicsController`/`WatchlaterController`/`SpecialFollowController`/`BangumiController`/`PopularController`/`LiveController` 等)里的池、游标、筛选/分类/分区选择——这些 QObject 独立于 QML 页面生命周期,`Loader.active=false` 不影响它们。
- 后台任务(如本地历史同步)——同样挂在控制器/线程池,不挂在页面 Item 上。

需要额外处理的是**仅存在于页面本地 QML `property`、控制器未镜像**的状态,主要是滚动位置(`Flickable`/`GridView.contentY` 或分页页码之外的滚动偏移)。做法:页面在 `Component.onDestruction`(或 `Loader.onActiveChanged` 转 false 前)把 `contentY`/当前页码等写回其控制器的一个新增属性(如 `OnlineHistoryController::scrollOffset`),`Component.onCompleted` 时若控制器已有记忆值则据此恢复,而不是重新拉取或滚动到顶部。此模式统一应用到所有既有规格要求"切页返回需保持滚动"的页面。

具体每页需要迁移的本地属性以任务列表逐页核对落地(见 tasks.md),不在此处穷举以避免设计文档与实现细节脱节。

备选方案(已否决,见与用户确认记录):"切走即整体销毁数据,返回重新拉取"——直接违反 `app-navigation-shell` 及各页面规格中一大批"MUST 保持已加载列表/游标,MUST NOT 重新拉取首页"的既有要求,需要重写 8+ 个能力规格,且对云端 cursor 分页(在线历史/动态)而言"重新拉取"意味着重放整条游标链,体验明显变差。渲染与数据分离的方案在保留全部既有规格文本(仅 app-navigation-shell 新增一条渲染生命周期说明)的前提下达成同等内存收益。

### 5. `SeasonCard` 番剧竖版海报设置 `sourceSize`

沿用 `FluImage` 已有实际显示尺寸 × `min(Screen.devicePixelRatio, 2)` 的解码限制。原来的无上限覆盖在 DPR>2 时反而增加内存；修复恢复上限 2，DPR≤2 没有新增解码内存收益。不改变请求的 CDN URL(仍是 `@.webp` 只转格式不裁剪),`card-cover-thumbnails` 规格描述的"不缩放或裁剪图片"针对的是请求到的图片内容而非解码后内存驻留尺寸,故此项为纯实现优化,不需要 spec delta。

## Risks / Trade-offs

- [虚拟化网格与现有 masonry 视觉差异] 卡片不再严格按"最短列"紧密排布,矮卡片下方可能有留白 → 已在 `history-browser-ui`/`dynamics-ui` spec delta 中明确改为"统一行高取实测最大高度",且该取舍已在 `popular-ui` 页面被验证可接受;任务列表要求截图/手测对比三页(本地历史/在线历史/动态)视觉一致性。
- [`GridView` 切换可能影响滚动惯性、回到顶部按钮、触底续载判定等既有交互细节] → 逐页任务里保留原有"距底部最后三行触发续载"判定逻辑,只替换底层容器,不改变判定阈值;每步骤要求手测原有交互场景(参考各页规格的 Scenario 列表)。
- [渲染释放/重建引入的短暂重建停顿] 切回一个已离开较久、条目很多的页面时,重建 `GridView` 与其可见 delegate 需要一次同步布局,可能有轻微卡顿 → 可接受(远小于当前"整页常驻"的内存代价),任务列表要求在千级条目量下手测切页观感。
- [页面本地状态遗漏迁移导致静默丢失某个次要 UI 状态(如某个折叠状态)] → 通过专门的只读代码审查(逐页核对 QML 本地 `property` 与规格中"切走再返回 MUST 保持"的条目一一对应)先行定位,再进入实现;任务列表按页列出需要核对/迁移的具体状态项。
- [条目池裁剪与"已加载列表 MUST 保持"字面冲突的边界场景] → 已在 Decision 3 中限定为远超正常使用量的托底场景,并在对应 spec delta 中显式承认;不追求消除该边界情形,只保证正常使用永不触发。

## Migration Plan

无数据迁移。按 tasks.md 的独立可编译粒度顺序实施,每步 `cmake --build build` 0 error 后再进入下一步;新增/改动 QML 与 C++ 无持久化格式变更,不需要用户侧迁移或版本兼容处理。若某一步骤手测发现明显回归,可单独回退该步骤对应的提交,不影响之前已验证的步骤。

## 2026-09-21 审查后补齐

页面恢复使用初始化守卫区分回显与用户提交；在线/动态保存稳定身份和行内偏移，模型和卡片高度就绪后恢复；首次创建不会清空保留搜索。下载/关于通过主窗口 QtObject 保存小型导航状态，不持有渲染树。完整路由清单与允许丢弃的瞬态状态见交付文档。

既有失败排查纳入第8节可靠性修复：HistoryController 用可重试的互斥初始化替代本机异常后阻塞的 once_flag；偏好测试及时关闭 INI 句柄以允许 Windows 原子替换；离屏流行页测试显式选用系统字体目录。媒体直连回归的新版 curl 后端问题记录到独立媒体 change。

原 Context 是优化前快照，相关播放器排除判断已由真实杜比实测推翻，不作为当前归因。

## 2026-09-23 与远端生命周期基线整合

远端 `optimize-memory-lifecycle` 已先行合入 `ExpiringPageLoader`：离开页面后按设置的 1/5/10/30 分钟（默认 5 分钟）保留同一实例，超时后卸载页面并调用对应控制器 `releasePageCache()`。本 change 以远端为基线变基，第 4 节"仅当前页为 active、切走立即销毁"的单活跃 Loader 路由被该机制取代，不再单独实现。

保留下来的部分：三页虚拟化网格、池上限、控制器侧搜索/页码/滚动/锚点记忆、页面初始化回显守卫，以及主窗口为下载/关于保存的轻量 QtObject 状态。它们服务于"数据仍保留时的重建"（阈值调整、个人空间覆盖等），并由 `page-state-restore` 等真实 QML 重建回归覆盖。超时回收时，`releasePageCache()` 同步清除依附于被丢弃列表的搜索词、页码、滚动偏移与锚点，避免旧偏移套用到新数据；番剧档位、动态分类等偏好性选择不属于浏览缓存，继续保留。
