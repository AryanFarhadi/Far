Set-Location "e:\Users\Codex\Projects\Far"
$fail = @()
function Test-Run($name, [scriptblock]$write, $expect) {
  & $write
  .\far.exe run "_ap.far" 2>&1 | Out-Null
  if ($LASTEXITCODE -ne $expect) { $script:fail += "$name want=$expect got=$LASTEXITCODE" }
  else { Write-Host "OK $name" }
}

Test-Run "nested_fin_ret" {
@'
fun main() -> i64 {
  try { try { return 1 } finally { n=1 } } finally { return 2 }
}
'@ | Set-Content _ap.far
} 2

Test-Run "throw_nested_catch" {
@'
fun main() -> i64 {
  try { throw 1 } catch (a) {
    try { throw 2 } catch (b) { return b }
  }
  return 0
}
'@ | Set-Content _ap.far
} 2

Test-Run "defer_in_loop_break" {
@'
fun main() -> i64 {
  n = 0
  for i in 0..<3 {
    defer n = n + 1
    if i == 2 { break }
  }
  return n
}
'@ | Set-Content _ap.far
} 1

Test-Run "match_int" {
@'
fun main() -> i64 {
  match 2 { 1 => return 0; 2 => return 5; _ => return 9 }
}
'@ | Set-Content _ap.far
} 5

Test-Run "dict_access" {
@'
fun main() -> i64 {
  d = {"a": 1, "b": 2}
  return d["a"]
}
'@ | Set-Content _ap.far
} 1

Test-Run "u64_max" {
@'
fun main() -> i64 { return i64(u64(18446744073709551615)) }
'@ | Set-Content _ap.far
} -1

if ($fail.Count) { $fail; exit 1 }
Write-Host "Batch OK"
exit 0
