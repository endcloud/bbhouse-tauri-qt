## Context

MainWindow 原先直接调用 visitPage，绕过导航控件的选中索引。FluentUI setCurrentIndex 只支持带 url 或自定义 onTapListener 的项；本项目使用 tap 连接缓存页面，没有 url。动态页从持久化值恢复七档分类，缺省 all。

## Decisions

- 启动调用 setCurrentIndex(0)，由同一路径更新指示器并触发已有 tap 路由。内嵌 FluentUI 为普通 tap 项同步主列表和页脚索引，保留自定义 listener 语义，并忽略非法索引、分隔项和禁用项。
- 六个有效分类仍恢复用户偏好；首次启动、旧 all 和未知值均使用 video，控制器初值也与页面一致。这里的默认值不强制覆盖已有有效分类。
- 用 FluDivider 和专栏按钮组成一个 Row，置于原 Flow 中；窄窗口折行时分隔线不会成为独立一行，主题色来自 FluentUI。

## Validation Strategy

构建 Release/Debug 与所有测试目标，运行 CTest、QML 隔离无头装载、OpenSpec 严格校验及 git diff --check。用户手测冷启动指示器、分类默认/恢复、分隔线和窄窗布局。本轮新 UI 不使用此前 change 的通过结论代替验收。
