param(
  [Parameter(Mandatory = $true)][int]$TimeoutSec,
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Args
)

$outFile = [System.IO.Path]::GetTempFileName()
$errFile = [System.IO.Path]::GetTempFileName()
try {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $Exe
  $psi.Arguments = ($Args | ForEach-Object {
    if ($_ -match '\s') { '"' + ($_ -replace '"', '""') + '"' } else { $_ }
  }) -join ' '
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true

  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  [void]$proc.Start()

  $stdout = $proc.StandardOutput.ReadToEndAsync()
  $stderr = $proc.StandardError.ReadToEndAsync()

  if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    try { $proc.Kill($true) } catch { try { $proc.Kill() } catch {} }
    exit 124
  }

  [void][System.Threading.Tasks.Task]::WaitAll(@($stdout, $stderr))
  [System.IO.File]::WriteAllText($outFile, $stdout.Result)
  [System.IO.File]::WriteAllText($errFile, $stderr.Result)
  exit $proc.ExitCode
} finally {
  Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $errFile -Force -ErrorAction SilentlyContinue
}
