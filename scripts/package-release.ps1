#Requires -Version 7
<#
.SYNOPSIS
  构建 Windows Release 交付包:deploy → 精简 → 自包含冒烟 → 输出 <项目名>[<短提交号>]。
.DESCRIPTION
  1) 用 Qt Creator 同款套件(MinGW 13.1.0 + Qt 6.11.1 mingw_64)配置并构建 Release;
  2) 走 CMake 的 deploy 目标生成 build/deploy;
  3) 复制到 build/release-stage 后按精简清单剔除未用文件(见 $dropDirs/$dropFiles 注释);
  4) 清空 PATH 做无头冒烟,验证包自包含可启动、QML 无报错;
  5) 复制为 <OutDir>\<Name>[<短提交号>],并在发布目录再跑一次冒烟。
  运行期数据用 BBHOUSE_DATA_DIR 隔离到 build 内,不动用户真实数据库。
.EXAMPLE
  pwsh -File scripts/package-release.ps1
  pwsh -File scripts/package-release.ps1 -OutDir E:\rel -KeepOpenGlSw -Force
#>
[CmdletBinding()]
param(
    [string]$OutDir = "D:\release",
    [string]$Name,
    [switch]$KeepOpenGlSw,
    [switch]$SkipSmoke,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$deploy = Join-Path $build "deploy"
$stage = Join-Path $build "release-stage"

# Qt Creator 同款套件;换机器/升级 Qt 时改这三行(或直接用 CMakePresets.json 的 preset)
$qtBin = "C:\Qt\6.11.1\mingw_64\bin"
$mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
$ninjaBin = "C:\Qt\Tools\Ninja"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"

foreach ($p in @($qtBin, $mingwBin, $ninjaBin, $cmake)) {
    if (-not (Test-Path $p)) { throw "缺少路径: $p(按需修改脚本顶部)" }
}
$env:Path = "$mingwBin;$qtBin;$ninjaBin;$env:Path"

if (-not $Name) {
    $hash = (& git -C $root rev-parse --short HEAD).Trim()
    if (-not $hash) { throw "取提交号失败(git rev-parse)" }
    $Name = "bbhouse-qt[$hash]"
}
$target = Join-Path $OutDir $Name

Write-Host "== 1/4 配置 + 构建 + deploy =="
& $cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "cmake 配置失败" }
& $cmake --build $build --target deploy -j ([Environment]::ProcessorCount)
if ($LASTEXITCODE -ne 0) { throw "deploy 目标失败" }

Write-Host "== 2/4 精简 =="
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
Copy-Item -LiteralPath $deploy -Destination $stage -Recurse

# 整目录剔除
$dropDirs = @(
    "translations",                                  # 程序只加载自带 :/i18n/bbhouse_en_US.qm,Qt 的 qt_*.qm 从不加载
    "qmltooling",                                    # QML 调试工具
    "generic",                                       # TUIO 触摸输入(未用)
    "vectorimageformats",                            # Lottie 矢量图(未用)
    "networkinformation",                            # QNetworkListManager(Neteork 状态可自降级)
    "styles",                                        # qmodernwindowsstyle(QT_QUICK_CONTROLS_STYLE=Basic)
    "qml\QtQuick\NativeStyle",                       # 程序固定 QT_QUICK_CONTROLS_STYLE=Basic
    "qml\QtQuick\Controls\FluentWinUI3",
    "qml\QtQuick\Controls\Windows",
    "qml\QtQuick\Controls\Material",
    "qml\QtQuick\Controls\Imagine",
    "qml\QtQuick\Controls\Fusion",
    "qml\QtQuick\Controls\Universal"
)
# 单文件剔除
$dropFiles = @(
    "fluentuiplugin.dll",                            # exe 导入表里没有它(objdump 核对),QML 走 FluentUI\ 下那份
    "FluentUI\plugins.qmltypes",                     # QML 元数据,仅开发工具用
    "FluentUI\libfluentuiplugin.a",                  # 导入库,仅链接期用
    "Qt6Svg.dll", "iconengines\qsvgicon.dll", "imageformats\qsvg.dll",   # 工程内无 svg 资源
    "tls\qcertonlybackend.dll",                      # 只留 Windows 原生 qschannelbackend
    "Qt6Lottie.dll", "Qt6LottieVectorImageGenerator.dll", "Qt6QuickVectorImageGenerator.dll",
    "Qt6Quick3DUtils.dll",
    "Qt6QuickControls2Fusion.dll", "Qt6QuickControls2FusionStyleImpl.dll",
    "Qt6QuickControls2Imagine.dll", "Qt6QuickControls2ImagineStyleImpl.dll",
    "Qt6QuickControls2Material.dll", "Qt6QuickControls2MaterialStyleImpl.dll",
    "Qt6QuickControls2Universal.dll", "Qt6QuickControls2UniversalStyleImpl.dll",
    "Qt6QuickControls2FluentWinUI3StyleImpl.dll", "Qt6QuickControls2WindowsStyleImpl.dll"
)
if (-not $KeepOpenGlSw) {
    $dropFiles += "opengl32sw.dll"                   # 软件 OpenGL 回退:仅无显卡驱动/远程桌面才需要
}
# 插件白名单:SQLite、封面/头像用的图片格式(jpg/webp;avif 无插件,程序走 CDN 转码)
$keepSqlDrivers = @("qsqlite.dll")
$keepImageFormats = @("qjpeg.dll", "qwebp.dll")
# 保留项说明:Qt6QuickShapes 虽未被本项目 QML 直接使用,但 FluentUI 的 FluTour.qml 里
# `import QtQuick.Shapes`,删掉会让该控件运行期才报模块缺失(只在 0.38MB),故保留。

foreach ($d in $dropDirs) {
    $p = Join-Path $stage $d
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force }
}
foreach ($f in $dropFiles) {
    $p = Join-Path $stage $f
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
}
foreach ($dir in @("sqldrivers", "imageformats")) {
    $keep = if ($dir -eq "sqldrivers") { $keepSqlDrivers } else { $keepImageFormats }
    $p = Join-Path $stage $dir
    if (-not (Test-Path -LiteralPath $p)) { continue }
    Get-ChildItem -LiteralPath $p -File | Where-Object { $keep -notcontains $_.Name } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
}

function Invoke-SelfContainedSmoke([string]$dir) {
    # 清空 PATH:只用包内文件,缺 DLL/插件的包会在这里暴露。
    # 用默认平台(不是 offscreen):这样"进程活着但窗口没起来"也能被发现(本项目踩过:
    # FluRouter 单例被 qmldir 当普通类型 → navigate 报错 → 首个窗口永不创建)。
    $savedPath = $env:Path
    $env:Path = "$env:SystemRoot\system32;$env:SystemRoot"
    $env:BBHOUSE_SMOKE = "1"
    $env:BBHOUSE_SMOKE_NAV = "LocalHistoryPage.qml,SettingsPage.qml"
    $env:BBHOUSE_DATA_DIR = Join-Path $build "smoke-data"
    # 无控制台时 Qt 默认把日志丢给调试器,重定向就抓不到;强制走 stderr
    $env:QT_ASSUME_STDERR_HAS_CONSOLE = "1"
    $log = Join-Path $build "smoke-err.log"
    $outLog = Join-Path $build "smoke-out.log"
    $windowSeen = $false
    try {
        # 用 ProcessStartInfo 直接起进程:Start-Process 会把 "<项目名>[<提交号>]" 里的
        # [..] 当通配符,路径解析失败。
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = Join-Path $dir "bbhouse-qt.exe"
        $psi.WorkingDirectory = $dir
        $psi.UseShellExecute = $false
        $psi.RedirectStandardError = $true
        $psi.RedirectStandardOutput = $true
        $proc = [System.Diagnostics.Process]::Start($psi)
        # 先轮询窗口(输出量很小,不会填满管道),再收尾读日志
        for ($i = 0; $i -lt 40; $i++) {
            Start-Sleep -Milliseconds 250
            $proc.Refresh()
            if ($proc.MainWindowHandle -ne 0) { $windowSeen = $true; break }
            if ($proc.HasExited) { break }
        }
        # 退出慢不代表包有问题:收尾要等后台 HTTP 任务(QThreadPool::waitForDone),
        # 网络卡顿时可能超过 30s。判定标准是"窗口出现 + 日志无 QML 错误"。
        $exitedInTime = $proc.WaitForExit(45000)
        if (-not $exitedInTime) {
            Write-Warning ("冒烟进程 45s 未退出(窗口已出现=$windowSeen),按警告处理并结束它: {0}" -f $psi.FileName)
            $proc.Kill()
        }
        $out2 = $proc.StandardOutput.ReadToEnd()
        $err2 = $proc.StandardError.ReadToEnd()
        $code = if ($exitedInTime) { $proc.ExitCode } else { 0 }
        $err2 | Out-File -LiteralPath $log -Encoding utf8
        $out2 | Out-File -LiteralPath $outLog -Encoding utf8
    } finally {
        $env:Path = $savedPath
        foreach ($v in @("BBHOUSE_SMOKE", "BBHOUSE_SMOKE_NAV", "BBHOUSE_DATA_DIR", "QT_ASSUME_STDERR_HAS_CONSOLE")) {
            Remove-Item "Env:$v" -ErrorAction SilentlyContinue
        }
    }
    $out = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue) +
           @(Get-Content -LiteralPath $outLog -ErrorAction SilentlyContinue)
    $bad = $out | Select-String -Pattern "is not installed|Cannot load library|cannot be loaded|Failed to load|no such file|QQmlApplicationEngine failed|TypeError|is not a function|ReferenceError|Unable to assign"
    if ($code -ne 0) { throw "冒烟失败(exit=$code,$log):`n$($out | Select-Object -Last 20 | Out-String)" }
    if ($bad) { throw "冒烟发现 QML/插件缺失($log):`n$($bad | Out-String)" }
    if (-not $windowSeen) { throw "冒烟期间没有出现窗口(进程可启动但界面没起来,$log):`n$($out | Select-Object -Last 20 | Out-String)" }
    Write-Host ("   冒烟通过(exit=0,窗口已出现,日志 {0})" -f $log)
}

if (-not $SkipSmoke) {
    Write-Host "== 3/4 自包含冒烟(暂存目录) =="
    Invoke-SelfContainedSmoke $stage
} else {
    Write-Host "== 3/4 冒烟已跳过(-SkipSmoke) =="
}

Write-Host "== 4/4 输出到发布目录 =="
if (Test-Path -LiteralPath $target) {
    if (-not $Force) { throw "目标已存在: $target(加 -Force 覆盖,或用 -Name 换名;删除项目外目录需你确认)" }
    Remove-Item -LiteralPath $target -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item -LiteralPath $stage -Destination $target -Recurse

if (-not $SkipSmoke) {
    Write-Host "   发布目录冒烟:"
    Invoke-SelfContainedSmoke $target
}

$files = Get-ChildItem -LiteralPath $target -Recurse -File
$mb = [math]::Round(($files | Measure-Object -Sum Length).Sum / 1MB, 1)
Write-Host ""
Write-Host "包目录: $target"
Write-Host "文件数: $($files.Count)"
Write-Host "体积  : $mb MB"
