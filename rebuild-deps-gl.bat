@echo off
rem Rebuild static deps with forced /GL so the final rime.dll link can do
rem whole-program optimization across librime + leveldb/marisa/glog/yaml/opencc.
rem Mirrors build.bat "deps" flags, plus forced CMAKE_(C|CXX)_FLAGS /GL.
rem ASCII only. 2026-10-31.
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
call env.bat
set RR=%RIME_ROOT%
set CFG=Release
set GEN=-G "%CMAKE_GENERATOR%" -A%ARCH% -T%PLATFORM_TOOLSET%
set COMMON=%GEN% -DCMAKE_CONFIGURATION_TYPES:STRING="%CFG%" -DCMAKE_BUILD_TYPE:STRING="%CFG%" ^
 -DCMAKE_USER_MAKE_RULES_OVERRIDE:PATH="%RR%\cmake\c_flag_overrides.cmake" ^
 -DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX:PATH="%RR%\cmake\cxx_flag_overrides.cmake" ^
 -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING="-llibcmt" ^
 -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
 -DCMAKE_C_FLAGS:STRING="/GL" -DCMAKE_CXX_FLAGS:STRING="/GL /utf-8" ^
 -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_INSTALL_PREFIX:PATH="%RR%"

echo building glog.
pushd deps\glog
cmake . -Bbuild %COMMON% -DBUILD_TESTING:BOOL=OFF -DWITH_GFLAGS:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building leveldb.
pushd deps\leveldb
cmake . -Bbuild %COMMON% -DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF -DLEVELDB_BUILD_TESTS:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building yaml-cpp.
pushd deps\yaml-cpp
cmake . -Bbuild %COMMON% -DMSVC_SHARED_RT:BOOL=OFF -DYAML_MSVC_SHARED_RT:BOOL=OFF -DYAML_CPP_BUILD_CONTRIB:BOOL=OFF -DYAML_CPP_BUILD_TOOLS:BOOL=OFF -DYAML_CPP_BUILD_TESTS:BOOL=OFF -DYAML_MODERN_CXX:BOOL=OFF || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building marisa.
pushd deps\marisa-trie
cmake . -Bbuild %COMMON% || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo building opencc.
pushd deps\opencc
cmake . -Bbuild %COMMON% -DBUILD_SHARED_LIBS:BOOL=OFF -DENABLE_DARTS:BOOL=ON || exit /b 1
cmake --build build --config %CFG% --target install || exit /b 1
popd

echo DEPS-GL-OK
