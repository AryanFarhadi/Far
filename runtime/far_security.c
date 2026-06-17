/* Far security runtime — included from far_rt.c */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

extern char* far_xor_crypt(const char* data, const char* key);
extern char* far_hash_md5_hex(const char* data);
extern int64_t far_hash_fnv(const char* s);
extern int64_t far_rand_i64(void);
extern void far_rand_seed(int64_t seed);

static char* far_sec_strdup(const char* s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  if (n > FAR_STR_MAX || n >= SIZE_MAX)
    return NULL;
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

/* --- Memory safety --- */

typedef struct {
  int64_t size;
  int64_t alive;
  unsigned char tag;
} FarMemGuard;

static FarMemGuard g_mem_guards[128];
static int g_mem_guard_n = 0;

int64_t far_sec_mem_guard(int64_t size) {
  if (size <= 0 || size > (1 << 20))
    return -1;
  if (g_mem_guard_n >= 128)
    return -1;
  FarMemGuard* g = &g_mem_guards[g_mem_guard_n++];
  g->size = size;
  g->alive = 1;
  g->tag = (unsigned char)(0xA0 + (g_mem_guard_n & 0x0F));
  return g_mem_guard_n;
}

int64_t far_sec_mem_valid(int64_t guard) {
  if (guard <= 0 || guard > g_mem_guard_n)
    return 0;
  return g_mem_guards[guard - 1].alive ? 1 : 0;
}

int64_t far_sec_mem_scrub(int64_t guard) {
  if (guard <= 0 || guard > g_mem_guard_n)
    return -1;
  g_mem_guards[guard - 1].alive = 0;
  g_mem_guards[guard - 1].size = 0;
  return 0;
}

int64_t far_sec_mem_size(int64_t guard) {
  if (guard <= 0 || guard > g_mem_guard_n || !g_mem_guards[guard - 1].alive)
    return -1;
  return g_mem_guards[guard - 1].size;
}

/* --- Bounds checking --- */

int64_t far_sec_bounds_check(int64_t index, int64_t len) {
  return (index >= 0 && index < len) ? 1 : 0;
}

int64_t far_sec_bounds_slice(int64_t start, int64_t len, int64_t cap) {
  if (start < 0 || len < 0 || cap < 0)
    return 0;
  if (start > cap)
    return 0;
  int64_t end;
#if defined(__GNUC__) || defined(__clang__)
  if (__builtin_add_overflow(start, len, &end))
    return 0;
#else
  if (len > cap - start)
    return 0;
  end = start + len;
#endif
  if (end > cap)
    return 0;
  return 1;
}

int64_t far_sec_bounds_clamp(int64_t index, int64_t len) {
  if (len <= 0)
    return 0;
  if (index < 0)
    return 0;
  if (index >= len)
    return len - 1;
  return index;
}

/* --- Integer overflow checks --- */

static int g_sec_overflow = 0;

int64_t far_sec_i64_add_safe(int64_t a, int64_t b) {
  g_sec_overflow = 0;
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
    g_sec_overflow = 1;
    return 0;
  }
  return a + b;
}

int64_t far_sec_i64_mul_safe(int64_t a, int64_t b) {
  g_sec_overflow = 0;
  if (a != 0 && b != 0) {
    if (a == -1 && b == INT64_MIN) {
      g_sec_overflow = 1;
      return 0;
    }
    if (b == -1 && a == INT64_MIN) {
      g_sec_overflow = 1;
      return 0;
    }
    if ((a > 0 && b > 0 && a > INT64_MAX / b) || (a > 0 && b < 0 && b < INT64_MIN / a) ||
        (a < 0 && b > 0 && a < INT64_MIN / b) || (a < 0 && b < 0 && a < INT64_MAX / b)) {
      g_sec_overflow = 1;
      return 0;
    }
  }
  return a * b;
}

int64_t far_sec_i64_sub_safe(int64_t a, int64_t b) {
  g_sec_overflow = 0;
  if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
    g_sec_overflow = 1;
    return 0;
  }
  return a - b;
}

int64_t far_sec_i64_overflowed(void) { return g_sec_overflow; }

/* --- Safe concurrency --- */

static int64_t g_sec_locks[64];
static int64_t g_sec_lock_owner[64];
static int g_sec_lock_n = 0;

static void far_sec_mu_lock(void) {
  far_spawn_mu_ensure();
  pthread_mutex_lock(&g_spawn_mu);
}

static void far_sec_mu_unlock(void) {
  pthread_mutex_unlock(&g_spawn_mu);
}

static int sec_lock_slot(int64_t id) {
  far_sec_mu_lock();
  for (int i = 0; i < g_sec_lock_n; ++i) {
    if (g_sec_locks[i] == id) {
      far_sec_mu_unlock();
      return i;
    }
  }
  if (g_sec_lock_n >= 64) {
    far_sec_mu_unlock();
    return -1;
  }
  g_sec_locks[g_sec_lock_n] = id;
  g_sec_lock_owner[g_sec_lock_n] = 0;
  int slot = g_sec_lock_n++;
  far_sec_mu_unlock();
  return slot;
}

int64_t far_sec_safe_lock(int64_t id) {
  for (;;) {
    far_sec_mu_lock();
    int slot = -1;
    for (int i = 0; i < g_sec_lock_n; ++i) {
      if (g_sec_locks[i] == id) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      if (g_sec_lock_n >= 64) {
        far_sec_mu_unlock();
        return -1;
      }
      slot = g_sec_lock_n++;
      g_sec_locks[slot] = id;
      g_sec_lock_owner[slot] = 0;
    }
    if (g_sec_lock_owner[slot] == 0) {
      g_sec_lock_owner[slot] = 1;
      far_sec_mu_unlock();
      return 1;
    }
    far_sec_mu_unlock();
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec ts = {0, 1000000};
    nanosleep(&ts, NULL);
#endif
  }
}

int64_t far_sec_safe_try_lock(int64_t id) {
  far_sec_mu_lock();
  int slot = -1;
  for (int i = 0; i < g_sec_lock_n; ++i) {
    if (g_sec_locks[i] == id) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (g_sec_lock_n >= 64) {
      far_sec_mu_unlock();
      return -1;
    }
    slot = g_sec_lock_n++;
    g_sec_locks[slot] = id;
    g_sec_lock_owner[slot] = 0;
  }
  if (g_sec_lock_owner[slot] != 0) {
    far_sec_mu_unlock();
    return 0;
  }
  g_sec_lock_owner[slot] = 1;
  far_sec_mu_unlock();
  return 1;
}

int64_t far_sec_safe_unlock(int64_t id) {
  far_sec_mu_lock();
  for (int i = 0; i < g_sec_lock_n; ++i) {
    if (g_sec_locks[i] == id) {
      g_sec_lock_owner[i] = 0;
      far_sec_mu_unlock();
      return 1;
    }
  }
  far_sec_mu_unlock();
  return 0;
}

int64_t far_sec_safe_owned(int64_t id) {
  far_sec_mu_lock();
  int64_t owned = 0;
  for (int i = 0; i < g_sec_lock_n; ++i) {
    if (g_sec_locks[i] == id) {
      owned = g_sec_lock_owner[i] ? 1 : 0;
      break;
    }
  }
  far_sec_mu_unlock();
  return owned;
}

/* --- Permission system --- */

static int64_t g_sec_perms[64];
static int64_t g_sec_perm_mask[64];
static int g_sec_perm_n = 0;

int64_t far_sec_perm_grant(int64_t actor, int64_t perm) {
  far_sec_mu_lock();
  int slot = -1;
  for (int i = 0; i < g_sec_perm_n; ++i) {
    if (g_sec_perms[i] == actor) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (g_sec_perm_n >= 64) {
      far_sec_mu_unlock();
      return -1;
    }
    slot = g_sec_perm_n++;
    g_sec_perms[slot] = actor;
    g_sec_perm_mask[slot] = 0;
  }
  g_sec_perm_mask[slot] |= perm;
  int64_t mask = g_sec_perm_mask[slot];
  far_sec_mu_unlock();
  return mask;
}

int64_t far_sec_perm_revoke(int64_t actor, int64_t perm) {
  far_sec_mu_lock();
  int slot = -1;
  for (int i = 0; i < g_sec_perm_n; ++i) {
    if (g_sec_perms[i] == actor) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    far_sec_mu_unlock();
    return -1;
  }
  g_sec_perm_mask[slot] &= ~perm;
  int64_t mask = g_sec_perm_mask[slot];
  far_sec_mu_unlock();
  return mask;
}

int64_t far_sec_perm_check(int64_t actor, int64_t perm) {
  far_sec_mu_lock();
  for (int i = 0; i < g_sec_perm_n; ++i) {
    if (g_sec_perms[i] == actor) {
      int64_t ok = (g_sec_perm_mask[i] & perm) == perm ? 1 : 0;
      far_sec_mu_unlock();
      return ok;
    }
  }
  far_sec_mu_unlock();
  return 0;
}

int64_t far_sec_perm_bits(int64_t actor) {
  far_sec_mu_lock();
  for (int i = 0; i < g_sec_perm_n; ++i) {
    if (g_sec_perms[i] == actor) {
      int64_t bits = g_sec_perm_mask[i];
      far_sec_mu_unlock();
      return bits;
    }
  }
  far_sec_mu_unlock();
  return 0;
}

/* --- Sandboxing --- */

static int64_t g_sec_sandbox_level = 0;
static char g_sec_sandbox_paths[16][256];
static int g_sec_sandbox_path_n = 0;

int64_t far_sec_sandbox_enter(int64_t level) {
  g_sec_sandbox_level = level > 0 ? level : 1;
  return g_sec_sandbox_level;
}

int64_t far_sec_sandbox_exit(void) {
  g_sec_sandbox_level = 0;
  g_sec_sandbox_path_n = 0;
  return 0;
}

int64_t far_sec_sandbox_active(void) { return g_sec_sandbox_level > 0 ? 1 : 0; }

int64_t far_sec_sandbox_allow(const char* path) {
  if (!path || !path[0] || g_sec_sandbox_path_n >= 16)
    return -1;
  if (strlen(path) >= sizeof(g_sec_sandbox_paths[0]))
    return -1;
  snprintf(g_sec_sandbox_paths[g_sec_sandbox_path_n], sizeof(g_sec_sandbox_paths[0]), "%s", path);
  ++g_sec_sandbox_path_n;
  return g_sec_sandbox_path_n;
}

int64_t far_sec_sandbox_can(const char* path) {
  if (!g_sec_sandbox_level)
    return 1;
  if (!path)
    return 0;
  for (int i = 0; i < g_sec_sandbox_path_n; ++i) {
    const char* allowed = g_sec_sandbox_paths[i];
    size_t n = strlen(allowed);
    if (strncmp(path, allowed, n) == 0 && (path[n] == '\0' || path[n] == '/' || path[n] == '\\'))
      return 1;
  }
  return 0;
}

/* --- Cryptography API --- */

char* far_sec_crypto_digest(const char* data) { return far_hash_md5_hex(data); }

char* far_sec_crypto_encrypt(const char* data, const char* key) { return far_xor_crypt(data, key); }

int64_t far_sec_crypto_verify(const char* a, const char* b) {
  if (!a || !b)
    return 0;
  size_t la = strlen(a);
  if (la != strlen(b))
    return 0;
  unsigned char diff = 0;
  for (size_t i = 0; i < la; ++i)
    diff |= (unsigned char)(a[i] ^ b[i]);
  return diff == 0 ? 1 : 0;
}

char* far_sec_crypto_token(int64_t nbytes) {
  if (nbytes <= 0)
    nbytes = 16;
  if (nbytes > 64)
    nbytes = 64;
  char* out = (char*)malloc((size_t)nbytes * 2 + 1);
  if (!out)
    return NULL;
  size_t p = 0;
  for (int64_t i = 0; i < nbytes; ++i) {
    unsigned v = (unsigned)(far_hash_fnv("tok") ^ far_rand_i64()) & 0xFFu;
    snprintf(out + p, 3, "%02x", v);
    p += 2;
  }
  out[p] = '\0';
  return out;
}
