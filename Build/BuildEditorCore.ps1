param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("WindowsX64")]
    [string]$TargetPlatform = "WindowsX64",

    [string]$MSBuildPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

function Find-MSBuild()
{
    if (![string]::IsNullOrWhiteSpace($MSBuildPath))
    {
        $candidate = $MSBuildPath
        if (![System.IO.Path]::IsPathRooted($candidate))
        {
            $candidate = Join-Path $repoRoot $candidate
        }

        if (Test-Path -LiteralPath $candidate)
        {
            return (Resolve-Path -LiteralPath $candidate).Path
        }

        throw "MSBuild executable was not found: $candidate"
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere))
    {
        throw "vswhere.exe was not found. Install Visual Studio C++ build tools."
    }

    $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsRoot)
    {
        throw "A Visual Studio installation with the x64 C++ toolchain was not found."
    }

    $candidate = Join-Path $vsRoot "MSBuild\Current\Bin\MSBuild.exe"
    if (!(Test-Path -LiteralPath $candidate))
    {
        throw "MSBuild executable was not found below the selected Visual Studio installation."
    }

    return $candidate
}

$coreProject = Join-Path $repoRoot "OrbedenCore\OrbedenCore.vcxproj"

switch ($TargetPlatform)
{
    "WindowsX64"
    {
        $vsPlatform = "x64"
        $nativePlatform = "WindowsX64"
    }
}

$outputDir = Join-Path $repoRoot "OrbedenEditor\Sdk\Native\$nativePlatform\$Configuration"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$outputDirValue = $outputDir.TrimEnd([char[]]@('\', '/')) + "\"
$msbuild = Find-MSBuild

& $msbuild $coreProject `
    /m `
    "/p:Configuration=$Configuration" `
    "/p:Platform=$vsPlatform" `
    "/p:OrbedenCoreOutputDir=$outputDirValue"

if ($LASTEXITCODE -ne 0)
{
    throw "Build OrbedenCore failed for $TargetPlatform $Configuration."
}

$libraryPath = Join-Path $outputDir "OrbedenCore.lib"
if (!(Test-Path -LiteralPath $libraryPath))
{
    throw "Build finished but did not produce $libraryPath."
}

Write-Host "Built Editor Core static library: $libraryPath"
