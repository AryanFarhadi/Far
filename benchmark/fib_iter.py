#!/usr/bin/env python3
import os
import time

STEPS = int(os.environ.get("BENCH_FIB_N", "30000000"))

def fib_iter() -> int:
    a, b = 0, 1
    mask = (1 << 64) - 1
    for _ in range(STEPS):
        a, b = b, (a + b) & mask
    return b

def main() -> None:
    t0 = time.perf_counter()
    result = fib_iter()
    ms = int((time.perf_counter() - t0) * 1000)
    print(f"{'Python':<8} {'fib_iter':<14} {ms:8d} ms  result={result}")

if __name__ == "__main__":
    main()
