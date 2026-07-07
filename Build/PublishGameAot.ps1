param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("WindowsX64", "LinuxX64", "LinuxX64Gcc", "FreeBsdX64", "Switch")]
    [string]$TargetPlatform = "WindowsX64",

    [string]$ProjectRoot = "",

    [string]$ScriptProject = "",

    [string]$SdkPath = "",

    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"

function Get-ProjectAssemblyName([string]$ProjectPath)
{
    [xml]$projectXml = Get-Content -Raw -LiteralPath $ProjectPath
    $nodes = $projectXml.SelectNodes("//AssemblyName")
    foreach ($node in $nodes)
    {
        $value = $node.InnerText.Trim()
        if (![string]::IsNullOrWhiteSpace($value))
        {
            return $value
        }
    }

    return [System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($ProjectRoot))
{
    throw "ProjectRoot is required."
}

if ([string]::IsNullOrWhiteSpace($ScriptProject))
{
    throw "ScriptProject is required."
}

$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$scriptProjectPath = (Resolve-Path -LiteralPath $ScriptProject).Path
if ([string]::IsNullOrWhiteSpace($SdkPath))
{
    $SdkPath = Join-Path $repoRoot "OrbedenGame\Sdk"
}

$sdkPathValue = (Resolve-Path -LiteralPath $SdkPath).Path
$runtimeAssembly = Join-Path $sdkPathValue "Managed\OrbedenCore.CSharp\OrbedenCore.CSharp.dll"
$assemblyName = Get-ProjectAssemblyName $scriptProjectPath

switch ($TargetPlatform)
{
    "WindowsX64"
    {
        $rid = "win-x64"
        $targetName = "windows-x64"
        $libraryName = "$assemblyName.lib"
    }
    "LinuxX64"
    {
        $rid = "linux-x64"
        $targetName = "linux-x64-clang"
        $libraryName = "lib$assemblyName.a"
    }
    "LinuxX64Gcc"
    {
        $rid = "linux-x64"
        $targetName = "linux-x64-gcc"
        $libraryName = "lib$assemblyName.a"
    }
    "FreeBsdX64"
    {
        $rid = "freebsd-x64"
        $targetName = "freebsd-x64"
        $libraryName = "lib$assemblyName.a"
    }
    "Switch"
    {
        throw "Switch NativeAOT publishing requires a vendor SDK RID/toolchain integration. This repository only contains the CMake entry skeleton."
    }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot))
{
    $OutputRoot = Join-Path $projectRootPath "Aot"
}

$outputDir = Join-Path $OutputRoot "$targetName\$Configuration"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = "1"
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = "1"

if (!(Test-Path -LiteralPath $runtimeAssembly))
{
    throw "Missing Runtime SDK assembly: $runtimeAssembly. Build OrbedenCore.CSharp.csproj in Visual Studio first."
}

dotnet restore $scriptProjectPath `
    -r $rid `
    /p:PublishAot=true `
    /p:NativeLib=Static `
    "/p:OrbedenSdkPath=$sdkPathValue\" `
    /p:RestoreSources=
if ($LASTEXITCODE -ne 0)
{
    throw "NativeAOT restore failed for $TargetPlatform ($rid). Install matching local NativeAOT packs through the SDK or Visual Studio installer; external NuGet sources are disabled for this repo."
}

dotnet publish $scriptProjectPath `
    --no-restore `
    -c $Configuration `
    -r $rid `
    /p:PublishAot=true `
    /p:NativeLib=Static `
    "/p:OrbedenSdkPath=$sdkPathValue\" `
    /p:RestoreSources= `
    -o $outputDir
if ($LASTEXITCODE -ne 0)
{
    throw "NativeAOT publish failed for $TargetPlatform ($rid)."
}

$libraryPath = Join-Path $outputDir $libraryName
if (!(Test-Path -LiteralPath $libraryPath))
{
    throw "NativeAOT publish finished but did not produce $libraryPath"
}

Write-Host "Published NativeAOT static library: $libraryPath"
