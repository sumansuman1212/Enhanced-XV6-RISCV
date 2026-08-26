// nsh.c — Enhanced Unix-like Shell for xv6
//
// Architecture:
//   readline   — reads input, builds history
//   parser     — tokenizer + recursive-descent parser → cmd tree
//   executor   — evaluates cmd tree (fork/exec/pipe/redir/back)
//   builtins   — cd, pwd, exit, history, jobs, fg, bg
//   jobmgr     — job table, background process tracking
//
// Limitations (documented):
//   - No POSIX process groups: xv6 has no setpgrp/tcsetpgrp.
//   - fg/bg cannot send SIGCONT/SIGSTOP: implemented as wait/no-wait.
//   - No signal masking beyond sigalarm.
//   - No tab completion or line editing.
//   - No environment variables.
//   - MAXARGS limited to 32.
//   - MAXCMD limited to 512 bytes.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// ───────────────────────────────────────────────
// Constants
// ───────────────────────────────────────────────

#define MAXCMD    512   // max command line length
#define MAXARGS   32    // max arguments per command
#define MAXJOBS   16    // max concurrent background jobs
#define MAXHIST   50    // history ring-buffer size
#define MAXPATH   128   // max path length

// ───────────────────────────────────────────────
// Command node types
// ───────────────────────────────────────────────

#define CMD_EXEC  1
#define CMD_REDIR 2
#define CMD_PIPE  3
#define CMD_LIST  4
#define CMD_BACK  5

// ───────────────────────────────────────────────
// Command tree structures
// ───────────────────────────────────────────────

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];  // end pointers (for nul-termination)
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
  char cmdstr[MAXCMD];   // copy of original string for jobs display
};

// ───────────────────────────────────────────────
// Job table
// ───────────────────────────────────────────────

#define JOB_RUNNING  1
#define JOB_DONE     2
#define JOB_STOPPED  3   // not truly implementable without signals

struct job {
  int    jid;            // job ID (1-based)
  int    pid;            // process ID
  int    state;          // JOB_RUNNING / JOB_DONE / JOB_STOPPED
  char   cmd[MAXCMD];    // command string for display
};

static struct job jobtab[MAXJOBS];
static int njobs = 0;
static int next_jid = 1;

// ───────────────────────────────────────────────
// History ring buffer
// ───────────────────────────────────────────────

static char history[MAXHIST][MAXCMD];
static int  hist_count = 0;  // total entries ever added
static int  hist_head  = 0;  // index of next write slot (mod MAXHIST)

static void
hist_add(const char *line)
{
  // Skip blank lines and duplicates of the immediately preceding entry
  if (line[0] == '\0' || line[0] == '\n')
    return;
  int prev = (hist_head + MAXHIST - 1) % MAXHIST;
  if (hist_count > 0 && strcmp(history[prev], line) == 0)
    return;
  // Copy, stripping trailing newline
  int i;
  for (i = 0; line[i] && line[i] != '\n' && i < MAXCMD - 1; i++)
    history[hist_head][i] = line[i];
  history[hist_head][i] = '\0';
  hist_head = (hist_head + 1) % MAXHIST;
  hist_count++;
}

// ───────────────────────────────────────────────
// Job management helpers
// ───────────────────────────────────────────────

static int
job_add(int pid, const char *cmd)
{
  if (njobs >= MAXJOBS)
    return -1;
  // Find free slot
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobtab[i].state == 0) {
      jobtab[i].jid   = next_jid++;
      jobtab[i].pid   = pid;
      jobtab[i].state = JOB_RUNNING;
      int j;
      for (j = 0; cmd[j] && j < MAXCMD - 1; j++)
        jobtab[i].cmd[j] = cmd[j];
      jobtab[i].cmd[j] = '\0';
      njobs++;
      return jobtab[i].jid;
    }
  }
  return -1;
}

static struct job *
job_by_jid(int jid)
{
  for (int i = 0; i < MAXJOBS; i++)
    if (jobtab[i].state != 0 && jobtab[i].jid == jid)
      return &jobtab[i];
  return 0;
}

static struct job * __attribute__((unused))
job_by_pid(int pid)
{
  for (int i = 0; i < MAXJOBS; i++)
    if (jobtab[i].state != 0 && jobtab[i].pid == pid)
      return &jobtab[i];
  return 0;
}

// Reap finished background jobs (non-blocking poll via wait with 0 addr).
// xv6 wait() blocks; we can't truly poll. Instead we mark completed jobs
// when we explicitly wait for them in fg, or when the user runs 'jobs'.
// We use a best-effort approach: attempt a non-blocking check by forking
// a sentinel. Since xv6 has no WNOHANG, this is done via job state tracking.
static void
jobs_reap(void)
{
  // We cannot do non-blocking wait in xv6 (no WNOHANG).
  // Jobs are reaped when fg is called or when the shell explicitly waits.
  // The job table is cleaned up in job_remove().
}

static void
job_remove(int jid)
{
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobtab[i].state != 0 && jobtab[i].jid == jid) {
      jobtab[i].state = 0;
      jobtab[i].pid   = 0;
      jobtab[i].jid   = 0;
      jobtab[i].cmd[0] = '\0';
      njobs--;
      return;
    }
  }
}

// ───────────────────────────────────────────────
// Current working directory helper
// ───────────────────────────────────────────────

// xv6 has no getcwd() syscall. We track cwd manually.
// Initialized to "/" at startup, updated on cd.
static char tracked_cwd[MAXPATH] = "/";

static void
update_cwd_on_cd(const char *path)
{
  if (path[0] == '/') {
    // Absolute path — copy directly (simplified; no ".." resolution)
    int i;
    for (i = 0; path[i] && i < MAXPATH - 1; i++)
      tracked_cwd[i] = path[i];
    tracked_cwd[i] = '\0';
  } else {
    // Relative path — append
    int base = strlen(tracked_cwd);
    if (base > 0 && tracked_cwd[base-1] != '/') {
      if (base < MAXPATH - 1)
        tracked_cwd[base++] = '/';
    }
    int j = 0;
    while (path[j] && base < MAXPATH - 1)
      tracked_cwd[base++] = path[j++];
    tracked_cwd[base] = '\0';
  }
}

// ───────────────────────────────────────────────
// Prompt display
// ───────────────────────────────────────────────

static void
print_prompt(void)
{
  fprintf(2, "nsh:%s$ ", tracked_cwd);
}

// ───────────────────────────────────────────────
// readline — read a line from stdin
// ───────────────────────────────────────────────

static int
readline(char *buf, int nbuf)
{
  memset(buf, 0, nbuf);
  int i = 0;
  char c;
  while (i < nbuf - 1) {
    int n = read(0, &c, 1);
    if (n <= 0)
      return -1;   // EOF
    if (c == '\n' || c == '\r') {
      buf[i++] = '\n';
      break;
    }
    buf[i++] = c;
  }
  buf[i] = '\0';
  return i;
}

// ───────────────────────────────────────────────
// Parser — tokenizer
// ───────────────────────────────────────────────

static char whitespace[] = " \t\r\n\v";
static char symbols[]    = "<|>&;()";

static int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s = *ps;
  int ret;

  while (s < es && strchr(whitespace, *s))
    s++;
  if (q) *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') { ret = '+'; s++; }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq) *eq = s;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

static int
peek(char **ps, char *es, char *toks)
{
  char *s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

// ───────────────────────────────────────────────
// Parser — cmd constructors
// ───────────────────────────────────────────────

static void nsh_panic(char *msg) __attribute__((noreturn));
static void
nsh_panic(char *msg)
{
  fprintf(2, "nsh: %s\n", msg);
  exit(1);
}

static struct cmd *
execcmd(void)
{
  struct execcmd *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_EXEC;
  return (struct cmd *)cmd;
}

static struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_REDIR;
  cmd->cmd  = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd   = fd;
  return (struct cmd *)cmd;
}

static struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type  = CMD_PIPE;
  cmd->left  = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

static struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type  = CMD_LIST;
  cmd->left  = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

static struct cmd *
backcmd(struct cmd *subcmd, const char *cmdstr)
{
  struct backcmd *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_BACK;
  cmd->cmd  = subcmd;
  int i;
  for (i = 0; cmdstr[i] && i < MAXCMD - 1; i++)
    cmd->cmdstr[i] = cmdstr[i];
  cmd->cmdstr[i] = '\0';
  return (struct cmd *)cmd;
}

// ───────────────────────────────────────────────
// Parser — forward declarations
// ───────────────────────────────────────────────

static struct cmd *parseline(char **ps, char *es, const char *orig);
static struct cmd *parsepipe(char **ps, char *es);
static struct cmd *parseexec(char **ps, char *es);
static struct cmd *parseredirs(struct cmd *cmd, char **ps, char *es);
static struct cmd *parseblock(char **ps, char *es);
static struct cmd *nulterminate(struct cmd *cmd);

// ───────────────────────────────────────────────
// Parser — grammar implementation
// ───────────────────────────────────────────────

static struct cmd *
parsecmd(char *s, const char *orig)
{
  char *es = s + strlen(s);
  struct cmd *cmd = parseline(&s, es, orig);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "nsh: parse error: unexpected token near '%s'\n", s);
    return 0;
  }
  nulterminate(cmd);
  return cmd;
}

static struct cmd *
parseline(char **ps, char *es, const char *orig)
{
  struct cmd *cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd, orig ? orig : "");
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es, orig));
  }
  return cmd;
}

static struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

static struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      nsh_panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

static struct cmd *
parseblock(char **ps, char *es)
{
  if (!peek(ps, es, "("))
    nsh_panic("parseblock: missing (");
  gettoken(ps, es, 0, 0);
  struct cmd *cmd = parseline(ps, es, 0);
  if (!peek(ps, es, ")"))
    nsh_panic("syntax: missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

static struct cmd *
parseexec(char **ps, char *es)
{
  if (peek(ps, es, "("))
    return parseblock(ps, es);

  struct cmd *ret = execcmd();
  struct execcmd *cmd = (struct execcmd *)ret;
  int argc = 0;
  char *q, *eq;

  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    int tok = gettoken(ps, es, &q, &eq);
    if (tok == 0) break;
    if (tok != 'a')
      nsh_panic("syntax error");
    cmd->argv[argc]  = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      nsh_panic("too many arguments");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc]  = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

static struct cmd *
nulterminate(struct cmd *cmd)
{
  if (!cmd) return 0;
  struct execcmd  *ecmd;
  struct redircmd *rcmd;
  struct pipecmd  *pcmd;
  struct listcmd  *lcmd;
  struct backcmd  *bcmd;
  int i;

  switch (cmd->type) {
  case CMD_EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;
  case CMD_REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;
  case CMD_PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;
  case CMD_LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;
  case CMD_BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}

// ───────────────────────────────────────────────
// Executor — fork helper
// ───────────────────────────────────────────────

static int
fork1(void)
{
  int pid = fork();
  if (pid == -1)
    nsh_panic("fork failed");
  return pid;
}

// ───────────────────────────────────────────────
// Built-ins
// ───────────────────────────────────────────────

// Returns 1 if the command was a built-in and handled, 0 if not.
// Built-ins must run in the shell process itself (for cd, exit, etc.)
// so they are detected before fork.

static int
is_builtin(struct execcmd *cmd)
{
  if (!cmd || !cmd->argv[0]) return 0;
  char *name = cmd->argv[0];
  if (strcmp(name, "cd")      == 0) return 1;
  if (strcmp(name, "pwd")     == 0) return 1;
  if (strcmp(name, "exit")    == 0) return 1;
  if (strcmp(name, "history") == 0) return 1;
  if (strcmp(name, "jobs")    == 0) return 1;
  if (strcmp(name, "fg")      == 0) return 1;
  if (strcmp(name, "bg")      == 0) return 1;
  return 0;
}

// ── cd ────────────────────────────────────────

static void
builtin_cd(struct execcmd *cmd)
{
  if (!cmd->argv[1]) {
    // cd with no argument — go to root (xv6 has no $HOME)
    if (chdir("/") < 0)
      fprintf(2, "nsh: cd: cannot cd to /\n");
    else
      update_cwd_on_cd("/");
    return;
  }
  if (chdir(cmd->argv[1]) < 0)
    fprintf(2, "nsh: cd: cannot cd to '%s'\n", cmd->argv[1]);
  else
    update_cwd_on_cd(cmd->argv[1]);
}

// ── pwd ───────────────────────────────────────

static void
builtin_pwd(void)
{
  // xv6 has no getcwd(). We use tracked_cwd.
  printf("%s\n", tracked_cwd);
}

// ── exit ──────────────────────────────────────

static void
builtin_exit(struct execcmd *cmd)
{
  int code = 0;
  if (cmd->argv[1])
    code = atoi(cmd->argv[1]);
  exit(code);
}

// ── history ───────────────────────────────────

static void
builtin_history(void)
{
  int total = hist_count > MAXHIST ? MAXHIST : hist_count;
  int start = hist_count > MAXHIST ? hist_head : 0;
  int base  = hist_count > MAXHIST ? hist_count - MAXHIST : 0;

  for (int i = 0; i < total; i++) {
    int idx = (start + i) % MAXHIST;
    printf("  %d  %s\n", base + i + 1, history[idx]);
  }
}

// ── jobs ──────────────────────────────────────

static void
builtin_jobs(void)
{
  jobs_reap();
  int found = 0;
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobtab[i].state != 0) {
      const char *statestr =
        jobtab[i].state == JOB_RUNNING ? "Running" :
        jobtab[i].state == JOB_STOPPED ? "Stopped" : "Done";
      printf("[%d] %s\t(pid %d)\t%s\n",
             jobtab[i].jid, statestr, jobtab[i].pid, jobtab[i].cmd);
      found = 1;
    }
  }
  if (!found)
    printf("nsh: no background jobs\n");
}

// ── fg ────────────────────────────────────────
// Limitation: xv6 has no tcsetpgrp/SIGCONT. fg simply waits for the
// job's PID to finish. If the job has already exited, wait() will fail.

static void
builtin_fg(struct execcmd *cmd)
{
  if (!cmd->argv[1]) {
    fprintf(2, "nsh: fg: usage: fg <job_id>\n");
    return;
  }
  int jid = atoi(cmd->argv[1]);
  struct job *j = job_by_jid(jid);
  if (!j) {
    fprintf(2, "nsh: fg: job %d not found\n", jid);
    return;
  }
  printf("[%d] fg: %s\n", j->jid, j->cmd);
  // Wait for the process to finish
  int status = 0;
  int wpid = wait(&status);
  if (wpid == j->pid || wpid >= 0) {
    job_remove(jid);
  } else {
    fprintf(2, "nsh: fg: wait failed for job %d\n", jid);
  }
}

// ── bg ────────────────────────────────────────
// Limitation: without SIGCONT, bg cannot resume a truly stopped process.
// bg marks a JOB_STOPPED job as JOB_RUNNING in our table (best-effort).

static void
builtin_bg(struct execcmd *cmd)
{
  if (!cmd->argv[1]) {
    fprintf(2, "nsh: bg: usage: bg <job_id>\n");
    return;
  }
  int jid = atoi(cmd->argv[1]);
  struct job *j = job_by_jid(jid);
  if (!j) {
    fprintf(2, "nsh: fg: job %d not found\n", jid);
    return;
  }
  if (j->state == JOB_RUNNING) {
    printf("nsh: bg: job %d is already running\n", jid);
    return;
  }
  // In xv6 we cannot send SIGCONT, so just mark it running.
  j->state = JOB_RUNNING;
  printf("[%d] bg: %s  (pid %d) — note: xv6 has no SIGCONT\n",
         j->jid, j->cmd, j->pid);
}

// ───────────────────────────────────────────────
// run_builtin — execute a built-in in the shell process
// ───────────────────────────────────────────────

static void
run_builtin(struct execcmd *cmd)
{
  char *name = cmd->argv[0];
  if (strcmp(name, "cd")      == 0) builtin_cd(cmd);
  else if (strcmp(name, "pwd")     == 0) builtin_pwd();
  else if (strcmp(name, "exit")    == 0) builtin_exit(cmd);
  else if (strcmp(name, "history") == 0) builtin_history();
  else if (strcmp(name, "jobs")    == 0) builtin_jobs();
  else if (strcmp(name, "fg")      == 0) builtin_fg(cmd);
  else if (strcmp(name, "bg")      == 0) builtin_bg(cmd);
}

// ───────────────────────────────────────────────
// Executor — runcmd (child process context)
// ───────────────────────────────────────────────

static void runcmd(struct cmd *cmd) __attribute__((noreturn));

static void
runcmd(struct cmd *cmd)
{
  if (!cmd)
    exit(1);

  struct execcmd  *ecmd;
  struct redircmd *rcmd;
  struct pipecmd  *pcmd;
  struct listcmd  *lcmd;
  struct backcmd  *bcmd;
  int p[2];

  switch (cmd->type) {

  default:
    nsh_panic("runcmd: unknown cmd type");

  case CMD_EXEC:
    ecmd = (struct execcmd *)cmd;
    if (!ecmd->argv[0])
      exit(1);
    // Built-ins inside a pipeline run in a child — limited usefulness,
    // but we handle them so pipelines like "echo hi | pwd" don't crash.
    if (is_builtin(ecmd)) {
      run_builtin(ecmd);
      exit(0);
    }
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "nsh: exec '%s' failed\n", ecmd->argv[0]);
    exit(1);

  case CMD_REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "nsh: cannot open '%s'\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;   // noreturn; unreachable

  case CMD_LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;   // noreturn

  case CMD_PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      nsh_panic("pipe failed");
    // Left side: write end
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    // Right side: read end
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    exit(0);

  case CMD_BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    // Parent (child of shell) exits immediately — shell's wait() returns.
    exit(0);
  }
  exit(0);
}

// ───────────────────────────────────────────────
// eval — evaluate one command line in the shell
// ───────────────────────────────────────────────

static void
eval(char *line)
{
  // Trim leading whitespace for built-in detection
  char *p = line;
  while (*p == ' ' || *p == '\t') p++;

  // Strip trailing newline for parsing
  int len = strlen(line);
  if (len > 0 && line[len-1] == '\n')
    line[len-1] = '\0';

  if (strlen(p) == 0)
    return;

  // Parse command tree
  struct cmd *cmd = parsecmd(line, line);
  if (!cmd)
    return;

  // Top-level built-ins run in the shell process directly
  // (so that cd/exit/history/jobs affect the shell's own state)
  if (cmd->type == CMD_EXEC) {
    struct execcmd *ecmd = (struct execcmd *)cmd;
    if (is_builtin(ecmd)) {
      run_builtin(ecmd);
      free(cmd);
      return;
    }
  }

  // Background command at top level: track in job table
  if (cmd->type == CMD_BACK) {
    struct backcmd *bcmd = (struct backcmd *)cmd;
    int pid = fork1();
    if (pid == 0) {
      runcmd(bcmd->cmd);  // noreturn
    }
    // Parent: register job
    int jid = job_add(pid, bcmd->cmdstr[0] ? bcmd->cmdstr : line);
    if (jid > 0)
      printf("[%d] %d\n", jid, pid);
    free(cmd);
    return;
  }

  // Foreground: fork, run, wait
  int pid = fork1();
  if (pid == 0) {
    runcmd(cmd);   // noreturn in child
  }
  wait(0);
  free(cmd);
}

// ───────────────────────────────────────────────
// main
// ───────────────────────────────────────────────

int
main(void)
{
  static char buf[MAXCMD];
  int fd;

  // Ensure three file descriptors are open (stdin/stdout/stderr)
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Initialize job table to empty
  memset(jobtab, 0, sizeof(jobtab));

  // Print welcome banner
  printf("nsh — Enhanced xv6 Shell\n");
  printf("Type 'help' for built-ins, Ctrl-D to exit.\n\n");

  // Main REPL loop
  while (1) {
    print_prompt();

    int n = readline(buf, sizeof(buf));
    if (n < 0)   // EOF (Ctrl-D)
      break;
    if (n == 0)
      continue;

    // Strip trailing newline before history storage
    char histbuf[MAXCMD];
    int i;
    for (i = 0; buf[i] && buf[i] != '\n' && i < MAXCMD - 1; i++)
      histbuf[i] = buf[i];
    histbuf[i] = '\0';
    hist_add(histbuf);

    eval(buf);
  }

  printf("\nnsh: exit\n");
  exit(0);
}
