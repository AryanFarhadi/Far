#include <setjmp.h>
#include <stdio.h>

int my_setjmp(jmp_buf* b) {
  return setjmp(*b);
}

int main(void) {
  jmp_buf buf;
  if (my_setjmp(&buf) == 0) {
    printf("before direct longjmp\n");
    longjmp(buf, 1);
  } else {
    printf("caught direct\n");
  }
  return 0;
}
