#!/usr/bin/env bash
# 交付打包:构建 → windeployqt → 精简 → 冒烟 → 产出 build/deploy(可运行目录)
# 用法: bash scripts/package.sh
set -euo pipefail

export PATH="/c/Qt/Tools/CMake_64/bin:/c/Qt/Tools/Ninja:/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.11.1/mingw_64/bin:$PATH"
cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target deploy

D=build/deploy

# 精简:自托管 FluentUI 模块(顶层)已被复制;windeployqt 可能从 SDK 复制重复模块与未用组件
rm -rf "$D/qml/FluentUI"                     # 自托管版本已随 deploy/FluentUI 提供
rm -rf "$D/qml/Qt/labs" "$D/qml/QtTest" "$D/qml/QtQuick/Particles.3" "$D/qml/Qt5Compat" 2>/dev/null || true
rm -f "$D"/Qt6Labs*.dll "$D"/Qt6Test.dll \
      "$D"/Qt6QuickControls2Imagine.dll "$D"/Qt6QuickControls2Material.dll \
      "$D"/Qt6QuickControls2Universal.dll "$D"/Qt6QuickControls2Fusion.dll 2>/dev/null || true
# 翻译只留简中/英文
find "$D/translations" -name "qt_*.qm" ! -name "*zh_CN*" ! -name "*en_US*" -delete 2>/dev/null || true

# 冒烟:交付目录自包含验证(不依赖 build/bin 与 PATH)
cp libmpv-2.dll "$D/" 2>/dev/null || true
BBHOUSE_SMOKE=1 BBHOUSE_SMOKE_NAV="LocalHistoryPage.qml,SettingsPage.qml" \
  QT_QPA_PLATFORM=offscreen "$D/bbhouse-qt.exe"

echo "=== package done: $D ==="
du -sm "$D"
