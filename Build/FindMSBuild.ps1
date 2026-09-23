# 定位 MSBuild.exe，供 Build/ 下的脚本点源复用。
# 先问 vswhere 要最新 Visual Studio 实例，再退回 PATH。Visual Studio 的安装盘符、版本和版本号都由用户自选，
# 写死安装路径必然在别人的机器上失效。
function Get-MSBuildPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        # 先只接受装了 x64 C++ 生成工具的实例，查不到再放宽条件。
        $installPath = & $vswhere -latest -prerelease -products * -requires "Microsoft.VisualStudio.Component.VC.Tools.x86.x64" -property installationPath -utf8 | Select-Object -First 1
        if (-not $installPath) {
            $installPath = & $vswhere -latest -prerelease -products * -property installationPath -utf8 | Select-Object -First 1
        }
        if ($installPath) {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }

    $onPath = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    throw "MSBuild.exe was not found. Install Visual Studio with the C++ workload, or run this script from a Developer Command Prompt."
}
