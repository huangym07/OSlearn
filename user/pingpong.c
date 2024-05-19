#include "kernel/types.h"
#include "user/user.h"

int main()
{
  // using two pipes instead of wait() in the parent
  int p1[2], p2[2];
  pipe(p1);
  pipe(p2);
  
  int pid = fork();

  char buf[] = {'a'};
  if (pid == 0) {
    // child
    close(p1[1]);
    read(p1[0], buf, 1);
    fprintf(1, "%d: received ping\n", getpid());
    write(p2[1], buf, 1);
    exit(0);
  } else {
    // parent
    close(p2[1]);
    close(p1[0]);
    write(p1[1], buf, 1);
    read(p2[0], buf, 1);
    fprintf(1, "%d: received pong\n", getpid());
  }

  exit(0);
}
