#Requires -Version 5.1
param(
  [string]$RepoRoot = "",
  [string]$InstallRoot = "",
  [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function Write-Ok($msg) {
  if (-not $Quiet) { Write-Host "    $msg" -ForegroundColor Green }
}

function Export-FarInstallFiles([string]$root, [string]$installRoot) {
  $builtFar = Join-Path $root "far.exe"
  if (-not (Test-Path $builtFar)) {
    throw "far.exe not found in $root"
  }

  $farExe = Join-Path $installRoot "far.exe"
  $runtimeDir = Join-Path $installRoot "runtime"

  New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
  New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null

  Copy-Item $builtFar $farExe -Force
  Get-ChildItem (Join-Path $root "runtime\*.c") | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $runtimeDir $_.Name) -Force
  }
  $rtObj = Join-Path $root "runtime\far_rt.windows-x64.o"
  if (Test-Path $rtObj) {
    Copy-Item $rtObj (Join-Path $runtimeDir "far_rt.windows-x64.o") -Force
  }

  return $farExe
}

if (-not $RepoRoot) {
  $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

if (-not $InstallRoot) {
  $manifestPath = Join-Path $env:APPDATA "Far\install.json"
  if (-not (Test-Path $manifestPath)) {
    exit 0
  }

  $manifest = Get-Content $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($manifest.installRoot) {
    $InstallRoot = [string]$manifest.installRoot
  } elseif ($manifest.farExe) {
    $InstallRoot = Split-Path ([string]$manifest.farExe) -Parent
  }
  if (-not $InstallRoot) {
    exit 0
  }
}

$farExe = Export-FarInstallFiles $RepoRoot $InstallRoot
Write-Ok "Updated installed Far at $farExe"
