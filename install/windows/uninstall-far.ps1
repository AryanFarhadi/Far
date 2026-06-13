#Requires -Version 5.1
param([switch]$Quiet)

$ErrorActionPreference = "Stop"

$InstallRoot = Join-Path $env:LOCALAPPDATA "Programs\Far"
$ManifestPath = Join-Path $env:APPDATA "Far\install.json"

function Remove-UserPathEntry([string]$dir) {
  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  if (-not $userPath) { return }
  $norm = $dir.TrimEnd('\')
  $parts = $userPath -split ';' | Where-Object {
    $_ -and $_.TrimEnd('\') -ine $norm
  }
  [Environment]::SetEnvironmentVariable("Path", ($parts -join ';'), "User")
}

function Clear-VsCodeFarSettings {
  $settingsPath = Join-Path $env:APPDATA "Code\User\settings.json"
  if (-not (Test-Path $settingsPath)) { return }
  try {
    $settings = Get-Content $settingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $settings.PSObject.Properties.Remove('far.compilerPath')
    $settings.PSObject.Properties.Remove('far.clangPath')
    ($settings | ConvertTo-Json -Depth 8) | Set-Content $settingsPath -Encoding UTF8
  } catch {
    Write-Host "Could not update VS Code settings.json" -ForegroundColor Yellow
  }
}

if (Test-Path $InstallRoot) {
  Remove-Item $InstallRoot -Recurse -Force
  Write-Host "Removed: $InstallRoot"
}

$clangBin = $null
if (Test-Path $ManifestPath) {
  try {
    $m = Get-Content $ManifestPath -Raw | ConvertFrom-Json
    if ($m.clangExe) { $clangBin = Split-Path $m.clangExe -Parent }
  } catch {}
  Remove-Item $ManifestPath -Force
  Write-Host "Removed: $ManifestPath"
}

Remove-UserPathEntry $InstallRoot
if ($clangBin) { Remove-UserPathEntry $clangBin }

[Environment]::SetEnvironmentVariable("FAR_CLANG", $null, "User")

Clear-VsCodeFarSettings

$manifestDir = Split-Path $ManifestPath -Parent
if ((Test-Path $manifestDir) -and -not (Get-ChildItem $manifestDir -ErrorAction SilentlyContinue)) {
  Remove-Item $manifestDir -Force
}

Write-Host "Far uninstalled." -ForegroundColor Green
if (-not $Quiet) { pause }
