# 先构建 Core Debug；测试真实资源导入、产物写出与从产物读回，无需图形窗口。
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$previousPath = $env:PATH
Push-Location $projectRoot
try {
    . (Join-Path $PSScriptRoot 'FindMSBuild.ps1')
    & (Get-MSBuildPath) Build/Tests/CookedAssetRoundTrip.vcxproj /m /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "Cooked asset test compilation failed." }
    $env:PATH = (Join-Path $projectRoot 'OrbedenEditor/Sdk/Native/WindowsX64/Debug') + ';' + (Join-Path $projectRoot 'OrbedenCore/Src/ThirdParty/glfw/lib-vc2022') + ';' + $previousPath
    & ./Log/CookedAssetRoundTrip/CookedAssetRoundTrip.exe
    if ($LASTEXITCODE -ne 0) { throw "Cooked asset round trip tests failed: $LASTEXITCODE" }
}
finally {
    $env:PATH = $previousPath
    Pop-Location
}
