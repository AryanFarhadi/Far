#!/usr/bin/env python3
import os
import time

LIMIT = int(os.environ.get("BENCH_COLLATZ_LIMIT", "8000000"))

def collatz_sum() -> int:
    total = 0
    for n in range(1, LIMIT + 1):
        x = n
        while x != 1:
            x = 3 * x + 1 if x & 1 else x // 2
            total += 1
    return total

def main() -> None:
    t0 = time.perf_counter()
    result = collatz_sum()
    ms = int((time.perf_counter() - t0) * 1000)
    print(f"{'Python':<8} {'collatz_sum':<14} {ms:8d} ms  result={result}")

if __name__ == "__main__":
    main()
