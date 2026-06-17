#include <setjmp.h>
#include <stdio.h>

int my_setjmp(jmp_buf* b) {
  printf("in my_setjmp before\n");
  int r = setjmp(*b);
  printf("in my_setjmp after r=%d\n", r);
  return r;
}

void inner(jmp_buf* b) {
  printf("inner before longjmp\n");
  longjmp(*b, 1);
  printf("inner after longjmp\n");
}

int main(void) {
  jmp_buf buf;
  printf("main start\n");
  if (my_setjmp(&buf) == 0) {
    printf("enter body\n");
    inner(&buf);
    printf("after inner\n");
  } else {
    printf("caught\n");
  }
  printf("main end\n");
  return 0;
}
