## Context
项目以 Qt/C++ API 解析向 libmpv 提交候选。原 Windows XML 方案已按用户要求被 `replace-windows-scheduler-with-service` 替代。

## Decisions
2. 在共用媒体 URL 策略中按主机识别已知 P2P（mcdn、pcdn、szbdyd）；已知 upos 云端节点先于未知普通节点，P2P 最后。同级保持服务端顺序。仅重排服务端已有有效 HTTP(S) 地址，不改域名、查询参数或签名，不探测或请求未下发地址。
3. DASH 保持既有档位、编码和音频规格选择，在所选流内部排序；durl 保留第一分段的主备链；直播在选定实际 qn 内跨协议/编码进行 CDN 优先排序，同类继续既有格式偏好。
4. 音轨在主文件装载后按排序链异步 audio-add，失败/超时推进下一个；用世代与候选标识屏蔽旧回调。视频 watchdog 与音频逐候选计时分离，避免第一条计时抢先中断后续音轨。durl 使用同样有界的视频候选循环。

## Risks and Validation
域名特征只能识别已知 CDN 类型，不能证明未知节点部署形态；新域名应扩充规则，不强制改写 URL。离线 fixture 覆盖视频/音频主备、字段变体、去重、未知/非法地址、纯 P2P 和直播跨轨排序；本地真实 libmpv 验证失败/超时回退和切播隔离。构建 Release、Qt Creator Debug 与完整 CTest；在线音画与 UI 保留用户手测。
