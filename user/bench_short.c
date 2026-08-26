// bench_short.c — Short-jobs benchmark workload
//
// Runs a sequence of N very short processes serially (one at a time).
// Measures overhead of process creation, scheduling, and reaping.
//
// Usage: bench_short [njobs]
// Default: 16 short jobs

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int njobs = 16;
  if (argc > 1)
    njobs = atoi(argv[1]);

  for (int i = 0; i < njobs; i++) {
    int pid = fork();
    if (pid == 0) {
      // Very short work: ~1M iterations
      volatile long c = 0;
      for (long j = 0; j < 1000000L; j++)
        c++;
      exit(0);
    }
    wait(0);  // wait for each before forking next
  }

  exit(0);
}
