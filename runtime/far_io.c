/* Far console I/O runtime — included from far_rt.c */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

static char* far_io_strdup(const char* s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

static char* far_io_read_line_impl(const char* prompt) {
  if (prompt && prompt[0]) {
    fputs(prompt, stdout);
    fflush(stdout);
  }
  char buf[4096];
  if (!fgets(buf, sizeof(buf), stdin))
    return far_io_strdup("");
  size_t n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = '\0';
  return far_io_strdup(buf);
}

/* --- Input --- */

char* far_io_read_line(void) { return far_io_read_line_impl(NULL); }

char* far_io_read_line_prompt(const char* prompt) { return far_io_read_line_impl(prompt); }

int64_t far_io_read_char(void) {
#ifdef _WIN32
  int c = _getch();
  return (int64_t)c;
#else
  int c = getchar();
  return c >= 0 ? (int64_t)c : -1;
#endif
}

int64_t far_io_read_i64(void) {
  char* line = far_io_read_line_impl(NULL);
  if (!line || !line[0]) {
    free(line);
    return 0;
  }
  char* end = NULL;
  errno = 0;
  long long v = strtoll(line, &end, 10);
  if (errno == ERANGE) {
    free(line);
    return 0;
  }
  if (end == line) {
    free(line);
    return 0;
  }
  while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
    ++end;
  if (*end != 0) {
    free(line);
    return 0;
  }
  free(line);
  return (int64_t)v;
}

double far_io_read_f64(void) {
  char* line = far_io_read_line_impl(NULL);
  if (!line || !line[0]) {
    free(line);
    return 0.0;
  }
  char* end = NULL;
  errno = 0;
  double v = strtod(line, &end);
  if (errno == ERANGE) {
    free(line);
    return 0.0;
  }
  if (end == line) {
    free(line);
    return 0.0;
  }
  while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
    ++end;
  if (*end != 0) {
    free(line);
    return 0.0;
  }
  free(line);
  return v;
}

int64_t far_io_has_input(void) {
#ifdef _WIN32
  return _kbhit() ? 1 : 0;
#else
  fd_set fds;
  struct timeval tv;
  FD_ZERO(&fds);
  FD_SET(0, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  int r = select(1, &fds, NULL, NULL, &tv);
  return (r > 0 && FD_ISSET(0, &fds)) ? 1 : 0;
#endif
}

int64_t far_io_confirm(const char* prompt) {
  char* line = far_io_read_line_impl(prompt);
  if (!line || !line[0]) {
    free(line);
    return 0;
  }
  char c = (char)tolower((unsigned char)line[0]);
  free(line);
  return (c == 'y' || c == '1') ? 1 : 0;
}

/* --- Output --- */

void far_io_write(const char* text) {
  if (text)
    fputs(text, stdout);
}

void far_io_writeln(const char* text) {
  if (text)
    fputs(text, stdout);
  fputc('\n', stdout);
}

void far_io_write_err(const char* text) {
  if (text)
    fputs(text, stderr);
}

void far_io_write_errln(const char* text) {
  if (text)
    fputs(text, stderr);
  fputc('\n', stderr);
}

void far_io_flush_stdout(void) { fflush(stdout); }

void far_io_flush_stderr(void) { fflush(stderr); }

/* --- Terminal --- */

void far_io_clear(void) {
  fputs("\033[2J\033[H", stdout);
  fflush(stdout);
}

int64_t far_io_is_tty(void) {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) ? 1 : 0;
#else
  return isatty(STDIN_FILENO) ? 1 : 0;
#endif
}

int64_t far_io_columns(void) {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    return (int64_t)(info.srWindow.Right - info.srWindow.Left + 1);
#endif
  return 80;
}

void far_io_beep(void) { fputc('\a', stdout); fflush(stdout); }
