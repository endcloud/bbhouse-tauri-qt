## 1. 实现

- [x] 1.1 导入平台图标资源并接线 Qt、FluentUI 和关于页。
- [x] 1.2 Windows 可执行文件 ICO、macOS ICNS 打包验证、Linux desktop/hicolor 资源接线。
- [x] 1.3 更新资源来源、构建及交付说明。

## 2. 验证

- [x] 2.1 构建与 CTest、资源及平台配置检查、OpenSpec 严格校验。
- [x] 2.2 清理临时文件并提交。
- [ ] 2.3 用户确认 Windows/macOS/Linux 原生图标显示与缓存刷新后的观感。

## 3. macOS Dock 尺寸反馈

- [x] 3.1 保留原始素材，为 macOS PNG 增加透明边距并同步生成 ICNS 各尺寸。
- [x] 3.2 验证图标边界、构建与 CTest，更新交付记录并提交。
- [ ] 3.3 用户复测 Dock 中与相邻应用的视觉大小。
