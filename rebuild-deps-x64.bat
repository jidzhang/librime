@echo off
rem Rebuild x64 static deps into the root prefix (librime\lib + librime\include).
rem ASCII only.
rem
rem 2026-11-15: glog was removed upstream (replaced by the spdlog facade), so the
rem glog step is gone. spdlog is header-only and consumed straight from the
rem deps\spdlog submodule by CMake; it needs no separate build step here.
rem
rem The x86 counterpart is rebuild-deps-x86.bat, which installs into librime\x86
rem so the two architectures never overwrite each other.
call "%~dp0vcvars-detect.bat"
cd /d "%~dp0"
call env.bat
set ARCH=x64
set RR=%RIME_ROOT:~0,-1%
set CFG=Release
set GEN=-G "%CMAKE_GENERATOR%" -A%ARCH% -T%PLATFORM_TOOLSET%
set COMMON=%GEN% -DCMAKE_CONFIGURATION_TYPES:STRING="%CFG%" -DCMAKE_BUILD_TYPE:STRING="%CFG%" ^
 -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt" ^
 -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
 -DCMAKE_C_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG" ^
 -DCMAKE_CXX_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG /utf-8" ^
 -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_INSTALL_PREFIX:PATH="%RR%"

echo building leveldb.
pushd deps\leveldb
if exist build rmdir /s /q build
cmake . -Bbuild %COMMON% -DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF -DLEVELDB_BUILD_TESTS:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building yaml-cpp.
pushd deps\yaml-cpp
if exist build rmdir /s /q build
cmake . -Bbuild %COMMON% -DMSVC_SHARED_RT:BOOL=OFF -DYAML_MSVC_SHARED_RT:BOOL=OFF -DYAML_CPP_BUILD_CONTRIB:BOOL=OFF -DYAML_CPP_BUILD_TOOLS:BOOL=OFF -DYAML_CPP_BUILD_TESTS:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building marisa.
pushd deps\marisa-trie
if exist build rmdir /s /q build
cmake . -Bbuild %COMMON% || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building opencc.
pushd deps\opencc
if exist build rmdir /s /q build
cmake . -Bbuild %COMMON% -DBUILD_SHARED_LIBS:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo DEPS-X64-OK
