# Far language tests

Each `.far` file is one self-contained program exercising a language area.

## Layout

| Pattern | Purpose |
|---------|---------|
| `types_*.far` | Feature-focused tests (constructors, control flow, generics, …) |
| `suite_100.far` | 100 numbered regression checks (types, ops, stdlib) |
| `suite_language.far` | 60 core language checks |
| `suite_builtins.far` | print/write/stringify smoke tests |
| `suite_10000.far` | Large generated suite (slow) |
| `suite_known_bugs.far` | String equality regression (now passing) |
| `comprehensive/` | Broader integration tests |
| `negative/` | Programs that must fail to compile |
| `scratch/` | Ad-hoc probes (not run by `run_tests.bat`) |

## Harness convention

Many files use:

```far
fun eq(a: i64, b: i64) -> i64 { ... }
fun run(n: i64, r: i64) -> i64 { ... }  # prints test id on failure
fun main() { ...; print(0); return 0 }
```

Success prints `0` and exits 0. Failure prints the failing test number.

## Run

Windows:

```bat
build.bat
run_tests.bat          :: 104 programs (check + run)
run_suite_10000.bat    :: 10,000 generated checks
```

Linux (x86_64 or ARM64):

```bash
chmod +x build.sh run_tests.sh
./build.sh
./run_tests.sh
```

Targets: `windows-x64`, `linux-x64`, `linux-arm64` via `FAR_TARGET` or `far compile --target <alias>`.
