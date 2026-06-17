#include "bench_util.h"

int main(void) {
  const int64_t limit = bench_env_i64("BENCH_COLLATZ_LIMIT", 8000000);
  uint64_t sum = 0;
  int64_t t0 = bench_now_ms();
  for (uint64_t n = 1; n <= (uint64_t)limit; ++n) {
    uint64_t x = n;
    while (x != 1) {
      if (x & 1)
        x = x * 3 + 1;
      else
        x /= 2;
      ++sum;
    }
  }
  int64_t ms = bench_now_ms() - t0;
  bench_print_line("C", "collatz_sum", ms, sum);
  return 0;
}
