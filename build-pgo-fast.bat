@echo off
rem Quick PGO relink for both arches: reuse archived profiles in pgo-profile\.
rem Use after small code changes only. After larger changes run build-pgo.bat
rem (full retrain) instead -- stale profiles on changed code can be worse
rem than no PGO.
rem
rem Prerequisites: run build-pgo.bat at least once so pgo-profile\x64 and
rem pgo-profile\x86 exist. ASCII only.
cd /d "%~dp0"
call "%~dp0build-pgo.bat" --skip-train
exit /b %ERRORLEVEL%