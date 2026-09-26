## Context

应用此前显示“B站历史记录”，构建名 bbhouse-qt，版本 1.0.1。应用数据使用 shizi/bbhouse-qt，已有用户配置和原生服务需继续兼容。

## Decisions

- 显示名和 Windows OUTPUT_NAME 使用 BBHouse；Qt applicationName、CMake target、bundle identifier 和服务宿主内部名继续作为稳定标识。
- 应用版本唯一来源为 CMake project VERSION 2.0.2；Windows 主程序和服务的 VERSIONINFO 由同一模板生成。关于页读取运行时版本，macOS 脚本继续读取 CMake。
- 发布脚本校验两份 EXE 的产品名/文件版本/产品版本，校验部署入口输出的运行时品牌和版本。
- 已存在暂存目录时创建时间戳新目录，保留旧目录；已有正式发布目录同样不覆盖。实际删除必须遵守自动审批结果。

## Risks / Trade-offs

Windows EXE 更名后新包从 BBHouse.exe 启动，已有快捷方式仍指向旧包。Windows 原生服务不会由测试重注册。macOS/Linux 在本机仅静态检查。
