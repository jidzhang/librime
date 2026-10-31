@echo off
rem Rebuild static deps from CURRENT sources WITHOUT /GL (isolation control
rem for the GL negative-optimization verification). Same as rebuild-deps-gl.bat
rem but: no forced /GL, and NO cxx_flag_overrides (which carry /GL in
rem RELEASE_INIT) - CRT/O2/utf-8 set explicitly instead.
rem ASCII only. 2026-10-31.
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
call env.bat
set RR=%RIME_ROOT%
set CFG=Release
set GEN=-G "%CMAKE_GENERATOR%" -A%ARCH% -T%PLATFORM_TOOLSET%
set COMMON=%GEN% -DCMAKE_CONFIGURATION_TYPES:STRING="%CFG%" -DCMAKE_BUILD_TYPE:STRING="%CFG%" ^
 -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt" ^
 -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
 -DCMAKE_C_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG" ^
 -DCMAKE_CXX_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG /utf-8" ^
 -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_INSTALL_PREFIX:PATH="%RR%"

echo building glog.
pushd deps\glog
if exist build rmdir /s /q build
cmake . -Bbuild %COMMON% -DBUILD_TESTING:BOOL=OFF -DWITH_GFLAGS:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building leveldb.
pushd deps\leveldb
if exist build rmdir /s /q build
cmake . -Bbuild %COMMON% -DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF -DLEVELDB_BUILD_TESTS:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building yaml-cpp.
pushd deps\yaml-cpp
if exist build rmdir /s /q build
cmake . -Bbuild %COMMON% -DMSVC_SHARED_RT:BOOL=OFF -DYAML_MSVC_SHARED_RT:BOOL=OFF -DYAML_CPP_BUILD_CONTRIB:BOOL=OFF -DYAML_CPP_BUILD_TOOLS:BOOL=OFF -DYAML_CPP_BUILD_TESTS:BOOL=OFF -DYAML_MODERN_CXX:BOOL=OFF || exit /b 1
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
cmake . -Bbuild %COMMON% -DBUILD_SHARED_LIBS:BOOL=OFF -DENABLE_DARTS:BOOL=ON || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo DEPS-NOGL-OK
