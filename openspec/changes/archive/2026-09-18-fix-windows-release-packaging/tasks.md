# Tasks: fix-windows-release-packaging

- [x] 1.1 deploy 目标:补拷 MinGW 运行时(libgcc_s_seh-1/libstdc++-6/libwinpthread-1)
- [x] 1.2 deploy 目标:补拷 Qt6::PrintSupport(fluentuiplugin.dll 的漏扫依赖)
- [x] 2.1 新增 scripts/package-release.ps1(构建 → deploy → 精简 → 自包含冒烟 → 发布目录 + 复跑冒烟)
- [x] 2.2 冒烟实现:GUI 子系统进程用 ProcessStartInfo 抓 stderr;路径含 `[提交号]` 时不用 Start-Process/Push-Location 的通配符解析
- [x] 2.3 运行期数据隔离(BBHOUSE_DATA_DIR=build/smoke-data)
- [x] 3.1 精简清单逐项 A/B 验证(删 → 冒烟 → 失败还原)
- [x] 3.2 确认保留项:Qt6QuickShapes(FluTour 引用)
- [x] 3.3 发布目录包复跑冒烟通过
- [x] 4.1 README 增加「Windows Release 包」段落
