# 先构建 Core Debug；测试真实导入、地形生成、保存和重载，无需图形窗口。
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$previousPath = $env:PATH
Push-Location $projectRoot
try {
    . (Join-Path $PSScriptRoot 'FindMSBuild.ps1')
    & (Get-MSBuildPath) Build/Tests/HeightFieldPersistence.vcxproj /m /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "HeightField test compilation failed." }
    $env:PATH = (Join-Path $projectRoot 'OrbedenEditor/Sdk/Native/WindowsX64/Debug') + ';' + (Join-Path $projectRoot 'OrbedenCore/Src/ThirdParty/glfw/lib-vc2022') + ';' + $previousPath
    & ./Log/HeightFieldPersistence/HeightFieldPersistence.exe
    if ($LASTEXITCODE -ne 0) { throw "HeightField persistence tests failed: $LASTEXITCODE" }
}
finally {
    $env:PATH = $previousPath
    Pop-Location
}
