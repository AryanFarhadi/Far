#!/usr/bin/env python3
import os
import time

N = int(os.environ.get("BENCH_NESTED_N", "520"))

def nested_loop() -> int:
    acc = 0
    for i in range(N):
        for j in range(N):
            for k in range(N):
                acc += (i ^ j ^ k) & 0xFF
    return acc

def main() -> None:
    t0 = time.perf_counter()
    result = nested_loop()
    ms = int((time.perf_counter() - t0) * 1000)
    print(f"{'Python':<8} {'nested_loop':<14} {ms:8d} ms  result={result}")

if __name__ == "__main__":
    main()
