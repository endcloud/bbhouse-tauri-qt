## ADDED Requirements

### Requirement: 应用品牌与版本一致性
应用全局显示名称 SHALL 为 BBHouse，中英文主窗口、关于页及平台应用显示名 SHALL 保持一致。版本 SHALL 从 CMake 项目声明读取，当前为 2.0.2，并同步 Qt 应用版本、Windows 原生产品版本与 macOS 包版本。内部应用标识和现有数据路径 MUST 保持兼容，名称变化 MUST NOT 导致旧 Cookie、数据库或偏好失联。

#### Scenario: 显示名称和版本
- **WHEN** 用户启动任一语言版本并打开关于页
- **THEN** 主窗口及关于页显示 BBHouse，版本和复制信息显示 2.0.2，原有数据继续可读。

#### Scenario: 原生发行元数据
- **WHEN** 构建 Windows 程序或执行 macOS 打包
- **THEN** 原生产品名称为 BBHouse、版本为 2.0.2，macOS 应用包为 BBHouse.app；内部可执行文件名及 bundle ID 保持兼容。
