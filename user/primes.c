// 递归实现
#include "kernel/types.h"
#include "user/user.h"

void new_proc(int p[]) {
  close(p[1]);
  int pri = -1;
  if (read(p[0], &pri, 4) != 4) {
    fprintf(1, "child process failed to read prime from the pipe\n");
    exit(1);
  }
  fprintf(1, "prime %d\n", pri);

  int num = 0, child = 0, newp[2];
  while (read(p[0], &num, 4)) {
    if (num % pri == 0) continue;
    if (!child) {
      pipe(newp);
      if (fork() == 0) new_proc(newp);
      else close(newp[0]);
      child = 1;
    } 
    write(newp[1], &num, 4);
  }

  close(p[0]);
  close(newp[1]);
  wait(0);
  exit(0);

}

int main()
{
  int p[2];
  pipe(p);

  if (fork() == 0) {
    new_proc(p);
  } else {
    close(p[0]);
    for (int i = 2; i <= 35; i++) {
      if (write(p[1], &i, 4) != 4) {
        fprintf(1, "first process failed to write %d into the pipe\n", i);
        exit(1);
      }
    }
    close(p[1]);
    wait(0);
    exit(0);
  }

  return 0;
}