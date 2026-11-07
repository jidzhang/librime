@echo off
call "%~dp0vcvars-detect.bat"
cd /d "%~dp0"
call env.bat
cmake --build build --config Release --target build_grammar > %~dp0..\librime-tools-build.log 2>&1
exit /b %ERRORLEVEL%
