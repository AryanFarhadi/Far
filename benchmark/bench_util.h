#ifndef BENCH_UTIL_H
#define BENCH_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static int64_t bench_now_ms(void) { return (int64_t)GetTickCount64(); }
#else
#include <time.h>
static int64_t bench_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}
#endif

static int64_t bench_env_i64(const char* name, int64_t fallback) {
  const char* v = getenv(name);
  if (!v || !*v)
    return fallback;
  return (int64_t)strtoll(v, NULL, 10);
}

static void bench_print_line(const char* lang, const char* name, int64_t ms, uint64_t result) {
  printf("%-8s %-14s %8lld ms  result=%llu\n", lang, name, (long long)ms, (unsigned long long)result);
}

#endif
