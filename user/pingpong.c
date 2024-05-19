#include "kernel/types.h"
#include "user/user.h"

int main()
{
  int p_arr[2];
  pipe(p_arr);
  
  int pid = fork();

  if (pid == 0) {
    char tmp;
    read(p_arr[0], &tmp, 1);
    fprintf(1, "%d: received ping\n", getpid());
    write(p_arr[1], &tmp, 1);
    close(p_arr[0]);
    close(p_arr[1]);
    exit(0);
  } else {
    char tmp;
    write(p_arr[1], "?", 1);
    wait(0);
    read(p_arr[0], &tmp, 1);
    fprintf(1, "%d: received pong\n", getpid());
    close(p_arr[0]);
    close(p_arr[1]);
  }

  exit(0);
}
