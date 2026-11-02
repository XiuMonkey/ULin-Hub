@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0..\.."
pushd "%ROOT%" || (echo cannot enter project root & pause & exit /b 1)

echo Cleaning dead files (.d.ts / .d.mts / .d.cts / .map / .pdb)
echo Root: %CD%
echo.

for %%R in (
  "assets\resources\server"
  "x64\Release\resources\server"
  "misc\release-build\bin\resources\server"
) do (
  if exist "%%~R\node_modules" (
    echo --- %%~R
    del /s /f /q "%%~R\node_modules\*.d.ts"   2>nul
    del /s /f /q "%%~R\node_modules\*.d.mts"  2>nul
    del /s /f /q "%%~R\node_modules\*.d.cts"  2>nul
    del /s /f /q "%%~R\node_modules\*.map"    2>nul
    del /s /f /q "%%~R\node_modules\*.pdb"    2>nul
  ) else (
    echo --- %%~R  ^(skipped, not found^)
  )
)

echo.
echo Done. LICENSE / COPYING / NOTICE files were not touched.
popd
pause
