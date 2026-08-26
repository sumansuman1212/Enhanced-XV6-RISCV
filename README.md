# Enhanced xv6 Operating System

## Overview

This project is an enhanced version of xv6, a re-implementation of Dennis Ritchie's and Ken Thompson's Unix Version 6 (v6), adapted for modern RISC-V multiprocessor systems. Our enhancements focus on improving system call functionality, scheduling policies, and memory management.

## Key Enhancements

### 1. System Call Counting (`getSysCount`)
- Added a new system call `getSysCount` to track the number of specific system call invocations
- Implemented `syscount` user program to monitor system call usage
- Features:
  - Counts system calls for a specific process and its children
  - Supports tracking a single system call via a bit mask
  - Prints detailed call statistics upon command completion

**Example Usage:**
```bash
$ syscount 32768 grep hello README.md
PID 6 called open 1 times.
```

### 2. Periodic CPU Time Alerting (`sigalarm` and `sigreturn`)
- Introduced two new system calls to provide user-level interrupt/fault handler-like functionality
- `sigalarm(interval, handler)`: Triggers a specified handler function after `n` CPU ticks
- `sigreturn()`: Resets process state to resume execution after handler completion

### 3. Advanced Scheduling Policies
Implemented two alternative scheduling algorithms that can be selected at compile-time:

#### A. Lottery-Based Scheduling (LBS)
- Preemptive scheduling with process priority via ticket allocation
- Features:
  - `settickets(int number)` system call to adjust process tickets
  - Default of 1 ticket per process
  - Ticket-based CPU time allocation with early arrival preference

#### B. Multi-Level Feedback Queue (MLFQ)
- Preemptive scheduler with dynamic priority adjustment
- Four priority queues (0-3) with varying time slices
- Prevents process starvation via priority boosting
- Adaptive queue placement based on process behavior

**Compilation Options:**
```bash
# Compile with Lottery-Based Scheduling
make clean; make qemu SCHEDULER=LBS

# Compile with Multi-Level Feedback Queue
make clean; make qemu SCHEDULER=MLFQ
```

### 4. Lazy Copy-on-Write (CoW) Fork
- Implemented an efficient memory-saving fork mechanism
- Shared memory pages marked read-only initially
- Page fault handling creates private copies only when write is attempted
- Reduces memory overhead during process creation

## Prerequisites
- RISC-V "newlib" toolchain
- QEMU compiled for riscv64-softmmu

## Building and Running
1. Install RISC-V toolchain and QEMU
2. Run `make qemu` to build and launch the system

## Detailed Implementation
For an in-depth explanation of the implementation details, feature motivations, and performance analysis, please refer to `report.md`.

## Acknowledgments
This project is based on the original xv6 developed by Frans Kaashoek, Robert Morris, and Russ Cox at MIT.


## Reporting Issues
For bug reports or suggestions, please contact the project maintainers.
