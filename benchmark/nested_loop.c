#include "bench_util.h"

int main(void) {
  const int64_t n = bench_env_i64("BENCH_NESTED_N", 520);
  int64_t acc = 0;
  int64_t t0 = bench_now_ms();
  for (int64_t i = 0; i < n; ++i) {
    for (int64_t j = 0; j < n; ++j) {
      for (int64_t k = 0; k < n; ++k) {
        acc += (i ^ j ^ k) & 0xff;
      }
    }
  }
  int64_t ms = bench_now_ms() - t0;
  bench_print_line("C", "nested_loop", ms, (uint64_t)acc);
  return 0;
}
