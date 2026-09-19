# login-onboarding Specification

## Purpose
定义首次启动和重新登录时的 Cookie 导入、凭据验证与安全保存，以及随应用分发的 Cookie-Editor 导入帮助；扫码入口暂时隐藏。

## Requirements

### Requirement: 初始化登录入口
应用 SHALL 在缺少有效主 Cookie 时首先展示初始化页面，仅提供 Cookie 文件/文本导入；Web 扫码入口 SHALL 暂时隐藏且页面 MUST NOT 触发二维码生成或轮询。首次登录前 MUST NOT 自动装载默认动态页并发出账号请求。已有有效 Cookie 的用户 SHALL 保持默认动态页启动。

#### Scenario: 首次启动
- **WHEN** 当前配置路径不存在有效的 bilibili.cookie.txt
- **THEN** 显示登录初始化页面及文件/粘贴导入入口，不显示扫码入口。

### Requirement: 安全持久化
有效导入 SHALL 经 nav 验证后原子保存到现有配置可读取的 bilibili.cookie.txt，失败 SHALL 保留旧文件；Cookie MUST NOT 进入日志和版本库。

#### Scenario: 导入成功
- **WHEN** 导入 Cookie 经服务端验证成功
- **THEN** 保存 Cookie 并进入主页面。

#### Scenario: 导入失败
- **WHEN** 用户导入不含有效 SESSDATA 的内容或写入失败
- **THEN** 展示可读错误且保留旧凭据，不进入已登录状态。

### Requirement: Cookie 导入帮助
登录页 SHALL 提供可展开的 Cookie-Editor 导入帮助和指定 Edge 扩展商店链接，说明在已登录的哔哩哔哩页面导出 Header String、粘贴或保存纯文本文件导入的步骤，明确不使用 JSON/Netscape 格式并提醒保护账号凭据。帮助 SHALL 支持中英文及窄窗换行。

#### Scenario: 获取导入帮助
- **WHEN** 用户展开“如何获取 Cookie？”
- **THEN** 页面显示安装、网页登录、导出 Header String、验证导入及失败后重新导出的说明；用户可点击链接打开 Cookie-Editor 扩展商店。

### Requirement: 导入帮助随应用分发
完整 `Cookie导入帮助.md` SHALL 从项目文档源直接编入 Qt 资源 `:/help/Cookie导入帮助.md`，随应用在各平台分发，不依赖源码目录中的外部文件。

#### Scenario: 发布程序携带帮助
- **WHEN** 构建应用并移入发行目录
- **THEN** 完整 Markdown 帮助仍包含在程序资源中。
