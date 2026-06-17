#include <stdio.h>
#include <setjmp.h>
int main() { printf("sizeof jmp_buf=%zu\n", sizeof(jmp_buf)); return 0; }
