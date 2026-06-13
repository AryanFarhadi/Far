# Register .far file type + orange F icon in Windows Explorer (per-user).
#Requires -Version 5.1
param(
  [string]$SourceDir = "",
  [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) {
  if (-not $Quiet) { Write-Host "==> $msg" }
}

function Find-EditorExe {
  $candidates = @(
    'D:\cursor\Cursor.exe',
    'C:\cursor\Cursor.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\cursor\Cursor.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Microsoft VS Code\Code.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft VS Code\Code.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft VS Code\Code.exe')
  )
  foreach ($p in $candidates) {
    if ($p -and (Test-Path $p)) { return (Resolve-Path $p).Path }
  }
  foreach ($name in @('cursor', 'code')) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $cmd) { continue }
    $src = $cmd.Source
    if ($src -match '\.cmd$') {
      $exe = Join-Path (Split-Path (Split-Path $src -Parent) -Parent) 'Cursor.exe'
      if (Test-Path $exe) { return (Resolve-Path $exe).Path }
      $exe = Join-Path (Split-Path (Split-Path $src -Parent) -Parent) 'Code.exe'
      if (Test-Path $exe) { return (Resolve-Path $exe).Path }
    }
    if (Test-Path $src) { return $src }
  }
  return $null
}

if ($env:OS -ne "Windows_NT") {
  if (-not $Quiet) { Write-Host "Skipped: Windows only." }
  exit 0
}

$ScriptDir = $PSScriptRoot
$VsCodeDir = Split-Path -Parent $ScriptDir
if (-not $SourceDir) { $SourceDir = $VsCodeDir }

$SrcIco = Join-Path $SourceDir "icons\far-file.ico"
$CreateScript = Join-Path $ScriptDir "create-far-file-icon.ps1"
$SFTA = Join-Path $ScriptDir "SFTA.ps1"

& $CreateScript

if (-not (Test-Path $SrcIco)) {
  Write-Error "Icon not found: $SrcIco"
}

$FarAppDir = Join-Path $env:APPDATA "Far"
$IconDir = Join-Path $FarAppDir "icons"
$DestIco = Join-Path $IconDir "far-file.ico"
New-Item -ItemType Directory -Path $IconDir -Force | Out-Null
Copy-Item $SrcIco $DestIco -Force

$ProgId = "Far.Lang.SourceFile"
$Ext = ".far"
$IconRef = "$DestIco,0"
$TypeName = "Far Source File"
$EditorExe = Find-EditorExe

Write-Step "Registering $Ext with Far orange icon..."

New-Item -Path "HKCU:\Software\Classes\$Ext" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\$Ext" -Name "(default)" -Value $ProgId

New-Item -Path "HKCU:\Software\Classes\$ProgId" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId" -Name "(default)" -Value $TypeName

New-Item -Path "HKCU:\Software\Classes\$ProgId\DefaultIcon" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId\DefaultIcon" -Name "(default)" -Value $IconRef

if (Test-Path $SFTA) {
  . $SFTA
  Set-FTA -ProgId $ProgId -Extension $Ext -Icon $IconRef
}

# Open handler MUST be set last (double-click in Explorer)
if ($EditorExe) {
  $openCmd = "`"$EditorExe`" `"%1`""
  New-Item -Path "HKCU:\Software\Classes\$ProgId\shell" -Force | Out-Null
  New-Item -Path "HKCU:\Software\Classes\$ProgId\shell\open" -Force | Out-Null
  Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId\shell\open" -Name "(default)" -Value "Open"
  New-Item -Path "HKCU:\Software\Classes\$ProgId\shell\open\command" -Force | Out-Null
  Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId\shell\open\command" -Name "(default)" -Value $openCmd
  cmd /c "ftype $ProgId=$openCmd" | Out-Null
  if (-not $Quiet) { Write-Host "    Opens with: $EditorExe" }
} else {
  Write-Warning "No VS Code/Cursor found - .far files have icon but need an editor installed to open."
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ShellNotify {
  [DllImport("shell32.dll")]
  public static extern void SHChangeNotify(int eventId, int flags, IntPtr item1, IntPtr item2);
}
"@ -ErrorAction SilentlyContinue
[ShellNotify]::SHChangeNotify(0x08000000, 0x0, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

if (-not $Quiet) {
  Write-Host '    .far files use the deep orange Far icon.'
  Write-Host '    Close and reopen Explorer if the icon looks cached (yellow/old).'
  Write-Host "    Icon: $DestIco"
}
