// User-visible process snapshot.  This deliberately contains values only:
// no kernel pointers, locks, or page-table addresses are exposed.
#ifndef PROCINFO_H
#define PROCINFO_H

#include "types.h"

struct procinfo {
  int pid;
  int ppid;
  int state;
  char name[16];
  uint64 vm_bytes;
  uint64 mapped_pages;
  uint64 cpu_ticks;
  uint64 wait_ticks;
  uint64 response_ticks;
  uint64 ctx_switches;
  uint64 vruntime;
  uint64 page_faults;
  uint64 cow_faults;
};

#endif
