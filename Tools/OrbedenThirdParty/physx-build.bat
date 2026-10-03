@echo off
rem Build PhysX static libraries from vendor/physx with ninja.
rem
rem Usage: physx-build.bat <debug|release>
rem
rem PhysX ships its own project generator which requires packman and a Visual
rem Studio generator preset; this machine only has VS18, so the preset in
rem physx-preset.xml selects the ninja generator with the VS toolchain instead.
rem Keep this file ASCII-only; cmd.exe reads batch files in the OEM code page.
setlocal
set "VSDIR=E:\Visual Studio 2019\IDE"
set "TPDIR=F:\Legacy\MyProjects\orbeden\Tools\OrbedenThirdParty"
set "PHYSXDIR=%TPDIR%\vendor\physx"
set "BUILDDIR=%PHYSXDIR%\compiler\orbeden-vc17win64-static"
set "PRESETNAME=orbeden-vc17win64-static"

set "CFG=%1"
if "%CFG%"=="" set "CFG=debug"

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

rem The generator only reads presets from inside the SDK tree; the canonical copy stays here.
copy /Y "%TPDIR%\physx-preset.xml" "%PHYSXDIR%\buildtools\presets\%PRESETNAME%.xml" >nul

if not exist "%BUILDDIR%\build.ninja" (
    call "%PHYSXDIR%\generate_projects.bat" %PRESETNAME% || exit /b 1
)

cmake --build "%BUILDDIR%" --config %CFG% --target PhysX PhysXCommon PhysXFoundation PhysXExtensions PhysXPvdSDK PhysXCooking PhysXCharacterKinematic PhysXVehicle
exit /b %ERRORLEVEL%
