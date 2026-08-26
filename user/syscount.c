#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(2, "Usage: syscount <mask> <command> [args]\n");
    exit(1);
  }

  int mask = atoi(argv[1]);

  if (getSysCount(mask) < 0) {
    fprintf(2, "syscount: getSysCount failed\n");
    exit(1);
  }

  // Prepare the arguments for the command to execute
  char *cmd_args[argc - 1];
  for (int i = 2; i < argc; i++) {
    cmd_args[i - 2] = argv[i];
  }
  cmd_args[argc - 2] = 0;

  // Execute the command
  exec(cmd_args[0], cmd_args);
  exit(0);
}
