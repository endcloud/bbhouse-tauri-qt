# Tasks: fix-fluentui-qml-singleton

- [x] 1.1 定位:FluRouter/FluEventBus 在生成的 qmldir 里是普通类型,盖住 C++ 单例注册
- [x] 1.2 FluentUI CMake 给两个单例 QML 设 QT_QML_SINGLETON_TYPE,生成 `singleton FluRouter 1.0 ...`
- [x] 2.1 bbhouse-qt 设为 WIN32_EXECUTABLE(双击不弹控制台)
- [x] 3.1 冒烟改默认平台 + QT_ASSUME_STDERR_HAS_CONSOLE=1,校验窗口出现 + 无 QML 报错
- [x] 3.2 冒烟退出超时(后台 HTTP 收尾)降级为警告
- [x] 4.1 验证:开发版窗口出现(title='B站历史记录')、stderr 无 TypeError;ctest 4/4;子系统 GUI
- [x] 4.2 重新出包并复跑冒烟(暂存 + 发布目录两次通过)
