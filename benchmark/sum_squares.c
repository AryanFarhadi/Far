#include "bench_util.h"

int main(void) {
  const int64_t n = bench_env_i64("BENCH_SUM_SQUARES_N", 80000000);
  const int64_t mod = 1000000007;
  int64_t sum = 0;
  int64_t t0 = bench_now_ms();
  for (int64_t i = 1; i <= n; ++i) {
    int64_t sq = i * i;
    sum = (sum + sq) % mod;
    if (sum < 0)
      sum += mod;
  }
  int64_t ms = bench_now_ms() - t0;
  bench_print_line("C", "sum_squares", ms, (uint64_t)sum);
  return 0;
}
