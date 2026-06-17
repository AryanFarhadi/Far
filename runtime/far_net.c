/* Far networking runtime — included from far_rt.c */

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define FAR_NET_RECV_MAX ((size_t)16 * 1024 * 1024)
#define FAR_HTTP_MAX_RESP FAR_NET_RECV_MAX
#define FAR_NET_MSG_MAX FAR_NET_RECV_MAX
#define FAR_NET_CONNECT_MS 5000
#define FAR_NET_IO_MS 15000

static char* far_net_strdup(const char* s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  if (n > FAR_NET_MSG_MAX || n >= SIZE_MAX)
    return NULL;
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

static char* far_net_alloc_fmt(int need) {
  if (need < 0 || (size_t)need >= SIZE_MAX)
    return NULL;
  return (char*)malloc((size_t)need + 1);
}

static int far_net_fmt_matches(int written, int expected) {
  return written >= 0 && written == expected;
}

static int far_net_str_len_ok(const char* s) {
  if (!s)
    return 1;
  return strlen(s) <= FAR_NET_MSG_MAX;
}

#ifdef _WIN32
static int g_net_wsa = 0;
static void far_net_init(void) {
  if (!g_net_wsa) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
      g_net_wsa = 1;
  }
}
#define NET_SOCK SOCKET
#define NET_CLOSE closesocket
#define NET_INVALID INVALID_SOCKET
#else
static void far_net_init(void) {}
#define NET_SOCK int
#define NET_CLOSE close
#define NET_INVALID (-1)
#endif

typedef struct {
  char host[256];
  char path[512];
  int port;
  int https;
} FarUrl;

static int far_net_no_ctl(const char* s) {
  if (!s)
    return 0;
  for (const char* p = s; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c < 32 || c == 127)
      return 0;
  }
  return 1;
}

static int far_net_slice_no_ctl(const char* start, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)start[i];
    if (c < 32 || c == 127)
      return 0;
  }
  return 1;
}

static const char* far_url_authority_end(const char* p) {
  const char* end = p + strlen(p);
  const char* slash = strchr(p, '/');
  const char* qmark = strchr(p, '?');
  const char* hash = strchr(p, '#');
  if (slash && slash < end)
    end = slash;
  if (qmark && qmark < end)
    end = qmark;
  if (hash && hash < end)
    end = hash;
  return end;
}

static int far_net_host_ok(const char* host, size_t len) {
  if (!len || !far_net_slice_no_ctl(host, len))
    return 0;
  for (size_t i = 0; i < len; ++i) {
    char c = host[i];
    if (c == '@' || c == ' ' || c == '\t')
      return 0;
  }
  return 1;
}

static int far_net_host_str_ok(const char* host) {
  if (!host)
    return 0;
  size_t len = strlen(host);
  return len < 256 && far_net_host_ok(host, len);
}

static int far_parse_url(const char* url, FarUrl* out) {
  if (!url || !out)
    return 0;
  memset(out, 0, sizeof(*out));
  out->port = 80;
  const char* p = url;
  if (strncmp(p, "https://", 8) == 0) {
    out->https = 1;
    out->port = 443;
    p += 8;
  } else if (strncmp(p, "http://", 7) == 0) {
    p += 7;
  }
  const char* auth_end = far_url_authority_end(p);
  const char* colon = strchr(p, ':');
  if (colon && colon < auth_end) {
    size_t hlen = (size_t)(colon - p);
    if (hlen >= sizeof(out->host) || !far_net_host_ok(p, hlen))
      return 0;
    memcpy(out->host, p, hlen);
    out->host[hlen] = 0;
    const char* port_str = colon + 1;
    char* endptr = NULL;
    long port = strtol(port_str, &endptr, 10);
    while (endptr && *endptr == ' ')
      ++endptr;
    if (port_str == endptr || endptr != auth_end || port <= 0 || port > 65535)
      return 0;
    out->port = (int)port;
    p = auth_end;
  } else {
    size_t hlen = (size_t)(auth_end - p);
    if (hlen >= sizeof(out->host) || !far_net_host_ok(p, hlen))
      return 0;
    memcpy(out->host, p, hlen);
    out->host[hlen] = 0;
    p = auth_end;
  }
  if (*p == '/' || *p == '?' || *p == '#') {
    if (!far_net_no_ctl(p))
      return 0;
    int pw = snprintf(out->path, sizeof(out->path), "%s", p);
    if (pw < 0 || (size_t)pw >= sizeof(out->path))
      return 0;
  } else
    strcpy(out->path, "/");
  return out->host[0] != 0;
}

static int far_http_host_value(const FarUrl* u, char* out, size_t cap) {
  int default_port = u->https ? 443 : 80;
  int w = (u->port != default_port) ? snprintf(out, cap, "%s:%d", u->host, u->port)
                                    : snprintf(out, cap, "%s", u->host);
  return w >= 0 && (size_t)w < cap;
}

static int far_tcp_set_blocking(NET_SOCK sock, int blocking) {
#ifdef _WIN32
  u_long mode = blocking ? 0u : 1u;
  return ioctlsocket(sock, FIONBIO, &mode) == 0 ? 0 : -1;
#else
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0)
    return -1;
  int next = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  return fcntl(sock, F_SETFL, next) == 0 ? 0 : -1;
#endif
}

static int far_tcp_wait_connected(NET_SOCK sock) {
  fd_set wfds;
  FD_ZERO(&wfds);
  FD_SET(sock, &wfds);
  struct timeval tv;
  tv.tv_sec = FAR_NET_CONNECT_MS / 1000;
  tv.tv_usec = (FAR_NET_CONNECT_MS % 1000) * 1000;
#ifdef _WIN32
  if (select(0, NULL, &wfds, NULL, &tv) <= 0)
    return -1;
#else
  if (select((int)sock + 1, NULL, &wfds, NULL, &tv) <= 0)
    return -1;
#endif
  int soerr = 0;
#ifdef _WIN32
  int slen = sizeof(soerr);
#else
  socklen_t slen = sizeof(soerr);
#endif
  if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&soerr, &slen) != 0 || soerr != 0)
    return -1;
  return 0;
}

static void far_tcp_apply_timeouts(NET_SOCK sock) {
  if (sock == NET_INVALID)
    return;
#ifdef _WIN32
  DWORD ms = (DWORD)FAR_NET_IO_MS;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
  struct timeval tv;
  tv.tv_sec = FAR_NET_IO_MS / 1000;
  tv.tv_usec = (FAR_NET_IO_MS % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static int far_tcp_connect_finish(NET_SOCK sock) {
  if (far_tcp_set_blocking(sock, 1) != 0)
    return -1;
  far_tcp_apply_timeouts(sock);
  return 0;
}

static int far_tcp_connect_addr(NET_SOCK sock, const struct sockaddr* addr, int addrlen) {
  if (far_tcp_set_blocking(sock, 0) != 0)
    return -1;
#ifdef _WIN32
  if (connect(sock, addr, addrlen) == 0)
    return far_tcp_connect_finish(sock);
  int err = WSAGetLastError();
  if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
    far_tcp_set_blocking(sock, 1);
    return -1;
  }
#else
  if (connect(sock, addr, addrlen) == 0)
    return far_tcp_connect_finish(sock);
  if (errno != EINPROGRESS) {
    far_tcp_set_blocking(sock, 1);
    return -1;
  }
#endif
  if (far_tcp_wait_connected(sock) != 0) {
    far_tcp_set_blocking(sock, 1);
    return -1;
  }
  return far_tcp_connect_finish(sock);
}

static int64_t far_tcp_socket_connect(const char* host, int port) {
  if (!host || port <= 0 || port > 65535)
    return -1;
  if (!far_net_host_str_ok(host))
    return -1;
  far_net_init();
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = NULL;
  if (getaddrinfo(host, portbuf, &hints, &res) != 0)
    return -1;
  NET_SOCK sock = NET_INVALID;
  for (struct addrinfo* it = res; it; it = it->ai_next) {
    sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (sock == NET_INVALID)
      continue;
    if (far_tcp_connect_addr(sock, it->ai_addr, (int)it->ai_addrlen) != 0) {
      NET_CLOSE(sock);
      sock = NET_INVALID;
      continue;
    }
    break;
  }
  freeaddrinfo(res);
  return (int64_t)sock;
}

/* --- TCP --- */

int64_t far_tcp_connect(const char* host, int64_t port) {
  return far_tcp_socket_connect(host, (int)port);
}

int64_t far_tcp_listen(int64_t port) {
  if (port <= 0 || port > 65535)
    return -1;
  far_net_init();
  NET_SOCK sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == NET_INVALID)
    return -1;
  int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t)port);
  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    NET_CLOSE(sock);
    return -1;
  }
  if (listen(sock, 8) != 0) {
    NET_CLOSE(sock);
    return -1;
  }
  return (int64_t)sock;
}

int64_t far_tcp_accept(int64_t sock) {
  if (sock < 0)
    return -1;
  struct sockaddr_in cli;
  socklen_t len = sizeof(cli);
  NET_SOCK c = accept((NET_SOCK)sock, (struct sockaddr*)&cli, &len);
  return (int64_t)c;
}

static int64_t far_sock_send_all(int64_t sock, const void* data, size_t len) {
  if (sock < 0 || !data)
    return -1;
  size_t off = 0;
  while (off < len) {
    size_t remain = len - off;
#ifdef _WIN32
    size_t chunk = remain > (size_t)INT_MAX ? (size_t)INT_MAX : remain;
    int n = send((SOCKET)sock, (const char*)data + off, (int)chunk, 0);
#else
    size_t chunk = remain;
    if (chunk > (size_t)SSIZE_MAX)
      chunk = (size_t)SSIZE_MAX;
    ssize_t n = send((int)sock, (const char*)data + off, chunk, 0);
#endif
    if (n <= 0)
      return -1;
    off += (size_t)n;
  }
  return (int64_t)len;
}

int64_t far_tcp_send(int64_t sock, const char* data) {
  if (sock < 0 || !data)
    return -1;
  size_t n = strlen(data);
  if (n > FAR_NET_MSG_MAX)
    return -1;
  return far_sock_send_all(sock, data, n);
}

char* far_tcp_recv(int64_t sock, int64_t max) {
  if (sock < 0 || max <= 0)
    return NULL;
  if ((uint64_t)max > FAR_NET_RECV_MAX)
    return NULL;
  if ((uint64_t)max > (uint64_t)SIZE_MAX - 1u)
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

int64_t far_tcp_close(int64_t sock) {
  if (sock < 0)
    return -1;
  return NET_CLOSE((NET_SOCK)sock) == 0 ? 0 : -1;
}

/* --- UDP --- */

int64_t far_udp_bind(int64_t port) {
  if (port < 0 || port > 65535)
    return -1;
  far_net_init();
  NET_SOCK sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == NET_INVALID)
    return -1;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);
  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    NET_CLOSE(sock);
    return -1;
  }
  return (int64_t)sock;
}

int64_t far_udp_send(int64_t sock, const char* host, int64_t port, const char* data) {
  if (sock < 0 || !host || !data || port <= 0 || port > 65535)
    return -1;
  if (!far_net_host_str_ok(host))
    return -1;
  size_t dlen = strlen(data);
  if (dlen > FAR_NET_MSG_MAX)
    return -1;
  far_net_init();
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%d", (int)port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM;
  struct addrinfo* res = NULL;
  if (getaddrinfo(host, portbuf, &hints, &res) != 0)
    return -1;
  int64_t sent = -1;
  for (struct addrinfo* it = res; it; it = it->ai_next) {
#ifdef _WIN32
    int n = sendto((SOCKET)sock, data, (int)(dlen > (size_t)INT_MAX ? (size_t)INT_MAX : dlen), 0,
                   it->ai_addr, (int)it->ai_addrlen);
#else
    size_t chunk = dlen;
    if (chunk > (size_t)SSIZE_MAX)
      chunk = (size_t)SSIZE_MAX;
    ssize_t n = sendto((int)sock, data, chunk, 0, it->ai_addr, it->ai_addrlen);
#endif
    if (n >= 0) {
      sent = (int64_t)n;
      break;
    }
  }
  freeaddrinfo(res);
  return sent;
}

char* far_udp_recv(int64_t sock, int64_t max) {
  return far_tcp_recv(sock, max);
}

int64_t far_udp_close(int64_t sock) { return far_tcp_close(sock); }

/* --- HTTP helpers --- */

static char* far_http_exchange(const char* host, int port, const char* req) {
  int64_t sock = far_tcp_socket_connect(host, port);
  if (sock < 0)
    return NULL;
  if (far_tcp_send(sock, req) < 0) {
    far_tcp_close(sock);
    return NULL;
  }
  size_t cap = 4096;
  size_t len = 0;
  char* resp = (char*)malloc(cap);
  if (!resp) {
    far_tcp_close(sock);
    return NULL;
  }
  for (;;) {
    if (len + 256 >= cap) {
      if (cap >= FAR_HTTP_MAX_RESP) {
        free(resp);
        far_tcp_close(sock);
        return NULL;
      }
      if (cap > SIZE_MAX / 2) {
        free(resp);
        far_tcp_close(sock);
        return NULL;
      }
      size_t new_cap = cap * 2;
      if (new_cap > FAR_HTTP_MAX_RESP)
        new_cap = FAR_HTTP_MAX_RESP;
      char* n = (char*)realloc(resp, new_cap);
      if (!n) {
        free(resp);
        far_tcp_close(sock);
        return NULL;
      }
      cap = new_cap;
      resp = n;
    }
#ifdef _WIN32
    int n = recv((SOCKET)sock, resp + len, (int)(cap - len - 1), 0);
#else
    ssize_t n = recv((int)sock, resp + len, cap - len - 1, 0);
#endif
    if (n <= 0)
      break;
    len += (size_t)n;
    if (len >= FAR_HTTP_MAX_RESP)
      break;
  }
  resp[len] = '\0';
  far_tcp_close(sock);
  return resp;
}

static char* far_http_body(const char* response) {
  if (!response)
    return NULL;
  const char* sep = strstr(response, "\r\n\r\n");
  if (!sep)
    sep = strstr(response, "\n\n");
  if (!sep)
    return far_net_strdup(response);
  sep += (sep[0] == '\r') ? 4 : 2;
  return far_net_strdup(sep);
}

char* far_http_post_ct(const char* url, const char* body, const char* ctype);

static char* far_http_method_ct(const char* url, const char* method, const char* body, const char* ctype) {
  FarUrl u;
  if (!far_parse_url(url, &u) || u.https)
    return NULL;
  if (!body)
    body = "";
  if (!ctype)
    ctype = "text/plain";
  if (!method)
    method = "POST";
  if (!far_net_no_ctl(method))
    return NULL;
  size_t body_len = strlen(body);
  if (body_len > FAR_NET_MSG_MAX)
    return NULL;
  char host_hdr[520];
  if (!far_http_host_value(&u, host_hdr, sizeof(host_hdr)))
    return NULL;
  int need = snprintf(
      NULL, 0,
      "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: "
      "close\r\n\r\n%s",
      method, u.path, host_hdr, ctype, body_len, body);
  if (need < 0)
    return NULL;
  char* req = far_net_alloc_fmt(need);
  if (!req)
    return NULL;
  int w = snprintf(req, (size_t)need + 1,
           "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: "
           "close\r\n\r\n%s",
           method, u.path, host_hdr, ctype, body_len, body);
  if (!far_net_fmt_matches(w, need)) {
    free(req);
    return NULL;
  }
  char* resp = far_http_exchange(u.host, u.port, req);
  free(req);
  char* out = far_http_body(resp);
  free(resp);
  return out;
}

char* far_http_get(const char* url) {
  FarUrl u;
  if (!far_parse_url(url, &u) || u.https)
    return NULL;
  char host_hdr[520];
  if (!far_http_host_value(&u, host_hdr, sizeof(host_hdr)))
    return NULL;
  int need = snprintf(NULL, 0, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path,
                      host_hdr);
  if (need < 0)
    return NULL;
  char* req = far_net_alloc_fmt(need);
  if (!req)
    return NULL;
  int w = snprintf(req, (size_t)need + 1, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path,
           host_hdr);
  if (!far_net_fmt_matches(w, need)) {
    free(req);
    return NULL;
  }
  char* resp = far_http_exchange(u.host, u.port, req);
  free(req);
  char* body = far_http_body(resp);
  free(resp);
  return body;
}

char* far_http_post(const char* url, const char* body) {
  return far_http_post_ct(url, body, "application/json");
}

char* far_http_post_ct(const char* url, const char* body, const char* ctype) {
  return far_http_method_ct(url, "POST", body, ctype);
}

int64_t far_http_parse_status(const char* response) {
  if (!response)
    return 0;
  if (strncmp(response, "HTTP/", 5) != 0)
    return 0;
  const char* p = response + 5;
  while (*p && *p != ' ')
    p++;
  if (*p != ' ')
    return 0;
  ++p;
  while (*p == ' ')
    ++p;
  char* end = NULL;
  long code = strtol(p, &end, 10);
  if (end == p || code < 100 || code > 599)
    return 0;
  if (*end != 0 && *end != ' ' && *end != '\r' && *end != '\n')
    return 0;
  return (int64_t)code;
}

/* --- HTTPS (WinHTTP on Windows) --- */

#ifdef _WIN32
static wchar_t* far_to_wide(const char* s) {
  if (!s)
    return NULL;
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (n <= 0)
    return NULL;
  if ((size_t)n > SIZE_MAX / sizeof(wchar_t))
    return NULL;
  wchar_t* w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
  if (!w)
    return NULL;
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static char* far_https_win(const char* url, const char* method, const char* body, const char* ctype) {
  FarUrl u;
  if (!far_parse_url(url, &u))
    return NULL;
  if (!method)
    method = "GET";
  if (!far_net_no_ctl(method))
    return NULL;
  if (body && body[0] && strlen(body) > FAR_NET_MSG_MAX)
    return NULL;
  wchar_t* whost = far_to_wide(u.host);
  wchar_t* wpath = far_to_wide(u.path);
  if (!whost || !wpath) {
    free(whost);
    free(wpath);
    return NULL;
  }
  HINTERNET ses = WinHttpOpen(L"Far/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
  if (!ses) {
    free(whost);
    free(wpath);
    return NULL;
  }
  HINTERNET con = WinHttpConnect(ses, whost, (INTERNET_PORT)u.port, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    free(whost);
    free(wpath);
    return NULL;
  }
  wchar_t* wmethod = far_to_wide(method ? method : "GET");
  HINTERNET req = WinHttpOpenRequest(con, wmethod, wpath, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     u.https ? WINHTTP_FLAG_SECURE : 0);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    free(whost);
    free(wpath);
    free(wmethod);
    return NULL;
  }
  BOOL ok = TRUE;
  if (body && body[0]) {
    char hdr[256];
    const char* ct = ctype ? ctype : "application/json";
    if (!far_net_no_ctl(ct)) {
      WinHttpCloseHandle(req);
      WinHttpCloseHandle(con);
      WinHttpCloseHandle(ses);
      free(whost);
      free(wpath);
      free(wmethod);
      return NULL;
    }
    int hw = snprintf(hdr, sizeof(hdr), "Content-Type: %s\r\n", ct);
    if (hw < 0 || (size_t)hw >= sizeof(hdr)) {
      WinHttpCloseHandle(req);
      WinHttpCloseHandle(con);
      WinHttpCloseHandle(ses);
      free(whost);
      free(wpath);
      free(wmethod);
      return NULL;
    }
    wchar_t* whdr = far_to_wide(hdr);
    ok = WinHttpSendRequest(req, whdr, (DWORD)-1L, (LPVOID)body, (DWORD)strlen(body),
                            (DWORD)strlen(body), 0);
    free(whdr);
  } else {
    ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  }
  if (!ok || !WinHttpReceiveResponse(req, NULL)) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    free(whost);
    free(wpath);
    free(wmethod);
    return NULL;
  }
  size_t cap = 4096, len = 0;
  char* out = (char*)malloc(cap);
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
      break;
    if (len >= FAR_HTTP_MAX_RESP)
      break;
    if (len + avail + 1 >= cap) {
      if (cap >= FAR_HTTP_MAX_RESP) {
        free(out);
        out = NULL;
        break;
      }
      if (cap > SIZE_MAX / 2 || avail > SIZE_MAX - len - 1) {
        free(out);
        out = NULL;
        break;
      }
      size_t need = len + avail + 1;
      if (need > FAR_HTTP_MAX_RESP)
        need = FAR_HTTP_MAX_RESP;
      while (cap < need) {
        if (cap > SIZE_MAX / 2) {
          cap = need;
          break;
        }
        cap *= 2;
      }
      if (cap > FAR_HTTP_MAX_RESP)
        cap = FAR_HTTP_MAX_RESP;
      char* n = (char*)realloc(out, cap);
      if (!n) {
        free(out);
        out = NULL;
        break;
      }
      out = n;
    }
    DWORD read = 0;
    DWORD to_read = avail;
    if (len + to_read > FAR_HTTP_MAX_RESP)
      to_read = (DWORD)(FAR_HTTP_MAX_RESP - len);
    if (!WinHttpReadData(req, out + len, to_read, &read))
      break;
    len += read;
  }
  if (out)
    out[len] = '\0';
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  free(whost);
  free(wpath);
  free(wmethod);
  return out;
}
#endif

static char* far_https_socket(const char* url, const char* method, const char* body, const char* ctype) {
  FarUrl u;
  if (!far_parse_url(url, &u))
    return NULL;
  if (!method)
    method = "GET";
  if (!far_net_no_ctl(method))
    return NULL;
  if (!body)
    body = "";
  if (!ctype)
    ctype = "application/json";
  size_t body_len = strlen(body);
  if (body_len > FAR_NET_MSG_MAX)
    return NULL;
  char host_hdr[520];
  if (!far_http_host_value(&u, host_hdr, sizeof(host_hdr)))
    return NULL;
  int need = snprintf(
      NULL, 0,
      "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: "
      "close\r\n\r\n%s",
      method, u.path, host_hdr, ctype, body_len, body);
  if (need < 0)
    return NULL;
  char* req = far_net_alloc_fmt(need);
  if (!req)
    return NULL;
  int w = snprintf(req, (size_t)need + 1,
           "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: "
           "close\r\n\r\n%s",
           method, u.path, host_hdr, ctype, body_len, body);
  if (!far_net_fmt_matches(w, need)) {
    free(req);
    return NULL;
  }
  char* resp = far_http_exchange(u.host, u.port, req);
  free(req);
  char* out = far_http_body(resp);
  free(resp);
  return out;
}

char* far_https_get(const char* url) {
#ifdef _WIN32
  return far_https_win(url, "GET", NULL, NULL);
#else
  (void)url;
  return NULL;
#endif
}

char* far_https_post(const char* url, const char* body) {
#ifdef _WIN32
  return far_https_win(url, "POST", body, "application/json");
#else
  (void)url;
  (void)body;
  return NULL;
#endif
}

char* far_web_get(const char* url) {
  FarUrl u;
  if (!far_parse_url(url, &u))
    return NULL;
  if (u.https)
    return far_https_get(url);
  return far_http_get(url);
}

char* far_web_post(const char* url, const char* body) {
  FarUrl u;
  if (!far_parse_url(url, &u))
    return NULL;
  if (u.https)
    return far_https_post(url, body);
  return far_http_post(url, body);
}

/* --- WebSocket (client text frames) --- */

static void far_ws_make_key(char* out, size_t cap) {
  static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (size_t i = 0; i < 16 && i + 1 < cap; ++i)
    out[i] = b64[rand() % 64];
  out[16] = 0;
}

static int far_http_status_is(const char* resp, int code) {
  if (!resp || strncmp(resp, "HTTP/", 5) != 0)
    return 0;
  const char* p = strchr(resp, ' ');
  if (!p)
    return 0;
  ++p;
  while (*p == ' ')
    ++p;
  char* end = NULL;
  long got = strtol(p, &end, 10);
  if (end == p || got < 100 || got > 599)
    return 0;
  if (*end != 0 && *end != ' ' && *end != '\r' && *end != '\n')
    return 0;
  return got == code;
}

static char* far_recv_http_headers(int64_t sock);

int64_t far_ws_connect(const char* host, int64_t port, const char* path) {
  if (!host || !path)
    return -1;
  if (port <= 0 || port > 65535)
    return -1;
  if (!far_net_host_str_ok(host) || !far_net_no_ctl(path))
    return -1;
  int64_t sock = far_tcp_socket_connect(host, (int)(port > 0 ? port : 80));
  if (sock < 0)
    return -1;
  char key[24];
  far_ws_make_key(key, sizeof(key));
  char host_hdr[520];
  int hh = (port > 0 && port != 80) ? snprintf(host_hdr, sizeof(host_hdr), "%s:%d", host, (int)port)
                                    : snprintf(host_hdr, sizeof(host_hdr), "%s", host);
  if (hh < 0 || (size_t)hh >= sizeof(host_hdr)) {
    far_tcp_close(sock);
    return -1;
  }
  char req[1024];
  int req_len = snprintf(req, sizeof(req),
                         "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
                         "%s\r\nSec-WebSocket-Version: 13\r\n\r\n",
                         path, host_hdr, key);
  if (req_len < 0 || (size_t)req_len >= sizeof(req)) {
    far_tcp_close(sock);
    return -1;
  }
  if (far_tcp_send(sock, req) < 0) {
    far_tcp_close(sock);
    return -1;
  }
  char* resp = far_recv_http_headers(sock);
  if (!resp || !far_http_status_is(resp, 101) || !strstr(resp, "Upgrade:")) {
    free(resp);
    far_tcp_close(sock);
    return -1;
  }
  free(resp);
  return sock;
}

int64_t far_ws_send(int64_t sock, const char* text) {
  if (sock < 0 || !text)
    return -1;
  size_t len = strlen(text);
  if (len > (1u << 24))
    return -1;
  unsigned char mask[4];
  for (int i = 0; i < 4; ++i)
    mask[i] = (unsigned char)(rand() & 0xFF);
  unsigned char hdr[14];
  size_t hlen;
  hdr[0] = 0x81;
  if (len < 126) {
    hdr[1] = 0x80 | (unsigned char)len;
    hlen = 2;
  } else if (len < 65536) {
    hdr[1] = 0x80 | 126;
    hdr[2] = (unsigned char)((len >> 8) & 0xFF);
    hdr[3] = (unsigned char)(len & 0xFF);
    hlen = 4;
  } else {
    hdr[1] = 0x80 | 127;
    uint64_t l = (uint64_t)len;
    for (int i = 0; i < 8; ++i)
      hdr[2 + i] = (unsigned char)((l >> (56 - 8 * i)) & 0xFF);
    hlen = 10;
  }
  memcpy(hdr + hlen, mask, 4);
  hlen += 4;
  if (far_sock_send_all(sock, hdr, hlen) < 0)
    return -1;
  char* masked = (char*)malloc(len);
  if (!masked)
    return -1;
  for (size_t i = 0; i < len; ++i)
    masked[i] = (char)((unsigned char)text[i] ^ mask[i % 4]);
  int64_t sent = far_sock_send_all(sock, masked, len);
  free(masked);
  return sent;
}

static int far_sock_recv_exact(int64_t sock, void* buf, size_t len) {
  size_t off = 0;
  while (off < len) {
#ifdef _WIN32
    int n = recv((SOCKET)sock, (char*)buf + off, (int)(len - off), 0);
#else
    ssize_t n = recv((int)sock, (char*)buf + off, len - off, 0);
#endif
    if (n <= 0)
      return 0;
    off += (size_t)n;
  }
  return 1;
}

static char* far_recv_http_headers(int64_t sock) {
  enum { FAR_HTTP_HDR_MAX = 65536 };
  size_t cap = 4096;
  size_t len = 0;
  char* resp = (char*)malloc(cap);
  if (!resp)
    return NULL;
  for (;;) {
    if (len + 256 >= cap) {
      if (cap >= FAR_HTTP_HDR_MAX) {
        free(resp);
        return NULL;
      }
      size_t new_cap = cap * 2;
      if (new_cap > FAR_HTTP_HDR_MAX)
        new_cap = FAR_HTTP_HDR_MAX;
      char* n = (char*)realloc(resp, new_cap);
      if (!n) {
        free(resp);
        return NULL;
      }
      cap = new_cap;
      resp = n;
    }
#ifdef _WIN32
    int n = recv((SOCKET)sock, resp + len, (int)(cap - len - 1), 0);
#else
    ssize_t n = recv((int)sock, resp + len, cap - len - 1, 0);
#endif
    if (n <= 0)
      break;
    len += (size_t)n;
    resp[len] = '\0';
    if (strstr(resp, "\r\n\r\n") || strstr(resp, "\n\n"))
      break;
  }
  resp[len] = '\0';
  return resp;
}

char* far_ws_recv(int64_t sock) {
  if (sock < 0)
    return NULL;
  unsigned char hdr[2];
  if (!far_sock_recv_exact(sock, hdr, 2))
    return NULL;
  int opcode = hdr[0] & 0x0F;
  if (opcode == 0x8)
    return NULL;
  if (opcode != 0x1 && opcode != 0x0)
    return NULL;
  int masked = hdr[1] & 0x80;
  size_t len = hdr[1] & 0x7F;
  if (len == 126) {
    unsigned char ext[2];
    if (!far_sock_recv_exact(sock, ext, 2))
      return NULL;
    len = ((size_t)ext[0] << 8) | ext[1];
  } else if (len == 127) {
    unsigned char ext[8];
    if (!far_sock_recv_exact(sock, ext, 8))
      return NULL;
    len = 0;
    for (int i = 0; i < 8; ++i)
      len = (len << 8) | ext[i];
  }
  unsigned char mask[4] = {0, 0, 0, 0};
  if (masked && !far_sock_recv_exact(sock, mask, 4))
    return NULL;
  if (len > (1u << 24))
    return NULL;
  char* buf = (char*)malloc(len + 1);
  if (!buf)
    return NULL;
  if (!far_sock_recv_exact(sock, buf, len)) {
    free(buf);
    return NULL;
  }
  if (masked) {
    for (size_t i = 0; i < len; ++i)
      buf[i] = (char)((unsigned char)buf[i] ^ mask[i % 4]);
  }
  buf[len] = '\0';
  return buf;
}

int64_t far_ws_close(int64_t sock) { return far_tcp_close(sock); }

/* --- RPC / REST / gRPC --- */

static int far_rpc_params_valid(const char* params) {
  if (!params)
    return 0;
  const char* ps = params;
  while (*ps == ' ' || *ps == '\t')
    ++ps;
  if (*ps == '\0')
    return 0;
  for (const char* p = params; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c < 32 && c != '\t')
      return 0;
  }
  if (strstr(params, "}{") != NULL)
    return 0;
  if (*ps != '{' && *ps != '[' && *ps != '"' && *ps != '-' && (*ps < '0' || *ps > '9') &&
      strncmp(ps, "null", 4) != 0 && strncmp(ps, "true", 4) != 0 && strncmp(ps, "false", 5) != 0)
    return 0;
  return 1;
}

char* far_rpc_call(const char* host, int64_t port, const char* method, const char* params) {
  if (!host || !method)
    return NULL;
  if (port <= 0 || port > 65535)
    return NULL;
  if (!far_net_host_str_ok(host))
    return NULL;
  if (!params)
    params = "{}";
  if (!far_rpc_params_valid(params))
    return NULL;
  if (!far_net_no_ctl(method))
    return NULL;
  for (const char* p = method; *p; ++p) {
    if (*p == '"' || *p == '\\')
      return NULL;
  }
  int need = snprintf(NULL, 0,
                      "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":1}", method,
                      params);
  if (need < 0 || need >= 2048)
    return NULL;
  char body[2048];
  if (snprintf(body, sizeof(body),
               "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":1}", method,
               params) >= (int)sizeof(body))
    return NULL;
  char url[512];
  int url_len = snprintf(url, sizeof(url), "http://%s:%" PRId64 "/rpc", host, port);
  if (url_len < 0 || (size_t)url_len >= sizeof(url))
    return NULL;
  return far_http_post(url, body);
}

char* far_rest_get(const char* url) { return far_http_get(url); }

char* far_rest_post(const char* url, const char* body) { return far_http_post(url, body); }

char* far_rest_put(const char* url, const char* body) {
  return far_http_method_ct(url, "PUT", body, "application/json");
}

char* far_rest_delete(const char* url) {
  FarUrl u;
  if (!far_parse_url(url, &u) || u.https)
    return NULL;
  char host_hdr[520];
  if (!far_http_host_value(&u, host_hdr, sizeof(host_hdr)))
    return NULL;
  int need = snprintf(NULL, 0, "DELETE %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path,
                      host_hdr);
  if (need < 0)
    return NULL;
  char* req = far_net_alloc_fmt(need);
  if (!req)
    return NULL;
  int w = snprintf(req, (size_t)need + 1, "DELETE %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path,
           host_hdr);
  if (!far_net_fmt_matches(w, need)) {
    free(req);
    return NULL;
  }
  char* resp = far_http_exchange(u.host, u.port, req);
  free(req);
  char* body = far_http_body(resp);
  free(resp);
  return body;
}

char* far_grpc_call(const char* host, int64_t port, const char* method, const char* payload) {
  if (!host || !method)
    return NULL;
  if (port <= 0 || port > 65535)
    return NULL;
  if (!far_net_host_str_ok(host))
    return NULL;
  if (!far_net_no_ctl(method))
    return NULL;
  if (!payload)
    payload = "";
  if (!far_net_no_ctl(payload))
    return NULL;
  int64_t sock = far_tcp_socket_connect(host, (int)port);
  if (sock < 0)
    return NULL;
  char frame[4096];
  int frame_len = snprintf(frame, sizeof(frame), "GRPC\n%s\n%zu\n%s", method, strlen(payload),
                           payload);
  if (frame_len < 0 || (size_t)frame_len >= sizeof(frame)) {
    far_tcp_close(sock);
    return NULL;
  }
  if (far_tcp_send(sock, frame) < 0) {
    far_tcp_close(sock);
    return NULL;
  }
  char* resp = far_tcp_recv(sock, 4096);
  far_tcp_close(sock);
  return resp;
}

/* --- Serialization --- */

char* far_net_pack(const char* service, const char* method, const char* body) {
  if (!service)
    service = "";
  if (!method)
    method = "";
  if (!body)
    body = "";
  if (!far_net_no_ctl(service) || !far_net_no_ctl(method) || !far_net_no_ctl(body))
    return NULL;
  if (!far_net_str_len_ok(service) || !far_net_str_len_ok(method) || !far_net_str_len_ok(body))
    return NULL;
  size_t sl = strlen(service);
  size_t ml = strlen(method);
  size_t bl = strlen(body);
  if (sl > SIZE_MAX - ml - bl - 32)
    return NULL;
  size_t n = sl + ml + bl + 32;
  char* out = (char*)malloc(n);
  if (!out)
    return NULL;
  int w = snprintf(out, n, "FARPKT\nservice:%s\nmethod:%s\nbody:%s\n", service, method, body);
  if (w < 0 || (size_t)w >= n) {
    free(out);
    return NULL;
  }
  return out;
}

static char* far_net_unpack_ordered(const char* packed, int field_idx) {
  static const char* kFields[] = {"service:", "method:", "body:"};
  if (!packed || strncmp(packed, "FARPKT\n", 7) != 0 || field_idx < 0 || field_idx > 2)
    return NULL;
  const char* p = packed + 7;
  for (int i = 0; i <= field_idx; ++i) {
    if (i > 0) {
      if (*p != '\n')
        return NULL;
      ++p;
    }
    const char* prefix = kFields[i];
    size_t plen = strlen(prefix);
    if (strncmp(p, prefix, plen) != 0)
      return NULL;
    p += plen;
    if (i == field_idx) {
      const char* end = p;
      while (*end && *end != '\n')
        ++end;
      size_t n = (size_t)(end - p);
      if (n > FAR_NET_MSG_MAX)
        return NULL;
      char* out = (char*)malloc(n + 1);
      if (!out)
        return NULL;
      memcpy(out, p, n);
      out[n] = '\0';
      return out;
    }
    while (*p && *p != '\n')
      ++p;
  }
  return NULL;
}

char* far_net_unpack_method(const char* packed) {
  return far_net_unpack_ordered(packed, 1);
}

char* far_net_unpack_body(const char* packed) {
  return far_net_unpack_ordered(packed, 2);
}

static size_t far_json_escaped_len(const char* data) {
  if (!data)
    return 0;
  if (!far_net_str_len_ok(data))
    return SIZE_MAX;
  size_t n = 0;
  for (const char* p = data; *p; ++p) {
    size_t add = 1;
    switch (*p) {
      case '"':
      case '\\':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        add = 2;
        break;
      default:
        if ((unsigned char)*p < 0x20)
          add = 6;
        break;
    }
    if (n > SIZE_MAX - add)
      return SIZE_MAX;
    n += add;
  }
  return n;
}

static void far_json_write_escaped(char* out, const char* data) {
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

char* far_net_serialize(const char* data) {
  if (!data)
    return far_net_strdup("{}");
  if (!far_net_str_len_ok(data))
    return NULL;
  size_t elen = far_json_escaped_len(data);
  if (elen >= SIZE_MAX)
    return NULL;
  char* escaped = (char*)malloc(elen + 1);
  if (!escaped)
    return NULL;
  far_json_write_escaped(escaped, data);
  if (elen > SIZE_MAX - 16) {
    free(escaped);
    return NULL;
  }
  size_t n = elen + 16;
  char* out = (char*)malloc(n);
  if (!out) {
    free(escaped);
    return NULL;
  }
  int w = snprintf(out, n, "{\"data\":\"%s\"}", escaped);
  if (w < 0 || (size_t)w >= n) {
    free(escaped);
    free(out);
    return NULL;
  }
  free(escaped);
  return out;
}

static const char* far_net_json_find_key(const char* json, const char* key) {
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

static int far_net_json_hex4(const char* p, uint32_t* out) {
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

static int far_net_deser_reserve(char** out, size_t* cap, size_t* len, size_t extra) {
  if (*len + extra > FAR_NET_MSG_MAX)
    return 0;
  if (*len + extra >= *cap) {
    if (*cap > SIZE_MAX / 2)
      return 0;
    size_t nc = *cap ? *cap : 64;
    while (nc < *len + extra + 1) {
      if (nc > SIZE_MAX / 2) {
        nc = *len + extra + 1;
        break;
      }
      nc *= 2;
    }
    char* bigger = (char*)realloc(*out, nc);
    if (!bigger)
      return 0;
    *out = bigger;
    *cap = nc;
  }
  return 1;
}

static int far_net_deser_push(char** out, size_t* cap, size_t* len, char c) {
  if (!far_net_deser_reserve(out, cap, len, 1))
    return 0;
  (*out)[(*len)++] = c;
  return 1;
}

static int far_net_json_codepoint_ok(uint32_t cp) {
  return cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu);
}

static int far_net_deser_push_cp(char** out, size_t* cap, size_t* len, uint32_t cp) {
  if (!far_net_json_codepoint_ok(cp))
    return 0;
  char buf[4];
  size_t n = 0;
  if (cp <= 0x7F)
    buf[n++] = (char)cp;
  else if (cp <= 0x7FF) {
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
  if (!far_net_deser_reserve(out, cap, len, n))
    return 0;
  memcpy(*out + *len, buf, n);
  *len += n;
  return 1;
}

char* far_net_deserialize(const char* json, const char* key) {
  if (!json || !key || !key[0])
    return NULL;
  if (!far_net_no_ctl(key))
    return NULL;
  const char* p = far_net_json_find_key(json, key);
  if (!p)
    return NULL;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    ++p;
  if (*p != '"') {
    return NULL;
  }
  ++p;
  size_t cap = 64;
  size_t len = 0;
  char* out = (char*)malloc(cap);
  if (!out)
    return NULL;
  while (*p && *p != '"') {
    if (*p == '\\') {
      if (!p[1]) {
        free(out);
        return NULL;
      }
      ++p;
      if (*p == 'u' && p[1] && p[2] && p[3] && p[4] && p[5]) {
        uint32_t cp = 0;
        if (far_net_json_hex4(p + 1, &cp) &&
            far_net_deser_push_cp(&out, &cap, &len, cp)) {
          p += 5;
          continue;
        }
        free(out);
        return NULL;
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
        case '"':
        case '\\':
        case '/':
          break;
        default:
          free(out);
          return NULL;
      }
      if (!far_net_deser_push(&out, &cap, &len, ch)) {
        free(out);
        return NULL;
      }
      ++p;
      continue;
    }
    if (!far_net_deser_push(&out, &cap, &len, *p++)) {
      free(out);
      return NULL;
    }
  }
  if (*p != '"') {
    free(out);
    return NULL;
  }
  out[len] = '\0';
  return out;
}

/* --- Packet compression (RLE) --- */

extern char* far_compress_rle(const char* data);
extern char* far_decompress_rle(const char* data);

char* far_net_compress(const char* data) { return far_compress_rle(data); }

char* far_net_decompress(const char* data) { return far_decompress_rle(data); }
