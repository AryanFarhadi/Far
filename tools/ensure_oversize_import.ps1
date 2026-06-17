param(
  [string]$Path = (Join-Path $PSScriptRoot "..\tests\negative\oversize_import.far")
)

$minBytes = [int64](65 * 1024 * 1024)
$dir = Split-Path -Parent $Path
if (-not (Test-Path $dir)) {
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
}
$existing = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
if ($existing -and $existing.Length -ge $minBytes) {
  exit 0
}
$stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try {
  $chunk = New-Object byte[] (1 * 1024 * 1024)
  for ($i = 0; $i -lt $chunk.Length; $i++) { $chunk[$i] = [byte][char]'x' }
  $written = [int64]0
  while ($written -lt $minBytes) {
    $take = [Math]::Min($chunk.Length, $minBytes - $written)
    $stream.Write($chunk, 0, [int]$take)
    $written += $take
  }
} finally {
  $stream.Close()
}
