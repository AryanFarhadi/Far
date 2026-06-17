# Far

**Far** is a compiled programming language with a native C++ frontend, LLVM IR codegen, and a rich standard library. Programs compile to native executables via **Clang** (`-O2`), giving performance in the same class as C — not an interpreter.

```far
fun main() -> i64 {
  print("Hello, Far!")
  return 0
}
```

```
far run hello.far
```

---

## Features

- **Native performance** — LLVM IR → Clang `-O2` → machine code
- **Static typing** — `i64`, `f64`, `string`, structs, classes, enums, generics, traits
- **Modules & packages** — `import math` then `math.sqrt(x)` (or `import math as m`); `from … import …` is rejected for stdlib; globals limited to `print`, `input`, `len`
- **Geometry (type modules)** — `import vectors` then `vectors.distance(a, b)`, `vectors.dot(a, b)`, `vectors.cross(x, y)`; types stay in scope for constructors/fields/operators (`dvec2(1, 2)`, `v.x`, `a + b`). Same pattern for `points`, `rects`, `matrices`, `quaternions`, `colors`, `bounds`, `transforms`.
- **Concurrency** — spawn, actors, parallel blocks
- **Large stdlib** — math, I/O, JSON, networking, crypto, science, perf introspection
- **Cross-compile targets** — `windows-x64`, `linux-x64`, `linux-arm64`
- **VS Code extension** — syntax, IntelliSense, diagnostics, Run button, `.far` file icon

---

## Requirements

### Windows (x64)

| Tool | Purpose | Install |
|------|---------|---------|
| **LLVM / Clang** | Compiles Far programs to `.exe` | [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) or `winget install MartinStorsjo.LLVM-MinGW.UCRT` |
| **Clang++** | Builds the Far compiler itself | Same LLVM package (`clang++` on PATH) |
| **VS Code** (optional) | Editor support | [code.visualstudio.com](https://code.visualstudio.com/) |

### Linux (x86_64 or ARM64)

| Tool | Purpose | Install |
|------|---------|---------|
| **clang / clang++** | Compiler + Far backend | `sudo apt install clang` (Debian/Ubuntu) or distro equivalent |
| **VS Code** (optional) | Editor support | Same as above |

### Build from source (all platforms)

- **C++17** compiler (`clang++` recommended)
- **Git**
- No CMake required — `build.bat` / `build.sh` invoke Clang directly

---

## Installation

### Windows — recommended (one-click)

Installs **Far**, **Clang** (via winget when available), **PATH**, and the **VS Code extension**:

```bat
git clone https://github.com/far-lang/far.git
cd far
install\windows\install-far.bat
```

Then reload VS Code and open any `.far` file.

Uninstall:

```bat
install\windows\uninstall-far.bat
```

Details: [`install/README.md`](install/README.md)

### Windows — manual

```bat
git clone https://github.com/far-lang/far.git
cd far
build.bat
```

Ensure `clang` and `clang++` are on PATH. Optional VS Code extension:

```bat
vscode\install-extension.bat
```

### Linux — manual

```bash
git clone https://github.com/far-lang/far.git
cd far
chmod +x build.sh run_tests.sh benchmark/run_benchmark.sh
./build.sh
```

Add `./far` to your PATH or copy it to `/usr/local/bin`.

Optional: install the VS Code extension from `vscode/far-lang-*.vsix` with `code --install-extension`.

---

## Quick start

```bat
:: Windows — from repo root (runtime/ must be reachable)
far run tests\suite_builtins.far

:: Build a standalone executable
far compile tests\suite_builtins.far -o suite_builtins.exe
```

```bash
# Linux — from repo root
./far run tests/suite_builtins.far
./far compile tests/suite_builtins.far -o suite_builtins
```

Every runnable program needs a `main` entry point:

```far
fun main() -> i64 {
  print(42)
  return 0
}
```

---

## Compiler CLI

```
far run [--target <alias>] <file.far>     Build (if stale) and run
far check <file.far>                      Typecheck only
far compile [--target <alias>] <file.far> -o <output>
far emit-ir [--target <alias>] <file.far> [-o out.ll]
far fmt <file.far>                        Format source
far repl                                  REPL (minimal)
far perf                                  Show backend / target info
```

### Cross-compilation targets

| Alias | Triple |
|-------|--------|
| `windows-x64` | `x86_64-w64-windows-gnu` |
| `linux-x64` | `x86_64-unknown-linux-gnu` |
| `linux-arm64` | `aarch64-unknown-linux-gnu` |

```bat
set FAR_TARGET=linux-x64
far compile app.far -o app
```

Or: `far compile --target linux-arm64 app.far -o app`

### Environment variables

| Variable | Description |
|----------|-------------|
| `FAR_CLANG` | Path to `clang` (set automatically by Windows installer) |
| `FAR_TARGET` | Default cross-compile target alias |

---

## VS Code

Install the bundled extension for `.far` syntax, scoped IntelliSense, live errors, and a **Run** button:

```bat
vscode\install-extension.bat
```

First-time manual setup (if not using the Windows installer): **Ctrl+Shift+P** → **Far: Setup Compiler** → point to `far.exe` and `clang`.

Recommended workspace settings (also applied by the extension):

```json
{
  "far.compilerPath": "${workspaceFolder}/far.exe",
  "files.associations": { "*.far": "far" },
  "[far]": {
    "editor.tabSize": 2,
    "editor.wordBasedSuggestions": "off"
  }
}
```

More: [`vscode/README.md`](vscode/README.md)

---

## Performance

Far is **AOT-compiled**, not interpreted. The pipeline is:

```
.far  →  Far compiler (C++)  →  LLVM IR  →  clang -O2  →  native binary
```

So numeric hot loops are compiled to native code — same pipeline as C. The benchmark suite below compares real wall-clock times; gaps vs C depend on workload and codegen maturity.

### Benchmark suite

Compare **C**, **Far**, and **Python** on identical numeric workloads (same checksums across all three):

| Benchmark | Default workload | What it measures |
|-----------|------------------|------------------|
| `fib_iter` | 30M steps | Iterative Fibonacci (`u64` wrap) |
| `collatz_sum` | n = 1..8M | Total Collatz steps |
| `sum_squares` | 80M terms | Σ i² mod 1 000 000 007 |
| `nested_loop` | 520³ | Triple nested XOR accumulator |

**Requirements:** `clang` and `python` on PATH (in addition to `far.exe`).

**Windows:**

```bat
build.bat
benchmark\run_benchmark.bat
```

**Linux:**

```bash
./build.sh
chmod +x benchmark/run_benchmark.sh
./benchmark/run_benchmark.sh
```

Sample output:

```
Lang     Benchmark          ms  checksum
------------------------------------------------
C        fib_iter             16 ms  result=10731788025614611713
Far      fib_iter             31 ms  result=10731788025614611713
Python   fib_iter           1231 ms  result=10731788025614611713
...
```

### Example results (Windows x64, Clang -O2)

Measured on one desktop — **your numbers will vary** (timer resolution, CPU, thermal throttling):

| Benchmark | C | Far | Python |
|-----------|---|-----|--------|
| `fib_iter` (30M) | 0–20 ms | ~31 ms | ~1.2 s |
| `collatz_sum` (8M) | ~1.0 s | ~9 s | ~60 s |
| `sum_squares` (80M) | ~313 ms | ~422 ms | ~4.5 s |
| `nested_loop` (520³) | ~31 ms | ~187 ms | ~6.8 s |

All implementations print the same `result=` checksum. Python is slower because it runs in the **CPython interpreter**; Far and C both emit native binaries via `clang -O2`.

**Scale C and Python** with environment variables:

| Variable | Default |
|----------|---------|
| `BENCH_FIB_N` | 30000000 |
| `BENCH_COLLATZ_LIMIT` | 8000000 |
| `BENCH_SUM_SQUARES_N` | 80000000 |
| `BENCH_NESTED_N` | 520 |

Far reads workload sizes from constants inside each `.far` file in `benchmark/` — edit those to match when scaling.

Built artifacts go to `benchmark/bin/` (gitignored).

More: [`benchmark/README.md`](benchmark/README.md)

---

## Testing

```bat
:: Windows
build.bat
run_tests.bat              :: 58 feature + suite tests
```

```bash
# Linux
./build.sh
./run_tests.sh
```

Test layout: [`tests/README.md`](tests/README.md)

---

## Project layout

```
far/
├── src/              Far compiler (lexer, parser, typecheck, codegen)
├── runtime/          C runtime (far_rt.c, stdlib helpers)
├── tests/            Language regression tests (.far)
│   ├── comprehensive/
│   ├── negative/
│   └── scratch/      Ad-hoc probes (not in CI harness)
├── examples/
│   ├── program.far   Sample entry program
├── benchmark/        C / Far / Python performance suite
│   ├── *.c, *.far, *.py   Four workloads (fib, collatz, sum_squares, nested_loop)
│   ├── run_benchmark.bat / run_benchmark.sh
│   └── bin/          Built binaries (gitignored)
├── vscode/           VS Code extension + IntelliSense data
├── install/          Windows installer scripts
├── build.bat         Build far.exe (Windows)
├── build.sh          Build ./far (Linux)
├── run_tests.bat     Run test suite
└── run_benchmark.bat Run C / Far / Python benchmarks (Windows)
```

---

## Hello world

`hello.far`:

```far
fun main() -> i64 {
  print("Hello, Far!")
  return 0
}
```

```bat
far run hello.far
```

---

## License

See [`vscode/LICENSE`](vscode/LICENSE) (MIT) for the VS Code extension. Check individual files for compiler licensing.

---

## Links

| Doc | Topic |
|-----|--------|
| [`install/README.md`](install/README.md) | Windows installer |
| [`vscode/README.md`](vscode/README.md) | Editor extension |
| [`benchmark/README.md`](benchmark/README.md) | Benchmarks |
| [`tests/README.md`](tests/README.md) | Test harness |
