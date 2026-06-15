/* Far standard library runtime — included from far_rt.c */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static uint32_t g_rand_state = 123456789u;

static char* far_strdup(const char* s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

static int64_t far_ptr_to_i64(const char* p) { return (int64_t)(intptr_t)p; }

static char* far_i64_to_ptr(int64_t v) { return (char*)(intptr_t)v; }

/* --- random --- */

void far_rand_seed(int64_t seed) {
  far_global_lock();
  g_rand_state = (uint32_t)seed ^ 0x9e3779b9u;
  far_global_unlock();
}

int64_t far_rand_i64(void) {
  far_global_lock();
  g_rand_state ^= g_rand_state << 13;
  g_rand_state ^= g_rand_state >> 17;
  g_rand_state ^= g_rand_state << 5;
  int64_t out = (int64_t)(g_rand_state & 0x7fffffff);
  far_global_unlock();
  return out;
}

double far_rand_f64(void) { return (double)far_rand_i64() / 2147483647.0; }

int64_t far_rand_range(int64_t lo, int64_t hi) {
  if (hi < lo) {
    int64_t t = lo;
    lo = hi;
    hi = t;
  }
  if (hi == lo)
    return lo;
  uint64_t span = (uint64_t)hi - (uint64_t)lo + 1ULL;
  if (span == 0 || span > (uint64_t)INT64_MAX)
    return lo;
  int64_t mod = (int64_t)span;
  if (mod <= 0)
    return lo;
  return lo + far_rand_i64() % mod;
}

/* --- time --- */

int64_t far_now_ms(void) {
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return (int64_t)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

int64_t far_now_sec(void) { return far_now_ms() / 1000; }

int64_t far_clock_ticks(void) {
#ifdef _WIN32
  return (int64_t)GetTickCount64();
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* --- date (UTC from epoch ms) --- */

static void far_split_ms(int64_t ms, int* y, int* mo, int* d, int* h, int* mi, int* s) {
  time_t sec = (time_t)(ms / 1000);
  struct tm tmv;
#ifdef _WIN32
  gmtime_s(&tmv, &sec);
#else
  gmtime_r(&sec, &tmv);
#endif
  *y = tmv.tm_year + 1900;
  *mo = tmv.tm_mon + 1;
  *d = tmv.tm_mday;
  *h = tmv.tm_hour;
  *mi = tmv.tm_min;
  *s = tmv.tm_sec;
}

int64_t far_date_year(int64_t ms) {
  int y, mo, d, h, mi, s;
  far_split_ms(ms, &y, &mo, &d, &h, &mi, &s);
  return y;
}
int64_t far_date_month(int64_t ms) {
  int y, mo, d, h, mi, s;
  far_split_ms(ms, &y, &mo, &d, &h, &mi, &s);
  return mo;
}
int64_t far_date_day(int64_t ms) {
  int y, mo, d, h, mi, s;
  far_split_ms(ms, &y, &mo, &d, &h, &mi, &s);
  return d;
}
int64_t far_date_hour(int64_t ms) {
  int y, mo, d, h, mi, s;
  far_split_ms(ms, &y, &mo, &d, &h, &mi, &s);
  return h;
}
int64_t far_date_minute(int64_t ms) {
  int y, mo, d, h, mi, s;
  far_split_ms(ms, &y, &mo, &d, &h, &mi, &s);
  return mi;
}
int64_t far_date_second(int64_t ms) {
  int y, mo, d, h, mi, s;
  far_split_ms(ms, &y, &mo, &d, &h, &mi, &s);
  return s;
}

/* --- filesystem --- */

extern int64_t far_sec_sandbox_active(void);
extern int64_t far_sec_sandbox_can(const char* path);

static int far_fs_allowed(const char* path) {
  if (!path)
    return 0;
  if (far_sec_sandbox_active() && !far_sec_sandbox_can(path))
    return 0;
  return 1;
}

char* far_fs_read(const char* path) {
  if (!far_fs_allowed(path))
    return NULL;
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
#if defined(_WIN32)
  if (_fseeki64(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  __int64 sz = _ftelli64(f);
#else
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
#endif
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  if ((unsigned long long)sz > (unsigned long long)SIZE_MAX - 1) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char* buf = (char*)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return NULL;
  }
  buf[sz] = '\0';
  fclose(f);
  return buf;
}

int64_t far_fs_write(const char* path, const char* content) {
  if (!path)
    return -1;
  if (!far_fs_allowed(path))
    return -1;
  FILE* f = fopen(path, "wb");
  if (!f)
    return -1;
  if (content) {
    size_t n = strlen(content);
    if (fwrite(content, 1, n, f) != n) {
      fclose(f);
      return -1;
    }
  }
  fclose(f);
  return 0;
}

int64_t far_fs_exists(const char* path) {
  if (!path)
    return 0;
  if (!far_fs_allowed(path))
    return 0;
#ifdef _WIN32
  return _access(path, 0) == 0 ? 1 : 0;
#else
  return access(path, F_OK) == 0 ? 1 : 0;
#endif
}

int64_t far_fs_is_file(const char* path) {
#ifdef _WIN32
  struct _stat st;
  if (_stat(path, &st) != 0)
    return 0;
  return (st.st_mode & _S_IFREG) ? 1 : 0;
#else
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

int64_t far_fs_is_dir(const char* path) {
#ifdef _WIN32
  struct _stat st;
  if (_stat(path, &st) != 0)
    return 0;
  return (st.st_mode & _S_IFDIR) ? 1 : 0;
#else
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int64_t far_fs_mkdir(const char* path) {
  if (!path)
    return -1;
  if (!far_fs_allowed(path))
    return -1;
#ifdef _WIN32
  return _mkdir(path) == 0 ? 0 : -1;
#else
  return mkdir(path, 0755) == 0 ? 0 : -1;
#endif
}

int64_t far_fs_remove(const char* path) {
  if (!path)
    return -1;
  if (!far_fs_allowed(path))
    return -1;
  return remove(path) == 0 ? 0 : -1;
}

/* --- networking --- */

#ifdef _WIN32
static int g_wsa_ok = 0;
static void far_net_ensure(void) {
  if (!g_wsa_ok) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
      g_wsa_ok = 1;
  }
}
#endif

int64_t far_net_connect(const char* host, int64_t port) {
  if (!host || port <= 0 || port > 65535)
    return -1;
#ifdef _WIN32
  far_net_ensure();
#endif
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%" PRId64, port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = NULL;
  if (getaddrinfo(host, portbuf, &hints, &res) != 0)
    return -1;
  int sock = -1;
  for (struct addrinfo* p = res; p; p = p->ai_next) {
    sock = (int)socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock < 0)
      continue;
    if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0)
      break;
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    sock = -1;
  }
  freeaddrinfo(res);
  return sock;
}

static int64_t far_net_send_all(int64_t sock, const char* data, size_t len) {
  if (sock < 0 || !data)
    return -1;
  size_t off = 0;
  while (off < len) {
#ifdef _WIN32
    int n = send((SOCKET)sock, data + off, (int)(len - off), 0);
#else
    ssize_t n = send((int)sock, data + off, len - off, 0);
#endif
    if (n <= 0)
      return -1;
    off += (size_t)n;
  }
  return (int64_t)len;
}

int64_t far_net_send(int64_t sock, const char* data) {
  if (sock < 0 || !data)
    return -1;
  return far_net_send_all(sock, data, strlen(data));
}

char* far_net_recv(int64_t sock, int64_t max) {
  if (sock < 0 || max <= 0)
    return NULL;
  size_t cap = (size_t)max;
  char* buf = (char*)malloc(cap + 1);
  if (!buf)
    return NULL;
#ifdef _WIN32
  int n = recv((SOCKET)sock, buf, (int)cap, 0);
#else
  ssize_t n = recv((int)sock, buf, cap, 0);
#endif
  if (n <= 0) {
    free(buf);
    return NULL;
  }
  buf[n] = '\0';
  return buf;
}

int64_t far_net_close(int64_t sock) {
  if (sock < 0)
    return -1;
#ifdef _WIN32
  return closesocket((SOCKET)sock) == 0 ? 0 : -1;
#else
  return close((int)sock) == 0 ? 0 : -1;
#endif
}

/* --- json (minimal) --- */

static void far_stdlib_json_write_escaped(char* out, const char* data) {
  for (const char* p = data; *p; ++p) {
    switch (*p) {
      case '"':
        *out++ = '\\';
        *out++ = '"';
        break;
      case '\\':
        *out++ = '\\';
        *out++ = '\\';
        break;
      case '\b':
        *out++ = '\\';
        *out++ = 'b';
        break;
      case '\f':
        *out++ = '\\';
        *out++ = 'f';
        break;
      case '\n':
        *out++ = '\\';
        *out++ = 'n';
        break;
      case '\r':
        *out++ = '\\';
        *out++ = 'r';
        break;
      case '\t':
        *out++ = '\\';
        *out++ = 't';
        break;
      default:
        if ((unsigned char)*p < 0x20)
          out += sprintf(out, "\\u%04x", (unsigned char)*p);
        else
          *out++ = *p;
        break;
    }
  }
  *out = '\0';
}

static size_t far_stdlib_json_escaped_len(const char* data) {
  size_t n = 2;
  if (!data)
    return n;
  for (const char* p = data; *p; ++p) {
    switch (*p) {
      case '"':
      case '\\':
        n += 2;
        break;
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        n += 2;
        break;
      default:
        if ((unsigned char)*p < 0x20)
          n += 6;
        else
          n += 1;
        break;
    }
  }
  return n;
}

char* far_json_escape(const char* s) {
  if (!s)
    return far_strdup("\"\"");
  size_t cap = far_stdlib_json_escaped_len(s) + 1;
  char* out = (char*)malloc(cap);
  if (!out)
    return NULL;
  char* p = out;
  *p++ = '"';
  far_stdlib_json_write_escaped(p, s);
  p += strlen(p);
  *p++ = '"';
  *p = '\0';
  return out;
}

char* far_json_stringify_i64(int64_t v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%" PRId64, v);
  return far_strdup(buf);
}

char* far_json_stringify_str(const char* s) { return far_json_escape(s); }

static const char* far_json_find_key(const char* json, const char* key) {
  if (!json || !key)
    return NULL;
  size_t klen = strlen(key);
  const char* p = json;
  while (*p) {
    if (*p == '"') {
      const char* ks = p + 1;
      const char* ke = ks;
      while (*ke && *ke != '"') {
        if (*ke == '\\' && ke[1])
          ke += 2;
        else
          ++ke;
      }
      if ((size_t)(ke - ks) == klen && strncmp(ks, key, klen) == 0) {
        p = ke + 1;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
          ++p;
        if (*p == ':')
          return p + 1;
      }
      p = (*ke == '"') ? ke + 1 : ke;
      continue;
    }
    ++p;
  }
  return NULL;
}

int64_t far_json_get_i64(const char* json, const char* key) {
  const char* p = far_json_find_key(json, key);
  if (!p)
    return 0;
  while (*p == ' ' || *p == '\t')
    ++p;
  char* end = NULL;
  long long v = strtoll(p, &end, 10);
  if (end == p)
    return 0;
  while (*end == ' ' || *end == '\t')
    ++end;
  if (*end != 0 && *end != ',' && *end != '}' && *end != ']' && *end != '\n' && *end != '\r')
    return 0;
  return (int64_t)v;
}

static int far_json_hex4(const char* p, uint32_t* out) {
  *out = 0;
  for (int i = 0; i < 4; ++i) {
    char c = p[i];
    int v;
    if (c >= '0' && c <= '9')
      v = c - '0';
    else if (c >= 'a' && c <= 'f')
      v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      v = c - 'A' + 10;
    else
      return 0;
    *out = (*out << 4) | (uint32_t)v;
  }
  return 1;
}

static int far_json_append_bytes(char** out, size_t* cap, size_t* len, const char* bytes, size_t n) {
  if (*len + n >= *cap) {
    size_t nc = *cap * 2;
    while (*len + n >= nc)
      nc *= 2;
    char* bigger = (char*)realloc(*out, nc);
    if (!bigger)
      return 0;
    *out = bigger;
    *cap = nc;
  }
  memcpy(*out + *len, bytes, n);
  *len += n;
  return 1;
}

static int far_json_append_codepoint(char** out, size_t* cap, size_t* len, uint32_t cp) {
  char buf[4];
  size_t n = 0;
  if (cp <= 0x7F) {
    buf[0] = (char)cp;
    n = 1;
  } else if (cp <= 0x7FF) {
    buf[0] = (char)(0xC0 | (cp >> 6));
    buf[1] = (char)(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp <= 0xFFFF) {
    buf[0] = (char)(0xE0 | (cp >> 12));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    n = 4;
  }
  return far_json_append_bytes(out, cap, len, buf, n);
}

char* far_json_get_str(const char* json, const char* key) {
  const char* p = far_json_find_key(json, key);
  if (!p)
    return NULL;
  while (*p == ' ' || *p == '\t')
    ++p;
  if (*p != '"')
    return NULL;
  ++p;
  size_t cap = 64;
  size_t len = 0;
  char* out = (char*)malloc(cap);
  if (!out)
    return NULL;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) {
      ++p;
      if (*p == 'u' && p[1] && p[2] && p[3] && p[4]) {
        uint32_t cp = 0;
        if (far_json_hex4(p + 1, &cp)) {
          if (!far_json_append_codepoint(&out, &cap, &len, cp)) {
            free(out);
            return NULL;
          }
          p += 5;
          continue;
        }
      }
      char ch = *p;
      switch (ch) {
        case 'b':
          ch = '\b';
          break;
        case 'f':
          ch = '\f';
          break;
        case 'n':
          ch = '\n';
          break;
        case 'r':
          ch = '\r';
          break;
        case 't':
          ch = '\t';
          break;
        default:
          break;
      }
      if (!far_json_append_bytes(&out, &cap, &len, &ch, 1)) {
        free(out);
        return NULL;
      }
      ++p;
      continue;
    }
    if (!far_json_append_bytes(&out, &cap, &len, p, 1)) {
      free(out);
      return NULL;
    }
    ++p;
  }
  out[len] = '\0';
  return out;
}

/* --- xml --- */

char* far_xml_escape(const char* s) {
  if (!s)
    return far_strdup("");
  size_t cap = strlen(s) * 6 + 1;
  char* out = (char*)malloc(cap);
  if (!out)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; s[i]; ++i) {
    const char* rep = NULL;
    switch (s[i]) {
      case '&':
        rep = "&amp;";
        break;
      case '<':
        rep = "&lt;";
        break;
      case '>':
        rep = "&gt;";
        break;
      case '"':
        rep = "&quot;";
        break;
      default:
        out[j++] = s[i];
        continue;
    }
    size_t rl = strlen(rep);
    memcpy(out + j, rep, rl);
    j += rl;
  }
  out[j] = '\0';
  return out;
}

char* far_xml_tag(const char* name, const char* body) {
  if (!name)
    name = "";
  if (!body)
    body = "";
  char* en = far_xml_escape(body);
  size_t n = strlen(name) * 2 + (en ? strlen(en) : 0) + 5;
  char* out = (char*)malloc(n);
  if (!out) {
    free(en);
    return NULL;
  }
  snprintf(out, n, "<%s>%s</%s>", name, en ? en : "", name);
  free(en);
  return out;
}

char* far_xml_get_attr(const char* xml, const char* attr) {
  if (!xml || !attr)
    return NULL;
  size_t alen = strlen(attr);
  const char* p = xml;
  while ((p = strstr(p, attr)) != NULL) {
    if (p > xml) {
      char prev = p[-1];
      if (prev != ' ' && prev != '\t' && prev != '\n' && prev != '\r' && prev != '<')
        goto next_match;
    }
    if (p[alen] == '=' && p[alen + 1] == '"') {
      p += alen + 2;
      const char* end = strchr(p, '"');
      if (!end)
        return NULL;
      size_t n = (size_t)(end - p);
      char* out = (char*)malloc(n + 1);
      if (!out)
        return NULL;
      memcpy(out, p, n);
      out[n] = '\0';
      return out;
    }
  next_match:
    p += alen;
  }
  return NULL;
}

/* --- yaml --- */

char* far_yaml_get(const char* yaml, const char* key) {
  if (!yaml || !key)
    return NULL;
  size_t klen = strlen(key);
  const char* p = yaml;
  while (*p) {
    const char* line = p;
    while (*line == ' ' || *line == '\t')
      ++line;
    while (*p && *p != '\n' && *p != '\r')
      ++p;
    if ((size_t)(p - line) >= klen + 1 && strncmp(line, key, klen) == 0 && line[klen] == ':') {
      p = line + klen + 1;
      while (*p == ' ' || *p == '\t')
        ++p;
      const char* end = p;
      while (*end && *end != '\n' && *end != '\r')
        ++end;
      size_t n = (size_t)(end - p);
      char* out = (char*)malloc(n + 1);
      if (!out)
        return NULL;
      memcpy(out, p, n);
      out[n] = '\0';
      return out;
    }
    if (*p == '\r')
      ++p;
    if (*p == '\n')
      ++p;
  }
  return NULL;
}

/* --- csv --- */

int64_t far_csv_count(const char* line) {
  if (!line || !*line)
    return 0;
  int64_t n = 1;
  for (const char* p = line; *p; ++p)
    if (*p == ',')
      ++n;
  return n;
}

char* far_csv_field(const char* line, int64_t index) {
  if (!line || index < 0)
    return NULL;
  const char* start = line;
  int64_t field = 0;
  for (const char* p = line;; ++p) {
    if (*p == ',' || *p == '\0') {
      if (field == index) {
        size_t n = (size_t)(p - start);
        char* out = (char*)malloc(n + 1);
        if (!out)
          return NULL;
        memcpy(out, start, n);
        out[n] = '\0';
        return out;
      }
      if (*p == '\0')
        break;
      ++field;
      start = p + 1;
    }
  }
  return NULL;
}

/* --- logging --- */

void far_log_info(const char* msg) { fprintf(stdout, "[INFO] %s\n", msg ? msg : ""); }
void far_log_warn(const char* msg) { fprintf(stdout, "[WARN] %s\n", msg ? msg : ""); }
void far_log_error(const char* msg) { fprintf(stderr, "[ERROR] %s\n", msg ? msg : ""); }
void far_log_debug(const char* msg) { fprintf(stdout, "[DEBUG] %s\n", msg ? msg : ""); }

/* --- regex (glob: * and ?) --- */

static int far_glob_match(const char* pat, const char* text) {
  if (!pat || !text)
    return 0;
  if (!*pat)
    return *text == '\0';
  if (*pat == '*') {
  if (far_glob_match(pat + 1, text))
      return 1;
    return *text && far_glob_match(pat, text + 1);
  }
  if (*pat == '?' || *pat == *text)
    return far_glob_match(pat + 1, text + 1);
  return 0;
}

int64_t far_regex_match(const char* pattern, const char* text) {
  return far_glob_match(pattern, text) ? 1 : 0;
}

int64_t far_regex_find(const char* pattern, const char* text) {
  if (!pattern || !text)
    return -1;
  size_t len = strlen(text);
  for (size_t i = 0; i < len; ++i) {
    if (far_glob_match(pattern, text + i))
      return (int64_t)i;
  }
  return -1;
}

/* --- compression (RLE: byte 0xFF repeats next byte count times) --- */

char* far_compress_rle(const char* data) {
  if (!data)
    return far_strdup("");
  size_t in_len = strlen(data);
  char* out = (char*)malloc(in_len * 3 + 4);
  if (!out)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; i < in_len;) {
    unsigned char c = (unsigned char)data[i];
    size_t run = 1;
    while (i + run < in_len && (unsigned char)data[i + run] == c && run < 255)
      ++run;
    if (run >= 3 || c == 0xFF) {
      out[j++] = (char)0xFF;
      out[j++] = (char)c;
      out[j++] = (char)run;
    } else {
      for (size_t k = 0; k < run; ++k)
        out[j++] = (char)c;
    }
    i += run;
  }
  out[j] = '\0';
  return out;
}

char* far_decompress_rle(const char* data) {
  if (!data)
    return far_strdup("");
  size_t cap = strlen(data) * 86 + 1;
  char* out = (char*)malloc(cap);
  if (!out)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; data[i];) {
    if ((unsigned char)data[i] == 0xFF && data[i + 1] && data[i + 2]) {
      unsigned char c = (unsigned char)data[i + 1];
      int run = (unsigned char)data[i + 2];
      for (int k = 0; k < run; ++k)
        out[j++] = (char)c;
      i += 3;
    } else {
      out[j++] = data[i++];
    }
  }
  out[j] = '\0';
  return out;
}

/* --- encryption --- */

char* far_xor_crypt(const char* data, const char* key) {
  if (!data)
    return far_strdup("");
  if (!key || !*key)
    return far_strdup(data);
  size_t dl = strlen(data);
  size_t kl = strlen(key);
  char* out = (char*)malloc(dl + 1);
  if (!out)
    return NULL;
  for (size_t i = 0; i < dl; ++i)
    out[i] = (char)(data[i] ^ key[i % kl]);
  out[dl] = '\0';
  return out;
}

/* --- hashing --- */

int64_t far_hash_fnv(const char* s) {
  if (!s)
    return 0;
  uint64_t h = 14695981039346656037ULL;
  for (; *s; ++s) {
    h ^= (unsigned char)*s;
    h *= 1099511628211ULL;
  }
  return (int64_t)h;
}

int64_t far_hash_crc32(const char* s) {
  if (!s)
    return 0;
  uint32_t crc = 0xFFFFFFFFu;
  for (; *s; ++s) {
    crc ^= (unsigned char)*s;
    for (int k = 0; k < 8; ++k)
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
  }
  return (int64_t)(crc ^ 0xFFFFFFFFu);
}

char* far_hash_md5_hex(const char* s) {
  /* FNV-based 128-bit style hex digest (not cryptographic MD5) */
  uint64_t a = (uint64_t)far_hash_fnv(s);
  uint64_t b = (uint64_t)far_hash_crc32(s);
  char* out = (char*)malloc(33);
  if (!out)
    return NULL;
  snprintf(out, 33, "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
  return out;
}

/* --- process --- */

int64_t far_proc_run(const char* cmd) {
  if (!cmd)
    return -1;
  return (int64_t)system(cmd);
}

int64_t far_proc_pid(void) {
#ifdef _WIN32
  return (int64_t)GetCurrentProcessId();
#else
  return (int64_t)getpid();
#endif
}

/* --- environment --- */

char* far_env_get(const char* name) {
  if (!name)
    return NULL;
  const char* v = getenv(name);
  return v ? far_strdup(v) : NULL;
}

int64_t far_env_set(const char* name, const char* value) {
  if (!name)
    return -1;
#ifdef _WIN32
  return _putenv_s(name, value ? value : "") == 0 ? 0 : -1;
#else
  return setenv(name, value ? value : "", 1) == 0 ? 0 : -1;
#endif
}

int64_t far_env_has(const char* name) {
  if (!name)
    return 0;
  return getenv(name) ? 1 : 0;
}

/* --- cli args --- */

static int g_far_argc = 0;
static char** g_far_argv = NULL;

void far_args_init(int argc, char** argv) {
  g_far_argc = argc;
  g_far_argv = argv;
}

int64_t far_args_count(void) {
#ifdef _WIN32
  if (g_far_argc > 0)
    return (int64_t)g_far_argc;
  return (int64_t)__argc;
#else
  if (g_far_argc > 0)
    return (int64_t)g_far_argc;
  return 0;
#endif
}

char* far_args_get(int64_t index) {
  if (index < 0)
    return NULL;
#ifdef _WIN32
  if (g_far_argc > 0 && g_far_argv) {
    if (index >= g_far_argc)
      return NULL;
    return far_strdup(g_far_argv[index]);
  }
  if (index >= __argc || !__argv)
    return NULL;
  return far_strdup(__argv[index]);
#else
  if (!g_far_argv || index >= g_far_argc)
    return NULL;
  return far_strdup(g_far_argv[index]);
#endif
}

/* --- benchmarking --- */

int64_t far_bench_now(void) { return far_clock_ticks(); }

int64_t far_bench_elapsed_ms(int64_t start) {
  int64_t now = far_clock_ticks();
  return now - start;
}
