@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d Z:\dev\IME\MoqiIME\librime-gaboolic
call env.bat
cmake --build build --config Release --target rime > Z:\dev\IME\MoqiIME\librime-build-full.log 2>&1
exit /b %ERRORLEVEL%
