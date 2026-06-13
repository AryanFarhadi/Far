/* Far networking runtime — included from far_rt.c */

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static char* far_net_strdup(const char* s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
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
  const char* slash = strchr(p, '/');
  const char* colon = strchr(p, ':');
  if (colon && (!slash || colon < slash)) {
    size_t hlen = (size_t)(colon - p);
    if (hlen >= sizeof(out->host))
      hlen = sizeof(out->host) - 1;
    memcpy(out->host, p, hlen);
    out->host[hlen] = 0;
    out->port = atoi(colon + 1);
    p = slash ? slash : colon + 1;
    while (*p && *p != '/')
      ++p;
  } else {
    size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hlen >= sizeof(out->host))
      hlen = sizeof(out->host) - 1;
    memcpy(out->host, p, hlen);
    out->host[hlen] = 0;
    p = slash ? slash : p + hlen;
  }
  if (*p == '/')
    snprintf(out->path, sizeof(out->path), "%s", p);
  else
    strcpy(out->path, "/");
  return out->host[0] != 0;
}

static int64_t far_tcp_socket_connect(const char* host, int port) {
  if (!host || port <= 0 || port > 65535)
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
    if (connect(sock, it->ai_addr, (int)it->ai_addrlen) == 0)
      break;
    NET_CLOSE(sock);
    sock = NET_INVALID;
  }
  freeaddrinfo(res);
  return (int64_t)sock;
}

/* --- TCP --- */

int64_t far_tcp_connect(const char* host, int64_t port) {
  return far_tcp_socket_connect(host, (int)port);
}

int64_t far_tcp_listen(int64_t port) {
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
  addr.sin_port = htons((uint16_t)(port > 0 ? port : 0));
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

int64_t far_tcp_send(int64_t sock, const char* data) {
  if (sock < 0 || !data)
    return -1;
#ifdef _WIN32
  int n = send((SOCKET)sock, data, (int)strlen(data), 0);
#else
  ssize_t n = send((int)sock, data, strlen(data), 0);
#endif
  return n < 0 ? -1 : (int64_t)n;
}

char* far_tcp_recv(int64_t sock, int64_t max) {
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

int64_t far_tcp_close(int64_t sock) {
  if (sock < 0)
    return -1;
  return NET_CLOSE((NET_SOCK)sock) == 0 ? 0 : -1;
}

/* --- UDP --- */

int64_t far_udp_bind(int64_t port) {
  far_net_init();
  NET_SOCK sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == NET_INVALID)
    return -1;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)(port > 0 ? port : 0));
  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    NET_CLOSE(sock);
    return -1;
  }
  return (int64_t)sock;
}

int64_t far_udp_send(int64_t sock, const char* host, int64_t port, const char* data) {
  if (sock < 0 || !host || !data || port <= 0)
    return -1;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, host, &addr.sin_addr);
  if (addr.sin_addr.s_addr == 0)
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
  int n = sendto((SOCKET)sock, data, (int)strlen(data), 0, (struct sockaddr*)&addr, sizeof(addr));
#else
  ssize_t n = sendto((int)sock, data, strlen(data), 0, (struct sockaddr*)&addr, sizeof(addr));
#endif
  return n < 0 ? -1 : (int64_t)n;
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
  far_tcp_send(sock, req);
  size_t cap = 4096;
  size_t len = 0;
  char* resp = (char*)malloc(cap);
  if (!resp) {
    far_tcp_close(sock);
    return NULL;
  }
  for (;;) {
    if (len + 256 >= cap) {
      cap *= 2;
      char* n = (char*)realloc(resp, cap);
      if (!n) {
        free(resp);
        far_tcp_close(sock);
        return NULL;
      }
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

char* far_http_get(const char* url) {
  FarUrl u;
  if (!far_parse_url(url, &u) || u.https)
    return NULL;
  char req[1024];
  snprintf(req, sizeof(req),
           "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path, u.host);
  char* resp = far_http_exchange(u.host, u.port, req);
  char* body = far_http_body(resp);
  free(resp);
  return body;
}

char* far_http_post(const char* url, const char* body) {
  return far_http_post_ct(url, body, "application/json");
}

char* far_http_post_ct(const char* url, const char* body, const char* ctype) {
  FarUrl u;
  if (!far_parse_url(url, &u) || u.https)
    return NULL;
  if (!body)
    body = "";
  if (!ctype)
    ctype = "text/plain";
  char req[4096];
  snprintf(req, sizeof(req),
           "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: "
           "close\r\n\r\n%s",
           u.path, u.host, ctype, strlen(body), body);
  char* resp = far_http_exchange(u.host, u.port, req);
  char* out = far_http_body(resp);
  free(resp);
  return out;
}

int64_t far_http_parse_status(const char* response) {
  if (!response)
    return 0;
  if (strncmp(response, "HTTP/", 5) != 0)
    return 0;
  const char* sp = strchr(response, ' ');
  if (!sp)
    return 0;
  return (int64_t)atoi(sp + 1);
}

/* --- HTTPS (WinHTTP on Windows) --- */

#ifdef _WIN32
static wchar_t* far_to_wide(const char* s) {
  if (!s)
    return NULL;
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (n <= 0)
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
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
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
    wchar_t hdr[256];
    const char* ct = ctype ? ctype : "application/json";
    snprintf((char*)hdr, sizeof(hdr), "Content-Type: %s\r\n", ct);
    wchar_t* whdr = far_to_wide((char*)hdr);
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
    if (len + avail + 1 >= cap) {
      cap = len + avail + 1;
      char* n = (char*)realloc(out, cap);
      if (!n) {
        free(out);
        out = NULL;
        break;
      }
      out = n;
    }
    DWORD read = 0;
    if (!WinHttpReadData(req, out + len, avail, &read))
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
  if (!body)
    body = "";
  if (!ctype)
    ctype = "application/json";
  char req[4096];
  if (body[0]) {
    snprintf(req, sizeof(req),
             "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: "
             "close\r\n\r\n%s",
             method, u.path, u.host, ctype, strlen(body), body);
  } else {
    snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", method, u.path,
             u.host);
  }
  char* resp = far_http_exchange(u.host, u.port, req);
  char* out = far_http_body(resp);
  free(resp);
  return out;
}

char* far_https_get(const char* url) {
#ifdef _WIN32
  return far_https_win(url, "GET", NULL, NULL);
#else
  char* resp = far_https_socket(url, "GET", NULL, NULL);
  if (resp)
    return resp;
  return far_http_get(url);
#endif
}

char* far_https_post(const char* url, const char* body) {
#ifdef _WIN32
  return far_https_win(url, "POST", body, "application/json");
#else
  char* resp = far_https_socket(url, "POST", body, "application/json");
  if (resp)
    return resp;
  return far_http_post(url, body);
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

int64_t far_ws_connect(const char* host, int64_t port, const char* path) {
  if (!host || !path)
    return -1;
  int64_t sock = far_tcp_socket_connect(host, (int)(port > 0 ? port : 80));
  if (sock < 0)
    return -1;
  char key[24];
  far_ws_make_key(key, sizeof(key));
  char req[1024];
  snprintf(req, sizeof(req),
           "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
           "%s\r\nSec-WebSocket-Version: 13\r\n\r\n",
           path, host, key);
  far_tcp_send(sock, req);
  char* resp = far_tcp_recv(sock, 2048);
  if (!resp || strstr(resp, "101") == NULL) {
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
  unsigned char hdr[10];
  size_t hlen = 2;
  hdr[0] = 0x81;
  if (len < 126) {
    hdr[1] = (unsigned char)len;
  } else {
    hdr[1] = 126;
    hdr[2] = (unsigned char)((len >> 8) & 0xFF);
    hdr[3] = (unsigned char)(len & 0xFF);
    hlen = 4;
  }
#ifdef _WIN32
  send((SOCKET)sock, (const char*)hdr, (int)hlen, 0);
  int n = send((SOCKET)sock, text, (int)len, 0);
#else
  send((int)sock, hdr, hlen, 0);
  ssize_t n = send((int)sock, text, len, 0);
#endif
  return n < 0 ? -1 : (int64_t)n;
}

char* far_ws_recv(int64_t sock) {
  if (sock < 0)
    return NULL;
  unsigned char hdr[2];
#ifdef _WIN32
  if (recv((SOCKET)sock, (char*)hdr, 2, 0) != 2)
    return NULL;
#else
  if (recv((int)sock, hdr, 2, 0) != 2)
    return NULL;
#endif
  size_t len = hdr[1] & 0x7F;
  if (len == 126) {
    unsigned char ext[2];
#ifdef _WIN32
    recv((SOCKET)sock, (char*)ext, 2, 0);
#else
    recv((int)sock, ext, 2, 0);
#endif
    len = ((size_t)ext[0] << 8) | ext[1];
  }
  char* buf = (char*)malloc(len + 1);
  if (!buf)
    return NULL;
#ifdef _WIN32
  recv((SOCKET)sock, buf, (int)len, 0);
#else
  recv((int)sock, buf, len, 0);
#endif
  buf[len] = '\0';
  return buf;
}

int64_t far_ws_close(int64_t sock) { return far_tcp_close(sock); }

/* --- RPC / REST / gRPC --- */

char* far_rpc_call(const char* host, int64_t port, const char* method, const char* params) {
  if (!host || !method)
    return NULL;
  if (!params)
    params = "{}";
  char body[2048];
  snprintf(body, sizeof(body),
           "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":1}", method, params);
  char url[512];
  snprintf(url, sizeof(url), "http://%s:%" PRId64 "/rpc", host, port);
  return far_http_post(url, body);
}

char* far_rest_get(const char* url) { return far_http_get(url); }

char* far_rest_post(const char* url, const char* body) { return far_http_post(url, body); }

char* far_rest_put(const char* url, const char* body) { return far_http_post_ct(url, body, "application/json"); }

char* far_rest_delete(const char* url) {
  FarUrl u;
  if (!far_parse_url(url, &u) || u.https)
    return NULL;
  char req[1024];
  snprintf(req, sizeof(req),
           "DELETE %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", u.path, u.host);
  char* resp = far_http_exchange(u.host, u.port, req);
  char* body = far_http_body(resp);
  free(resp);
  return body;
}

char* far_grpc_call(const char* host, int64_t port, const char* method, const char* payload) {
  if (!host || !method)
    return NULL;
  if (!payload)
    payload = "";
  int64_t sock = far_tcp_socket_connect(host, (int)port);
  if (sock < 0)
    return NULL;
  char frame[4096];
  snprintf(frame, sizeof(frame), "GRPC\n%s\n%zu\n%s", method, strlen(payload), payload);
  far_tcp_send(sock, frame);
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
  size_t n = strlen(service) + strlen(method) + strlen(body) + 32;
  char* out = (char*)malloc(n);
  if (!out)
    return NULL;
  snprintf(out, n, "FARPKT\nservice:%s\nmethod:%s\nbody:%s\n", service, method, body);
  return out;
}

static const char* far_net_field(const char* packed, const char* key) {
  if (!packed || !key)
    return NULL;
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "%s:", key);
  const char* p = strstr(packed, pattern);
  if (!p)
    return NULL;
  p += strlen(pattern);
  const char* end = strchr(p, '\n');
  size_t n = end ? (size_t)(end - p) : strlen(p);
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, p, n);
  out[n] = '\0';
  return out;
}

char* far_net_unpack_method(const char* packed) { return (char*)far_net_field(packed, "method"); }

char* far_net_unpack_body(const char* packed) { return (char*)far_net_field(packed, "body"); }

char* far_net_serialize(const char* data) {
  if (!data)
    return far_net_strdup("{}");
  size_t n = strlen(data) + 16;
  char* out = (char*)malloc(n);
  if (!out)
    return NULL;
  snprintf(out, n, "{\"data\":\"%s\"}", data);
  return out;
}

char* far_net_deserialize(const char* json, const char* key) {
  if (!json || !key)
    return NULL;
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* p = strstr(json, pattern);
  if (!p)
    return NULL;
  p += strlen(pattern);
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

/* --- Packet compression (RLE) --- */

extern char* far_compress_rle(const char* data);
extern char* far_decompress_rle(const char* data);

char* far_net_compress(const char* data) { return far_compress_rle(data); }

char* far_net_decompress(const char* data) { return far_decompress_rle(data); }
