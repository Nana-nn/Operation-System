/* The MINIX model of memory allocation reserves a fixed amount of memory for
 * the combined text, data, and stack segments.  The amount used for a child
 * process created by FORK is the same as the parent had.  If the child does
 * an EXEC later, the new size is taken from the header of the file EXEC'ed.
 *
 * The layout in memory consists of the text segment, followed by the data
 * segment, followed by a gap (unused memory), followed by the stack segment.
 * The data segment grows upward and the stack grows downward, so each can
 * take memory from the gap.  If they meet, the process must be killed.  The
 * procedures in this file deal with the growth of the data and stack segments.
 *
 * The entry points into this file are:
 *   do_brk:	  BRK/SBRK system calls to grow or shrink the data segment
 *   adjust:	  see if a proposed segment adjustment is allowed
 *   size_ok:	  see if the segment sizes are feasible (i86 only)
 */

#include "pm.h"
#include <signal.h>
#include "mproc.h"
#include "param.h"
#include <lib.h>
#define DATA_CHANGED       1	/* flag value when data segment size changed */
#define STACK_CHANGED      2	/* flag value when stack size changed */

/*===========================================================================*
 *				do_brk  				     *
 *===========================================================================*/
PUBLIC int do_brk()
{
/* Perform the brk(addr) system call.
 *
 * The call is complicated by the fact that on some machines (e.g., 8088),
 * the stack pointer can grow beyond the base of the stack segment without
 * anybody noticing it.
 * The parameter, 'addr' is the new virtual address in D space.
 */

  register struct mproc *rmp;
  int r;
  vir_bytes v, new_sp;
  vir_clicks new_clicks;

  rmp = mp;
  v = (vir_bytes) m_in.addr;
  new_clicks = (vir_clicks) ( ((long) v + CLICK_SIZE - 1) >> CLICK_SHIFT);
  if (new_clicks < rmp->mp_seg[D].mem_vir) {
	rmp->mp_reply.reply_ptr = (char *) -1;
	return(ENOMEM);
  }
  new_clicks -= rmp->mp_seg[D].mem_vir;
  if ((r=get_stack_ptr(who_e, &new_sp)) != OK) /* ask kernel for sp value */
  	panic(__FILE__,"couldn't get stack pointer", r);
  r = adjust(rmp, new_clicks, new_sp);
  rmp->mp_reply.reply_ptr = (r == OK ? m_in.addr : (char *) -1);
  return(r);			/* return new address or -1 */
}

/*===========================================================================*
 *				allocate_new_mem  				     *
 *===========================================================================*/
 /*首先需要申请新的足够大的内存空间，这里是申请为原来的2倍，然后将程序现有的数据段、堆栈段的内容分别拷贝至新内存区域的底部(bottom)和顶部(top)；
 这就需要知道物理地址 虚拟地址
 通知内核程序的映像发生了变化；返回do_brk函数
 */
PUBLIC int allocate_new_mem(rmp,clicks) 
register struct mproc *rmp; //pm的进程表
phys_clicks clicks; 
{
    register struct mem_map *mem_sp, *mem_dp;//stack data段
    //字节版本
    phys_bytes old_bytes, data_bytes;
    phys_bytes stak_bytes;
    phys_bytes old_d_tran, new_d_tran;
    phys_bytes old_s_tran, new_s_tran;
    //数据段的以前的起始地址，内存以前的长度，新的起始地址，内存新的长度
    phys_clicks old_clicks, old_base; //地址和长度都是以click为单位。click是1024个字节。
    phys_clicks new_clicks, new_base;
    //栈段以前的起始地址，新的起始地址，长度
    phys_clicks new_s_base;
    phys_clicks old_s_base, stak_clicks;


    mem_dp = &rmp->mp_seg[D];//指向数据段
    mem_sp = &rmp->mp_seg[S];//指向栈段
    //内存以前的长度
    old_clicks = clicks;
    //新的2倍大小的内存空间
    new_clicks = clicks * 2;
    /* 调用alloc_mem获得新的2倍大小的内存空间 */
    /* 如果不成功, 不释放原来的内存空间 */
    if ((new_base = alloc_mem(new_clicks))==NO_MEM){
        return (ENOMEM);
    }

    /* 得到原来栈段和数据段的地址和大小 */
    data_bytes = (phys_bytes) rmp->mp_seg[D].mem_len << CLICK_SHIFT;//因为是字节，所以得到数据段的长度要左移CLICK_SHIFT
    stak_bytes = (phys_bytes) rmp->mp_seg[S].mem_len << CLICK_SHIFT;//同理原来的栈段的长度也是这样
    old_base = rmp->mp_seg[D].mem_phys;//以前的数据段的物理地址
    old_s_base = rmp->mp_seg[S].mem_phys;//以前的栈段的物理地址
    old_d_tran = (phys_bytes)old_base << CLICK_SHIFT;//要把上面获得的click为单位的化为以byte为单位的
    old_s_tran = (phys_bytes)old_s_base << CLICK_SHIFT;
    /* 计算得到新的栈端和数据段的地址 */
    new_s_base = new_base + new_clicks - mem_sp->mem_len; //计算得到新的栈段的地址，单位为click 只需要已经开了2倍大小的内存空间的起始地址
    //+内存大小再减去栈的大小就可以得到
    new_d_tran = (phys_bytes) new_base << CLICK_SHIFT; //数据段的起始地址化为byte单位
    new_s_tran = (phys_bytes)new_s_base << CLICK_SHIFT;//栈段的。。。
    //之所以要化成字节因为后面在调用sys_memset，复制的时候都需要以字节为单位，而不是click
    /* 调用sys_memset函数用0填充新获得的内存 */
    sys_memset(0,new_d_tran,(new_clicks<<CLICK_SHIFT));
    /* 将数据段和栈段分别复制到新的内存空间的底部和顶部 */
    d = sys_abscopy(old_d_tran,new_d_tran,data_bytes);  
    if (d < 0) 
        panic(__FILE__,"allocate_new_mem can't copy",d); 
    s = sys_abscopy(old_s_tran,new_s_tran,stak_bytes); 
    if (s < 0) 
        panic(__FILE__,"allocate_new_mem can't copy",s); 
    /* 更新进程数据段和栈段的内存地址以及栈段的虚拟地址 */
    rmp->mp_seg[D].mem_phys = new_base; //数据段的物理地址
    rmp->mp_seg[S].mem_phys = new_s_base;  //栈段的物理地址
    rmp->mp_seg[S].mem_vir = rmp->mp_seg[D].mem_vir + new_clicks - mem_sp->mem_len;
    /* 释放原来内存 */
    free_mem(old_base,old_clicks); 
    return (OK);
}


/*===========================================================================*
 *				adjust  				     *
 *===========================================================================*/
PUBLIC int adjust(rmp, data_clicks, sp)
register struct mproc *rmp;	/* whose memory is being adjusted? */
vir_clicks data_clicks;		/* how big is data segment to become? */
vir_bytes sp;			/* new value of sp */
{
/* See if data and stack segments can coexist, adjusting them if need be.
 * Memory is never allocated or freed.  Instead it is added or removed from the
 * gap between data segment and stack segment.  If the gap size becomes
 * negative, the adjustment of data or stack fails and ENOMEM is returned.
 */

  register struct mem_map *mem_sp, *mem_dp;
  vir_clicks sp_click, gap_base, lower, old_clicks;
  int changed, r, ft;
  long base_of_stack, delta;	/* longs avoid certain problems */

  mem_dp = &rmp->mp_seg[D];	/* pointer to data segment map */
  mem_sp = &rmp->mp_seg[S];	/* pointer to stack segment map */
  changed = 0;			/* set when either segment changed */

  if (mem_sp->mem_len == 0) return(OK);	/* don't bother init */

  /* See if stack size has gone negative (i.e., sp too close to 0xFFFF...) */
  base_of_stack = (long) mem_sp->mem_vir + (long) mem_sp->mem_len;
  sp_click = sp >> CLICK_SHIFT;	/* click containing sp */
  if (sp_click >= base_of_stack) return(ENOMEM);	/* sp too high */

  /* Compute size of gap between stack and data segments. */
  delta = (long) mem_sp->mem_vir - (long) sp_click;
  lower = (delta > 0 ? sp_click : mem_sp->mem_vir);//栈的起始位置

  /* Add a safety margin for future stack growth. Impossible to do right. */
#define SAFETY_BYTES  (384 * sizeof(char *))
#define SAFETY_CLICKS ((SAFETY_BYTES + CLICK_SIZE - 1) / CLICK_SIZE)
  gap_base = mem_dp->mem_vir + data_clicks + SAFETY_CLICKS;
  if (lower < gap_base) /* data and stack collided */    
  if(allocate_new_mem(rmp,(phys_clicks)(rmp->mp_seg[S].mem_vir - rmp->mp_seg[D].mem_vir + rmp->mp_seg[S].mem_len)))
        return(ENOMEM); 

  /* Update data length (but not data orgin) on behalf of brk() system call. */
  old_clicks = mem_dp->mem_len;
  if (data_clicks != mem_dp->mem_len) {
	mem_dp->mem_len = data_clicks;
	changed |= DATA_CHANGED;
  }

  /* Update stack length and origin due to change in stack pointer. */
  if (delta > 0) {
	mem_sp->mem_vir -= delta;
	mem_sp->mem_phys -= delta;
	mem_sp->mem_len += delta;
	changed |= STACK_CHANGED;
  }

  /* Do the new data and stack segment sizes fit in the address space? */
  ft = (rmp->mp_flags & SEPARATE);
#if (CHIP == INTEL && _WORD_SIZE == 2)
  r = size_ok(ft, rmp->mp_seg[T].mem_len, rmp->mp_seg[D].mem_len, 
       rmp->mp_seg[S].mem_len, rmp->mp_seg[D].mem_vir, rmp->mp_seg[S].mem_vir);
#else
  r = (rmp->mp_seg[D].mem_vir + rmp->mp_seg[D].mem_len > 
          rmp->mp_seg[S].mem_vir) ? ENOMEM : OK;
#endif
  if (r == OK) {
	int r2;
	if (changed && (r2=sys_newmap(rmp->mp_endpoint, rmp->mp_seg)) != OK)
  		panic(__FILE__,"couldn't sys_newmap in adjust", r2);
	return(OK);
  }

  /* New sizes don't fit or require too many page/segment registers. Restore.*/
  if (changed & DATA_CHANGED) mem_dp->mem_len = old_clicks;
  if (changed & STACK_CHANGED) {
	mem_sp->mem_vir += delta;
	mem_sp->mem_phys += delta;
	mem_sp->mem_len -= delta;
  }
  return(ENOMEM);
}

#if (CHIP == INTEL && _WORD_SIZE == 2)
/*===========================================================================*
 *				size_ok  				     *
 *===========================================================================*/
PUBLIC int size_ok(file_type, tc, dc, sc, dvir, s_vir)
int file_type;			/* SEPARATE or 0 */
vir_clicks tc;			/* text size in clicks */
vir_clicks dc;			/* data size in clicks */
vir_clicks sc;			/* stack size in clicks */
vir_clicks dvir;		/* virtual address for start of data seg */
vir_clicks s_vir;		/* virtual address for start of stack seg */
{
/* Check to see if the sizes are feasible and enough segmentation registers
 * exist.  On a machine with eight 8K pages, text, data, stack sizes of
 * (32K, 16K, 16K) will fit, but (33K, 17K, 13K) will not, even though the
 * former is bigger (64K) than the latter (63K).  Even on the 8088 this test
 * is needed, since the data and stack may not exceed 4096 clicks.
 * Note this is not used for 32-bit Intel Minix, the test is done in-line.
 */

  int pt, pd, ps;		/* segment sizes in pages */

  pt = ( (tc << CLICK_SHIFT) + PAGE_SIZE - 1)/PAGE_SIZE;
  pd = ( (dc << CLICK_SHIFT) + PAGE_SIZE - 1)/PAGE_SIZE;
  ps = ( (sc << CLICK_SHIFT) + PAGE_SIZE - 1)/PAGE_SIZE;

  if (file_type == SEPARATE) {
	if (pt > MAX_PAGES || pd + ps > MAX_PAGES) return(ENOMEM);
  } else {
	if (pt + pd + ps > MAX_PAGES) return(ENOMEM);
  }

  if (dvir + dc > s_vir) return(ENOMEM);

  return(OK);
}
#endif