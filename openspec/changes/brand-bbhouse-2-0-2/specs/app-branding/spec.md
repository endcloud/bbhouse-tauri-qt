## ADDED Requirements

### Requirement: 统一应用品牌与构建版本
应用 SHALL 在主窗口、关于页、Windows 产品属性和 macOS/Linux 桌面显示名称使用 BBHouse；运行时和平台元数据 SHALL 使用 CMake 的当前构建版本。Windows 用户启动入口 SHALL 为 BBHouse.exe。显示品牌变更 MUST 保持既有用户配置、历史数据和内部服务标识兼容。

#### Scenario: 启动 2.0.2 发布包
- **WHEN** 用户运行 BBHouse 2.0.2 发布包
- **THEN** 主窗口和关于页显示 BBHouse，版本显示 2.0.2，Windows 主程序产品名为 BBHouse，文件和产品版本均为 2.0.2；已有配置和数据库仍按原路径使用。

#### Scenario: 发布前名称版本校验
- **WHEN** Windows 发布脚本验证暂存包和最终发布目录
- **THEN** 程序属性或运行时版本不匹配源码时验证失败，全部匹配且视频和依赖检查通过后才能完成交付。
