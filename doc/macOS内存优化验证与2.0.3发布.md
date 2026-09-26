# macOS 内存优化验证与 2.0.3 发布

**日期**: 2026-09-26  
**平台**: macOS (Darwin 27.0.0)  
**版本**: BBHouse 2.0.3

## 验证范围

近期三个内存优化提交（`optimize-memory-footprint` / `optimize-memory-lifecycle` / `reduce-player-memory-churn`）在 Windows 开发并通过完整回归，本次在 macOS 补齐功能验证和回归测试。

## 回归测试

**完整 CTest**: 48/48 通过（79.46 秒）

关键测试覆盖：
- **page-lifecycle**: 后台页面限时 Loader、超时销毁 QML 对象、返回重建、阈值修改
- **page-state-restore**: 动态/在线历史/流行/直播/番剧的搜索/页码/滚动/档位恢复，池裁剪后视口稳定
- **dynamics-page**: 虚拟化网格、增量更新、分区补查
- **online-history-page**: 虚拟化网格、锚点恢复  
- **popular-page**: 虚拟化网格、池上限
- **local-player**: 同内容换档复用已加载/正在请求的弹幕，换内容隔离旧结果

## 功能验证

- ✅ 离屏冒烟导航：11 个主导航页 QML 装载无错误
- ✅ 窗口启动：正常加载并交互，RSS ~268 MB

## Release 打包

**构建配置**:
- Qt: 6.11.2  
- CMake: Qt Tools bundled  
- libmpv: Homebrew 2.dylib  
- 媒体工具: aria2 1.37.0_2, FFmpeg 9.0.1_1, curl 系统版本

**交付产物**:
- **路径**: `build/release/BBHouse-macos-arm64_20260926_155015/`
- **BBHouse.app**: 174 MB，ad-hoc 签名通过深度严格校验
- **BBHouse-2.0.3-macos-arm64.dmg**: 67 MB
- **SHA-256**: `47f3f960b65c0cdcf6ae69d17732c63e789623c6f5cdc85163b8aa239930f0ff`
- **最低系统**: macOS 27.0

**验证通过**:
- 代码签名（ad-hoc）深度严格校验
- 原地隔离冒烟：libmpv/aria2c/FFmpeg/curl、媒体编解码、SQLite、图片插件、QML 装载
- 迁移后隔离冒烟：独立临时目录重定位运行，所有功能正常

## 结论

macOS 平台完整验证通过，内存优化修复在两平台均工作正常。Release 包已就绪，可交付。
