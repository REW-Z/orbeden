param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("WindowsX64")]
    [string]$TargetPlatform = "WindowsX64",

    [string]$MSBuildPath = ""
)

$ErrorActionPreference = "Stop"

function Find-MSBuild()
{
    if (![string]::IsNullOrWhiteSpace($MSBuildPath))
    {
        return $MSBuildPath
    }

    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "msbuild"
    )

    foreach ($candidate in $candidates)
    {
        if ($candidate -eq "msbuild")
        {
            return $candidate
        }

        if (Test-Path -LiteralPath $candidate)
        {
            return $candidate
        }
    }

    return "msbuild"
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
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
