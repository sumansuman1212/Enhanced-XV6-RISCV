# PROJECT_AUDIT.md — Enhanced-XV6 Repository Audit

> Produced as **STEP 0** of the six-component extension plan.
> No code was modified during this audit.

---

## 1. Repository Architecture Overview

```
Enhanced-XV6-main/
├── kernel/          # Kernel source (C + assembly)
│   ├── proc.c/h     # Process management, scheduler
│   ├── vm.c         # Virtual memory, page tables
│   ├── kalloc.c     # Physical memory allocator + CoW ref-counting
│   ├── trap.c       # Trap/interrupt handling, MLFQ time-slice logic
│   ├── syscall.c/h  # Syscall dispatch table
│   ├── sysproc.c    # Process-related syscall implementations
│   ├── sysfile.c    # File-related syscall implementations
│   ├── riscv.h      # RISC-V CSR helpers, PTE flags (incl. PTE_COW)
│   ├── memlayout.h  # Physical memory layout, PHYSTOP, PA2REF macro
│   ├── param.h      # System-wide constants (NPROC=64, NCPU=8, etc.)
│   ├── defs.h       # Kernel function prototypes
│   ├── main.c       # Kernel entry point, hardware initialization
│   ├── virtio_disk.c# VirtIO block device driver
│   └── ...          # Other standard xv6 kernel files
├── user/            # User-space programs
│   ├── sh.c         # Existing xv6 shell
│   ├── schedulertest.c # Basic scheduler benchmark (rtime/wtime)
│   ├── alarmtest.c  # sigalarm/sigreturn test suite
│   ├── lazytest.c   # CoW fork test suite
│   ├── syscount.c   # getSysCount user program
│   └── ...          # Standard xv6 utilities
├── mkfs/mkfs.c      # Filesystem image builder
├── Makefile         # Build system with SCHEDULER= flag
├── plot_mlfq.py     # Python script for MLFQ visualization
├── data.txt/data2.txt # Sample MLFQ output data
├── *.png            # MLFQ timeline graphs
├── README.md        # Project readme
├── Report.md        # Detailed implementation report
└── README           # Original xv6 README
```

**Build system:** GNU Make with `SCHEDULER=RR|LBS|MLFQ` compile-time flag.
**Default:** `SCHEDULER=RR` (Round Robin).
**QEMU config:** `-machine virt`, 128 MB RAM, 3 CPUs (`CPUS=3`), VirtIO disk.
**Toolchain:** `riscv64-unknown-elf-gcc` or `riscv64-linux-gnu-gcc`.

---

## 2. Existing Features — Detailed Inventory

---

### SECTION A: INHERITED FUNCTIONALITY (standard xv6-RISC-V)

---

#### A1. xv6-RISC-V Base OS

| Item | Status |
|------|--------|
| RISC-V Sv39 virtual memory | Inherited |
| 3-level page table walk | Inherited |
| Kernel/user mode separation | Inherited |
| UART console I/O | Inherited |
| VirtIO disk driver | Inherited |
| Simple filesystem (xv6fs) | Inherited |
| Pipe IPC | Inherited |
| `fork`, `exec`, `wait`, `exit` | Inherited |
| 21 base system calls | Inherited |
| `waitx` (extended wait with timing) | **Modified** |
| Round-Robin scheduler (default) | Inherited baseline |
| Trap handling via `trampoline.S` | Inherited |
| Multi-CPU support (up to NCPU=8) | Inherited |

**Relevant files:** Nearly all of `kernel/`.

**How it works:** Standard xv6 RISC-V implementation. Processes are stored in a fixed-size table (`proc[NPROC]`, NPROC=64). The scheduler loops over the table, calling `swtch()` to context-switch between kernel scheduler and user processes. Physical memory is managed by a free-list allocator in `kalloc.c`.

**Limitations:**
- NPROC capped at 64
- NOFILE capped at 16 per process
- No dynamic library support
- No virtual memory beyond single contiguous heap

---

#### A2. `waitx` — Extended Wait with Timing

**Status:** Modified from original xv6 (added timing fields).

**Relevant files:** `kernel/proc.c` (`waitx`, `update_time`), `kernel/sysproc.c` (`sys_waitx`), `kernel/syscall.h` (`SYS_waitx = 22`).

**How it works:**
- Each process has `rtime` (run time), `ctime` (creation time), `etime` (exit time).
- `update_time()` is called every timer tick; increments `rtime` for RUNNING processes and `timespentincurrqueue` for RUNNABLE ones.
- `waitx(addr, wtime, rtime)` returns the child's runtime and computed wait time (`etime - ctime - rtime`).

**Existing tests:** `schedulertest.c` uses `waitx` to collect average rtime/wtime.

**Limitations:** `update_time` does not hold per-process lock when incrementing, creating a minor TOCTOU race (benign in practice for counters).

---

### SECTION B: NEW FUNCTIONALITY IMPLEMENTED BY THIS PROJECT

---

#### B1. `getSysCount` — Syscall Count Tracking

**Status:** NEW (not in vanilla xv6).

**Relevant files:**
- `kernel/proc.h`: `int syscall_counts[32]`, `int mask`
- `kernel/proc.c`: `syscallnames[]` array, initialization in `allocproc()`, aggregation in `exit()`
- `kernel/syscall.c`: `p->syscall_counts[num]++` in `syscall()`
- `kernel/sysproc.c`: `sys_getSysCount()`
- `kernel/syscall.h`: `SYS_getSysCount = 23`
- `user/syscount.c`: user program
- `user/user.h`: `int getSysCount(int)`
- `user/usys.pl`: `entry("getSysCount")`

**How it works:**
1. User calls `syscount <mask> <command> [args]`.
2. `getSysCount(mask)` sets `p->mask = mask` for the current process.
3. Every syscall increments `p->syscall_counts[num]` in the dispatch loop.
4. On `exit()`, the child's counts are aggregated into the parent's counts.
5. On exit, if `(p->mask >> i) & 1`, print count for syscall `i`.

**Existing tests:** Manual usage — `syscount 32768 grep hello README.md` (mask 32768 = 1<<15 = `SYS_open`).

**Limitations:**
- Lock on parent is commented out in exit(): `// acquire(&p->parent->lock)` — potential race when multiple children exit simultaneously.
- Only 32 syscall slots tracked; array size equals syscall numbering scheme.
- Mask tracks only one syscall at a time per the report.

---

#### B2. `sigalarm` / `sigreturn` — Periodic CPU Alarm

**Status:** NEW (not in vanilla xv6).

**Relevant files:**
- `kernel/proc.h`: `int interval`, `uint64 handler`, `int currticks`, `int staticinterval`, `struct trapframe *initial_trapframe`
- `kernel/sysproc.c`: `sys_sigalarm()`, `sys_sigreturn()`
- `kernel/trap.c`: Timer interrupt branch in `usertrap()`
- `kernel/syscall.h`: `SYS_sigalarm = 24`, `SYS_sigreturn = 25`
- `user/alarmtest.c`: Test suite (test0–test3)
- `user/user.h`: `int sigalarm(int, void(*)())`, `int sigreturn(void)`

**How it works:**
1. `sigalarm(n, fn)` sets `p->interval=n`, `p->handler=fn`.
2. On each timer tick, `p->currticks` is incremented.
3. When `currticks >= interval`: save the current trapframe into `p->initial_trapframe` (heap-allocated page), set `p->interval=0` (prevent re-entry), redirect `trapframe->epc` to handler.
4. `sigreturn()` restores `p->trapframe` from `initial_trapframe` and resets `currticks=0`, `interval=staticinterval`.

**Existing tests:** `alarmtest.c` — test0 (single invocation), test1 (multiple calls + register integrity), test2 (re-entrancy prevention), test3 (a0 register preservation through sigreturn).

**Limitations:**
- `initial_trapframe` is `kalloc()`'d but never `kfree()`'d — **memory leak** on every alarm invocation.
- Only one pending alarm handler at a time.
- No support for signal masking or multiple signal types.

---

#### B3. Lottery-Based Scheduling (LBS)

**Status:** NEW (not in vanilla xv6).

**Compile flag:** `make qemu SCHEDULER=LBS`

**Relevant files:**
- `kernel/proc.c`: `rand()` LCG, `scheduler()` `#ifdef LBS` block
- `kernel/proc.h`: `int tickets`
- `kernel/sysproc.c`: `sys_settickets()`
- `kernel/syscall.h`: `SYS_settickets = 26`
- `user/user.h`: `int settickets(int)`
- `user/usys.pl`: `entry("settickets")`

**How it works:**
1. Each process starts with 1 ticket (`allocproc()`). Fork inherits parent's ticket count.
2. `settickets(n)` allows a process to set its ticket count (must be > 0).
3. Scheduler sums all runnable tickets, draws `winner = rand() % total`.
4. Iterates the process table accumulating ticket counts; picks the process where the running sum exceeds `winner`.
5. Tie-breaking: if two RUNNABLE processes have equal tickets, the one with earlier `ctime` wins.

**Random number generator:** Linear Congruential Generator with fixed seed 42949672. Note from Report: seed does **not** use `ticks`, making it purely deterministic.

**Existing tests:** `schedulertest.c` reports average rtime/wtime.

**Limitations:**
- LCG seed is fixed at compile time — deterministic, not truly random.
- The lottery draw acquires/releases each process lock twice (one pass to sum tickets, one pass to find winner) — O(NPROC) each time, quadratic in worst case.
- Lock released between summing and selecting phases: race condition where total could change.
- No mechanism to prevent starvation of low-ticket processes under heavy load.

---

#### B4. Multi-Level Feedback Queue (MLFQ)

**Status:** NEW (not in vanilla xv6).

**Compile flag:** `make qemu SCHEDULER=MLFQ CPUS=1`

**Relevant files:**
- `kernel/proc.h`: `queuepriority`, `currentticks`, `timespentincurrqueue` (first impl.), `queuepriority2`, `currentticks2` (second impl.), `mlfqueue[4][NPROC]`, `queuesize[4]`, `enqueue()`, `dequeue()`
- `kernel/proc.c`: `enqueue()`, `dequeue()`, `scheduler()` `#ifdef MLFQ` block, `update_time()` for timespentincurrqueue
- `kernel/trap.c`: `usertrap()` + `kerneltrap()` MLFQ time-slice logic; `clockintr()` priority boost every 48 ticks

**How it works (second/active implementation):**
1. Four queues (0=highest, 3=lowest) with time slices 1, 4, 8, 16 ticks.
2. New processes enter Queue 0 (`enqueue(p, 0)`).
3. Each timer tick, `currentticks2` increments. When it reaches the queue's limit, process demotes to the next queue and yields.
4. If a higher-priority queue has runnable processes, the current process yields immediately.
5. Every 48 ticks (`clockintr`), all non-UNUSED processes are reset to Queue 0 (anti-starvation boost).
6. Sleeping processes are dequeued; killed processes are dequeued.
7. `scheduler()` picks from Queue 0 first, falling back to lower queues.

**Existing tests:** `schedulertest.c`; MLFQ timeline data collected via `update_time()` printf and visualized with `plot_mlfq.py`.

**Known performance data (from Report.md):**

| Scheduler | Avg rtime | Avg wtime |
|-----------|-----------|-----------|
| RR        | 13        | 152       |
| LBS       | 13        | 148       |
| MLFQ      | 13        | 152       |

**Limitations:**
- `enqueue()` scans the entire queue to prevent duplicates — O(n) per enqueue.
- `mlfqueue[4][NPROC]` is a static 2D array; no true priority queue data structure.
- The switch-case for time slices has **fall-through** bug: `case 0` and `case 1` lack `break` before the `if` check, meaning a process in queue 0 that hasn't hit 1 tick will fall through to check queue 1's limit (4 ticks), etc. This means queue 0 processes can run up to 4 ticks before yielding in some code paths.
- Priority boost resets even SLEEPING processes (calls dequeue/enqueue on UNUSED processes conditionally).
- The `clockintr()` boost resets the currently RUNNING process's state to RUNNABLE while holding tickslock — this is racy on multiprocessor systems.
- Not thoroughly tested with CPUS>1.

---

#### B5. Lazy Copy-on-Write (CoW) Fork

**Status:** NEW (not in vanilla xv6).

**Relevant files:**
- `kernel/riscv.h`: `PTE_COW (1L << 8)` — uses bit 8 of PTE flags
- `kernel/kalloc.c`: `cow_ref` struct with ref-count array, `cow_init()`, `cow_incref()`, `cow_decref()`, `cow_handler()`, modified `kfree()` and `kalloc()`
- `kernel/vm.c`: `uvmcopy()` — sets PTE_COW flag, removes PTE_W; `copyout()` — calls `cow_handler()` before writing
- `kernel/trap.c`: `usertrap()` — handles scause=15 (store page fault) via `cow_handler()`
- `kernel/main.c`: `cow_init()` called before `kinit()`
- `kernel/memlayout.h`: `PA2REF(pa)` and `MAXREF` macros
- `user/lazytest.c`: CoW test suite

**How it works:**
1. `cow_init()` initializes a global ref-count array for all physical pages.
2. On `fork()`, `uvmcopy()` marks all child pages as CoW (sets `PTE_COW`, clears `PTE_W`), increments ref-count for each shared physical page via `cow_incref()`.
3. `kalloc()` sets `cow_ref.ref[index] = 1` for new pages.
4. `kfree()` decrements ref-count; only frees physical page when count reaches 0.
5. On a store fault (scause=15), `cow_handler()` is called with the faulting VA.
6. `cow_handler()` validates the PTE, allocates a new physical page, copies content, remaps with `PTE_W` and without `PTE_COW`.
7. `copyout()` also triggers `cow_handler()` for kernel-to-user copies.

**Note:** The implementation uses `kalloc()` (not `cow_decref()`) in `cow_handler` — a slight implementation choice that always allocates a new page even when ref-count = 1. The `cow_decref()` function exists but is commented out in `cow_handler`.

**Existing tests:** `lazytest.c` — `simpletest()`, `threetest()`, `filetest()` — all pass ("ALL COW TESTS PASSED").

**Limitations:**
- `PTE_COW` uses bit 8, which is the "Global" PTE bit in the RISC-V spec. This reuse is non-standard; the standard software-available bits are 8 and 9 (`RSW`), so using bit 8 is actually correct for RSW[0]. However, bit 8 in xv6's riscv.h is labeled `PTE_COW` while the standard calls it `RSW`. This is technically correct usage of the reserved software bit.
- `cow_handler()` always allocates a new page via `kalloc()` even if the page is the last reference (ref=1). `cow_decref()` was designed for the "reuse if last ref" optimization but is not used.
- `cow_init()` initializes all ref-counts to 1, which is called before `kinit()`. This means when `kinit()`→`freerange()`→`kfree()` runs, `kfree()` decrements ref from 1 to 0 and frees the pages normally — correct but subtle.
- The `cow_ref` lock is a spinlock used inside `cow_handler()` after `walk()` but before `mappages()` — there is a window where the lock is released and another CPU could observe a stale state.

---

## 3. Existing Shell Capabilities

**File:** `user/sh.c`

The existing shell is the **standard xv6 shell** with these capabilities:

### Supported
| Feature | Support |
|---------|---------|
| Simple command execution | ✓ |
| Arguments | ✓ (`MAXARGS = 10`) |
| Input redirection `< file` | ✓ |
| Output redirection `> file` | ✓ |
| Append redirection `>> file` | ✓ |
| Pipe `\|` | ✓ (recursive via `parsepipe`) |
| Sequential commands `;` | ✓ (via `listcmd`) |
| Background `&` | ✓ (via `backcmd`, does NOT wait) |
| `cd` built-in | ✓ (handled specially in `main()`, not forked) |
| Grouped commands `( )` | ✓ (via `parseblock`) |

### NOT Supported
| Feature | Status |
|---------|--------|
| `pwd` built-in | ✗ |
| `exit` built-in | ✗ (only by EOF/Ctrl-D) |
| `history` | ✗ |
| `jobs` | ✗ |
| `fg` / `bg` job control | ✗ |
| Job table | ✗ |
| Command-line editing (arrow keys, etc.) | ✗ |
| Tab completion | ✗ |
| Environment variables | ✗ |
| Quotes (`"`, `'`) | ✗ |
| Glob expansion (`*`, `?`) | ✗ |
| Multiple pipes with correct cleanup | Partial (existing pipe leaks FDs if child exits early) |

**Architecture:** Monolithic recursive descent parser producing a tree of `cmd` structs (`execcmd`, `redircmd`, `pipecmd`, `listcmd`, `backcmd`). `runcmd()` evaluates the tree recursively. All commands execute in a child process except `cd`.

**Buffer size:** Shell command buffer is only **100 bytes** (`static char buf[100]`).

---

## 4. Existing Syscall Table

| Number | Name | Status |
|--------|------|--------|
| 1 | fork | Inherited |
| 2 | exit | Inherited |
| 3 | wait | Inherited |
| 4 | pipe | Inherited |
| 5 | read | Inherited |
| 6 | kill | Inherited |
| 7 | exec | Inherited |
| 8 | fstat | Inherited |
| 9 | chdir | Inherited |
| 10 | dup | Inherited |
| 11 | getpid | Inherited |
| 12 | sbrk | Inherited |
| 13 | sleep | Inherited |
| 14 | uptime | Inherited |
| 15 | open | Inherited |
| 16 | write | Inherited |
| 17 | mknod | Inherited |
| 18 | unlink | Inherited |
| 19 | link | Inherited |
| 20 | mkdir | Inherited |
| 21 | close | Inherited |
| 22 | waitx | Modified |
| 23 | getSysCount | NEW |
| 24 | sigalarm | NEW |
| 25 | sigreturn | NEW |
| 26 | settickets | NEW |

**Next available syscall number: 27**

---

## 5. Process Structure Fields

The `struct proc` in `proc.h` currently contains:

```c
// Standard xv6 fields
struct spinlock lock;
enum procstate state;
void *chan;
int killed;
int xstate;
int pid;
struct proc *parent;
uint64 kstack;
uint64 sz;
pagetable_t pagetable;
struct trapframe *trapframe;
struct context context;
struct file *ofile[NOFILE];
struct inode *cwd;
char name[16];

// Extended timing fields (waitx)
uint rtime;     // run time
uint ctime;     // creation time
uint etime;     // exit time

// getSysCount fields
int syscall_counts[32];
int mask;

// sigalarm fields
int interval;
uint64 handler;
int currticks;
int staticinterval;
struct trapframe *initial_trapframe;

// LBS fields
int tickets;

// MLFQ first-impl fields
int queuepriority;
int currentticks;
int timespentincurrqueue;

// MLFQ second-impl fields
int queuepriority2;
int currentticks2;
```

**Observation:** Both MLFQ implementations have fields in the struct simultaneously, though only one is active at compile time. This wastes memory but doesn't cause bugs.

---

## 6. Existing Benchmark Infrastructure

| Component | Exists? | Notes |
|-----------|---------|-------|
| `schedulertest.c` | ✓ | Forks 10 processes (5 IO-bound, 5 CPU-bound), reports avg rtime/wtime |
| `plot_mlfq.py` | ✓ | Python matplotlib graph for MLFQ queue assignment over time |
| `data.txt` / `data2.txt` | ✓ | Captured MLFQ timeline output |
| CSV output | ✗ | No machine-readable output format |
| Multiple scheduler comparison | ✗ | Must re-run manually with different SCHEDULER= flags |
| Statistical analysis (mean/stddev) | ✗ | Only single-run average |
| Dedicated `benchmarks/` directory | ✗ | None exists |

---

## 7. Memory Architecture Summary

| Component | Implementation |
|-----------|---------------|
| Physical allocator | Free-list (`kmem.freelist`) in `kalloc.c` |
| Page size | 4096 bytes |
| Physical RAM | 128 MB (`PHYSTOP = KERNBASE + 128*1024*1024`) |
| Address space | Sv39 (39-bit VA), 3-level page table |
| CoW ref-counting | `cow_ref.ref[MAXREF]` static array, indexed by `PA2REF(pa) = pa/PGSIZE` |
| Lazy allocation | NOT implemented (sbrk allocates eagerly via `uvmalloc`) |
| Swap / page replacement | NOT implemented |
| Memory instrumentation | NOT implemented |
| `memstat` syscall | NOT implemented |

---

## 8. Files That Will Need Modification

The following files will need modification for the six planned additions:

| File | Reason |
|------|--------|
| `kernel/proc.h` | Add CFS fields (vruntime, weight, etc.), procstat fields |
| `kernel/proc.c` | Add CFS scheduler block, procstat syscall impl, stats tracking |
| `kernel/kalloc.c` | Add memory instrumentation counters |
| `kernel/trap.c` | Add CFS yield logic |
| `kernel/syscall.h` | Add new syscall numbers |
| `kernel/syscall.c` | Register new syscalls |
| `kernel/sysproc.c` | Implement new syscall handlers |
| `kernel/defs.h` | Add new function prototypes |
| `kernel/vm.c` | Add page replacement hooks (if implemented) |
| `user/user.h` | Add new syscall prototypes |
| `user/usys.pl` | Add new syscall stubs |
| `Makefile` | Add new user programs, CFS scheduler flag |

**New files to create:**
- `user/nsh.c` — New enhanced shell
- `kernel/cfs.c` or inline in `proc.c` — CFS scheduler
- `benchmarks/` directory — benchmark framework
- `user/procstat.c` — procstat utility
- `user/memstat.c` — memstat utility
- Various test files

---

## 9. Potential Conflicts and Risks

### 9.1 Scheduler Selection Mechanism

Currently uses `#ifdef` compile-time flags. Adding CFS requires either:
- Adding `#ifdef CFS` blocks alongside `#ifdef LBS` and `#ifdef MLFQ`
- Or a runtime selection mechanism (more complex, risks instability)

**Recommendation:** Continue with compile-time flags (`SCHEDULER=CFS`).

### 9.2 `struct proc` Growth

The struct already has redundant fields (two MLFQ implementations). Adding CFS fields, memory instrumentation fields, and procstat fields will make it large. Need to audit carefully.

**Recommendation:** Remove unused `queuepriority/currentticks/timespentincurrqueue` (first MLFQ impl fields) since the second implementation is the active one. This frees 3 × 4 = 12 bytes.

### 9.3 `PTE_COW` Bit Conflict

`PTE_COW` uses bit 8 (RSW[0]). Any page replacement implementation that needs to store metadata in PTE flags must avoid bit 8 and use bit 9 (RSW[1]) instead.

### 9.4 Memory Leak in sigalarm

`p->initial_trapframe` is `kalloc()`'d on every alarm but never freed. This must be fixed before implementing memory instrumentation to get accurate allocation counts.

### 9.5 MLFQ Multiprocessor Safety

The MLFQ `clockintr()` boost resets the current process's state to RUNNABLE without proper lock ordering. This is a latent race on CPUS>1. MLFQ is documented to require `CPUS=1`.

### 9.6 LBS Lock-Release Race

The LBS scheduler releases the lock between computing `totalticket` and selecting the winner. A process could change state between the two passes. This is an existing bug that could cause the scheduler to run a non-RUNNABLE process (guarded by the second `if(runnow->state==RUNNABLE)` check).

### 9.7 Shell Buffer Size

The existing shell uses only 100-byte command buffer. The enhanced shell should use a larger buffer (at least 512 bytes).

### 9.8 `cow_handler` Always Allocates

`cow_handler()` always calls `kalloc()` even when the page ref-count is 1 (i.e., no actual sharing). This is wasteful but not incorrect.

---

## 10. Detailed Implementation Plan for Six Additions

---

### ADDITION 1: Custom Unix Shell (`nsh`)

**Strategy:** Create a new shell `user/nsh.c` alongside the existing `user/sh.c`. Do not modify `sh.c`.

**New capabilities over existing shell:**
- `pwd` built-in
- `exit [code]` built-in
- `history` built-in (in-memory ring buffer, N=50 entries)
- `jobs` built-in
- `fg <job>` — bring background job to foreground (limited by xv6 lack of process groups)
- `bg <job>` — mark stopped job as running in background
- Larger command buffer (512 bytes)
- Prompt with `cwd` display: `nsh:/dir$ `
- Improved error messages

**Architecture:**
```
nsh.c
├── parser.c/h equivalent (inline) — parse input into cmd tree
├── executor   — runcmd() equivalent
├── builtins   — cd, pwd, exit, history, jobs, fg, bg
├── jobmgr     — job table, background process tracking
└── readline   — improved input with buffer
```

**xv6 limitation for job control:** xv6 has no process groups, signals (SIGSTOP/SIGCONT), or terminal control (`tcsetpgrp`). `fg`/`bg` will be implemented on a best-effort basis using a job table with PID tracking. `fg` will call `wait()` for the specific job. True POSIX job control is not possible; this will be documented.

**Syscalls needed:** No new syscalls required (uses existing `fork`, `exec`, `wait`, `pipe`, `open`, `close`, `chdir`, `write`, `read`).

---

### ADDITION 2: CFS-Inspired Scheduler

**Strategy:** Add `#ifdef CFS` blocks in `proc.c` and `trap.c` following the existing pattern.

**New fields in `struct proc`:**
```c
uint64 vruntime;   // virtual runtime (ns-equivalent in ticks)
int    weight;     // scheduling weight (default 1024)
uint64 exec_start; // tick when this process started running
```

**Algorithm:**
- Maintain all runnable processes conceptually sorted by `vruntime`.
- At each scheduling decision, select the process with minimum `vruntime`.
- When a process runs for Δt ticks: `vruntime += Δt * (1024 / weight)`.
- Default weight = 1024 → 1:1 CPU time.
- `setweight(n)` syscall to set weight (new syscall #27).
- On new process creation: set `vruntime = min(vruntime of all runnable) + epsilon` to prevent unfair head-of-line blocking.

**Starvation prevention:** If a process's `vruntime` is > 48 ticks behind the minimum, it gets a vruntime reset to `min_vruntime + 1` on the next boost tick.

**Compile flag:** `make qemu SCHEDULER=CFS`

**New syscall:** `SYS_setweight = 27`

---

### ADDITION 3: Scheduler Benchmark Framework

**Strategy:** Create `benchmarks/` directory with workload programs and a driver.

**Workloads (user programs):**
- `benchmarks/cpu_bound.c` — pure CPU loop
- `benchmarks/io_bound.c` — repeated sleep
- `benchmarks/mixed.c` — alternating CPU and sleep
- `benchmarks/many_procs.c` — fork N processes
- `benchmarks/short_jobs.c` — many short-lived forks
- `benchmarks/long_jobs.c` — few long-running processes

**Framework driver:** `user/runbench.c` — runs each workload, collects CSV output via `waitx`.

**Output format:**
```
scheduler,workload,pid,rtime,wtime,ctime,etime
```

**External tools:** Shell scripts in `benchmarks/` to:
- Run QEMU with each scheduler
- Capture output
- Compute mean/median/stddev in Python

---

### ADDITION 4: Memory-Management Instrumentation

**Strategy:** Add counters to `kalloc.c` and `proc` struct. Add `memstat` syscall.

**System-wide counters (in `kalloc.c`):**
```c
struct memstats {
    uint64 total_allocs;
    uint64 total_frees;
    uint64 active_pages;
    uint64 total_faults;
    uint64 cow_faults;
    uint64 peak_pages;
};
```

**Per-process counters (new `struct proc` fields):**
```c
uint64 p_pages;        // current pages mapped
uint64 p_faults;       // total page faults
uint64 p_cow_faults;   // CoW faults specifically
uint64 p_allocs;       // pages allocated
uint64 p_frees;        // pages freed
```

**New syscalls:**
- `SYS_memstat = 28` → fills a user struct with system-wide stats
- `SYS_getprocmem = 29` → fills a user struct with per-process memory stats

**User programs:**
- `user/memstat.c` — displays system-wide memory stats
- Stats incorporated into `procstat`

---

### ADDITION 5: Page Replacement

**Assessment of feasibility:** xv6 has a simple free-list allocator. There is no swap partition. VirtIO disk is used for the filesystem. Implementing full disk-backed swapping would require:
1. A swap partition/file on VirtIO disk
2. Modified `uvmalloc`/`uvmdealloc` to evict to swap
3. Modified page fault handler to page-in from swap
4. PTE "swapped-out" bit tracking

This is feasible but high-risk. Plan:

**Phase A (always):** Implement Clock/Second-Chance page frame tracker — per-page metadata with reference bits. This provides the eviction policy infrastructure without actual disk swapping.

**Phase B (if time permits):** Implement actual swap using a reserved section of the VirtIO disk image.

**Per-page metadata:**
```c
struct pageinfo {
    uint8 ref_bit;     // recently accessed
    uint8 dirty_bit;   // written since load
    uint8 valid;       // in use
    uint pid;          // owner process
};
```

**Clock hand:** Global integer pointer cycling through physical pages.

**Eviction trigger:** When `kalloc()` finds the free list empty, invoke the clock algorithm to find a victim page.

---

### ADDITION 6: Process Monitoring (`procstat`)

**Strategy:** Add a `getprocstat` syscall that fills a user-visible struct. Create `user/procstat.c`.

**New struct (user-visible):**
```c
struct pstat {
    int pid;
    int ppid;
    char name[16];
    int state;           // UNUSED/RUNNABLE/RUNNING/SLEEPING/ZOMBIE
    uint rtime;          // run time (ticks)
    uint ctime;          // creation time
    uint64 sz;           // address space size
    int tickets;         // LBS tickets
    int queuepriority;   // MLFQ queue
    uint64 vruntime;     // CFS vruntime
    uint64 p_pages;
    uint64 p_faults;
    uint64 p_cow_faults;
    int ctx_switches;    // context switch count (new field)
};
```

**New syscall:** `SYS_getprocstat = 30` → copies a snapshot of all process stats to user memory.

**User program:** `user/procstat.c` — calls `getprocstat()`, displays formatted table.

---

## 11. Summary Table: Feature Provenance

| Feature | In Original xv6 | In Existing Enhanced-XV6 | To Be Added | Evidence |
|---------|----------------|--------------------------|-------------|---------|
| Round-Robin scheduler | ✓ | ✓ | — | `proc.c` `#ifdef RR` |
| LBS scheduler | ✗ | ✓ | — | `proc.c` `#ifdef LBS`, Report.md |
| MLFQ scheduler | ✗ | ✓ | — | `proc.c` `#ifdef MLFQ`, `trap.c` |
| CoW fork | ✗ | ✓ | — | `kalloc.c`, `vm.c`, `lazytest.c` |
| `getSysCount` | ✗ | ✓ | — | `sysproc.c`, `syscount.c` |
| `sigalarm`/`sigreturn` | ✗ | ✓ | — | `sysproc.c`, `trap.c`, `alarmtest.c` |
| `settickets` | ✗ | ✓ | — | `sysproc.c` |
| `waitx` (timing) | ✗ | ✓ | — | `proc.c`, `sysproc.c` |
| Basic shell | ✓ | ✓ | — | `user/sh.c` |
| Enhanced shell (`nsh`) | ✗ | ✗ | ✓ | To create `user/nsh.c` |
| CFS-inspired scheduler | ✗ | ✗ | ✓ | To modify `proc.c`, `trap.c` |
| Benchmark framework | Partial | Partial | ✓ | To create `benchmarks/` |
| Memory instrumentation | ✗ | ✗ | ✓ | To modify `kalloc.c`, `proc.h` |
| `memstat` syscall | ✗ | ✗ | ✓ | New syscall |
| Page replacement | ✗ | ✗ | ✓ | Clock algorithm in `kalloc.c` |
| `procstat` utility | ✗ | ✗ | ✓ | New syscall + user program |

---

## 12. Known Bugs and Issues (Pre-Existing)

1. **sigalarm memory leak:** `p->initial_trapframe` is `kalloc()`'d but never `kfree()`'d. Fix before memory instrumentation.
2. **getSysCount lock race:** `p->parent->lock` not held during syscall count aggregation in `exit()`.
3. **LBS double-scan race:** Ticket total computed without locks held between scan passes.
4. **MLFQ fall-through in switch:** Cases 0–2 lack `break` before the next case's `if` condition, allowing time-slice checks to bleed across queue levels.
5. **MLFQ multiprocessor unsafety:** `clockintr()` boost changes another CPU's running process state without proper lock.
6. **Shell 100-byte buffer:** Very short commands, no argument to increase it.
7. **`cow_handler` no reuse:** Always allocates a new page even for last-reference pages.

---

*End of PROJECT_AUDIT.md*
