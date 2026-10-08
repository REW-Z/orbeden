@echo off
rem Build PhysX static libraries from vendor/physx with ninja.
rem
rem Usage: physx-build.bat <debug|release> ["VS install dir"]
rem
rem PhysX ships its own project generator which requires packman and a Visual
rem Studio generator preset; this machine only has VS18, so the preset in
rem physx-preset.xml selects the ninja generator with the VS toolchain instead.
rem Keep this file ASCII-only; cmd.exe reads batch files in the OEM code page.
setlocal
rem Never hardcode a VS path here: edition and install location differ per machine.
rem The project passes $(VSInstallDir) as the second argument; pass the same value by hand.
rem No lookup is done here on purpose: vswhere lives under a path with spaces and parentheses,
rem which cmd mangles inside a for /f statement.
set "VSDIR=%~2"
if not defined VSDIR (
    echo Usage: physx-build.bat ^<debug^|release^> "VS install dir"
    echo Ask vswhere for it: vswhere -latest -products * -property installationPath
    exit /b 1
)
rem This script sits in the generator root; derive its own directory so the repo can move.
set "TPDIR=%~dp0"
set "PHYSXDIR=%TPDIR%\vendor\physx\physx"
set "BUILDDIR=%PHYSXDIR%\compiler\orbeden-vc17win64-static"
set "PRESETNAME=orbeden-vc17win64-static"
set "PM_PACKAGES_ROOT=%TPDIR%.cache\packman"

set "CFG=%1"
if not "%CFG%"=="debug" if not "%CFG%"=="release" exit /b 1

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

rem The generator only reads presets from inside the SDK tree; the canonical copy stays here.
copy /Y "%TPDIR%\physx-preset.xml" "%PHYSXDIR%\buildtools\presets\%PRESETNAME%.xml" >nul || exit /b 1

call "%PHYSXDIR%\generate_projects.bat" %PRESETNAME% || exit /b 1

cmake --build "%BUILDDIR%" --config %CFG% --target PhysX PhysXCommon PhysXFoundation PhysXExtensions PhysXPvdSDK PhysXCooking PhysXCharacterKinematic PhysXVehicle
exit /b %ERRORLEVEL%
