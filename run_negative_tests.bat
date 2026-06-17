@echo off
setlocal enabledelayedexpansion
set FAR=.\far.exe
if not exist "%FAR%" set FAR=far.exe
set FAIL=0
set PASS=0

for %%f in (tests\negative\*.far) do (
  echo|set /p="neg   %%~nxf ... "
  "%FAR%" check "%%f" >nul 2>&1
  if not errorlevel 1 (
    echo FAIL ^(expected compile error^)
    set /a FAIL+=1
  ) else (
    echo ok
    set /a PASS+=1
  )
)

echo.
echo negative: %PASS% passed, %FAIL% failed
if %FAIL% gtr 0 exit /b 1
exit /b 0
