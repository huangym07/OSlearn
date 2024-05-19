#include "kernel/types.h"
#include "user/user.h"

int 
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(1, "usage: sleep <int whick means times.>\n");
    exit(1);
  }

  int sleep_time = atoi(argv[1]);
  
  sleep(sleep_time);
  
  exit(0);
}
