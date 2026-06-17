#!/usr/bin/env python3
import os
import time

N = int(os.environ.get("BENCH_SUM_SQUARES_N", "80000000"))
MOD = 1_000_000_007

def sum_squares() -> int:
    s = 0
    for i in range(1, N + 1):
        s = (s + i * i) % MOD
    return s

def main() -> None:
    t0 = time.perf_counter()
    result = sum_squares()
    ms = int((time.perf_counter() - t0) * 1000)
    print(f"{'Python':<8} {'sum_squares':<14} {ms:8d} ms  result={result}")

if __name__ == "__main__":
    main()
