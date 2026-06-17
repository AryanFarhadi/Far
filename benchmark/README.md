# Heavy benchmark: C vs Far vs Python

Identical numeric workloads compiled/run natively. Far is AOT-compiled to LLVM IR then `clang -O2` (same as C).

## Workloads

| Benchmark | Default size | Work |
|-----------|--------------|------|
| `fib_iter` | 30M steps | Iterative Fibonacci (`u64`) |
| `collatz_sum` | n = 1..8M | Total Collatz steps |
| `sum_squares` | 80M terms | Σ i² mod 1 000 000 007 |
| `nested_loop` | 520³ | Triple nested XOR accumulator |

All three implementations must print the same `result=` checksum.

## Run

Windows (from repo root, after `build.bat`):

```bat
benchmark\run_benchmark.bat
```

Linux:

```bash
chmod +x benchmark/run_benchmark.sh
./benchmark/run_benchmark.sh
```

## Scale

C and Python read environment variables:

| Variable | Default |
|----------|---------|
| `BENCH_FIB_N` | 30000000 |
| `BENCH_COLLATZ_LIMIT` | 8000000 |
| `BENCH_SUM_SQUARES_N` | 80000000 |
| `BENCH_NESTED_N` | 520 |

Far uses the workload constants in each `.far` file — edit those to match when scaling.

Built binaries go to `benchmark/bin/` (gitignored).
