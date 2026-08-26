// bench_many.c — Many-processes benchmark workload
//
// Forks N concurrent CPU-bound processes and waits for all to finish.
// Tests scheduler overhead under high process count.
//
// Usage: bench_many [nprocs]
// Default: 8 processes (NPROC limit in xv6 is 64; keep headroom)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int nprocs = 8;
  if (argc > 1)
    nprocs = atoi(argv[1]);

  for (int i = 0; i < nprocs; i++) {
    int pid = fork();
    if (pid == 0) {
      // Each child: small CPU burst
      volatile long c = 0;
      for (long j = 0; j < 10000000L; j++)
        c++;
      exit(0);
    }
    if (pid < 0)
      break;
  }

  // Wait for all children
  while (wait(0) >= 0)
    ;

  exit(0);
}
