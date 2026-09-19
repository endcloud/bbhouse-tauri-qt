# OpenSpec 积压清理与遗留差异

日期：2026-09-18。用户确认手测“完全 pass”，明确除 `migrate-scheduled-crawler-service` 和 `migrate-local-records-ui` 外，其余活动 change 已实现，并授权直接同步归档。

## 清理结果

本次检查 10 个活动 change，归档 8 个，保留 2 个。归档目录均为 `openspec/changes/archive/2026-09-18-<change>/`。没有删除历史归档，没有修改应用代码，没有补做 Windows 或跨屏 UI 手测。

| 顺序 | Change | 核对依据与处理 |
| --- | --- | --- |
| 1 | `fix-macos-port-and-runtime-bugs` | 核对平台构建、mpv 加载、FluentUI/图片修复；依据用户验收补勾任务 4.2。原 change 声明无规格增量，使用 `--skip-specs` 归档。 |
| 2 | `fix-windows-qt-kit-selection` | 编译器/Qt 配套检测、显式前缀优先、UTF-8、DLL 复制和预设均有实现；历史记录含 Windows 构建与测试。无规格增量归档。 |
| 3 | `fix-fluentui-qml-singleton` | 单例标记、GUI 子系统、窗口及 QML 错误冒烟检查均有实现。无规格增量归档。 |
| 4 | `fix-windows-release-packaging` | 打包脚本、MinGW/PrintSupport 运行库、隔离数据与两阶段冒烟均有实现；Windows 验证依据原交付记录。无规格增量归档。 |
| 5 | `harden-playback-and-layout` | 头像与页面边距、播放错误/重试、免费 PGC 判定均有实现；同步 `app-navigation-shell` 的 2 条新增要求及播放器的 2 条新增、1 条修订要求。 |
| 6 | `refactor-player-controls-ui` | FluentUI 控制面板及既有功能接线存在；以完整 MODIFIED 块同步原控制栏要求，保留场景覆盖。 |
| 7 | `evaluate-danmaku-rendering` | 原场景图、平台字体、纯视频截图、资源预算均已实现；手测记录补齐，原版流畅度反馈由后继可切换方案处理。同步 4 条新增、1 条修订要求后归档。 |
| 8 | `add-danmaku-implementations` | 用户已验收新旧实现切换与新方案；补勾任务 2.3，最后同步可切换实现、图片缓存、播放过渡同步 3 条要求并归档。 |

保留项未修改，任务仍为各 0/4：

- `migrate-scheduled-crawler-service`：服务入口、调度器、服务控制界面尚未迁移。
- `migrate-local-records-ui`：待播/归档存储、挂点与本地记录界面尚未迁移。

## 同步时处理的规格冲突

1. **免费 PGC**：只新增“免费 PGC 响应兼容”会与原“PGC 权益判定与拒播处理”中的 `has_paid + VIP 档位` 客户端拒播规则冲突。本次在对应 change 内补充完整 MODIFIED 块，改为以服务端试看/权益错误为准，保留拒播流程及场景标题；原“非会员付费内容拒播”场景改为服务端明确权益错误触发，并补充免费资源场景。
2. **纯视频截图**：原“帧截图”保留了“回落模式使用窗口捕获”的表述，会重新引入弹幕和控制面板。对应弹幕重构 change 补充 MODIFIED 块，明确 DASH/durl 均经内核异步抓取纯视频帧。其他原截图要求未删除，其尚未实现部分见下表。
3. **暂停时资源准备**：新图像实现允许有限帧收取后台结果及完成分批上传，完成后停止，动画时间不推进；在新规格中明确该例外，避免与禁止持续空帧混淆。
4. 四个无增量修复 proposal 使用旧中文结构标题，CLI 给出非阻断告警；归档后统一为 `Why / What Changes / Impact`，内容与验证事实保留。

没有使用 `--no-validate` 绕过验证，也没有把尚未实现的功能通过改写主规格描述成已完成。

## 仍需报告的问题：早期主规格与 Qt 实现不完全一致

以下是检查中确认的历史差异，不是本次 8 个 change 的未完成任务，也没有因本轮手测通过而标记为已实现。早期主规格继承了 WinUI 原型的完整目标；已归档 change 只代表各自范围完成。此表不是一次全项目规格逐条验收。

| 差异 | 主规格 | 当前证据与影响 |
| --- | --- | --- |
| CC 字幕与 S 快捷键 | `video-playback-window` → CC 字幕、键盘快捷键 | `app/core/PlayerApi.cpp` 已有字幕查询辅助函数，但 `PlayerController` 与控制面板没有字幕轨选择/渲染接线；控制面板 change 已明确不新增无后端入口。 |
| 截图目录与 5MB 压缩上限 | `video-playback-window` → 帧截图 | `PlayerController::screenshot()` 保存到应用数据目录 `Screenshots`，直接异步写 PNG；没有原规格要求的图片目录 `bilibili_screenshot` 或超 5MB 的 JPEG 重编码流程。纯视频隔离、异步结果与唯一文件名已完成。 |
| 内核自动热重建及独立回落内核 | `video-playback-window` → MPV 播放内核 | `kernelDead` 接线将内核标为不可用，下一次起播再建；没有自动原位重建及其请求队列。durl 回落仍依赖 libmpv，不能在 libmpv 缺失时作为独立播放器继续播放。 |
| 相邻条目预解析 | `video-playback-window` → 相邻条目预解析 | 当前控制器按用户起播/切换请求解析，未接入相邻条目预解析队列。 |
| 5× 倍速 | `video-playback-window` → 倍速播放 | `PlayerControls.qml` 提供 0.5/0.75/1/1.25/1.5/2/3，缺少原规格的 5× 菜单项。 |
| 非大会员 Cookie 调试切换 | `settings-ui` → 调试开关；播放器 PGC 调试场景 | 设置页和偏好中没有切换 `bilibili.cookie.normal.txt` 的入口及完整联动。服务端实际权益判断与免费内容兼容不依赖这一未实现入口。 |
| WinUI 工程机制残留 | `ui-localization`、`qt-app-scaffold` 及若干能力描述 | 主规格仍出现 XAML、`.resw`、程序集资源、XAML 岛与 MinGW 导入库链接等表述；当前是 QML、`.ts/.qm`、QTranslator、Qt 场景图及 QLibrary 动态加载。应另行按 Qt 实际设计统一规格，不应照这些旧措辞改回 WinUI。 |

后续处理需要区分“继续补迁移功能”与“正式调整产品范围/平台描述”。本次保留原功能要求并记录差异，没有擅自扩展实现或创建额外活动 change。

## 验证与验收依据

- 用户当前明确反馈手测完全通过；此前已确认原场景图功能可用，随后提出流畅度优化。两个弹幕 change 的验收依据分别保留，没有将后继优化抹回旧版历史。
- 当前 Qt 6.11.2 Release 主程序及全部测试目标增量构建通过；CTest **9/9** 通过。
- 全部当前主规格及保留 change 执行 `openspec validate --all --strict`，**20/20 通过，无警告或错误**（仅保留原有长段落 INFO）；归档增量由 CLI 在每次同步时验证。
- 对本次 8 个归档目录的 **14 个增量要求块**逐项核对主规格内容，一致性检查通过，避免只移动目录未同步规格；保留迁移项与原 Git 内容一致。
- 工作区未包含本轮临时文件或真实运行数据；最终同步及验收记录一并提交。
