@echo off
rem Locate vcvars64.bat on any machine/edition via vswhere (2026-11-07
rem portability pass: the build bats used to hardcode VS2022 Enterprise).
rem Usage: call "%~dp0vcvars-detect.bat" [arch]
rem Sets VCVARS64_BAT to the full path (empty + errorlevel 1 if absent),
rem then invokes it. arch: x64 (default) or x86 (Hostx64\x86 cross tools).
setlocal enabledelayedexpansion
set "ARCH=%~1"
if "%ARCH%"=="" set ARCH=x64
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [vcvars-detect] vswhere.exe not found - install Visual Studio 2019+ 1>&2
  endlocal & set VCVARS64_BAT= & exit /b 1
)
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
  echo [vcvars-detect] no VS with C++ tools found via vswhere 1>&2
  endlocal & set VCVARS64_BAT= & exit /b 1
)
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo [vcvars-detect] vcvars64.bat missing under %VS_PATH% 1>&2
  endlocal & set VCVARS64_BAT= & exit /b 1
)
endlocal & set "VCVARS64_BAT=%VCVARS%"
call "%VCVARS64_BAT%" >nul
if errorlevel 1 (
  echo [vcvars-detect] vcvars64 invocation failed 1>&2
  exit /b 1
)
exit /b 0
