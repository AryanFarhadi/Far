#include <inttypes.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "far_pthread_win32.h"
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <ctype.h>

#include <stdint.h>

#include <limits.h>

#include <errno.h>

#include <math.h>

#define FAR_TAG_STRING 16
#define FAR_EX_TAG_UNWRAP_NONE ((int64_t)0x7FFFFFF0)
#define FAR_EX_TAG_UNWRAP_ERR ((int64_t)0x7FFFFFF1)
#define FAR_COLL_MAX_CAP (1 << 24)
#define FAR_ARENA_MAX_CAP ((int64_t)1 << 26)
#define FAR_STR_MAX ((size_t)64 * 1024 * 1024)
#define FAR_MALLOC_MAX ((int64_t)1 << 30)


typedef struct {

  int64_t* data;

  int64_t len;

} FarArray;

void far_panic(int64_t msg);
void far_call_reset(void);
void far_call_enter(void);
void far_call_leave(void);

int64_t far_i64_add_checked(int64_t a, int64_t b);
int64_t far_i64_mul_checked(int64_t a, int64_t b);
int64_t far_i64_mod_checked(int64_t a, int64_t b);
int64_t far_i64_sub_checked(int64_t a, int64_t b);

static int64_t far_i64_mul_or_zero(int64_t a, int64_t b) {
#if defined(__GNUC__) || defined(__clang__)
  int64_t out;
  if (__builtin_mul_overflow(a, b, &out))
    return 0;
  return out;
#else
  if (a > 0 && b > 0 && a > INT64_MAX / b)
    return 0;
  if (a > 0 && b < 0 && b < INT64_MIN / a)
    return 0;
  if (a < 0 && b > 0 && a < INT64_MIN / b)
    return 0;
  if (a < 0 && b < 0 && a < INT64_MAX / b)
    return 0;
  return a * b;
#endif
}

static int far_i64_add_ok(int64_t a, int64_t b, int64_t* out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_add_overflow(a, b, out);
#else
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
    return 0;
  *out = a + b;
  return 1;
#endif
}

static int far_alloc_size_ok(int64_t size) {
  return size > 0 && size <= FAR_MALLOC_MAX && (uint64_t)size <= (uint64_t)SIZE_MAX;
}

// Returns span length, 0 if empty, -1 if unrepresentable or unreasonably large.
static int64_t halfopen_span_checked(int64_t lo, int64_t hi) {
  if (hi <= lo)
    return 0;
  int64_t diff;
#if defined(__GNUC__) || defined(__clang__)
  if (__builtin_sub_overflow(hi, lo, &diff))
    return -1;
#else
  if (hi < lo)
    return -1;
  diff = hi - lo;
#endif
  if (diff > (1 << 24))
    return -1;
  return diff;
}

static int64_t offset_add_checked(int64_t base, int64_t delta) {
  return far_i64_add_checked(base, delta);
}

static int64_t cap_double(int64_t cap) {
  if (cap <= 0 || cap > INT64_MAX / 2)
    return -1;
  return cap * 2;
}

// Half-open range [lo, hi) with fixed step; returns 0 if empty or unrepresentable.
static int64_t range_span_len(int64_t lo, int64_t hi_exclusive, int64_t step) {
  if (step == 0)
    return 0;
  int64_t abs_step = step > 0 ? step : -step;
  if (abs_step == 0 || step == INT64_MIN)
    return 0;
  if (step > 0) {
    if (lo >= hi_exclusive)
      return 0;
    int64_t diff;
    if (__builtin_sub_overflow(hi_exclusive, lo, &diff))
      return 0;
    int64_t adj;
    if (__builtin_add_overflow(diff, abs_step - 1, &adj))
      return 0;
    return adj / abs_step;
  }
  if (lo <= hi_exclusive)
    return 0;
  int64_t diff;
  if (__builtin_sub_overflow(lo, hi_exclusive, &diff))
    return 0;
  int64_t adj;
  if (__builtin_add_overflow(diff, abs_step - 1, &adj))
    return 0;
  return adj / abs_step;
}

void far_print_i64(int64_t value) {

  printf("%" PRId64 "\n", value);

}



double far_sin(double x) { return sin(x); }
double far_cos(double x) { return cos(x); }

static int far_recip_singular(double v) {
  return v != v || fabs(v) < 1e-15;
}

double far_tan(double x) {
  double c = far_cos(x);
  if (far_recip_singular(c))
    return 0.0;
  double r = far_sin(x) / c;
  if (r != r)
    return 0.0;
  return r;
}
double far_asin(double x) {
  if (x != x || x < -1.0 || x > 1.0)
    return 0.0;
  return asin(x);
}
double far_acos(double x) {
  if (x != x || x < -1.0 || x > 1.0)
    return 0.0;
  return acos(x);
}
double far_atan(double x) { return atan(x); }
double far_atan2(double y, double x) { return atan2(y, x); }
double far_sinh(double x) { return sinh(x); }
double far_cosh(double x) { return cosh(x); }
double far_tanh(double x) { return tanh(x); }
double far_asinh(double x) { return asinh(x); }
double far_acosh(double x) {
  if (x != x || x < 1.0)
    return 0.0;
  return acosh(x);
}
double far_atanh(double x) {
  if (x != x || x <= -1.0 || x >= 1.0)
    return 0.0;
  return atanh(x);
}
double far_sqrt(double x) {
  if (x != x || x < 0.0)
    return 0.0;
  return sqrt(x);
}
double far_cbrt(double x) { return cbrt(x); }
double far_hypot(double x, double y) { return hypot(x, y); }
double far_pow(double x, double y) {
  double r = pow(x, y);
  if (r != r)
    return 0.0;
  return r;
}
double far_exp(double x) { return exp(x); }

static double far_log_domain(double x, double (*fn)(double)) {
  if (x != x || x <= 0.0)
    return 0.0;
  return fn(x);
}

double far_log(double x) { return far_log_domain(x, log); }
double far_log10(double x) { return far_log_domain(x, log10); }
double far_log2(double x) { return far_log_domain(x, log2); }
double far_exp2(double x) { return exp2(x); }
double far_log1p(double x) {
  if (x != x || x < -1.0)
    return 0.0;
  return log1p(x);
}
double far_expm1(double x) { return expm1(x); }
double far_floor(double x) { return floor(x); }
double far_ceil(double x) { return ceil(x); }
double far_round(double x) { return round(x); }
double far_trunc(double x) { return trunc(x); }
double far_fabs(double x) { return fabs(x); }
double far_fmod(double x, double y) {
  if (y == 0.0 || y != y || x != x)
    return 0.0;
  return fmod(x, y);
}
double far_copysign(double x, double y) { return copysign(x, y); }

int64_t far_ipow(int64_t base, int64_t exp) {

  if (exp < 0)

    return 0;

  int64_t result = 1;

  while (exp > 0) {

    if (exp & 1) {
      if (base != 0 && base != 1 && base != -1 && result > INT64_MAX / base)
        return 0;
      if (base == -1 && result == INT64_MIN)
        return 0;
      result *= base;
    }

    if (exp > 1 && base != 0 && base != 1 && base != -1) {
      if (base > 1 && base > INT64_MAX)
        return 0;
      if (base < -1 && base < INT64_MIN / 2)
        return 0;
      if (base > 1 && base > INT64_MAX / base)
        return 0;
      if (base < -1 && base < INT64_MIN / base)
        return 0;
    }
    base *= base;

    exp >>= 1;

  }

  return result;

}



void far_print_f32(float value) {

  printf("%g\n", (double)value);

}



void far_print_f64(double value) {

  printf("%g\n", value);

}



void far_print_str(const char* value) {

  if (value)

    printf("%s\n", value);

  else

    printf("(null)\n");

}

void far_print_char(int16_t value) {
  if (value == 0) {
    printf("\n");
    return;
  }
  unsigned char ch = (unsigned char)value;
  printf("%c\n", ch);
}



int64_t far_str_len(const char* s) {

  return s ? (int64_t)strlen(s) : 0;

}

int64_t far_str_equal(const char* a, const char* b) {
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  return (strcmp(a, b) == 0) ? 1 : 0;
}

int64_t far_str_char_at(const char* s, int64_t index) {
  if (!s)
    return 0;
  int64_t len = (int64_t)strlen(s);
  if (index < 0)
    index = len + index;
  if (index < 0 || index >= len)
    return 0;
  return (unsigned char)s[index];
}

static int is_string_tag(uint16_t tag) { return tag == FAR_TAG_STRING; }

static int far_str_n_ok(size_t n) { return n <= FAR_STR_MAX; }

static char* str_dup_n(const char* s, size_t n) {
  if (!far_str_n_ok(n))
    return NULL;
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

static char* str_dup(const char* s) {
  if (!s)
    s = "";
  size_t n = strlen(s);
  if (!far_str_n_ok(n))
    return NULL;
  return str_dup_n(s, n);
}

static int64_t coll_store_owned(uint16_t tag, int64_t value) {
  if (!is_string_tag(tag))
    return value;
  const char* s = (const char*)(intptr_t)value;
  char* dup = str_dup(s ? s : "");
  if (!dup)
    return 0;
  return (int64_t)(intptr_t)dup;
}

static int coll_try_store(uint16_t tag, int64_t value, int64_t* out) {
  int64_t stored = coll_store_owned(tag, value);
  if (is_string_tag(tag) && !stored)
    return 0;
  *out = stored;
  return 1;
}

static void coll_release_owned(uint16_t tag, int64_t value) {
  if (is_string_tag(tag) && value)
    free((void*)(intptr_t)value);
}

char* far_str_slice(const char* s, int64_t start, int64_t end) {
  if (!s)
    return str_dup("");
  int64_t len = (int64_t)strlen(s);
  if (start < 0) start = len + start;
  if (start < 0) start = 0;
  if (end < 0) end = len + end;
  if (end < 0) end = 0;
  if (end > len) end = len;
  if (end < start) end = start;
  int64_t n = end - start;
  if (!far_str_n_ok((size_t)n))
    return NULL;
  char* out = (char*)malloc((size_t)n + 1);
  if (!out)
    return NULL;
  if (n > 0)
    memcpy(out, s + (size_t)start, (size_t)n);
  out[n] = '\0';
  return out;
}



char* far_i64_to_str(int64_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%" PRId64, value);
  size_t len = strlen(buf);
  char* out = (char*)malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, buf, len + 1);
  return out;
}

char* far_u64_to_str(int64_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%" PRIu64, (uint64_t)value);
  size_t len = strlen(buf);
  char* out = (char*)malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, buf, len + 1);
  return out;
}

int64_t far_str_to_i64(const char* s) {
  if (!s || !*s)
    return 0;
  char* end = NULL;
  errno = 0;
  long long v = strtoll(s, &end, 10);
  if (errno == ERANGE)
    return 0;
  if (end == s)
    return 0;
  while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
    ++end;
  if (*end != 0)
    return 0;
  return (int64_t)v;
}

double far_str_to_f64(const char* s) {
  if (!s || !*s)
    return 0.0;
  char* end = NULL;
  errno = 0;
  double v = strtod(s, &end);
  if (errno == ERANGE)
    return 0.0;
  if (end == s)
    return 0.0;
  while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
    ++end;
  if (*end != 0)
    return 0.0;
  return v;
}

char* far_f64_to_str(double value) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%g", value);
  size_t len = strlen(buf);
  char* out = (char*)malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, buf, len + 1);
  return out;
}

char* far_bool_to_str(int64_t value) {
  const char* text = value ? "true" : "false";
  size_t len = strlen(text);
  char* out = (char*)malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, text, len + 1);
  return out;
}

char* far_char_to_str(int16_t value) {
  if (value == 0)
    far_panic(0);
  char* out = (char*)malloc(2);
  if (!out)
    return NULL;
  out[0] = (char)(unsigned char)value;
  out[1] = '\0';
  return out;
}

char* far_str_concat(const char* a, const char* b) {

  if (!a) a = "";

  if (!b) b = "";

  size_t la = strlen(a), lb = strlen(b);
  if (!far_str_n_ok(la) || !far_str_n_ok(lb) || la > FAR_STR_MAX - lb)
    far_panic(0);
  if (la > SIZE_MAX - lb - 1)
    far_panic(0);

  char* out = (char*)malloc(la + lb + 1);

  if (!out)
    far_panic(0);

  memcpy(out, a, la);

  memcpy(out + la, b, lb);

  out[la + lb] = '\0';

  return out;

}



static FarArray* array_from_handle(int64_t handle) {
  return handle ? (FarArray*)(intptr_t)handle : NULL;
}

int64_t far_tarray_new(int64_t len, int16_t tag, int64_t elem_sz);
int64_t far_tarray_len(int64_t handle);
int64_t far_tarray_get(int64_t handle, int64_t index);
void far_tarray_set(int64_t handle, int64_t index, int64_t value);

int64_t far_array_new(int64_t len) { return far_tarray_new(len, 0, 8); }

int64_t far_array_len(int64_t handle) { return far_tarray_len(handle); }

int64_t far_array_get(int64_t handle, int64_t index) { return far_tarray_get(handle, index); }

void far_array_set(int64_t handle, int64_t index, int64_t value) {
  far_tarray_set(handle, index, value);
}

/* --- Typed collections --- */

typedef struct {
  uint16_t type_tag;
  int64_t len;
  int64_t* data;
} FarTypedArray;

typedef struct {
  uint16_t type_tag;
  int64_t len;
  int64_t cap;
  int64_t* data;
} FarList;

typedef struct {
  uint16_t key_tag;
  uint16_t val_tag;
  int64_t len;
  int64_t cap;
  int64_t* keys;
  int64_t* vals;
  uint8_t* used;
} FarDict;

typedef struct {
  uint16_t key_tag;
  int64_t len;
  int64_t cap;
  int64_t* keys;
  uint8_t* used;
} FarSet;

typedef struct {
  uint16_t type_tag;
  int64_t len;
  int64_t cap;
  int64_t head;
  int64_t* data;
} FarQueue;

typedef struct {
  uint16_t type_tag;
  int64_t len;
  int64_t cap;
  int64_t* data;
} FarStack;

typedef struct FarLLNode {
  int64_t value;
  struct FarLLNode* prev;
  struct FarLLNode* next;
} FarLLNode;

typedef struct {
  uint16_t type_tag;
  int64_t len;
  FarLLNode* head;
  FarLLNode* tail;
} FarLinkedList;

typedef struct {
  int64_t start;
  int64_t end;
  int64_t step;
} FarRange;

static FarTypedArray* tarray_from_handle(int64_t h) {
  return h ? (FarTypedArray*)(intptr_t)h : NULL;
}

static int tarray_copy_range(FarTypedArray* dst, uint16_t tag, int64_t* src, int64_t start, int64_t n) {
  if (!dst || !dst->data || n < 0)
    return 0;
  for (int64_t i = 0; i < n; ++i) {
    int64_t nv;
    if (!coll_try_store(tag, src[start + i], &nv)) {
      for (int64_t j = 0; j < i; ++j)
        coll_release_owned(tag, dst->data[j]);
      memset(dst->data, 0, (size_t)i * sizeof(int64_t));
      return 0;
    }
    dst->data[i] = nv;
  }
  return 1;
}

static FarList* list_from_handle(int64_t h) { return h ? (FarList*)(intptr_t)h : NULL; }
static FarDict* dict_from_handle(int64_t h) { return h ? (FarDict*)(intptr_t)h : NULL; }
static FarSet* set_from_handle(int64_t h) { return h ? (FarSet*)(intptr_t)h : NULL; }
static FarQueue* queue_from_handle(int64_t h) { return h ? (FarQueue*)(intptr_t)h : NULL; }
static FarStack* stack_from_handle(int64_t h) { return h ? (FarStack*)(intptr_t)h : NULL; }
static FarLinkedList* llist_from_handle(int64_t h) { return h ? (FarLinkedList*)(intptr_t)h : NULL; }
static FarRange* range_from_handle(int64_t h) { return h ? (FarRange*)(intptr_t)h : NULL; }

int64_t far_tarray_new(int64_t len, int16_t tag, int64_t elem_sz) {
  (void)elem_sz;
  if (len < 0)
    return 0;
  if (len > FAR_COLL_MAX_CAP)
    return 0;
  if (len > 0 && (uint64_t)len > (uint64_t)SIZE_MAX / sizeof(int64_t))
    return 0;
  FarTypedArray* arr = (FarTypedArray*)malloc(sizeof(FarTypedArray));
  if (!arr)
    return 0;
  arr->type_tag = (uint16_t)tag;
  arr->len = len;
  arr->data = len > 0 ? (int64_t*)calloc((size_t)len, sizeof(int64_t)) : NULL;
  if (len > 0 && !arr->data) {
    free(arr);
    return 0;
  }
  return (int64_t)(intptr_t)arr;
}

int64_t far_tarray_len(int64_t handle) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr)
    return 0;
  return arr->len;
}

int64_t far_tarray_get(int64_t handle, int64_t index) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr || !arr->data || index < 0 || index >= arr->len)
    return 0;
  return arr->data[index];
}

void far_tarray_set(int64_t handle, int64_t index, int64_t value) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr || !arr->data)
    return;
  if (index < 0 || index >= arr->len)
    far_panic(0);
  int64_t nv;
  if (!coll_try_store(arr->type_tag, value, &nv))
    return;
  coll_release_owned(arr->type_tag, arr->data[index]);
  arr->data[index] = nv;
}

void far_tarray_drop(int64_t handle) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr) return;
  for (int64_t i = 0; i < arr->len; ++i)
    coll_release_owned(arr->type_tag, arr->data[i]);
  free(arr->data);
  free(arr);
}

int64_t far_tarray_contains(int64_t handle, int64_t value) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr || !arr->data)
    return 0;
  for (int64_t i = 0; i < arr->len; ++i) {
    if (arr->data[i] == value) return 1;
  }
  return 0;
}

void far_print_tarray(int64_t handle) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr) {
    printf("[]\n");
    return;
  }
  printf("[");
  for (int64_t i = 0; i < arr->len; ++i) {
    if (i) printf(", ");
    printf("%" PRId64, arr->data[i]);
  }
  printf("]\n");
}

int64_t far_list_new(int16_t tag, int64_t cap) {
  if (cap < 0)
    return 0;
  if (cap > FAR_COLL_MAX_CAP)
    return 0;
  if (cap > 0 && (uint64_t)cap > (uint64_t)SIZE_MAX / sizeof(int64_t))
    return 0;
  if (cap < 4) cap = 4;
  FarList* list = (FarList*)malloc(sizeof(FarList));
  if (!list)
    return 0;
  list->type_tag = (uint16_t)tag;
  list->len = 0;
  list->cap = cap;
  list->data = (int64_t*)calloc((size_t)cap, sizeof(int64_t));
  if (!list->data) {
    free(list);
    return 0;
  }
  return (int64_t)(intptr_t)list;
}

static int list_grow(FarList* list) {
  if (list->cap >= FAR_COLL_MAX_CAP)
    return 0;
  int64_t nc = cap_double(list->cap);
  if (nc < 0 || nc > FAR_COLL_MAX_CAP)
    nc = FAR_COLL_MAX_CAP;
  int64_t* nd = (int64_t*)realloc(list->data, (size_t)nc * sizeof(int64_t));
  if (!nd) return 0;
  memset(nd + list->cap, 0, (size_t)(nc - list->cap) * sizeof(int64_t));
  list->data = nd;
  list->cap = nc;
  return 1;
}

int64_t far_list_len(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return 0;
  return list->len;
}

static int list_push_stored(FarList* list, int64_t stored_value) {
  if (!list || !list->data)
    return 0;
  if (list->len >= list->cap && !list_grow(list))
    far_panic(0);
  list->data[list->len++] = stored_value;
  return 1;
}

static int list_push_preowned(FarList* list, int64_t owned_value) {
  if (!list || !list->data || !is_string_tag(list->type_tag)) {
    coll_release_owned(list ? list->type_tag : FAR_TAG_STRING, owned_value);
    return 0;
  }
  if (list->len >= list->cap && !list_grow(list)) {
    coll_release_owned(list->type_tag, owned_value);
    far_panic(0);
  }
  list->data[list->len++] = owned_value;
  return 1;
}

void far_list_push(int64_t handle, int64_t value) {
  FarList* list = list_from_handle(handle);
  if (!list || !list->data)
    return;
  int64_t nv;
  if (!coll_try_store(list->type_tag, value, &nv))
    return;
  if (list->len >= list->cap && !list_grow(list)) {
    coll_release_owned(list->type_tag, nv);
    far_panic(0);
  }
  list->data[list->len++] = nv;
}

int64_t far_list_pop(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return 0;
  if (list->len <= 0)
    return 0;
  int64_t v = list->data[--list->len];
  list->data[list->len] = 0;
  return v;
}

int64_t far_list_get(int64_t handle, int64_t index) {
  FarList* list = list_from_handle(handle);
  if (!list || index < 0 || index >= list->len)
    return 0;
  return list->data[index];
}

void far_list_set(int64_t handle, int64_t index, int64_t value) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return;
  if (index < 0 || index >= list->len)
    far_panic(0);
  int64_t nv;
  if (!coll_try_store(list->type_tag, value, &nv))
    return;
  coll_release_owned(list->type_tag, list->data[index]);
  list->data[index] = nv;
}

void far_list_clear(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return;
  for (int64_t i = 0; i < list->len; ++i)
    coll_release_owned(list->type_tag, list->data[i]);
  list->len = 0;
}

int64_t far_list_slice(int64_t handle, int64_t start, int64_t end) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return 0;
  if (start < 0) start = 0;
  if (end > list->len) end = list->len;
  if (end < start) end = start;
  int64_t nlen = end - start;
  int64_t nh = far_tarray_new(nlen, (int16_t)list->type_tag, 8);
  if (!nh)
    return 0;
  FarTypedArray* arr = tarray_from_handle(nh);
  if (!tarray_copy_range(arr, list->type_tag, list->data, start, nlen)) {
    far_tarray_drop(nh);
    return 0;
  }
  return nh;
}

void far_list_insert(int64_t handle, int64_t index, int64_t value) {
  FarList* list = list_from_handle(handle);
  if (!list || !list->data)
    return;
  if (index < 0)
    return;
  if (index > list->len) index = list->len;
  int64_t nv;
  if (!coll_try_store(list->type_tag, value, &nv))
    return;
  if (list->len >= list->cap && !list_grow(list)) {
    coll_release_owned(list->type_tag, nv);
    far_panic(0);
  }
  memmove(list->data + index + 1, list->data + index, (size_t)(list->len - index) * sizeof(int64_t));
  list->data[index] = nv;
  list->len++;
}

static int coll_keys_equal(uint16_t tag, int64_t a, int64_t b);

void far_list_remove_at(int64_t handle, int64_t index) {
  FarList* list = list_from_handle(handle);
  if (!list || index < 0 || index >= list->len)
    return;
  coll_release_owned(list->type_tag, list->data[index]);
  if (index + 1 < list->len)
    memmove(list->data + index, list->data + index + 1, (size_t)(list->len - index - 1) * sizeof(int64_t));
  list->len--;
  list->data[list->len] = 0;
}

int64_t far_list_remove_value(int64_t handle, int64_t value) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return 0;
  for (int64_t i = 0; i < list->len; ++i) {
    if (coll_keys_equal(list->type_tag, list->data[i], value)) {
      far_list_remove_at(handle, i);
      return 1;
    }
  }
  return 0;
}

void far_list_drop(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list) return;
  far_list_clear(handle);
  free(list->data);
  free(list);
}

static int64_t hash_key(int64_t k) {
  uint64_t x = (uint64_t)k;
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  return (int64_t)x;
}

static int coll_keys_equal(uint16_t tag, int64_t a, int64_t b) {
  if (a == b) return 1;
  if (is_string_tag(tag))
    return (int)far_str_equal((const char*)(intptr_t)a, (const char*)(intptr_t)b);
  return 0;
}

static const char* str_skip_ws(const char* s) {
  while (*s && isspace((unsigned char)*s))
    ++s;
  return s;
}

char* far_str_trim(const char* text) {
  if (!text)
    return str_dup("");
  const char* start = str_skip_ws(text);
  const char* end = text + strlen(text);
  while (end > start && isspace((unsigned char)end[-1]))
    --end;
  return str_dup_n(start, (size_t)(end - start));
}

char* far_str_tolower(const char* s) {
  if (!s)
    return str_dup("");
  size_t n = strlen(s);
  if (!far_str_n_ok(n))
    return NULL;
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  for (size_t i = 0; i < n; ++i)
    out[i] = (char)tolower((unsigned char)s[i]);
  out[n] = '\0';
  return out;
}

char* far_str_toupper(const char* s) {
  if (!s)
    return str_dup("");
  size_t n = strlen(s);
  if (!far_str_n_ok(n))
    return NULL;
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  for (size_t i = 0; i < n; ++i)
    out[i] = (char)toupper((unsigned char)s[i]);
  out[n] = '\0';
  return out;
}

int64_t far_str_contains(const char* s, const char* sub) {
  if (!s || !sub || !sub[0])
    return 0;
  return strstr(s, sub) ? 1 : 0;
}

int64_t far_str_starts_with(const char* s, const char* prefix) {
  if (!s || !prefix)
    return 0;
  size_t n = strlen(prefix);
  if (strlen(s) < n)
    return 0;
  return memcmp(s, prefix, n) == 0 ? 1 : 0;
}

int64_t far_str_ends_with(const char* s, const char* suffix) {
  if (!s || !suffix)
    return 0;
  size_t sn = strlen(s);
  size_t fn = strlen(suffix);
  if (sn < fn)
    return 0;
  return memcmp(s + sn - fn, suffix, fn) == 0 ? 1 : 0;
}

int64_t far_str_split(const char* s, const char* sep) {
  if (!s)
    s = "";
  if (!sep || !sep[0])
    sep = ",";
  int64_t list = far_list_new(FAR_TAG_STRING, 4);
  if (!list)
    return 0;
  FarList* lst = list_from_handle(list);
  size_t seplen = strlen(sep);
  const char* start = s;
  if (*s == '\0') {
    char* empty = str_dup("");
    if (!empty || !list_push_preowned(lst, (int64_t)(intptr_t)empty)) {
      free(empty);
      far_list_drop(list);
      return 0;
    }
    return list;
  }
  while (*start) {
    if (lst->len >= FAR_COLL_MAX_CAP) {
      far_list_drop(list);
      return 0;
    }
    const char* found = strstr(start, sep);
    if (!found) {
      char* part = str_dup(start);
      if (!part || !list_push_preowned(lst, (int64_t)(intptr_t)part)) {
        free(part);
        far_list_drop(list);
        return 0;
      }
      break;
    }
    char* part = str_dup_n(start, (size_t)(found - start));
    if (!part || !list_push_preowned(lst, (int64_t)(intptr_t)part)) {
      free(part);
      far_list_drop(list);
      return 0;
    }
    start = found + seplen;
    if (*start == '\0') {
      char* empty = str_dup("");
      if (!empty || !list_push_preowned(lst, (int64_t)(intptr_t)empty)) {
        free(empty);
        far_list_drop(list);
        return 0;
      }
      break;
    }
  }
  return list;
}

void far_print_list(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list) {
    printf("[]\n");
    return;
  }
  printf("[");
  for (int64_t i = 0; i < list->len; ++i) {
    if (i) printf(", ");
    if (is_string_tag(list->type_tag)) {
      const char* s = (const char*)(intptr_t)list->data[i];
      if (s)
        printf("\"%s\"", s);
      else
        printf("\"\"");
    } else {
      printf("%" PRId64, list->data[i]);
    }
  }
  printf("]\n");
}

static int64_t hash_coll_key(uint16_t tag, int64_t k) {
  if (is_string_tag(tag)) {
    const char* s = (const char*)(intptr_t)k;
    if (!s) {
    return 0;
  }
    uint64_t h = 14695981039346656037ULL;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
    return (int64_t)h;
  }
  return hash_key(k);
}

int64_t far_dict_new(int16_t key_tag, int16_t val_tag) {
  FarDict* d = (FarDict*)calloc(1, sizeof(FarDict));
  if (!d)
    return 0;
  d->key_tag = (uint16_t)key_tag;
  d->val_tag = (uint16_t)val_tag;
  d->cap = 8;
  d->keys = (int64_t*)calloc(8, sizeof(int64_t));
  d->vals = (int64_t*)calloc(8, sizeof(int64_t));
  d->used = (uint8_t*)calloc(8, sizeof(uint8_t));
  if (!d->keys || !d->vals || !d->used) {
    free(d->keys);
    free(d->vals);
    free(d->used);
    free(d);
    return 0;
  }
  return (int64_t)(intptr_t)d;
}

static int dict_resize(FarDict* d) {
  int64_t old_cap = d->cap;
  int64_t* ok = d->keys;
  int64_t* ov = d->vals;
  uint8_t* ou = d->used;
  int64_t new_cap = cap_double(d->cap);
  if (new_cap < 0 || new_cap > FAR_COLL_MAX_CAP) return 0;
  int64_t* nk = (int64_t*)calloc((size_t)new_cap, sizeof(int64_t));
  int64_t* nv = (int64_t*)calloc((size_t)new_cap, sizeof(int64_t));
  uint8_t* nu = (uint8_t*)calloc((size_t)new_cap, sizeof(uint8_t));
  if (!nk || !nv || !nu) {
    free(nk);
    free(nv);
    free(nu);
    return 0;
  }
  d->cap = new_cap;
  d->len = 0;
  d->keys = nk;
  d->vals = nv;
  d->used = nu;
  for (int64_t i = 0; i < old_cap; ++i) {
    if (ou[i] == 1) {
      int64_t h = hash_coll_key(d->key_tag, ok[i]) & (d->cap - 1);
      int64_t probe = 0;
      while (d->used[h]) {
        if (++probe >= d->cap) {
          free(nk);
          free(nv);
          free(nu);
          d->cap = old_cap;
          d->keys = ok;
          d->vals = ov;
          d->used = ou;
          d->len = 0;
          for (int64_t j = 0; j < old_cap; ++j)
            if (ou[j] == 1)
              d->len++;
          return 0;
        }
        h = (h + 1) & (d->cap - 1);
      }
      d->keys[h] = ok[i];
      d->vals[h] = ov[i];
      d->used[h] = 1;
      d->len++;
    }
  }
  free(ok);
  free(ov);
  free(ou);
  return 1;
}

int64_t far_dict_len(int64_t handle) {
  FarDict* d = dict_from_handle(handle);
  if (!d)
    return 0;
  return d->len;
}

int64_t far_dict_get(int64_t handle, int64_t key) {
  FarDict* d = dict_from_handle(handle);
  if (!d || d->cap == 0)
    return 0;
  int64_t h = hash_coll_key(d->key_tag, key) & (d->cap - 1);
  for (int64_t i = 0; i < d->cap; ++i) {
    int64_t slot = (h + i) & (d->cap - 1);
    if (d->used[slot] == 0)
      return 0;
    if (d->used[slot] == 1 && coll_keys_equal(d->key_tag, d->keys[slot], key)) return d->vals[slot];
  }
  return 0;
}

void far_dict_set(int64_t handle, int64_t key, int64_t value) {
  FarDict* d = dict_from_handle(handle);
  if (!d)
    return;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (d->len * 4 >= d->cap * 3 && !dict_resize(d))
      far_panic(0);
    int64_t h = hash_coll_key(d->key_tag, key) & (d->cap - 1);
    int64_t first_tomb = -1;
    for (int64_t i = 0; i < d->cap; ++i) {
      int64_t slot = (h + i) & (d->cap - 1);
      if (d->used[slot] == 0) {
        int64_t ins = first_tomb >= 0 ? first_tomb : slot;
        int64_t nk, nv;
        if (!coll_try_store(d->key_tag, key, &nk))
          return;
        if (!coll_try_store(d->val_tag, value, &nv)) {
          coll_release_owned(d->key_tag, nk);
          return;
        }
        d->keys[ins] = nk;
        d->vals[ins] = nv;
        d->used[ins] = 1;
        d->len++;
        return;
      }
      if (d->used[slot] == 2) {
        if (first_tomb < 0)
          first_tomb = slot;
        continue;
      }
      if (coll_keys_equal(d->key_tag, d->keys[slot], key)) {
        int64_t nv;
        if (!coll_try_store(d->val_tag, value, &nv))
          return;
        coll_release_owned(d->val_tag, d->vals[slot]);
        d->vals[slot] = nv;
        return;
      }
    }
    if (!dict_resize(d))
      far_panic(0);
  }
  far_panic(0);
}

int64_t far_dict_contains_key(int64_t handle, int64_t key) {
  FarDict* d = dict_from_handle(handle);
  if (!d)
    return 0;
  if (d->cap == 0) return 0;
  int64_t h = hash_coll_key(d->key_tag, key) & (d->cap - 1);
  for (int64_t i = 0; i < d->cap; ++i) {
    int64_t slot = (h + i) & (d->cap - 1);
    if (d->used[slot] == 0) return 0;
    if (d->used[slot] == 1 && coll_keys_equal(d->key_tag, d->keys[slot], key)) return 1;
  }
  return 0;
}

void far_dict_remove(int64_t handle, int64_t key) {
  FarDict* d = dict_from_handle(handle);
  if (!d)
    return;
  if (d->cap == 0) return;
  int64_t h = hash_coll_key(d->key_tag, key) & (d->cap - 1);
  for (int64_t i = 0; i < d->cap; ++i) {
    int64_t slot = (h + i) & (d->cap - 1);
    if (d->used[slot] == 0) return;
    if (d->used[slot] == 1 && coll_keys_equal(d->key_tag, d->keys[slot], key)) {
      coll_release_owned(d->val_tag, d->vals[slot]);
      coll_release_owned(d->key_tag, d->keys[slot]);
      d->keys[slot] = 0;
      d->vals[slot] = 0;
      d->used[slot] = 2;
      d->len--;
      return;
    }
  }
}

int64_t far_dict_keys(int64_t handle) {
  FarDict* d = dict_from_handle(handle);
  if (!d)
    return 0;
  int64_t nh = far_list_new((int16_t)d->key_tag, d->len > 0 ? d->len : 4);
  if (!nh)
    return 0;
  FarList* lst = list_from_handle(nh);
  for (int64_t i = 0; i < d->cap; ++i) {
    if (d->used[i] != 1)
      continue;
    int64_t nv;
    if (!coll_try_store(d->key_tag, d->keys[i], &nv)) {
      far_list_drop(nh);
      return 0;
    }
    if (!list_push_stored(lst, nv)) {
      coll_release_owned(d->key_tag, nv);
      far_list_drop(nh);
      return 0;
    }
  }
  return nh;
}

int64_t far_dict_values(int64_t handle) {
  FarDict* d = dict_from_handle(handle);
  if (!d)
    return 0;
  int64_t nh = far_list_new((int16_t)d->val_tag, d->len > 0 ? d->len : 4);
  if (!nh)
    return 0;
  FarList* lst = list_from_handle(nh);
  for (int64_t i = 0; i < d->cap; ++i) {
    if (d->used[i] != 1)
      continue;
    int64_t nv;
    if (!coll_try_store(d->val_tag, d->vals[i], &nv)) {
      far_list_drop(nh);
      return 0;
    }
    if (!list_push_stored(lst, nv)) {
      coll_release_owned(d->val_tag, nv);
      far_list_drop(nh);
      return 0;
    }
  }
  return nh;
}

void far_dict_drop(int64_t handle) {
  FarDict* d = dict_from_handle(handle);
  if (!d) return;
  for (int64_t i = 0; i < d->cap; ++i) {
    if (d->used[i] == 1) {
      coll_release_owned(d->key_tag, d->keys[i]);
      coll_release_owned(d->val_tag, d->vals[i]);
    }
  }
  free(d->keys);
  free(d->vals);
  free(d->used);
  free(d);
}

int64_t far_set_new(int16_t key_tag) {
  FarSet* s = (FarSet*)calloc(1, sizeof(FarSet));
  if (!s)
    return 0;
  s->key_tag = (uint16_t)key_tag;
  s->cap = 8;
  s->keys = (int64_t*)calloc(8, sizeof(int64_t));
  s->used = (uint8_t*)calloc(8, sizeof(uint8_t));
  if (!s->keys || !s->used) {
    free(s->keys);
    free(s->used);
    free(s);
    return 0;
  }
  return (int64_t)(intptr_t)s;
}

int64_t far_set_len(int64_t handle) {
  FarSet* s = set_from_handle(handle);
  if (!s)
    return 0;
  return s->len;
}

static int set_resize(FarSet* s) {
  int64_t old_cap = s->cap;
  int64_t* ok = s->keys;
  uint8_t* ou = s->used;
  int64_t new_cap = cap_double(s->cap);
  if (new_cap < 0 || new_cap > FAR_COLL_MAX_CAP) return 0;
  int64_t* nk = (int64_t*)calloc((size_t)new_cap, sizeof(int64_t));
  uint8_t* nu = (uint8_t*)calloc((size_t)new_cap, sizeof(uint8_t));
  if (!nk || !nu) {
    free(nk);
    free(nu);
    return 0;
  }
  s->cap = new_cap;
  s->len = 0;
  s->keys = nk;
  s->used = nu;
  for (int64_t i = 0; i < old_cap; ++i) {
    if (ou[i] == 1) {
      int64_t h = hash_coll_key(s->key_tag, ok[i]) & (s->cap - 1);
      int64_t probe = 0;
      while (s->used[h]) {
        if (++probe >= s->cap) {
          free(nk);
          free(nu);
          s->cap = old_cap;
          s->keys = ok;
          s->used = ou;
          s->len = 0;
          for (int64_t j = 0; j < old_cap; ++j)
            if (ou[j] == 1)
              s->len++;
          return 0;
        }
        h = (h + 1) & (s->cap - 1);
      }
      s->keys[h] = ok[i];
      s->used[h] = 1;
      s->len++;
    }
  }
  free(ok);
  free(ou);
  return 1;
}

void far_set_add(int64_t handle, int64_t key) {
  FarSet* s = set_from_handle(handle);
  if (!s)
    return;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (s->len * 4 >= s->cap * 3 && !set_resize(s))
      far_panic(0);
    int64_t h = hash_coll_key(s->key_tag, key) & (s->cap - 1);
    int64_t first_tomb = -1;
    for (int64_t i = 0; i < s->cap; ++i) {
      int64_t slot = (h + i) & (s->cap - 1);
      if (s->used[slot] == 0) {
        int64_t ins = first_tomb >= 0 ? first_tomb : slot;
        int64_t nk;
        if (!coll_try_store(s->key_tag, key, &nk))
          return;
        s->keys[ins] = nk;
        s->used[ins] = 1;
        s->len++;
        return;
      }
      if (s->used[slot] == 2) {
        if (first_tomb < 0)
          first_tomb = slot;
        continue;
      }
      if (coll_keys_equal(s->key_tag, s->keys[slot], key))
        return;
    }
    if (!set_resize(s))
      far_panic(0);
  }
  far_panic(0);
}

int64_t far_set_contains(int64_t handle, int64_t key) {
  FarSet* s = set_from_handle(handle);
  if (!s)
    return 0;
  if (s->cap == 0) return 0;
  int64_t h = hash_coll_key(s->key_tag, key) & (s->cap - 1);
  for (int64_t i = 0; i < s->cap; ++i) {
    int64_t slot = (h + i) & (s->cap - 1);
    if (s->used[slot] == 0) return 0;
    if (s->used[slot] == 1 && coll_keys_equal(s->key_tag, s->keys[slot], key)) return 1;
  }
  return 0;
}

void far_set_remove(int64_t handle, int64_t key) {
  FarSet* s = set_from_handle(handle);
  if (!s)
    return;
  if (s->cap == 0) return;
  int64_t h = hash_coll_key(s->key_tag, key) & (s->cap - 1);
  for (int64_t i = 0; i < s->cap; ++i) {
    int64_t slot = (h + i) & (s->cap - 1);
    if (s->used[slot] == 0) return;
    if (s->used[slot] == 1 && coll_keys_equal(s->key_tag, s->keys[slot], key)) {
      coll_release_owned(s->key_tag, s->keys[slot]);
      s->keys[slot] = 0;
      s->used[slot] = 2;
      s->len--;
      return;
    }
  }
}

int64_t far_set_keys(int64_t handle) {
  FarSet* s = set_from_handle(handle);
  if (!s)
    return 0;
  int64_t nh = far_list_new((int16_t)s->key_tag, s->len > 0 ? s->len : 4);
  if (!nh)
    return 0;
  FarList* lst = list_from_handle(nh);
  for (int64_t i = 0; i < s->cap; ++i) {
    if (s->used[i] != 1)
      continue;
    int64_t nv;
    if (!coll_try_store(s->key_tag, s->keys[i], &nv)) {
      far_list_drop(nh);
      return 0;
    }
    if (!list_push_stored(lst, nv)) {
      coll_release_owned(s->key_tag, nv);
      far_list_drop(nh);
      return 0;
    }
  }
  return nh;
}

void far_set_drop(int64_t handle) {
  FarSet* s = set_from_handle(handle);
  if (!s) return;
  for (int64_t i = 0; i < s->cap; ++i) {
    if (s->used[i] == 1)
      coll_release_owned(s->key_tag, s->keys[i]);
  }
  free(s->keys);
  free(s->used);
  free(s);
}

int64_t far_queue_new(int16_t tag) { return far_list_new(tag, 4); }

int64_t far_queue_len(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return 0;
  return list->len;
}

void far_queue_enqueue(int64_t handle, int64_t value) { far_list_push(handle, value); }

int64_t far_queue_dequeue(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return 0;
  if (list->len <= 0)
    return 0;
  int64_t v = list->data[0];
  if (list->len > 1)
    memmove(list->data, list->data + 1, (size_t)(list->len - 1) * sizeof(int64_t));
  list->len--;
  list->data[list->len] = 0;
  return v;
}

int64_t far_queue_peek(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list || list->len <= 0)
    return 0;
  return list->data[0];
}

int64_t far_stack_new(int16_t tag) { return far_list_new(tag, 4); }

int64_t far_stack_len(int64_t handle) { return far_list_len(handle); }

void far_stack_push(int64_t handle, int64_t value) { far_list_push(handle, value); }

int64_t far_stack_pop(int64_t handle) { return far_list_pop(handle); }

int64_t far_stack_peek(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list || list->len <= 0)
    return 0;
  return list->data[list->len - 1];
}

int64_t far_llist_new(int16_t tag) {
  FarLinkedList* ll = (FarLinkedList*)calloc(1, sizeof(FarLinkedList));
  if (!ll)
    return 0;
  ll->type_tag = (uint16_t)tag;
  return (int64_t)(intptr_t)ll;
}

int64_t far_llist_len(int64_t handle) {
  FarLinkedList* ll = llist_from_handle(handle);
  if (!ll)
    return 0;
  return ll->len;
}

void far_llist_push_front(int64_t handle, int64_t value) {
  FarLinkedList* ll = llist_from_handle(handle);
  if (!ll || ll->len >= FAR_COLL_MAX_CAP)
    return;
  FarLLNode* n = (FarLLNode*)malloc(sizeof(FarLLNode));
  if (!n)
    return;
  int64_t nv;
  if (!coll_try_store(ll->type_tag, value, &nv)) {
    free(n);
    return;
  }
  n->value = nv;
  n->prev = NULL;
  n->next = ll->head;
  if (ll->head) ll->head->prev = n;
  ll->head = n;
  if (!ll->tail) ll->tail = n;
  ll->len++;
}

void far_llist_push_back(int64_t handle, int64_t value) {
  FarLinkedList* ll = llist_from_handle(handle);
  if (!ll || ll->len >= FAR_COLL_MAX_CAP)
    return;
  FarLLNode* n = (FarLLNode*)malloc(sizeof(FarLLNode));
  if (!n)
    return;
  int64_t nv;
  if (!coll_try_store(ll->type_tag, value, &nv)) {
    free(n);
    return;
  }
  n->value = nv;
  n->next = NULL;
  n->prev = ll->tail;
  if (ll->tail) ll->tail->next = n;
  ll->tail = n;
  if (!ll->head) ll->head = n;
  ll->len++;
}

int64_t far_llist_pop_front(int64_t handle) {
  FarLinkedList* ll = llist_from_handle(handle);
  if (!ll)
    return 0;
  if (!ll->head)
    return 0;
  FarLLNode* n = ll->head;
  int64_t v = n->value;
  ll->head = n->next;
  if (ll->head) ll->head->prev = NULL;
  else ll->tail = NULL;
  free(n);
  ll->len--;
  return v;
}

int64_t far_llist_pop_back(int64_t handle) {
  FarLinkedList* ll = llist_from_handle(handle);
  if (!ll)
    return 0;
  if (!ll->tail)
    return 0;
  FarLLNode* n = ll->tail;
  int64_t v = n->value;
  ll->tail = n->prev;
  if (ll->tail) ll->tail->next = NULL;
  else ll->head = NULL;
  free(n);
  ll->len--;
  return v;
}

void far_llist_drop(int64_t handle) {
  FarLinkedList* ll = llist_from_handle(handle);
  if (!ll) return;
  while (ll->head) {
    FarLLNode* n = ll->head;
    coll_release_owned(ll->type_tag, n->value);
    ll->head = n->next;
    free(n);
  }
  ll->tail = NULL;
  ll->len = 0;
  free(ll);
}

int64_t far_slice_new(int64_t src, int64_t start, int64_t end, int16_t tag) {
  FarTypedArray* arr = tarray_from_handle(src);
  if (!arr)
    return 0;
  if (start < 0) start = arr->len + start;
  if (start < 0) start = 0;
  if (end < 0) end = arr->len + end;
  if (end < 0) end = 0;
  if (end > arr->len) end = arr->len;
  if (end < start) end = start;
  int64_t nlen = end - start;
  int64_t nh = far_tarray_new(nlen, tag, 8);
  if (!nh)
    return 0;
  FarTypedArray* dst = tarray_from_handle(nh);
  if (!tarray_copy_range(dst, (uint16_t)tag, arr->data, start, nlen)) {
    far_tarray_drop(nh);
    return 0;
  }
  return nh;
}

void far_range_drop(int64_t handle) {
  if (handle)
    free((void*)(intptr_t)handle);
}

int64_t far_range_new(int64_t start, int64_t end, int64_t step) {
  if (step == 0) step = 1;
  FarRange* r = (FarRange*)malloc(sizeof(FarRange));
  if (!r)
    return 0;
  r->start = start;
  r->end = end;
  r->step = step;
  return (int64_t)(intptr_t)r;
}

int64_t far_range_len(int64_t handle) {
  FarRange* r = range_from_handle(handle);
  if (!r)
    return 0;
  if (r->step == 0) return 0;
  return range_span_len(r->start, r->end, r->step);
}

int64_t far_range_contains(int64_t handle, int64_t value) {
  FarRange* r = range_from_handle(handle);
  if (!r)
    return 0;
  if (r->step == 0) return 0;
  if (r->step > 0) {
    if (value < r->start || value >= r->end) return 0;
    int64_t off;
    if (__builtin_sub_overflow(value, r->start, &off))
      return 0;
    return (off % r->step) == 0;
  }
  if (value > r->start || value <= r->end) return 0;
  int64_t abs_step = r->step == INT64_MIN ? 0 : -r->step;
  if (abs_step == 0) return 0;
  int64_t off;
  if (__builtin_sub_overflow(r->start, value, &off))
    return 0;
  return (off % abs_step) == 0;
}



typedef int64_t (*FarFn0)(void);

typedef int64_t (*FarFn1)(int64_t);

typedef int64_t (*FarFn2)(int64_t, int64_t);

typedef int64_t (*FarFn3)(int64_t, int64_t, int64_t);

typedef int64_t (*FarFn4)(int64_t, int64_t, int64_t, int64_t);



typedef struct {

  void* fn;

  int64_t args[4];

  int nargs;

  int64_t result;

} SpawnCtx;



typedef struct {

  pthread_t thread;

  SpawnCtx* ctx;

  int joined;

  int live;

  int join_done;

  int64_t join_result;

} FarThreadSlot;



static FarThreadSlot* g_threads = NULL;

static size_t g_thread_cap = 0;

static pthread_mutex_t g_spawn_mu;
static pthread_cond_t g_join_cv;

#ifdef _WIN32
static INIT_ONCE g_spawn_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK far_spawn_mu_init_once(PINIT_ONCE once, PVOID param, PVOID* ctx) {
  (void)once;
  (void)param;
  (void)ctx;
  pthread_mutex_init(&g_spawn_mu, NULL);
  pthread_cond_init(&g_join_cv, NULL);
  return TRUE;
}
static void far_spawn_mu_ensure(void) {
  InitOnceExecuteOnce(&g_spawn_once, far_spawn_mu_init_once, NULL, NULL);
}
#else
static pthread_once_t g_spawn_once = PTHREAD_ONCE_INIT;
static void far_spawn_mu_init_fn(void) {
  pthread_mutex_init(&g_spawn_mu, NULL);
  pthread_cond_init(&g_join_cv, NULL);
}
static void far_spawn_mu_ensure(void) { pthread_once(&g_spawn_once, far_spawn_mu_init_fn); }
#endif

#define FAR_CHAN_MAX_CAP (1 << 20)
#define FAR_POOL_MAX_ELEM ((size_t)1 << 24)
#define FAR_MAX_THREADS 4096
#define FAR_MAX_ACTORS 4096



static void* thread_trampoline(void* arg) {

  SpawnCtx* ctx = (SpawnCtx*)arg;
  far_call_reset();

  switch (ctx->nargs) {

    case 0: ctx->result = ((FarFn0)ctx->fn)(); break;

    case 1: ctx->result = ((FarFn1)ctx->fn)(ctx->args[0]); break;

    case 2: ctx->result = ((FarFn2)ctx->fn)(ctx->args[0], ctx->args[1]); break;

    case 3: ctx->result = ((FarFn3)ctx->fn)(ctx->args[0], ctx->args[1], ctx->args[2]); break;

    case 4: ctx->result = ((FarFn4)ctx->fn)(ctx->args[0], ctx->args[1], ctx->args[2], ctx->args[3]); break;

    default:
      ctx->result = 0;
      break;

  }

  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  for (size_t i = 0; i < g_thread_cap; ++i) {
    if (g_threads[i].ctx == ctx) {
      g_threads[i].join_result = ctx->result;
      g_threads[i].ctx = NULL;
      break;
    }
  }
  pthread_mutex_unlock(&g_spawn_mu);
  free(ctx);
  return NULL;

}



static int ensure_thread_cap(size_t need) {

  if (need > FAR_MAX_THREADS) return 0;

  if (need <= g_thread_cap) return 1;

  size_t new_cap = g_thread_cap == 0 ? 8 : g_thread_cap * 2;

  while (new_cap < need) {
    if (new_cap > SIZE_MAX / 2)
      return 0;
    new_cap *= 2;
  }

  if (new_cap > SIZE_MAX / sizeof(FarThreadSlot))
    return 0;

  FarThreadSlot* nt = (FarThreadSlot*)realloc(g_threads, new_cap * sizeof(FarThreadSlot));

  if (!nt) return 0;

  memset(nt + g_thread_cap, 0, (new_cap - g_thread_cap) * sizeof(FarThreadSlot));

  g_threads = nt;

  g_thread_cap = new_cap;

  return 1;

}



int64_t far_thread_count(void) {

#ifdef _WIN32

  SYSTEM_INFO info;

  GetSystemInfo(&info);

  return (int64_t)info.dwNumberOfProcessors;

#else

  long n = sysconf(_SC_NPROCESSORS_ONLN);

  return n > 0 ? (int64_t)n : 1;

#endif

}



int64_t far_spawn(void* fn, int64_t nargs, int64_t a0, int64_t a1, int64_t a2, int64_t a3) {

  if (!fn || nargs < 0 || nargs > 4)
    return -1;

  SpawnCtx* ctx = (SpawnCtx*)malloc(sizeof(SpawnCtx));

  if (!ctx)
    return -1;

  ctx->fn = fn;

  ctx->nargs = (int)nargs;

  ctx->args[0] = a0; ctx->args[1] = a1; ctx->args[2] = a2; ctx->args[3] = a3;

  ctx->result = 0;

  size_t handle = 0;

  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  for (; handle < g_thread_cap; ++handle) {

    if (g_threads[handle].ctx == NULL && !g_threads[handle].live) break;

  }

  if (!ensure_thread_cap(handle + 1)) {

    pthread_mutex_unlock(&g_spawn_mu);
    free(ctx);

    return -1;

  }

  g_threads[handle].ctx = ctx;
  g_threads[handle].joined = 0;
  g_threads[handle].join_result = 0;
  g_threads[handle].join_done = 0;
  g_threads[handle].live = 1;

  if (pthread_create(&g_threads[handle].thread, NULL, thread_trampoline, ctx) != 0) {

    g_threads[handle].ctx = NULL;
    g_threads[handle].live = 0;
    pthread_mutex_unlock(&g_spawn_mu);
    free(ctx);

    return -1;

  }

  pthread_mutex_unlock(&g_spawn_mu);

  return (int64_t)handle;

}



int64_t far_join(int64_t handle) {

  if (handle < 0 || (size_t)handle >= g_thread_cap)
    return -1;

  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  FarThreadSlot* slot = &g_threads[(size_t)handle];

  if (slot->join_done) {
    int64_t cached = slot->join_result;
    pthread_mutex_unlock(&g_spawn_mu);
    return cached;
  }

  if (slot->joined) {
    while (!slot->join_done)
      pthread_cond_wait(&g_join_cv, &g_spawn_mu);
    int64_t cached = slot->join_result;
    pthread_mutex_unlock(&g_spawn_mu);
    return cached;
  }

  if (slot->ctx == NULL) {
    if (!slot->live) {
      pthread_mutex_unlock(&g_spawn_mu);
      return -1;
    }
    slot->join_done = 1;
    int64_t result = slot->join_result;
    slot->live = 0;
    pthread_mutex_unlock(&g_spawn_mu);
    return result;
  }

  slot->joined = 1;
  pthread_t thread = slot->thread;
  pthread_mutex_unlock(&g_spawn_mu);

  pthread_join(thread, NULL);

  int64_t result = 0;

  pthread_mutex_lock(&g_spawn_mu);
  result = slot->join_result;
  slot->ctx = NULL;
  slot->join_result = result;
  slot->join_done = 1;
  slot->live = 0;
  slot->joined = 0;
  pthread_cond_broadcast(&g_join_cv);
  pthread_mutex_unlock(&g_spawn_mu);

  return result;

}

int64_t far_await(int64_t handle) { return far_join(handle); }

int64_t far_gen_next(int64_t gen_ptr) {
  (void)gen_ptr;
  return 0;
}

#define FAR_CAP_FORM_STRING 1000

static int64_t far_dict_dup(int64_t handle) {
  FarDict* d = dict_from_handle(handle);
  if (!d)
    return 0;
  int64_t nh = far_dict_new(d->key_tag, d->val_tag);
  if (!nh)
    return 0;
  for (int64_t i = 0; i < d->cap; ++i) {
    if (d->used[i] == 1)
      far_dict_set(nh, d->keys[i], d->vals[i]);
  }
  return nh;
}

static int64_t far_set_dup(int64_t handle) {
  FarSet* s = set_from_handle(handle);
  if (!s)
    return 0;
  int64_t nh = far_set_new(s->key_tag);
  if (!nh)
    return 0;
  for (int64_t i = 0; i < s->cap; ++i) {
    if (s->used[i] == 1)
      far_set_add(nh, s->keys[i]);
  }
  return nh;
}

static int64_t far_llist_dup(int64_t handle) {
  FarLinkedList* ll = llist_from_handle(handle);
  if (!ll)
    return 0;
  int64_t nh = far_llist_new((int16_t)ll->type_tag);
  if (!nh)
    return 0;
  for (FarLLNode* n = ll->head; n; n = n->next)
    far_llist_push_back(nh, n->value);
  return nh;
}

static int64_t far_list_dup(int64_t handle) {
  FarList* list = list_from_handle(handle);
  if (!list)
    return 0;
  int64_t cap = list->len > 4 ? list->len : 4;
  int64_t nh = far_list_new((int16_t)list->type_tag, cap);
  if (!nh)
    return 0;
  FarList* dst = list_from_handle(nh);
  for (int64_t i = 0; i < list->len; ++i) {
    int64_t nv;
    if (!coll_try_store(list->type_tag, list->data[i], &nv)) {
      far_list_drop(nh);
      return 0;
    }
    if (!list_push_stored(dst, nv)) {
      coll_release_owned(list->type_tag, nv);
      far_list_drop(nh);
      return 0;
    }
  }
  return nh;
}

static int64_t far_range_dup(int64_t handle) {
  FarRange* r = range_from_handle(handle);
  if (!r)
    return 0;
  return far_range_new(r->start, r->end, r->step);
}

int64_t far_owned_dup(int64_t form, int64_t value) {
  if (!value) return 0;
  switch ((int)form) {
    case FAR_CAP_FORM_STRING: {
      const char* s = (const char*)(intptr_t)value;
      return (int64_t)(intptr_t)str_dup(s ? s : "");
    }
    case 1: { /* Array */
      FarTypedArray* arr = tarray_from_handle(value);
      if (!arr)
        return 0;
      int64_t nh = far_tarray_new(arr->len, (int16_t)arr->type_tag, 8);
      if (!nh)
        return 0;
      FarTypedArray* dst = tarray_from_handle(nh);
      if (!tarray_copy_range(dst, arr->type_tag, arr->data, 0, arr->len)) {
        far_tarray_drop(nh);
        return 0;
      }
      return nh;
    }
    case 2:  /* List */
    case 6:  /* Queue */
    case 7: { /* Stack */
      return far_list_dup(value);
    }
    case 4: return far_dict_dup(value);
    case 5: return far_set_dup(value);
    case 8: return far_llist_dup(value);
    case 10: /* Slice */
    case 11: { /* Tuple */
      FarTypedArray* arr = tarray_from_handle(value);
      if (!arr)
        return 0;
      int64_t nh = far_tarray_new(arr->len, (int16_t)arr->type_tag, 8);
      if (!nh)
        return 0;
      FarTypedArray* dst = tarray_from_handle(nh);
      if (!tarray_copy_range(dst, arr->type_tag, arr->data, 0, arr->len)) {
        far_tarray_drop(nh);
        return 0;
      }
      return nh;
    }
    case 12: return far_range_dup(value);
    default: return value;
  }
}

void far_owned_drop(int64_t form, int64_t value) {
  if (!value) return;
  switch ((int)form) {
    case FAR_CAP_FORM_STRING:
      free((void*)(intptr_t)value);
      break;
    case 1:
    case 10:
    case 11:
      far_tarray_drop(value);
      break;
    case 2:
    case 6:
    case 7:
      far_list_drop(value);
      break;
    case 4:
      far_dict_drop(value);
      break;
    case 5:
      far_set_drop(value);
      break;
    case 8:
      far_llist_drop(value);
      break;
    case 12:
      far_range_drop(value);
      break;
    default:
      break;
  }
}

char* far_str_copy(const char* s) { return str_dup(s ? s : ""); }

typedef struct FarClosure {
  void* fn;
  int ncaps;
  int64_t cap_forms[4];
  int64_t caps[4];
} FarClosure;

typedef int64_t (*FarFn1)(int64_t);
typedef int64_t (*FarFn2)(int64_t, int64_t);
typedef int64_t (*FarFn3)(int64_t, int64_t, int64_t);
typedef int64_t (*FarFn4)(int64_t, int64_t, int64_t, int64_t);
typedef int64_t (*FarFn5)(int64_t, int64_t, int64_t, int64_t, int64_t);

int64_t far_closure_new(void* fn, int64_t ncaps, int64_t f0, int64_t c0, int64_t f1, int64_t c1,
                        int64_t f2, int64_t c2, int64_t f3, int64_t c3) {
  if (!fn || ncaps < 0 || ncaps > 4)
    return 0;
  FarClosure* c = (FarClosure*)malloc(sizeof(FarClosure));
  if (!c)
    return 0;
  c->fn = fn;
  c->ncaps = (int)ncaps;
  c->cap_forms[0] = f0;
  c->caps[0] = c0;
  c->cap_forms[1] = f1;
  c->caps[1] = c1;
  c->cap_forms[2] = f2;
  c->caps[2] = c2;
  c->cap_forms[3] = f3;
  c->caps[3] = c3;
  return (int64_t)(intptr_t)c;
}

void far_closure_drop(int64_t handle) {
  FarClosure* c = (FarClosure*)(intptr_t)handle;
  if (!c)
    return;
  for (int i = 0; i < c->ncaps; ++i)
    far_owned_drop(c->cap_forms[i], c->caps[i]);
  free(c);
}

int64_t far_closure_call(int64_t handle, int64_t arg) {
  FarClosure* c = (FarClosure*)(intptr_t)handle;
  if (!c)
    return 0;
  far_call_enter();
  int64_t r = 0;
  switch (c->ncaps) {
    case 0:
      r = ((FarFn1)c->fn)(arg);
      break;
    case 1:
      r = ((FarFn2)c->fn)(c->caps[0], arg);
      break;
    case 2:
      r = ((FarFn3)c->fn)(c->caps[0], c->caps[1], arg);
      break;
    case 3:
      r = ((FarFn4)c->fn)(c->caps[0], c->caps[1], c->caps[2], arg);
      break;
    case 4:
      r = ((FarFn5)c->fn)(c->caps[0], c->caps[1], c->caps[2], c->caps[3], arg);
      break;
    default:
      r = 0;
      break;
  }
  far_call_leave();
  return r;
}

int64_t far_closure_call0(int64_t handle) {
  FarClosure* c = (FarClosure*)(intptr_t)handle;
  if (!c)
    return 0;
  far_call_enter();
  int64_t r = 0;
  switch (c->ncaps) {
    case 0:
      r = ((FarFn0)c->fn)();
      break;
    case 1:
      r = ((FarFn1)c->fn)(c->caps[0]);
      break;
    case 2:
      r = ((FarFn2)c->fn)(c->caps[0], c->caps[1]);
      break;
    case 3:
      r = ((FarFn3)c->fn)(c->caps[0], c->caps[1], c->caps[2]);
      break;
    case 4:
      r = ((FarFn4)c->fn)(c->caps[0], c->caps[1], c->caps[2], c->caps[3]);
      break;
    default:
      r = 0;
      break;
  }
  far_call_leave();
  return r;
}

int64_t far_parallel(void* fn, int64_t count) {

  if (count <= 0) return 0;

  if (!fn)
    return -1;

  if (count > (1 << 20))
    return -1;

  if (count == 1) return ((FarFn1)fn)(0);

  int64_t total = 0;

  if ((uint64_t)count > (uint64_t)SIZE_MAX / sizeof(int64_t))
    return -1;

  int64_t* handles = (int64_t*)malloc((size_t)count * sizeof(int64_t));

  if (!handles)
    return -1;

  int64_t spawned = 0;
  for (int64_t i = 0; i < count; ++i) {
    handles[i] = far_spawn(fn, 1, i, 0, 0, 0);
    if (handles[i] < 0) {
      for (int64_t j = 0; j < spawned; ++j)
        far_join(handles[j]);
      free(handles);
      return -1;
    }
    spawned++;
  }

  for (int64_t i = 0; i < count; ++i) {
    int64_t jr = far_join(handles[i]);
    if (!far_i64_add_ok(total, jr, &total)) {
      for (int64_t j = i + 1; j < count; ++j)
        (void)far_join(handles[j]);
      free(handles);
      return -1;
    }
  }

  free(handles);

  return total;

}

typedef struct { float x, y; } FarFVec2;
typedef struct { float x, y, z; } FarFVec3;
typedef struct { float x, y, z, w; } FarFVec4;
typedef struct { double x, y; } FarDVec2;
typedef struct { double x, y, z; } FarDVec3;
typedef struct { double x, y, z, w; } FarDVec4;
typedef FarFVec2 FarFPoint;
typedef FarDVec2 FarDPoint;
typedef struct { float xmin, ymin, xmax, ymax; } FarFRect;
typedef struct { double xmin, ymin, xmax, ymax; } FarDRect;

#define FAR_VEC2_DOT(T, PFX) \
  double PFX##_dot(const T* a, const T* b) { return (double)a->x * b->x + (double)a->y * b->y; }
#define FAR_VEC2_LEN2(T, PFX) \
  double PFX##_length2(const T* v) { return PFX##_dot(v, v); }
#define FAR_VEC2_LEN(T, PFX) \
  double PFX##_length(const T* v) { return sqrt(PFX##_length2(v)); }
#define FAR_VEC2_DIST2(T, PFX) \
  double PFX##_distance2(const T* a, const T* b) { \
    double dx = (double)a->x - b->x; \
    double dy = (double)a->y - b->y; \
    return dx * dx + dy * dy; \
  }
#define FAR_VEC2_DIST(T, PFX) \
  double PFX##_distance(const T* a, const T* b) { return sqrt(PFX##_distance2(a, b)); }
#define FAR_VEC2_NORM(T, PFX) \
  void PFX##_normalize(const T* v, T* out) { \
    double l = PFX##_length(v); \
    if (l == 0.0) { *out = *v; return; } \
    out->x = (typeof(out->x))(v->x / l); \
    out->y = (typeof(out->y))(v->y / l); \
  }
#define FAR_VEC2_PRINT(T, PFX, FMT) \
  void far_print_##PFX(const T* v) { printf("(" FMT ", " FMT ")\n", v->x, v->y); }

FAR_VEC2_DOT(FarFVec2, far_fvec2)
FAR_VEC2_LEN2(FarFVec2, far_fvec2)
FAR_VEC2_LEN(FarFVec2, far_fvec2)
FAR_VEC2_DIST2(FarFVec2, far_fvec2)
FAR_VEC2_DIST(FarFVec2, far_fvec2)
FAR_VEC2_NORM(FarFVec2, far_fvec2)
FAR_VEC2_PRINT(FarFVec2, FVec2, "%g")

FAR_VEC2_DOT(FarDVec2, far_dvec2)
FAR_VEC2_LEN2(FarDVec2, far_dvec2)
FAR_VEC2_LEN(FarDVec2, far_dvec2)
FAR_VEC2_DIST2(FarDVec2, far_dvec2)
FAR_VEC2_DIST(FarDVec2, far_dvec2)
FAR_VEC2_NORM(FarDVec2, far_dvec2)
FAR_VEC2_PRINT(FarDVec2, DVec2, "%g")

double far_fpoint_distance_to(const FarFPoint* a, const FarFPoint* b) { return far_fvec2_distance(a, b); }
double far_dpoint_distance_to(const FarDPoint* a, const FarDPoint* b) { return far_dvec2_distance(a, b); }
double far_fpoint_distance2(const FarFPoint* a, const FarFPoint* b) { return far_fvec2_distance2(a, b); }
double far_dpoint_distance2(const FarDPoint* a, const FarDPoint* b) { return far_dvec2_distance2(a, b); }
void far_fpoint_normalize(const FarFPoint* v, FarFPoint* out) { far_fvec2_normalize(v, out); }
void far_dpoint_normalize(const FarDPoint* v, FarDPoint* out) { far_dvec2_normalize(v, out); }
double far_fpoint_dot(const FarFPoint* a, const FarFPoint* b) { return far_fvec2_dot(a, b); }
double far_dpoint_dot(const FarDPoint* a, const FarDPoint* b) { return far_dvec2_dot(a, b); }
double far_fpoint_length(const FarFPoint* v) { return far_fvec2_length(v); }
double far_dpoint_length(const FarDPoint* v) { return far_dvec2_length(v); }
double far_fpoint_length2(const FarFPoint* v) { return far_fvec2_length2(v); }
double far_dpoint_length2(const FarDPoint* v) { return far_dvec2_length2(v); }
void far_print_FPoint(const FarFPoint* v) { far_print_FVec2(v); }
void far_print_DPoint(const FarDPoint* v) { far_print_DVec2(v); }

#define FAR_VEC3_DOT(T, PFX) \
  double PFX##_dot(const T* a, const T* b) { \
    return (double)a->x * b->x + (double)a->y * b->y + (double)a->z * b->z; \
  }
#define FAR_VEC3_CROSS(T, PFX) \
  void PFX##_cross(const T* a, const T* b, T* out) { \
    out->x = (typeof(out->x))(a->y * b->z - a->z * b->y); \
    out->y = (typeof(out->y))(a->z * b->x - a->x * b->z); \
    out->z = (typeof(out->z))(a->x * b->y - a->y * b->x); \
  }
#define FAR_VEC3_LEN2(T, PFX) \
  double PFX##_length2(const T* v) { return PFX##_dot(v, v); }
#define FAR_VEC3_LEN(T, PFX) \
  double PFX##_length(const T* v) { return sqrt(PFX##_length2(v)); }
#define FAR_VEC3_NORM(T, PFX) \
  void PFX##_normalize(const T* v, T* out) { \
    double l = PFX##_length(v); \
    if (l == 0.0) { *out = *v; return; } \
    out->x = (typeof(out->x))(v->x / l); \
    out->y = (typeof(out->y))(v->y / l); \
    out->z = (typeof(out->z))(v->z / l); \
  }
#define FAR_VEC3_PRINT(T, PFX, FMT) \
  void far_print_##PFX(const T* v) { printf("(" FMT ", " FMT ", " FMT ")\n", v->x, v->y, v->z); }

#define FAR_VEC3_DIST2(T, PFX) \
  double PFX##_distance2(const T* a, const T* b) { \
    double dx = (double)a->x - b->x; \
    double dy = (double)a->y - b->y; \
    double dz = (double)a->z - b->z; \
    return dx * dx + dy * dy + dz * dz; \
  }
#define FAR_VEC3_DIST(T, PFX) \
  double PFX##_distance(const T* a, const T* b) { return sqrt(PFX##_distance2(a, b)); }

FAR_VEC3_DOT(FarFVec3, far_fvec3)
FAR_VEC3_CROSS(FarFVec3, far_fvec3)
FAR_VEC3_LEN2(FarFVec3, far_fvec3)
FAR_VEC3_LEN(FarFVec3, far_fvec3)
FAR_VEC3_DIST2(FarFVec3, far_fvec3)
FAR_VEC3_DIST(FarFVec3, far_fvec3)
FAR_VEC3_NORM(FarFVec3, far_fvec3)
FAR_VEC3_PRINT(FarFVec3, FVec3, "%g")

FAR_VEC3_DOT(FarDVec3, far_dvec3)
FAR_VEC3_CROSS(FarDVec3, far_dvec3)
FAR_VEC3_LEN2(FarDVec3, far_dvec3)
FAR_VEC3_LEN(FarDVec3, far_dvec3)
FAR_VEC3_DIST2(FarDVec3, far_dvec3)
FAR_VEC3_DIST(FarDVec3, far_dvec3)
FAR_VEC3_NORM(FarDVec3, far_dvec3)
FAR_VEC3_PRINT(FarDVec3, DVec3, "%g")

#define FAR_VEC4_DOT(T, PFX) \
  double PFX##_dot(const T* a, const T* b) { \
    return (double)a->x * b->x + (double)a->y * b->y + (double)a->z * b->z + (double)a->w * b->w; \
  }
#define FAR_VEC4_LEN2(T, PFX) \
  double PFX##_length2(const T* v) { return PFX##_dot(v, v); }
#define FAR_VEC4_LEN(T, PFX) \
  double PFX##_length(const T* v) { return sqrt(PFX##_length2(v)); }
#define FAR_VEC4_NORM(T, PFX) \
  void PFX##_normalize(const T* v, T* out) { \
    double l = PFX##_length(v); \
    if (l == 0.0) { *out = *v; return; } \
    out->x = (typeof(out->x))(v->x / l); \
    out->y = (typeof(out->y))(v->y / l); \
    out->z = (typeof(out->z))(v->z / l); \
    out->w = (typeof(out->w))(v->w / l); \
  }
#define FAR_VEC4_PRINT(T, PFX, FMT) \
  void far_print_##PFX(const T* v) { \
    printf("(" FMT ", " FMT ", " FMT ", " FMT ")\n", v->x, v->y, v->z, v->w); \
  }

#define FAR_VEC4_DIST2(T, PFX) \
  double PFX##_distance2(const T* a, const T* b) { \
    double dx = (double)a->x - b->x; \
    double dy = (double)a->y - b->y; \
    double dz = (double)a->z - b->z; \
    double dw = (double)a->w - b->w; \
    return dx * dx + dy * dy + dz * dz + dw * dw; \
  }
#define FAR_VEC4_DIST(T, PFX) \
  double PFX##_distance(const T* a, const T* b) { return sqrt(PFX##_distance2(a, b)); }

FAR_VEC4_DOT(FarFVec4, far_fvec4)
FAR_VEC4_LEN2(FarFVec4, far_fvec4)
FAR_VEC4_LEN(FarFVec4, far_fvec4)
FAR_VEC4_DIST2(FarFVec4, far_fvec4)
FAR_VEC4_DIST(FarFVec4, far_fvec4)
FAR_VEC4_NORM(FarFVec4, far_fvec4)
FAR_VEC4_PRINT(FarFVec4, FVec4, "%g")

FAR_VEC4_DOT(FarDVec4, far_dvec4)
FAR_VEC4_LEN2(FarDVec4, far_dvec4)
FAR_VEC4_LEN(FarDVec4, far_dvec4)
FAR_VEC4_DIST2(FarDVec4, far_dvec4)
FAR_VEC4_DIST(FarDVec4, far_dvec4)
FAR_VEC4_NORM(FarDVec4, far_dvec4)
FAR_VEC4_PRINT(FarDVec4, DVec4, "%g")

int64_t far_frect_contains(const FarFRect* r, const FarFPoint* p) {
  return p->x >= r->xmin && p->x <= r->xmax && p->y >= r->ymin && p->y <= r->ymax;
}
int64_t far_drect_contains(const FarDRect* r, const FarDPoint* p) {
  return p->x >= r->xmin && p->x <= r->xmax && p->y >= r->ymin && p->y <= r->ymax;
}
int64_t far_frect_intersects(const FarFRect* a, const FarFRect* b) {
  return !(a->xmax < b->xmin || b->xmax < a->xmin || a->ymax < b->ymin || b->ymax < a->ymin);
}
int64_t far_drect_intersects(const FarDRect* a, const FarDRect* b) {
  return !(a->xmax < b->xmin || b->xmax < a->xmin || a->ymax < b->ymin || b->ymax < a->ymin);
}
void far_frect_center(const FarFRect* r, FarFPoint* out) {
  out->x = (r->xmin + r->xmax) * 0.5f;
  out->y = (r->ymin + r->ymax) * 0.5f;
}
void far_drect_center(const FarDRect* r, FarDPoint* out) {
  out->x = (r->xmin + r->xmax) * 0.5;
  out->y = (r->ymin + r->ymax) * 0.5;
}
void far_frect_expand(const FarFRect* r, float m, FarFRect* out) {
  out->xmin = r->xmin - m;
  out->ymin = r->ymin - m;
  out->xmax = r->xmax + m;
  out->ymax = r->ymax + m;
}
void far_drect_expand(const FarDRect* r, double m, FarDRect* out) {
  out->xmin = r->xmin - m;
  out->ymin = r->ymin - m;
  out->xmax = r->xmax + m;
  out->ymax = r->ymax + m;
}
void far_print_FRect(const FarFRect* r) {
  printf("(%g, %g, %g, %g)\n", r->xmin, r->ymin, r->xmax, r->ymax);
}
void far_print_DRect(const FarDRect* r) {
  printf("(%g, %g, %g, %g)\n", r->xmin, r->ymin, r->xmax, r->ymax);
}

typedef struct { int32_t x, y; } FarIVec2;
typedef struct { int32_t x, y, z; } FarIVec3;
typedef struct { int32_t x, y, z, w; } FarIVec4;
typedef struct { float m00, m01, m10, m11; } FarMat2;
typedef struct { float m00, m01, m02, m10, m11, m12, m20, m21, m22; } FarMat3;
typedef struct {
  float m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33;
} FarMat4;
typedef struct { double m00, m01, m10, m11; } FarDMat2;
typedef struct { double m00, m01, m02, m10, m11, m12, m20, m21, m22; } FarDMat3;
typedef struct {
  double m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33;
} FarDMat4;
typedef struct { float x, y, z, w; } FarQuat;
typedef struct { double x, y, z, w; } FarDQuat;
typedef struct { float r, g, b, a; } FarColor;
typedef struct { uint8_t r, g, b, a; } FarColor32;
typedef struct {
  float px, py, pz, qx, qy, qz, qw, sx, sy, sz;
} FarTransform;
typedef struct { float min_x, min_y, min_z, max_x, max_y, max_z; } FarBounds;

#define FAR_IVEC2_DOT(T, PFX) \
  int64_t PFX##_dot(const T* a, const T* b) { return (int64_t)a->x * b->x + (int64_t)a->y * b->y; }
#define FAR_IVEC2_LEN2(T, PFX) \
  int64_t PFX##_length2(const T* v) { return PFX##_dot(v, v); }
#define FAR_IVEC2_LEN(T, PFX) \
  double PFX##_length(const T* v) { return sqrt((double)PFX##_length2(v)); }

FAR_IVEC2_DOT(FarIVec2, far_ivec2)
FAR_IVEC2_LEN2(FarIVec2, far_ivec2)
FAR_IVEC2_LEN(FarIVec2, far_ivec2)
void far_print_IVec2(const FarIVec2* v) { printf("(%d, %d)\n", v->x, v->y); }

#define FAR_IVEC3_DOT(T, PFX) \
  int64_t PFX##_dot(const T* a, const T* b) { \
    return (int64_t)a->x * b->x + (int64_t)a->y * b->y + (int64_t)a->z * b->z; \
  }
#define FAR_IVEC3_CROSS(T, PFX) \
  void PFX##_cross(const T* a, const T* b, T* out) { \
    out->x = a->y * b->z - a->z * b->y; \
    out->y = a->z * b->x - a->x * b->z; \
    out->z = a->x * b->y - a->y * b->x; \
  }
#define FAR_IVEC3_LEN2(T, PFX) \
  int64_t PFX##_length2(const T* v) { return PFX##_dot(v, v); }
#define FAR_IVEC3_LEN(T, PFX) \
  double PFX##_length(const T* v) { return sqrt((double)PFX##_length2(v)); }

FAR_IVEC3_DOT(FarIVec3, far_ivec3)
FAR_IVEC3_CROSS(FarIVec3, far_ivec3)
FAR_IVEC3_LEN2(FarIVec3, far_ivec3)
FAR_IVEC3_LEN(FarIVec3, far_ivec3)
void far_print_IVec3(const FarIVec3* v) { printf("(%d, %d, %d)\n", v->x, v->y, v->z); }

#define FAR_IVEC4_DOT(T, PFX) \
  int64_t PFX##_dot(const T* a, const T* b) { \
    return (int64_t)a->x * b->x + (int64_t)a->y * b->y + (int64_t)a->z * b->z + (int64_t)a->w * b->w; \
  }
#define FAR_IVEC4_LEN2(T, PFX) \
  int64_t PFX##_length2(const T* v) { return PFX##_dot(v, v); }
#define FAR_IVEC4_LEN(T, PFX) \
  double PFX##_length(const T* v) { return sqrt((double)PFX##_length2(v)); }

FAR_IVEC4_DOT(FarIVec4, far_ivec4)
FAR_IVEC4_LEN2(FarIVec4, far_ivec4)
FAR_IVEC4_LEN(FarIVec4, far_ivec4)
void far_print_IVec4(const FarIVec4* v) { printf("(%d, %d, %d, %d)\n", v->x, v->y, v->z, v->w); }

void far_mat2_transpose(const FarMat2* m, FarMat2* out) {
  out->m00 = m->m00; out->m01 = m->m10; out->m10 = m->m01; out->m11 = m->m11;
}
double far_mat2_determinant(const FarMat2* m) { return (double)m->m00 * m->m11 - (double)m->m01 * m->m10; }
void far_mat2_mul_mat(const FarMat2* a, const FarMat2* b, FarMat2* out) {
  out->m00 = a->m00 * b->m00 + a->m01 * b->m10;
  out->m01 = a->m00 * b->m01 + a->m01 * b->m11;
  out->m10 = a->m10 * b->m00 + a->m11 * b->m10;
  out->m11 = a->m10 * b->m01 + a->m11 * b->m11;
}
void far_mat2_mul_vec(const FarMat2* m, const FarFVec2* v, FarFVec2* out) {
  out->x = m->m00 * v->x + m->m01 * v->y;
  out->y = m->m10 * v->x + m->m11 * v->y;
}
void far_print_Mat2(const FarMat2* m) {
  printf("[[%g,%g],[%g,%g]]\n", m->m00, m->m01, m->m10, m->m11);
}

void far_mat3_transpose(const FarMat3* m, FarMat3* out) {
  out->m00 = m->m00; out->m01 = m->m10; out->m02 = m->m20;
  out->m10 = m->m01; out->m11 = m->m11; out->m12 = m->m21;
  out->m20 = m->m02; out->m21 = m->m12; out->m22 = m->m22;
}
double far_mat3_determinant(const FarMat3* m) {
  return (double)m->m00 * (m->m11 * m->m22 - m->m12 * m->m21) -
         (double)m->m01 * (m->m10 * m->m22 - m->m12 * m->m20) +
         (double)m->m02 * (m->m10 * m->m21 - m->m11 * m->m20);
}
void far_mat3_mul_mat(const FarMat3* a, const FarMat3* b, FarMat3* out) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k) {
        float av = *((&a->m00) + r * 3 + k);
        float bv = *((&b->m00) + k * 3 + c);
        sum += av * bv;
      }
      *((&out->m00) + r * 3 + c) = sum;
    }
  }
}
void far_mat3_mul_vec(const FarMat3* m, const FarFVec3* v, FarFVec3* out) {
  out->x = m->m00 * v->x + m->m01 * v->y + m->m02 * v->z;
  out->y = m->m10 * v->x + m->m11 * v->y + m->m12 * v->z;
  out->z = m->m20 * v->x + m->m21 * v->y + m->m22 * v->z;
}
void far_print_Mat3(const FarMat3* m) {
  printf("mat3(...)\n");
}

void far_mat4_transpose(const FarMat4* m, FarMat4* out) {
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      *((&out->m00) + c * 4 + r) = *((&m->m00) + r * 4 + c);
}
double far_mat4_determinant(const FarMat4* m) {
  (void)m;
  return 0.0;
}
void far_mat4_mul_mat(const FarMat4* a, const FarMat4* b, FarMat4* out) {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) {
        double av = *((&a->m00) + r * 4 + k);
        double bv = *((&b->m00) + k * 4 + c);
        sum += av * bv;
      }
      *((&out->m00) + r * 4 + c) = (float)sum;
    }
  }
}
void far_mat4_mul_vec(const FarMat4* m, const FarFVec4* v, FarFVec4* out) {
  out->x = m->m00 * v->x + m->m01 * v->y + m->m02 * v->z + m->m03 * v->w;
  out->y = m->m10 * v->x + m->m11 * v->y + m->m12 * v->z + m->m13 * v->w;
  out->z = m->m20 * v->x + m->m21 * v->y + m->m22 * v->z + m->m23 * v->w;
  out->w = m->m30 * v->x + m->m31 * v->y + m->m32 * v->z + m->m33 * v->w;
}
void far_print_Mat4(const FarMat4* m) { (void)m; printf("mat4(...)\n"); }

void far_dmat2_transpose(const FarDMat2* m, FarDMat2* out) {
  out->m00 = m->m00; out->m01 = m->m10; out->m10 = m->m01; out->m11 = m->m11;
}
double far_dmat2_determinant(const FarDMat2* m) { return m->m00 * m->m11 - m->m01 * m->m10; }
void far_dmat2_mul_mat(const FarDMat2* a, const FarDMat2* b, FarDMat2* out) {
  out->m00 = a->m00 * b->m00 + a->m01 * b->m10;
  out->m01 = a->m00 * b->m01 + a->m01 * b->m11;
  out->m10 = a->m10 * b->m00 + a->m11 * b->m10;
  out->m11 = a->m10 * b->m01 + a->m11 * b->m11;
}
void far_dmat2_mul_vec(const FarDMat2* m, const FarDVec2* v, FarDVec2* out) {
  out->x = m->m00 * v->x + m->m01 * v->y;
  out->y = m->m10 * v->x + m->m11 * v->y;
}
void far_print_DMat2(const FarDMat2* m) {
  printf("[[%g,%g],[%g,%g]]\n", m->m00, m->m01, m->m10, m->m11);
}

void far_dmat3_transpose(const FarDMat3* m, FarDMat3* out) {
  out->m00 = m->m00; out->m01 = m->m10; out->m02 = m->m20;
  out->m10 = m->m01; out->m11 = m->m11; out->m12 = m->m21;
  out->m20 = m->m02; out->m21 = m->m12; out->m22 = m->m22;
}
double far_dmat3_determinant(const FarDMat3* m) {
  return m->m00 * (m->m11 * m->m22 - m->m12 * m->m21) -
         m->m01 * (m->m10 * m->m22 - m->m12 * m->m20) +
         m->m02 * (m->m10 * m->m21 - m->m11 * m->m20);
}
void far_dmat3_mul_mat(const FarDMat3* a, const FarDMat3* b, FarDMat3* out) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      double sum = 0.0;
      for (int k = 0; k < 3; ++k) {
        double av = *((&a->m00) + r * 3 + k);
        double bv = *((&b->m00) + k * 3 + c);
        sum += av * bv;
      }
      *((&out->m00) + r * 3 + c) = sum;
    }
  }
}
void far_dmat3_mul_vec(const FarDMat3* m, const FarDVec3* v, FarDVec3* out) {
  out->x = m->m00 * v->x + m->m01 * v->y + m->m02 * v->z;
  out->y = m->m10 * v->x + m->m11 * v->y + m->m12 * v->z;
  out->z = m->m20 * v->x + m->m21 * v->y + m->m22 * v->z;
}
void far_print_DMat3(const FarDMat3* m) { (void)m; printf("dmat3(...)\n"); }

void far_dmat4_transpose(const FarDMat4* m, FarDMat4* out) {
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      *((&out->m00) + c * 4 + r) = *((&m->m00) + r * 4 + c);
}
double far_dmat4_determinant(const FarDMat4* m) { (void)m; return 0.0; }
void far_dmat4_mul_mat(const FarDMat4* a, const FarDMat4* b, FarDMat4* out) {
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) {
        double av = *((&a->m00) + r * 4 + k);
        double bv = *((&b->m00) + k * 4 + c);
        sum += av * bv;
      }
      *((&out->m00) + r * 4 + c) = sum;
    }
  }
}
void far_dmat4_mul_vec(const FarDMat4* m, const FarDVec4* v, FarDVec4* out) {
  out->x = m->m00 * v->x + m->m01 * v->y + m->m02 * v->z + m->m03 * v->w;
  out->y = m->m10 * v->x + m->m11 * v->y + m->m12 * v->z + m->m13 * v->w;
  out->z = m->m20 * v->x + m->m21 * v->y + m->m22 * v->z + m->m23 * v->w;
  out->w = m->m30 * v->x + m->m31 * v->y + m->m32 * v->z + m->m33 * v->w;
}
void far_print_DMat4(const FarDMat4* m) { (void)m; printf("dmat4(...)\n"); }

double far_quat_dot(const FarQuat* a, const FarQuat* b) {
  return (double)a->x * b->x + (double)a->y * b->y + (double)a->z * b->z + (double)a->w * b->w;
}
double far_quat_length2(const FarQuat* q) { return far_quat_dot(q, q); }
double far_quat_length(const FarQuat* q) { return sqrt(far_quat_length2(q)); }
void far_quat_normalize(const FarQuat* q, FarQuat* out) {
  double l = far_quat_length(q);
  if (l == 0.0) { *out = *q; return; }
  out->x = (float)(q->x / l); out->y = (float)(q->y / l);
  out->z = (float)(q->z / l); out->w = (float)(q->w / l);
}
void far_quat_mul(const FarQuat* a, const FarQuat* b, FarQuat* out) {
  out->w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
  out->x = a->w * b->x + a->x * b->w + a->y * b->z - a->z * b->y;
  out->y = a->w * b->y - a->x * b->z + a->y * b->w + a->z * b->x;
  out->z = a->w * b->z + a->x * b->y - a->y * b->x + a->z * b->w;
}
void far_print_Quat(const FarQuat* q) { printf("(%g, %g, %g, %g)\n", q->x, q->y, q->z, q->w); }

double far_dquat_dot(const FarDQuat* a, const FarDQuat* b) {
  return a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;
}
double far_dquat_length2(const FarDQuat* q) { return far_dquat_dot(q, q); }
double far_dquat_length(const FarDQuat* q) { return sqrt(far_dquat_length2(q)); }
void far_dquat_normalize(const FarDQuat* q, FarDQuat* out) {
  double l = far_dquat_length(q);
  if (l == 0.0) { *out = *q; return; }
  out->x = q->x / l; out->y = q->y / l; out->z = q->z / l; out->w = q->w / l;
}
void far_dquat_mul(const FarDQuat* a, const FarDQuat* b, FarDQuat* out) {
  out->w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
  out->x = a->w * b->x + a->x * b->w + a->y * b->z - a->z * b->y;
  out->y = a->w * b->y - a->x * b->z + a->y * b->w + a->z * b->x;
  out->z = a->w * b->z + a->x * b->y - a->y * b->x + a->z * b->w;
}
void far_print_DQuat(const FarDQuat* q) { printf("(%g, %g, %g, %g)\n", q->x, q->y, q->z, q->w); }

void far_print_Color(const FarColor* c) { printf("(%g, %g, %g, %g)\n", c->r, c->g, c->b, c->a); }
void far_print_Color32(const FarColor32* c) {
  printf("(%u, %u, %u, %u)\n", (unsigned)c->r, (unsigned)c->g, (unsigned)c->b, (unsigned)c->a);
}
void far_color32_to_color(const FarColor32* src, FarColor* out) {
  out->r = src->r / 255.0f; out->g = src->g / 255.0f;
  out->b = src->b / 255.0f; out->a = src->a / 255.0f;
}

void far_print_Transform(const FarTransform* t) {
  printf("transform(pos=%g,%g,%g scale=%g,%g,%g)\n", t->px, t->py, t->pz, t->sx, t->sy, t->sz);
}

int64_t far_frect_contains_vec(const FarFRect* r, const FarFVec2* p) {
  return p->x >= r->xmin && p->x <= r->xmax && p->y >= r->ymin && p->y <= r->ymax;
}

int64_t far_bounds_contains(const FarBounds* b, const FarFVec3* p) {
  return p->x >= b->min_x && p->x <= b->max_x && p->y >= b->min_y && p->y <= b->max_y &&
         p->z >= b->min_z && p->z <= b->max_z;
}
int64_t far_bounds_intersects(const FarBounds* a, const FarBounds* b) {
  return !(a->max_x < b->min_x || b->max_x < a->min_x || a->max_y < b->min_y || b->max_y < a->min_y ||
           a->max_z < b->min_z || b->max_z < a->min_z);
}
void far_bounds_expand(const FarBounds* b, float m, FarBounds* out) {
  out->min_x = b->min_x - m; out->min_y = b->min_y - m; out->min_z = b->min_z - m;
  out->max_x = b->max_x + m; out->max_y = b->max_y + m; out->max_z = b->max_z + m;
}
void far_bounds_center(const FarBounds* b, FarFVec3* out) {
  out->x = (b->min_x + b->max_x) * 0.5f;
  out->y = (b->min_y + b->max_y) * 0.5f;
  out->z = (b->min_z + b->max_z) * 0.5f;
}
void far_bounds_size(const FarBounds* b, FarFVec3* out) {
  out->x = b->max_x - b->min_x;
  out->y = b->max_y - b->min_y;
  out->z = b->max_z - b->min_z;
}
void far_print_Bounds(const FarBounds* b) {
  printf("bounds(%g,%g,%g -> %g,%g,%g)\n", b->min_x, b->min_y, b->min_z, b->max_x, b->max_y, b->max_z);
}

double far_fmin(double a, double b) { return fmin(a, b); }
double far_fmax(double a, double b) { return fmax(a, b); }

/* --- core math builtins (language/stdlib) --- */

double far_pi(void) { return 3.14159265358979323846; }
double far_nan(void) { return 0.0 / 0.0; }
double far_e(void) { return 2.71828182845904523536; }
double far_tau(void) { return far_pi() * 2.0; }
double far_phi(void) { return 1.61803398874989484820; }
double far_sqrt2(void) { return 1.41421356237309504880; }
double far_sqrt3(void) { return 1.73205080756887729352; }
double far_ln2(void) { return 0.69314718055994530942; }
double far_ln10(void) { return 2.30258509299404568402; }
double far_deg_per_rad(void) { return 180.0 / far_pi(); }
double far_rad_per_deg(void) { return far_pi() / 180.0; }

static int64_t far_i64_min(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t far_i64_max(int64_t a, int64_t b) { return a > b ? a : b; }

int64_t far_imin(int64_t a, int64_t b) { return far_i64_min(a, b); }
int64_t far_imax(int64_t a, int64_t b) { return far_i64_max(a, b); }
int64_t far_imin3(int64_t a, int64_t b, int64_t c) { return far_i64_min(far_i64_min(a, b), c); }
int64_t far_imax3(int64_t a, int64_t b, int64_t c) { return far_i64_max(far_i64_max(a, b), c); }
int64_t far_iabs(int64_t x) {
  if (x == INT64_MIN)
    return INT64_MAX;
  return x < 0 ? -x : x;
}
int64_t far_isign(int64_t x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }
int64_t far_clamp_i(int64_t x, int64_t lo, int64_t hi) {
  if (lo > hi) {
    int64_t t = lo;
    lo = hi;
    hi = t;
  }
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}
int64_t far_is_even(int64_t x) { return (x % 2) == 0; }
int64_t far_is_odd(int64_t x) { return (x % 2) != 0; }
int64_t far_mod_pos(int64_t x, int64_t m) {
  if (m <= 0)
    return 0;
  int64_t r = far_i64_mod_checked(x, m);
  if (r >= 0)
    return r;
  int64_t out = 0;
  if (!far_i64_add_ok(r, m, &out))
    return 0;
  return out;
}
int64_t far_gcd(int64_t a, int64_t b) {
  if (a == 0)
    return far_iabs(b);
  if (b == 0)
    return far_iabs(a);
  uint64_t ua = (uint64_t)(a < 0 ? -(uint64_t)a : (uint64_t)a);
  uint64_t ub = (uint64_t)(b < 0 ? -(uint64_t)b : (uint64_t)b);
  while (ub) {
    uint64_t t = ub;
    ub = ua % ub;
    ua = t;
  }
  /* |INT64_MIN| does not fit in int64_t; match far_iabs(INT64_MIN) -> INT64_MAX. */
  if (ua > (uint64_t)INT64_MAX)
    return INT64_MAX;
  return (int64_t)ua;
}
int64_t far_lcm(int64_t a, int64_t b) {
  if (a == 0 || b == 0) return 0;
  int64_t g = far_gcd(a, b);
  int64_t qa = far_iabs(a / g);
  int64_t qb = far_iabs(b);
  return far_i64_mul_or_zero(qa, qb);
}
int64_t far_factorial(int64_t n) {
  if (n < 0)
    return 0;
  if (n > (1 << 20))
    return 0;
  int64_t r = 1;
  for (int64_t i = 2; i <= n; ++i) {
    r = far_i64_mul_or_zero(r, i);
    if (r == 0)
      return 0;
  }
  return r;
}
int64_t far_binomial(int64_t n, int64_t k) {
  if (k < 0 || k > n) return 0;
  if (k > (1 << 16) || n > (1 << 24)) return 0;
  if (k == 0 || k == n) return 1;
  k = far_i64_min(k, n - k);
  int64_t num = 1;
  int64_t den = 1;
  for (int64_t i = 1; i <= k; ++i) {
    num = far_i64_mul_or_zero(num, n - k + i);
    if (num == 0)
      return 0;
    den = far_i64_mul_or_zero(den, i);
    if (den == 0)
      return 0;
  }
  return num / den;
}
int64_t far_isqrt(int64_t n) {
  if (n < 0)
    return 0;
  if (n < 2)
    return n;
  uint64_t un = (uint64_t)n;
  uint64_t x = un;
  uint64_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + un / x) / 2;
  }
  return (int64_t)x;
}
int64_t far_sum_range(int64_t lo, int64_t hi) {
  if (halfopen_span_checked(lo, hi) < 0)
    return 0;
  int64_t total = 0;
  for (int64_t i = lo; i < hi; ++i) {
    if (!far_i64_add_ok(total, i, &total))
      return 0;
  }
  return total;
}
int64_t far_sum_range_inclusive(int64_t lo, int64_t hi) {
  if (lo > hi) {
    int64_t t = lo;
    lo = hi;
    hi = t;
  }
  int64_t end = 0;
  if (!far_i64_add_ok(hi, 1, &end))
    return 0;
  return far_sum_range(lo, end);
}
int64_t far_product_range(int64_t lo, int64_t hi) {
  if (halfopen_span_checked(lo, hi) < 0)
    return 0;
  int64_t total = 1;
  for (int64_t i = lo; i < hi; ++i) {
    total = far_i64_mul_or_zero(total, i);
    if (total == 0)
      return 0;
  }
  return total;
}
static int64_t far_fib_impl(int64_t n) {
  if (n < 0)
    return 0;
  if (n > (1 << 20))
    return 0;
  if (n < 2) return n;
  int64_t a = 0;
  int64_t b = 1;
  for (int64_t i = 2; i <= n; ++i) {
    int64_t t = 0;
    if (!far_i64_add_ok(a, b, &t))
      return 0;
    a = b;
    b = t;
  }
  return b;
}
int64_t far_fib(int64_t n) { return far_fib_impl(n); }
int64_t far_fib_iter(int64_t n) { return far_fib_impl(n); }
int64_t far_twice(int64_t x) { return x * 2; }
int64_t far_thrice(int64_t x) { return x * 3; }
int64_t far_quad(int64_t x) { return x * 4; }

double far_deg_to_rad(double deg) { return deg * far_rad_per_deg(); }
double far_rad_to_deg(double rad) { return rad * far_deg_per_rad(); }
double far_sin_deg(double x) { return far_sin(far_deg_to_rad(x)); }
double far_cos_deg(double x) { return far_cos(far_deg_to_rad(x)); }
double far_tan_deg(double x) { return far_tan(far_deg_to_rad(x)); }
double far_asin_deg(double x) { return far_rad_to_deg(far_asin(x)); }
double far_acos_deg(double x) { return far_rad_to_deg(far_acos(x)); }
double far_atan_deg(double x) { return far_rad_to_deg(far_atan(x)); }
double far_atan2_deg(double y, double x) { return far_rad_to_deg(far_atan2(y, x)); }
double far_normalize_rad(double theta) {
  double t = far_fmod(theta, far_tau());
  return t < 0.0 ? t + far_tau() : t;
}
double far_normalize_deg(double theta) {
  double t = far_fmod(theta, 360.0);
  return t < 0.0 ? t + 360.0 : t;
}

double far_sec(double x) {
  double c = far_cos(x);
  if (far_recip_singular(c))
    return 0.0;
  double r = 1.0 / c;
  if (r != r)
    return 0.0;
  return r;
}
double far_csc(double x) {
  double s = far_sin(x);
  if (far_recip_singular(s))
    return 0.0;
  double r = 1.0 / s;
  if (r != r)
    return 0.0;
  return r;
}
double far_cot(double x) {
  double s = far_sin(x);
  if (far_recip_singular(s))
    return 0.0;
  double c = far_cos(x);
  if (c != c)
    return 0.0;
  double r = c / s;
  if (r != r)
    return 0.0;
  return r;
}
double far_haversine(double lat1, double lon1, double lat2, double lon2) {
  double dlat = far_deg_to_rad(lat2 - lat1);
  double dlon = far_deg_to_rad(lon2 - lon1);
  double a = far_sin(dlat / 2.0);
  a = a * a + far_cos(far_deg_to_rad(lat1)) * far_cos(far_deg_to_rad(lat2)) * far_sin(dlon / 2.0) * far_sin(dlon / 2.0);
  return 2.0 * far_atan2(far_sqrt(a), far_sqrt(1.0 - a));
}

double far_dmin(double a, double b) { return fmin(a, b); }
double far_dmax(double a, double b) { return fmax(a, b); }
double far_dmin3(double a, double b, double c) { return fmin(fmin(a, b), c); }
double far_dmax3(double a, double b, double c) { return fmax(fmax(a, b), c); }
double far_clamp_d(double x, double lo, double hi) {
  if (x != x)
    return 0.0;
  if (lo > hi) {
    double t = lo;
    lo = hi;
    hi = t;
  }
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}
double far_saturate(double x) {
  if (x != x)
    return 0.0;
  return far_clamp_d(x, 0.0, 1.0);
}
double far_lerp(double a, double b, double t) {
  if (a != a || b != b || t != t)
    return 0.0;
  return a + (b - a) * t;
}
double far_inv_lerp(double a, double b, double v) {
  if (a != a || b != b || v != v)
    return 0.0;
  double d = b - a;
  if (d == 0.0)
    return 0.0;
  return (v - a) / d;
}
double far_remap(double x, double in_lo, double in_hi, double out_lo, double out_hi) {
  if (x != x || in_lo != in_lo || in_hi != in_hi || out_lo != out_lo || out_hi != out_hi)
    return out_lo;
  return far_lerp(out_lo, out_hi, far_inv_lerp(in_lo, in_hi, x));
}
double far_square(double x) { return x * x; }
double far_cube(double x) { return x * x * x; }
int64_t far_approx_eq(double a, double b, double eps) { return far_fabs(a - b) <= eps; }
int64_t far_approx_zero(double x, double eps) { return far_fabs(x) <= eps; }
double far_dist2(double x1, double y1, double x2, double y2) {
  double dx = x2 - x1;
  double dy = y2 - y1;
  return dx * dx + dy * dy;
}
double far_dist(double x1, double y1, double x2, double y2) {
  return far_sqrt(far_dist2(x1, y1, x2, y2));
}
double far_sign_d(double x) {
  if (x != x)
    return 0.0;
  return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
}

static int64_t far_d_to_i64_saturating(double x) {
  if (x != x)
    return 0;
  if (x >= (double)INT64_MAX)
    return INT64_MAX;
  if (x <= (double)INT64_MIN)
    return INT64_MIN;
  return (int64_t)x;
}

int64_t far_round_i(double x) { return far_d_to_i64_saturating(far_round(x)); }
int64_t far_floor_i(double x) { return far_d_to_i64_saturating(far_floor(x)); }
int64_t far_ceil_i(double x) { return far_d_to_i64_saturating(far_ceil(x)); }
double far_log_n(double x, double base) {
  if (x != x || base != base)
    return 0.0;
  if (base == 1.0 || base == 0.0 || base < 0.0 || x <= 0.0)
    return 0.0;
  return far_log(x) / far_log(base);
}
double far_exp10(double x) { return far_pow(10.0, x); }
double far_smoothstep(double edge0, double edge1, double x) {
  if (edge0 == edge1)
    return x >= edge0 ? 1.0 : 0.0;
  double t = far_saturate(far_inv_lerp(edge0, edge1, x));
  return t * t * (3.0 - 2.0 * t);
}
double far_mean2(double a, double b) { return (a + b) / 2.0; }
double far_mean3(double a, double b, double c) { return (a + b + c) / 3.0; }
double far_variance2(double a, double b) {
  double m = far_mean2(a, b);
  double d0 = a - m;
  double d1 = b - m;
  return (d0 * d0 + d1 * d1) / 2.0;
}
double far_stddev2(double a, double b) { return far_sqrt(far_variance2(a, b)); }

int64_t far_arr_min(int64_t handle) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr || arr->len == 0)
    return 0;
  int64_t m = arr->data[0];
  for (int64_t i = 1; i < arr->len; ++i)
    if (arr->data[i] < m) m = arr->data[i];
  return m;
}
int64_t far_arr_max(int64_t handle) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr || arr->len == 0)
    return 0;
  int64_t m = arr->data[0];
  for (int64_t i = 1; i < arr->len; ++i)
    if (arr->data[i] > m) m = arr->data[i];
  return m;
}

int64_t far_arr_sum(int64_t handle) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr)
    return 0;
  int64_t total = 0;
  for (int64_t i = 0; i < arr->len; ++i) {
    if (!far_i64_add_ok(total, arr->data[i], &total))
      return 0;
  }
  return total;
}
int64_t far_arr_mean(int64_t handle) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr || arr->len == 0)
    return 0;
  return far_arr_sum(handle) / arr->len;
}
int64_t far_arr_count(int64_t handle, int64_t value) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr)
    return 0;
  int64_t c = 0;
  for (int64_t i = 0; i < arr->len; ++i)
    if (arr->data[i] == value) ++c;
  return c;
}
int64_t far_arr_index_of(int64_t handle, int64_t value) {
  FarTypedArray* arr = tarray_from_handle(handle);
  if (!arr)
    return -1;
  for (int64_t i = 0; i < arr->len; ++i)
    if (arr->data[i] == value) return i;
  return -1;
}

void far_vec2_lerp(const FarDVec2* a, const FarDVec2* b, double t, FarDVec2* out) {
  out->x = a->x + (b->x - a->x) * t;
  out->y = a->y + (b->y - a->y) * t;
}
void far_vec2_reflect(const FarDVec2* v, const FarDVec2* n, FarDVec2* out) {
  double d = far_dvec2_dot(v, n) * 2.0;
  out->x = v->x - n->x * d;
  out->y = v->y - n->y * d;
}
double far_vec2_angle(const FarDVec2* v) { return far_atan2(v->y, v->x); }
void far_rect_from_xywh(double x, double y, double w, double h, FarDRect* out) {
  out->xmin = x;
  out->ymin = y;
  out->xmax = x + w;
  out->ymax = y + h;
}
void far_rect_union(const FarDRect* a, const FarDRect* b, FarDRect* out) {
  out->xmin = fmin(a->xmin, b->xmin);
  out->ymin = fmin(a->ymin, b->ymin);
  out->xmax = fmax(a->xmax, b->xmax);
  out->ymax = fmax(a->ymax, b->ymax);
}

int64_t far_box_alloc(int64_t size) {
  if (!far_alloc_size_ok(size))
    return 0;
  void* p = calloc(1, (size_t)size);
  if (!p)
    return 0;
  return (int64_t)(uintptr_t)p;
}

int64_t far_union_new(int64_t tag, int64_t f0, int64_t f1, int64_t f2, int64_t f3, int64_t f4,
                      int64_t f5, int64_t f6, int64_t f7) {
  int64_t* p = (int64_t*)malloc(9 * sizeof(int64_t));
  if (!p)
    return 0;
  p[0] = tag;
  p[1] = f0;
  p[2] = f1;
  p[3] = f2;
  p[4] = f3;
  p[5] = f4;
  p[6] = f5;
  p[7] = f6;
  p[8] = f7;
  return (int64_t)(uintptr_t)p;
}

void far_union_drop(int64_t handle) {
  if (!handle)
    return;
  free((void*)(uintptr_t)handle);
}

int64_t far_union_tag(int64_t handle) {
  if (!handle)
    return -1;
  return ((int64_t*)(uintptr_t)handle)[0];
}

int64_t far_union_field(int64_t handle, int64_t idx) {
  if (!handle || idx < 0 || idx > 7)
    return 0;
  return ((int64_t*)(uintptr_t)handle)[1 + idx];
}

int64_t far_reflect_kind(int64_t tag) {
  if (tag < 0x8000) return 0;
  return (tag - 0x8000) / 256;
}

int64_t far_reflect_fields(int64_t tag) {
  if (tag < 0x8000) return 0;
  return (tag - 0x8000) % 256;
}

/* --- Memory management --- */

typedef struct FarMemBox {
  int64_t data;
  int64_t size;
  int64_t alive;
} FarMemBox;

typedef struct FarMemRc {
  int64_t data;
  int64_t rc;
  int64_t size;
} FarMemRc;

typedef struct FarArena {
  char* base;
  int64_t cap;
  int64_t off;
  int64_t* live;
  int64_t live_len;
  int64_t live_cap;
} FarArena;

typedef struct FarPool {
  int64_t* free_stack;
  void** objects;
  int64_t cap;
  int64_t free_count;
  int64_t elem_size;
} FarPool;

#define FAR_PTR_GUARD_CAP 4096
static int64_t g_ptr_revoked[FAR_PTR_GUARD_CAP];
static int g_ptr_revoked_pos;

static void far_ptr_guard_clear(int64_t ptr) {
  if (!ptr)
    return;
  for (int i = 0; i < FAR_PTR_GUARD_CAP; ++i) {
    if (g_ptr_revoked[i] == ptr)
      g_ptr_revoked[i] = 0;
  }
}

static void far_ptr_guard_revoke(int64_t ptr) {
  if (!ptr)
    return;
  g_ptr_revoked[g_ptr_revoked_pos % FAR_PTR_GUARD_CAP] = ptr;
  g_ptr_revoked_pos++;
}

static void far_ptr_guard_check(int64_t ptr) {
  if (!ptr)
    return;
  for (int i = 0; i < FAR_PTR_GUARD_CAP; ++i) {
    if (g_ptr_revoked[i] == ptr)
      far_panic(0);
  }
}

int64_t far_malloc(int64_t size) {
  if (!far_alloc_size_ok(size))
    return 0;
  void* p = malloc((size_t)size);
  if (!p)
    return 0;
  far_ptr_guard_clear((int64_t)(uintptr_t)p);
  return (int64_t)(uintptr_t)p;
}

void far_free(int64_t ptr) {
  if (ptr) {
    far_ptr_guard_revoke(ptr);
    free((void*)(uintptr_t)ptr);
  }
}

int64_t far_realloc(int64_t ptr, int64_t size) {
  if (!far_alloc_size_ok(size))
    return 0;
  if (ptr)
    far_ptr_guard_revoke(ptr);
  void* p = realloc((void*)(uintptr_t)ptr, (size_t)size);
  if (!p)
    return 0;
  far_ptr_guard_clear((int64_t)(uintptr_t)p);
  return (int64_t)(uintptr_t)p;
}

void far_ptr_store_i64(int64_t ptr, int64_t val) {
  if (!ptr)
    return;
  far_ptr_guard_check(ptr);
  *(int64_t*)(uintptr_t)ptr = val;
}

int64_t far_ptr_load_i64(int64_t ptr) {
  if (!ptr)
    return 0;
  far_ptr_guard_check(ptr);
  return *(int64_t*)(uintptr_t)ptr;
}

int64_t far_ptr_load_i8_as_i64(int64_t ptr) {
  if (!ptr)
    return 0;
  far_ptr_guard_check(ptr);
  return (int64_t) * (int8_t*)(uintptr_t)ptr;
}

int64_t far_ptr_load_i16_as_i64(int64_t ptr) {
  if (!ptr)
    return 0;
  far_ptr_guard_check(ptr);
  return (int64_t) * (int16_t*)(uintptr_t)ptr;
}

int64_t far_ptr_load_i32_as_i64(int64_t ptr) {
  if (!ptr)
    return 0;
  far_ptr_guard_check(ptr);
  return (int64_t) * (int32_t*)(uintptr_t)ptr;
}

double far_ptr_load_f64(int64_t ptr) {
  if (!ptr)
    return 0.0;
  far_ptr_guard_check(ptr);
  return *(double*)(uintptr_t)ptr;
}

double far_ptr_load_f32_as_f64(int64_t ptr) {
  if (!ptr)
    return 0.0;
  far_ptr_guard_check(ptr);
  return (double) * (float*)(uintptr_t)ptr;
}

uint16_t far_ptr_load_f16(int64_t ptr) {
  if (!ptr)
    return 0;
  far_ptr_guard_check(ptr);
  return *(uint16_t*)(uintptr_t)ptr;
}

void far_ptr_store_i8(int64_t ptr, int64_t val) {
  if (!ptr)
    return;
  far_ptr_guard_check(ptr);
  *(int8_t*)(uintptr_t)ptr = (int8_t)val;
}

void far_ptr_store_i16(int64_t ptr, int64_t val) {
  if (!ptr)
    return;
  far_ptr_guard_check(ptr);
  *(int16_t*)(uintptr_t)ptr = (int16_t)val;
}

void far_ptr_store_i32(int64_t ptr, int64_t val) {
  if (!ptr)
    return;
  far_ptr_guard_check(ptr);
  *(int32_t*)(uintptr_t)ptr = (int32_t)val;
}

void far_ptr_store_f64(int64_t ptr, double val) {
  if (!ptr)
    return;
  far_ptr_guard_check(ptr);
  *(double*)(uintptr_t)ptr = val;
}

void far_ptr_store_f32(int64_t ptr, double val) {
  if (!ptr)
    return;
  far_ptr_guard_check(ptr);
  *(float*)(uintptr_t)ptr = (float)val;
}

void far_ptr_store_f16(int64_t ptr, uint16_t bits) {
  if (!ptr)
    return;
  far_ptr_guard_check(ptr);
  *(uint16_t*)(uintptr_t)ptr = bits;
}

static FarMemBox* box_from(int64_t h) { return (FarMemBox*)(uintptr_t)h; }

int64_t far_box_new(int64_t size) {
  if (!far_alloc_size_ok(size))
    return 0;
  FarMemBox* b = (FarMemBox*)malloc(sizeof(FarMemBox));
  if (!b)
    return 0;
  void* data = calloc(1, (size_t)size);
  if (!data) {
    free(b);
    return 0;
  }
  far_ptr_guard_clear((int64_t)(uintptr_t)data);
  b->data = (int64_t)(uintptr_t)data;
  b->size = size;
  b->alive = 1;
  return (int64_t)(uintptr_t)b;
}

int64_t far_box_get(int64_t handle) {
  if (!handle)
    return 0;
  FarMemBox* b = box_from(handle);
  if (!b || !b->alive)
    return 0;
  return b->data;
}

void far_box_drop(int64_t handle) {
  if (!handle) return;
  FarMemBox* b = box_from(handle);
  if (!b || !b->alive) return;
  if (b->data) {
    far_ptr_guard_revoke(b->data);
    free((void*)(uintptr_t)b->data);
  }
  b->data = 0;
  b->size = 0;
  b->alive = 0;
}

int64_t far_box_move(int64_t handle) {
  if (!handle)
    return 0;
  FarMemBox* b = box_from(handle);
  if (!b || !b->alive)
    return 0;
  int64_t data = b->data;
  b->data = 0;
  b->alive = 0;
  free(b);
  return data;
}

static FarMemRc* rc_from(int64_t h) { return (FarMemRc*)(uintptr_t)h; }

int64_t far_rc_new(int64_t size) {
  if (!far_alloc_size_ok(size))
    return 0;
  FarMemRc* r = (FarMemRc*)malloc(sizeof(FarMemRc));
  if (!r)
    return 0;
  void* data = calloc(1, (size_t)size);
  if (!data) {
    free(r);
    return 0;
  }
  far_ptr_guard_clear((int64_t)(uintptr_t)data);
  r->data = (int64_t)(uintptr_t)data;
  r->rc = 1;
  r->size = size;
  return (int64_t)(uintptr_t)r;
}

int64_t far_rc_get(int64_t handle) {
  if (!handle)
    return 0;
  FarMemRc* r = rc_from(handle);
  if (!r || r->rc <= 0)
    return 0;
  return r->data;
}

int64_t far_rc_clone(int64_t handle) {
  FarMemRc* r = rc_from(handle);
  if (!r)
    return 0;
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  if (r->rc <= 0) {
    pthread_mutex_unlock(&g_spawn_mu);
    return 0;
  }
  r->rc++;
  pthread_mutex_unlock(&g_spawn_mu);
  return handle;
}

void far_rc_drop(int64_t handle) {
  FarMemRc* r = rc_from(handle);
  if (!r || r->rc <= 0) return;
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  if (r->rc <= 0) {
    pthread_mutex_unlock(&g_spawn_mu);
    return;
  }
  r->rc--;
  int drop = r->rc <= 0;
  pthread_mutex_unlock(&g_spawn_mu);
  if (drop) {
    if (r->data) {
      far_ptr_guard_revoke(r->data);
      free((void*)(uintptr_t)r->data);
    }
    r->data = 0;
    r->rc = 0;
    /* Keep header so stale handles cannot UAF on clone/get. */
  }
}

static FarArena* arena_from(int64_t h) { return (FarArena*)(uintptr_t)h; }

static int arena_live_push(FarArena* a, int64_t ptr) {
  if (!a || !ptr)
    return 0;
  if (a->live_len >= a->live_cap) {
    int64_t nc = a->live_cap < 8 ? 8 : a->live_cap * 2;
    int64_t* nl = (int64_t*)realloc(a->live, (size_t)nc * sizeof(int64_t));
    if (!nl)
      return 0;
    a->live = nl;
    a->live_cap = nc;
  }
  a->live[a->live_len++] = ptr;
  return 1;
}

static void arena_revoke_live(FarArena* a) {
  if (!a || !a->live)
    return;
  for (int64_t i = 0; i < a->live_len; ++i)
    far_ptr_guard_revoke(a->live[i]);
  a->live_len = 0;
}

int64_t far_arena_new(int64_t cap) {
  if (cap <= 0 || cap > FAR_ARENA_MAX_CAP)
    return 0;
  if ((uint64_t)cap > (uint64_t)SIZE_MAX)
    return 0;
  FarArena* a = (FarArena*)malloc(sizeof(FarArena));
  if (!a)
    return 0;
  a->base = (char*)malloc((size_t)cap);
  if (!a->base) {
    free(a);
    return 0;
  }
  a->cap = cap;
  a->off = 0;
  a->live = NULL;
  a->live_len = 0;
  a->live_cap = 0;
  return (int64_t)(uintptr_t)a;
}

int64_t far_arena_alloc(int64_t handle, int64_t size) {
  FarArena* a = arena_from(handle);
  if (!a || !a->base)
    return 0;
  if (size <= 0)
    return 0;
  int64_t align = 8;
  int64_t off = (a->off + align - 1) & ~(align - 1);
  if (off < 0 || off > a->cap)
    return 0;
  if ((uint64_t)size > (uint64_t)a->cap - (uint64_t)off)
    return 0;
  int64_t new_off = 0;
  if (!far_i64_add_ok(off, size, &new_off))
    return 0;
  int64_t saved_off = a->off;
  a->off = new_off;
  int64_t ptr = (int64_t)(uintptr_t)(a->base + off);
  far_ptr_guard_clear(ptr);
  if (!arena_live_push(a, ptr)) {
    a->off = saved_off;
    return 0;
  }
  return ptr;
}

void far_arena_reset(int64_t handle) {
  FarArena* a = arena_from(handle);
  if (!a)
    return;
  arena_revoke_live(a);
  a->off = 0;
}

void far_arena_drop(int64_t handle) {
  FarArena* a = arena_from(handle);
  if (!a)
    return;
  arena_revoke_live(a);
  free(a->live);
  if (a->base) free(a->base);
  free(a);
}

static FarPool* pool_from(int64_t h) { return (FarPool*)(uintptr_t)h; }

int64_t far_pool_new(int64_t elem_size, int64_t cap) {
  if (cap <= 0 || cap > FAR_COLL_MAX_CAP || elem_size <= 0)
    return 0;
  if ((uint64_t)elem_size > FAR_POOL_MAX_ELEM)
    return 0;
  if ((uint64_t)elem_size > (uint64_t)SIZE_MAX)
    return 0;
  if ((uint64_t)cap > (uint64_t)SIZE_MAX / (uint64_t)elem_size)
    return 0;
  if ((uint64_t)cap > (uint64_t)SIZE_MAX / sizeof(int64_t) ||
      (uint64_t)cap > (uint64_t)SIZE_MAX / sizeof(void*))
    return 0;
  FarPool* p = (FarPool*)malloc(sizeof(FarPool));
  if (!p)
    return 0;
  p->elem_size = elem_size;
  p->cap = cap;
  p->free_count = cap;
  p->free_stack = (int64_t*)malloc((size_t)cap * sizeof(int64_t));
  p->objects = (void**)malloc((size_t)cap * sizeof(void*));
  if (!p->free_stack || !p->objects) {
    free(p->free_stack);
    free(p->objects);
    free(p);
    return 0;
  }
  for (int64_t i = 0; i < cap; ++i) {
    p->objects[i] = calloc(1, (size_t)elem_size);
    if (!p->objects[i]) {
      for (int64_t j = 0; j < i; ++j)
        free(p->objects[j]);
      free(p->free_stack);
      free(p->objects);
      free(p);
      return 0;
    }
    far_ptr_guard_clear((int64_t)(uintptr_t)p->objects[i]);
    p->free_stack[i] = cap - 1 - i;
  }
  return (int64_t)(uintptr_t)p;
}

int64_t far_pool_acquire(int64_t handle) {
  FarPool* p = pool_from(handle);
  if (!p)
    return 0;
  if (p->free_count <= 0)
    return 0;
  p->free_count--;
  int64_t idx = p->free_stack[p->free_count];
  int64_t obj = (int64_t)(uintptr_t)p->objects[idx];
  far_ptr_guard_clear(obj);
  return obj;
}

void far_pool_release(int64_t handle, int64_t obj) {
  FarPool* p = pool_from(handle);
  if (!p)
    return;
  if (p->free_count >= p->cap)
    return;
  for (int64_t i = 0; i < p->cap; ++i) {
    if ((int64_t)(uintptr_t)p->objects[i] == obj) {
      for (int64_t j = 0; j < p->free_count; ++j) {
        if (p->free_stack[j] == i)
          return;
      }
      p->free_stack[p->free_count++] = i;
      return;
    }
  }
}

void far_pool_drop(int64_t handle) {
  FarPool* p = pool_from(handle);
  if (!p)
    return;
  for (int64_t i = 0; i < p->cap; ++i) {
    if (p->objects[i]) {
      far_ptr_guard_revoke((int64_t)(uintptr_t)p->objects[i]);
      free(p->objects[i]);
    }
  }
  free(p->objects);
  free(p->free_stack);
  free(p);
}

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
  pthread_cond_t drained;
  int64_t* buf;
  int cap;
  int count;
  int head;
  int tail;
  int closed;
  int active;
  int freeing;
} FarChannel;

static FarChannel* channel_from(int64_t handle) {
  return handle ? (FarChannel*)(uintptr_t)handle : NULL;
}

static int32_t sanitize_cap(int64_t cap) {
  if (cap <= 0)
    return -1;
  if (cap > (int64_t)INT32_MAX)
    return -1;
  return (int32_t)cap;
}

int64_t far_channel_new(int64_t cap) {
  int32_t icap = sanitize_cap(cap);
  if (icap < 0 || icap > FAR_CHAN_MAX_CAP)
    return 0;
  FarChannel* c = (FarChannel*)calloc(1, sizeof(FarChannel));
  if (!c)
    return 0;
  c->cap = icap;
  c->buf = (int64_t*)malloc((size_t)icap * sizeof(int64_t));
  if (!c->buf) {
    free(c);
    return 0;
  }
  pthread_mutex_init(&c->mu, NULL);
  pthread_cond_init(&c->not_empty, NULL);
  pthread_cond_init(&c->not_full, NULL);
  pthread_cond_init(&c->drained, NULL);
  return (int64_t)(uintptr_t)c;
}

int64_t far_channel_send(int64_t handle, int64_t value) {
  FarChannel* c = channel_from(handle);
  if (!c)
    return -1;
  pthread_mutex_lock(&c->mu);
  if (c->freeing) {
    pthread_mutex_unlock(&c->mu);
    return -1;
  }
  c->active++;
  while (c->count >= c->cap && !c->closed)
    pthread_cond_wait(&c->not_full, &c->mu);
  if (c->closed) {
    c->active--;
    if (c->freeing && c->active == 0)
      pthread_cond_signal(&c->drained);
    pthread_mutex_unlock(&c->mu);
    return -1;
  }
  c->buf[c->tail] = value;
  c->tail = (c->tail + 1) % c->cap;
  c->count++;
  pthread_cond_signal(&c->not_empty);
  c->active--;
  if (c->freeing && c->active == 0)
    pthread_cond_signal(&c->drained);
  pthread_mutex_unlock(&c->mu);
  return 0;
}

int64_t far_channel_recv(int64_t handle) {
  FarChannel* c = channel_from(handle);
  if (!c)
    return INT64_MIN;
  pthread_mutex_lock(&c->mu);
  if (c->freeing) {
    pthread_mutex_unlock(&c->mu);
    return INT64_MIN;
  }
  c->active++;
  while (c->count == 0 && !c->closed)
    pthread_cond_wait(&c->not_empty, &c->mu);
  if (c->count == 0) {
    c->active--;
    if (c->freeing && c->active == 0)
      pthread_cond_signal(&c->drained);
    pthread_mutex_unlock(&c->mu);
    /* Closed and drained: INT64_MIN signals no more values. */
    return INT64_MIN;
  }
  int64_t value = c->buf[c->head];
  c->head = (c->head + 1) % c->cap;
  c->count--;
  pthread_cond_signal(&c->not_full);
  c->active--;
  if (c->freeing && c->active == 0)
    pthread_cond_signal(&c->drained);
  pthread_mutex_unlock(&c->mu);
  return value;
}

int64_t far_channel_try_recv(int64_t handle) {
  FarChannel* c = channel_from(handle);
  if (!c)
    return INT64_MIN;
  pthread_mutex_lock(&c->mu);
  if (c->freeing) {
    pthread_mutex_unlock(&c->mu);
    return INT64_MIN;
  }
  c->active++;
  if (c->count == 0) {
    c->active--;
    if (c->freeing && c->active == 0)
      pthread_cond_signal(&c->drained);
    pthread_mutex_unlock(&c->mu);
    return INT64_MIN;
  }
  int64_t value = c->buf[c->head];
  c->head = (c->head + 1) % c->cap;
  c->count--;
  pthread_cond_signal(&c->not_full);
  c->active--;
  if (c->freeing && c->active == 0)
    pthread_cond_signal(&c->drained);
  pthread_mutex_unlock(&c->mu);
  return value;
}

int64_t far_channel_try_send(int64_t handle, int64_t value) {
  FarChannel* c = channel_from(handle);
  if (!c)
    return -1;
  pthread_mutex_lock(&c->mu);
  if (c->freeing) {
    pthread_mutex_unlock(&c->mu);
    return -1;
  }
  c->active++;
  if (c->closed || c->count >= c->cap) {
    c->active--;
    if (c->freeing && c->active == 0)
      pthread_cond_signal(&c->drained);
    pthread_mutex_unlock(&c->mu);
    return -1;
  }
  c->buf[c->tail] = value;
  c->tail = (c->tail + 1) % c->cap;
  c->count++;
  pthread_cond_signal(&c->not_empty);
  c->active--;
  if (c->freeing && c->active == 0)
    pthread_cond_signal(&c->drained);
  pthread_mutex_unlock(&c->mu);
  return 0;
}

void far_channel_close(int64_t handle) {
  FarChannel* c = channel_from(handle);
  if (!c)
    return;
  pthread_mutex_lock(&c->mu);
  c->closed = 1;
  pthread_cond_broadcast(&c->not_empty);
  pthread_cond_broadcast(&c->not_full);
  pthread_mutex_unlock(&c->mu);
}

int64_t far_channel_is_closed(int64_t handle) {
  FarChannel* c = channel_from(handle);
  if (!c)
    return 0;
  pthread_mutex_lock(&c->mu);
  int closed = c->closed;
  pthread_mutex_unlock(&c->mu);
  return closed ? 1 : 0;
}

int64_t far_channel_pending(int64_t handle) {
  FarChannel* c = channel_from(handle);
  if (!c)
    return 0;
  pthread_mutex_lock(&c->mu);
  int64_t n = c->count;
  pthread_mutex_unlock(&c->mu);
  return n;
}

void far_channel_drop(int64_t handle) {
  FarChannel* c = channel_from(handle);
  if (!c) return;
  pthread_mutex_lock(&c->mu);
  c->closed = 1;
  c->freeing = 1;
  pthread_cond_broadcast(&c->not_empty);
  pthread_cond_broadcast(&c->not_full);
  while (c->active > 0)
    pthread_cond_wait(&c->drained, &c->mu);
  pthread_mutex_unlock(&c->mu);
  pthread_mutex_destroy(&c->mu);
  pthread_cond_destroy(&c->not_empty);
  pthread_cond_destroy(&c->not_full);
  pthread_cond_destroy(&c->drained);
  free(c->buf);
  free(c);
}

#if defined(_WIN32)
typedef DWORD far_mutex_owner_t;
static far_mutex_owner_t far_mutex_current_owner(void) { return GetCurrentThreadId(); }
static int far_mutex_same_owner(far_mutex_owner_t a, far_mutex_owner_t b) { return a != 0 && a == b; }
#define FAR_MUTEX_OWNER_NONE ((DWORD)0)
#else
typedef pthread_t far_mutex_owner_t;
static far_mutex_owner_t far_mutex_current_owner(void) { return pthread_self(); }
static int far_mutex_same_owner(far_mutex_owner_t a, far_mutex_owner_t b) {
  return a != (pthread_t)0 && pthread_equal(a, b);
}
#define FAR_MUTEX_OWNER_NONE ((pthread_t)0)
#endif

typedef struct {
  pthread_mutex_t mu;
  int locked;
  far_mutex_owner_t owner;
} FarMutex;

static FarMutex* mutex_from(int64_t handle) {
  return handle ? (FarMutex*)(uintptr_t)handle : NULL;
}

int64_t far_mutex_new(void) {
  FarMutex* m = (FarMutex*)calloc(1, sizeof(FarMutex));
  if (!m)
    return 0;
  pthread_mutex_init(&m->mu, NULL);
  m->owner = FAR_MUTEX_OWNER_NONE;
  return (int64_t)(uintptr_t)m;
}

void far_mutex_lock(int64_t handle) {
  FarMutex* m = mutex_from(handle);
  if (!m)
    return;
  if (m->locked && far_mutex_same_owner(m->owner, far_mutex_current_owner())) {
    far_panic(0);
    return;
  }
  pthread_mutex_lock(&m->mu);
  m->owner = far_mutex_current_owner();
  m->locked = 1;
}

void far_mutex_unlock(int64_t handle) {
  FarMutex* m = mutex_from(handle);
  if (!m || !m->locked)
    return;
  if (!far_mutex_same_owner(m->owner, far_mutex_current_owner()))
    return;
  m->locked = 0;
  m->owner = FAR_MUTEX_OWNER_NONE;
  pthread_mutex_unlock(&m->mu);
}

void far_mutex_drop(int64_t handle) {
  FarMutex* m = mutex_from(handle);
  if (!m)
    return;
  if (m->locked) {
    far_panic(0);
    return;
  }
  pthread_mutex_destroy(&m->mu);
  free(m);
}

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t cv;
  int count;
} FarSemaphore;

static FarSemaphore* semaphore_from(int64_t handle) {
  return handle ? (FarSemaphore*)(uintptr_t)handle : NULL;
}

int64_t far_semaphore_new(int64_t initial) {
  if (initial < 0 || initial > (int64_t)INT_MAX)
    return 0;
  FarSemaphore* s = (FarSemaphore*)calloc(1, sizeof(FarSemaphore));
  if (!s)
    return 0;
  s->count = (int)initial;
  pthread_mutex_init(&s->mu, NULL);
  pthread_cond_init(&s->cv, NULL);
  return (int64_t)(uintptr_t)s;
}

void far_semaphore_wait(int64_t handle) {
  FarSemaphore* s = semaphore_from(handle);
  if (!s)
    return;
  pthread_mutex_lock(&s->mu);
  while (s->count <= 0)
    pthread_cond_wait(&s->cv, &s->mu);
  s->count--;
  pthread_mutex_unlock(&s->mu);
}

int64_t far_semaphore_try_wait(int64_t handle) {
  FarSemaphore* s = semaphore_from(handle);
  if (!s)
    return 0;
  pthread_mutex_lock(&s->mu);
  if (s->count <= 0) {
    pthread_mutex_unlock(&s->mu);
    return 0;
  }
  s->count--;
  pthread_mutex_unlock(&s->mu);
  return 1;
}

void far_semaphore_signal(int64_t handle) {
  FarSemaphore* s = semaphore_from(handle);
  if (!s)
    return;
  pthread_mutex_lock(&s->mu);
  if (s->count < INT_MAX)
    s->count++;
  pthread_cond_signal(&s->cv);
  pthread_mutex_unlock(&s->mu);
}

void far_semaphore_drop(int64_t handle) {
  FarSemaphore* s = semaphore_from(handle);
  if (!s)
    return;
  pthread_mutex_destroy(&s->mu);
  pthread_cond_destroy(&s->cv);
  free(s);
}

typedef struct {
  pthread_mutex_t mu;
  int64_t value;
} FarAtomic;

static FarAtomic* atomic_from(int64_t handle) {
  return handle ? (FarAtomic*)(uintptr_t)handle : NULL;
}

int64_t far_atomic_new(int64_t initial) {
  FarAtomic* a = (FarAtomic*)calloc(1, sizeof(FarAtomic));
  if (!a)
    return 0;
  a->value = initial;
  pthread_mutex_init(&a->mu, NULL);
  return (int64_t)(uintptr_t)a;
}

int64_t far_atomic_load(int64_t handle) {
  FarAtomic* a = atomic_from(handle);
  if (!a)
    return 0;
  pthread_mutex_lock(&a->mu);
  int64_t v = a->value;
  pthread_mutex_unlock(&a->mu);
  return v;
}

void far_atomic_store(int64_t handle, int64_t value) {
  FarAtomic* a = atomic_from(handle);
  if (!a)
    return;
  pthread_mutex_lock(&a->mu);
  a->value = value;
  pthread_mutex_unlock(&a->mu);
}

int64_t far_atomic_fetch_add(int64_t handle, int64_t delta) {
  FarAtomic* a = atomic_from(handle);
  if (!a)
    return 0;
  pthread_mutex_lock(&a->mu);
  int64_t prev = a->value;
  int64_t next = 0;
  if (far_i64_add_ok(prev, delta, &next))
    a->value = next;
  pthread_mutex_unlock(&a->mu);
  return prev;
}

int64_t far_atomic_compare_exchange(int64_t handle, int64_t expected, int64_t desired) {
  FarAtomic* a = atomic_from(handle);
  if (!a)
    return 0;
  pthread_mutex_lock(&a->mu);
  int64_t prev = a->value;
  if (prev == expected)
    a->value = desired;
  pthread_mutex_unlock(&a->mu);
  return prev;
}

void far_atomic_drop(int64_t handle) {
  FarAtomic* a = atomic_from(handle);
  if (!a)
    return;
  pthread_mutex_destroy(&a->mu);
  free(a);
}

typedef struct {
  pthread_mutex_t mu;
  int64_t* buf;
  int cap;
  int count;
  int head;
  int tail;
} FarLFQueue;

static FarLFQueue* lfqueue_from(int64_t handle) {
  return handle ? (FarLFQueue*)(uintptr_t)handle : NULL;
}

int64_t far_lfqueue_new(int64_t cap) {
  int32_t icap = sanitize_cap(cap);
  if (icap < 0 || icap > FAR_CHAN_MAX_CAP)
    return 0;
  FarLFQueue* q = (FarLFQueue*)calloc(1, sizeof(FarLFQueue));
  if (!q)
    return 0;
  q->cap = icap;
  q->buf = (int64_t*)malloc((size_t)icap * sizeof(int64_t));
  if (!q->buf) {
    free(q);
    return 0;
  }
  pthread_mutex_init(&q->mu, NULL);
  return (int64_t)(uintptr_t)q;
}

int64_t far_lfqueue_push(int64_t handle, int64_t value) {
  FarLFQueue* q = lfqueue_from(handle);
  if (!q)
    return -1;
  pthread_mutex_lock(&q->mu);
  if (q->count >= q->cap) {
    pthread_mutex_unlock(&q->mu);
    return -1;
  }
  q->buf[q->tail] = value;
  q->tail = (q->tail + 1) % q->cap;
  q->count++;
  pthread_mutex_unlock(&q->mu);
  return 0;
}

int64_t far_lfqueue_pop(int64_t handle) {
  FarLFQueue* q = lfqueue_from(handle);
  if (!q)
    return -1;
  pthread_mutex_lock(&q->mu);
  if (q->count == 0) {
    pthread_mutex_unlock(&q->mu);
    return -1;
  }
  int64_t value = q->buf[q->head];
  q->head = (q->head + 1) % q->cap;
  q->count--;
  pthread_mutex_unlock(&q->mu);
  return value;
}

void far_lfqueue_drop(int64_t handle) {
  FarLFQueue* q = lfqueue_from(handle);
  if (!q)
    return;
  pthread_mutex_destroy(&q->mu);
  free(q->buf);
  free(q);
}

typedef struct {
  pthread_mutex_t mu;
  int nworkers;
  int shutdown;
} FarThreadPool;

static FarThreadPool* threadpool_from(int64_t handle) {
  return handle ? (FarThreadPool*)(uintptr_t)handle : NULL;
}

int64_t far_threadpool_new(int64_t nworkers) {
  if (nworkers <= 0)
    return 0;
  if (nworkers > 1024)
    nworkers = 1024;
  FarThreadPool* p = (FarThreadPool*)calloc(1, sizeof(FarThreadPool));
  if (!p)
    return 0;
  p->nworkers = (int)nworkers;
  pthread_mutex_init(&p->mu, NULL);
  return (int64_t)(uintptr_t)p;
}

int64_t far_threadpool_submit(int64_t handle, void* fn, int64_t arg) {
  FarThreadPool* p = threadpool_from(handle);
  if (!p)
    return -1;
  pthread_mutex_lock(&p->mu);
  int shutdown = p->shutdown;
  pthread_mutex_unlock(&p->mu);
  if (shutdown)
    return -1;
  int64_t task = far_spawn(fn, 1, arg, 0, 0, 0);
  if (task < 0)
    return -1;
  return task;
}

void far_threadpool_shutdown(int64_t handle) {
  FarThreadPool* p = threadpool_from(handle);
  if (!p)
    return;
  pthread_mutex_lock(&p->mu);
  p->shutdown = 1;
  pthread_mutex_unlock(&p->mu);
}

void far_threadpool_drop(int64_t handle) {
  FarThreadPool* p = threadpool_from(handle);
  if (!p)
    return;
  pthread_mutex_destroy(&p->mu);
  free(p);
}

int64_t far_parallel_for(void* fn, int64_t start, int64_t end) {
  if (end <= start) return 0;
  if (!fn)
    return -1;
  int64_t n;
  if (__builtin_sub_overflow(end, start, &n))
    return -1;
  if (n > (1 << 24))
    return -1;
  int64_t max_workers = far_thread_count();
  if (max_workers < 1) max_workers = 1;
  if (max_workers > 64) max_workers = 64;
  int64_t total = 0;
  for (int64_t base = 0; base < n; base += max_workers) {
    int64_t batch = n - base;
    if (batch > max_workers) batch = max_workers;
    int64_t* handles = (int64_t*)malloc((size_t)batch * sizeof(int64_t));
    if (!handles)
      return -1;
    int64_t spawned = 0;
    for (int64_t i = 0; i < batch; ++i) {
      int64_t idx = 0;
      int64_t t = 0;
      if (!far_i64_add_ok(start, base, &t) || !far_i64_add_ok(t, i, &idx)) {
        for (int64_t j = 0; j < spawned; ++j)
          far_join(handles[j]);
        free(handles);
        return -1;
      }
      handles[i] = far_spawn(fn, 1, idx, 0, 0, 0);
      if (handles[i] < 0) {
        for (int64_t j = 0; j < spawned; ++j)
          far_join(handles[j]);
        free(handles);
        return -1;
      }
      spawned++;
    }
    for (int64_t i = 0; i < batch; ++i) {
      int64_t jr = far_join(handles[i]);
      if (!far_i64_add_ok(total, jr, &total)) {
        for (int64_t j = i + 1; j < batch; ++j)
          (void)far_join(handles[j]);
        free(handles);
        return -1;
      }
    }
    free(handles);
  }
  return total;
}

static int64_t far_pfor_closure_tramp(int64_t closure, int64_t idx) {
  return far_closure_call(closure, idx);
}

int64_t far_parallel_for_cl(int64_t closure, int64_t start, int64_t end) {
  if (end <= start) return 0;
  if (!closure)
    return -1;
  int64_t n;
  if (__builtin_sub_overflow(end, start, &n))
    return -1;
  if (n > (1 << 24))
    return -1;
  int64_t max_workers = far_thread_count();
  if (max_workers < 1) max_workers = 1;
  if (max_workers > 64) max_workers = 64;
  int64_t total = 0;
  for (int64_t base = 0; base < n; base += max_workers) {
    int64_t batch = n - base;
    if (batch > max_workers) batch = max_workers;
    int64_t* handles = (int64_t*)malloc((size_t)batch * sizeof(int64_t));
    if (!handles)
      return -1;
    int64_t spawned = 0;
    for (int64_t i = 0; i < batch; ++i) {
      int64_t idx = 0;
      int64_t t = 0;
      if (!far_i64_add_ok(start, base, &t) || !far_i64_add_ok(t, i, &idx)) {
        for (int64_t j = 0; j < spawned; ++j)
          far_join(handles[j]);
        free(handles);
        return -1;
      }
      handles[i] = far_spawn((void*)far_pfor_closure_tramp, 2, closure, idx, 0, 0);
      if (handles[i] < 0) {
        for (int64_t j = 0; j < spawned; ++j)
          far_join(handles[j]);
        free(handles);
        return -1;
      }
      spawned++;
    }
    for (int64_t i = 0; i < batch; ++i) {
      int64_t jr = far_join(handles[i]);
      if (!far_i64_add_ok(total, jr, &total)) {
        for (int64_t j = i + 1; j < batch; ++j)
          (void)far_join(handles[j]);
        free(handles);
        return -1;
      }
    }
    free(handles);
  }
  return total;
}

typedef int64_t (*FarActorHandler)(int64_t state, int64_t msg);

typedef struct {
  FarActorHandler handler;
  int64_t state;
  pthread_mutex_t mu;
  pthread_cond_t drained;
  int stopped;
  int active;
} FarActor;

typedef struct {
  FarActor* a;
} FarActorSlot;

static FarActorSlot* g_actors = NULL;
static size_t g_actor_cap = 0;

static int ensure_actor_cap(size_t need) {
  if (need > FAR_MAX_ACTORS)
    return 0;
  if (need <= g_actor_cap)
    return 1;
  size_t new_cap = g_actor_cap == 0 ? 8 : g_actor_cap * 2;
  while (new_cap < need) {
    if (new_cap > SIZE_MAX / 2)
      return 0;
    new_cap *= 2;
  }
  if (new_cap > SIZE_MAX / sizeof(FarActorSlot))
    return 0;
  FarActorSlot* nt = (FarActorSlot*)realloc(g_actors, new_cap * sizeof(FarActorSlot));
  if (!nt)
    return 0;
  memset(nt + g_actor_cap, 0, (new_cap - g_actor_cap) * sizeof(FarActorSlot));
  g_actors = nt;
  g_actor_cap = new_cap;
  return 1;
}

static FarActor* actor_from_locked(int64_t handle) {
  if (handle < 0 || (size_t)handle >= g_actor_cap)
    return NULL;
  return g_actors[(size_t)handle].a;
}

static FarActor* actor_from(int64_t handle) {
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  FarActor* a = actor_from_locked(handle);
  pthread_mutex_unlock(&g_spawn_mu);
  return a;
}

int64_t far_actor_spawn(void* fn, int64_t initial_state) {
  if (!fn)
    return -1;
  FarActor* a = (FarActor*)calloc(1, sizeof(FarActor));
  if (!a)
    return -1;
  a->handler = (FarActorHandler)fn;
  a->state = initial_state;
  pthread_mutex_init(&a->mu, NULL);
  pthread_cond_init(&a->drained, NULL);
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  size_t slot = 0;
  for (; slot < g_actor_cap; ++slot) {
    if (g_actors[slot].a == NULL)
      break;
  }
  if (!ensure_actor_cap(slot + 1)) {
    pthread_mutex_unlock(&g_spawn_mu);
    pthread_mutex_destroy(&a->mu);
    pthread_cond_destroy(&a->drained);
    free(a);
    return -1;
  }
  g_actors[slot].a = a;
  pthread_mutex_unlock(&g_spawn_mu);
  return (int64_t)slot;
}

void far_actor_tell(int64_t handle, int64_t msg) {
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  FarActor* a = actor_from_locked(handle);
  if (!a) {
    pthread_mutex_unlock(&g_spawn_mu);
    return;
  }
  pthread_mutex_lock(&a->mu);
  pthread_mutex_unlock(&g_spawn_mu);
  if (a->stopped) {
    pthread_mutex_unlock(&a->mu);
    return;
  }
  FarActorHandler handler = a->handler;
  int64_t state = a->state;
  if (!handler) {
    pthread_mutex_unlock(&a->mu);
    return;
  }
  a->active++;
  pthread_mutex_unlock(&a->mu);
  int64_t new_state = handler(state, msg);
  pthread_mutex_lock(&a->mu);
  a->state = new_state;
  a->active--;
  if (a->stopped && a->active == 0)
    pthread_cond_signal(&a->drained);
  pthread_mutex_unlock(&a->mu);
}

int64_t far_actor_ask(int64_t handle, int64_t msg) {
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  FarActor* a = actor_from_locked(handle);
  if (!a) {
    pthread_mutex_unlock(&g_spawn_mu);
    return INT64_MIN;
  }
  pthread_mutex_lock(&a->mu);
  pthread_mutex_unlock(&g_spawn_mu);
  if (a->stopped) {
    pthread_mutex_unlock(&a->mu);
    return INT64_MIN;
  }
  FarActorHandler handler = a->handler;
  int64_t state = a->state;
  if (!handler) {
    pthread_mutex_unlock(&a->mu);
    return INT64_MIN;
  }
  a->active++;
  pthread_mutex_unlock(&a->mu);
  int64_t reply = handler(state, msg);
  pthread_mutex_lock(&a->mu);
  a->state = reply;
  a->active--;
  if (a->stopped && a->active == 0)
    pthread_cond_signal(&a->drained);
  pthread_mutex_unlock(&a->mu);
  return reply;
}

void far_actor_stop(int64_t handle) {
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
  FarActor* a = actor_from_locked(handle);
  if (!a) {
    pthread_mutex_unlock(&g_spawn_mu);
    return;
  }
  pthread_mutex_unlock(&g_spawn_mu);
  pthread_mutex_lock(&a->mu);
  if (a->stopped) {
    pthread_mutex_unlock(&a->mu);
    return;
  }
  a->stopped = 1;
  while (a->active > 0)
    pthread_cond_wait(&a->drained, &a->mu);
  pthread_mutex_unlock(&a->mu);
  pthread_mutex_destroy(&a->mu);
  pthread_cond_destroy(&a->drained);
  free(a);
  pthread_mutex_lock(&g_spawn_mu);
  if ((size_t)handle < g_actor_cap)
    g_actors[(size_t)handle].a = NULL;
  pthread_mutex_unlock(&g_spawn_mu);
}

void far_stack_trace(void) {
  fprintf(stderr, "stack trace:\n");
#ifdef _WIN32
  void* trace[32];
  USHORT n = CaptureStackBackTrace(1, 32, trace, NULL);
  for (USHORT i = 0; i < n; ++i)
    fprintf(stderr, "  at %p\n", trace[i]);
#else
  (void)0;
#endif
}

int64_t far_i64_add_checked(int64_t a, int64_t b) {
#if defined(__GNUC__) || defined(__clang__)
  int64_t out;
  if (__builtin_add_overflow(a, b, &out))
    far_panic(0);
  return out;
#else
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
    far_panic(0);
  return a + b;
#endif
}

int64_t far_i64_sub_checked(int64_t a, int64_t b) {
#if defined(__GNUC__) || defined(__clang__)
  int64_t out;
  if (__builtin_sub_overflow(a, b, &out))
    far_panic(0);
  return out;
#else
  if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
    far_panic(0);
  return a - b;
#endif
}

int64_t far_i64_mul_checked(int64_t a, int64_t b) {
#if defined(__GNUC__) || defined(__clang__)
  int64_t out;
  if (__builtin_mul_overflow(a, b, &out))
    far_panic(0);
  return out;
#else
  if (a != 0 && b != 0) {
    if (a == -1 && b == INT64_MIN) {
      far_panic(0);
      return 0;
    }
    if (b == -1 && a == INT64_MIN) {
      far_panic(0);
      return 0;
    }
    if ((a > 0 && b > 0 && a > INT64_MAX / b) || (a > 0 && b < 0 && b < INT64_MIN / a) ||
        (a < 0 && b > 0 && a < INT64_MIN / b) || (a < 0 && b < 0 && a < INT64_MAX / b)) {
      far_panic(0);
      return 0;
    }
  }
  return a * b;
#endif
}

int64_t far_i64_div_checked(int64_t a, int64_t b) {
  if (b == 0)
    far_panic(0);
  if (a == INT64_MIN && b == -1)
    return INT64_MIN;
  return a / b;
}

int64_t far_i64_mod_checked(int64_t a, int64_t b) {
  if (b == 0)
    far_panic(0);
  if (a == INT64_MIN && b == -1)
    return 0;
  return a % b;
}

int64_t far_i64_neg_checked(int64_t a) {
  if (a == INT64_MIN)
    far_panic(0);
  return -a;
}

int64_t far_u64_div_checked(int64_t a, int64_t b) {
  if (b == 0)
    far_panic(0);
  return (int64_t)((uint64_t)a / (uint64_t)b);
}

int64_t far_u64_mod_checked(int64_t a, int64_t b) {
  if (b == 0)
    far_panic(0);
  return (int64_t)((uint64_t)a % (uint64_t)b);
}

int64_t far_i64_shl_checked(int64_t a, int64_t b) {
  if (b < 0 || b >= 64)
    far_panic(0);
  uint64_t ul = (uint64_t)a;
  uint64_t ur = (uint64_t)b;
  if (ur > 0 && ul > (UINT64_MAX >> ur))
    far_panic(0);
  int64_t out = (int64_t)(ul << ur);
  if (a >= 0 && out < 0)
    far_panic(0);
  return out;
}

int64_t far_i64_shr_checked(int64_t a, int64_t b) {
  if (b < 0 || b >= 64)
    far_panic(0);
  return a >> b;
}

int64_t far_u64_shr_checked(int64_t a, int64_t b) {
  if (b < 0 || b >= 64)
    far_panic(0);
  return (int64_t)((uint64_t)a >> (uint64_t)b);
}

double far_f64_div_checked(double a, double b) {
  if (a != a || b != b)
    return 0.0;
  if (b == 0.0) {
    if (a == 0.0)
      return a / b;
    return 0.0;
  }
  return a / b;
}

double far_f64_rem_checked(double a, double b) {
  if (b == 0.0 || b != b || a != a)
    return 0.0;
  return fmod(a, b);
}

#define FAR_MAX_CALL_DEPTH 10000

static _Thread_local int64_t g_far_call_depth = 0;

void far_call_reset(void) {
  g_far_call_depth = 0;
}

void far_call_enter(void) {
  if (g_far_call_depth >= FAR_MAX_CALL_DEPTH)
    far_panic(0);
  ++g_far_call_depth;
}

void far_call_leave(void) {
  if (g_far_call_depth > 0)
    --g_far_call_depth;
}

void far_panic(int64_t msg) {
  if (msg)
    far_print_str((char*)(intptr_t)msg);
  else
    fprintf(stderr, "panic\n");
  far_stack_trace();
  exit(1);
}

void far_assert(int64_t cond, int64_t msg) {
  if (cond)
    return;
  if (msg)
    far_print_str((char*)(intptr_t)msg);
  else
    fprintf(stderr, "assertion failed\n");
  far_stack_trace();
  exit(1);
}

#define FAR_TRY_MAX 64

#if defined(_MSC_VER)
#define FAR_TLS __declspec(thread)
#else
#define FAR_TLS __thread
#endif

FAR_TLS static struct {
  int64_t tag;
  int64_t value;
} g_try_stack[FAR_TRY_MAX];

FAR_TLS static int g_try_depth = 0;
FAR_TLS static int g_far_pending_throw = 0;

extern void far_store_caught(int64_t tag, int64_t value);

#if defined(_MSC_VER)
#define FAR_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define FAR_NOINLINE __attribute__((noinline))
#else
#define FAR_NOINLINE
#endif

FAR_NOINLINE void far_try_push(void) {
  if (g_try_depth >= FAR_TRY_MAX) {
    far_panic(0);
    return;
  }
  g_try_depth++;
}

FAR_NOINLINE void far_try_success(void) {
  if (g_try_depth > 0)
    g_try_depth--;
}

FAR_NOINLINE void far_throw(int64_t tag, int64_t value) {
  if (g_try_depth <= 0) {
    fprintf(stderr, "uncaught exception tag=%" PRId64 " value=%" PRId64 "\n", tag, value);
    far_stack_trace();
    exit(1);
  }
  int idx = g_try_depth - 1;
  g_try_stack[idx].tag = tag;
  g_try_stack[idx].value = value;
  far_store_caught(tag, value);
  g_far_pending_throw = 1;
}

FAR_NOINLINE int32_t far_pending_throw_active(void) {
  return g_far_pending_throw ? 1 : 0;
}

FAR_NOINLINE void far_clear_pending_throw(void) {
  g_far_pending_throw = 0;
}

FAR_NOINLINE void far_mark_pending_throw(void) {
  g_far_pending_throw = 1;
}

FAR_TLS static volatile int64_t g_far_caught_tag = 0;
FAR_TLS static volatile int64_t g_far_caught_value = 0;

void far_store_caught(int64_t tag, int64_t value) {
  g_far_caught_tag = tag;
  g_far_caught_value = value;
}

FAR_NOINLINE int32_t far_caught_matches(int64_t expected_tag) {
  return g_far_caught_tag == expected_tag ? 1 : 0;
}

FAR_NOINLINE int64_t far_caught_tag(void) {
  return g_far_caught_tag;
}

FAR_NOINLINE int64_t far_caught_value(void) {
  return g_far_caught_value;
}

typedef struct {
  int64_t value;
} FarBoxedI64;

#define FAR_OK_BOXED_TAG ((uint64_t)1 << 63)
#define FAR_ERR_BOXED_TAG ((uint64_t)1 << 62)
#define FAR_OK_INLINE_MAX ((int64_t)((1ULL << 61) - 1))

static int64_t option_inline_enc(int64_t v) {
  return (int64_t)(((uint64_t)v << 1) | 1u);
}

static int64_t option_inline_dec(int64_t enc) {
  return (int64_t)(((uint64_t)enc ^ 1u) >> 1);
}

static int ok_is_boxed(int64_t opt) {
  return ((uint64_t)opt & FAR_OK_BOXED_TAG) != 0;
}

static int option_is_inline(int64_t opt) {
  return (opt & 1) && !ok_is_boxed(opt) && !((uint64_t)opt & FAR_ERR_BOXED_TAG);
}

static int64_t result_err_inline_enc(int64_t v) {
  return (int64_t)((uint64_t)v << 1);
}

static int64_t result_err_inline_dec(int64_t enc) {
  return (int64_t)((uint64_t)enc >> 1);
}

static int err_is_boxed(int64_t res) {
  return ((uint64_t)res & FAR_ERR_BOXED_TAG) != 0;
}

static int result_err_is_inline(int64_t res) {
  return !(res & 1) && !err_is_boxed(res) && !ok_is_boxed(res);
}

static FarBoxedI64* boxed_i64_new(int64_t value) {
  FarBoxedI64* box = (FarBoxedI64*)malloc(sizeof(FarBoxedI64));
  if (!box)
    return NULL;
  box->value = value;
  return box;
}

static int64_t ok_boxed_handle(FarBoxedI64* box) {
  return (int64_t)(FAR_OK_BOXED_TAG | (uintptr_t)box);
}

static int64_t err_boxed_handle(FarBoxedI64* box) {
  return (int64_t)(FAR_ERR_BOXED_TAG | (uintptr_t)box);
}

static FarBoxedI64* ok_boxed_from_handle(int64_t handle) {
  return (FarBoxedI64*)(uintptr_t)((uint64_t)handle & ~FAR_OK_BOXED_TAG);
}

static FarBoxedI64* err_boxed_from_handle(int64_t handle) {
  return (FarBoxedI64*)(uintptr_t)((uint64_t)handle & ~FAR_ERR_BOXED_TAG);
}

int64_t far_option_some(int64_t value) {
  if (value >= 0) {
    int64_t enc = option_inline_enc(value);
    if ((uint64_t)enc <= (uint64_t)FAR_OK_INLINE_MAX)
      return enc;
  }
  FarBoxedI64* box = boxed_i64_new(value);
  if (!box)
    far_panic(0);
  return ok_boxed_handle(box);
}

int64_t far_option_none(void) { return 0; }

int64_t far_option_is_some(int64_t opt) { return option_is_inline(opt) || ok_is_boxed(opt); }

int64_t far_option_unwrap(int64_t opt) {
  if (!far_option_is_some(opt)) {
    if (g_try_depth > 0) {
      far_throw(FAR_EX_TAG_UNWRAP_NONE, 0);
      return 0;
    }
    fprintf(stderr, "unwrap on None\n");
    far_stack_trace();
    exit(1);
  }
  if (ok_is_boxed(opt))
    return ok_boxed_from_handle(opt)->value;
  return option_inline_dec(opt);
}

int64_t far_option_unwrap_or(int64_t opt, int64_t alt) {
  if (!far_option_is_some(opt))
    return alt;
  if (ok_is_boxed(opt))
    return ok_boxed_from_handle(opt)->value;
  return option_inline_dec(opt);
}

int64_t far_result_ok(int64_t value) {
  return far_option_some(value);
}

int64_t far_result_err(int64_t value) {
  if (value >= 0) {
    int64_t enc = result_err_inline_enc(value);
    if ((uint64_t)enc < FAR_ERR_BOXED_TAG && !(enc & 1))
      return enc;
  }
  FarBoxedI64* box = boxed_i64_new(value);
  if (!box)
    far_panic(0);
  return err_boxed_handle(box);
}

int64_t far_result_is_ok(int64_t res) { return option_is_inline(res) || ok_is_boxed(res); }

int64_t far_result_is_err(int64_t res) { return result_err_is_inline(res) || err_is_boxed(res); }

int64_t far_result_unwrap(int64_t res) {
  if (far_result_is_ok(res)) {
    if (ok_is_boxed(res))
      return ok_boxed_from_handle(res)->value;
    return option_inline_dec(res);
  }
  int64_t err = 0;
  if (err_is_boxed(res))
    err = err_boxed_from_handle(res)->value;
  else
    err = result_err_inline_dec(res);
  if (g_try_depth > 0) {
    far_throw(FAR_EX_TAG_UNWRAP_ERR, err);
    return 0;
  }
  fprintf(stderr, "unwrap on Err\n");
  far_stack_trace();
  exit(1);
}

int64_t far_result_unwrap_or(int64_t res, int64_t alt) {
  if (far_result_is_ok(res)) {
    if (ok_is_boxed(res))
      return ok_boxed_from_handle(res)->value;
    return option_inline_dec(res);
  }
  return alt;
}

int64_t far_result_ok_val(int64_t res) {
  if (!far_result_is_ok(res))
    return 0;
  if (ok_is_boxed(res))
    return ok_boxed_from_handle(res)->value;
  return option_inline_dec(res);
}

int64_t far_result_err_val(int64_t res) {
  if (!far_result_is_err(res))
    return 0;
  if (err_is_boxed(res))
    return err_boxed_from_handle(res)->value;
  return result_err_inline_dec(res);
}

static void far_global_lock(void) {
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
}

static void far_global_unlock(void) { pthread_mutex_unlock(&g_spawn_mu); }

#include "far_stdlib.c"
#include "far_science.c"
#include "far_net.c"
#include "far_modern.c"
#include "far_security.c"
#include "far_perf.c"
#include "far_io.c"

