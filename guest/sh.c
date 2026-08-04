/* /bin/sh — the shell.
 *
 * One command line per invocation, which is what an interactive session and a
 * remote connection both actually need. Builtins that change the process's own
 * state (cd, bind) act on the session, because the session IS a process and
 * its namespace and cwd persist between commands.
 *
 * Anything not a builtin is looked up in PATH and executed as a real program
 * on this machine. There is no magic: `ls` is /bin/ls, an rv64 binary, and if
 * it has been corrupted then `ls` fails the way a corrupted binary fails.
 */
#include "gsys.h"

static char line[512], cwd[256], tmp[512];

static const char *PATHDIRS[] = { "/bin", "/usr/bin", "/sbin", "/usr/sbin", 0 };

static int try_exec(const char *prog, const char *rest)
{
    NomStat st;
    if (prog[0] == '/' || prog[0] == '.') {
        if (g_stat(prog, &st) != 0) return -2;
        return (int)g_spawn(prog, rest);
    }
    for (int i = 0; PATHDIRS[i]; i++) {
        g_copy(tmp, PATHDIRS[i], sizeof tmp);
        g_cat(tmp, "/", sizeof tmp);
        g_cat(tmp, prog, sizeof tmp);
        if (g_stat(tmp, &st) == 0) return (int)g_spawn(tmp, rest);
    }
    return -2;
}

void _start(void)
{
    g_getarg(line, sizeof line);
    char *cmd = g_trim(line);
    if (!*cmd || *cmd == '#') g_exit(0);

    /* split verb from the rest, keeping the rest intact for the child */
    char *rest = cmd;
    while (*rest && *rest != ' ' && *rest != '\t') rest++;
    if (*rest) { *rest++ = 0; while (*rest == ' ') rest++; }

    if (g_streq(cmd, "cd")) {
        const char *to = *rest ? rest : "/";
        if (g_chdir(to) != 0) { g_puts("cd: "); g_puts(to); g_putln(": no such directory"); g_exit(1); }
        g_exit(0);
    }
    if (g_streq(cmd, "pwd")) {
        g_getcwd(cwd, sizeof cwd);
        g_putln(cwd);
        g_exit(0);
    }
    if (g_streq(cmd, "bind")) {
        char *v[GARGS];
        int n = g_argv(rest, v);
        if (n < 2) { g_putln("usage: bind <target> <at>"); g_exit(1); }
        if (g_bind(v[0], v[1]) != 0) { g_putln("bind: failed"); g_exit(1); }
        g_exit(0);
    }
    if (g_streq(cmd, "unbind")) {
        char *v[GARGS];
        if (g_argv(rest, v) < 1) { g_putln("usage: unbind <at>"); g_exit(1); }
        if (g_unbind(v[0]) != 0) { g_putln("unbind: nothing bound there"); g_exit(1); }
        g_exit(0);
    }
    if (g_streq(cmd, "echo")) { g_putln(rest); g_exit(0); }
    if (g_streq(cmd, "help")) {
        g_putln("builtins:  cd  pwd  bind  unbind  echo  help");
        g_putln("programs:  ls  cat  ps  ns  pkg  stat  chmod  boot");
        g_putln("");
        g_putln("the machine's own state is under /proc: try `cat /proc/self/ns`");
        g_putln("what differs from what was shipped: `pkg verify`");
        g_exit(0);
    }

    int rc = try_exec(cmd, rest);
    if (rc == -2) { g_puts(cmd); g_putln(": command not found"); g_exit(127); }
    g_exit(rc == 0 ? 0 : 1);
}
