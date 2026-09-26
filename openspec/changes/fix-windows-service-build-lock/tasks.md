## 1. 定位与恢复

- [x] 1.1 读取项目文档和服务规格，确认工作区初始干净。
- [x] 1.2 在原 Qt Creator Debug 目录复现链接失败，核实运行中服务的路径匹配失败输出。
- [x] 1.3 用户确认已暂停退出；SCM 显示 Stopped / PID 0，原服务宿主成功重新链接。
- [x] 1.4 修复后续 MinGW Makefiles 中文帮助资源依赖错误，保留原资源 URL。

## 2. 验证与交付

- [x] 2.1 原 Debug 目录主程序、服务宿主及全部 regression-tests 构建退出码 0；完整 CTest 34/39，5 项运行失败单独串行复跑仍失败，已在交付文档记录，未宣称全量通过。
- [x] 2.2 补充 Windows 构建故障处理说明，完成 OpenSpec 严格校验及差异检查。
- [x] 2.3 清理本轮中断测试遗留的隔离目录和临时日志，提交构建修复、文档与工作记录。

## 验证命令与边界

```powershell
$env:PATH = 'C:/Qt/Tools/mingw1310_64/bin;C:/Qt/6.11.1/mingw_64/bin;' + $env:PATH
& C:/Qt/Tools/CMake_64/bin/cmake.exe --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --target bbhouse-qt regression-tests -j 6
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_ASSUME_STDERR_HAS_CONSOLE = '1'
& C:/Qt/Tools/CMake_64/bin/ctest.exe --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --output-on-failure -j 4
```

5 项失败为 `player-runtime`、`danmaku-scene`、`preferences`、`popular-page`、`history-controller`。本次源码修改仅涉及 CMake 帮助资源输入路径；上述测试目标不使用该帮助资源，运行时失败另见 `doc/Windows原生服务与UAC.md`，不纳入本次构建修复的完成声明。

帮助资源的原文件/暂存文件 SHA-256 一致，生成 make 依赖已改为英文路径，QRC 仍使用原中文别名；CMake 的重新配置依赖中保留原文档路径。服务保持停止，不触发真实定时同步。
