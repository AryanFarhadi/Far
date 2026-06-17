/* Far scientific library runtime — included from far_rt.c */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FAR_SCI_FFT_MAX_N 4096
#define FAR_SCI_MAX_ARR (1 << 20)

extern int64_t far_tarray_new(int64_t len, int16_t tag, int64_t elem_sz);
extern int64_t far_tarray_len(int64_t handle);
extern int64_t far_tarray_get(int64_t handle, int64_t index);
extern void far_tarray_set(int64_t handle, int64_t index, int64_t value);

static double sci_arr_val(int64_t h, int64_t i) { return (double)far_tarray_get(h, i); }

static int64_t sci_arr_len(int64_t h) {
  if (!h)
    return 0;
  return far_tarray_len(h);
}

static int sci_is_pow2(int64_t n) { return n > 0 && (n & (n - 1)) == 0; }

static int sci_n_ok(int64_t n) { return n > 0 && n <= FAR_SCI_MAX_ARR; }

static int sci_cmp_i64(const void* a, const void* b) {
  int64_t av = *(const int64_t*)a;
  int64_t bv = *(const int64_t*)b;
  if (av < bv)
    return -1;
  if (av > bv)
    return 1;
  return 0;
}

/* --- statistics --- */

double far_sci_mean(int64_t arr) {
  int64_t n = sci_arr_len(arr);
  if (!sci_n_ok(n))
    return 0.0;
  double s = 0.0;
  for (int64_t i = 0; i < n; ++i)
    s += sci_arr_val(arr, i);
  return s / (double)n;
}

double far_sci_variance(int64_t arr) {
  int64_t n = sci_arr_len(arr);
  if (n < 2 || n > FAR_SCI_MAX_ARR)
    return 0.0;
  double m = far_sci_mean(arr);
  double s = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    double d = sci_arr_val(arr, i) - m;
    s += d * d;
  }
  return s / (double)(n - 1);
}

double far_sci_stddev(int64_t arr) { return sqrt(far_sci_variance(arr)); }

double far_sci_median(int64_t arr) {
  int64_t n = sci_arr_len(arr);
  if (n == 0)
    return 0.0;
  if (n > FAR_SCI_MAX_ARR)
    return 0.0;
  if ((uint64_t)n > (uint64_t)SIZE_MAX / sizeof(int64_t))
    return 0.0;
  int64_t* tmp = (int64_t*)malloc((size_t)n * sizeof(int64_t));
  if (!tmp)
    return 0.0;
  for (int64_t i = 0; i < n; ++i)
    tmp[i] = far_tarray_get(arr, i);
  qsort(tmp, (size_t)n, sizeof(int64_t), sci_cmp_i64);
  double med;
  if (n % 2 == 1)
    med = (double)tmp[n / 2];
  else
    med = ((double)tmp[n / 2 - 1] + (double)tmp[n / 2]) * 0.5;
  free(tmp);
  return med;
}

double far_sci_correlation(int64_t a, int64_t b) {
  int64_t n = sci_arr_len(a);
  if (n < 2 || n > FAR_SCI_MAX_ARR || n != sci_arr_len(b))
    return 0.0;
  double ma = far_sci_mean(a);
  double mb = far_sci_mean(b);
  double num = 0.0, da = 0.0, db = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    double xa = sci_arr_val(a, i) - ma;
    double xb = sci_arr_val(b, i) - mb;
    num += xa * xb;
    da += xa * xa;
    db += xb * xb;
  }
  if (da == 0.0 || db == 0.0)
    return 0.0;
  return num / sqrt(da * db);
}

double far_sci_min(int64_t arr) {
  int64_t n = sci_arr_len(arr);
  if (!sci_n_ok(n))
    return 0.0;
  double m = sci_arr_val(arr, 0);
  for (int64_t i = 1; i < n; ++i) {
    double v = sci_arr_val(arr, i);
    if (v < m)
      m = v;
  }
  return m;
}

double far_sci_max(int64_t arr) {
  int64_t n = sci_arr_len(arr);
  if (!sci_n_ok(n))
    return 0.0;
  double m = sci_arr_val(arr, 0);
  for (int64_t i = 1; i < n; ++i) {
    double v = sci_arr_val(arr, i);
    if (v > m)
      m = v;
  }
  return m;
}

/* --- FFT (radix-2 DFT magnitude, integer samples) --- */

static void sci_dft(const double* in, double* re, double* im, int64_t n) {
  for (int64_t k = 0; k < n; ++k) {
    double sr = 0.0, si = 0.0;
    for (int64_t t = 0; t < n; ++t) {
      double ang = -2.0 * 3.14159265358979323846 * (double)k * (double)t / (double)n;
      sr += in[t] * cos(ang);
      si += in[t] * sin(ang);
    }
    re[k] = sr;
    im[k] = si;
  }
}

int64_t far_sci_fft(int64_t input) {
  int64_t n = sci_arr_len(input);
  if (n <= 0)
    return 0;
  if (!sci_is_pow2(n))
    return 0;
  if (n > FAR_SCI_FFT_MAX_N)
    return 0;
  if ((uint64_t)n > (uint64_t)SIZE_MAX / sizeof(double))
    return 0;
  double* in = (double*)malloc((size_t)n * sizeof(double));
  double* re = (double*)malloc((size_t)n * sizeof(double));
  double* im = (double*)malloc((size_t)n * sizeof(double));
  if (!in || !re || !im) {
    free(in);
    free(re);
    free(im);
    return 0;
  }
  for (int64_t i = 0; i < n; ++i)
    in[i] = sci_arr_val(input, i);
  sci_dft(in, re, im, n);
  int64_t out = far_tarray_new(n, 0, 8);
  if (!out) {
    free(in);
    free(re);
    free(im);
    return 0;
  }
  for (int64_t i = 0; i < n; ++i) {
    double mag = sqrt(re[i] * re[i] + im[i] * im[i]);
    far_tarray_set(out, i, (int64_t)(mag + 0.5));
  }
  free(in);
  free(re);
  free(im);
  return out;
}

int64_t far_sci_ifft(int64_t input) {
  int64_t n = sci_arr_len(input);
  if (!sci_n_ok(n))
    return 0;
  int64_t out = far_tarray_new(n, 0, 8);
  if (!out)
    return 0;
  for (int64_t i = 0; i < n; ++i)
    far_tarray_set(out, i, far_tarray_get(input, i));
  return out;
}

/* --- optimization --- */

double far_sci_gradient_descent(double x, double lr, double grad) { return x - lr * grad; }

double far_sci_parabola_min(double a, double b, double c) {
  if (a == 0.0)
    return 0.0;
  return -b / (2.0 * a);
}

/* --- machine learning --- */

double far_sci_sigmoid(double x) { return 1.0 / (1.0 + exp(-x)); }

double far_sci_relu(double x) { return x > 0.0 ? x : 0.0; }

double far_sci_tanh(double x) { return tanh(x); }

int64_t far_sci_softmax(int64_t arr) {
  int64_t n = sci_arr_len(arr);
  if (n == 0)
    return 0;
  if (n > FAR_SCI_MAX_ARR)
    return 0;
  if ((uint64_t)n > (uint64_t)SIZE_MAX / sizeof(double))
    return 0;
  double* ex = (double*)malloc((size_t)n * sizeof(double));
  if (!ex)
    return 0;
  double maxv = sci_arr_val(arr, 0);
  for (int64_t i = 1; i < n; ++i) {
    double v = sci_arr_val(arr, i);
    if (v > maxv)
      maxv = v;
  }
  double sum = 0.0;
  for (int64_t i = 0; i < n; ++i) {
    ex[i] = exp(sci_arr_val(arr, i) - maxv);
    sum += ex[i];
  }
  if (sum == 0.0) {
    free(ex);
    return 0;
  }
  int64_t out = far_tarray_new(n, 0, 8);
  if (!out) {
    free(ex);
    return 0;
  }
  for (int64_t i = 0; i < n; ++i)
    far_tarray_set(out, i, (int64_t)((ex[i] / sum) * 1000.0 + 0.5));
  free(ex);
  return out;
}

double far_sci_dot(int64_t a, int64_t b) {
  int64_t n = sci_arr_len(a);
  if (n != sci_arr_len(b) || n > FAR_SCI_MAX_ARR)
    return 0.0;
  double s = 0.0;
  for (int64_t i = 0; i < n; ++i)
    s += sci_arr_val(a, i) * sci_arr_val(b, i);
  return s;
}

/* --- numerical methods --- */

double far_sci_trapz(int64_t arr, double h) {
  int64_t n = sci_arr_len(arr);
  if (n < 2 || n > FAR_SCI_MAX_ARR)
    return 0.0;
  double s = 0.0;
  for (int64_t i = 0; i < n - 1; ++i)
    s += (sci_arr_val(arr, i) + sci_arr_val(arr, i + 1)) * 0.5;
  return s * h;
}

double far_sci_simpson(int64_t arr, double h) {
  int64_t n = sci_arr_len(arr);
  if (n < 3 || (n % 2) == 0)
    return far_sci_trapz(arr, h);
  double s = sci_arr_val(arr, 0) + sci_arr_val(arr, n - 1);
  for (int64_t i = 1; i < n - 1; i += 2)
    s += 4.0 * sci_arr_val(arr, i);
  for (int64_t i = 2; i < n - 1; i += 2)
    s += 2.0 * sci_arr_val(arr, i);
  return s * h / 3.0;
}

int64_t far_sci_finite_diff(int64_t arr) {
  int64_t n = sci_arr_len(arr);
  if (n < 2 || n > FAR_SCI_MAX_ARR)
    return 0;
  int64_t out = far_tarray_new(n - 1, 0, 8);
  if (!out)
    return 0;
  for (int64_t i = 0; i < n - 1; ++i)
    far_tarray_set(out, i, far_tarray_get(arr, i + 1) - far_tarray_get(arr, i));
  return out;
}

int64_t far_sci_lerp_arr(int64_t arr, double t) {
  int64_t n = sci_arr_len(arr);
  if (!sci_n_ok(n))
    return 0;
  int64_t out = far_tarray_new(n, 0, 8);
  if (!out)
    return 0;
  for (int64_t i = 0; i < n; ++i) {
    double v = sci_arr_val(arr, i) * t;
    far_tarray_set(out, i, (int64_t)(v + 0.5));
  }
  return out;
}

/* --- physics --- */

double far_sci_kinetic_energy(double m, double v) { return 0.5 * m * v * v; }

double far_sci_potential_energy(double m, double g, double h) { return m * g * h; }

double far_sci_gravitational_force(double m1, double m2, double r) {
  const double G = 6.67430e-11;
  if (r == 0.0)
    return 0.0;
  return G * m1 * m2 / (r * r);
}

double far_sci_projectile_range(double v0, double angle_deg, double g) {
  if (fabs(g) < 1e-12)
    return 0.0;
  double rad = angle_deg * 3.14159265358979323846 / 180.0;
  return (v0 * v0 * sin(2.0 * rad)) / g;
}

double far_sci_hooke_force(double k, double x) { return -k * x; }

/* --- vectors / matrices --- */

double far_sci_v3_dot(double ax, double ay, double az, double bx, double by, double bz) {
  return ax * bx + ay * by + az * bz;
}

double far_sci_v3_norm(double x, double y, double z) { return sqrt(x * x + y * y + z * z); }

double far_sci_mat2_det(double m00, double m01, double m10, double m11) { return m00 * m11 - m01 * m10; }

double far_sci_mat2_trace(double m00, double m01, double m10, double m11) {
  (void)m01;
  (void)m10;
  return m00 + m11;
}
