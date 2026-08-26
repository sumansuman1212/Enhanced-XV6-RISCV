// memstat.c — Display system-wide memory statistics
//
// Usage: memstat
//
// Example output:
//
//   === System Memory Statistics ===
//   Total allocations : 4821
//   Total frees       : 4610
//   Active pages      :  211   (844 KB)
//   Peak pages        :  284  (1136 KB)
//   Free pages        : 28945 (~113 MB)
//   Page faults       :   42
//   CoW faults        :   17
//
// All values are cumulative since boot.
// "Free pages" is an approximation (no lock held during snapshot).

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user/memstat.h"

#define PGSIZE 4096

static void
print_kb(uint64 pages)
{
  uint64 kb = pages * PGSIZE / 1024;
  if (kb >= 1024)
    printf("%llu MB", kb / 1024);
  else
    printf("%llu KB", kb);
}

int
main(void)
{
  struct memstats s;

  if (memstat(&s) < 0) {
    fprintf(2, "memstat: syscall failed\n");
    exit(1);
  }

  printf("\n=== System Memory Statistics ===\n");
  printf("  Total allocations : %llu\n",  s.total_allocs);
  printf("  Total frees       : %llu\n",  s.total_frees);
  printf("  Active pages      : %llu  (", s.active_pages);
  print_kb(s.active_pages);
  printf(")\n");
  printf("  Peak pages        : %llu  (", s.peak_pages);
  print_kb(s.peak_pages);
  printf(")\n");
  printf("  Free pages        : %llu  (~", s.free_pages);
  print_kb(s.free_pages);
  printf(")\n");
  printf("  Page faults       : %llu\n",  s.total_faults);
  printf("  CoW faults        : %llu\n",  s.cow_faults);
  printf("\n");

  exit(0);
}
