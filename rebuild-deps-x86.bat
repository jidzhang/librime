@echo off
rem Rebuild x86 (Win32) static deps into the separate librime\x86 prefix, so the
rem x64 artifacts in librime\lib + librime\include stay untouched. ASCII only.
rem
rem 2026-11-15: glog was removed upstream (replaced by the spdlog facade), so the
rem glog step is gone. spdlog is header-only and consumed straight from the
rem deps\spdlog submodule by CMake; it needs no separate build step here.
rem
rem The x64 counterpart is rebuild-deps-x64.bat.
call "%~dp0vcvars-detect.bat"
cd /d "%~dp0"
call env.bat
set ARCH=Win32
set RR=%RIME_ROOT:~0,-1%\x86
set CFG=Release
set GEN=-G "%CMAKE_GENERATOR%" -A%ARCH% -T%PLATFORM_TOOLSET%
set COMMON=%GEN% -DCMAKE_CONFIGURATION_TYPES:STRING="%CFG%" -DCMAKE_BUILD_TYPE:STRING="%CFG%" ^
 -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt" ^
 -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
 -DCMAKE_C_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG" ^
 -DCMAKE_CXX_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG /utf-8" ^
 -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_INSTALL_PREFIX:PATH="%RR%"

echo building leveldb x86.
pushd deps\leveldb
if exist build-x86 rmdir /s /q build-x86
cmake . -Bbuild-x86 %COMMON% -DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF -DLEVELDB_BUILD_TESTS:BOOL=OFF || exit /b 1
cmake --build build-x86 --config %CFG% --target install || exit /b 1
popd

echo building yaml-cpp x86.
pushd deps\yaml-cpp
if exist build-x86 rmdir /s /q build-x86
cmake . -Bbuild-x86 %COMMON% -DMSVC_SHARED_RT:BOOL=OFF -DYAML_MSVC_SHARED_RT:BOOL=OFF -DYAML_CPP_BUILD_CONTRIB:BOOL=OFF -DYAML_CPP_BUILD_TOOLS:BOOL=OFF -DYAML_CPP_BUILD_TESTS:BOOL=OFF || exit /b 1
cmake --build build-x86 --config %CFG% --target install || exit /b 1
popd

echo building marisa x86.
pushd deps\marisa-trie
if exist build-x86 rmdir /s /q build-x86
cmake . -Bbuild-x86 %COMMON% || exit /b 1
cmake --build build-x86 --config %CFG% --target install || exit /b 1
popd

echo building opencc x86.
pushd deps\opencc
if exist build-x86 rmdir /s /q build-x86
cmake . -Bbuild-x86 %COMMON% -DBUILD_SHARED_LIBS:BOOL=OFF || exit /b 1
cmake --build build-x86 --config %CFG% --target install || exit /b 1
popd

echo DEPS-X86-OK
