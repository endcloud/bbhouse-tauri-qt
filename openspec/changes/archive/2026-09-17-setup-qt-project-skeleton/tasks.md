# Tasks: setup-qt-project-skeleton

## 1. 构建体系

- [x] 1.1 根 `CMakeLists.txt`:Qt6 查找(Core/Quick/Qml/Widgets/Network/Sql/PrintSupport)、`3rd/FluentUI` 子目录(插件输出重定向到 build/bin/FluentUI)、`app/` 子目录
- [x] 1.2 `app/CMakeLists.txt`:`qt_add_executable(bbhouse-qt)` + `qt_add_qml_module`(URI `bbhouse`,main.qml 入口)+ 链接 fluentuiplugin
- [x] 1.3 Ninja 配置 + 全量构建通过(0 error)

## 2. 应用入口

- [x] 2.1 `app/main.cpp`:FluApp 初始化(QML 侧)、QML import 路径注册(appDirPath)、主 QML 启动、窗口尺寸常量(QML 内)
- [x] 2.2 `app/qml/Main.qml`(FluLauncher + FluRouter 路由)+ `MainWindow.qml`(FluWindow 1200×720,最小 880×560);offscreen 冒烟无 QML 错误

## 3. i18n 机制

- [x] 3.1 `app/translations/bbhouse_en_US.ts` + lrelease 产出 `.qm` 嵌入资源 `:/i18n/`;CMake 自定义命令集成
- [x] 3.2 `app/preferences/AppPreferences.h/cpp`:语言(跟随系统/zh/en)/主题偏好持久化(QSettings IniFormat),启动按系统语言链装配 Translator

## 4. mpv 链接

- [x] 4.1 `app/player/MpvLib.h/cpp`:QLibrary 动态加载 libmpv-2.dll 探测接口(available/libraryPath + 核心符号解析),exe 同级目录优先
- [x] 4.2 头文件路径接入(3rd/mpv/include);实现调整:不链接导入库避免 exe 硬依赖,纯动态加载、缺库优雅降级(冒烟实测:有 dll 加载成功、无 dll 不崩溃)

## 5. 验证与交付形态

- [x] 5.1 offscreen 冒烟:BBHOUSE_SMOKE=1 自动退出,stderr 无 QML 错误(exit 0)
- [x] 5.2 CMake `deploy` 目标(windeployqt + FluentUI 模块 + fluentuiplugin.dll + libmpv-2.dll → build/deploy/)
- [ ] 5.3 openspec validate 通过并归档,git commit

