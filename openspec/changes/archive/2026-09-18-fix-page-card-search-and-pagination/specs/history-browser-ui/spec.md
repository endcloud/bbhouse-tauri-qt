## MODIFIED Requirements

### Requirement: 卡片跳转与封面预览互斥

有效 archive/pgc 视频卡片主体左键 MUST 调用统一新窗口播放入口；非视频卡片主体 MUST 保持用系统浏览器打开条目链接（live→直播间、article→cv 页，uri 优先）。右键菜单 MUST 保留打开链接并移除新窗口播放项。点击封面 MUST 只打开原图预览，不得触发播放或跳转；失效视频 MUST NOT 播放。

#### Scenario: 点击卡片打开视频

- **WHEN** 用户点击有效稿件视频卡片的文字区域
- **THEN** 现有播放窗口入口起播该视频，不唤起浏览器

#### Scenario: 点击封面不跳转

- **WHEN** 用户点击卡片封面
- **THEN** 打开封面预览，不发起播放或唤起浏览器

#### Scenario: 原链接仍可访问

- **WHEN** 用户右键卡片选择打开链接
- **THEN** 默认浏览器打开该条目原链接
