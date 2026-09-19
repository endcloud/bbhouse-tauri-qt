## Context
播放 API 已在线程池执行，选集列表使用 QML ListView；离线复现发现：继承 FluMenuItem 的分集行在菜单隐藏时高度归零，导致 ListView 将全部分集实例化；另有同步 audio-add 在 GUI 线程等待远程音轨。封面已有转码 helper，但部分入口仍使用原图，需逐一核查。

## Decisions
- 保留现有播放代数和条目身份隔离，不以缩短网络超时替代消除主线程等待。
- 分集 delegate 固定 36 逻辑像素高度，隐藏弹层不归零，保留虚拟化与现有菜单交互。5000 集回归覆盖打开、末集定位、键盘选择、关闭。
- audio-add 使用 mpv_command_async；以 loadSerial 和 playlist entry ID 校验完成回调，新 load/stop/销毁使用 mpv_abort_async_command 取消旧挂轨，错误不冒充装载成功。
- 横版卡片的 B 站 CDN 封面固定 400×225 WebP；番剧页竖版封面仅用 `@.webp` 转格式，不限制尺寸或比例。保留原始数据字段作为预览入口；本地及其他不支持 B 站转码的地址不拼接后缀。
- 卡片启用固定 URL 图片缓存，失败显示既有重试提示，不回退为全尺寸原图；Qt 部署需包含 WebP 图像插件。

## Validation
使用真实 libmpv 与本地延迟 HTTP fixture 检查事件循环响应、切换与过期回调；离屏检查长分集菜单和各封面入口。构建 Release/Debug、CTest、OpenSpec 严格校验。真实在线播放与 UI 观感由用户复测。
