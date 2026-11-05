// procstat — display a point-in-time snapshot of xv6 processes.
#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"
#include "user/procinfo.h"

static char *states[] = { "unused", "used", "sleep", "runnable", "running", "zombie" };

static char *
state_name(int state)
{
  if (state >= 0 && state < 6)
    return states[state];
  return "unknown";
}

int
main(int argc, char **argv)
{
  static struct procinfo ps[NPROC];
  int n = getprocs(ps, NPROC);
  if (n < 0) {
    fprintf(2, "procstat: syscall failed\n");
    exit(1);
  }

  int requested = -1;
  if (argc == 2) {
    requested = atoi(argv[1]);
    if (requested <= 0) {
      fprintf(2, "usage: procstat [pid]\n");
      exit(1);
    }
  } else if (argc > 2) {
    fprintf(2, "usage: procstat [pid]\n");
    exit(1);
  }

  printf("PID PPID STATE    CPU WAIT RESP CTX VRT VM-PG FLT COW NAME\n");
  int shown = 0;
  for (int i = 0; i < n; i++) {
    if (requested != -1 && ps[i].pid != requested)
      continue;
    printf("%d %d %s %l %l %l %l %l %l %l %l %s\n",
           ps[i].pid, ps[i].ppid, state_name(ps[i].state), ps[i].cpu_ticks,
           ps[i].wait_ticks, ps[i].response_ticks, ps[i].ctx_switches,
           ps[i].vruntime, ps[i].mapped_pages, ps[i].page_faults,
           ps[i].cow_faults, ps[i].name);
    shown++;
  }
  if (requested != -1 && shown == 0) {
    fprintf(2, "procstat: pid %d not found\n", requested);
    exit(1);
  }
  exit(0);
}
