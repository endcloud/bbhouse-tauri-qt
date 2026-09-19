## Context

项目以 Qt/QML、内嵌 FluentUI、动态加载 libmpv 和 SQLite 实现。OpenSpec 的归档是对应范围的记录，不等同全功能或正式分发验收。

## Decisions

- 缺省值通过现有 QSettings 的 value fallback 提供；测试用独立配置路径及子进程验证读取无写入。用户输入 locahost 按 localhost 修正，区域代理继续只影响区域 API。
- 1080P 是新点播会话优先清晰度，服务端权益/可用流约束及降级不变；H.265 仍接受已保存编码覆盖；默认速度为 1x，不覆盖用户的记忆速度。
- CI 使用显式 Qt/架构与构建目标，产物与持久化目录隔离，全部测试先编译。无 Windows libmpv 时明确标记内核依赖测试缺口，不下载不明 GPL DLL 充当已审计发布包。
- 源码 GPL-3.0-only 不改变 Qt、FluentUI 及其子组件、mpv API 头文件和素材的原许可证。微软字体授权缺口保留为发布阻塞项；没有授权或替换前不能声称全仓库可合法再分发。
- 普通编译 artifacts 不自动附加到 Release。将来独立发行须配齐 Qt、libmpv 及传递依赖，附确切版本对应源码和构建材料，并另行验证签名/平台运行。

## Validation

构建 app 与全部 CTest 目标并运行非交互回归；静态检查 workflow、Git 当前树与可达历史的敏感特征、OpenSpec 严格校验。无在线账号测试，不读真实 cookie/数据库。
