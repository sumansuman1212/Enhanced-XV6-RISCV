// bench_io.c — IO-bound benchmark workload
//
// Repeatedly sleeps to simulate IO-bound behavior.
// This causes frequent voluntary context switches.
//
// Usage: bench_io [sleep_ticks] [repetitions]
// Defaults: sleep_ticks=5, repetitions=10

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int ticks = 5;
  int reps  = 10;
  if (argc > 1) ticks = atoi(argv[1]);
  if (argc > 2) reps  = atoi(argv[2]);

  for (int i = 0; i < reps; i++)
    sleep(ticks);

  exit(0);
}
