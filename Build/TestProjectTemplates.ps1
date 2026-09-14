# 验证模板文件同步；只在工作区 Log/ 内生成临时项目，不修改真实模板。
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path "Log/ProjectTemplateTests" | Out-Null
    & g++ -std=c++20 -I OrbedenCore/Src -I OrbedenEditor/Src Build/Tests/ProjectTemplates.cpp OrbedenEditor/Src/Editor/NewProjectTemplate.cpp -o Log/ProjectTemplateTests/ProjectTemplates.exe
    if ($LASTEXITCODE -ne 0) { throw "Template test compilation failed." }
    & ./Log/ProjectTemplateTests/ProjectTemplates.exe
    if ($LASTEXITCODE -ne 0) { throw "Template regression tests failed." }
}
finally {
    Pop-Location
}
