@echo off
rem ============================================================
rem One-click PGO build: instrument -> train -> optimize -> verify
rem ============================================================
rem Usage:  build-pgo.bat [--skip-train]
rem   --skip-train: reuse existing .pgc profiles (small code changes only)
rem
rem Prerequisites:
rem   1. Static deps built and installed in lib\ (rebuild-deps-gl.bat first time,
rem      or build.bat deps for non-GL. Current verdict: GL deps + PGO = best).
rem   2. eval-workspace\data exists (dictionaries for training).
rem   3. eval-sentences-v3.exe exists (training driver).
rem
rem Stages:
rem   [1/4] Clean + build instrumented dll  (~4 min)
rem   [2/4] Run training workload            (~5 min)
rem         -> generates rime!*.pgc profile files
rem   [3/4] Build optimized dll              (~4 min)
rem   [4/4] Verify output                    (instant)
rem
rem Total: ~13 minutes fresh, ~8 minutes with --skip-train
rem
rem IMPORTANT: after source code changes, always retrain (no --skip-train).
rem PGO profiles encode branch frequencies; stale profiles on changed code
rem can produce WORSE layouts than no PGO at all.
rem ASCII only. 2026-10-31.
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
call env.bat
set RR=%RIME_ROOT%
set EVAL=Z:\dev\IME\MoqiIME\eval-workspace
set TRAINDIR=%RR%\build\bin\Release
set PGORT=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\pgort140.dll
set SKIP_TRAIN=0
if "%1"=="--skip-train" set SKIP_TRAIN=1

echo ============================================================
echo  [1/4] Building instrumented rime.dll (PGINSTRUMENT)
echo ============================================================
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist build rmdir /s /q build
cmake . -B build -G "%CMAKE_GENERATOR%" -A %ARCH% -T %PLATFORM_TOOLSET%^
  -DCMAKE_CONFIGURATION_TYPES:STRING="Release"^
  -DCMAKE_BUILD_TYPE:STRING="Release"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE:PATH="%RR%\cmake\c_flag_overrides.cmake"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX:PATH="%RR%\cmake\cxx_flag_overrides.cmake"^
  -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt"^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"^
  -DBUILD_STATIC=ON^
  -DBUILD_SHARED_LIBS=ON^
  -DBUILD_TEST=OFF^
  -DENABLE_LOGGING=ON^
  -DCMAKE_PREFIX_PATH:PATH="%RR%\deps\usr"^
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING="/LTCG:PGINSTRUMENT /DEBUG /OPT:REF /OPT:ICF" >nul 2>&1
if errorlevel 1 (echo FAIL: cmake configure & exit /b 1)
cmake --build build --config Release --target rime >nul 2>&1
if errorlevel 1 (echo FAIL: instrumented build & exit /b 1)
echo OK: instrumented dll = !TRAINDIR!\rime.dll

if %SKIP_TRAIN%==1 (
  echo ============================================================
  echo  [2/4] SKIPPING training (--skip-train, using existing profiles^)
  echo ============================================================
  if not exist "%RR%\pgo-profile\rime.pgd" (
    echo FAIL: no existing profiles in pgo-profile\, cannot skip training
    exit /b 1
  )
  rem restore pgd+pgc to build dir for the optimize link
  copy /y "%RR%\pgo-profile\rime.pgd" "%TRAINDIR%\" >nul
  copy /y "%RR%\pgo-profile\rime!*.pgc" "%TRAINDIR%\" >nul
) else (
  echo ============================================================
  echo  [2/4] Training workload (bench 4 cells + perkey 2 rounds^)
  echo ============================================================
  rem Deploy instrumented dll + pgort to eval workspace
  copy /y "%TRAINDIR%\rime.dll" "%EVAL%\rime.dll" >nul
  copy /y "%PGORT%" "%EVAL%\" >nul
  rem Clean old profiles
  if exist "%EVAL%\rime!*.pgc" del /q "%EVAL%\rime!*.pgc"
  rem Training cell 1-4: bench-ab (gram on/off x zhihu120/words234^)
  for %%A in (on off) do (
    for %%C in (zhihu120:5 words234:2) do (
      set NAME=%%C
      set NAME=!NAME:~0,-2!
      set U=%EVAL%\users\pgotrain-%%A-!NAME!
      if exist "!U!" rmdir /s /q "!U!"
      if exist "!U!-ime" rmdir /s /q "!U!-ime"
      mkdir "!U!-ime\rime" "!U!-ime\Moqi"
      if %%A==on (
        printf 'patch:\n  grammar/language: zh-hans-t-essay-bgw\n  "translator/contextual_suggestions": true\n' > "!U!-ime\rime\rime_frost.custom.yaml"
      ) else (
        printf 'patch:\n  "translator/contextual_suggestions": true\n' > "!U!-ime\rime\rime_frost.custom.yaml"
      )
      echo   training: %%A/!NAME! ...
      "%EVAL%\eval-sentences-v3.exe"^
        --corpus "Z:\dev\IME\windows-ime-bench\corpus\!NAME!.txt"^
        --minLen 2^
        --stack ime^
        --pinyin "Z:\dev\IME\google-input-tools\reference\pinyin-data\pinyin.txt"^
        --pinyinCorpus "%EVAL%\bench-!NAME!-pinyin.tsv"^
        --data "%EVAL%\data"^
        --user "!U!" >nul 2>&1
    )
  )
  rem Training round: perkey (burst pairs for short-input hot path^)
  echo   training: perkey ...
  set PU=%EVAL%\users\pgotrain-perkey
  if exist "!PU!" rmdir /s /q "!PU!"
  if exist "!PU!-ime" rmdir /s /q "!PU!-ime"
  pushd "%EVAL%"
  if exist probe-perkey-lat.exe (
    probe-perkey-lat.exe "!PU!" 500 >nul 2>&1
  ) else (
    echo   WARN: probe-perkey-lat.exe not found, skipping perkey training
  )
  popd
  rem Collect profiles
  echo   collecting .pgc profiles ...
  if not exist "%EVAL%\rime!1.pgc" (
    echo FAIL: no .pgc files generated. Check eval-sentences-v3.exe ran OK.
    exit /b 1
  )
  copy /y "%EVAL%\rime!*.pgc" "%TRAINDIR%\" >nul
  rem Archive for --skip-train next time
  if not exist "%RR%\pgo-profile" mkdir "%RR%\pgo-profile"
  copy /y "%TRAINDIR%\rime.pgd" "%RR%\pgo-profile\" >nul
  copy /y "%TRAINDIR%\rime!*.pgc" "%RR%\pgo-profile\" >nul
  echo OK: profiles archived to pgo-profile\
)

echo ============================================================
echo  [3/4] Building optimized rime.dll (PGOPTIMIZE^)
echo ============================================================
cmake . -B build -G "%CMAKE_GENERATOR%" -A %ARCH% -T %PLATFORM_TOOLSET%^
  -DCMAKE_CONFIGURATION_TYPES:STRING="Release"^
  -DCMAKE_BUILD_TYPE:STRING="Release"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE:PATH="%RR%\cmake\c_flag_overrides.cmake"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX:PATH="%RR%\cmake\cxx_flag_overrides.cmake"^
  -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt"^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"^
  -DBUILD_STATIC=ON^
  -DBUILD_SHARED_LIBS=ON^
  -DBUILD_TEST=OFF^
  -DENABLE_LOGGING=ON^
  -DCMAKE_PREFIX_PATH:PATH="%RR%\deps\usr"^
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING="/LTCG:PGOPTIMIZE /USEPROFILE /DEBUG /OPT:REF /OPT:ICF" >nul 2>&1
if errorlevel 1 (echo FAIL: cmake reconfigure for PGOPTIMIZE & exit /b 1)
cmake --build build --config Release --target rime >nul 2>&1
if errorlevel 1 (echo FAIL: PGO optimized build. Check .pgd/.pgc in %TRAINDIR% & exit /b 1)

echo ============================================================
echo  [4/4] Verify
echo ============================================================
for %%F in ("%TRAINDIR%\rime.dll") do set SIZE=%%~zF
echo   size: !SIZE! bytes
if !SIZE! GTR 3600000 (
  echo   WARN: size ^> 3.6MB - PGO may not have applied. Expected ~3.2MB.
  echo   Check build log for "PGOPTIMIZE" mention.
) else (
  echo   OK: size consistent with PGO optimization (~3.2MB^)
)
echo.
echo   Product sync commands:
echo     copy /y "%TRAINDIR%\rime.dll" "Z:\dev\IME\MoqiIME\moqi-ime\input_methods\rime\rime.dll"
echo     copy /y "%TRAINDIR%\rime.dll" "Z:\dev\IME\MoqiIME\eval-workspace\rime.dll"
echo.
echo PGO-BUILD-OK
endlocal
