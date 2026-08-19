#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "ipt.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Dump physical memory frame information to user space
int
sys_dump_physmem_info(void)
{
  char *addr;
  int max_entries;

  if(argptr(0, &addr, sizeof(void*)) < 0)
    return -1;
  if(argint(1, &max_entries) < 0)
    return -1;

  return dump_physmem_info((void*)addr, max_entries);
}

// C-5: System call for sw_vtop
// C-3: Enhanced with TLB caching
int
sys_vtop(void)
{
  char *va;
  uint *pa_out;
  uint *flags_out;
  uint pa, flags;
  int ret;
  struct proc *curproc = myproc();

  if(argptr(0, &va, sizeof(void*)) < 0)
    return -1;
  if(argptr(1, (char**)&pa_out, sizeof(uint*)) < 0)
    return -1;
  if(argptr(2, (char**)&flags_out, sizeof(uint*)) < 0)
    return -1;

  // C-3: Try TLB lookup first
  if(tlb_lookup(curproc->pid, (uint)va, &pa, &flags)){
    // TLB hit! Return cached result
    if(pa_out)
      *pa_out = pa;
    if(flags_out)
      *flags_out = flags;
    return 0;
  }

  // TLB miss - use page walker
  ret = sw_vtop(curproc->pgdir, va, &pa, &flags);

  if(ret == 0){
    // C-3: Insert into TLB for future lookups
    tlb_insert(curproc->pid, (uint)va, pa, flags);

    // Return results
    if(pa_out)
      *pa_out = pa;
    if(flags_out)
      *flags_out = flags;
  }

  return ret;
}

// C-5: System call for phys2virt (reverse lookup using IPT)
int
sys_phys2virt(void)
{
  uint pa_page;
  char *out;
  int max;
  struct ipt_entry entries[16];
  int count;

  if(argint(0, (int*)&pa_page) < 0)
    return -1;
  if(argptr(1, &out, sizeof(void*)) < 0)
    return -1;
  if(argint(2, &max) < 0)
    return -1;

  if(max > 16)
    max = 16;

  // Convert PA to PFN
  uint pfn = pa_page / PGSIZE;

  // Lookup IPT
  count = ipt_lookup_pfn(pfn, entries, max);

  // Copy to user space
  if(count > 0){
    struct proc *curproc = myproc();
    if(copyout(curproc->pgdir, (uint)out, entries, count * sizeof(struct ipt_entry)) < 0)
      return -1;
  }

  return count;
}
