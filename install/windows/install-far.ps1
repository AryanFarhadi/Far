#Requires -Version 5.1
param(
  [string]$InstallRoot = "",
  [switch]$SkipBuild,
  [switch]$SkipClangInstall,
  [switch]$SkipVsCode,
  [switch]$SkipPath,
  [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) {
  if (-not $Quiet) { Write-Host "==> $msg" -ForegroundColor Cyan }
}

function Write-Ok($msg) {
  if (-not $Quiet) { Write-Host "    $msg" -ForegroundColor Green }
}

function Write-Warn($msg) {
  Write-Host "    WARNING: $msg" -ForegroundColor Yellow
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $InstallRoot) {
  $InstallRoot = Join-Path $env:LOCALAPPDATA "Programs\Far"
}

$FarExe = Join-Path $InstallRoot "far.exe"
$ManifestDir = Join-Path $env:APPDATA "Far"
$ManifestPath = Join-Path $ManifestDir "install.json"
$VsixPath = Join-Path $RepoRoot "vscode\far-lang-0.2.2.vsix"

function Find-Clang {
  $cmd = Get-Command clang -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $candidates = @(
    "$env:ProgramFiles\LLVM\bin\clang.exe",
    "${env:ProgramFiles(x86)}\LLVM\bin\clang.exe"
  )

  $wingetRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
  if (Test-Path $wingetRoot) {
    $found = Get-ChildItem -Path $wingetRoot -Recurse -Filter "clang.exe" -ErrorAction SilentlyContinue |
      Select-Object -First 1 -ExpandProperty FullName
    if ($found) { return $found }
  }

  foreach ($c in $candidates) {
    if (Test-Path $c) { return $c }
  }
  return $null
}

function Install-LlvmViaWinget {
  $winget = Get-Command winget -ErrorAction SilentlyContinue
  if (-not $winget) {
    Write-Warn "winget not found. Install LLVM manually: https://github.com/mstorsjo/llvm-mingw/releases"
    return $false
  }
  Write-Step "Installing LLVM-MinGW (clang) via winget..."
  & winget install --id MartinStorsjo.LLVM-MinGW.UCRT -e --accept-source-agreements --accept-package-agreements
  if ($LASTEXITCODE -ne 0) {
    Write-Warn "winget install failed (exit $LASTEXITCODE). Install clang manually."
    return $false
  }
  return $true
}

function Add-UserPathEntry([string]$dir) {
  if (-not (Test-Path $dir)) { return }
  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $parts = @()
  if ($userPath) { $parts = $userPath -split ';' | Where-Object { $_ -and $_.Trim() -ne '' } }
  $norm = $dir.TrimEnd('\')
  if ($parts | Where-Object { $_.TrimEnd('\') -ieq $norm }) { return }
  $parts += $dir
  [Environment]::SetEnvironmentVariable("Path", ($parts -join ';'), "User")
  $env:Path = "$dir;$env:Path"
  Write-Ok "Added to user PATH: $dir"
}

function Read-JsonFile([string]$path) {
  if (-not (Test-Path $path)) { return $null }
  $raw = Get-Content $path -Raw -Encoding UTF8
  if (-not $raw.Trim()) { return $null }
  return $raw | ConvertFrom-Json
}

function Write-JsonFile([string]$path, $obj) {
  ($obj | ConvertTo-Json -Depth 8) | Set-Content $path -Encoding UTF8
}

function Merge-VsCodeSettings([string]$farPath, [string]$clangPath) {
  $code = Get-Command code -ErrorAction SilentlyContinue
  if (-not $code) {
    Write-Warn "VS Code 'code' CLI not on PATH - skip auto settings (extension may still install)."
    return
  }

  $settingsPath = Join-Path $env:APPDATA "Code\User\settings.json"
  $settingsDir = Split-Path $settingsPath -Parent
  if (-not (Test-Path $settingsDir)) { New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null }

  $json = @"
{
  "far.compilerPath": "$($farPath -replace '\\','\\')",
  "far.clangPath": "$($clangPath -replace '\\','\\')",
  "files.associations": {
    "*.far": "far"
  }
}
"@

  if (-not (Test-Path $settingsPath)) {
    $json | Set-Content $settingsPath -Encoding UTF8
    Write-Ok "Created VS Code settings: $settingsPath"
    return
  }

  try {
    $existing = Read-JsonFile $settingsPath
    if ($null -eq $existing) {
      $json | Set-Content $settingsPath -Encoding UTF8
    } else {
      $existing | Add-Member -NotePropertyName "far.compilerPath" -NotePropertyValue $farPath -Force
      $existing | Add-Member -NotePropertyName "far.clangPath" -NotePropertyValue $clangPath -Force
      if (-not $existing.'files.associations') {
        $existing | Add-Member -NotePropertyName "files.associations" -NotePropertyValue (@{ "*.far" = "far" }) -Force
      } else {
        $existing.'files.associations'.'*.far' = "far"
      }
      Write-JsonFile $settingsPath $existing
    }
    Write-Ok "Updated VS Code settings: $settingsPath"
  } catch {
    Write-Warn "Could not merge settings.json - set far.compilerPath manually in VS Code."
  }
}

function Install-VsCodeExtension {
  if ($SkipVsCode) { return }
  if (-not (Test-Path $VsixPath)) {
    Write-Step "Building VS Code extension..."
    $pkg = Join-Path $RepoRoot "vscode\package-extension.bat"
    if (Test-Path $pkg) {
      Push-Location $RepoRoot
      cmd /c "`"$pkg`""
      Pop-Location
    }
  }
  $vsix = Get-ChildItem (Join-Path $RepoRoot "vscode\far-lang-*.vsix") -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $vsix) {
    Write-Warn "VSIX not found - skip extension install."
    return
  }
  $code = Get-Command code -ErrorAction SilentlyContinue
  if (-not $code) {
    Write-Warn "Install VS Code extension manually: $($vsix.FullName)"
    return
  }
  Write-Step "Installing VS Code extension..."
  & code --install-extension $vsix.FullName --force
  Write-Ok "Extension installed: $($vsix.Name)"

  $regScript = Join-Path $RepoRoot "vscode\scripts\register-far-windows.ps1"
  if (Test-Path $regScript) {
    Write-Step "Registering Windows .far file icon..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File $regScript -SourceDir (Join-Path $RepoRoot "vscode")
    Write-Ok "Windows .far file type registered"
  }
}

# --- Build far.exe ---
if (-not $SkipBuild) {
  if (-not (Test-Path (Join-Path $RepoRoot "far.exe"))) {
    Write-Step "Building far.exe..."
    Push-Location $RepoRoot
    cmd /c build.bat
    if ($LASTEXITCODE -ne 0) { throw "build.bat failed" }
    Pop-Location
  }
}

$builtFar = Join-Path $RepoRoot "far.exe"
if (-not (Test-Path $builtFar)) {
  throw "far.exe not found. Run build.bat from repo root first."
}

# --- Install files ---
Write-Step "Installing Far to $InstallRoot ..."
& (Join-Path $PSScriptRoot "sync-far-install.ps1") -RepoRoot $RepoRoot -InstallRoot $InstallRoot -Quiet
Write-Ok "Installed far.exe + runtime (sources + headers)"

# --- Clang ---
$clangPath = Find-Clang
if (-not $clangPath -and -not $SkipClangInstall) {
  Install-LlvmViaWinget | Out-Null
  $clangPath = Find-Clang
}
if (-not $clangPath) {
  throw @"
clang not found.

Install LLVM-MinGW, then re-run this installer:
  winget install MartinStorsjo.LLVM-MinGW.UCRT

Or download: https://github.com/mstorsjo/llvm-mingw/releases
"@
}
Write-Ok "Clang: $clangPath"

$clangBin = Split-Path $clangPath -Parent
[Environment]::SetEnvironmentVariable("FAR_CLANG", $clangPath, "User")
$env:FAR_CLANG = $clangPath

# --- PATH ---
if (-not $SkipPath) {
  Add-UserPathEntry $InstallRoot
  Add-UserPathEntry $clangBin
}

# --- Manifest ---
New-Item -ItemType Directory -Path $ManifestDir -Force | Out-Null
$manifest = @{
  version      = "0.2.2"
  installRoot  = $InstallRoot
  farExe       = $FarExe
  clangExe     = $clangPath
  installedAt  = (Get-Date).ToString("o")
}
$manifest | ConvertTo-Json | Set-Content $ManifestPath -Encoding UTF8
Write-Ok "Wrote manifest: $ManifestPath"

# --- VS Code ---
Merge-VsCodeSettings $FarExe $clangPath
Install-VsCodeExtension

# --- Verify ---
Write-Step "Verifying installation..."
Push-Location $env:TEMP
& $FarExe perf
if ($LASTEXITCODE -ne 0) { Write-Warn "far perf returned non-zero" }
Pop-Location

Write-Host ""
Write-Host "Far installed successfully!" -ForegroundColor Green
Write-Host "  far.exe : $FarExe"
Write-Host "  clang   : $clangPath"
Write-Host ""
Write-Host "Reload VS Code, then open a .far file and click Run."
Write-Host "If PATH was updated, open a NEW terminal window."
