# 验证资源导入、打包、场景持久化和隐藏 OpenGL 上下文中的反射
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$previousPath = $env:PATH
Push-Location $projectRoot
try {
    . (Join-Path $PSScriptRoot 'FindMSBuild.ps1')
    & (Get-MSBuildPath) Build/Tests/EnvironmentReflection.vcxproj /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Environment reflection test compilation failed.' }
    $env:PATH = (Join-Path $projectRoot 'OrbedenEditor/Sdk/Native/WindowsX64/Debug') + ';' + (Join-Path $projectRoot 'OrbedenCore/Src/ThirdParty/glfw/lib-vc2022') + ';' + $previousPath
    & ./Log/EnvironmentReflection/EnvironmentReflection.exe
    if ($LASTEXITCODE -ne 0) { throw "Environment reflection tests failed: $LASTEXITCODE" }
}
finally {
    $env:PATH = $previousPath
    Pop-Location
}
