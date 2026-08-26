#!/usr/bin/env python3
"""
benchmarks/analyze.py — Statistical analysis of scheduler benchmark results

Usage:
    python3 benchmarks/analyze.py <combined_csv> [output_dir]

Input:
    combined_csv — path to combined.csv produced by run_all.sh
    output_dir   — where to write report files (default: benchmarks/results)

Output files produced:
    summary.txt     — human-readable per-scheduler per-workload statistics
    comparison.csv  — machine-readable comparison table
    comparison.png  — bar charts comparing schedulers (if matplotlib available)

Metrics computed per (scheduler, workload) group:
    count        — number of measured processes
    rtime_mean   — mean CPU ticks (runtime)
    rtime_median — median CPU ticks
    rtime_min    — min CPU ticks
    rtime_max    — max CPU ticks
    rtime_std    — standard deviation of CPU ticks
    wtime_mean   — mean wait ticks
    wtime_std    — standard deviation of wait ticks
    turnaround_mean  — mean turnaround ticks
    turnaround_std   — standard deviation of turnaround

IMPORTANT: All numbers are raw xv6 timer ticks.
           One tick ≈ 10ms at the default QEMU timer frequency.
           Do NOT compare absolute tick values across machines or
           QEMU configurations with different timer rates.
"""

import sys
import os
import csv
import math
from collections import defaultdict

# ── statistics helpers (no external dependencies) ─────────────────────────────

def mean(vals):
    if not vals: return 0.0
    return sum(vals) / len(vals)

def median(vals):
    if not vals: return 0.0
    s = sorted(vals)
    n = len(s)
    mid = n // 2
    return s[mid] if n % 2 == 1 else (s[mid-1] + s[mid]) / 2.0

def stddev(vals):
    if len(vals) < 2: return 0.0
    m = mean(vals)
    variance = sum((x - m) ** 2 for x in vals) / (len(vals) - 1)
    return math.sqrt(variance)

def fmt(v, decimals=1):
    """Format a float to fixed decimals."""
    return f"{v:.{decimals}f}"

# ── data loading ──────────────────────────────────────────────────────────────

def load_csv(path):
    """
    Load the combined CSV.
    Returns list of dicts with keys:
      scheduler, workload, run, pid, rtime, wtime, turnaround
    Skips comment lines (starting with #) and blank lines.
    """
    rows = []
    with open(path, newline='') as f:
        reader = csv.DictReader(
            (line for line in f if not line.startswith('#') and line.strip()),
        )
        for row in reader:
            try:
                rows.append({
                    'scheduler':  row['scheduler'].strip(),
                    'workload':   row['workload'].strip(),
                    'run':        int(row['run']),
                    'pid':        int(row['pid']),
                    'rtime':      int(row['rtime']),
                    'wtime':      int(row['wtime']),
                    'turnaround': int(row['turnaround']),
                })
            except (KeyError, ValueError):
                continue  # skip malformed rows
    return rows

# ── aggregation ───────────────────────────────────────────────────────────────

def aggregate(rows):
    """
    Group rows by (scheduler, workload).
    Returns dict: (sched, workload) -> stats_dict
    """
    groups = defaultdict(lambda: defaultdict(list))
    for r in rows:
        key = (r['scheduler'], r['workload'])
        groups[key]['rtime'].append(r['rtime'])
        groups[key]['wtime'].append(r['wtime'])
        groups[key]['turnaround'].append(r['turnaround'])

    stats = {}
    for key, fields in groups.items():
        rt = fields['rtime']
        wt = fields['wtime']
        ta = fields['turnaround']
        stats[key] = {
            'count':           len(rt),
            'rtime_mean':      mean(rt),
            'rtime_median':    median(rt),
            'rtime_min':       min(rt) if rt else 0,
            'rtime_max':       max(rt) if rt else 0,
            'rtime_std':       stddev(rt),
            'wtime_mean':      mean(wt),
            'wtime_std':       stddev(wt),
            'turnaround_mean': mean(ta),
            'turnaround_std':  stddev(ta),
        }
    return stats

# ── text report ───────────────────────────────────────────────────────────────

WORKLOAD_ORDER = [
    'cpu_bound', 'io_bound', 'mixed',
    'many_procs', 'short_jobs', 'long_jobs',
]
SCHED_ORDER = ['RR', 'LBS', 'MLFQ', 'CFS']

def write_summary(stats, out_path):
    schedulers = sorted({k[0] for k in stats}, key=lambda s: SCHED_ORDER.index(s) if s in SCHED_ORDER else 99)
    workloads  = sorted({k[1] for k in stats}, key=lambda w: WORKLOAD_ORDER.index(w) if w in WORKLOAD_ORDER else 99)

    lines = []
    lines.append("=" * 72)
    lines.append("  Enhanced-XV6 Scheduler Benchmark Results")
    lines.append("=" * 72)
    lines.append("")
    lines.append("  All values in xv6 timer ticks (1 tick ≈ 10ms at default QEMU rate).")
    lines.append("  Std-dev requires ≥2 samples; shown as 0.0 with <2 samples.")
    lines.append("")

    for wl in workloads:
        lines.append(f"── Workload: {wl} " + "─" * (50 - len(wl)))
        # Column header
        lines.append(
            f"  {'Scheduler':<8} {'N':>4}  "
            f"{'rtime_mean':>10} {'rtime_std':>9} {'rtime_med':>9} "
            f"{'wtime_mean':>10} {'wtime_std':>9} "
            f"{'turnaround':>10}"
        )
        lines.append("  " + "-" * 68)
        for sched in schedulers:
            key = (sched, wl)
            if key not in stats:
                lines.append(f"  {sched:<8} {'(no data)':>4}")
                continue
            s = stats[key]
            lines.append(
                f"  {sched:<8} {s['count']:>4}  "
                f"{fmt(s['rtime_mean']):>10} {fmt(s['rtime_std']):>9} "
                f"{fmt(s['rtime_median']):>9} "
                f"{fmt(s['wtime_mean']):>10} {fmt(s['wtime_std']):>9} "
                f"{fmt(s['turnaround_mean']):>10}"
            )
        lines.append("")

    # Per-scheduler summary (average across all workloads)
    lines.append("── Overall Average Turnaround (across all workloads) " + "─" * 19)
    lines.append(f"  {'Scheduler':<8} {'avg_turnaround':>14} {'avg_rtime':>10} {'avg_wtime':>10}")
    lines.append("  " + "-" * 44)
    for sched in schedulers:
        sched_keys = [(sched, wl) for wl in workloads if (sched, wl) in stats]
        if not sched_keys:
            continue
        all_ta   = [stats[k]['turnaround_mean'] for k in sched_keys]
        all_rt   = [stats[k]['rtime_mean']      for k in sched_keys]
        all_wt   = [stats[k]['wtime_mean']      for k in sched_keys]
        lines.append(
            f"  {sched:<8} {fmt(mean(all_ta)):>14} "
            f"{fmt(mean(all_rt)):>10} {fmt(mean(all_wt)):>10}"
        )
    lines.append("")
    lines.append("NOTE: Numbers are averages of averages (unweighted across workloads).")
    lines.append("      See comparison.csv for raw per-(scheduler,workload) data.")
    lines.append("")
    lines.append("METHODOLOGY: See benchmarks/METHODOLOGY.md")

    with open(out_path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f"[analyze] Summary written → {out_path}")

# ── CSV comparison table ──────────────────────────────────────────────────────

def write_comparison_csv(stats, out_path):
    workloads  = sorted({k[1] for k in stats}, key=lambda w: WORKLOAD_ORDER.index(w) if w in WORKLOAD_ORDER else 99)
    schedulers = sorted({k[0] for k in stats}, key=lambda s: SCHED_ORDER.index(s) if s in SCHED_ORDER else 99)

    fieldnames = ['workload', 'scheduler', 'count',
                  'rtime_mean', 'rtime_median', 'rtime_min', 'rtime_max', 'rtime_std',
                  'wtime_mean', 'wtime_std',
                  'turnaround_mean', 'turnaround_std']

    with open(out_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for wl in workloads:
            for sched in schedulers:
                key = (sched, wl)
                if key not in stats:
                    continue
                s = stats[key]
                writer.writerow({
                    'workload':        wl,
                    'scheduler':       sched,
                    'count':           s['count'],
                    'rtime_mean':      round(s['rtime_mean'],    2),
                    'rtime_median':    round(s['rtime_median'],  2),
                    'rtime_min':       s['rtime_min'],
                    'rtime_max':       s['rtime_max'],
                    'rtime_std':       round(s['rtime_std'],     2),
                    'wtime_mean':      round(s['wtime_mean'],    2),
                    'wtime_std':       round(s['wtime_std'],     2),
                    'turnaround_mean': round(s['turnaround_mean'], 2),
                    'turnaround_std':  round(s['turnaround_std'],  2),
                })
    print(f"[analyze] Comparison CSV → {out_path}")

# ── bar chart (optional, requires matplotlib) ─────────────────────────────────

def write_charts(stats, out_dir):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("[analyze] matplotlib not available — skipping charts")
        return

    workloads  = sorted({k[1] for k in stats}, key=lambda w: WORKLOAD_ORDER.index(w) if w in WORKLOAD_ORDER else 99)
    schedulers = sorted({k[0] for k in stats}, key=lambda s: SCHED_ORDER.index(s) if s in SCHED_ORDER else 99)

    # ── Chart 1: turnaround time per workload per scheduler
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    for ax_idx, (metric_key, metric_label) in enumerate([
        ('turnaround_mean', 'Mean Turnaround (ticks)'),
        ('wtime_mean',      'Mean Wait Time (ticks)'),
    ]):
        ax = axes[ax_idx]
        x = np.arange(len(workloads))
        width = 0.8 / len(schedulers)

        for i, sched in enumerate(schedulers):
            vals = []
            errs = []
            for wl in workloads:
                key = (sched, wl)
                if key in stats:
                    vals.append(stats[key][metric_key])
                    std_key = metric_key.replace('_mean', '_std')
                    errs.append(stats[key].get(std_key, 0))
                else:
                    vals.append(0)
                    errs.append(0)

            offset = (i - len(schedulers) / 2.0 + 0.5) * width
            bars = ax.bar(x + offset, vals, width * 0.9,
                          label=sched, yerr=errs, capsize=3)

        ax.set_xlabel('Workload', fontsize=11)
        ax.set_ylabel(metric_label, fontsize=11)
        ax.set_title(f'Scheduler Comparison — {metric_label}', fontsize=12)
        ax.set_xticks(x)
        ax.set_xticklabels(workloads, rotation=20, ha='right', fontsize=9)
        ax.legend(title='Scheduler', fontsize=9)
        ax.grid(axis='y', linestyle='--', alpha=0.5)

    plt.tight_layout()
    chart_path = os.path.join(out_dir, 'comparison.png')
    plt.savefig(chart_path, dpi=150)
    plt.close()
    print(f"[analyze] Chart saved → {chart_path}")

    # ── Chart 2: rtime mean per workload (separate plot)
    fig, ax = plt.subplots(figsize=(10, 5))
    x = np.arange(len(workloads))
    width = 0.8 / len(schedulers)

    for i, sched in enumerate(schedulers):
        vals = [stats.get((sched, wl), {}).get('rtime_mean', 0) for wl in workloads]
        errs = [stats.get((sched, wl), {}).get('rtime_std',  0) for wl in workloads]
        offset = (i - len(schedulers) / 2.0 + 0.5) * width
        ax.bar(x + offset, vals, width * 0.9, label=sched, yerr=errs, capsize=3)

    ax.set_xlabel('Workload', fontsize=11)
    ax.set_ylabel('Mean CPU Time (ticks)', fontsize=11)
    ax.set_title('CPU Time (rtime) per Scheduler per Workload', fontsize=12)
    ax.set_xticks(x)
    ax.set_xticklabels(workloads, rotation=20, ha='right', fontsize=9)
    ax.legend(title='Scheduler', fontsize=9)
    ax.grid(axis='y', linestyle='--', alpha=0.5)
    plt.tight_layout()
    rtime_chart = os.path.join(out_dir, 'rtime_comparison.png')
    plt.savefig(rtime_chart, dpi=150)
    plt.close()
    print(f"[analyze] rtime chart → {rtime_chart}")

# ── entry point ───────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <combined_csv> [output_dir]", file=sys.stderr)
        sys.exit(1)

    csv_path = sys.argv[1]
    out_dir  = sys.argv[2] if len(sys.argv) >= 3 else os.path.dirname(csv_path)

    if not os.path.isfile(csv_path):
        print(f"[analyze] ERROR: file not found: {csv_path}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(out_dir, exist_ok=True)

    print(f"[analyze] Loading {csv_path} ...")
    rows = load_csv(csv_path)
    print(f"[analyze] {len(rows)} data rows loaded")

    if not rows:
        print("[analyze] No data to analyse. Exiting.", file=sys.stderr)
        sys.exit(0)

    stats = aggregate(rows)

    write_summary(stats,         os.path.join(out_dir, 'summary.txt'))
    write_comparison_csv(stats,  os.path.join(out_dir, 'comparison.csv'))
    write_charts(stats,          out_dir)

    print("[analyze] Done.")

if __name__ == '__main__':
    main()
