#include <setjmp.h>
#include <stdio.h>

static jmp_buf buf;

void inner(void) {
  printf("inner throw\n");
  longjmp(buf, 1);
}

int main(void) {
  if (setjmp(buf) == 0) {
    printf("enter\n");
    inner();
    printf("after inner\n");
  } else {
    printf("caught\n");
  }
  return 0;
}
