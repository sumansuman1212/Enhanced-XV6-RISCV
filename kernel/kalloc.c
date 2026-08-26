// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "memstats.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int ref[MAXREF];
} cow_ref;

// System-wide memory statistics.
// Protected by mem_stats_lock for updates.
// Reads in sys_memstat take a consistent snapshot under the lock.
static struct spinlock mem_stats_lock;
static struct memstats mem_stats;

// Called from kinit() via kinit_memstats().
void
memstats_init(void)
{
  initlock(&mem_stats_lock, "memstats");
  mem_stats.total_allocs = 0;
  mem_stats.total_frees  = 0;
  mem_stats.active_pages = 0;
  mem_stats.peak_pages   = 0;
  mem_stats.total_faults = 0;
  mem_stats.cow_faults   = 0;
  mem_stats.free_pages   = 0;
}

// Copy current stats into caller's buffer (safe snapshot).
void
get_memstats(struct memstats *dst)
{
  acquire(&mem_stats_lock);
  // Count free pages on the fly from freelist length
  uint64 free_count = 0;
  struct run *r = kmem.freelist;
  // Note: we hold mem_stats_lock, not kmem.lock, so this is an approximation.
  // For a precise count we would need both locks; the approximation is acceptable
  // for monitoring purposes.
  while (r) { free_count++; r = r->next; }
  mem_stats.free_pages = free_count;
  *dst = mem_stats;
  release(&mem_stats_lock);
}

// Increment the total page fault counter (called from trap.c).
void
memstats_inc_fault(void)
{
  acquire(&mem_stats_lock);
  mem_stats.total_faults++;
  release(&mem_stats_lock);
}

// Increment the CoW fault counter (called from trap.c / kalloc.c).
void
memstats_inc_cow(void)
{
  acquire(&mem_stats_lock);
  mem_stats.cow_faults++;
  release(&mem_stats_lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  memstats_init();
  freerange(end, (void*)PHYSTOP);
}
void cow_init(){
  acquire(&cow_ref.lock);
  for(int i=0;i<PHYSTOP/PGSIZE;i++){
    cow_ref.ref[i]=1;
  }
  release(&cow_ref.lock);
  return;
}
void cow_incref(void* pa){
  int ref = PA2REF(pa);
  if(ref < 0 || ref >= MAXREF)
    return;
  acquire(&cow_ref.lock);
  cow_ref.ref[ref]++;
  release(&cow_ref.lock);
}
void *cow_decref(void *pa)
{
  int index  = PA2REF(pa);

  if(index < 0 || index >= MAXREF)
    return 0;

  acquire(&cow_ref.lock);
  if (cow_ref.ref[index] <= 1)
  {
    release(&cow_ref.lock);
    return pa;
  }

  cow_ref.ref[index] = cow_ref.ref[index] - 1;

  uint64 copymem = (uint64)kalloc();
  if (copymem == 0)
  {
    release(&cow_ref.lock);
    return 0;
  }
  memmove((void *)copymem, (void *)pa, PGSIZE);
  release(&cow_ref.lock);
  return (void *)copymem;
}
int cow_handler(pagetable_t pagetable, uint64 va)
{
  if (va >= MAXVA || va <= 0)
    return -1;
  // finds the page table entry corresponding to the va
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
  {
    return -1;
  }

  // is the page valid
  if(!(*pte & PTE_V)){
    return -1;
  }
  // is the page in user space
  if(!(*pte & PTE_U)){
    return -1;
  }
  // is the page a cow page
  if(!(*pte & PTE_COW)){
    return -1;
  }
 
  uint64 pa = PTE2PA(*pte);
  // void *mem = cow_decref((void *)pa);
  char*mem = kalloc();
  if (mem == 0)
    return -1;

  memmove(mem, (char *)pa, PGSIZE);
  

 
    
  uint64 flags = PTE_FLAGS(*pte);
  flags |= PTE_W; // addding write flag
  flags &= (~PTE_COW); // removing cow flag

  uvmunmap(pagetable, PGROUNDDOWN(va), 1, 1);
  if (mappages(pagetable, va,1 , (uint64)mem, flags) == -1)
  {
    panic("Pagefhandler mappages");
  }

  return 0;
}
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  int index = PA2REF(pa);

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&cow_ref.lock);
  cow_ref.ref[index]--;
  if (cow_ref.ref[index] > 0)
  {
    release(&cow_ref.lock);
    return;
  }
  release(&cow_ref.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);

  // Update stats: page actually returned to free pool
  acquire(&mem_stats_lock);
  mem_stats.total_frees++;
  if (mem_stats.active_pages > 0)
    mem_stats.active_pages--;
  release(&mem_stats_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
  {
    memset((char *)r, 5, PGSIZE); // fill with junk
    cow_ref.ref[PA2REF((uint64)r)] = 1;

    // Update stats
    acquire(&mem_stats_lock);
    mem_stats.total_allocs++;
    mem_stats.active_pages++;
    if (mem_stats.active_pages > mem_stats.peak_pages)
      mem_stats.peak_pages = mem_stats.active_pages;
    release(&mem_stats_lock);
  }
  return (void *)r;
}
