#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

void main(void) {
  // printf("%d %d\n", f(8)+1, 13);
  // unsigned int i = 0x00646c72; // 从低地址到高地址，依次存储了单字节的 114(r)->108(l)->100(d)->0('\0')
	// printf("H%x Wo%s", 57616, &i); // 打印出 HE110 World
  printf("x=%d y=%d\n", 3);
  exit(0);
}
