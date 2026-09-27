# 本地一键打包 Class Widgets Next Windows 安装器（Inno Setup 6）。
# 流程：构建 + cmake --install + windeployqt（-SkipBuild 可跳过，直接用现有 dist/）
#       -> 清掉 dist/logs 残留日志 -> 定位 ISCC.exe -> 编译 scripts/cwn-cicd.iss
# 产物：output/ClassWidgets-<版本>-Win-Installer.exe
# 依赖：Qt 环境（PATH 含 windeployqt）、Inno Setup 6（winget install JRSoftware.InnoSetup）。
# CI 复用同一脚本（.github/workflows/build.yml 以 -SkipBuild 调用）。
#
# 用法：
#   scripts\build-installer.ps1                # 完整流程：构建 + 部署 + 打安装器
#   scripts\build-installer.ps1 -SkipBuild     # 复用现有 dist/，只打安装器
#   scripts\build-installer.ps1 -Version 2.1.0 # 指定版本号（缺省解析 CMakeLists 的 CWN_VERSION）
[CmdletBinding()]
param(
    [string]$Version,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# ---- 1. 版本号：缺省从 CMakeLists.txt 解析 CWN_VERSION ----
if (-not $Version) {
    $cmakeLists = Get-Content (Join-Path $repoRoot 'CMakeLists.txt') -Raw
    if ($cmakeLists -match 'set\(\s*CWN_VERSION\s+"([^"]+)"') {
        $Version = $Matches[1]
    } else {
        Write-Error "无法从 CMakeLists.txt 解析 CWN_VERSION，请用 -Version 显式指定"
        exit 1
    }
}
Write-Host "==> 版本号: $Version"

# ---- 2. 构建 + 部署运行时根（-SkipBuild 跳过，直接用现有 dist/）----
if (-not $SkipBuild) {
    if (-not (Get-Command windeployqt -ErrorAction SilentlyContinue)) {
        Write-Error "PATH 中找不到 windeployqt，请先进入 Qt 环境（把 Qt 的 bin 目录加入 PATH）"
        exit 1
    }
    Write-Host '==> CMake 配置 + 构建'
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host '==> 安装运行时资源到 dist/'
    cmake --install build --prefix dist
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host '==> 部署 Qt 运行时（windeployqt）'
    windeployqt --release --compiler-runtime --qmldir app dist/ClassWidgetsNext.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    if (-not (Test-Path 'dist\ClassWidgetsNext.exe')) {
        Write-Error "未找到 dist\ClassWidgetsNext.exe：-SkipBuild 需要一个已构建好的 dist/ 目录"
        exit 1
    }
}

# ---- 3. 清掉运行/冒烟测试残留的日志（.iss 的 Excludes 也会兜底排除）----
Remove-Item 'dist\logs\*.log' -ErrorAction SilentlyContinue | Out-Null

# ---- 4. 定位 ISCC.exe（PATH -> Program Files 标准 安装路径）----
$isccPath = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $isccPath) {
    # 兼容 Windows PowerShell 5.1：不用 ?? 运算符，逐个试标准安装路径
    $candidates = @("$env:ProgramFiles\Inno Setup 6\ISCC.exe")
    if (${env:ProgramFiles(x86)}) {
        $candidates += "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    }
    $isccPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $isccPath) {
    Write-Error "未找到 Inno Setup 6（ISCC.exe）。请先安装：winget install JRSoftware.InnoSetup"
    exit 1
}
Write-Host "==> ISCC: $isccPath"

# ---- 5. 编译安装器 ----
$env:CWN_VERSION = $Version
& $isccPath (Join-Path $repoRoot 'scripts\cwn-cicd.iss')
if ($LASTEXITCODE -ne 0) {
    Write-Error "ISCC 编译失败（exit $LASTEXITCODE）"
    exit $LASTEXITCODE
}

# ---- 6. 校验并报告产物 ----
$installer = Join-Path $repoRoot "output\ClassWidgets-$Version-Win-Installer.exe"
if (-not (Test-Path $installer)) {
    Write-Error "编译成功但未找到产物: $installer"
    exit 1
}
$sizeMB = [math]::Round((Get-Item $installer).Length / 1MB, 1)
Write-Host "== 安装器已生成: $installer ($sizeMB MB)"
