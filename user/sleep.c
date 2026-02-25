// /user/sleep.c 
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{

  if(argc!=2){
        printf("the argument number input error");
  }
  int tick = atoi(argv[1]);
  sleep(tick);

  
  exit(0);
}