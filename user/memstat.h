// user/memstat.h — User-space definition of memory statistics structures.
//
// Must match kernel/memstats.h exactly (same field order, same types).
// Include this in user programs that call memstat().

#ifndef USER_MEMSTAT_H
#define USER_MEMSTAT_H

#include "kernel/types.h"

// System-wide memory statistics (filled by memstat syscall).
struct memstats {
  uint64 total_allocs;    // total successful kalloc() calls since boot
  uint64 total_frees;     // total kfree() calls that freed a page
  uint64 active_pages;    // current allocated physical pages
  uint64 peak_pages;      // maximum active_pages ever
  uint64 total_faults;    // total page faults handled
  uint64 cow_faults;      // total CoW faults handled
  uint64 free_pages;      // current free pages (approximate)
};

// Per-process memory info (filled by getprocmem syscall, used by procstat).
struct proc_meminfo {
  int    pid;
  char   name[16];
  uint64 vm_size;         // virtual address space bytes (p->sz)
  uint64 pages;           // vm_size / 4096
  uint64 faults;          // page faults for this process
  uint64 cow_faults;      // CoW faults for this process
};

#endif // USER_MEMSTAT_H
