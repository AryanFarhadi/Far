# Build far-lang VSIX without npm/vsce (offline-friendly).
param(
  [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$VsCodeDir = Split-Path -Parent $ScriptDir
$Root = Split-Path -Parent $VsCodeDir
$PkgJson = Join-Path $VsCodeDir "package.json"
if (-not $Version) {
  $pkg = Get-Content $PkgJson -Raw | ConvertFrom-Json
  $Version = $pkg.version
}
$VsixDir = Join-Path $VsCodeDir ".vsix-build"
$OutVsix = Join-Path $VsCodeDir "far-lang-$Version.vsix"

Write-Host "=== Building Far file icon ==="
$iconScript = Join-Path $VsCodeDir "scripts\create-far-file-icon.ps1"
if (Test-Path $iconScript) {
  & powershell -NoProfile -ExecutionPolicy Bypass -File $iconScript
}

Write-Host "=== Core API (from far_stdlib_modules.cpp) ==="
$apiScript = Join-Path $Root "tools\generate-core-api.mjs"
if (Test-Path $apiScript) {
  Push-Location $Root
  node $apiScript
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  Pop-Location
} else {
  Write-Host "Using committed vscode/data/core-api.json"
}

if (Test-Path $VsixDir) { Remove-Item $VsixDir -Recurse -Force }
$ExtDir = Join-Path $VsixDir "extension"
New-Item -ItemType Directory -Path $ExtDir | Out-Null

$Include = @(
  "package.json", "extension.js", "intellisense.js", "far-syntax-colors.js", "language-configuration.json", "README.md", "LICENSE",
  "syntaxes", "snippets", "data", "icons", "themes", "scripts"
)
foreach ($item in $Include) {
  $src = Join-Path $VsCodeDir $item
  $dst = Join-Path $ExtDir $item
  if (Test-Path $src) {
    Copy-Item $src $dst -Recurse -Force
  }
}

Copy-Item (Join-Path $VsCodeDir "vsix-template\extension.vsixmanifest") (Join-Path $VsixDir "extension.vsixmanifest") -Force
Copy-Item (Join-Path $VsCodeDir "vsix-template\[Content_Types].xml") (Join-Path $VsixDir "[Content_Types].xml") -Force

if (Test-Path $OutVsix) { Remove-Item $OutVsix -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($VsixDir, $OutVsix)

Remove-Item $VsixDir -Recurse -Force
Write-Host "Created: $OutVsix"
Write-Host ""
Write-Host "Install:"
Write-Host "  code --install-extension `"$OutVsix`""
