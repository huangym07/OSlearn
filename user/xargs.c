#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

// xv6 xargs 要完成从标准输入读取内容，按照 '\n' 换行来分割成多行参数。
// 每次  <command> (args ...) 时添加一个额外参数
// 就像是 Unix 的 ... | xargs -n1 <command> (args ...) 一样

#define BUFSIZ 512

int readline(char *xargs[32], int now_argc, char *buf) // 返回读取到额外的参数后的总参数个数（没有额外参数时返回 0）
{
  int n = 0;
  while (read(0, buf + n, 1)) {
    if (n == BUFSIZ - 1) {
      fprintf(2, "xargs: argument too long\n");
      exit(1);
    }
    if (buf[n] == '\n') break;
    n++;
  }
  if (n == 0) return 0;
  buf[n] = '\0';
  int offset = 0;
  while (offset < n) {
    xargs[now_argc++] = buf + offset;
    while (offset < n && buf[offset] != ' ') offset++;
    while (offset < n && buf[offset] == ' ') buf[offset++] = '\0';
  }
  return now_argc;
}

int main(int argc, char *argv[])
{
  if (argc <= 1) {
    fprintf(2, "usage: xargs <command> (args ...)\n");
    exit(1);
  }
  char *xargs[MAXARG];
  char buf[BUFSIZ];

  for (int i = 1; i < argc; i++) {
    xargs[i - 1] = argv[i];
  }
  int now_argc;
  while ((now_argc = readline(xargs, argc - 1, buf))) {
    xargs[now_argc] = 0;
    if (fork() == 0) {
      exec(xargs[0], xargs);
      fprintf(2, "xargs failed\n");
      exit(1);
    }
    wait(0);
  }
  exit(0);
}