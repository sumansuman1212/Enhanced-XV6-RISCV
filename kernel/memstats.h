// memstats.h — System-wide memory statistics structure
//
// Shared between kalloc.c (which fills it) and sysproc.c (which exports it).
// All fields are updated atomically under mem_stats_lock in kalloc.c.
//
// Counters are cumulative since boot unless noted.

#ifndef MEMSTATS_H
#define MEMSTATS_H

#include "types.h"

struct memstats {
  uint64 total_allocs;    // total successful kalloc() calls since boot
  uint64 total_frees;     // total kfree() calls that actually freed a page
  uint64 active_pages;    // current number of allocated physical pages
  uint64 peak_pages;      // maximum active_pages ever observed
  uint64 total_faults;    // total page faults handled (all processes)
  uint64 cow_faults;      // total CoW faults handled (all processes)
  uint64 free_pages;      // current free pages (snapshot; may race)
};

// User-visible per-process memory stats (filled by getprocmem syscall, used by procstat)
struct proc_meminfo {
  int    pid;
  char   name[16];
  uint64 vm_size;         // virtual address space size (p->sz bytes)
  uint64 pages;           // vm_size / PGSIZE (approximate RSS)
  uint64 faults;          // page faults attributed to this process
  uint64 cow_faults;      // CoW faults attributed to this process
};

#endif // MEMSTATS_H
