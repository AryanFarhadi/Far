#Requires -Version 5.1
param(
  [string]$Far = "",
  [int]$CheckTimeoutSec = 120,
  [int]$RunTimeoutSec = 120
)

$ErrorActionPreference = "Continue"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $root

if (-not $Far) {
  $Far = Join-Path $root "far.exe"
}
if (-not (Test-Path -LiteralPath $Far)) {
  $Far = "far.exe"
}

$timeoutPs = Join-Path $PSScriptRoot "run_with_timeout.ps1"

function Invoke-FarCheck([string]$path) {
  & $Far check $path 2>&1 | Out-Null
  return $LASTEXITCODE
}

function Invoke-FarRun([string]$path, [int]$timeoutSec) {
  & powershell -NoProfile -ExecutionPolicy Bypass -File $timeoutPs `
    -TimeoutSec $timeoutSec -Exe $Far run $path 2>&1 | Out-Null
  return $LASTEXITCODE
}

$checkFiles = @()
$checkFiles += Get-ChildItem (Join-Path $root "tests\*.far") | Sort-Object Name
$checkFiles += Get-ChildItem (Join-Path $root "tests\comprehensive\*.far") -Recurse | Sort-Object FullName

$checkFail = 0
$checkPass = 0
foreach ($f in $checkFiles) {
  Write-Host "check $($f.Name)"
  $ec = Invoke-FarCheck $f.FullName
  if ($ec -ne 0) {
    Write-Host "FAIL"
    & $Far check $f.FullName 2>&1
    $checkFail++
  } else {
    Write-Host "ok"
    $checkPass++
  }
}

Write-Host ""
Write-Host "check: $checkPass passed, $checkFail failed"

$runFail = 0
$runPass = 0
foreach ($f in $checkFiles) {
  Write-Host "run   $($f.Name)"
  $ec = Invoke-FarRun $f.FullName $RunTimeoutSec
  if ($ec -eq 124) {
    Write-Host "TIMEOUT"
    $runFail++
  } elseif ($ec -ne 0) {
    Write-Host "FAIL"
    $runFail++
  } else {
    Write-Host "ok"
    $runPass++
  }
}

Write-Host ""
Write-Host "run: $runPass passed, $runFail failed"

if ($checkFail -gt 0 -or $runFail -gt 0) {
  exit 1
}

$negBat = Join-Path $root "run_negative_tests.bat"
if (Test-Path -LiteralPath $negBat) {
  & cmd /c $negBat
  if ($LASTEXITCODE -ne 0) {
    exit 1
  }
}

exit 0
