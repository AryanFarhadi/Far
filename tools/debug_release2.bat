@echo off
setlocal enabledelayedexpansion
set FAR=.\far.exe
set TIMEOUT_PS=%~dp0tools\run_with_timeout.ps1
set FAR_CHECK_TIMEOUT=60
for %%f in (tests\*.far) do (
  echo check %%~nxf
  if /i not "%%~nxf"=="types_release.far" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%TIMEOUT_PS%" -TimeoutSec %FAR_CHECK_TIMEOUT% -Exe "%FAR%" check "%%f" >nul 2>&1
    set _ec=!errorlevel!
    if !_ec! geq 1 (echo FAIL ec=!_ec!) else (echo ok)
  )
)
echo === types_release ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%TIMEOUT_PS%" -TimeoutSec %FAR_CHECK_TIMEOUT% -Exe "%FAR%" check "tests\types_release.far" >nul 2>&1
set _ec=!errorlevel!
echo types_release_ec=!_ec!
