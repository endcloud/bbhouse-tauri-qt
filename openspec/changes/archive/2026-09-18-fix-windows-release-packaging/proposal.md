# Proposal: fix-windows-release-packaging

Windows Release 交付包修复:补全运行时依赖,并用「冒烟验证 + 精简清单」产出可运行的最小包。

## Why

`deploy` 目标产出的目录(280.7MB)**不可独立运行**,有两处漏拷贝:

1. `fluentuiplugin.dll` 的导入表依赖 `Qt6PrintSupport.dll`,而 `windeployqt` 只扫描 exe 的依赖 → QML 侧报
   `Cannot load library .../FluentUI/fluentuiplugin.dll: 找不到指定的模块`(实测 exit=-1,QML 根对象加载失败)。
2. `windeployqt` 配了 `--no-compiler-runtime`,不拷 MinGW 运行时(`libstdc++-6`/`libgcc_s_seh-1`/`libwinpthread-1`),
   exe 在干净环境下直接 `STATUS_DLL_NOT_FOUND (0xC0000135)`;开发时靠 Qt Creator 注入 PATH 掩盖了。

同时交付目录里带了一批本项目不用的组件(未用 QML 样式/插件、Qt 自带翻译、软件 OpenGL 回退等),以及
一份与模块目录重复的 `fluentuiplugin.dll`(exe 导入表里其实没有它)。

## What Changes

1. `app/CMakeLists.txt`:`deploy` 目标补拷 MinGW 运行时(从编译器目录探测)与 `$<TARGET_FILE:Qt6::PrintSupport>`。
2. 新增 `scripts/package-release.ps1`:构建 → `deploy` → 复制到 `build/release-stage` → 按清单精简 → 清空 PATH 自包含冒烟 → 输出 `<OutDir>\<项目名>[<短提交号>]` 并复跑冒烟;运行期数据用 `BBHOUSE_DATA_DIR` 隔离,不触碰真实数据库。
3. 精简清单(逐项经"删除 → 冒烟"验证):Qt 自带翻译、`qmltooling`、`generic`、`vectorimageformats`、`networkinformation`、`styles`、未用 QML 样式(NativeStyle/Windows/Material/Imagine/Fusion/Universal/FluentWinUI3)、未用 Qt 库(Lottie/QuickVectorImageGenerator/Quick3DUtils/各 Style 及 StyleImpl)、多余图片格式(仅留 jpeg/webp)、SVG 相关(Qt6Svg/qsvgicon/qsvg,工程无 svg 资源)、`qcertonlybackend`、根目录重复的 `fluentuiplugin.dll`、`plugins.qmltypes`/`libfluentuiplugin.a`;默认再移除 `opengl32sw.dll`(可用 `-KeepOpenGlSw` 保留)。
4. 保留 `Qt6QuickShapes.dll`:它只被 FluentUI 的 `FluTour.qml` 引用,删掉属于"运行期才暴露"的隐患,仅 0.38MB,不值得换。

## Impact

- 只影响 `deploy`/打包产物,不改运行代码、不改规格契约(无 spec delta)。
- 交付包从 280.7MB(且跑不起来)变为 208.6MB(自包含冒烟通过)。

## 验证

- `scripts/package-release.ps1` 全流程:两次自包含冒烟(暂存目录 + 发布目录)均 exit=0 且无 QML/插件报错。
- 精简清单逐项 A/B:删除候选 → 冒烟 → 失败即还原;唯一 FAIL 项是 `FluentUI\fluentuiplugin.dll`(必需,已保留)。
- `D:\release\bbhouse-qt[a76aa65]`(修复依赖、未做扩展精简)= 234.6MB;扩展精简后 = 208.6MB。
