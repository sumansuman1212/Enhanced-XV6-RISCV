// memstat_test.c — Tests for memory-management instrumentation
//
// Tests:
//   T01  memstat syscall works (returns 0, fills struct)
//   T02  active_pages is non-zero at start
//   T03  total_allocs increases after sbrk (forces allocations)
//   T04  total_frees increases after sbrk shrink
//   T05  page faults increase after fork+write (CoW trigger)
//   T06  CoW faults increment after fork+write
//   T07  peak_pages >= active_pages always
//   T08  active_pages + free_pages ≈ total physical pages
//   T09  multiple forks accumulate cow_faults
//   T10  stats after process exit: active_pages decreases

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user/memstat.h"

#define PGSIZE 4096

static int passed = 0;
static int failed = 0;

static void
test_pass(const char *name) {
  printf("  [PASS] %s\n", name);
  passed++;
}

static void
test_fail(const char *name, const char *reason) {
  printf("  [FAIL] %s: %s\n", name, reason);
  failed++;
}

// Helper: snapshot stats
static int
snap(struct memstats *s) {
  return memstat(s);
}

// ── T01: syscall returns 0 and fills struct ───────────────────────────────────
static void test_syscall_basic(void) {
  struct memstats s;
  int r = snap(&s);
  if (r == 0)
    test_pass("T01-memstat-syscall-ok");
  else
    test_fail("T01-memstat-syscall-ok", "memstat() returned non-zero");
}

// ── T02: active_pages > 0 ─────────────────────────────────────────────────────
static void test_active_nonzero(void) {
  struct memstats s;
  snap(&s);
  if (s.active_pages > 0)
    test_pass("T02-active-pages-nonzero");
  else
    test_fail("T02-active-pages-nonzero", "active_pages=0 at boot");
}

// ── T03: total_allocs increases after sbrk ───────────────────────────────────
static void test_allocs_increase(void) {
  struct memstats before, after;
  snap(&before);

  // Allocate 8 pages via sbrk
  char *p = sbrk(8 * PGSIZE);
  if (p == (char *)-1) {
    test_fail("T03-allocs-increase", "sbrk failed");
    return;
  }
  // Touch pages to force physical allocation
  for (int i = 0; i < 8; i++)
    p[i * PGSIZE] = 1;

  snap(&after);
  sbrk(-(8 * PGSIZE));   // release

  if (after.total_allocs > before.total_allocs)
    test_pass("T03-allocs-increase");
  else
    test_fail("T03-allocs-increase", "total_allocs did not increase");
}

// ── T04: total_frees increases after sbrk shrink ─────────────────────────────
static void test_frees_increase(void) {
  // Allocate then free some pages
  char *p = sbrk(4 * PGSIZE);
  if (p == (char *)-1) {
    test_fail("T04-frees-increase", "sbrk alloc failed");
    return;
  }
  for (int i = 0; i < 4; i++) p[i * PGSIZE] = 2;

  struct memstats before, after;
  snap(&before);
  sbrk(-(4 * PGSIZE));
  snap(&after);

  if (after.total_frees > before.total_frees)
    test_pass("T04-frees-increase");
  else
    test_fail("T04-frees-increase", "total_frees did not increase after sbrk shrink");
}

// ── T05: page faults increase after fork + write ─────────────────────────────
static void test_faults_increase(void) {
  struct memstats before, after;
  snap(&before);

  // Allocate memory, fork, then write in child (triggers CoW fault)
  char *p = sbrk(PGSIZE);
  if (p == (char *)-1) {
    test_fail("T05-faults-increase", "sbrk failed");
    return;
  }
  *p = 0;  // touch before fork so the page exists

  int pid = fork();
  if (pid == 0) {
    // Child: write to CoW page — triggers page fault
    *p = 99;
    exit(0);
  }
  wait(0);
  snap(&after);
  sbrk(-PGSIZE);

  if (after.total_faults > before.total_faults)
    test_pass("T05-faults-increase");
  else
    test_fail("T05-faults-increase", "total_faults did not increase after CoW write");
}

// ── T06: CoW faults increment ────────────────────────────────────────────────
static void test_cow_faults(void) {
  struct memstats before, after;

  char *p = sbrk(PGSIZE);
  if (p == (char *)-1) {
    test_fail("T06-cow-faults", "sbrk failed");
    return;
  }
  *p = 0;

  snap(&before);
  int pid = fork();
  if (pid == 0) {
    *p = 55;   // CoW write
    exit(0);
  }
  wait(0);
  snap(&after);
  sbrk(-PGSIZE);

  if (after.cow_faults > before.cow_faults)
    test_pass("T06-cow-faults-increment");
  else
    test_fail("T06-cow-faults-increment", "cow_faults did not increase");
}

// ── T07: peak_pages >= active_pages always ───────────────────────────────────
static void test_peak_ge_active(void) {
  struct memstats s;
  snap(&s);
  if (s.peak_pages >= s.active_pages)
    test_pass("T07-peak-ge-active");
  else
    test_fail("T07-peak-ge-active", "peak_pages < active_pages (invariant violated)");
}

// ── T08: active + free ≈ total physical pages ────────────────────────────────
// Physical memory = 128MB = 128*1024*1024 / 4096 = 32768 pages
// Kernel uses some, so actual usable is less.
// Just check that active + free is in a plausible range (> 1000).
static void test_memory_accounting(void) {
  struct memstats s;
  snap(&s);
  uint64 total = s.active_pages + s.free_pages;
  if (total > 1000)
    test_pass("T08-memory-accounting-plausible");
  else
    test_fail("T08-memory-accounting-plausible", "active+free too small (accounting bug?)");
}

// ── T09: multiple forks accumulate CoW faults ────────────────────────────────
static void test_multiple_cow_forks(void) {
  struct memstats before, after;

  // Allocate a few pages and touch them
  char *p = sbrk(4 * PGSIZE);
  if (p == (char *)-1) { test_fail("T09-multi-cow", "sbrk failed"); return; }
  for (int i = 0; i < 4; i++) p[i * PGSIZE] = (char)i;

  snap(&before);

  // Fork 4 times, each child writes to all pages
  for (int i = 0; i < 4; i++) {
    int pid = fork();
    if (pid == 0) {
      for (int j = 0; j < 4; j++) p[j * PGSIZE] = (char)(i + j);
      exit(0);
    }
    wait(0);
  }

  snap(&after);
  sbrk(-(4 * PGSIZE));

  if (after.cow_faults > before.cow_faults)
    test_pass("T09-multiple-cow-forks");
  else
    test_fail("T09-multiple-cow-forks", "cow_faults did not accumulate across forks");
}

// ── T10: active_pages decreases after child exits ────────────────────────────
static void test_pages_freed_on_exit(void) {
  struct memstats before, after;

  snap(&before);

  int pid = fork();
  if (pid == 0) {
    // Allocate several pages in child only
    char *p = sbrk(16 * PGSIZE);
    if (p != (char *)-1)
      for (int i = 0; i < 16; i++) p[i * PGSIZE] = 1;
    exit(0);
  }
  wait(0);

  snap(&after);

  // After child exits, active_pages should be <= before (or same if we
  // didn't allocate extra in parent). Either way it must not exceed
  // before + some threshold (we allow a small delta for kernel bookkeeping).
  if (after.active_pages <= before.active_pages + 4)
    test_pass("T10-pages-freed-on-exit");
  else
    test_fail("T10-pages-freed-on-exit", "active_pages still high after child exit");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(void) {
  printf("\n=== Memory Instrumentation Test Suite ===\n\n");

  test_syscall_basic();
  test_active_nonzero();
  test_allocs_increase();
  test_frees_increase();
  test_faults_increase();
  test_cow_faults();
  test_peak_ge_active();
  test_memory_accounting();
  test_multiple_cow_forks();
  test_pages_freed_on_exit();

  printf("\n=== Results: %d passed, %d failed ===\n\n", passed, failed);
  exit(failed > 0 ? 1 : 0);
}
