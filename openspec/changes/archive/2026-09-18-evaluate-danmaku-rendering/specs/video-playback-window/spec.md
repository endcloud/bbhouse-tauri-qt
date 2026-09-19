## ADDED Requirements

### Requirement: 弹幕文字资源复用

弹幕渲染 SHALL 复用已准备的文字资源，稳定显示期间 MUST NOT 在每帧重新构造或光栅化相同文字的完整描边与填充。每帧主要更新在屏项的位置与透明度。资源准备与回收 MUST 不阻断既有暂停、seek、切片和窗口关闭行为。

#### Scenario: 稳定文字滚动

- **WHEN** 文字、字体和效果不变且弹幕仍在屏内
- **THEN** 更新位置时复用文字资源；离屏项可回收，内存受控

### Requirement: 弹幕平台默认字体

弹幕 SHALL 在 Windows 优先使用 Microsoft YaHei、macOS 优先使用 PingFang SC，并提供可用字体回退；布局与绘制 MUST 使用一致的字体参数。跨屏缩放变化 SHALL 更新依赖实际像素密度的资源。

#### Scenario: 跨屏显示弹幕

- **WHEN** 播放窗口移到不同缩放比例的显示器
- **THEN** 字体逻辑尺寸及时间位置一致，依赖 DPR 的资源更新以保持清晰度

### Requirement: 纯视频帧截图隔离

顶部截图按钮 SHALL 通过播放内核请求纯视频帧，不包含客户端弹幕、控制面板或可关闭的字幕层；视频源内烧录文字不属于可剥离覆盖层。保存 MUST 异步报告真实结果，连续截图 MUST 使用独立文件名。

#### Scenario: 弹幕开关不影响截图

- **WHEN** 同一暂停帧分别在弹幕开启和关闭状态下点击顶部截图按钮
- **THEN** 两次输出的视频像素一致，无客户端弹幕，且得到两个独立文件

### Requirement: 弹幕刷新节奏与密度预算

弹幕 SHALL 按 Qt Quick 帧节奏更新在屏位置，空屏时等待下一入场；暂停、隐藏、禁用或零车道时 MUST NOT 连续驱动空帧。文字准备 SHALL 有每帧预算与在屏数量上限，超预算的已到期条目可跳过但 MUST NOT 积压到之后播放。媒体时钟小幅回校 SHALL 平滑处理，显式 seek SHALL 立即重置到目标时间。

#### Scenario: 暂停和空车道

- **WHEN** 播放暂停或窗口没有完整可用车道
- **THEN** 弹幕不持续请求新帧；恢复或几何变化后能继续显示

#### Scenario: 密集弹幕与拖动

- **WHEN** 同时到达大量条目或用户拖动到新的时间位置
- **THEN** 准备工作有界、过期任务不延迟积压；拖动立即切换弹幕时间轴

## MODIFIED Requirements

### Requirement: 帧截图

播放窗口 SHALL 提供"截图"操作：捕获当前播放画面帧并保存至 `图片\bilibili_screenshot\`，文件名含视频标题与时间戳；成功后 SHALL 以 InfoBar 提示保存路径。截图 SHALL 经播放内核异步捕获纯视频帧；DASH 与 durl 单流均 MUST NOT 回落为窗口捕获，客户端弹幕、控制面板及可关闭的字幕层 MUST NOT 进入截图。完成提示 SHALL 依据真实命令与落盘结果，连续截图 SHALL 使用独立文件名。**落盘文件大小 MUST NOT 超过 5MB**：捕获产物不超过 5MB 时 SHALL 保持无损 PNG 原样落盘；超过 5MB 时 SHALL 以 JPEG 压缩算法重编码（按图像质量逐级下调迭代，必要时叠加降采样兜底）直至满足上限，落盘文件扩展名 MUST 与实际格式一致。截图失败 MUST 以 InfoBar 报错且不中断播放。截图 MUST NOT 暂停、中断或改变当前播放状态。

#### Scenario: 成功截图

- **WHEN** 播放中用户点击"截图"且捕获产物不超过 5MB
- **THEN** 当前视频帧写入 `图片\bilibili_screenshot\` 下的 PNG 文件，InfoBar 显示完整路径，播放不中断

#### Scenario: 超限压缩落盘

- **WHEN** 播放中用户点击"截图"且捕获的 PNG 产物超过 5MB（如 4K 高信息量画面）
- **THEN** 截图经 JPEG 压缩迭代后以 .jpg 文件落盘且大小不超过 5MB，InfoBar 显示实际保存路径，播放不中断

#### Scenario: 截图失败降级

- **WHEN** 帧捕获或保存失败
- **THEN** InfoBar 显示失败原因，播放与界面保持正常
