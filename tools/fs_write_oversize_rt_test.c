#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern int64_t far_fs_write(const char* path, const char* content);

int main(void) {
  size_t n = (size_t)64 * 1024 * 1024 + 1;
  char* buf = (char*)malloc(n + 1);
  if (!buf)
    return 2;
  memset(buf, 'q', n);
  buf[n] = '\0';
  int64_t r = far_fs_write("far_fs_oversize_write_rt.bin", buf);
  free(buf);
  return r == -1 ? 0 : 1;
}
