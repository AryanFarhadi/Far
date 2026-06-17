#include "bench_util.h"

int main(void) {
  const int64_t steps = bench_env_i64("BENCH_FIB_N", 30000000);
  uint64_t a = 0, b = 1;
  int64_t t0 = bench_now_ms();
  for (int64_t i = 0; i < steps; ++i) {
    uint64_t next = a + b;
    a = b;
    b = next;
  }
  int64_t ms = bench_now_ms() - t0;
  bench_print_line("C", "fib_iter", ms, b);
  return 0;
}
