## ADDED Requirements

### Requirement: 启动更新检查

应用启动后 SHALL 异步请求 `https://cos.endcloud.cn/bbhouse-qt/version.json`。本地缺少独立 version-code 时 SHALL 使用 10；仅当远端 `code` 为更高整数且 `url` 为合法 HTTP(S) 地址时显示更新弹窗，点击前往更新 SHALL 打开该 URL。请求失败或载荷无效 SHALL 不阻断启动。

#### Scenario: 发现新版本

- **WHEN** version.json 返回 code 大于本地版本码且包含合法 url
- **THEN** 应用显示更新弹窗，前往更新打开远端 url

#### Scenario: 无更新或请求失败

- **WHEN** code 不高、载荷非法或网络请求失败
- **THEN** 应用正常启动且不显示更新弹窗
