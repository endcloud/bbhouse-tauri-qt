# Proposal: setup-qt-project-skeleton

## Why

迁移自 WinUI 3 项目 Bilibili.History。在实现任何功能能力之前,需要先落地 Qt6+QML 工程骨架:构建体系(CMake+Ninja)、FluentUI 组件库集成(import FluentUI)、i18n 机制(zh-CN 中性 + en-US 翻译)、mpv 链接与部署形态(可运行 exe)。后续所有能力变更都依赖此骨架。

## What Changes

- 新增能力规格 `qt-app-scaffold`(工程脚手架),约束:
  - 仓库结构与构建入口(根 CMakeLists + `app/` 主工程 + `3rd/` 第三方库);
  - FluentUI 以源码方式参与构建,运行时 `import FluentUI` 可解析;
  - 应用入口 `main.cpp` 初始化 FluApp 与 QML 引擎;
  - i18n 机制:QML 源文案即 zh-CN(中性语言),en-US 经 `.ts/.qm` 翻译,启动按语言偏好加载;
  - mpv(libmpv-2.dll)经 `3rd/mpv` 链接(MinGW 导入库),交付物为含全部 DLL 的可运行目录;
  - 代理侧验证方式为 CMake 构建成功 + offscreen 冒烟运行无 QML 错误(UI/UX 由用户手测)。
- 不改动既有 17 个能力规格。

## Impact

- Affected specs: `qt-app-scaffold`(新增能力)
- Affected code: 新增根 `CMakeLists.txt`、`app/`(CMakeLists.txt、main.cpp、main.qml、qml 模块)、`.gitignore` 补充构建目录
