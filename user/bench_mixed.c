// bench_mixed.c — Mixed CPU+IO benchmark workload
//
// Alternates between CPU bursts and sleep periods,
// simulating interactive or mixed workloads.
//
// Usage: bench_mixed [phases]
// Default phases: 5  (each phase = ~20M CPU iters + 3 tick sleep)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int phases = 5;
  if (argc > 1)
    phases = atoi(argv[1]);

  for (int p = 0; p < phases; p++) {
    // CPU burst
    volatile long count = 0;
    for (long i = 0; i < 20000000L; i++)
      count++;
    // IO pause
    sleep(3);
  }

  exit(0);
}
