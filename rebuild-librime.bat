@echo off
call "%~dp0vcvars-detect.bat"
cd /d "%~dp0"
call env.bat
cmake --build build --config Release --target rime > %~dp0..\librime-build-full.log 2>&1
exit /b %ERRORLEVEL%
