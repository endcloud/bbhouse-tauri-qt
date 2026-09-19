## ADDED Requirements

### Requirement: 独立关于页面
关于页 SHALL 显示应用信息、当前版本、可复制版本行、法律和许可声明，仓库链接 SHALL 指向 https://github.com/endcloud/bbhouse-tauri-qt。标题 SHALL 冻结、正文 SHALL 滚动并适配窄窗和中英文。

#### Scenario: 浏览与复制
- **WHEN** 用户进入关于页并复制版本或点击仓库链接
- **THEN** 显示版本 1.0.1，复制应用版本信息或以系统浏览器打开正确仓库。

### Requirement: 开源来源逐项展示
直接依赖、内嵌组件及已记录的参考开源项目 SHALL 每个单独展示名称、来源链接和一句话介绍作为副标题；保留第三方原许可及非开源素材的授权边界，不把参考项目描述为全量捆绑。

#### Scenario: 检查依赖与引用
- **WHEN** 用户滚动查看关于页
- **THEN** 每个项目有独立条目，可查看其介绍并打开来源。
