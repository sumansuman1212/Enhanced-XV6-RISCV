// bench_cpu.c — CPU-bound benchmark workload
//
// Executes a pure CPU loop for a configurable number of iterations.
// Reports: workload=cpu_bound, pid, ctime, etime, rtime, wtime
//
// Usage: bench_cpu [iterations_million]
// Default iterations: 100 million
//
// Output format (CSV row, printed to stdout):
//   cpu_bound,<pid>,<ctime>,<etime>,<rtime>,<wtime>,<turnaround>

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int iters = 100;  // millions of iterations
  if (argc > 1)
    iters = atoi(argv[1]);

  // Each iteration is a simple volatile increment
  volatile long count = 0;
  long limit = (long)iters * 1000000L;
  for (long i = 0; i < limit; i++)
    count++;

  exit(0);
}
