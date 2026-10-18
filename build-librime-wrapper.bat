@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d Z:\dev\IME\MoqiIME\librime-gaboolic
call build.bat librime
