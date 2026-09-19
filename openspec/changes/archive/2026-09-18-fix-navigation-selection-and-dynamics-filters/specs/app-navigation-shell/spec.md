## MODIFIED Requirements

### Requirement: NavigationView 导航主体

主窗口 MUST 以 NavigationView 作为主体框架：导航窗格居左，内容区宿主当前选中菜单项对应的页面，整个 NavigationView MUST 让出窗口顶部自定义标题栏高度（内容与窗格均不得与标题栏重叠）。窗口 MUST 保持 Mica 背景与既有自定义标题栏形态（内容扩展进标题栏）。应用启动 MUST 同步设置导航控件选中索引，默认选中"动态"并呈现其页面，MUST NOT 仅切换内容而遗漏导航指示器，导航窗格 MUST 以折叠窄轨初始呈现。

#### Scenario: 启动进入动态

- **WHEN** 应用启动
- **THEN** 导航窗格中"动态"呈选中态且窗格为折叠窄轨，内容区显示动态页并按 dynamics-ui 规格拉取与渲染动态流，标题栏下方的页面内容与标题栏无重叠，窗口保持 Mica 背景与自定义标题栏
