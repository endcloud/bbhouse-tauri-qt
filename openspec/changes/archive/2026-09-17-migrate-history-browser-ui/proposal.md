# Proposal: migrate-history-browser-ui

## Why

本地历史浏览视图(瀑布流卡片/分页/封面预览下载/回顶/多次观看角标)是应用的核心界面(契约见 `history-browser-ui` 规格)。骨架阶段 LocalHistoryPage 仅占位,本变更补全完整浏览体验,并把右键"在新窗口播放"接入播放窗口(打通 card→player)。

## What Changes

- `app/qml/controls/HistoryCard.qml`:卡片(封面+时长角标+进度条+标题两行+副标题+UP主+业务标签+观看时间+进度文本+多次观看角标与tooltip);封面缩略图 CDN 转码后缀按容器物理尺寸请求(`@WxH_1c.avif`,失败回退原 URL 一次)
- `app/qml/controls/CoverPreviewOverlay.qml`:全屏原图预览(去 `@` 后缀;左键单击关闭;右键下载原图到 系统图片目录/bilibili_cover,带 Referer/UA;InfoBar 反馈;打开/关闭缩放过渡动画)
- `app/qml/pages/LocalHistoryPage.qml` 完整化:瀑布流(300px 定宽列,列数自适应,最短列放置,整体居中,无横向滚动;FluStaggeredLayout 或自绘 Flow)+ 分页栏(30/页,首/上/下/末+文案)+ 状态栏计数 + 回到顶部 FAB + 卡片点击跳转/封面预览互斥 + 右键"在新窗口播放"(business 可播判定:archive 必要字段齐备;调用 PlayerController.openWith)
- `app/controllers/HistoryController` 扩展:coverDownload(url) 线程池下载;items 附带 viewRecords 时间戳列表(tooltip)
- i18n + 冒烟
- 规格无 delta(实现既有契约;缩放动画以 Qt 缩放变换等价实现)

## Impact

- Affected specs: 无修改
- Affected code: qml/controls/*、qml/pages/LocalHistoryPage.qml、HistoryController、CMake、translations
