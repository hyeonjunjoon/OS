#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "spinlock.h"
#include "ipt.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// C-2: Inverted Page Table (IPT) structures
#define IPT_BUCKETS 1024

// struct ipt_entry is defined in ipt.h

struct {
  struct spinlock lock;
  struct ipt_entry *hash[IPT_BUCKETS];
} ipt;

// IPT hash function
static uint
ipt_hash_func(uint pfn)
{
  return pfn % IPT_BUCKETS;
}

// C-3: Software TLB (Translation Lookaside Buffer)
#define TLB_SIZE 64  // Number of TLB entries

struct tlb_entry {
  int valid;           // Entry is valid
  uint pid;            // Process ID
  uint va;             // Virtual address (page-aligned)
  uint pa;             // Physical address
  uint flags;          // PTE flags
};

struct {
  struct spinlock lock;
  struct tlb_entry entries[TLB_SIZE];
  uint next_victim;    // For FIFO replacement
} sw_tlb;

// C-3: TLB initialization
void
tlb_init(void)
{
  int i;
  initlock(&sw_tlb.lock, "sw_tlb");
  for(i = 0; i < TLB_SIZE; i++){
    sw_tlb.entries[i].valid = 0;
  }
  sw_tlb.next_victim = 0;
}

// C-3: TLB lookup
// Returns 1 if hit, 0 if miss
// On hit, fills pa_out and flags_out
int
tlb_lookup(uint pid, uint va, uint *pa_out, uint *flags_out)
{
  int i;
  uint va_page = va & ~(PGSIZE - 1);  // Page-align

  acquire(&sw_tlb.lock);
  for(i = 0; i < TLB_SIZE; i++){
    if(sw_tlb.entries[i].valid &&
       sw_tlb.entries[i].pid == pid &&
       sw_tlb.entries[i].va == va_page){
      // TLB hit!
      if(pa_out)
        *pa_out = sw_tlb.entries[i].pa + (va & (PGSIZE - 1));
      if(flags_out)
        *flags_out = sw_tlb.entries[i].flags;
      release(&sw_tlb.lock);
      return 1;
    }
  }
  release(&sw_tlb.lock);
  return 0;  // TLB miss
}

// C-3: TLB insert
// Uses FIFO replacement policy
void
tlb_insert(uint pid, uint va, uint pa, uint flags)
{
  uint va_page = va & ~(PGSIZE - 1);
  uint pa_page = pa & ~(PGSIZE - 1);

  acquire(&sw_tlb.lock);

  // Use FIFO replacement
  sw_tlb.entries[sw_tlb.next_victim].valid = 1;
  sw_tlb.entries[sw_tlb.next_victim].pid = pid;
  sw_tlb.entries[sw_tlb.next_victim].va = va_page;
  sw_tlb.entries[sw_tlb.next_victim].pa = pa_page;
  sw_tlb.entries[sw_tlb.next_victim].flags = flags;

  sw_tlb.next_victim = (sw_tlb.next_victim + 1) % TLB_SIZE;

  release(&sw_tlb.lock);
}

// C-3: TLB flush for a specific process
void
tlb_flush_pid(uint pid)
{
  int i;
  acquire(&sw_tlb.lock);
  for(i = 0; i < TLB_SIZE; i++){
    if(sw_tlb.entries[i].valid && sw_tlb.entries[i].pid == pid){
      sw_tlb.entries[i].valid = 0;
    }
  }
  release(&sw_tlb.lock);
}

// C-3: TLB flush all entries
void
tlb_flush_all(void)
{
  int i;
  acquire(&sw_tlb.lock);
  for(i = 0; i < TLB_SIZE; i++){
    sw_tlb.entries[i].valid = 0;
  }
  release(&sw_tlb.lock);
}

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir, 0);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space

  // C-3: Flush TLB entries for previous process on context switch
  tlb_flush_all();

  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// C-4: Added pid parameter for IPT tracking
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz, int pid)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz, pid);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz, pid);
      kfree(mem);
      return 0;
    }
    // C-4: Add IPT entry for user mappings
    if(pid > 0){
      uint pfn = V2P(mem) / PGSIZE;
      ipt_insert(pfn, pid, a, PTE_W|PTE_U|PTE_P);
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// C-4: Added pid parameter for IPT tracking
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz, int pid)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
      // C-4: Remove IPT entry
      if(pid > 0){
        uint pfn = pa / PGSIZE;
        ipt_remove(pfn, pid, a);
        // C-3: Flush TLB entry for this page
        tlb_flush_pid(pid);
      }
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
// C-4: Added pid parameter for IPT tracking
void
freevm(pde_t *pgdir, int pid)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0, pid);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
  // C-3: Flush all TLB entries for this process
  if(pid > 0){
    tlb_flush_pid(pid);
  }
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d, 0);  // C-4: pid=0 for cleanup on error
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

// C-1: Software page walker (sw_vtop)
// Translates virtual address to physical address without hardware access
// Returns 0 on success, -1 on failure
int
sw_vtop(pde_t *pgdir, const void *va, uint *pa_out, uint *pte_flags_out)
{
  pde_t *pde;
  pte_t *pgtab;
  pte_t *pte;
  uint pdx, ptx, offset;
  uint pa;

  if(pgdir == 0)
    return -1;

  // Get page directory index
  pdx = PDX(va);
  pde = &pgdir[pdx];

  // Check if page directory entry is present
  if((*pde & PTE_P) == 0)
    return -1;

  // Get page table from PDE
  pgtab = (pte_t*)P2V(PTE_ADDR(*pde));

  // Get page table index
  ptx = PTX(va);
  pte = &pgtab[ptx];

  // Check if page table entry is present
  if((*pte & PTE_P) == 0)
    return -1;

  // Extract physical frame address from PTE
  pa = PTE_ADDR(*pte);

  // Add offset within page
  offset = (uint)va & (PGSIZE - 1);
  pa = pa + offset;

  // Return physical address
  if(pa_out != 0)
    *pa_out = pa;

  // Return PTE flags
  if(pte_flags_out != 0)
    *pte_flags_out = PTE_FLAGS(*pte);

  return 0;
}

// C-2: IPT initialization
void
ipt_init(void)
{
  int i;
  initlock(&ipt.lock, "ipt");
  for(i = 0; i < IPT_BUCKETS; i++){
    ipt.hash[i] = 0;
  }
}

// C-2: IPT insert - add a mapping (pfn -> pid, va)
int
ipt_insert(uint pfn, uint pid, uint va, ushort flags)
{
  struct ipt_entry *e;
  uint bucket;

  // Allocate new entry
  if((e = (struct ipt_entry*)kalloc()) == 0)
    return -1;

  e->pfn = pfn;
  e->pid = pid;
  e->va = va & ~(PGSIZE - 1);  // Page-aligned
  e->flags = flags;
  e->refcnt = 1;

  acquire(&ipt.lock);

  // Insert at head of hash chain
  bucket = ipt_hash_func(pfn);
  e->next = ipt.hash[bucket];
  ipt.hash[bucket] = e;

  release(&ipt.lock);
  return 0;
}

// C-2: IPT remove - remove a specific (pfn, pid, va) mapping
int
ipt_remove(uint pfn, uint pid, uint va)
{
  struct ipt_entry *e, *prev;
  uint bucket;

  va = va & ~(PGSIZE - 1);  // Page-aligned

  acquire(&ipt.lock);

  bucket = ipt_hash_func(pfn);
  prev = 0;
  for(e = ipt.hash[bucket]; e != 0; prev = e, e = e->next){
    if(e->pfn == pfn && e->pid == pid && e->va == va){
      // Found it - remove from chain
      if(prev == 0)
        ipt.hash[bucket] = e->next;
      else
        prev->next = e->next;

      release(&ipt.lock);
      kfree((char*)e);
      return 0;
    }
  }

  release(&ipt.lock);
  return -1;  // Not found
}

// C-2: IPT remove all mappings for a PFN
void
ipt_remove_pfn(uint pfn)
{
  struct ipt_entry *e, *prev, *next;
  uint bucket;

  acquire(&ipt.lock);

  bucket = ipt_hash_func(pfn);
  prev = 0;
  e = ipt.hash[bucket];

  while(e != 0){
    next = e->next;
    if(e->pfn == pfn){
      // Remove this entry
      if(prev == 0)
        ipt.hash[bucket] = next;
      else
        prev->next = next;

      kfree((char*)e);
      e = next;
    } else {
      prev = e;
      e = next;
    }
  }

  release(&ipt.lock);
}

// C-2: IPT lookup - find all (pid, va) mappings for a PFN
// Returns number of entries copied (up to max)
int
ipt_lookup_pfn(uint pfn, struct ipt_entry *out, int max)
{
  struct ipt_entry *e;
  uint bucket;
  int count = 0;

  acquire(&ipt.lock);

  bucket = ipt_hash_func(pfn);
  for(e = ipt.hash[bucket]; e != 0 && count < max; e = e->next){
    if(e->pfn == pfn){
      out[count] = *e;
      count++;
    }
  }

  release(&ipt.lock);
  return count;
}

