# 定位并导入 MSVC 构建环境（vcvarsall.bat 注入的环境变量）到当前 PowerShell 进程。
# 供 scripts\build-installer.ps1（本地一键打包）与 .github\workflows\build.yml
# （CI 的 windeployqt 步骤）共用 —— 单一实现，避免两处逻辑漂移。
#
# 为什么必须有：
#   windeployqt 依赖 VCINSTALLDIR / VCToolsRedistDir / WindowsSdkVerBinPath 这几个变量
#   定位 MSVC 运行库与 dxcompiler.dll、dxil.dll。这些变量不会自动出现在会话里 ——
#   VS 装在非默认路径时（本机 E:\VSBuild）连 windeployqt 自身的探测都会失败。
#   缺了它们 windeployqt 只打一行 warning、退出码仍是 0：
#       Warning: Cannot find any version of the dxcompiler.dll and dxil.dll.
#       Warning: Cannot find Visual Studio installation directory, VCINSTALLDIR is not set.
#   于是 dist/ 静默少了 vc_redist.x64.exe（没装 VC++ 运行库的机器上装完起不来）
#   与 dxcompiler/dxil（Qt6ShaderTools 运行期 HLSL 编译失败）。
#
# 用法（导入与 windeployqt 必须在同一个进程内完成 —— GitHub Actions 每个 step 是
# 独立进程，导出到环境变量的做法跨 step 无效，故不能拆成单独的 step）：
#   . scripts\msvc-env.ps1
#   Initialize-MSVCEnvironment
#   windeployqt --release --compiler-runtime --qmldir app dist\ClassWidgetsNext.exe

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