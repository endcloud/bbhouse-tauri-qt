# Tasks: migrate-app-navigation-shell

## 1. Controller 层

- [x] 1.1 `AppController`:theme(亮/暗/系统→FluTheme.darkMode 即时生效+持久化)、language(读写偏好,重启生效)、应用信息
- [x] 1.2 `HistoryController`:后台线程装载本地历史分页(loadPage→pageLoaded(QVariantList,int total))、计数;后台 store 初始化 ready 状态;同步编排(startSync/cancelSync/状态文本信号,HistorySyncRunner 驱动)

## 2. QML 外壳

- [x] 2.1 `MainWindow.qml`:FluWindow + FluNavigationView(左窄轨折叠)+ 汉堡 + 六菜单与页脚设置 + 分隔线;启动默认"动态";页面路由 FluRouter→Loader 缓存
- [x] 2.2 标题栏居中搜索框(仅卡片页显示;本变更建 UI 与接口,搜索投影在 card-title-search 变更实现)
- [x] 2.3 `pages/LocalHistoryPage.qml` 骨架(标题带+工具栏)与 `pages/PlaceholderPage.qml`(占位:动态/特别关注/稍后再看)
- [x] 2.4 `SettingsPage.qml` 骨架(主题三档即时生效 + 语言三档提示重启生效;About 区在 settings-ui 变更补全)

## 3. i18n 与验证

- [x] 3.1 全部新文案 qsTr/Loc + .ts 条目补齐
- [x] 3.2 构建 0 error;offscreen 冒烟(外壳装载/页面切换/主题切换无 QML 错误)
- [ ] 3.3 validate + 归档 + commit
