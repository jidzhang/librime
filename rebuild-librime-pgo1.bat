@echo off
rem PGO stage 1: build instrumented rime.dll (/LTCG:PGINSTRUMENT).
rem After build, copy build\bin\Release\rime.dll (+ .pgd) to eval-workspace,
rem run the training workload there, then run rebuild-librime-pgo2.bat.
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
call env.bat
cmake . -B build -G "%CMAKE_GENERATOR%" -A %ARCH% -T %PLATFORM_TOOLSET%^
  -DCMAKE_CONFIGURATION_TYPES:STRING="Release"^
  -DCMAKE_BUILD_TYPE:STRING="Release"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE:PATH="%RIME_ROOT%\cmake\c_flag_overrides.cmake"^
  -DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX:PATH="%RIME_ROOT%\cmake\cxx_flag_overrides.cmake"^
  -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt"^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"^
  -DBUILD_STATIC=ON^
  -DBUILD_SHARED_LIBS=ON^
  -DBUILD_TEST=OFF^
  -DENABLE_LOGGING=ON^
  -DCMAKE_PREFIX_PATH:PATH="%RIME_ROOT%\deps\usr"^
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING="/LTCG:PGINSTRUMENT /DEBUG /OPT:REF /OPT:ICF" > %~dp0..\librime-reconfigure.log 2>&1
cmake --build build --config Release --target rime > %~dp0..\librime-build-pgo1.log 2>&1
exit /b %ERRORLEVEL%
