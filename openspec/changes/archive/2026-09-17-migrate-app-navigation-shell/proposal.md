# Proposal: migrate-app-navigation-shell

## Why

主窗口外壳(导航/标题栏/页面路由/i18n 装配)是全部 UI 页面的宿主。按 `app-navigation-shell` 与 `ui-localization` 规格,把 WinUI 的 NavigationView 外壳迁移为 FluentUI 的 FluNavigationView 形态,并建立 Controller 桥接层与 QML 页面骨架。

## What Changes

- `MainWindow.qml` 重构为:FluWindow + FluNavigationView(Left 折叠窄轨)+ 汉堡 + 自定义标题栏区 + 标题栏居中搜索框(仅卡片页显示,本变更先建接线,搜索实现在 card-title-search 变更)+ 页面路由(FluRouter + Loader)。
- 菜单:动态/特别关注/分隔线/稍后再看/在线历史/本地历史 + 页脚设置;启动默认选中"动态"(2026-09-01 用户指定,占位页)。
- 新增 `controllers/AppController`(主题切换:亮/暗/跟随系统,经 FluTheme.darkMode 即时生效;语言偏好读写)+ `controllers/HistoryController`(本地历史分页装载 + 同步编排接线,供本地历史页使用)。
- 页面骨架:`pages/LocalHistoryPage.qml` 骨架(工具栏占位)+ 占位页 PlaceholderPage;页面 NavigationCache 语义以 Loader 缓存等价实现。
- i18n:全部外壳文案 qsTr 化 + .ts 条目。
- 规格无 delta(实现既有 app-navigation-shell/ui-localization 契约;WinUI 控件名→FluentUI 映射见 doc/qt-migration-notes.md)。

## Impact

- Affected specs: 无修改
- Affected code: app/controllers/*、app/qml/MainWindow.qml 重构、qml/pages/*、translations
