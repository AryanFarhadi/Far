param(
  [string]$Path = (Join-Path $PSScriptRoot "..\far_oversize_read_test.bin")
)

$minBytes = [int64](64 * 1024 * 1024) + 1
$dir = Split-Path -Parent $Path
if ($dir -and -not (Test-Path $dir)) {
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
}
$existing = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
if ($existing -and $existing.Length -ge $minBytes) {
  exit 0
}
$stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try {
  $chunk = New-Object byte[] (1 * 1024 * 1024)
  for ($i = 0; $i -lt $chunk.Length; $i++) { $chunk[$i] = [byte][char]'a' }
  $written = [int64]0
  while ($written -lt $minBytes) {
    $take = [Math]::Min($chunk.Length, $minBytes - $written)
    $stream.Write($chunk, 0, [int]$take)
    $written += $take
  }
} finally {
  $stream.Close()
}
