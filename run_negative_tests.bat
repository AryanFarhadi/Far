@echo off

setlocal enabledelayedexpansion

set FAR=.\far.exe

if not exist "%FAR%" set FAR=far.exe

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ensure_oversize_import.ps1" >nul 2>&1

set FAIL=0

set PASS=0

for %%f in (tests\negative\*.far) do (

  if /i not "%%~nxf"=="oversize_import.far" (

    echo|set /p="neg   %%~nxf ... "

    call :do_neg_check "%%f"

  )

)

echo.

echo negative: %PASS% passed, %FAIL% failed

if %FAIL% gtr 0 exit /b 1

exit /b 0

:do_neg_check

"%FAR%" check "%~1" >nul 2>&1

if errorlevel 1 (

  echo ok

  set /a PASS+=1

  exit /b 0

)

echo FAIL ^(expected compile error^)

set /a FAIL+=1

exit /b 0
