## ADDED Requirements

### Requirement: 独立 macOS 应用包

打包脚本 SHALL 产出 arm64 Release .app，包含 Qt/QML、FluentUI、libmpv 及实际需要的非系统动态依赖。加载路径 MUST 可重定位且不依赖开发机安装。Info.plist SHALL 声明真实系统下限。

#### Scenario: 从新路径启动
- **WHEN** 将应用复制到其他目录且无 Qt/Homebrew 搜索路径
- **THEN** 应用加载包内依赖并通过部署检查

### Requirement: 安全的部署验证

部署诊断 SHALL 使用显式隔离目录且不启动账号网络请求、不读取真实 Cookie/配置/数据库、不注册系统任务。打包流程 MUST 校验代码签名与所有非系统动态库的闭包及架构。

#### Scenario: 本机包自检
- **WHEN** 打包流程启动隔离诊断
- **THEN** QML、SQLite、图片插件及包内 libmpv 本地媒体播放成功，失败阻止交付

### Requirement: 拖拽安装磁盘映像

打包流程 SHALL 生成含 .app 和 Applications 快捷入口的只读压缩 DMG，提供明确的 Finder 图标布局。DMG MUST 校验并挂载验证后交付，不能附带用户数据。

#### Scenario: 打开 DMG
- **WHEN** 用户挂载映像
- **THEN** 可将 .app 拖入 Applications；应用版本、架构、系统下限及签名状态在交付说明中清楚列出

### Requirement: 分发材料与签名边界

应用 SHALL 携带许可证、依赖版本清单和现有授权缺口说明。缺少 Developer ID 时 SHALL 以 ad-hoc 签名交付并明确未公证，不声称已满足公开分发的所有许可和 Gatekeeper 要求。

#### Scenario: 本地试用交付
- **WHEN** 本机无有效发行签名身份
- **THEN** 可完成本地 .app/DMG 打包，不上传远程发行；保留已有字体许可阻塞说明

### Requirement: 包外凭据

macOS .app SHALL 从用户数据目录读取 Cookie，MUST NOT 要求修改签名包内部，也不自动搬运开发凭据。非 bundle 开发运行保留既有仓库根查找行为。

#### Scenario: 安装后配置账号
- **WHEN** 用户将 Cookie 放入应用用户数据目录
- **THEN** 下次请求使用该文件，应用签名不变，凭据不会进入打包产物

### Requirement: 下载工具自包含与旧版清理

macOS 应用 SHALL 携带 aria2c、FFmpeg、curl 可执行文件及非系统动态依赖，并通过现有包内优先查找定位；MUST 保留用户显式设置和真实配置。工具 SHALL 纳入架构、重定位、签名、许可证和隔离部署验证。内置 aria2 SHALL 使用 macOS 公共 CA，curl SHALL 使用 AppleSecTrust，MUST NOT 依赖 Homebrew CA 路径或关闭证书校验。新包验证成功后 SHALL 按本次用户要求清理项目内旧 macOS 交付目录。

#### Scenario: 无开发环境的下载工具
- **WHEN** 原包或 DMG 副本无法访问 Homebrew 和开发 SDK
- **THEN** 三个工具从包内运行，FFmpeg 完成本地媒体转换，aria2 与 curl 完成本地 fixture 传输

#### Scenario: 替换旧交付
- **WHEN** 新版应用及 DMG 完整验证成功
- **THEN** 保留新交付并清理旧版本打包目录，不影响用户配置或源码
