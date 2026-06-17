@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0
set BIN=%ROOT%bin
set "ROOT=%ROOT:~0,-1%"
if not defined FAR set FAR=%ROOT%..\far.exe
if not exist "%FAR%" set FAR=far.exe
where clang >nul 2>&1
if errorlevel 1 (
  echo clang not found on PATH
  exit /b 1
)
where python >nul 2>&1
if errorlevel 1 (
  echo python not found on PATH
  exit /b 1
)

if not exist "%BIN%" mkdir "%BIN%"

echo === Far heavy benchmark: C vs Far vs Python ===
echo Compiler: clang -O2   Far: %FAR%
echo.
echo Lang     Benchmark          ms  checksum
echo ------------------------------------------------

set NAMES=fib_iter collatz_sum sum_squares nested_loop

for %%B in (%NAMES%) do (
  clang -O2 -I"%ROOT%" "%ROOT%\%%B.c" -o "%BIN%\%%B_c.exe"
  if errorlevel 1 (
    echo C compile failed: %%B
    exit /b 1
  )
  "%FAR%" compile "%ROOT%\%%B.far" -o "%BIN%\%%B_far.exe" >nul 2>&1
  if errorlevel 1 (
    echo Far compile failed: %%B
    exit /b 1
  )
)

for %%B in (%NAMES%) do (
  "%BIN%\%%B_c.exe"
  set "FMS="
  set "FRES="
  for /f "usebackq delims=" %%L in (`"%BIN%\%%B_far.exe"`) do (
    if not defined FMS (set "FMS=%%L") else (set "FRES=%%L")
  )
  for /f %%U in ('powershell -NoProfile -Command "[BitConverter]::ToUInt64([BitConverter]::GetBytes([int64]!FRES!),0)"') do set "FRES=%%U"
  echo Far      %%B           !FMS! ms  result=!FRES!
  python "%ROOT%\%%B.py"
)

echo.
echo Done. Scale C/Python via BENCH_FIB_N, BENCH_COLLATZ_LIMIT, BENCH_SUM_SQUARES_N, BENCH_NESTED_N
echo Far uses workload constants in each .far file; edit them to match when scaling.
exit /b 0
