// Must match kernel/procinfo.h exactly.
#ifndef USER_PROCINFO_H
#define USER_PROCINFO_H

#include "kernel/types.h"

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
