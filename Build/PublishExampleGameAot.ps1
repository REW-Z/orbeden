param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("Win32", "x64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$rid = if ($Platform -eq "Win32") { "win-x86" } else { "win-x64" }
$outputDir = Join-Path $repoRoot "ExampleProject\Aot\$Platform\$Configuration"
$project = Join-Path $repoRoot "ExampleProject\Script\ExampleGame.csproj"
$sdkPath = Join-Path $repoRoot "OrbedenEditor\Sdk"
$runtimeAssembly = Join-Path $sdkPath "Managed\OrbedenCore.CSharp\OrbedenCore.CSharp.dll"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = "1"
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = "1"

if (!(Test-Path -LiteralPath $runtimeAssembly))
{
    throw "Missing Runtime SDK assembly: $runtimeAssembly. Build OrbedenCore.CSharp.csproj in Visual Studio first."
}

dotnet restore $project `
    -r $rid `
    /p:PublishAot=true `
    /p:NativeLib=Static `
    "/p:OrbedenSdkPath=$sdkPath\" `
    /p:RestoreSources=
if ($LASTEXITCODE -ne 0)
{
    throw "NativeAOT restore failed. Install the .NET 10 NativeAOT/ILCompiler packs locally through the SDK or Visual Studio installer; external NuGet sources are disabled for this repo."
}

dotnet publish $project `
    --no-restore `
    -c $Configuration `
    -r $rid `
    /p:PublishAot=true `
    /p:NativeLib=Static `
    "/p:OrbedenSdkPath=$sdkPath\" `
    /p:RestoreSources= `
    -o $outputDir
if ($LASTEXITCODE -ne 0)
{
    throw "NativeAOT publish failed."
}

$libraryPath = Join-Path $outputDir "ExampleGame.lib"
if (!(Test-Path -LiteralPath $libraryPath))
{
    throw "NativeAOT publish finished but did not produce $libraryPath"
}

Write-Host "Published NativeAOT static library: $libraryPath"
