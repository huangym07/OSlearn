#include "kernel/types.h"
#include "user/user.h"

int main()
{
  close(0);
  int p1[2], p2[2], p3[2], p4[2];
  char buf[] = {'a'};
  pipe(p1);
  pipe(p2);
  int pid = fork();
  if (pid == 0) {
    close(p1[1]);
    close(p2[0]);
    write(p2[1], buf, 1);
    int pri = -1, child = 0; // child: whether it has child
    int num;
    while (read(p1[0], &num, 4)) {
      if (pri == -1) {
        pri = num;
        fprintf(1, "prime %d\n", pri);
      }
      if (num % pri == 0) {
        write(p2[1], buf, 1);
        continue;
      }
      if (!child) {
        child = 1;
        pipe(p3);
        pipe(p4);
        pid = fork();
        if (pid == 0) {
          close(p1[0]);
          close(p2[1]);
          p1[0] = p3[0], p1[1] = p3[1];
          p2[0] = p4[0], p2[1] = p4[1];
          close(p1[1]);
          close(p2[0]);
          pri = -1, child = 0;
          write(p2[1], buf, 1);
          read(p1[0], &num, 4);
          pri = num;
          fprintf(1, "prime %d\n", pri);
        } else {
          close(p3[0]);
          close(p4[1]);
          if (read(p4[0], buf, 1)) write(p3[1], &num, 4);
        }
      } else {
        if (read(p4[0], buf, 1)) write(p3[1], &num, 4);
      }
      write(p2[1], buf, 1);
    }
    if (!child) {
      close(p1[0]);
      close(p2[1]);
      exit(0);
    } else {
      close(p3[1]);
      read(p4[0], buf, 1);
      close(p4[0]);
      close(p1[0]);
      close(p2[1]);
      exit(0);
    }
  } else {
    close(p1[0]);
    close(p2[1]);
    for (int i = 2; i <= 35; i++) {
      read(p2[0], buf, 1);
      write(p1[1], &i, 4);
    }
    close(p1[1]);
    read(p2[0], buf, 1);
    close(p2[0]);
    exit(0);
  }

  exit(0);
}