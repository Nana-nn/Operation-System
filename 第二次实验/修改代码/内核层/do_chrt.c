

#include "kernel/system.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <lib.h>
#include <minix/endpoint.h>
#include <minix/u64.h>

#if USE_CHRT
int do_chrt(struct proc *caller, message *m_ptr)
{
  struct proc *rp;
  rp = proc_addr(m_ptr->m2_i1);//get the process
  rp->p_deadline =m_ptr->m2_l1;//set the deadline
  return (OK);
}

#endif