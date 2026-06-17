#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/bin"
FAR="${FAR:-$ROOT/../far}"
CLANG="${CLANG:-clang}"

command -v "$CLANG" >/dev/null
command -v python3 >/dev/null
[[ -x "$FAR" || -x "$FAR.exe" ]] || FAR="$(command -v far || true)"
[[ -n "$FAR" ]] || { echo "far not found"; exit 1; }

mkdir -p "$BIN"

echo "=== Far heavy benchmark: C vs Far vs Python ==="
echo "Compiler: $CLANG -O2   Far: $FAR"
echo
printf '%-8s %-14s %8s  %s\n' Lang Benchmark ms checksum
echo "------------------------------------------------"

NAMES=(fib_iter collatz_sum sum_squares nested_loop)

for b in "${NAMES[@]}"; do
  "$CLANG" -O2 -I"$ROOT" "$ROOT/$b.c" -o "$BIN/${b}_c"
  "$FAR" compile "$ROOT/$b.far" -o "$BIN/${b}_far"
done

for b in "${NAMES[@]}"; do
  "$BIN/${b}_c"
  mapfile -t far_out < <("$BIN/${b}_far")
  far_res="$(python3 -c "import struct; print(struct.unpack('>Q', struct.pack('>q', int('${far_out[1]}')))[0])")"
  printf 'Far      %-14s %8s ms  result=%s\n' "$b" "${far_out[0]}" "$far_res"
  python3 "$ROOT/$b.py"
done

echo
echo "Done. Scale C/Python via BENCH_* env vars; edit consts in .far files to match."
