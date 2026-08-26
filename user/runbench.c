// runbench.c — Reproducible Scheduler Benchmark Driver
//
// Runs a suite of workloads, collects per-process metrics via waitx,
// and emits one CSV row per process to stdout.
//
// CSV header:
//   scheduler,workload,run,pid,rtime,wtime,turnaround
//
// Fields:
//   scheduler   — string passed as argv[1] (e.g. "RR", "LBS", "MLFQ", "CFS")
//   workload    — workload name (cpu_bound, io_bound, mixed, many_procs,
//                               short_jobs, long_jobs)
//   run         — repetition index (0-based) within this workload
//   pid         — child process ID returned by fork()
//   rtime       — CPU ticks the process actually ran (from waitx)
//   wtime       — ticks spent waiting/sleeping (etime - ctime - rtime)
//   turnaround  — rtime + wtime  (total elapsed ticks from creation to exit)
//
// Usage:
//   runbench <scheduler_name> [repetitions]
//
// Example (run inside xv6):
//   runbench RR 3
//
// The driver repeats every workload <repetitions> times.
// Default repetitions: 3
//
// Design notes:
//   - All timing comes from waitx; no external clock is needed.
//   - Workloads are isolated: each run forks fresh children.
//   - The CSV header line is printed once at startup.
//   - A "---" separator line is printed between workloads for readability.
//   - This file is intentionally self-contained (no separate workload .c files
//     are exec'd; workloads are implemented inline as fork()ed functions).
//     This keeps the benchmark reproducible even without all workload programs.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// ── constants ─────────────────────────────────────────────────────────────────

#define DEFAULT_REPS   3        // repetitions per workload per run
#define MANY_NPROCS    8        // processes in many_procs workload
#define SHORT_NJOBS    12       // jobs in short_jobs workload
#define CPU_ITERS      80000000L  // iterations for cpu workloads
#define LONG_ITERS    200000000L  // iterations for long_jobs
#define IO_SLEEP_TICKS  5       // sleep ticks per IO cycle
#define IO_REPS         8       // IO cycles in io_bound workload
#define MIXED_PHASES    4       // phases in mixed workload

// ── CSV output helpers ────────────────────────────────────────────────────────

static const char *sched_name;  // set from argv[1]

// Print one CSV row.
static void
emit_row(const char *workload, int run, int pid, int rtime, int wtime)
{
  int turnaround = rtime + wtime;
  printf("%s,%s,%d,%d,%d,%d,%d\n",
         sched_name, workload, run, pid, rtime, wtime, turnaround);
}

// ── workload implementations ──────────────────────────────────────────────────
// Each workload is run in a child process.  The parent collects results
// via waitx and emits a CSV row.

// ── 1. cpu_bound ─────────────────────────────────────────────────────────────
// Pure CPU loop: one process per rep.

static void
run_cpu_bound(int rep)
{
  int wtime = 0, rtime = 0;
  int pid = fork();
  if (pid == 0) {
    volatile long c = 0;
    for (long i = 0; i < CPU_ITERS; i++) c++;
    exit(0);
  }
  if (pid < 0) return;
  int wpid = waitx(0, &wtime, &rtime);
  if (wpid > 0)
    emit_row("cpu_bound", rep, wpid, rtime, wtime);
}

// ── 2. io_bound ──────────────────────────────────────────────────────────────
// Repeated sleeps simulate IO-bound behavior.

static void
run_io_bound(int rep)
{
  int wtime = 0, rtime = 0;
  int pid = fork();
  if (pid == 0) {
    for (int i = 0; i < IO_REPS; i++)
      sleep(IO_SLEEP_TICKS);
    exit(0);
  }
  if (pid < 0) return;
  int wpid = waitx(0, &wtime, &rtime);
  if (wpid > 0)
    emit_row("io_bound", rep, wpid, rtime, wtime);
}

// ── 3. mixed ─────────────────────────────────────────────────────────────────
// Alternating CPU burst + sleep.

static void
run_mixed(int rep)
{
  int wtime = 0, rtime = 0;
  int pid = fork();
  if (pid == 0) {
    for (int p = 0; p < MIXED_PHASES; p++) {
      volatile long c = 0;
      for (long i = 0; i < 20000000L; i++) c++;
      sleep(3);
    }
    exit(0);
  }
  if (pid < 0) return;
  int wpid = waitx(0, &wtime, &rtime);
  if (wpid > 0)
    emit_row("mixed", rep, wpid, rtime, wtime);
}

// ── 4. many_procs ────────────────────────────────────────────────────────────
// N concurrent CPU-bound processes.  Emit one row per child.

static void
run_many_procs(int rep)
{
  // Fork all children first, then reap
  int pids[MANY_NPROCS];
  int spawned = 0;

  for (int i = 0; i < MANY_NPROCS; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      volatile long c = 0;
      for (long j = 0; j < 10000000L; j++) c++;
      exit(0);
    }
    if (pids[i] > 0)
      spawned++;
  }

  // Collect results — waitx returns any child, not necessarily in order
  for (int i = 0; i < spawned; i++) {
    int wtime = 0, rtime = 0;
    int wpid = waitx(0, &wtime, &rtime);
    if (wpid > 0)
      emit_row("many_procs", rep, wpid, rtime, wtime);
  }
}

// ── 5. short_jobs ────────────────────────────────────────────────────────────
// SHORT_NJOBS sequential very-short processes.

static void
run_short_jobs(int rep)
{
  for (int i = 0; i < SHORT_NJOBS; i++) {
    int wtime = 0, rtime = 0;
    int pid = fork();
    if (pid == 0) {
      volatile long c = 0;
      for (long j = 0; j < 1000000L; j++) c++;
      exit(0);
    }
    if (pid < 0) continue;
    int wpid = waitx(0, &wtime, &rtime);
    if (wpid > 0)
      emit_row("short_jobs", rep, wpid, rtime, wtime);
  }
}

// ── 6. long_jobs ─────────────────────────────────────────────────────────────
// 2 concurrent long-running CPU-bound processes.

static void
run_long_jobs(int rep)
{
  int pids[2];
  int spawned = 0;

  for (int i = 0; i < 2; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      volatile long c = 0;
      for (long j = 0; j < LONG_ITERS; j++) c++;
      exit(0);
    }
    if (pids[i] > 0) spawned++;
  }

  for (int i = 0; i < spawned; i++) {
    int wtime = 0, rtime = 0;
    int wpid = waitx(0, &wtime, &rtime);
    if (wpid > 0)
      emit_row("long_jobs", rep, wpid, rtime, wtime);
  }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(2, "Usage: runbench <scheduler> [repetitions]\n");
    fprintf(2, "  scheduler: RR | LBS | MLFQ | CFS\n");
    fprintf(2, "  repetitions: number of times each workload runs (default %d)\n",
            DEFAULT_REPS);
    exit(1);
  }

  sched_name = argv[1];
  int reps = DEFAULT_REPS;
  if (argc >= 3)
    reps = atoi(argv[2]);
  if (reps <= 0) reps = 1;
  if (reps > 10) reps = 10;  // cap to prevent excessive runtime

  // Print CSV header
  printf("scheduler,workload,run,pid,rtime,wtime,turnaround\n");

  // Run each workload for 'reps' repetitions
  for (int r = 0; r < reps; r++) {
    run_cpu_bound(r);
    run_io_bound(r);
    run_mixed(r);
    run_many_procs(r);
    run_short_jobs(r);
    run_long_jobs(r);
  }

  printf("# benchmark complete: scheduler=%s reps=%d\n", sched_name, reps);
  exit(0);
}
