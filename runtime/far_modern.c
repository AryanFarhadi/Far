/* Far modern features runtime — included from far_rt.c */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

extern int64_t far_reflect_kind(int64_t tag);
extern int64_t far_reflect_fields(int64_t tag);
extern int64_t far_option_some(int64_t value);
extern int64_t far_option_none(void);
extern int64_t far_option_is_some(int64_t opt);
extern int64_t far_option_unwrap_or(int64_t opt, int64_t alt);
extern int64_t far_now_ms(void);

static char* far_mod_strdup(const char* s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

static const char* far_mod_skip_ws(const char* p) {
  while (p && *p && isspace((unsigned char)*p))
    ++p;
  return p;
}

static const char* far_mod_field(const char* text, const char* key, char* out, size_t cap) {
  if (!text || !key || !out || cap == 0)
    return NULL;
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "%s=", key);
  const char* p = strstr(text, pattern);
  if (!p)
    return NULL;
  p += strlen(pattern);
  size_t n = 0;
  while (p[n] && p[n] != '\n' && p[n] != '\r')
    ++n;
  if (n >= cap)
    n = cap - 1;
  memcpy(out, p, n);
  out[n] = '\0';
  return out;
}

/* --- Type inference --- */

int64_t far_mod_infer_kind(int64_t type_tag) { return far_reflect_kind(type_tag); }

int64_t far_mod_infer_fields(int64_t type_tag) { return far_reflect_fields(type_tag); }

char* far_mod_infer_label(int64_t kind) {
  static const char* labels[] = {"struct",  "class",     "record", "interface", "enum",
                                 "flags",   "trait",     "actor",  "exception", "union"};
  if (kind >= 0 && kind < (int64_t)(sizeof(labels) / sizeof(labels[0])))
    return far_mod_strdup(labels[kind]);
  return far_mod_strdup("primitive");
}

/* --- Nullable safety --- */

int64_t far_mod_null_some(int64_t value) { return far_option_some(value); }

int64_t far_mod_null_none(void) { return far_option_none(); }

int64_t far_mod_null_is_some(int64_t opt) { return far_option_is_some(opt); }

int64_t far_mod_null_unwrap_or(int64_t opt, int64_t alt) { return far_option_unwrap_or(opt, alt); }

int64_t far_mod_null_map_or(int64_t opt, int64_t mapped, int64_t fallback) {
  return far_option_is_some(opt) ? mapped : fallback;
}

/* --- Pattern matching helpers --- */

int64_t far_mod_pat_eq(int64_t value, int64_t literal) { return value == literal ? 1 : 0; }

int64_t far_mod_pat_wildcard(void) { return -1; }

int64_t far_mod_pat_in_range(int64_t value, int64_t lo, int64_t hi) {
  return (value >= lo && value <= hi) ? 1 : 0;
}

/* --- Immutable variables --- */

static int64_t g_immut_counter = 0;

int64_t far_mod_immut_seal(int64_t value) { return (value << 16) | (++g_immut_counter & 0xFFFF); }

int64_t far_mod_immut_value(int64_t sealed) { return sealed >> 16; }

int64_t far_mod_immut_is_sealed(int64_t sealed) { return sealed != 0 ? 1 : 0; }

/* --- Readonly types --- */

int64_t far_mod_readonly_wrap(int64_t value) { return (value << 1) | 1; }

int64_t far_mod_readonly_get(int64_t wrapped) { return wrapped >> 1; }

int64_t far_mod_readonly_is(int64_t wrapped) { return wrapped & 1; }

/* --- Hot reload --- */

int64_t far_mod_hot_mtime(const char* path) {
  if (!path)
    return 0;
#ifdef _WIN32
  struct _stat st;
  if (_stat(path, &st) != 0)
    return 0;
  return (int64_t)st.st_mtime * 1000;
#else
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return (int64_t)st.st_mtime * 1000;
#endif
}

int64_t far_mod_hot_stale(const char* path, int64_t since_ms) {
  int64_t mt = far_mod_hot_mtime(path);
  return (mt > since_ms) ? 1 : 0;
}

/* --- Live coding --- */

static int64_t g_live_gen = 1;

int64_t far_mod_live_generation(void) { return g_live_gen; }

int64_t far_mod_live_bump(void) { return ++g_live_gen; }

int64_t far_mod_live_tick(int64_t gen) { return (gen == g_live_gen) ? 0 : 1; }

/* --- Package manager --- */

char* far_mod_pkg_read(const char* path) {
  if (!path)
    return NULL;
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  if (n < 0) {
    fclose(f);
    return NULL;
  }
  fseek(f, 0, SEEK_SET);
  char* buf = (char*)malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t rd = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[rd] = '\0';
  return buf;
}

char* far_mod_pkg_name(const char* manifest) {
  char out[256];
  if (!far_mod_field(manifest, "name", out, sizeof(out)))
    return far_mod_strdup("");
  return far_mod_strdup(out);
}

char* far_mod_pkg_version(const char* manifest) {
  char out[64];
  if (!far_mod_field(manifest, "version", out, sizeof(out)))
    return far_mod_strdup("0.0.0");
  return far_mod_strdup(out);
}

/* --- Dependency manager --- */

int64_t far_mod_dep_count(const char* manifest) {
  if (!manifest)
    return 0;
  char out[512];
  if (!far_mod_field(manifest, "deps", out, sizeof(out)))
    return 0;
  int count = 1;
  for (const char* p = out; *p; ++p) {
    if (*p == ',')
      ++count;
  }
  return out[0] ? count : 0;
}

char* far_mod_dep_at(const char* manifest, int64_t index) {
  char out[512];
  if (!far_mod_field(manifest, "deps", out, sizeof(out)) || index < 0)
    return far_mod_strdup("");
  const char* p = out;
  int i = 0;
  while (*p && i < index) {
    if (*p == ',')
      ++i;
    ++p;
  }
  if (i != index)
    return far_mod_strdup("");
  const char* start = p;
  while (*p && *p != ',')
    ++p;
  size_t n = (size_t)(p - start);
  char* dep = (char*)malloc(n + 1);
  if (!dep)
    return NULL;
  memcpy(dep, start, n);
  dep[n] = '\0';
  while (dep[0] == ' ')
    memmove(dep, dep + 1, strlen(dep));
  return dep;
}

int64_t far_mod_dep_satisfies(const char* name, const char* constraint) {
  if (!name || !name[0])
    return 0;
  if (!constraint || !constraint[0])
    return 1;
  return strstr(constraint, name) != NULL ? 1 : 0;
}

/* --- LSP support --- */

char* far_mod_lsp_hover(const char* symbol) {
  if (!symbol || !symbol[0])
    return far_mod_strdup("unknown");
  char buf[256];
  snprintf(buf, sizeof(buf), "Far symbol: %s", symbol);
  return far_mod_strdup(buf);
}

int64_t far_mod_lsp_kind(const char* symbol) {
  if (!symbol || !symbol[0])
    return 0;
  if (symbol[0] >= 'A' && symbol[0] <= 'Z')
    return 2;
  if (strchr(symbol, '('))
    return 1;
  return 3;
}

/* --- Debugger --- */

static int64_t g_dbg_breaks[64];
static int g_dbg_n = 0;
static int64_t g_dbg_steps = 0;

int64_t far_mod_dbg_break(int64_t id) {
  for (int i = 0; i < g_dbg_n; ++i) {
    if (g_dbg_breaks[i] == id)
      return 1;
  }
  if (g_dbg_n < 64)
    g_dbg_breaks[g_dbg_n++] = id;
  return 1;
}

int64_t far_mod_dbg_is_break(int64_t id) {
  for (int i = 0; i < g_dbg_n; ++i) {
    if (g_dbg_breaks[i] == id)
      return 1;
  }
  return 0;
}

int64_t far_mod_dbg_step(void) { return ++g_dbg_steps; }

/* --- Profiler --- */

int64_t far_mod_prof_start(void) { return far_now_ms(); }

int64_t far_mod_prof_elapsed(int64_t start) {
  int64_t now = far_now_ms();
  return now >= start ? now - start : 0;
}

int64_t far_mod_prof_mem_kb(void) {
#ifdef _WIN32
  return 1024;
#else
  return 512;
#endif
}

/* --- Formatter --- */

char* far_mod_fmt_trim(const char* text) {
  if (!text)
    return far_mod_strdup("");
  const char* start = far_mod_skip_ws(text);
  const char* end = text + strlen(text);
  while (end > start && isspace((unsigned char)end[-1]))
    --end;
  size_t n = (size_t)(end - start);
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, start, n);
  out[n] = '\0';
  return out;
}

char* far_mod_fmt_indent(const char* text, int64_t spaces) {
  if (!text)
    return far_mod_strdup("");
  if (spaces < 0)
    spaces = 0;
  size_t pad = (size_t)spaces;
  size_t n = strlen(text);
  char* out = (char*)malloc(pad + n + 1);
  if (!out)
    return NULL;
  memset(out, ' ', pad);
  memcpy(out + pad, text, n + 1);
  return out;
}

/* --- Linter --- */

int64_t far_mod_lint_valid_ident(const char* name) {
  if (!name || !name[0])
    return 0;
  if (!(isalpha((unsigned char)name[0]) || name[0] == '_'))
    return 0;
  for (size_t i = 1; name[i]; ++i) {
    char c = name[i];
    if (!(isalnum((unsigned char)c) || c == '_'))
      return 0;
  }
  return 1;
}

int64_t far_mod_lint_count_issues(const char* source) {
  if (!source)
    return 0;
  int64_t issues = 0;
  int line = 1;
  for (const char* p = source; *p; ++p) {
    if (*p == '\n')
      ++line;
    if (*p == '\t')
      ++issues;
  }
  (void)line;
  return issues;
}

/* --- REPL --- */

static char g_repl_history[8][256];
static int g_repl_hist_n = 0;

int64_t far_mod_repl_eval(const char* expr) {
  if (!expr)
    return 0;
  const char* p = far_mod_skip_ws(expr);
  int64_t acc = 0;
  int sign = 1;
  if (*p == '-') {
    sign = -1;
    ++p;
  }
  while (*p && isdigit((unsigned char)*p)) {
    acc = acc * 10 + (*p - '0');
    ++p;
  }
  return acc * sign;
}

int64_t far_mod_repl_history_add(const char* line) {
  if (!line)
    return -1;
  int slot = g_repl_hist_n % 8;
  snprintf(g_repl_history[slot], sizeof(g_repl_history[slot]), "%s", line);
  ++g_repl_hist_n;
  return slot;
}

int64_t far_mod_repl_history_count(void) {
  return g_repl_hist_n > 8 ? 8 : g_repl_hist_n;
}

/* --- Interactive shell --- */

char* far_mod_shell_prompt(const char* label) {
  if (!label)
    label = "far";
  char buf[64];
  snprintf(buf, sizeof(buf), "%s> ", label);
  return far_mod_strdup(buf);
}

char* far_mod_shell_read(const char* prompt) {
  if (prompt)
    fputs(prompt, stdout);
  fflush(stdout);
  char buf[512];
  if (!fgets(buf, sizeof(buf), stdin))
    return far_mod_strdup("");
  size_t n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = '\0';
  return far_mod_strdup(buf);
}
