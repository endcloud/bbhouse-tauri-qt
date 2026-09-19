# Qt 迁移实现约定(供后续变更/代理遵循)

> 本文档是 bbhouse-qt 各 UI 变更的公共架构约定。原 WinUI 语义 → Qt/QML 的映射在此固化,
> 避免各代理自行发明。

## 目录布局

```
app/
  main.cpp                 入口(勿轻易改)
  core/                    数据层:API 管线 + 端点 + HistoryStore + SyncRunner(UI 无关)
  controllers/             QML 桥接层:AppController / HistoryController / 各页 Controller
  player/                  播放域:MpvLib + MpvClient + 弹幕引擎(DanmakuEngine)
  qml/
    Main.qml               FluLauncher + FluRouter 路由(勿改结构)
    MainWindow.qml         FluWindow + FluNavigationView 外壳
    pages/                 各页面(LocalHistoryPage.qml ...)
    controls/              复用控件(HistoryCard.qml / CoverPreviewOverlay.qml ...)
    js/                    纯 JS 工具(formatTime 等)
  translations/bbhouse_en_US.ts   en-US 翻译(zh-CN 为 qsTr 源文案,免条目)
```

## 线程与数据流约定

- **数据层全部阻塞式**,只允许在非 UI 线程调用。Controller 层用 `QThreadPool::globalInstance()->start(...)` 包裹,结果经 `QMetaObject::invokeMethod(this, signal, Qt::QueuedConnection)` 回主线程。
- Controller 对 QML 暴露:Q_PROPERTY(状态) + Q_INVOKABLE(动作) + 信号(结果)。列表数据以 `QVariantList`(每项 QVariantMap)传递,键名与 HistoryItem 字段一致(videoKey/title/subtitle/coverUrl/authorName/viewAt/progress/duration/badge/linkUrl/viewCount/rawJson/business/oid/kid)。
- Controller 单例经 `qmlRegisterSingletonInstance` 或 rootContext 属性暴露;名字与 WinUI 页面对应(如 `HistoryController`、`AppController`)。
- 耗时初始化(store.initialize、cookie 预检)启动时在后台线程做,UI 显示就绪状态。

## QML 控件映射(WinUI → FluentUI 1.7.7)

| WinUI | FluentUI |
| --- | --- |
| Window/标题栏 | FluWindow(自带 frameless;`appBar`/`title` 由 FluWindow 提供) |
| NavigationView | FluNavigationView(Left 折叠窄轨;FluPaneItem 各菜单) |
| 瀑布流 MasonryPanel | FluStaggeredLayout(查明 API 后用;或 Flow+列宽计算自绘) |
| ListView(分页列表) | ListView + FluPagination(本地历史 30 条/页标准分页) |
| ContentDialog | FluContentDialog(FluApp 的 dialog 体系) |
| InfoBar | FluInfoBar |
| MenuFlyout | FluMenu |
| SelectorBar/RadioButton 组 | FluRadioButtons 或 FluToggleButton 组(动态页七档筛选用后者) |
| ProgressRing/Slider/ToggleSwitch | FluProgressRing / FluSlider / FluToggleSwitch |
| 悬浮按钮(FAB) | FluIconButton + FluShadow 或 FluFilledButton 圆角 |
| ToolTip | FluTooltip |
| 卡片 | FluFrame(圆角+悬浮态) |

- 图标:WinUI Segoe MDL2 glyph(F0E2 等)→ FluentUI `FluentIcon.type`(查 `3rd/FluentUI/FluentIconDef.h` 的 enum;或 FluIcon.text 直接给 glyph 字符)。
- 所有列表滚动容器用 FluentUI 的 FluScrollBar。

## i18n 约定

- QML 全部文案 `qsTr("中文")`;zh-CN 即源语言零条目;en 在 `app/translations/bbhouse_en_US.ts` 补 `<message>`(context = 文件名去 .qml);改完 .ts 由 CMake lrelease 自动出 .qm。
- C++ 用户可见文案:`Loc::get("中文")`(context "core"),同样补进 .ts 的 `<name>core</name>`。
- 新增键需同时补 .ts 条目,不接受的裸字符串:禁止。

## 代码风格

- C++17/Qt6;成员 `camelCase_` 尾下划线;Q_PROPERTY READ+NOTIFY;信号 `xxxChanged`;头文件 include guard(与现有一致)。
- QML:2 空格缩进;id 小驼峰;属性块空行分组;信号处理器 onXxx。
- 注释:解释"为什么"(口径/坑/实测偏差),不复述代码。

## 冒烟验证(代理侧完成标准)

1. `cmake --build build` 0 error;
2. `BBHOUSE_SMOKE=1 QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 build/bin/bbhouse-qt.exe` 无 QML 错误(加载每个新页面:navigate 后 quit);
3. UI/UX 视觉与交互由用户手测——不追求像素级还原,追求结构/口径/文案与原规格一致。
