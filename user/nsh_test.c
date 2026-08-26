// nsh_test.c — Functional tests for the nsh enhanced shell
//
// These tests exercise nsh features by running nsh with piped input
// via a script-like approach: each test forks, writes commands to
// the write end of a pipe connected to nsh's stdin, and checks output.
//
// Tests covered:
//   T01  simple command (echo)
//   T02  command with arguments
//   T03  invalid command (should print error, not crash)
//   T04  output redirection (> file)
//   T05  input redirection (< file)
//   T06  append redirection (>> file)
//   T07  single pipe
//   T08  multi-stage pipe
//   T09  background execution (&)
//   T10  built-in: pwd
//   T11  built-in: cd + pwd
//   T12  built-in: history
//   T13  built-in: exit
//   T14  sequential commands (;)
//   T15  file descriptor cleanup (no leaks after redir)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

static void
test_pass(const char *name)
{
  printf("  [PASS] %s\n", name);
  passed++;
}

static void
test_fail(const char *name, const char *reason)
{
  printf("  [FAIL] %s: %s\n", name, reason);
  failed++;
}

// Write a string to a file descriptor.
static void
writestr(int fd, const char *s)
{
  write(fd, s, strlen(s));
}

// Read up to nbuf-1 bytes from fd into buf; null-terminate; return bytes read.
static int
readbuf(int fd, char *buf, int nbuf)
{
  int total = 0;
  char c;
  while (total < nbuf - 1) {
    int n = read(fd, &c, 1);
    if (n <= 0) break;
    buf[total++] = c;
  }
  buf[total] = '\0';
  return total;
}

// Check whether needle appears in haystack.
static int
contains(const char *haystack, const char *needle)
{
  int hlen = strlen(haystack);
  int nlen = strlen(needle);
  if (nlen == 0) return 1;
  for (int i = 0; i <= hlen - nlen; i++) {
    if (memcmp(haystack + i, needle, nlen) == 0)
      return 1;
  }
  return 0;
}

// ── T01: simple command ───────────────────────────────────────────────────────

static void
test_simple_command(void)
{
  // Run: echo hello
  // Expected output contains "hello"
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T01-simple", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    // child: be nsh with stdin=pin[0], stdout=pout[1]
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);

  // Feed command and close write end so nsh sees EOF
  writestr(pin[1], "echo hello\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  if (contains(buf, "hello"))
    test_pass("T01-simple-command");
  else
    test_fail("T01-simple-command", "output did not contain 'hello'");
}

// ── T02: command with arguments ───────────────────────────────────────────────

static void
test_arguments(void)
{
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T02-args", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "echo arg1 arg2 arg3\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  if (contains(buf, "arg1") && contains(buf, "arg2") && contains(buf, "arg3"))
    test_pass("T02-arguments");
  else
    test_fail("T02-arguments", "not all arguments echoed");
}

// ── T03: invalid command ──────────────────────────────────────────────────────

static void
test_invalid_command(void)
{
  // Run a non-existent command; shell should print an error but not crash.
  int pin[2], perr[2];
  if (pipe(pin) < 0 || pipe(perr) < 0) { test_fail("T03-invalid", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(perr[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(2); dup(perr[1]); close(perr[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(perr[1]);
  writestr(pin[1], "nosuchcommand_xyzzy\n");
  close(pin[1]);

  char buf[512];
  readbuf(perr[0], buf, sizeof(buf));
  close(perr[0]);
  int st = 0;
  wait(&st);

  // Shell should still exit cleanly (not hang), status 0
  if (st == 0)
    test_pass("T03-invalid-command-no-crash");
  else
    test_fail("T03-invalid-command-no-crash", "nsh exited with non-zero status");
}

// ── T04: output redirection ───────────────────────────────────────────────────

static void
test_output_redir(void)
{
  // echo hello > /redir_out_test
  unlink("redir_out_test");

  int pin[2];
  if (pipe(pin) < 0) { test_fail("T04-outredir", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]);
    close(0); dup(pin[0]); close(pin[0]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]);
  writestr(pin[1], "echo redirected_output > redir_out_test\n");
  close(pin[1]);
  wait(0);

  // Read the file
  int fd = open("redir_out_test", O_RDONLY);
  if (fd < 0) { test_fail("T04-output-redir", "output file not created"); return; }
  char buf[256];
  int n = read(fd, buf, sizeof(buf)-1);
  close(fd);
  unlink("redir_out_test");
  if (n > 0) buf[n] = '\0'; else buf[0] = '\0';

  if (contains(buf, "redirected_output"))
    test_pass("T04-output-redir");
  else
    test_fail("T04-output-redir", "expected content not in output file");
}

// ── T05: input redirection ────────────────────────────────────────────────────

static void
test_input_redir(void)
{
  // Create a file, then cat it via input redir
  int fd = open("redir_in_test", O_WRONLY | O_CREATE | O_TRUNC);
  if (fd < 0) { test_fail("T05-inredir", "cannot create test file"); return; }
  write(fd, "input_content\n", 14);
  close(fd);

  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T05-inredir", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "cat < redir_in_test\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);
  unlink("redir_in_test");

  if (contains(buf, "input_content"))
    test_pass("T05-input-redir");
  else
    test_fail("T05-input-redir", "cat did not read from redirected input");
}

// ── T06: append redirection ───────────────────────────────────────────────────

static void
test_append_redir(void)
{
  unlink("append_test");

  int pin[2];
  if (pipe(pin) < 0) { test_fail("T06-append", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]);
    close(0); dup(pin[0]); close(pin[0]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]);
  writestr(pin[1], "echo line1 > append_test\n");
  writestr(pin[1], "echo line2 >> append_test\n");
  close(pin[1]);
  wait(0);

  int fd = open("append_test", O_RDONLY);
  if (fd < 0) { test_fail("T06-append-redir", "file not created"); return; }
  char buf[256];
  int n = read(fd, buf, sizeof(buf)-1);
  close(fd);
  unlink("append_test");
  if (n > 0) buf[n] = '\0'; else buf[0] = '\0';

  if (contains(buf, "line1") && contains(buf, "line2"))
    test_pass("T06-append-redir");
  else
    test_fail("T06-append-redir", "both lines not found in file");
}

// ── T07: single pipe ──────────────────────────────────────────────────────────

static void
test_single_pipe(void)
{
  // echo hello | cat   →  should output "hello"
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T07-pipe", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "echo pipetest | cat\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  if (contains(buf, "pipetest"))
    test_pass("T07-single-pipe");
  else
    test_fail("T07-single-pipe", "pipe output missing 'pipetest'");
}

// ── T08: multi-stage pipe ─────────────────────────────────────────────────────

static void
test_multi_pipe(void)
{
  // echo "hello world" | cat | cat  →  "hello world"
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T08-multipipe", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "echo multipipetest | cat | cat\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  if (contains(buf, "multipipetest"))
    test_pass("T08-multi-pipe");
  else
    test_fail("T08-multi-pipe", "multi-pipe output missing 'multipipetest'");
}

// ── T09: background execution ────────────────────────────────────────────────

static void
test_background(void)
{
  // echo bg & — shell should print [jid] pid and return immediately
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T09-bg", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "echo background_job &\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  // Should see [1] <pid> in output
  if (contains(buf, "[1]"))
    test_pass("T09-background-execution");
  else
    test_fail("T09-background-execution", "no job table entry printed");
}

// ── T10: built-in pwd ─────────────────────────────────────────────────────────

static void
test_builtin_pwd(void)
{
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T10-pwd", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "pwd\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  // Should contain '/' (at minimum the root path)
  if (contains(buf, "/"))
    test_pass("T10-builtin-pwd");
  else
    test_fail("T10-builtin-pwd", "pwd output missing '/'");
}

// ── T11: built-in cd + pwd ────────────────────────────────────────────────────

static void
test_builtin_cd(void)
{
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T11-cd", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "cd /\npwd\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  if (contains(buf, "/"))
    test_pass("T11-builtin-cd");
  else
    test_fail("T11-builtin-cd", "cd+pwd output unexpected");
}

// ── T12: built-in history ────────────────────────────────────────────────────

static void
test_builtin_history(void)
{
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T12-hist", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "echo aaa\n");
  writestr(pin[1], "echo bbb\n");
  writestr(pin[1], "history\n");
  close(pin[1]);

  char buf[1024];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  if (contains(buf, "echo aaa") && contains(buf, "echo bbb"))
    test_pass("T12-builtin-history");
  else
    test_fail("T12-builtin-history", "history output missing commands");
}

// ── T13: built-in exit ───────────────────────────────────────────────────────

static void
test_builtin_exit(void)
{
  int pin[2];
  if (pipe(pin) < 0) { test_fail("T13-exit", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]);
    close(0); dup(pin[0]); close(pin[0]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]);
  writestr(pin[1], "exit 0\n");
  close(pin[1]);

  int st = -1;
  wait(&st);

  if (st == 0)
    test_pass("T13-builtin-exit");
  else
    test_fail("T13-builtin-exit", "exit did not result in status 0");
}

// ── T14: sequential commands ─────────────────────────────────────────────────

static void
test_sequential(void)
{
  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T14-seq", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "echo seq_first; echo seq_second\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);

  if (contains(buf, "seq_first") && contains(buf, "seq_second"))
    test_pass("T14-sequential-commands");
  else
    test_fail("T14-sequential-commands", "not both outputs found");
}

// ── T15: file-descriptor cleanup after redirection ───────────────────────────

static void
test_fd_cleanup(void)
{
  // After two redirections, shell should still be functional
  unlink("fd_test1");
  unlink("fd_test2");

  int pin[2], pout[2];
  if (pipe(pin) < 0 || pipe(pout) < 0) { test_fail("T15-fdclean", "pipe failed"); return; }

  int pid = fork();
  if (pid == 0) {
    close(pin[1]); close(pout[0]);
    close(0); dup(pin[0]); close(pin[0]);
    close(1); dup(pout[1]); close(pout[1]);
    char *argv[] = { "nsh", 0 };
    exec("nsh", argv);
    exit(1);
  }
  close(pin[0]); close(pout[1]);
  writestr(pin[1], "echo a > fd_test1\n");
  writestr(pin[1], "echo b > fd_test2\n");
  writestr(pin[1], "echo after_redir\n");
  close(pin[1]);

  char buf[512];
  readbuf(pout[0], buf, sizeof(buf));
  close(pout[0]);
  wait(0);
  unlink("fd_test1");
  unlink("fd_test2");

  if (contains(buf, "after_redir"))
    test_pass("T15-fd-cleanup");
  else
    test_fail("T15-fd-cleanup", "shell non-functional after redirections");
}

// ── main ─────────────────────────────────────────────────────────────────────

int
main(void)
{
  printf("\n=== nsh Test Suite ===\n\n");

  test_simple_command();
  test_arguments();
  test_invalid_command();
  test_output_redir();
  test_input_redir();
  test_append_redir();
  test_single_pipe();
  test_multi_pipe();
  test_background();
  test_builtin_pwd();
  test_builtin_cd();
  test_builtin_history();
  test_builtin_exit();
  test_sequential();
  test_fd_cleanup();

  printf("\n=== Results: %d passed, %d failed ===\n\n", passed, failed);
  exit(failed > 0 ? 1 : 0);
}
