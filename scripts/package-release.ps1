#Requires -Version 7
<#
.SYNOPSIS
  生成包含运行及下载依赖的精简 Windows Release 包，并隔离验证。
.DESCRIPTION
  输出 <OutDir>\BBHouse[<提交号>]，重名附时间戳，不删除已有发布目录。
  使用 -StageOnly 在 build/release-stage 内验证；提交后去掉该参数正式输出。
  FFmpeg 建议指定静态 essentials 版，aria2c 指定静态 Windows x64 版。
  VulkanPath 指向可再分发 Vulkan Loader；curl 使用 Windows 10/11 系统组件。
#>
[CmdletBinding()]
param(
    [string]$OutDir = 'D:\release',
    [string]$Name,
    [string]$FfmpegPath,
    [string]$Aria2Path,
    [string]$VulkanPath = "$env:SystemRoot\System32\vulkan-1.dll",
    [int]$Jobs = 6,
    [switch]$KeepOpenGlSw,
    [switch]$SkipBuild,
    [switch]$StageOnly,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$deploy = Join-Path $build 'deploy'
$stage = Join-Path $build 'release-stage'
if (Test-Path -LiteralPath $stage) {
    $stage += '_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff')
}
$projectText = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($projectText -notmatch 'project\(bbhouse-qt VERSION ([\d.]+)') { throw '无法读取应用版本' }
$version = $Matches[1]
$qtBin = 'C:\Qt\6.11.1\mingw_64\bin'
$mingwBin = 'C:\Qt\Tools\mingw1310_64\bin'
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$python = (Get-Command python -ErrorAction Stop).Source
if (-not $FfmpegPath) { $FfmpegPath = (Get-Command ffmpeg -ErrorAction Stop).Source }
if (-not $Aria2Path) { $Aria2Path = (Get-Command aria2c -ErrorAction Stop).Source }
foreach ($path in @($cmake, $FfmpegPath, $Aria2Path, $VulkanPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少文件: $path" }
}
if ($Jobs -lt 1) { throw 'Jobs 必须为正数' }
$hash = (& git -C $root rev-parse --short HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $hash) { throw '读取 Git 提交号失败' }
if (-not $Name) { $Name = "BBHouse[$hash]" }
if ($Name -in @('.', '..') -or $Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw 'Name 必须是单个合法目录名称'
}
if ($Force) { Write-Warning '-Force 不再删除已有发布目录；同名目录自动追加时间戳。' }

function Remove-ProjectArtifact([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    $stagePrefix = [IO.Path]::GetFullPath($stage) + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "拒绝裁剪本次暂存目录外路径: $full"
    }
    if (Test-Path -LiteralPath $full) {
        if ((Get-Item -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "拒绝清理重解析点: $full"
        }
        Remove-Item -LiteralPath $full -Recurse
    }
}

function Invoke-IsolatedProcess([string]$file, [string[]]$arguments, [string]$workingDirectory) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $file
    $psi.WorkingDirectory = $workingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($arg in $arguments) { $psi.ArgumentList.Add($arg) }
    foreach ($key in @($psi.Environment.Keys)) {
        if ($key -match '^(QT_|QML|BBHOUSE_|MPV_)') { $psi.Environment.Remove($key) | Out-Null }
    }
    $psi.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $psi.Environment['QT_ASSUME_STDERR_HAS_CONSOLE'] = '1'
    $process = [Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    try {
        if (-not $process.WaitForExit(45000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw "验证超时: $file"
        }
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "验证失败(exit=$($process.ExitCode)): $output" }
        return $output
    } finally { $process.Dispose() }
}

function Test-Package([string]$directory, [string]$label) {
    foreach ($executable in @('BBHouse.exe', 'bbhouse-history-service.exe')) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $directory $executable))
        if ($info.ProductName -ne 'BBHouse' -or $info.ProductVersion -ne $version -or $info.FileVersion -ne $version) {
            throw "程序名称或版本与源码不一致: $executable"
        }
    }
    $output = Invoke-IsolatedProcess (Join-Path $directory 'BBHouse.exe') @(
        '--deployment-smoke-test', '--scratch-dir', $build) $directory
    if ($output -notmatch 'DEPLOYMENT_SMOKE_OK:') { throw "隔离验证未完成: $output" }
    if ($output -notmatch 'DEPLOYMENT_VIDEO_OK:') { throw "原生视频验证未完成: $output" }
    if (-not $output.Contains("DEPLOYMENT_IDENTITY: BBHouse $version")) { throw "运行时应用名称或版本不一致: $output" }
    if ($output -match 'Windows system media .*failed|Windows system media controls unavailable') {
        throw "Windows 系统媒体验证失败: $output"
    }
    if ($output -match 'is not installed|Cannot load library|QQmlApplicationEngine failed|TypeError|ReferenceError|Unable to assign') {
        throw "QML/依赖验证失败: $output"
    }
    $output | Set-Content -LiteralPath (Join-Path $build "release-$label-smoke.log") -Encoding utf8
    & $python (Join-Path $root 'tools/windows_package_check.py') $directory --scratch $build --report (Join-Path $build "release-$label-check.json")
    if ($LASTEXITCODE -ne 0) { throw 'PE 依赖或外部工具验证失败' }
    Write-Host "隔离部署验证通过: $label"
}

if (-not $SkipBuild) {
    $savedPath = $env:PATH
    try {
        $env:PATH = "$mingwBin;$qtBin;C:\Qt\Tools\Ninja;$savedPath"
        & $cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }
        & $cmake --build $build --target deploy -j $Jobs
        if ($LASTEXITCODE -ne 0) { throw 'Release deploy 构建失败' }
    } finally { $env:PATH = $savedPath }
}
if (-not (Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -SimpleMatch 'CMAKE_BUILD_TYPE:STRING=Release')) {
    throw '只允许打包 Release 构建'
}
foreach ($required in @('BBHouse.exe','bbhouse-history-service.exe','libmpv-2.dll','FluentUI\fluentuiplugin.dll','LICENSE','THIRD_PARTY_NOTICES.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $deploy $required))) { throw "deploy 缺少 $required" }
}
if (Test-Path -LiteralPath $stage) { throw "暂存目录已存在: $stage" }
Copy-Item -LiteralPath $deploy -Destination $stage -Recurse
# FluentUI's embedded table control imports this module; windeployqt only
# scans the application's loose QML and cannot discover that embedded import.
Copy-Item -LiteralPath (Join-Path $qtBin 'Qt6LabsQmlModels.dll') -Destination $stage
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'qml/Qt/labs') | Out-Null
Copy-Item -LiteralPath (Join-Path $qtBin '../qml/Qt/labs/qmlmodels') -Destination (Join-Path $stage 'qml/Qt/labs/qmlmodels') -Recurse
$beforeBytes = (Get-ChildItem -LiteralPath $stage -Recurse -File | Measure-Object Length -Sum).Sum

$dropDirs = @('translations','qmltooling','generic','vectorimageformats','networkinformation','styles',
    'qml\QtQuick\NativeStyle','qml\QtQuick\Controls\FluentWinUI3','qml\QtQuick\Controls\Windows',
    'qml\QtQuick\Controls\Material','qml\QtQuick\Controls\Imagine','qml\QtQuick\Controls\Fusion',
    'qml\QtQuick\Controls\Universal','qml\FluentUI')
$dropFiles = @('fluentuiplugin.dll','Qt6Svg.dll','iconengines\qsvgicon.dll','imageformats\qsvg.dll',
    'tls\qcertonlybackend.dll','tls\qopensslbackend.dll','Qt6Lottie.dll','Qt6LottieVectorImageGenerator.dll',
    'Qt6QuickVectorImageGenerator.dll','Qt6Quick3DUtils.dll','Qt6QuickControls2Fusion.dll',
    'Qt6QuickControls2FusionStyleImpl.dll','Qt6QuickControls2Imagine.dll','Qt6QuickControls2ImagineStyleImpl.dll',
    'Qt6QuickControls2Material.dll','Qt6QuickControls2MaterialStyleImpl.dll','Qt6QuickControls2Universal.dll',
    'Qt6QuickControls2UniversalStyleImpl.dll','Qt6QuickControls2FluentWinUI3StyleImpl.dll','Qt6QuickControls2WindowsStyleImpl.dll')
if (-not $KeepOpenGlSw) { $dropFiles += 'opengl32sw.dll' }
foreach ($relative in ($dropDirs + $dropFiles)) { Remove-ProjectArtifact (Join-Path $stage $relative) }
foreach ($dir in @('sqldrivers','imageformats')) {
    $keep = if ($dir -eq 'sqldrivers') { @('qsqlite.dll') } else { @('qjpeg.dll','qwebp.dll') }
    Get-ChildItem -LiteralPath (Join-Path $stage $dir) -File | Where-Object Name -NotIn $keep |
        ForEach-Object { Remove-ProjectArtifact $_.FullName }
}
# FluentUI's qmldir prefers embedded qrc resources. Retain the map and plugin.
Get-ChildItem -LiteralPath (Join-Path $stage 'FluentUI') -Force | Where-Object Name -NotIn @('qmldir','fluentuiplugin.dll') |
    ForEach-Object { Remove-ProjectArtifact $_.FullName }
Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Extension -in @('.qmltypes','.a','.prl','.pdb','.debug','.qrc') } |
    ForEach-Object { Remove-ProjectArtifact $_.FullName }
foreach ($module in Get-ChildItem -LiteralPath $stage -Recurse -File -Filter qmldir) {
    $lines = Get-Content -LiteralPath $module.FullName | Where-Object { $_ -notmatch '^typeinfo ' }
    $lines | Set-Content -LiteralPath $module.FullName -Encoding utf8NoBOM
}
foreach ($relative in @('BBHouse.exe','bbhouse-history-service.exe','FluentUI\fluentuiplugin.dll')) {
    & (Join-Path $mingwBin 'strip.exe') --strip-unneeded (Join-Path $stage $relative)
    if ($LASTEXITCODE -ne 0) { throw "去除符号失败: $relative" }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tools') | Out-Null
Copy-Item -LiteralPath $FfmpegPath -Destination (Join-Path $stage 'tools/ffmpeg.exe')
Copy-Item -LiteralPath $Aria2Path -Destination (Join-Path $stage 'tools/aria2c.exe')
Copy-Item -LiteralPath $VulkanPath -Destination (Join-Path $stage 'vulkan-1.dll')
@'
[Paths]
Prefix=.
Plugins=.
QmlImports=qml
'@ | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding utf8NoBOM
Test-Package $stage 'stage'
$afterBytes = (Get-ChildItem -LiteralPath $stage -Recurse -File | Measure-Object Length -Sum).Sum
$toolsBytes = (Get-Item -LiteralPath (Join-Path $stage 'tools/ffmpeg.exe'),(Join-Path $stage 'tools/aria2c.exe'),(Join-Path $stage 'vulkan-1.dll') | Measure-Object Length -Sum).Sum
$summary = "原 deploy: $([math]::Round($beforeBytes/1MB,1)) MiB; 裁剪节省: $([math]::Round(($beforeBytes+$toolsBytes-$afterBytes)/1MB,1)) MiB; 完整暂存: $([math]::Round($afterBytes/1MB,1)) MiB"
Write-Host $summary
if ($StageOnly) { Write-Host "已验证暂存包: $stage"; return }
if (& git -C $root status --porcelain --untracked-files=normal) { throw '正式输出前请先提交工作区，使目录提交号与源码一致；可先用 -StageOnly 验证。' }
$target = Join-Path $OutDir $Name
if (Test-Path -LiteralPath $target) {
    $target = Join-Path $OutDir ($Name + '_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
}
if (Test-Path -LiteralPath $target) { throw "目标已存在，请重试: $target" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item -LiteralPath $stage -Destination $target -Recurse
Test-Package $target 'published'
$fullHash = (& git -C $root rev-parse HEAD).Trim()
$manifest = @("# BBHouse $version Windows Release", '', "- 提交：$fullHash", "- 版本：$version", '- 平台：Windows 10/11 x64，需正常显卡驱动。',
    '- 启动：双击 BBHouse.exe。账号/历史/配置不随包附带。',
    '- 已包含 Qt、FluentUI、libmpv、Vulkan Loader、aria2 和 FFmpeg；curl 使用系统自带版本。',
    '- 已通过视频像素/进度/关闭重开、Windows 原生媒体状态回读、隔离部署、PE 依赖及媒体合并/封面/本机下载验证；UI 和在线业务仍需手测。',
    '- 许可证与来源见 THIRD_PARTY_NOTICES.md 和 licenses；现有公开分发授权事项见项目发布文档。', '', $summary)
$manifest | Set-Content -LiteralPath (Join-Path $target '使用说明.md') -Encoding utf8
$check = Get-Content -LiteralPath (Join-Path $build 'release-published-check.json') -Raw | ConvertFrom-Json
$dependencyNotes = @('# 依赖与验证', '', "- 源码提交：$fullHash", "- x64 PE 文件：$($check.pe_images.Count)",
    '- Qt 6.11.1 MinGW x64；运行库与插件已随包。', '- 所有非系统 PE 导入均已在包内解析。',
    '- 文件校验值见 SHA256SUMS；原始来源及许可见 THIRD_PARTY_NOTICES.md。', '')
foreach ($entry in $check.tool_versions.PSObject.Properties) { $dependencyNotes += "- $($entry.Value)" }
$dependencyNotes += @('', '- 系统 curl 与显卡驱动由 Windows 提供。', '- 视频和原生系统媒体回归不替代 UI/在线业务手测；全量 CTest 遗留失败与本次交付记录见项目 doc/WindowsRelease起播闪退修复.md。')
$dependencyNotes | Set-Content -LiteralPath (Join-Path $target '依赖清单.md') -Encoding utf8
$hashLines = Get-ChildItem -LiteralPath $target -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($target, $_.FullName).Replace('\','/')
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $relative"
}
$hashLines | Set-Content -LiteralPath (Join-Path $target 'SHA256SUMS') -Encoding utf8NoBOM
$files = @(Get-ChildItem -LiteralPath $target -Recurse -File)
$size = ($files | Measure-Object Length -Sum).Sum
Write-Host "发布完成: $target"
Write-Host "文件数: $($files.Count); 总体积: $([math]::Round($size/1MB,2)) MiB ($size bytes)"
