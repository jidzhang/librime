@echo off
call "%~dp0vcvars-detect.bat"
cd /d "%~dp0"
call build.bat deps
