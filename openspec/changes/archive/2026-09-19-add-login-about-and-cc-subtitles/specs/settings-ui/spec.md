## REMOVED Requirements

### Requirement: About 区
**Reason**: 关于与开源信息迁移到独立关于页面。
**Migration**: 使用导航底部的“关于”入口。

## ADDED Requirements

### Requirement: 设置末尾重新登录
设置页 SHALL 在选项列表末尾提供“重新登录”，点击 SHALL 进入与首次启动相同的初始化页面，成功前 SHALL 保留原 Cookie。

#### Scenario: 重新登录
- **WHEN** 用户点击设置末尾“重新登录”
- **THEN** 仅显示 Cookie 文件/文本导入入口及帮助，不显示扫码，不立即删除现有凭据。
