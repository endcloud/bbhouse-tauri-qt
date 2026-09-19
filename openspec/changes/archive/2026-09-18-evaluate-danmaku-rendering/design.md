## Context

重构前的路径光栅化评估见 doc/bililocal弹幕重构评估.md；当前实现与验证见 doc/弹幕场景图重构与手测.md。

## Goals / Non-Goals

保留既有三种弹幕模式、媒体时钟和播放功能，改善文字绘制性能及字体，输出纯帧截图。不移植 Qt 5 原生 OpenGL 后端，不新增高级弹幕或字幕播放功能。

## Decisions

- GUI 线程 DanmakuLayout 生成在屏快照与不可变 QTextLayout；Qt 同步阶段在 GUI 阻塞时读取快照，渲染线程独占 QSG 节点。
- QQuickItem + 公开 QSGTextNode，QtRendering/Outline；按 ID 复用节点，只更新变换，场景图失效由 Qt 回收。
- Windows Microsoft YaHei/macOS PingFang SC，Medium 字重；统一度量和绘制，DPR 改变重建节点。
- frameSwapped 排队到 GUI 驱动，单次定时器处理空屏下一入场；无窗口/隐藏/禁用/暂停/零车道不持续驱动。
- 小幅媒体时间回校逐渐收敛；显式 seek 使用目标时间硬重置。
- 每帧最多 64 次准备、256 在屏项、128 车道，异常长文本上限 1024 UTF-16 单元；超预算已到期项跳过，不延时积压。
- mpv_command_async + COMMAND_REPLY 对应完成回调；顶部截图固定 video 模式、毫秒唯一命名，不猜测完成时间。

## Risks / Trade-offs

密集度上限会跳过超过显示能力的条目。文字首次排版仍在 GUI 线程，已通过有界准备限制突发；暂不引入线程池和自建图集。GPU 测试仅有 CPU 提交耗时，不能代替真实播放帧时间。Windows 和用户原截图场景待手测。

## Migration Plan

生产实现已迁移，模型/场景图/截图回归覆盖并提供可执行文件。旧评估数据保留为历史，真实 UI/UX 验收由用户完成后再归档变更。
