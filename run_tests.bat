@echo off
setlocal enabledelayedexpansion
set FAR=.\far.exe
if not exist "%FAR%" set FAR=far.exe
set FAIL=0
set PASS=0
for /r examples\tests %%f in (*.far) do (
  echo|set /p="check %%~nxf ... "
  "%FAR%" check "%%f" >nul 2>&1
  if errorlevel 1 (
    echo FAIL
    set /a FAIL+=1
  ) else (
    echo ok
    set /a PASS+=1
  )
)
echo.
echo check: %PASS% passed, %FAIL% failed
set RFAIL=0
set RPASS=0
for /r examples\tests %%f in (*.far) do (
  echo|set /p="run   %%~nxf ... "
  "%FAR%" run "%%f" >nul 2>&1
  if errorlevel 1 (
    echo FAIL
    set /a RFAIL+=1
  ) else (
    echo ok
    set /a RPASS+=1
  )
)
echo.
echo run: %RPASS% passed, %RFAIL% failed
if %FAIL% gtr 0 exit /b 1
if %RFAIL% gtr 0 exit /b 1
exit /b 0
