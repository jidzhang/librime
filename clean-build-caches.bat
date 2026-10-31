@echo off
rem Clean all local build caches (librime + static deps) to force a full rebuild.
rem Use before important builds: incremental msbuild can silently reuse stale
rem objects when dep sources were updated after their libs were built
rem (2026-10-31 real case: opencc src updated 19:02, lib built 12:40, stale
rem opencc.obj in build\ kept linking the old API).
rem ASCII only. Usage: clean-build-caches.bat
cd /d "%~dp0"
echo cleaning librime\build ...
if exist build rmdir /s /q build
for %%D in (glog googletest leveldb marisa-trie opencc yaml-cpp) do (
  echo cleaning deps\%%D\build ...
  if exist deps\%%D\build rmdir /s /q deps\%%D\build
)
echo.
echo NOTE: installed outputs in lib\ include\ share\ are NOT removed.
echo       To rebuild deps too, run: rebuild-deps-gl.bat
echo CLEAN-DONE
