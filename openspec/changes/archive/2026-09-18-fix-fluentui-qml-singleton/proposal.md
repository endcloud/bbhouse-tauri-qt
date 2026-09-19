# Proposal: fix-fluentui-qml-singleton

修复"进程能起但窗口永不出现":FluentUI 的 QML 单例被 qmldir 当普通类型,并加固发布包冒烟。

## Why

用户双击 Release 包后只见控制台,窗口不出现,输出:

```
libmpv loaded: "D:/release/bbhouse-qt[f240329]/libmpv-2.dll"
qrc:/qt/qml/bbhouse/qml/Main.qml:16: TypeError: Property 'navigate' of object FluRouter is not a function
```

根因(开发版 `build/bin` 同样复现,与打包无关):

1. `3rd/FluentUI/Qt6/imports/FluentUI/Controls/FluRouter.qml` / `FluEventBus.qml` 是 `pragma Singleton`,
   插件 `FluentUI.cpp` 也用 `qmlRegisterSingletonType(QUrl(...))` 注册了单例。
2. 但 CMake 里没有给这两个源文件设 `QT_QML_SINGLETON_TYPE`,于是 `qt_add_qml_module` 生成的模块 qmldir
   把它们写成普通类型(`FluRouter 1.0 Controls/FluRouter.qml`,源码 `Controls/qmldir` 里本来就是 `singleton`),
   运行时该类型条目盖住 C++ 单例 → `FluRouter` 变成"类型对象",`navigate` 不存在。
3. `Main.qml` 的 `Component.onCompleted` 里第一次 `FluRouter.navigate("/")` 抛错 → 首个窗口永远不创建,
   但事件循环仍在跑,所以表现为"进程活着 + 无窗口 + 无退出"。

附带两处工程性问题:

- exe 是**控制台子系统**(MinGW 默认),双击弹黑框,也正因如此才看得到上面那行报错;发布包应为 GUI 子系统。
- 原打包冒烟用 `QT_QPA_PLATFORM=offscreen` 且只看退出码,既看不到窗口缺失,也抓不到 Qt 日志
  (无控制台时 Qt 把日志发给调试器而非 stderr),所以这个 bug 被放过了。

## What Changes

1. `3rd/FluentUI/CMakeLists.txt`:`set_source_files_properties(... FluRouter.qml FluEventBus.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)`,生成的 qmldir 变为 `singleton FluRouter 1.0 ...`。
2. `app/CMakeLists.txt`:`set_target_properties(bbhouse-qt PROPERTIES WIN32_EXECUTABLE TRUE)` —— 双击不弹控制台。
3. `scripts/package-release.ps1` 冒烟加固:改用默认平台启动(不是 offscreen)、设 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 强制日志进 stderr、要求**窗口句柄出现**、把 `TypeError/is not a function/ReferenceError/模块缺失` 等计入失败;退出慢(后台 HTTP 收尾)降级为警告。

## Impact

- 只影响 FluentUI 模块的 qmldir 生成方式与 exe 子系统;不改 API/规格契约(无 spec delta)。
- `FluEventBus` 同为单例,一并修正(此前同样会失去 JS 成员)。

## 验证

- 修复前:`build/bin/bbhouse-qt.exe` 与发布包均无窗口(`MainWindowHandle=0`)+ `TypeError`;修复后窗口出现(`title='B站历史记录'`),stderr 仅 `libmpv loaded`。
- `ctest`:4/4 通过;exe 子系统 `00000002 (Windows GUI)`。
- `scripts/package-release.ps1` 两次冒烟均"窗口已出现 + exit=0 + 无 QML 报错"。
