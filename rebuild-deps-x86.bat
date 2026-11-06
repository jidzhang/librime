@echo off
rem Build x86 (Win32) static deps into librime\x86\ — separate prefix and
rem build dirs so the x64 artifacts in lib\ are untouched. boost stays
rem header-only (install-boost.bat only runs b2 headers). ASCII only.
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
call env.bat
set ARCH=Win32
set RR=%RIME_ROOT%
set CFG=Release
set GEN=-G "%CMAKE_GENERATOR%" -A%ARCH% -T%PLATFORM_TOOLSET%
set COMMON=%GEN% -DCMAKE_CONFIGURATION_TYPES:STRING="%CFG%" -DCMAKE_BUILD_TYPE:STRING="%CFG%" ^
 -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt" ^
 -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
 -DCMAKE_C_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG" ^
 -DCMAKE_CXX_FLAGS_RELEASE:STRING="/MT /O2 /Ob2 /DNDEBUG /utf-8" ^
 -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_INSTALL_PREFIX:PATH="%RR%\x86"

echo building glog x86.
pushd deps\glog
if exist build-x86 rmdir /s /q build-x86
cmake . -Bbuild-x86 %COMMON% -DBUILD_TESTING:BOOL=OFF -DWITH_GFLAGS:BOOL=OFF || exit /b 1
cmake --build build-x86 --config %CFG% --target install || exit /b 1
popd

echo building leveldb x86.
pushd deps\leveldb
if exist build-x86 rmdir /s /q build-x86
cmake . -Bbuild-x86 %COMMON% -DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF -DLEVELDB_BUILD_TESTS:BOOL=OFF || exit /b 1
cmake --build build-x86 --config %CFG% --target install || exit /b 1
popd

echo building yaml-cpp x86.
pushd deps\yaml-cpp
if exist build-x86 rmdir /s /q build-x86
cmake . -Bbuild-x86 %COMMON% -DMSVC_SHARED_RT:BOOL=OFF -DYAML_MSVC_SHARED_RT:BOOL=OFF -DYAML_CPP_BUILD_CONTRIB:BOOL=OFF -DYAML_CPP_BUILD_TOOLS:BOOL=OFF -DYAML_CPP_BUILD_TESTS:BOOL=OFF -DYAML_MODERN_CXX:BOOL=OFF || exit /b 1
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
cmake . -Bbuild-x86 %COMMON% -DBUILD_SHARED_LIBS:BOOL=OFF -DENABLE_DARTS:BOOL=ON || exit /b 1
cmake --build build-x86 --config %CFG% --target install || exit /b 1
popd

echo DEPS-X86-OK
