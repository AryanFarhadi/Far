#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

SRC=(
  error lexer parser modules comptime macros types type_desc functions generics
  aggregate aggregate_codegen collections collection_codegen string_methods string_codegen object_model object_codegen
  memory memory_codegen concurrency concurrency_codegen errors errors_codegen
  pattern pattern_codegen typecheck builtins far_stdlib far_stdlib_modules
  far_science far_net far_modern far_security far_perf far_io codegen target main
)

OBJS=()
for s in "${SRC[@]}"; do
  OBJS+=("src/${s}.cpp")
done

clang++ -std=c++17 -O2 -Isrc "${OBJS[@]}" -o far

if [[ "$(uname -s)" == "Linux" ]]; then
  if [[ "$(uname -m)" == "aarch64" ]]; then
    RT_OBJ="runtime/far_rt.linux-arm64.o"
    RT_FLAGS="-fPIC --target=aarch64-unknown-linux-gnu"
  else
    RT_OBJ="runtime/far_rt.linux-x64.o"
    RT_FLAGS="-fPIC --target=x86_64-unknown-linux-gnu"
  fi
else
  echo "build.sh is intended for Linux; use build.bat on Windows"
  exit 1
fi

clang -c runtime/far_rt.c -o "$RT_OBJ" $RT_FLAGS
echo "Built ./far and $RT_OBJ"
