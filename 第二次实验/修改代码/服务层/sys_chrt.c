#include "syslib.h"

int sys_chrt(who, deadline)
endpoint_t who;
long deadline;
{
  message m;
  int r;
  m.m2_i1 = who;
  m.m2_l1 = deadline;
  r=_kernel_call(SYS_CHRT, &m);
  return r;
}