# Benchmark Methodology

## Overview

This document describes how the Enhanced-XV6 scheduler benchmarks are
designed, executed, and interpreted.  All claims in the benchmark results
are derived from actual measured output.  No numbers are fabricated.

---

## Hardware / QEMU Configuration

| Parameter            | Value                        |
|----------------------|------------------------------|
| Machine              | QEMU `virt` (RISC-V)         |
| Memory               | 128 MB                       |
| CPUs                 | 1 (set `CPUS=1` for fairness)|
| Disk                 | VirtIO block, RAW image      |
| Timer frequency      | ~100 Hz (1 tick ≈ 10 ms)     |
| QEMU version         | ≥ 6.0 recommended            |

> **Important:** Use `CPUS=1` for all comparison runs.  With multiple CPUs,
> scheduler behaviour is influenced by per-CPU load balancing effects that
> are scheduler-specific and make direct comparison unreliable.

---

## Schedulers Tested

| Scheduler | Compile flag    | Description                             |
|-----------|-----------------|-----------------------------------------|
| RR        | `SCHEDULER=RR`  | Round-Robin (default xv6)               |
| LBS       | `SCHEDULER=LBS` | Lottery-Based Scheduling                |
| MLFQ      | `SCHEDULER=MLFQ`| Multi-Level Feedback Queue (4 queues)   |
| CFS       | `SCHEDULER=CFS` | CFS-inspired (min-vruntime, weight)     |

---

## Workloads

| Name         | Program           | Description                                        |
|--------------|-------------------|----------------------------------------------------|
| cpu_bound    | `bench_cpu.c`     | Pure CPU loop, 80M iterations                      |
| io_bound     | `bench_io.c`      | 8× sleep(5) — simulates blocked IO                 |
| mixed        | `bench_mixed.c`   | 4 phases of 20M CPU iters + sleep(3)               |
| many_procs   | `bench_many.c`    | 8 concurrent CPU-bound children                    |
| short_jobs   | `bench_short.c`   | 12 sequential 1M-iteration jobs                    |
| long_jobs    | `bench_long.c`    | 2 concurrent 200M-iteration processes              |

---

## Metrics

All metrics are measured using the `waitx(addr, &wtime, &rtime)` syscall,
which is built in to this xv6 implementation.

| Metric      | Definition                                      | Unit   |
|-------------|-------------------------------------------------|--------|
| `rtime`     | Ticks the process spent in RUNNING state        | ticks  |
| `wtime`     | `etime - ctime - rtime` (waiting + sleeping)    | ticks  |
| `turnaround`| `rtime + wtime` = total wall-clock ticks        | ticks  |

`ctime` = tick when process was created (fork)
`etime` = tick when process exited

---

## Statistical Methodology

Each workload is run **3 times per scheduler** (configurable via `REPS=N`).
The `analyze.py` script computes per-(scheduler, workload) group:

- **Mean** — arithmetic mean over all measured process instances
- **Median** — median of measured values
- **Min / Max** — range
- **Standard deviation** — sample std-dev (N-1 denominator)

> With only 3 repetitions, standard deviation estimates are noisy.
> Increase `REPS` (e.g. `REPS=10`) for more reliable std-dev estimates.

---

## Execution Instructions

```bash
# From the repository root:

# Option 1: run all schedulers automatically
bash benchmarks/run_all.sh

# Option 2: run manually for one scheduler
make clean && make qemu SCHEDULER=RR CPUS=1
# Inside xv6:
$ runbench RR 3

# Analyse captured CSV (after run_all.sh produces combined.csv):
python3 benchmarks/analyze.py benchmarks/results/combined.csv benchmarks/results/
```

Output files produced in `benchmarks/results/`:

| File               | Contents                                   |
|--------------------|--------------------------------------------|
| `RR.csv`           | Raw CSV rows for Round-Robin               |
| `LBS.csv`          | Raw CSV rows for LBS                       |
| `MLFQ.csv`         | Raw CSV rows for MLFQ                      |
| `CFS.csv`          | Raw CSV rows for CFS                       |
| `combined.csv`     | All rows merged                            |
| `summary.txt`      | Human-readable statistics table            |
| `comparison.csv`   | Machine-readable per-group statistics      |
| `comparison.png`   | Bar charts (turnaround, wait time)         |
| `rtime_comparison.png` | CPU time bar chart                     |

---

## Known Limitations

1. **Timer granularity:** xv6 timer fires at ~100 Hz.  Processes that run
   for less than 1 tick may show `rtime=0`.  Short-job workloads are most
   affected.

2. **Single-CPU only:** All comparison runs must use `CPUS=1`.  Multi-CPU
   runs are valid for scalability testing but not for direct comparisons.

3. **No cache effects:** QEMU does not model cache behaviour.  Results
   reflect instruction-count-driven timing only.

4. **MLFQ requires `CPUS=1`:** The existing MLFQ implementation has a
   known race condition on multi-CPU configurations (documented in
   `PROJECT_AUDIT.md`).

5. **Iteration counts are fixed:** The CPU workload loop count is tuned
   for reasonable runtime on typical QEMU installations.  On slower
   machines, the `cpu_bound` workload may take significantly longer.

6. **No I/O disk benchmarks:** The `io_bound` workload uses `sleep()`,
   not actual disk I/O, because xv6 filesystem benchmarks add filesystem
   state dependencies that make reproducibility harder.

---

## Interpreting Results

- A **lower turnaround** is generally better for interactive workloads.
- A **lower wtime** indicates the scheduler reduces unnecessary waiting.
- **rtime** should be roughly equal across schedulers for CPU-bound work
  (the same amount of CPU work must be done regardless of scheduler).
- For **io_bound** workloads, rtime will be very low and wtime will
  dominate — this is expected behaviour, not a scheduler deficiency.
- For **fairness comparison** (CFS vs RR), look at the spread (std-dev)
  of rtime across the `many_procs` workload.  A smaller std-dev indicates
  more equal CPU distribution.
