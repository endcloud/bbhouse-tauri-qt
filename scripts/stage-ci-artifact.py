#!/usr/bin/env python3
"""Stage only named build products and notices; never archive a developer data directory."""

import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile


def main():
    root = Path(__file__).resolve().parent.parent
    platform = os.environ["BUILD_PLATFORM"]
    if platform not in {"windows-x64", "macos-arm64"}:
        raise SystemExit(f"Unsupported build platform: {platform}")
    binary_dir = root / "build" / "ci" / "bin"
    name = f"bbhouse-qt-{platform}-build"
    stage = root / "build" / "ci-stage" / name
    # A stale directory is an error: do not merge unrelated files into an artifact.
    stage.mkdir(parents=True, exist_ok=False)
    executable = "BBHouse.exe" if platform == "windows-x64" else "bbhouse-qt"
    plugin = "fluentuiplugin.dll" if platform == "windows-x64" else "libfluentuiplugin.dylib"
    products = [executable, plugin]
    if platform == "windows-x64":
        products.append("bbhouse-history-service.exe")
    for filename in products:
        shutil.copy2(binary_dir / filename, stage / filename)
    shutil.copytree(binary_dir / "FluentUI", stage / "FluentUI",
                    ignore=shutil.ignore_patterns("*.a", "*.lib", "*.pdb", "*.qrc"))
    for filename in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(root / filename, stage / filename)
    shutil.copytree(root / "licenses", stage / "licenses")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    qt_version = os.environ["QT_VERSION"]
    (stage / "BUILD-INFO.md").write_text(
        f"# BBHouse 编译产物 / build-only artifact\n\n"
        f"- 平台：{platform}\n- Qt SDK：{qt_version}\n- 提交：{commit}\n"
        f"- 应用及内嵌组件对应源码（不含外部 Qt SDK）：https://github.com/endcloud/bbhouse-tauri-qt/archive/{commit}.tar.gz\n\n"
        "此包仅供检查编译结果，不是独立安装包。不含 Qt 运行库、libmpv、系统运行库，"
        "也未签名或公证。需要匹配的 Qt SDK 和 FluentUI QML 导入路径；"
        "开发机绝对路径及 RPATH 尚未重定位。不要作为最终 Release 安装包分发。\n\n"
        "播放还需兼容架构的 libmpv：Windows 将 libmpv-2.dll 及其依赖放到程序同级；"
        "macOS 可先安装 Homebrew mpv。libmpv 和传递依赖的许可证及对应源码"
        "须在独立发行包阶段核对。\n\n"
        "Windows 编译全部测试，CTest 排除 player-runtime、screenshot、live-player"
        "（缺少 libmpv）；macOS 使用 Homebrew libmpv，排除需要原生图形会话的 "
        "screenshot-macos。完整 UI/UX 与实际播放仍需人工验收。\n",
        encoding="utf-8",
    )
    output = root / "build" / "ci-artifacts"
    output.mkdir(parents=True, exist_ok=True)
    if platform == "macos-arm64":
        # tar preserves executable bits; upload-artifact's outer zip does not.
        archive = output / f"{name}.tar.gz"
        with tarfile.open(archive, "x:gz") as bundle:
            bundle.add(stage, arcname=name)
    else:
        archive = output / f"{name}.zip"
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
            for file in sorted(stage.rglob("*")):
                if file.is_file():
                    bundle.write(file, file.relative_to(stage.parent))
    print(archive.relative_to(root))


if __name__ == "__main__":
    main()
