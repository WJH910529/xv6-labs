#include "kernel/types.h"
#include "user/user.h"

int a[3] = {1, 2, 3};
char b[3] = {'a', 'b', 'c'};

int
main(int argc, char **argv)
{
  printf("[arraydemo] initial a[0]=%d a[1]=%d a[2]=%d\n", a[0], a[1], a[2]);

  a[1] += 1;      // index access
  *(a + 2) = 5;   // pointer access
  printf("[arraydemo] after write a[1]=%d a[2]=%d\n", a[1], a[2]);

  printf("[arraydemo] a=%p a+1=%p a+2=%p &a[2]=%p\n", a, a + 1, a + 2, &a[2]);
  printf("[arraydemo] b=%p b+1=%p\n", b, b + 1);
  printf("[arraydemo] note: int* jumps 4 bytes; char* jumps 1 byte\n");

  exit(0);
}
