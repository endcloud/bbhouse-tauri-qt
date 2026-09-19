## ADDED Requirements

### Requirement: macOS 硬解帧截图兼容
macOS 播放器 SHALL 在保留可用硬件解码的同时，提供可编码的当前视频帧用于异步截图；截图 MUST NOT 因直接传递 VideoToolbox 硬件格式给软件图像编码链路而失败。截图 SHALL 保持纯视频帧隔离、独立文件名与真实命令/落盘结果反馈，MUST NOT 在截图时重启解码器、改变播放暂停状态或捕获客户端窗口。

#### Scenario: macOS 硬解视频截图
- **WHEN** macOS 使用 VideoToolbox 播放视频并连续请求帧截图
- **THEN** 每次请求异步保存独立且可解码的 PNG 或 JPEG，不包含可关闭字幕、客户端弹幕或控制面板，播放状态保持不变

#### Scenario: 暂停截图与写入失败
- **WHEN** macOS 暂停视频后请求截图，或保存路径不可写
- **THEN** 暂停帧可正常保存，写入失败返回真实失败结果且不误报成功，两种情况均不改变暂停状态

## MODIFIED Requirements

### Requirement: 帧截图

播放窗口 SHALL 提供截图操作：经播放内核异步捕获当前纯视频帧，保存到当前应用数据目录的 `Screenshots` 子目录，文件名包含视频标题与时间戳，成功后以 InfoBar 提示实际路径。DASH 与 durl 单流均 MUST NOT 回落为窗口捕获；客户端弹幕、控制面板及可关闭字幕层 MUST NOT 进入截图。连续截图 SHALL 使用独立文件名，MUST NOT 覆盖已有文件。最终落盘文件大小 MUST 严格小于 **3 MiB（3,145,728 字节）**：捕获 PNG 小于上限时 SHALL 原样保留；达到或超过上限时 SHALL 在后台线程中使用 JPEG 重编码，优先保持分辨率逐级下调质量，必要时等比降采样，直到满足上限。最终扩展名 MUST 与实际格式一致，超限原始帧及中间产物 SHALL 作为临时文件清理。成功提示 SHALL 依据最终命令、编码及落盘结果；失败 SHALL 以 InfoBar 报错。截图及压缩 MUST NOT 暂停、中断或改变播放状态，MUST NOT 在 GUI 线程执行大图重编码。

#### Scenario: 成功截图
- **WHEN** 捕获 PNG 严格小于 3 MiB
- **THEN** 原样保存 PNG，InfoBar 显示真实路径，播放状态不变

#### Scenario: 超限压缩落盘
- **WHEN** 捕获 PNG 达到或超过 3 MiB
- **THEN** 后台按 JPEG 质量迭代及必要的等比缩小生成严格小于 3 MiB 的 `.jpg`，显示最终路径并清理原始临时 PNG

#### Scenario: 截图失败降级
- **WHEN** 帧读取、编码或写入失败
- **THEN** InfoBar 显示失败原因，不误报成功、不覆盖既有图片，临时产物被清理，界面和播放保持正常

#### Scenario: 连续截图
- **WHEN** 用户连续截图
- **THEN** 每次成功截图使用独立文件名且满足大小限制，后台串行压缩，界面和播放保持正常
