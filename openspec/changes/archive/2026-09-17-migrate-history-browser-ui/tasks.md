# Tasks: migrate-history-browser-ui

## 1. 控件

- [ ] 1.1 `HistoryCard.qml`:全部卡片信息元素 + 转码缩略图(物理尺寸后缀+回退一次)+ 多次观看角标/tooltip + 右键菜单(在新窗口播放/打开链接)
- [ ] 1.2 `CoverPreviewOverlay.qml`:原图预览/左键关/右键下载(Referer+UA,图片目录 bilibili_cover)/缩放过渡动画

## 2. 页面完整化

- [ ] 2.1 瀑布流:300px 定宽列自适应列数/最短列/整体居中/无横向滚动
- [ ] 2.2 分页栏(边界禁用+文案)/状态栏计数/回到顶部 FAB(滚动联动)
- [ ] 2.3 点击跳转(浏览器)与封面预览互斥;右键起播接 PlayerController
- [ ] 2.4 HistoryController:coverDownload + viewRecords 时间戳透出

## 3. 验证

- [ ] 3.1 构建 0 error;冒烟 exit 0 无 QML 错误
- [ ] 3.2 validate + 归档 + commit
