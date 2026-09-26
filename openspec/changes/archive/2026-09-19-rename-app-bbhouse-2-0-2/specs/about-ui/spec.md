## MODIFIED Requirements

### Requirement: 独立关于页面
关于页 SHALL 显示 BBHouse 应用信息、当前版本、可复制版本行、法律和许可声明，仓库链接 SHALL 指向 https://github.com/endcloud/bbhouse-tauri-qt。标题 SHALL 冻结、正文 SHALL 滚动并适配窄窗和中英文。

#### Scenario: 浏览与复制
- **WHEN** 用户进入关于页并复制版本或点击仓库链接
- **THEN** 显示 BBHouse 及版本 2.0.2，复制应用版本信息或以系统浏览器打开正确仓库。

