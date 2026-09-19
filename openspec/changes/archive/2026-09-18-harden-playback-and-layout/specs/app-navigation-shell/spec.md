## ADDED Requirements

### Requirement: 窄窗口布局与统一内边距

页面 SHALL 由单一外层负责 24px 边距，避免 FluPage 默认 padding 重复叠加。macOS 内容页 SHALL 位于原生标题栏下的工具行之后。正常标题带保持 56px；番剧与特别关注页在筛选按钮无法并排容纳时 SHALL 允许扩展至 96px，分页说明与按钮 SHALL 可分行，不能产生负尺寸或覆盖正文。

#### Scenario: 窄窗口与展开导航

- **WHEN** 用户缩小窗口或展开导航导致页面可用宽度减少
- **THEN** 筛选、分页和内容保持可操作，文字不覆盖按钮，页面无多余重复内边距

### Requirement: 圆形头像保留抗锯齿覆盖率

FluClip 的遮罩 SHALL 保留边缘连续 alpha，并按设备像素比采样。特别关注头像和管理弹层头像 SHALL 共用该行为。

#### Scenario: 高 DPI 圆形头像

- **WHEN** macOS Retina 或 Windows 缩放显示头像
- **THEN** 圆边使用平滑遮罩，不将半透明边缘二值化
