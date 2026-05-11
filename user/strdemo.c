#include "kernel/types.h"
#include "user/user.h"

char *s = "123";

int
main(int argc, char **argv)
{
  char s1[4] = {'1', '2', '3', '\0'};

  printf("[strdemo] s=%s s1=%s\n", s, s1);
  printf("[strdemo] s[0]=%c *s=%c\n", s[0], *s);
  printf("[strdemo] s[2]=%c *(s+2)=%c\n", s[2], *(s + 2));
  printf("[strdemo] strlen(s)=%d strlen(s1)=%d\n", strlen(s), strlen(s1));

  printf("[strdemo] s1 addr=%p s1 end addr=&s1[3]=%p\n", s1, &s1[3]);
  printf("[strdemo] note: do not read/write beyond '\\0' or array bound\n");

  exit(0);
}
