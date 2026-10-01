# 本地一键打包 Class Widgets Next Windows 安装器（Inno Setup 6）。
# 流程：构建 + cmake --install + windeployqt（-SkipBuild 可跳过，直接用现有 dist/）
#       -> 清掉 dist/logs 残留日志 -> 定位 ISCC.exe -> 编译 scripts/cwn-cicd.iss
# 产物：output/ClassWidgets-<版本>-Win-Installer.exe
# 依赖：Qt 环境（PATH 含 windeployqt）、Inno Setup 6（winget install JRSoftware.InnoSetup）。
# CI 复用同一脚本（.github/workflows/build.yml 以 -SkipBuild 调用）。
#
# 打包前会断言 dist 里的 MSVC 运行库与 dxcompiler/dxil 齐备 —— windeployqt 找不到它们时
# 只打一行 warning、退出码仍是 0，否则会静默产出一个装完可能起不来的安装器。
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

# ---- 工具函数 ----

# 定位 vcvarsall.bat：vswhere -> VSINSTALLDIR -> 常见安装路径。
# 必须用 vswhere 而非写死路径：VS 装在非默认盘符/路径时（本机 E:\VSBuild），
# windeployqt 自身探测不到，会漏掉运行库。
function Get-VcVarsAllPath {
    $candidates = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (${env:ProgramFiles(x86)} -and (Test-Path $vswhere)) {
        $vsRoot = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($vsRoot) {
            $candidates += (Join-Path ($vsRoot | Select-Object -First 1).Trim() `
                'VC\Auxiliary\Build\vcvarsall.bat')
        }
    }
    if ($env:VSINSTALLDIR) {
        $candidates += (Join-Path $env:VSINSTALLDIR 'VC\Auxiliary\Build\vcvarsall.bat')
    }
    foreach ($ed in 'Community', 'Professional', 'Enterprise', 'BuildTools') {
        $candidates += "$env:ProgramFiles\Microsoft Visual Studio\2022\$ed\VC\Auxiliary\Build\vcvarsall.bat"
    }
    $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

# 把 vcvarsall 的环境导入当前进程，使 windeployqt 能定位 MSVC 运行库与 Windows SDK
# 的 dxcompiler/dxil。vcvarsall 只对其自身进程树生效，故导出后逐条并入当前会话。
function Initialize-MSVCEnvironment {
    $vcvars = Get-VcVarsAllPath
    if (-not $vcvars) {
        Write-Warning '未找到 vcvarsall.bat：windeployqt 可能定位不到 MSVC 运行库与 dxcompiler'
        return $false
    }
    Write-Host "==> 初始化 MSVC 环境: $vcvars"
    $envLines = & cmd /c "call `"$vcvars`" x64 >nul 2>&1 && set"
    foreach ($line in $envLines) {
        # set 输出的 "=C:=C:\..." 这类以 = 开头的行不匹配 [^=]+
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    if (-not $env:VCINSTALLDIR) {
        Write-Warning 'vcvarsall 执行后 VCINSTALLDIR 仍为空'
        return $false
    }
    Write-Host "    VCINSTALLDIR=$($env:VCINSTALLDIR)"
    return $true
}

# 断言 windeployqt 的产物：MSVC 运行库 + dxcompiler/dxil 缺一不可。
# 缺运行库 = 没装 VC++ 运行库的机器上装完起不来；缺 dxcompiler = Qt6ShaderTools
# 运行时 HLSL 编译失败（本项目用 graphical effects，会踩到）。
# 该断言在两种模式下都跑：dist 缺这些文件时，安装器本身就是坏的，无论谁构建的 dist。
function Assert-DeployedRuntime {
    $dist = Join-Path $repoRoot 'dist'
    # 运行库有两种可能形态：windeployqt 找不到 redist 时内嵌 vc_redist.exe，
    # 找得到时直接铺 msvcp140/vcruntime140*.dll，任一存在即可
    $runtimeHit = @('vc_redist.x64.exe', 'msvcp140.dll', 'vcruntime140.dll') |
        Where-Object { Test-Path (Join-Path $dist $_) }
    $missing = @()
    if (-not $runtimeHit) {
        $missing += 'MSVC 运行库（vc_redist.x64.exe 或 msvcp140.dll 等）'
    }
    foreach ($f in 'dxcompiler.dll', 'dxil.dll') {
        if (-not (Test-Path (Join-Path $dist $f))) { $missing += $f }
    }
    if ($missing) {
        # 这里不用 Write-Error：$ErrorActionPreference='Stop' 会让它直接抛异常，
        # 下面的排查指引就来不及打印了。改为显式输出后 exit 1。
        Write-Host "!! dist 缺少运行时文件，装出来的安装器会缺依赖：$($missing -join '、')" -ForegroundColor Red
        Write-Host '  常见原因：windeployqt 找不到 Visual Studio 安装目录（VS 装在非默认路径时'
        Write-Host '  VCINSTALLDIR 未设置），或 Windows SDK 缺失。排查顺序：'
        Write-Host '    1) 确认能找到 vcvarsall.bat（vswhere -latest -products * -property installationPath）；'
        Write-Host '    2) 从 %VCToolsRedistDir%\vc_redist.x64.exe 补运行库；'
        Write-Host '    3) 从 Windows Kits\10\bin\<ver>\x64\ 补 dxcompiler.dll 与 dxil.dll。'
        Write-Host '  windeployqt 完整输出见 build\deploy-output.log'
        exit 1
    }
    Write-Host ('==> 运行时校验通过（运行库: ' + ($runtimeHit -join ' / ') + '，dxcompiler + dxil 齐备）')
}

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
    # 必须把 -Version 传进 cmake：CWN_VERSION 是 CACHE 变量，已有的 build/ 里缓存着
    # 旧版本号，只改 CMakeLists.txt 的默认值对存量构建目录无效；不传则 exe 内嵌的
    # QGuiApplication 版本与 .iss 的 AppVersion/产物名会不一致。
    #   - 直接写 -DCWN_VERSION（而非 -DCWN_VERSION_OVERRIDE）：后者同样会被 CMake
    #     记进缓存并长期驻留，日后即使改了 CMakeLists.txt 默认值也会被它盖掉，悄无声息
    #     地编出旧版本号；写 CWN_VERSION 则缓存值本身就是本次请求的版本，不存在旁路。
    #   - -U 先清掉 CWN_VERSION_OVERRIDE 残留（CI 或早期调用可能已写入缓存），
    #     否则它优先级高于 CWN_VERSION（见 CMakeLists.txt 版本段），仍会劫持版本号。
    #   - 整个 -D 参数必须加引号：PowerShell 5.1 向原生程序传 -Dfoo=2.1.0.4 时会在
    #     点号处拆成 "-Dfoo=2" + ".1.0.4"，后者被 cmake 当成多余路径（Ignoring extra path）。
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -U CWN_VERSION_OVERRIDE "-DCWN_VERSION=$Version"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host '==> 安装运行时资源到 dist/'
    cmake --install build --prefix dist
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host '==> 部署 Qt 运行时（windeployqt）'
    # windeployqt 依赖 VCINSTALLDIR / Windows SDK 变量定位 MSVC 运行库与 dxcompiler。
    # VS 装在非默认路径（本机 E:\VSBuild）时这些变量缺失，它只会 warning 一句就继续，
    # 于是静默漏掉 vc_redist.x64.exe + dxcompiler.dll + dxil.dll。故先初始化环境。
    Initialize-MSVCEnvironment | Out-Null
    $deployLog = Join-Path $repoRoot 'build\deploy-output.log'
    New-Item -ItemType Directory -Force (Split-Path -Parent $deployLog) | Out-Null
    # 2>&1 把 windeployqt 打到 stderr 的 warning 也收进日志，供下面的断言与排查使用
    & windeployqt --release --compiler-runtime --qmldir app dist/ClassWidgetsNext.exe 2>&1 |
        Tee-Object -FilePath $deployLog | Out-Null
    $deployExit = $LASTEXITCODE
    if ($deployExit -ne 0) {
        Get-Content $deployLog -Tail 40 | ForEach-Object { Write-Host "  $_" }
        Write-Error "windeployqt 失败（exit $deployExit），详见 $deployLog"
        exit $deployExit
    }
    # 提前把 warning 顶到控制台：它既不改变退出码也不打印到 stdout，默认会被 Tee 吞掉
    Select-String -Path $deployLog -Pattern 'Cannot find|Warning:' |
        ForEach-Object { Write-Warning $_.Line.Trim() }
} else {
    if (-not (Test-Path 'dist\ClassWidgetsNext.exe')) {
        Write-Error "未找到 dist\ClassWidgetsNext.exe：-SkipBuild 需要一个已构建好的 dist/ 目录"
        exit 1
    }
}

# ---- 3. 校验运行时依赖齐备（两种模式都跑，见 Assert-DeployedRuntime 注释）----
Assert-DeployedRuntime

# ---- 4. 清掉运行/冒烟测试残留的日志（.iss 的 Excludes 也会兜底排除）----
Remove-Item 'dist\logs\*.log' -ErrorAction SilentlyContinue | Out-Null

# ---- 5. 定位 ISCC.exe（PATH -> 标准安装路径 -> 本机 D 盘工具目录）----
$isccPath = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $isccPath) {
    # 兼容 Windows PowerShell 5.1：不用 ?? 运算符，逐个试候选路径
    $candidates = @(
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "D:\WorkBuddy_WorkSpace\_tools\InnoSetup6\ISCC.exe"  # 本机便携安装位置
    )
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

# ---- 6. 编译安装器 ----
$env:CWN_VERSION = $Version
& $isccPath (Join-Path $repoRoot 'scripts\cwn-cicd.iss')
if ($LASTEXITCODE -ne 0) {
    Write-Error "ISCC 编译失败（exit $LASTEXITCODE）"
    exit $LASTEXITCODE
}

# ---- 7. 校验并报告产物 ----
$installer = Join-Path $repoRoot "output\ClassWidgets-$Version-Win-Installer.exe"
if (-not (Test-Path $installer)) {
    Write-Error "编译成功但未找到产物: $installer"
    exit 1
}
$sizeMB = [math]::Round((Get-Item $installer).Length / 1MB, 1)
Write-Host "== 安装器已生成: $installer ($sizeMB MB)"
