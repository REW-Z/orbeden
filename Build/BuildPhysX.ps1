param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$PhysXSource = "",

    [switch]$SkipPackage
)

$ErrorActionPreference = "Stop"
$env:VSLANG = "1033"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($PhysXSource)) {
    $PhysXSource = Join-Path $repoRoot "..\PhysX-110.1-omni-and-physx-5.9.0"
} elseif (![System.IO.Path]::IsPathRooted($PhysXSource)) {
    $PhysXSource = Join-Path $repoRoot $PhysXSource
}

$inputSource = (Resolve-Path -LiteralPath $PhysXSource).Path
$physXSource = if (Test-Path -LiteralPath (Join-Path $inputSource "physx\CMakeLists.txt")) {
    Join-Path $inputSource "physx"
} elseif (Test-Path -LiteralPath (Join-Path $inputSource "CMakeLists.txt")) {
    $inputSource
} else {
    throw "PhysX source was not found below: $PhysXSource"
}

# PhysX configuration writes generated headers into its source tree, so build a private cache copy.
$cacheRoot = Join-Path $repoRoot "Build\.cache\PhysX-5.9.0"
$cachedSource = Join-Path $cacheRoot "physx"
New-Item -ItemType Directory -Force -Path $cachedSource | Out-Null
Copy-Item -Path (Join-Path $physXSource "*") -Destination $cachedSource -Recurse -Force

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe was not found. Install Visual Studio C++ build tools."
}

$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw "A Visual Studio installation with the x64 C++ toolchain was not found."
}

$devShell = Join-Path $vsRoot "Common7\Tools\Launch-VsDevShell.ps1"
$cmake = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (-not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath $ninja)) {
    throw "Visual Studio CMake/Ninja components are required."
}

& $devShell -Arch amd64 -HostArch amd64
$configName = $Configuration.ToLowerInvariant()
$buildDir = Join-Path $cacheRoot "build\windows-x64-msvc-$configName"
$stageDir = Join-Path $cacheRoot "stage\windows-x64-msvc\$Configuration"

# This is the supported PhysX CPU subset: no CUDA language, GPU projects, NVTX, or GPU archives.
& $cmake -S $cachedSource -B $buildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_BUILD_TYPE=$configName" `
    "-DCMAKE_INSTALL_PREFIX=$stageDir" `
    -DPX_GENERATE_STATIC_LIBRARIES=ON `
    -DPX_GENERATE_GPU_PROJECTS=OFF `
    -DPX_GENERATE_GPU_STATIC_LIBRARIES=OFF `
    -DPX_BUILDSNIPPETS=OFF `
    -DPX_BUILDPVDRUNTIME=OFF `
    -DPX_USE_NVTX=OFF `
    -DNV_USE_STATIC_WINCRT=OFF `
    "-DNV_USE_DEBUG_WINCRT=$($Configuration -eq 'Debug')"
if ($LASTEXITCODE -ne 0) { throw "PhysX configure failed." }

& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "PhysX build failed." }

& $cmake --install $buildDir
if ($LASTEXITCODE -ne 0) { throw "PhysX install failed." }

$artifactRoots = @((Join-Path $stageDir "lib"), (Join-Path $stageDir "bin")) | Where-Object { Test-Path -LiteralPath $_ }
$forbidden = $artifactRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File } | Where-Object {
    $_.Name -match "PhysXGpu|cuda|nvrtc" -or $_.Extension -in ".dll", ".so"
}
if ($forbidden) {
    throw "GPU/runtime artifacts were found in the CPU-only package: $($forbidden.FullName -join ', ')"
}

if (-not $SkipPackage) {
    $packageRoot = Join-Path $repoRoot "OrbedenCore\Src\ThirdParty\PhysX"
    $packageLib = Join-Path $packageRoot "lib\WindowsX64\$Configuration"
    New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot "include") | Out-Null
    New-Item -ItemType Directory -Force -Path $packageLib | Out-Null
    Get-ChildItem -LiteralPath $packageLib -Filter *.lib -File | Remove-Item -Force
    Copy-Item -Path (Join-Path $stageDir "include\*") -Destination (Join-Path $packageRoot "include") -Recurse -Force
    Copy-Item -Path (Join-Path $stageDir "lib\*.lib") -Destination $packageLib -Force
    $license = @(
        (Join-Path $inputSource "LICENSE.md"),
        (Join-Path (Split-Path -Parent $physXSource) "LICENSE.md")
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($license) { Copy-Item -LiteralPath $license -Destination (Join-Path $packageRoot "LICENSE.md") -Force }
}

Write-Host "PhysX CPU-only $Configuration completed: $stageDir"
