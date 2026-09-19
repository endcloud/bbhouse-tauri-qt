## ADDED Requirements

### Requirement: 正式发布信息与依赖归属

项目 SHALL 声明本项目代码的 GPLv3 许可证、正式仓库链接和第三方依赖/参考来源。第三方许可 MUST 保留原归属；未确认可分发的资源 MUST 明确列为发布阻塞项。

#### Scenario: 阅读开源说明
- **WHEN** 用户打开仓库或 About 页
- **THEN** 看到正式 GitHub 地址和依赖说明入口，不能再声称 mpv 仅供个人自用或整份 FluentUI 都是 MIT

### Requirement: 隐私安全的代码与构建产物

仓库和 CI 产物 MUST NOT 包含真实 Cookie、配置、观看记录、缓存或带签名媒体地址。持久化审查 SHALL 覆盖当前 Git 跟踪树与可达历史，并记录检查范围与限制。播放器 MUST NOT 把原始 mpv 日志自动落盘。

#### Scenario: 发布前检查
- **WHEN** 检查跟踪文件及构建上传目录
- **THEN** 用户运行数据由忽略规则和明确产物清单排除，诊断输出不包含凭据正文

### Requirement: 双平台 tag 构建

GitHub Actions SHALL 在 tag push 时编译 Windows x64 与 macOS arm64；测试 MUST 使用隔离数据。编译 artifacts MUST 清楚说明是否包含 Qt/libmpv 及完整发行所需材料，不能将跳过的内核测试报告为通过。

#### Scenario: 推送版本 tag
- **WHEN** 推送 tag
- **THEN** 两个平台独立构建并上传平台/架构明确的产物，编译失败阻止该平台上传成功产物

### Requirement: 播放内核分发说明

发行说明 SHALL 区分开发依赖、源码和独立二进制包，记录 libmpv 与传递依赖的分发义务及体积测量范围。

#### Scenario: 计划独立 Release
- **WHEN** 维护者准备供未安装开发环境的用户下载
- **THEN** 说明必须捆绑兼容运行库或明确要求用户安装，且遵守对应构建的许可证与源码提供义务
