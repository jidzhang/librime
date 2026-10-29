@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
call env.bat
cmake --build build --config Release --target rime > %~dp0..\librime-build-full.log 2>&1
exit /b %ERRORLEVEL%
