@echo off
setlocal enabledelayedexpansion
set FAR=.\far.exe
if not exist "%FAR%" set FAR=far.exe
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ensure_fs_max_read_fixture.ps1" >nul 2>&1
set FAIL=0
set PASS=0
for %%f in (tests\runtime_negative\*.far) do (
  echo|set /p="rtneg %%~nxf ... "
  if /i "%%~nxf"=="sem_wait_zero_blocks.far" (
    call :do_rtneg_timeout "%%f" 3
  ) else (
    call :do_rtneg_run "%%f"
  )
)
echo|set /p="rtneg fs_write_oversize (C) ... "
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_fs_write_oversize_rt_test.ps1" >nul 2>&1
if errorlevel 1 (
  echo FAIL
  set /a FAIL+=1
) else (
  echo ok
  set /a PASS+=1
)
echo.
echo runtime_negative: %PASS% passed, %FAIL% failed
if %FAIL% gtr 0 exit /b 1
exit /b 0

:do_rtneg_run

"%FAR%" run "%~1" >nul 2>&1

if errorlevel 1 (

  echo ok

  set /a PASS+=1

  exit /b 0

)

echo FAIL ^(expected runtime panic/nonzero exit^)

set /a FAIL+=1

exit /b 0

:do_rtneg_timeout

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_with_timeout.ps1" -TimeoutSec %~2 -Exe "%FAR%" run "%~1" >nul 2>&1

if errorlevel 1 (

  echo ok

  set /a PASS+=1

  exit /b 0

)

echo FAIL ^(expected hang/timeout/nonzero exit^)

set /a FAIL+=1

exit /b 0
