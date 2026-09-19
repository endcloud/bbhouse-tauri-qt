## Why

用户要求参照截图补齐“设置 → 播放与弹幕”的显示控制，并参考 pakku.js 为两种弹幕实现增加相似弹幕合并，降低重复内容遮挡。

## What Changes

- 增加默认弹幕状态、不透明度、字号、顶部 1/4 / 半屏 / 全屏区域、密度上限及合并相似弹幕设置，复用 FluentUI 控件并持久化。
- 设置即时应用于两种弹幕实现，与播放器开关共用状态。
- 后台按有限时间窗口合并重复/近似弹幕，显示数量；保留原始数据供关闭合并时恢复。
- 对密度、区域、异步结果隔离和持久化做非交互回归；实际 UI/UX 交由用户手测。

## Capabilities

### Modified Capabilities

- `settings-ui`: 新增播放与弹幕控制项。
- `video-playback-window`: 共享显示设置、密度限制与相似合并处理。

## Impact

AppPreferences、设置 QML/翻译、PlayerController、DanmakuEngine、两种布局器及回归测试。外部 pakku.js 只读，参考算法思路，不引入浏览器扩展、WASM 或词典依赖。
