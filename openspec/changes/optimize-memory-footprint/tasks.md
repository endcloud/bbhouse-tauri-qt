## 1. 在线历史：虚拟化网格与内存上限

> 2026-09-21 审查：原有勾选保留为实施历史，不代表本轮验收通过。第 8 节列出的实际缺口需补齐后再验收；尤其原 6.7 所称的 Popular/Live 页面接线未落地，6.8 关于 Component.onCompleted 仅执行一次的解释错误。详见[审查报告](../../../doc/内存优化审查与高画质内存分析.md)。

- [x] 1.1 `OnlineHistoryController` 池新增内存安全上限（2000 条，超出丢弃最早追加条目，去重 key 集合同步清理），不改变到底判定/去重/刷新路径；补单元测试覆盖"超限后仍可续载且不重复计入已丢弃条目"。`cmake --build build` 0 error。（新增 `online-history-controller-test`，10/10 断言通过；同时顺带完成任务 6.1 的 `ensureLoaded()` 守卫，一并在此测试覆盖。）
- [x] 1.2 `OnlineHistoryController` 新增滚动位置记忆属性（如 `scrollOffset`），供页面在渲染释放前写入、重建后读取（本任务只加属性与存取，不改 QML 接线）。
- [x] 1.3 `OnlineHistoryPage.qml` 的 `Flickable+Repeater` masonry 替换为内联 `GridView`（列宽/列数计算、单调递增 `measuredCardHeight`、`cacheBuffer: cellHeight`、delegate 内 `HistoryCard` 顶部居中，参照 `PopularPage.qml` 现有实现），数据源改用 `ListModel { dynamicRoles: true }` + 按 `videoKey` diff 的 `syncCards()`（同样参照 `PopularPage.qml`），保留"距底部最后三行触发续载""回到顶部按钮""刷新重置""搜索投影"既有行为；同时接入 `ensureLoaded()`（任务 6.1）与控制器侧 `searchText`/`scrollOffset` 回显（任务 6 的一部分，提前在此完成，避免二次改动同一文件）。`cmake --build build` 0 error，`BBHOUSE_SMOKE=1 BBHOUSE_SMOKE_NAV=OnlineHistoryPage.qml` 离屏冒烟通过（无 QML 错误）。
- [ ] 1.4 手测：滚动数百至上千条历史（真实账号或构造大量本地 fixture），确认无白块、列数随窗口自适应、矮卡片不裁切、续载/到底/搜索/刷新与变更前行为一致；并核对切走再切回时列表/游标/搜索词/滚动位置保持、不重新拉取首页（依赖任务 6.9 的 MainWindow 路由改动一并生效后才能完整验证，可先行核对页面自身行为无回归）。

## 2. 动态页：虚拟化网格与内存上限

- [x] 2.1 `DynamicsController` 池新增内存安全上限（2000 条，超出丢弃最早追加条目及其去重分组记录），复用现有 `DynamicCardModel`（不新增模型类）；扩展或新增单元测试覆盖"超限后去重分组与投影仍正确"。`cmake --build build` 0 error。（`dynamics-controller-test` 新增 5 项断言：池上限裁剪、按 pubTs 丢弃最旧、去重 key/出现记录同步释放、释放后可重新收录、scrollOffset 去抖；全部通过。）
- [x] 2.2 `DynamicsController` 新增滚动位置记忆属性（同任务 1.2 模式）。
- [x] 2.3 `DynamicsPage.qml` 的 `Flow+Repeater` 替换为内联 `GridView`（同任务 1.3 的网格/测高模式）直接绑定既有 `DynamicsController.cardModel`（C++ 增量模型，无需 `syncCards()`）；`dynamics-ui` 规格中"分区补查增量呈现"要求局部更新不重建卡片——验证方式：现有 `dynamics-model` CTest 全部通过且新增用例覆盖"虚拟化视图下分区补查增量更新不重建可见卡片"。`cmake --build build` 0 error。（未改动 `DynamicCardModel`/`reproject()` 的增量更新粒度，`dynamics-model`/`dynamics-controller` 既有断言"分区解析仅 dataChanged 更新对应行、不重建投影"继续覆盖该契约，GridView 按标准 Qt Quick 语义响应 dataChanged 仅刷新受影响 delegate；顺带修复 `Component.onCompleted` 覆盖控制器 `zoneFilter`/`searchText` 的 bug（任务 6.3）。`BBHOUSE_SMOKE_NAV` 离屏冒烟通过，`dynamics-model`/`dynamics-controller` CTest 全部通过。）
- [ ] 2.4 手测：六档分类筛选、分区筛选、搜索叠加、刷新、稀疏筛选自动续拉、超长会话滚动，确认虚拟化后以上行为与变更前一致，无白块与卡顿；并核对切走再切回时列表/offset/筛选/分区/搜索词/滚动位置保持、不重新拉取首页（依赖任务 6.9 一并生效后完整验证）。

## 3. 本地历史页统一渲染（视觉一致性，不改数据层）

- [x] 3.1 `LocalHistoryPage.qml` 的自绘 masonry 替换为内联 `GridView`（同任务 1.3 模式，数据源用同样的 `ListModel + syncCards()`）；数据仍走既有 30 条/页 SQLite 分页,`HistoryController` 的数据加载逻辑不改动。`cmake --build build` 0 error。（顺带完成任务 6.2:新增 `HistoryController.lastRequestedPage`/`searchText` 属性,`Component.onCompleted` 改为请求记忆页码而非硬编码 1,`searchQuery` 属性初始化改为绑定 `HistoryController.searchText`。`BBHOUSE_SMOKE_NAV` 离屏冒烟通过。运行 `history-controller-test` 时发现一项与本次改动无关的既存失败:`failed initialization can be retried without restarting` 断言 FAIL 并抛出 `start sync run: Parameter count mismatch` 异常终止进程;已用 `git stash` 验证该失败在改动前的基线代码上同样存在,非本次改动引入,不在本次范围内修复,但记录于此供后续关注。）
- [ ] 3.2 手测三页并列对比（本地历史/在线历史/动态）：列宽、列间距、行高取最大卡片高度、整体居中的视觉表现一致，符合 `history-browser-ui`"瀑布流卡片布局"新描述。

## 4. 流行页池上限

- [x] 4.1 `PopularController` 综合热门累加器新增内存安全上限（2000 条，超出丢弃最早追加条目），不改变现有 `GridView` 渲染与去重/搜索逻辑；补或扩展单元测试。`cmake --build build` 0 error。（`PopularController` 每个 State 的 `items` 在 `finishFetch` 累加后超出 `kMaxPoolSize` 时从头部裁剪保留最近条目；`popular-controller-test` 新增 3 项断言：超限裁剪保留最新、续载时维持上限滑动窗口、刷新重置池大小。）

## 5. 番剧竖版封面解码尺寸

- [x] 5.1 `SeasonCard.qml` 的 `FluImage` 按显示尺寸与 `Screen.devicePixelRatio` 设置 `sourceSize`，不改 CDN 请求地址；`cmake --build build` 0 error。（修复后使用 `width/height * min(devicePixelRatio, 2)`，与 FluImage 已有上限一致；DPR≤2 不宣称新增解码收益，DPR>2 避免无上限覆盖。）
- [ ] 5.2 手测番剧页竖版海报清晰度无肉眼可见劣化（对比变更前后截图）。

## 6. 页面渲染释放（MainWindow 路由）

只读代码审查（已完成，逐文件核对页面本地 QML `property` 与对应控制器 `Q_PROPERTY` 的对应关系）确认：`local-records-ui`（"本地记录"待播/历史页）在当前代码库中未实现（`MainWindow.qml` 无 `records` 导航项/Loader），不在本次范围内。以下任务按审查发现的具体问题逐一修复，是切到单活跃 Loader 前的**前置条件**（不先修，切换后会静默违反对应规格的"切走再返回 MUST 保持"要求）：

- [x] 6.1 `OnlineHistoryController` 新增 `ensureLoaded()`/已加载状态守卫（当前 `OnlineHistoryPage.qml` 的 `Component.onCompleted` 无条件调用 `OnlineHistoryController.refresh()`，该调用会清空 `pool_` 并重新拉取首页；只因页面从未被销毁过才未暴露）；`OnlineHistoryPage.qml` 改为按守卫条件调用。`cmake --build build` 0 error；单元测试覆盖"已有数据时 `ensureLoaded()` 不清空重拉"。（已随任务 1.1/1.3 一并完成：控制器侧 `ensureLoaded()`+单测，`OnlineHistoryPage.qml` 的 `Component.onCompleted` 调用点，以及顺带补齐的 `searchText`/`scrollOffset` 控制器属性与页面接线。）
- [x] 6.2 本地历史页码状态迁移出页面本地作用域：`LocalHistoryPage.qml` 的 `pageIndex`/`pageItems`/`totalItemCount`/`initialized` 目前无任何控制器侧对应，且 `Component.onCompleted` 硬编码 `HistoryController.loadPage(1)`（当前被同文件另一处 `onVisibleChanged` 处理器掩盖，页面销毁重建后该掩盖失效）。新增可查询的"当前页码"记忆点（`HistoryController` 新增属性，或专门的页面状态存储），`Component.onCompleted` 改为请求记忆的页码而非硬编码 1。`cmake --build build` 0 error；手测：浏览到第 3 页、切走再切回，确认停留第 3 页而非跳回第 1 页。（已随任务 3.1 一并完成:`pageIndex`/`pageItems`/`totalItemCount` 本身无需迁移——它们在 `onPageLoaded` 信号回调中重建,只要 `Component.onCompleted` 请求正确页码即可全部正确重算,故只新增了 `lastRequestedPage` 一个记忆点。）
- [x] 6.3 `DynamicsPage.qml` 的 `Component.onCompleted`（约 540-541 行）把页面本地已重置为默认值的 `page.zoneFilter`/`page.queryLower` 无条件写入 `DynamicsController.zoneFilter`/`searchText`，会覆盖控制器原本正确保留的值；改为页面创建时从控制器读取回显（而非写入覆盖）。`cmake --build build` 0 error；手测：设置分区筛选与搜索词、切走再切回，两者均保持。（已随任务 2.3 一并完成：`searchQuery`/`zoneFilter` 属性初始化改为绑定 `DynamicsController.searchText`/`zoneFilter`，`Component.onCompleted` 移除覆盖写入；顺带修正 `searchText` 此前被写入小写投影值而非原始输入的既存不一致。）
- [x] 6.4 `BangumiPage.qml` 同类问题：`Component.onCompleted` 把重置后的 `page.searchQuery` 写入 `BangumiController.setRegionalSearch(...)`，覆盖控制器保留的港澳台搜索词；且 `currentTab`（动画/国创/电影/纪录片五档细分）当前完全无控制器归宿。`BangumiController` 新增细分档位记忆属性，页面创建时从控制器回显而非覆盖写入。`cmake --build build` 0 error；手测：选中"国创"细分档 + 港澳台搜索词、切走再切回，均保持。（`BangumiController` 新增 `currentTab`/`regionalSearch` 属性与 `setCurrentTab()` setter；`BangumiPage.qml` 的 `currentTab`/`searchQuery` 属性初始化改为绑定控制器值，`Component.onCompleted` 移除 `setRegionalSearch(page.searchQuery)` 覆盖，`selectTab()` 调用 `setCurrentTab()` 同步记忆。）
- [x] 6.5 `WatchlaterController` 新增 `searchText`/`pageIndex` 属性（`WatchlaterPage.qml` 当前两者均为纯页面本地 `property`，无任何控制器侧记忆）；`WatchlaterPage.qml` 接线读写。`cmake --build build` 0 error；手测：提交搜索词并翻页、切走再切回，词与页码均保持。（`WatchlaterController` 新增两个属性与 setter；`WatchlaterPage.qml` 的 `searchQuery`/`pageIndex` 属性初始化改为绑定控制器值，`onSearchQueryChanged` 调用 `setSearchText()`，`selectPage()` 调用 `setPageIndex()` 同步记忆。）
- [x] 6.6 `SpecialFollowController` 新增 `searchText`/`currentTab`（投稿/合集/专栏）属性（`SpecialFollowPage.qml` 当前两者均为纯页面本地 `property`）；`SpecialFollowPage.qml` 接线读写。展开中的合集（`expandedSeason` 等）视为尽力保持项，不强制在本任务内做控制器持久化（切走再回来若已收起属可接受行为，非既有 Scenario 明确覆盖）。`cmake --build build` 0 error；手测：切到"专栏"分块并提交搜索词、切走再切回，分块与搜索词均保持。（`SpecialFollowController` 新增两个属性与 setter；`SpecialFollowPage.qml` 的 `searchQuery`/`currentTab` 属性初始化改为绑定控制器值，`onSearchQueryChanged` 调用 `setSearchText()`，`selectTab()`/`resetView()` 调用对应 setter 同步记忆。）
- [x] 6.7 `PopularController` 新增 `searchText`（+ 非综合热门档的 `pageIndex`）属性；`LiveController` 新增 `searchText` 属性。这两页是 `app-navigation-shell` 规格中唯二明确用 SHALL 要求"滚动位置"必须恢复（而非其余页面的尽力而为）的页面，因此额外新增真实的滚动位置记忆属性（而非仅在数据更新时临时保留 `contentY`），页面渲染释放前写入、重建后据此定位。`cmake --build build` 0 error；手测：流行页与直播页分别提交搜索词、滚动到中部、切走再切回，搜索词与滚动位置均恢复。（`PopularController` 新增 `searchText`/`scrollOffset` 属性与 setter（含 0.5px 去抖）；`LiveController` 新增同样的两个属性与 setter。页面 QML 接线在任务 6.9 MainWindow 切换为单活跃 Loader 后一并完成，避免二次改动。）
- [x] 6.8 核对 `DownloadsPage.qml` 内嵌 `Component.onCompleted`（`showingLibrary = true` 当 `LoginController.needsLogin`）在页面重建下是否幂等安全；如有同类"仅期望运行一次"的隐患一并修正。`cmake --build build` 0 error。（审查后已修复：每次重建都会执行生命周期回调。默认媒体库仅在无保存状态的首次创建使用，后续从主窗口轻量状态恢复；真实页面重建回归通过。）
- [x] 6.9 `MainWindow.qml` 全部页面 `Loader` 的 `active:` 绑定从 `window.visitedPages.indexOf(key) !== -1` 改为 `window.currentPage === key`（仅当前页渲染，切走销毁）；`visitedPages` 相关的登录后刷新判断（约 89 行，"动态页是否已访问过"）改为不依赖 `visitedPages` 语义变化的等价判断（如直接查 `DynamicsController.pool.length > 0`）；`pageKeys`/`activeSearchPage`/`StackLayout.currentIndex` 逻辑不变。`cmake --build build` 0 error，`BBHOUSE_SMOKE=1` 离屏冒烟逐页通过（`smokeNavigate()` 遍历全部页面不产生 QML 错误）。（所有 Loader 的 active 改为 `window.currentPage === key`；visitPage() 移除 visitedPages 追加逻辑；LoginController.onAuthenticated 改用 `DynamicsController.pool.length > 0` 判断是否已加载；恢复 activeSearchPage 计算逻辑。）
- [ ] 6.10 手测覆盖 `app-navigation-shell` 规格中逐页"切页返回状态保持"场景（在线历史/动态/特别关注/番剧/稍后再看/直播/流行/本地历史各一项，参照对应能力规格的 Scenario 列表逐条核对），确认游标、筛选、搜索词、滚动位置均按预期恢复，且切回时不发起多余网络请求（可用现有网络请求日志核对，尤其复核 6.1/6.2 修复的两处曾经会静默重拉的路径）。
- [ ] 6.11 手测覆盖"本地历史同步进行中切走再切回"场景（`app-navigation-shell` 既有 Scenario），确认后台同步不因渲染释放中断。

## 7. 整体验证

- [x] 7.1 运行完整 CTest 套件（含 `danmaku-scene`/`danmaku-sprite`/`dynamics-model`/`store`/`api`/`player-runtime` 等既有用例），全部通过。
- [ ] 7.2 Windows 手测内存对比：冷启动、浏览完在线历史/动态各至少 500 条并切换全部导航页一轮后、播放视频中，分别在任务管理器记录常驻内存，与变更前基线对比，确认有显著下降（目标：空闲不超过原先的一半量级，具体数字记入交付说明文档，不作为硬性门槛写入规格）。
- [x] 7.3 更新 `doc/` 下交付说明文档，记录本次内存优化的验证结果与已知边界（如超长会话池裁剪的适用范围），遵循仓库既有交付说明文档惯例。

## 8. 2026-09-21 apply 审查后必须补齐

- [x] 8.1 补齐 Popular/Live 搜索、分页、滚动恢复；为全部受 Loader 销毁影响的路由建立状态清单（含下载/关于），增加真实 QML 销毁重建的非交互回归。
- [x] 8.2 动态读取并恢复滚动锚点；在线历史在模型布局完成后恢复，避免旧 contentHeight 将偏移夹为 0；覆盖变列数与初始搜索回显。
- [x] 8.3 动态池按最早追加顺序淘汰，与发布时间排序解耦；补发布时间递减的多页 fixture，确保超过2000条仍呈现新续载内容、游标与投影一致，并验证裁剪视口稳定性。
- [x] 8.4 番剧和稍后再看初始化分页控件至控制器记忆页码，覆盖已有数据且不重发 pageInfoChanged 的重建场景。
- [x] 8.5 澄清 SeasonCard 与 FluImage 已有 sourceSize 限制的关系，明确 DPR>2 的内存/清晰度取舍；同步 proposal/design/SUMMARY 中不准确的完成度与收益说明。
- [x] 8.6 补少量匹配不足一屏的续载、裁剪后锚点、动态分区增量更新的视图回归；核对 Popular 裁剪仅限综合热门。
- [ ] 8.7 修复/解释现存四项 CTest 失败后重跑完整回归，完成原有 UI 手测和内存测量项。当前38/42通过不能勾选7.1。

审查证据：完整构建成功；严格规格校验通过；动态降序分页控制器探针暴露新页被裁掉；Qt离屏机制探针确认模型布局前恢复偏移被夹到0。媒体管线高画质预算属于另一个问题，探索结果见报告，尚未改变本 change 的播放器非目标边界。

## 9. 本轮修复交付（2026-09-21）

用户已明确授权按审查结论修复。第8节实施项现已补齐，完整路由清单、实际QML重建/裁剪/稀疏投影测试以及原四项失败排查见[交付与手测](../../../doc/内存优化修复与杜比用例验证.md)。第1—6节保留历史验证说明；其中较早的“页面接线稍后完成”“按发布时间裁剪”不代表修复后的实现。

8.7 包含真实UI/内存验收，故继续保留未勾选；自动化结果按交付文档单列。页面/媒体change均不因构建通过自动归档。真实杜比1GiB级台阶仍未消除，不在本change范围内冒充修复。

最终完整CTest 46/46通过（110.72秒）；中途preferences一次间歇失败及随后连续三次/全套复验情况如实记录在交付文档，不放宽断言。

## 9. 2026-09-23 以远端生命周期基线变基

- [x] 9.1 以远端 `ExpiringPageLoader` + `pageCacheMinutes` 取代任务 6.9 的单活跃 Loader；下载/关于轻量状态接入限时 Loader，页面状态记忆叠加在远端路由之上。
- [x] 9.2 各控制器 `releasePageCache()` 同步清除搜索/页码/滚动/锚点记忆，并在动态/流行/直播回归中断言；app-navigation-shell delta 改为与"后台页面限时回收"一致的重建状态要求。
- [ ] 9.3 用户手测：阈值内往返保持同一实例；超时后返回从首屏重建；下载/关于轻量状态与后台下载/同步不受影响。
