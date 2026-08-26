# Benchmark Results

This directory is populated by `benchmarks/run_all.sh`.

Run from the repository root:

```bash
bash benchmarks/run_all.sh
```

Files produced here:

| File               | Contents                                         |
|--------------------|--------------------------------------------------|
| `RR.csv`           | Raw per-process metrics for Round-Robin          |
| `LBS.csv`          | Raw per-process metrics for LBS                  |
| `MLFQ.csv`         | Raw per-process metrics for MLFQ                 |
| `CFS.csv`          | Raw per-process metrics for CFS                  |
| `combined.csv`     | All schedulers merged into one file              |
| `summary.txt`      | Human-readable statistics table                  |
| `comparison.csv`   | Machine-readable per-(scheduler,workload) stats  |
| `comparison.png`   | Bar chart: turnaround and wait time              |
| `rtime_comparison.png` | Bar chart: CPU time                          |

See `benchmarks/METHODOLOGY.md` for full methodology documentation.
