## ADDED Requirements

### Requirement: 关于和初始化导航
主窗口 SHALL 在导航底部按从上到下“设置”、“关于”的顺序提供入口，缓存页面并隐藏卡片搜索框。首次缺少 Cookie 及设置重新登录 SHALL 导向初始化页面，成功后进入主界面。

#### Scenario: 导航关于页
- **WHEN** 用户点击“关于”并切换页面后返回
- **THEN** 关于页显示，搜索框隐藏，滚动位置保留；“关于”位于“设置”下方。

## MODIFIED Requirements

### Requirement: 设置页面接线

“设置”菜单项 MUST 导航到设置页面，选中态与页面同步；页面行为按 settings-ui 规格执行，关于信息使用独立关于页面。

#### Scenario: 从导航进入设置
- **WHEN** 用户在导航菜单选中“设置”
- **THEN** 内容区呈现设置页，末尾提供重新登录入口，关于与开源信息不再混排。
