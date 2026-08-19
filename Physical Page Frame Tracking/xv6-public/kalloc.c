// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// A-1: Physical frame information structure
struct physframe_info {
  uint frame_index;
  int allocated;
  int pid;
  uint start_tick;
};

#define PFNNUM 60000
struct physframe_info pf_info[PFNNUM];

// A-1: Initialize physical frame information table
void
init_pf_info(void)
{
  int i;
  for(i = 0; i < PFNNUM; i++){
    pf_info[i].frame_index = i;
    pf_info[i].allocated = 0;
    pf_info[i].pid = -1;
    pf_info[i].start_tick = 0;
  }
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
static int pf_info_initialized = 0;

void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;

  // Initialize frame info table after full page table is set up
  if(!pf_info_initialized){
    init_pf_info();
    pf_info_initialized = 1;
  }
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;
  uint pa, frame_idx;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);

  // A-2: Update frame info when freeing
  if(pf_info_initialized){
    pa = V2P(v);
    frame_idx = pa / PGSIZE;
    if(frame_idx < PFNNUM){
      pf_info[frame_idx].allocated = 0;
      pf_info[frame_idx].pid = -1;
      pf_info[frame_idx].start_tick = 0;
    }
  }

  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  uint pa, frame_idx;
  struct proc *curproc;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;

    // A-2: Update frame info when allocating
    // Only track user process allocations
    if(pf_info_initialized){
      curproc = myproc();
      if(curproc != 0){
        pa = V2P((char*)r);
        frame_idx = pa / PGSIZE;
        if(frame_idx < PFNNUM){
          pf_info[frame_idx].allocated = 1;
          pf_info[frame_idx].pid = curproc->pid;
          acquire(&tickslock);
          pf_info[frame_idx].start_tick = ticks;
          release(&tickslock);
        }
      }
    }
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

// A-3: Dump physical memory frame information to user space
int
dump_physmem_info(void *addr, int max_entries)
{
  struct proc *curproc = myproc();
  int i, copied = 0;

  if(!pf_info_initialized)
    return -1;

  if(max_entries <= 0 || max_entries > PFNNUM)
    max_entries = PFNNUM;

  if(kmem.use_lock)
    acquire(&kmem.lock);

  for(i = 0; i < PFNNUM && copied < max_entries; i++){
    if(copyout(curproc->pgdir, (uint)addr + copied * sizeof(struct physframe_info),
               &pf_info[i], sizeof(struct physframe_info)) < 0){
      if(kmem.use_lock)
        release(&kmem.lock);
      return -1;
    }
    copied++;
  }

  if(kmem.use_lock)
    release(&kmem.lock);

  return copied;
}

