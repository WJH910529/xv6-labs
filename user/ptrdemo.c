#include "kernel/types.h"
#include "user/user.h"

int g = 3;

struct two {
  int a;
  int b;
};

int
main(int argc, char **argv)
{
  int l = 5;
  int *p, *q;

  p = &g;
  q = &l;
  printf("[ptrdemo] p=%p q=%p\n", p, q);

  *p = 11;
  *q = 13;
  printf("[ptrdemo] g=%d l=%d\n", g, l);

  struct two s;
  s.a = 10;
  s.b = 20;
  struct two *sp = &s;
  printf("[ptrdemo] s.a=%d sp->a=%d s.b=%d sp->b=%d\n", s.a, sp->a, s.b, sp->b);

  int **pp = &p;
  printf("[ptrdemo] pp=%p *pp=%p **pp=%d\n", pp, *pp, **pp);

  int (*f)(int, char **);
  f = &main;
  printf("[ptrdemo] main addr=%p\n", f);

  exit(0);
}
