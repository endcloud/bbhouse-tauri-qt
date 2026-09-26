#!/usr/bin/env bash
# Windows 发布统一使用 PowerShell 工作流，避免旧裁剪规则删除必需依赖。
set -euo pipefail
cd "$(dirname "$0")/.."
exec pwsh -NoProfile -File scripts/package-release.ps1 "$@"
