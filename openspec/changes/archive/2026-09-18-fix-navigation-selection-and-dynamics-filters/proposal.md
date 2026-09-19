## Why

启动时动态页面已加载但左侧导航指示器未选中；用户要求动态分类删除“全部”、默认视频，并将直播与专栏分组。

## What Changes

- 启动通过 FluentUI 导航选中 API 激活动态，修复无 url、仅 tap 的导航项无法程序选中的问题。
- 动态分类收敛为六档，默认视频；旧 all 和未知持久化值迁移到 video，其他有效偏好继续恢复。
- 直播与专栏之间添加竖向分隔线，和专栏按钮共同参与响应式折行。

## Capabilities

### Modified Capabilities

- `app-navigation-shell`: 启动页面与导航指示器同步。
- `dynamics-ui`: 六档分类、视频默认值与分隔线。

## Impact

修改 MainWindow、DynamicsPage、DynamicsController 默认值和本项目内嵌 Qt6 FluentUI 导航 API。接口、分区增量模型及分页续载算法不变。构建、CTest 与隔离装载验证后交由用户手测。
