param([switch]$Render)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$testOutput = Join-Path $repositoryRoot '.tmp/flight-regression-generated'
$nativeSdk = Join-Path $repositoryRoot 'OrbedenEditor/Sdk/Native/WindowsX64/Debug'
$glfwDirectory = Join-Path $repositoryRoot 'OrbedenCore/Src/ThirdParty/glfw/lib-vc2022'
$flightTemplate = Join-Path $repositoryRoot 'OrbedenEditor/Templates/FlightTraining'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$visualStudio) { throw 'Visual Studio C++ tools are required.' }
if (!(Test-Path (Join-Path $nativeSdk 'OrbedenCore.dll'))) { throw 'Build OrbedenCore Debug x64 first.' }

#加载编译环境，所有输出限定在工作区临时目录。
& (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
New-Item -ItemType Directory -Force $testOutput | Out-Null
Push-Location $repositoryRoot
$previousPath = $env:PATH
try {
    & dotnet run --project Tools/OrbedenMetaGen/OrbedenMetaGen.csproj -- "$flightTemplate/Native" $testOutput --game-module
    if ($LASTEXITCODE -ne 0) { throw 'Template MetaGen failed.' }
    $targets = @('FlightTrainingRegression')
    if ($Render) { $targets += 'FlightTrainingRenderSmoke' }
    foreach ($target in $targets) {
        $compilerArguments = @(
            '/nologo', '/std:c++20', '/EHsc', '/MDd', '/utf-8',
            '/IOrbedenCore/Src', '/IOrbedenCore/Src/ThirdParty/glad/include',
            '/IOrbedenCore/Src/ThirdParty/glfw/include', "/I$flightTemplate/Native",
            "Tests/$target.cpp", "$flightTemplate/Native/FlightController.cpp",
            "$flightTemplate/Native/SampleNativeBehaviour.cpp", "$flightTemplate/Native/FlightTerrainStreamer.cpp", "$flightTemplate/Native/FlightOrbitCamera.cpp", "$testOutput/Reflection.Generated.cpp",
            "/Fo$testOutput/", "/Fd$testOutput/compiler.pdb", "/Fe$testOutput/$target.exe",
            '/link', "$nativeSdk/OrbedenCore.lib", "$glfwDirectory/glfw3dll.lib"
        )
        & cl @compilerArguments
        if ($LASTEXITCODE -ne 0) { throw "$target compilation failed." }
    }
    $env:PATH = "$nativeSdk;$glfwDirectory;$previousPath"
    foreach ($frequency in 30, 50, 60, 120) {
        Write-Output "Testing $frequency Hz"
        & "$testOutput/FlightTrainingRegression.exe" $flightTemplate $frequency
        if ($LASTEXITCODE -ne 0) { throw "Flight regression failed at $frequency Hz." }
    }
    if ($Render) {
        & "$testOutput/FlightTrainingRenderSmoke.exe" $flightTemplate "$testOutput/terrain.ppm"
        if ($LASTEXITCODE -ne 0) { throw 'OpenGL terrain rendering failed.' }
    }
}
finally {
    $env:PATH = $previousPath
    Pop-Location
}
