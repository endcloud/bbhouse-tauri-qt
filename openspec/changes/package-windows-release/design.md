## Context

Windows 主程序通过 Qt/QML 插件和动态 libmpv 工作；下载和封面提取依赖外部进程。仅 windeployqt 或清空 PATH 不能发现被本机 System32 中第三方 DLL 掩盖的遗漏。

## Goals / Non-Goals

- 在保留现有功能及许可证的前提下缩减体积，打包所有非系统必需依赖。
- 不重新编译裁功能版 Qt/libmpv，不用 UPX 改写第三方签名二进制，不把仅启动成功等同 UI/在线播放验收。

## Decisions

- Qt/MinGW 与现有 Qt Creator 套件一致，使用 Release + Ninja。
- 使用静态 FFmpeg essentials 工具，保留媒体合并和解码/封面能力，省略 ffplay/ffprobe 与开发文档；aria2 静态程序独立附带。curl 使用 Windows 10/11 内置版本。
- PE 导入检查仅允许明确 Windows 系统库和包内文件；Vulkan Loader 必须随包附带。
- 在项目内暂存、裁剪并验证，发布后再从含方括号的新路径复验。既有目标不覆盖，重复命名追加时间戳。
- 隔离部署入口在普通控制器创建前分流；禁用远程请求，临时数据放 build，PATH/QML 路径不引用 SDK。

## Risks / Trade-offs

- 移除软件 OpenGL 回退以缩小体积，运行机器需要正常显卡驱动；Windows 系统组件和显卡驱动不属于随包依赖。
- Windows Debug 全量测试已有 5 项运行失败；记录其边界，另做本次 Release 与自包含部署检查。
- 不宣称完成现有公开分发授权事项；本任务交付本地可运行目录。
