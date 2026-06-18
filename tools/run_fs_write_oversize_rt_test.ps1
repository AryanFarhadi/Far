#Requires -Version 5.1
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $root

$rtO = Join-Path $root "runtime\far_rt.windows-gnu.o"
$rtC = Join-Path $root "runtime\far_rt.c"
$testSrc = Join-Path $PSScriptRoot "fs_write_oversize_rt_test.c"
$testExe = Join-Path $root "fs_write_oversize_rt_test.exe"

if (-not (Test-Path -LiteralPath $rtO)) {
  if (-not (Get-Command clang -ErrorAction SilentlyContinue)) {
    Write-Host "skip fs_write_oversize_rt_test (no runtime object and no clang)"
    exit 0
  }
  $rtTmp = "$rtO.tmp"
  & clang -O0 -c $rtC -o $rtTmp -I (Join-Path $root "runtime")
  if ($LASTEXITCODE -ne 0) { exit 1 }
  Move-Item -Force $rtTmp $rtO
}

if (-not (Get-Command clang -ErrorAction SilentlyContinue)) {
  Write-Host "skip fs_write_oversize_rt_test (no clang)"
  exit 0
}

& clang -O2 $testSrc $rtO -o $testExe -lws2_32 -lwinhttp
if ($LASTEXITCODE -ne 0) { exit 1 }

& $testExe
exit $LASTEXITCODE
