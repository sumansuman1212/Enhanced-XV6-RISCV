#!/usr/bin/env bash
# benchmarks/run_all.sh — Run scheduler benchmarks for all three schedulers
#
# This script:
#   1. Builds xv6 for each scheduler (RR, LBS, MLFQ, CFS)
#   2. Starts QEMU in non-interactive mode
#   3. Feeds the runbench command to the xv6 shell via stdin
#   4. Captures the CSV output to results/<scheduler>.csv
#   5. Calls analyze.py when all runs are done
#
# Prerequisites:
#   - RISC-V toolchain (riscv64-linux-gnu-gcc or riscv64-unknown-elf-gcc)
#   - qemu-system-riscv64
#   - Python 3 with pandas and scipy (for analysis)
#   - Run from the repository root: bash benchmarks/run_all.sh
#
# Configuration:
REPS=${REPS:-3}           # repetitions per workload per scheduler
CPUS=${CPUS:-1}           # number of QEMU CPUs (1 recommended for reproducibility)
TIMEOUT=${TIMEOUT:-300}   # seconds before killing a QEMU instance
RESULTS_DIR="benchmarks/results"

# ── helpers ───────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

mkdir -p "$RESULTS_DIR"

log() { echo "[run_all] $*" >&2; }

die() { echo "[run_all] ERROR: $*" >&2; exit 1; }

# Check required tools
command -v qemu-system-riscv64 >/dev/null 2>&1 || \
  die "qemu-system-riscv64 not found. Install QEMU with RISC-V support."

# ── build + run one scheduler ─────────────────────────────────────────────────
run_scheduler() {
  local SCHED="$1"
  local OUT="$RESULTS_DIR/${SCHED}.csv"

  log "=== Scheduler: $SCHED ==="

  # Build
  log "Building with SCHEDULER=$SCHED CPUS=$CPUS ..."
  make clean -s
  make -s SCHEDULER="$SCHED" CPUS="$CPUS" 2>&1 | tail -5 || die "Build failed for $SCHED"
  log "Build OK"

  # The command we want to run inside xv6
  local BENCH_CMD="runbench $SCHED $REPS"

  # Run QEMU with stdin piped so xv6 boots and runs the command.
  # We wait for the "# benchmark complete" marker then kill QEMU.
  log "Starting QEMU (timeout ${TIMEOUT}s) ..."

  # Write xv6 input: wait for boot prompt, then run command
  # The boot sequence takes a few seconds; we use a sleep + command approach
  {
    sleep 8                  # wait for xv6 to boot fully
    echo "$BENCH_CMD"        # send the benchmark command
    sleep "$TIMEOUT"         # keep stdin open until timeout kills it
  } | timeout "$((TIMEOUT + 15))" \
      qemu-system-riscv64 \
        -machine virt -bios none \
        -kernel kernel/kernel \
        -m 128M -smp "$CPUS" \
        -nographic \
        -global virtio-mmio.force-legacy=false \
        -drive file=fs.img,if=none,format=raw,id=x0 \
        -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
        2>/dev/null \
    | grep -E '^(scheduler|[A-Z].*,|# benchmark)' \
    | head -500 \
    > "$OUT" || true

  # Verify output was produced
  if [[ -s "$OUT" ]]; then
    local nrows
    nrows=$(grep -c '^[A-Z]' "$OUT" 2>/dev/null || echo 0)
    log "Captured $nrows data rows → $OUT"
  else
    log "WARNING: No output captured for $SCHED. Output file is empty."
    log "  Make sure xv6 boots and 'runbench' is in the filesystem."
    # Write a placeholder so analyze.py doesn't fail
    echo "scheduler,workload,run,pid,rtime,wtime,turnaround" > "$OUT"
    echo "# no data captured - check QEMU output manually" >> "$OUT"
  fi
}

# ── main ──────────────────────────────────────────────────────────────────────
log "Benchmark run starting"
log "  Repetitions per workload: $REPS"
log "  QEMU CPUs: $CPUS"
log "  Results directory: $RESULTS_DIR"
echo ""

for SCHED in RR LBS MLFQ CFS; do
  run_scheduler "$SCHED"
  echo ""
done

# Merge all CSV files into one combined file
COMBINED="$RESULTS_DIR/combined.csv"
log "Merging results → $COMBINED"
{
  # Print header once
  echo "scheduler,workload,run,pid,rtime,wtime,turnaround"
  # Append data rows (skip headers) from each file
  for SCHED in RR LBS MLFQ CFS; do
    local_file="$RESULTS_DIR/${SCHED}.csv"
    if [[ -f "$local_file" ]]; then
      grep -v '^scheduler' "$local_file" | grep -v '^#' || true
    fi
  done
} > "$COMBINED"

log "Combined results: $COMBINED"
log ""
log "Running statistical analysis..."
python3 benchmarks/analyze.py "$COMBINED" "$RESULTS_DIR" || \
  log "WARNING: analysis.py failed (check Python dependencies)"

log ""
log "=== All benchmarks complete ==="
log "Results in: $RESULTS_DIR/"
