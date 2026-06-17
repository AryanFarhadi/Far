Set-Location "e:\Users\Codex\Projects\Far"
$fail = @()

function Test-Run($name, $src, $expect) {
  $src | Set-Content "_ap.far" -NoNewline
  .\far.exe run "_ap.far" 2>&1 | Out-Null
  if ($LASTEXITCODE -ne $expect) { $script:fail += "$name want=$expect got=$LASTEXITCODE" }
  else { Write-Host "OK $name" }
}

function Test-CheckFail($name, $src) {
  $src | Set-Content "_ap.far" -NoNewline
  .\far.exe check "_ap.far" 2>&1 | Out-Null
  if ($LASTEXITCODE -eq 0) { $script:fail += "$name should fail check" }
  else { Write-Host "OK $name (compile fail)" }
}

# precedence
Test-Run "and_or" "fun main() -> i64 { if (false && (1/0)==1) { return 1 }; return 0 }" 0
Test-Run "or_short" "fun main() -> i64 { if (true || (1/0)==1) { return 0 }; return 1 }" 0
Test-Run "pow_chain" "fun main() -> i64 { return 2 ** 3 ** 2 }" 512
Test-Run "cmp_chain" "fun main() -> i64 { if (1 < 2 < 3) { return 1 }; return 0 }" 1

# ranges
Test-Run "range_empty" "fun main() -> i64 { n=0; for i in 5..4 { n=1 }; return n }" 0
Test-Run "range_one" "fun main() -> i64 { n=0; for i in 3..3 { n=n+1 }; return n }" 1

# bitwise boundary
Test-Run "shl0" "fun main() -> i64 { return 1 << 0 }" 1
Test-Run "shr_big" "fun main() -> i64 { return 1 >> 63 }" 0

# strings
Test-Run "str_empty" "fun main() -> i64 { return len("") }" 0
Test-Run "str_eq" "fun main() -> i64 { if ("" == "") { return 0 }; return 1 }" 0

# try edge
Test-Run "catch_val" @"
fun main() -> i64 {
  try { throw 42 } catch (e) { return e }
  return 0
}
"@ 42

# negative compile
Test-CheckFail "empty_file" ""
Test-CheckFail "unclosed_str" "fun main() { x = `"hi"
Test-CheckFail "bad_escape" 'fun main() { x = "\q" }'

if ($fail.Count) { $fail | ForEach-Object { Write-Host "FAIL $_" }; exit 1 }
Write-Host "All probes OK"
exit 0
