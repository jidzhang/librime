@echo off
rem ============================================================
rem One-click PGO build for BOTH architectures (2026-11-06).
rem   x64 rime.dll -> moqi-ime\input_methods\rime (x64 TSF in-proc)
rem   x86 rime.dll -> build-x86 (loaded by MoqiDaemon, production)
rem
rem Usage (double-click or):
rem   build-pgo.bat                full: instrument -> train -> optimize
rem   build-pgo.bat --skip-train   reuse pgo-profile\, no training
rem
rem Prerequisites (all inside this project, no Z: share needed):
rem   eval-workspace\data, eval-sentences-v3.exe, bench-*.tsv
rem   ..\windows-ime-bench\corpus\zhihu120.txt, words234.txt
rem   ..\google-input-tools\reference\pinyin-data\pinyin.txt
rem   deps\usr (x64 deps) and x86\ (x86 deps)
rem
rem Stages: [1] x64 instrument [2] x64 train [3] x64 optimize
rem         [4] x86 instrument [5] x86 train [6] x86 optimize
rem         [7] verify + sync product copies
rem Fresh run ~40 min, --skip-train ~10 min. ASCII only.
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0."
rem Same values as env.bat, inlined so the script does not depend on cwd.
set RIME_ROOT=%~dp0
set BOOST_ROOT=%~dp0deps\boost-1.92.0
set ARCH=x64
set CMAKE_GENERATOR=Visual Studio 17 2022
set PLATFORM_TOOLSET=v143

set RR=%~dp0
set PROJ=%RR%..
set EVAL=%PROJ%\eval-workspace
set CORPUS=%PROJ%\..\windows-ime-bench\corpus
set PINYIN=%PROJ%\..\google-input-tools\reference\pinyin-data\pinyin.txt
set PROFILE=%RR%pgo-profile
set BACKUP=%PROJ%\pgo-backup
set LOG=%PROJ%\pgo-build.log
set LOG64=%PROJ%\pgo-build-x64.log
set LOG86=%PROJ%\pgo-build-x86.log
set X86TRAIN=%EVAL%\pgo-x86
set SKIP_TRAIN=0
set FROM_X86=0
if /i "%~1"=="--skip-train" set SKIP_TRAIN=1
rem --from-x86: skip the x64 stages (optimized dll + profiles already in
rem place) and rerun from [4/7] x86 instrument. Use to retrain the x86
rem side alone (e.g. after fixing the training host) without redoing x64.
if /i "%~1"=="--from-x86" set FROM_X86=1

echo. > "%LOG%" > "%LOG64%" > "%LOG86%"
echo ============================================================
echo  Moqi rime.dll PGO build (x64 + x86)
echo  Log: %LOG%
echo ============================================================

echo.
echo [preflight] checking inputs ...
if not exist "%RR%lib\glog.lib" (set FAILMSG=x64 deps missing: %RR%lib\glog.lib & goto :fail)
if not exist "%RR%include\glog" (set FAILMSG=x64 deps headers missing: %RR%include\glog & goto :fail)
if not exist "%RR%x86" (set FAILMSG=x86 deps dir missing: %RR%x86 & goto :fail)
if not exist "%EVAL%\data" (set FAILMSG=eval data dir missing: %EVAL%\data & goto :fail)
if not exist "%EVAL%\eval-sentences-v3.exe" (set FAILMSG=training driver missing: %EVAL%\eval-sentences-v3.exe & goto :fail)
if not exist "%EVAL%\bench-zhihu120-pinyin.tsv" (set FAILMSG=missing %EVAL%\bench-zhihu120-pinyin.tsv & goto :fail)
if not exist "%EVAL%\bench-words234-pinyin.tsv" (set FAILMSG=missing %EVAL%\bench-words234-pinyin.tsv & goto :fail)
if not exist "%CORPUS%\zhihu120.txt" (set FAILMSG=corpus missing: %CORPUS%\zhihu120.txt & goto :fail)
if not exist "%CORPUS%\words234.txt" (set FAILMSG=corpus missing: %CORPUS%\words234.txt & goto :fail)
if not exist "%PINYIN%" (set FAILMSG=pinyin table missing: %PINYIN% & goto :fail)
if %SKIP_TRAIN%==1 (
  if not exist "%PROFILE%\x64\rime.pgd" (
    if exist "%PROFILE%\rime.pgd" (
      if not exist "%PROFILE%\x64" mkdir "%PROFILE%\x64"
      copy /y "%PROFILE%\rime.pgd" "%PROFILE%\x64\" >nul
      copy /y "%PROFILE%\rime*.pgc" "%PROFILE%\x64\" >nul
    ) else (
      set FAILMSG=no profiles in %PROFILE%\x64 and none to migrate & goto :fail
    )
  )
  if not exist "%PROFILE%\x86\rime.pgd" (set FAILMSG=no x86 profiles in %PROFILE%\x86; run without --skip-train first & goto :fail)
)

call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (set FAILMSG=vcvars64 failed & goto :fail)
set PGORT64=%VCToolsInstallDir%bin\Hostx64\x64\pgort140.dll
set PGORT32=%VCToolsInstallDir%bin\Hostx86\x86\pgort140.dll
if not exist "%PGORT64%" (set FAILMSG=pgort140.dll x64 not found in %VCToolsInstallDir% & goto :fail)
if not exist "%PGORT32%" (set FAILMSG=pgort140.dll x86 not found in %VCToolsInstallDir% & goto :fail)

rem Back up the current product dlls before any destructive step.
if not exist "%BACKUP%" mkdir "%BACKUP%"
if exist "%RR%build\bin\Release\rime.dll" copy /y "%RR%build\bin\Release\rime.dll" "%BACKUP%\rime-x64-prev.dll" >nul
if exist "%RR%build-x86\bin\Release\rime.dll" copy /y "%RR%build-x86\bin\Release\rime.dll" "%BACKUP%\rime-x86-prev.dll" >nul
echo [preflight] OK

if %SKIP_TRAIN%==1 goto :optimize_x64
if %FROM_X86%==1 goto :x86_instrument

rem ------------------------------------------------------------
echo.
echo ============================================================
echo  [1/7] x64 instrumented build (PGINSTRUMENT)
echo ============================================================
rem ------------------------------------------------------------
if exist "%RR%build" rmdir /s /q "%RR%build"
call :configure_x64 PGINSTRUMENT
if errorlevel 1 (set FAILMSG=cmake configure x64 failed & goto :fail)
call :build_x64
if errorlevel 1 (set FAILMSG=x64 instrumented build failed & goto :fail)
echo OK: %RR%build\bin\Release\rime.dll

rem ------------------------------------------------------------
echo.
echo ============================================================
echo  [2/7] x64 training workload (4 cells)
echo ============================================================
rem ------------------------------------------------------------
copy /y "%RR%build\bin\Release\rime.dll" "%EVAL%\rime.dll" >nul
copy /y "%PGORT64%" "%EVAL%\pgort140.dll" >nul
del /q "%EVAL%\rime*.pgc" >nul 2>&1
set HOSTEXE=%EVAL%\eval-sentences-v3.exe
set DATADIR=%EVAL%\data
set ARCHTAG=x64
call :train_cell on zhihu120
call :train_cell off zhihu120
call :train_cell on words234
call :train_cell off words234
if not exist "%EVAL%\probe-perkey.exe" (
  echo   building x64 perkey probe ...
  pushd "%PROJ%\moqi-ime"
  set GOOS=windows
  set GOARCH=amd64
  set CGO_ENABLED=0
  go build -o "%EVAL%\probe-perkey.exe" ./cmd/probe-perkey >> "%LOG%" 2>&1
  set GOBUILDRC=!errorlevel!
  set GOOS=
  set GOARCH=
  set CGO_ENABLED=
  popd
  if not "!GOBUILDRC!"=="0" echo   WARN: perkey probe build failed, skipping perkey training
)
if exist "%EVAL%\probe-perkey.exe" (
  echo   training: perkey ...
  "%EVAL%\probe-perkey.exe" "%EVAL%\users\pgotrain-%ARCHTAG%-perkey" "%DATADIR%" 120 >> "%LOG%" 2>&1
)
if not exist "%EVAL%\rime*.pgc" (set FAILMSG=no x64 .pgc generated; see %LOG% & goto :fail)
copy /y "%EVAL%\rime*.pgc" "%RR%build\bin\Release\" >nul
if not exist "%PROFILE%\x64" mkdir "%PROFILE%\x64"
copy /y "%RR%build\bin\Release\rime.pgd" "%PROFILE%\x64\" >nul
copy /y "%RR%build\bin\Release\rime*.pgc" "%PROFILE%\x64\" >nul
echo OK: x64 profiles archived to %PROFILE%\x64

rem ------------------------------------------------------------
echo.
echo ============================================================
echo  [3/7] x64 optimized build (PGOPTIMIZE)
echo ============================================================
rem ------------------------------------------------------------
call :configure_x64 PGOPTIMIZE
if errorlevel 1 (set FAILMSG=cmake configure x64 optimize failed & goto :fail)
call :build_x64
if errorlevel 1 (set FAILMSG=x64 PGO optimized build failed & goto :fail)
call :check_pgo "%CLOG%"
if errorlevel 1 (set FAILMSG=x64 link did not consume .pgc profile data & goto :fail)
echo OK: x64 optimized rime.dll

:optimize_x64
if %SKIP_TRAIN%==1 (
  echo.
  echo ============================================================
  echo  [1/7] x64 optimized build from archived profiles
  echo ============================================================
  call :configure_x64 PGOPTIMIZE
  if errorlevel 1 (set FAILMSG=cmake configure x64 failed & goto :fail)
  if not exist "%RR%build\bin\Release" mkdir "%RR%build\bin\Release"
  copy /y "%PROFILE%\x64\rime.pgd" "%RR%build\bin\Release\" >nul
  copy /y "%PROFILE%\x64\rime*.pgc" "%RR%build\bin\Release\" >nul
  call :build_x64
  if errorlevel 1 (set FAILMSG=x64 PGO build skip-train failed & goto :fail)
  call :check_pgo "%CLOG%"
  if errorlevel 1 (set FAILMSG=x64 link did not consume .pgc profile data & goto :fail)
  echo OK: x64 optimized rime.dll
)

if %SKIP_TRAIN%==1 goto :optimize_x86

rem ------------------------------------------------------------
:x86_instrument
echo.
echo ============================================================
echo  [4/7] x86 instrumented build (PGINSTRUMENT)
echo ============================================================
rem ------------------------------------------------------------
if exist "%RR%build-x86" rmdir /s /q "%RR%build-x86"
call :configure_x86 PGINSTRUMENT
if errorlevel 1 (set FAILMSG=cmake configure x86 failed & goto :fail)
call :build_x86
if errorlevel 1 (set FAILMSG=x86 instrumented build failed & goto :fail)
echo OK: %RR%build-x86\bin\Release\rime.dll

rem ------------------------------------------------------------
echo.
echo ============================================================
echo  [5/7] x86 training workload (4 cells)
echo ============================================================
rem ------------------------------------------------------------
rem Prepare the training dir BEFORE building the host: the host exe must
rem live next to the 32-bit pgort140.dll in %X86TRAIN%. LoadLibrary does
rem not search the DLL's own directory for dependencies, only the exe
rem directory - a host placed in the eval root picks up the 64-bit
rem pgort140.dll left there by the x64 stage and fails with
rem "%1 is not a valid Win32 application" (2026-11-06: all 4 cells died
rem this way; only perkey survived because it shared the dir).
if not exist "%X86TRAIN%" mkdir "%X86TRAIN%"
if exist "%X86TRAIN%\data" rmdir "%X86TRAIN%\data"
mklink /J "%X86TRAIN%\data" "%EVAL%\data" >nul
if errorlevel 1 (set FAILMSG=failed to link %X86TRAIN%\data & goto :fail)
copy /y "%RR%build-x86\bin\Release\rime.dll" "%X86TRAIN%\rime.dll" >nul
copy /y "%PGORT32%" "%X86TRAIN%\pgort140.dll" >nul
del /q "%X86TRAIN%\rime*.pgc" >nul 2>&1
rem Remove the stale host from the old location (built to eval root by
rem older script versions; it loads the wrong-arch pgort and always dies).
if exist "%EVAL%\eval-sentences-x86.exe" del /q "%EVAL%\eval-sentences-x86.exe" >nul 2>&1
if not exist "%X86TRAIN%\eval-sentences-x86.exe" (
  echo   building x86 training host GOARCH=386 ...
  pushd "%PROJ%\moqi-ime"
  set GOOS=windows
  set GOARCH=386
  set CGO_ENABLED=0
  go build -o "%X86TRAIN%\eval-sentences-x86.exe" ./eval/cmd/eval-sentences >> "%LOG%" 2>&1
  set GOBUILDRC=!errorlevel!
  set GOOS=
  set GOARCH=
  set CGO_ENABLED=
  popd
  if not "!GOBUILDRC!"=="0" (set FAILMSG=x86 training host build failed; check Go toolchain & goto :fail)
)
set HOSTEXE=%X86TRAIN%\eval-sentences-x86.exe
set DATADIR=%X86TRAIN%\data
set ARCHTAG=x86
call :train_cell on zhihu120
call :train_cell off zhihu120
call :train_cell on words234
call :train_cell off words234
if not exist "%X86TRAIN%\probe-perkey-x86.exe" (
  echo   building x86 perkey probe ...
  pushd "%PROJ%\moqi-ime"
  set GOOS=windows
  set GOARCH=386
  set CGO_ENABLED=0
  go build -o "%X86TRAIN%\probe-perkey-x86.exe" ./cmd/probe-perkey >> "%LOG%" 2>&1
  set GOBUILDRC=!errorlevel!
  set GOOS=
  set GOARCH=
  set CGO_ENABLED=
  popd
  if not "!GOBUILDRC!"=="0" echo   WARN: x86 perkey probe build failed, skipping
)
if exist "%X86TRAIN%\probe-perkey-x86.exe" (
  echo   training: x86 perkey ...
  "%X86TRAIN%\probe-perkey-x86.exe" "%EVAL%\users\pgotrain-%ARCHTAG%-perkey" "%DATADIR%" 120 >> "%LOG%" 2>&1
)
if not exist "%X86TRAIN%\rime*.pgc" (set FAILMSG=no x86 .pgc generated; x86 host did not exercise the dll & goto :fail)
copy /y "%X86TRAIN%\rime*.pgc" "%RR%build-x86\bin\Release\" >nul
if not exist "%PROFILE%\x86" mkdir "%PROFILE%\x86"
copy /y "%RR%build-x86\bin\Release\rime.pgd" "%PROFILE%\x86\" >nul
copy /y "%RR%build-x86\bin\Release\rime*.pgc" "%PROFILE%\x86\" >nul
echo OK: x86 profiles archived to %PROFILE%\x86

:optimize_x86
if %SKIP_TRAIN%==1 (
  echo.
  echo ============================================================
  echo  [4/7] x86 optimized build from archived profiles
  echo ============================================================
  call :configure_x86 PGOPTIMIZE
  if errorlevel 1 (set FAILMSG=cmake configure x86 failed & goto :fail)
  if not exist "%RR%build-x86\bin\Release" mkdir "%RR%build-x86\bin\Release"
  copy /y "%PROFILE%\x86\rime.pgd" "%RR%build-x86\bin\Release\" >nul
  copy /y "%PROFILE%\x86\rime*.pgc" "%RR%build-x86\bin\Release\" >nul
  call :build_x86
  if errorlevel 1 (set FAILMSG=x86 PGO build skip-train failed & goto :fail)
  call :check_pgo "%CLOG%"
  if errorlevel 1 (set FAILMSG=x86 link did not consume .pgc profile data & goto :fail)
  echo OK: x86 optimized rime.dll
  goto :verify
)

rem ------------------------------------------------------------
echo.
echo ============================================================
echo  [6/7] x86 optimized build (PGOPTIMIZE)
echo ============================================================
rem ------------------------------------------------------------
call :configure_x86 PGOPTIMIZE
if errorlevel 1 (set FAILMSG=cmake configure x86 optimize failed & goto :fail)
call :build_x86
if errorlevel 1 (set FAILMSG=x86 PGO optimized build failed & goto :fail)
call :check_pgo "%CLOG%"
if errorlevel 1 (set FAILMSG=x86 link did not consume .pgc profile data & goto :fail)
echo OK: x86 optimized rime.dll

rem ------------------------------------------------------------
:verify
echo.
echo ============================================================
echo  [7/7] Verify and sync
echo ============================================================
rem ------------------------------------------------------------
set X64DLL=%RR%build\bin\Release\rime.dll
set X86DLL=%RR%build-x86\bin\Release\rime.dll
if not exist "%X64DLL%" (set FAILMSG=x64 rime.dll not produced & goto :fail)
if not exist "%X86DLL%" (set FAILMSG=x86 rime.dll not produced & goto :fail)
for %%F in ("%X64DLL%") do set X64SIZE=%%~zF
for %%F in ("%X86DLL%") do set X86SIZE=%%~zF
echo   x64: !X64SIZE! bytes
echo   x86: !X86SIZE! bytes
if !X64SIZE! LSS 2500000 (set FAILMSG=x64 dll suspiciously small: !X64SIZE! & goto :fail)
if !X86SIZE! LSS 2000000 (set FAILMSG=x86 dll suspiciously small: !X86SIZE! & goto :fail)

rem Sync product copies: x64 feeds the TSF runtime tree and the eval tools;
rem x86 stays in build-x86 (install.ps1 copies it next to MoqiDaemon).
copy /y "%X64DLL%" "%PROJ%\moqi-ime\input_methods\rime\rime.dll" >nul
copy /y "%X64DLL%" "%EVAL%\rime.dll" >nul
rem NOTE: no "->" in echo text - cmd parses it as redirection and tries to
rem create a file named moqi-ime\... in the CWD (2026-11-06 path-not-found).
echo   synced x64 to moqi-ime\input_methods\rime\rime.dll
echo   synced x64 to eval-workspace\rime.dll

echo.
echo   sha256 x64:
certutil -hashfile "%X64DLL%" SHA256 | findstr /v ":"
echo   sha256 x86:
certutil -hashfile "%X86DLL%" SHA256 | findstr /v ":"

echo.
echo ============================================================
echo  PGO-BUILD-OK
echo ============================================================
echo.
if not defined MOQI_PGO_NOPAUSE pause
exit /b 0

rem ------------------------------------------------------------
:configure_x64
set CLOG=%LOG64%
rem %1 = PGINSTRUMENT or PGOPTIMIZE
cmake . -B build -G "%CMAKE_GENERATOR%" -A x64 -T %PLATFORM_TOOLSET%^
  -DCMAKE_CONFIGURATION_TYPES:STRING="Release"^
  -DCMAKE_BUILD_TYPE:STRING="Release"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE:PATH="%RR%cmake\c_flag_overrides.cmake"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX:PATH="%RR%cmake\cxx_flag_overrides.cmake"^
  -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt"^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"^
  -DBUILD_STATIC=ON -DBUILD_SHARED_LIBS=ON -DBUILD_TEST=OFF -DENABLE_LOGGING=ON^
  -DCMAKE_PREFIX_PATH:PATH="%RR%."^
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING="/LTCG:%1 /DEBUG /OPT:REF /OPT:ICF" >> "%CLOG%" 2>&1
exit /b %ERRORLEVEL%

:configure_x86
set CLOG=%LOG86%
cmake . -B build-x86 -G "%CMAKE_GENERATOR%" -A Win32 -T %PLATFORM_TOOLSET%^
  -DCMAKE_CONFIGURATION_TYPES:STRING="Release"^
  -DCMAKE_BUILD_TYPE:STRING="Release"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE:PATH="%RR%cmake\c_flag_overrides.cmake"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX:PATH="%RR%cmake\cxx_flag_overrides.cmake"^
  -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt"^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"^
  -DBUILD_STATIC=ON -DBUILD_SHARED_LIBS=ON -DBUILD_TEST=OFF -DENABLE_LOGGING=ON^
  -DCMAKE_PREFIX_PATH:PATH="%RR%x86"^
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING="/LTCG:%1 /DEBUG /OPT:REF /OPT:ICF" >> "%CLOG%" 2>&1
exit /b %ERRORLEVEL%

:build_x64
set CLOG=%LOG64%
echo   building (~5 min) ...
cmake --build build --config Release --target rime >> "%CLOG%" 2>&1
exit /b %ERRORLEVEL%

:build_x86
set CLOG=%LOG86%
echo   building (~5 min) ...
cmake --build build-x86 --config Release --target rime >> "%CLOG%" 2>&1
exit /b %ERRORLEVEL%

:check_pgo
rem %1 = log file. The linker prints one ".pgc" merge line per profile file.
findstr /c:".pgc" "%~1" >nul
exit /b %ERRORLEVEL%

:train_cell
rem %1 = arm (on|off)  %2 = corpus name
set TARM=%1
set TNAME=%2
set TU=%EVAL%\users\pgotrain-%ARCHTAG%-%TARM%-%TNAME%
if exist "%TU%" rmdir /s /q "%TU%"
if exist "%TU%-ime" rmdir /s /q "%TU%-ime"
mkdir "%TU%-ime\rime" 2>nul
mkdir "%TU%-ime\Moqi" 2>nul
(
  echo patch:
  if /i "%TARM%"=="on" echo   grammar/language: zh-hans-t-essay-bgw
  echo   "translator/contextual_suggestions": true
) > "%TU%-ime\rime\rime_frost.custom.yaml"
echo   training: %ARCHTAG%/%TARM%/%TNAME% ...
"%HOSTEXE%" --corpus "%CORPUS%\%TNAME%.txt" --minLen 2 --stack ime --pinyin "%PINYIN%" --pinyinCorpus "%EVAL%\bench-%TNAME%-pinyin.tsv" --data "%DATADIR%" --user "%TU%" >> "%LOG%" 2>&1
if errorlevel 1 echo   WARN: training cell %ARCHTAG%/%TARM%/%TNAME% exited nonzero; continuing (see %LOG%)
exit /b 0

:fail
echo.
echo ============================================================
echo  PGO-BUILD-FAILED: %FAILMSG%
echo ============================================================
echo   last log lines (%LOG%):
powershell -NoProfile -Command "Get-Content -LiteralPath '%LOG%' -Tail 25"
echo.
if not defined MOQI_PGO_NOPAUSE pause
exit /b 1