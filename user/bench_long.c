// bench_long.c — Long-jobs benchmark workload
//
// Runs 2 concurrent long-running CPU-bound processes.
// Tests scheduler behavior with few, long-lived processes.
//
// Usage: bench_long [iters_million]
// Default: 200 million iterations per process

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int iters = 200;  // millions
  if (argc > 1)
    iters = atoi(argv[1]);

  long limit = (long)iters * 1000000L;

  int pid1 = fork();
  if (pid1 == 0) {
    volatile long c = 0;
    for (long i = 0; i < limit; i++) c++;
    exit(0);
  }

  int pid2 = fork();
  if (pid2 == 0) {
    volatile long c = 0;
    for (long i = 0; i < limit; i++) c++;
    exit(0);
  }

  wait(0);
  wait(0);
  exit(0);
}
