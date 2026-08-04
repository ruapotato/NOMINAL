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

static char line[2048], cwd[256], tmp[512], expanded[2048];

/* One variable, set by `for`. A full environment is not what this shell is
 * for -- loops over a device list are, because that is the shape of the work:
 *   for i in dev sys proc; do mount /$i /mnt/$i; done
 */
static char var_name[32], var_val[128];

/* Substitute $name. Only the loop variable exists, so anything else expands
 * to nothing, exactly as an unset variable does in sh. */
static void expand(const char *in, char *out, u64 cap)
{
    u64 o = 0;
    for (u64 i = 0; in[i] && o + 1 < cap; ) {
        if (in[i] == '$' && in[i+1]) {
            u64 j = i + 1;
            char nm[32]; u64 k = 0;
            /* ${i} and $i both work; the braces matter when the name is
             * followed by a letter, as in /mnt/${i}x */
            int braced = (in[j] == '{');
            if (braced) j++;
            while (in[j] && k + 1 < sizeof nm &&
                   ((in[j] >= 'a' && in[j] <= 'z') || (in[j] >= 'A' && in[j] <= 'Z') ||
                    (in[j] >= '0' && in[j] <= '9') || in[j] == '_')) nm[k++] = in[j++];
            nm[k] = 0;
            if (braced && in[j] == '}') j++;
            if (k && g_streq(nm, var_name)) {
                for (u64 q = 0; var_val[q] && o + 1 < cap; q++) out[o++] = var_val[q];
            }
            i = j;
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = 0;
}

static int run_line(char *cmd);

/* Run a ;-separated list of commands. Stops at the first failure, which is
 * what `set -e` gives you and what a boot script needs. */
static int is_for_impl(const char *s2)
{
    while (*s2 == ' ') s2++;
    return s2[0] == 'f' && s2[1] == 'o' && s2[2] == 'r' && s2[3] == ' ';
}

static int is_for(const char *s2) { return is_for_impl(s2); }

static const char *PATHDIRS[] = { "/bin", "/usr/bin", "/sbin", "/usr/sbin", 0 };

/* a | b | c
 *
 * Each stage runs to completion and its output becomes the next stage's
 * input. There is no concurrency, which is right: these are filters, and a
 * filter that has not finished has nothing to say yet. */
static int run_pipeline(char *s2)
{
    int rc = 0;
    char *stage = s2;
    while (stage) {
        char *bar = stage;
        /* Quotes hide a pipe. Without this, `sed "s|a|b|" f` was torn into
         * three "pipeline stages" and the shell reported the middle of a
         * substitution as a command that could not be found -- which is why
         * an alternate sed delimiter appeared not to work at all. */
        {
            char q = 0;
            for (; *bar; bar++) {
                if (q)                        { if (*bar == q) q = 0; continue; }
                if (*bar == '"' || *bar == '\'') { q = *bar; continue; }
                if (*bar == '|') break;
            }
        }
        char save = *bar;
        *bar = 0;
        char *one = g_trim(stage);
        if (*one) {
            /* split verb from arguments for this stage */
            char *rest = one;
            while (*rest && *rest != ' ' && *rest != '\t') rest++;
            if (*rest) { *rest++ = 0; while (*rest == ' ') rest++; }
            static char full[256];
            NomStat st;
            const char *prog = 0;
            if (one[0] == '/') { if (g_stat(one, &st) == 0) prog = one; }
            else {
                for (int i = 0; PATHDIRS[i] && !prog; i++) {
                    g_copy(full, PATHDIRS[i], sizeof full);
                    g_cat(full, "/", sizeof full);
                    g_cat(full, one, sizeof full);
                    if (g_stat(full, &st) == 0) prog = full;
                }
            }
            if (!prog) {
                g_puts(one);
                g_putln(": command not found");
                return 127;
            }
            rc = (int)g_pipe(prog, rest);
        }
        *bar = save;
        stage = save ? bar + 1 : 0;
    }
    g_pipeout();
    return rc;
}

/* `a && b` and `a || b`, with the short-circuit that makes them worth having.
 *
 * Neither was parsed. `echo a && echo b` printed the literal text
 * "a && echo b", because `&&` was never an operator and echo simply received
 * it as an argument -- which is the sort of thing that makes a shell feel
 * fake. They bind tighter than `;` here, as they do everywhere.
 *
 * Quotes are respected while scanning, so `grep "a && b" f` is one command. */
static int run_andor(char *s2)
{
    int rc = 0;
    while (*s2) {
        char *p = s2;
        char *op = 0;
        int is_and = 0;
        char q = 0;
        for (; *p; p++) {
            if (q)                      { if (*p == q) q = 0; continue; }
            if (*p == '"' || *p == '\'') { q = *p; continue; }
            if (p[0] == '&' && p[1] == '&') { op = p; is_and = 1; break; }
            if (p[0] == '|' && p[1] == '|') { op = p; is_and = 0; break; }
        }
        if (op) *op = 0;
        char *one = g_trim(s2);
        if (*one) rc = run_line(one);
        if (!op) return rc;
        s2 = op + 2;
        /* Short-circuit: && skips the rest on failure, || on success. */
        if (( is_and && rc != 0) || (!is_and && rc == 0)) {
            /* Skip to the next operator of the OPPOSITE persuasion, or the
             * end. Chained `a || b || c` must not run b AND c. */
            while (*s2) {
                char *n = s2, qq = 0;
                int found = 0;
                for (; *n; n++) {
                    if (qq)                        { if (*n == qq) qq = 0; continue; }
                    if (*n == '"' || *n == '\'')   { qq = *n; continue; }
                    if ((n[0] == '&' && n[1] == '&') ||
                        (n[0] == '|' && n[1] == '|')) { found = 1; break; }
                }
                if (!found) return rc;
                int next_and = (n[0] == '&');
                s2 = n + 2;
                if (( is_and && !next_and) || (!is_and && next_and)) break;
            }
        }
    }
    return rc;
}

static int run_list(char *s2)
{
    int rc = 0;
    /* A `for` owns the whole line, semicolons and all -- splitting first
     * would tear `for i in a b; do x; done` into three broken fragments. */
    if (is_for(s2)) return run_line(s2);
    while (*s2) {
        char *semi = s2, q = 0;
        for (; *semi; semi++) {
            if (q)                       { if (*semi == q) q = 0; continue; }
            if (*semi == '"' || *semi == '\'') { q = *semi; continue; }
            if (*semi == ';') break;
        }
        char save = *semi;
        *semi = 0;
        char *one = g_trim(s2);
        /* `;` runs the next command whatever happened to the last one. It
         * used to stop on failure, which is `&&` wearing a semicolon. */
        if (*one) rc = run_andor(one);
        *semi = save;
        s2 = save ? semi + 1 : semi;
    }
    return rc;
}


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
    g_exit(run_list(cmd));
}

/* Output redirection. `>` truncates, `>>` appends. Implemented by running the
 * command with stdout pointed at a file, which is what a shell does -- and it
 * is the only way to edit a file on this machine, so it is not a luxury:
 *     echo "nameserver 10.0.2.3" > /etc/resolv.conf
 */
static int redirect_fd = -1;

static int run_line(char *cmd0)
{
    /* A `for` is parsed BEFORE expansion. Expanding first would substitute
     * $i while the loop variable is still unset, so the body would be built
     * with empty values and the loop would run the wrong command every time.
     * (That bug mounted / over everything.) */
    if (!is_for(cmd0)) {
        expand(cmd0, expanded, sizeof expanded);
    } else {
        g_copy(expanded, cmd0, sizeof expanded);
    }
    char *cmd = g_trim(expanded);
    if (!*cmd || *cmd == '#') return 0;

    /* for NAME in A B C; do BODY; done
     *
     * Parsed here rather than by the session, because loop syntax is the
     * shell's business. The body is run once per word with NAME set. */
    if (cmd[0] == 'f' && cmd[1] == 'o' && cmd[2] == 'r' && cmd[3] == ' ') {
        char *p = cmd + 4;
        while (*p == ' ') p++;
        char *nm = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
        while (*p == ' ') p++;
        if (!(p[0] == 'i' && p[1] == 'n' && (p[2] == ' ' || p[2] == 0))) {
            g_putln("sh: for: expected `in`");
            return 1;
        }
        p += 2;
        char *words_start = p;
        /* the word list runs up to `do` (or a `;` before it) */
        char *dopos = 0;
        for (char *q = p; *q; q++) {
            if ((q == p || q[-1] == ' ' || q[-1] == ';') &&
                q[0] == 'd' && q[1] == 'o' && (q[2] == ' ' || q[2] == ';' || q[2] == 0)) {
                dopos = q; break;
            }
        }
        if (!dopos) { g_putln("sh: for: expected `do`"); return 1; }
        char *body = dopos + 2;
        dopos[0] = 0;
        /* trim a trailing ; from the word list */
        for (char *q = words_start; *q; q++) if (*q == ';') *q = ' ';
        /* the body runs up to a trailing `done` */
        u64 bl = g_strlen(body);
        while (bl && (body[bl-1] == ' ' || body[bl-1] == ';')) body[--bl] = 0;
        if (bl >= 4 && body[bl-4] == 'd' && body[bl-3] == 'o' &&
            body[bl-2] == 'n' && body[bl-1] == 'e') {
            body[bl-4] = 0;
        } else {
            g_putln("sh: for: expected `done`");
            return 1;
        }

        char *wv[GARGS];
        static char wcopy[512];
        g_copy(wcopy, words_start, sizeof wcopy);
        int wn = g_argv(wcopy, wv);
        g_copy(var_name, nm, sizeof var_name);
        for (int i = 0; i < wn; i++) {
            g_copy(var_val, wv[i], sizeof var_val);
            static char bodycopy[1024];
            g_copy(bodycopy, body, sizeof bodycopy);
            int rc = run_list(bodycopy);
            if (rc != 0) { var_name[0] = 0; return rc; }
        }
        var_name[0] = 0;
        return 0;
    }

    /* A pipeline is handled as a whole. Builtins do not pipe: cd and bind
     * change this process, and there is nothing to pipe them to. */
    for (char *q = cmd; *q; q++) {
        if (*q != '|') continue;
        return run_pipeline(cmd);
    }

    /* pull off a trailing > or >> before the verb is parsed */
    int append = 0;
    char *redir = 0;
    for (char *q = cmd; *q; q++) {
        if (*q != '>') continue;
        redir = q;
        *q = 0;
        q++;
        if (*q == '>') { append = 1; q++; }
        while (*q == ' ') q++;
        redir = q;
        break;
    }
    if (redir && !*redir) { g_putln("sh: > needs a file"); return 1; }
    if (redir) {
        redirect_fd = g_open(redir, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC));
        if (redirect_fd < 0) {
            g_puts("sh: "); g_puts(redir); g_putln(": cannot write");
            return 1;
        }
    }
    cmd = g_trim(cmd);

    /* split verb from the rest, keeping the rest intact for the child */
    char *rest = cmd;
    while (*rest && *rest != ' ' && *rest != '\t') rest++;
    if (*rest) { *rest++ = 0; while (*rest == ' ') rest++; }

    if (g_streq(cmd, "cd")) {
        const char *to = *rest ? rest : "/";
        if (g_chdir(to) != 0) { g_puts("cd: "); g_puts(to); g_putln(": no such directory"); return 1; }
        return 0;
    }
    if (g_streq(cmd, "pwd")) {
        g_getcwd(cwd, sizeof cwd);
        g_putln(cwd);
        return 0;
    }
    if (g_streq(cmd, "bind")) {
        char *v[GARGS];
        int n = g_argv(rest, v);
        if (n < 2) { g_putln("usage: bind <target> <at>"); return 1; }
        if (g_bind(v[0], v[1]) != 0) { g_putln("bind: failed"); return 1; }
        return 0;
    }
    if (g_streq(cmd, "unbind")) {
        char *v[GARGS];
        if (g_argv(rest, v) < 1) { g_putln("usage: unbind <at>"); return 1; }
        if (g_unbind(v[0]) != 0) { g_putln("unbind: nothing bound there"); return 1; }
        return 0;
    }
    if (g_streq(cmd, "exit")) {
        /* Leaving a chroot is what `exit` means when you are in one -- that is
         * the flow the help text describes, and hanging up the connection
         * instead (which is what happened before) strands the player. */
        char cw[8] = "/";
        if (sysc(SYS_chroot, (i64)"//LEAVE", 0, 0) == 0) {
            g_putln("exit: left the chroot, back on the rescue medium");
            (void)cw;
            return 0;
        }
        g_putln("exit: not in a chroot (use `quit` to hang up)");
        return 0;
    }
    if (g_streq(cmd, "chroot")) {
        /* A builtin, not /bin/chroot, for the same reason cd is: it changes
         * THIS process's idea of where the root is, and a child that changed
         * its own and then exited would have accomplished nothing. */
        char *v[GARGS];
        if (g_argv(rest, v) < 1) { g_putln("usage: chroot <dir>"); return 1; }
        i64 crc = sysc(SYS_chroot, (i64)v[0], 0, 0);
        if (crc == -2) {
            /* The shell in there cannot run. Refusing is the whole point:
             * entering anyway leaves you unable to run even `exit`. */
            g_puts("chroot: "); g_puts(v[0]);
            g_putln(": there is a /bin/sh in there, and it cannot run --");
            g_putln("  its libraries are missing or the wrong version, so every");
            g_putln("  command inside would fail, including the one to get out.");
            g_putln("  Work on the disk from OUT HERE instead:");
            g_puts("      pkg --root "); g_puts(v[0]); g_putln(" verify");
            g_putln("  takes the same verbs (verify, owns, diff, reinstall) and");
            g_putln("  never runs anything off the broken disk. `ldd` on a binary");
            g_putln("  under the mount point will tell you which library it is.");
            return 1;
        }
        if (crc != 0) {
            g_puts("chroot: "); g_puts(v[0]);
            g_putln(": not a directory (is anything mounted there?)");
            return 1;
        }
        g_puts("chroot: root is now "); g_putln(v[0]);
        return 0;
    }
    if (g_streq(cmd, "echo")) {
        /* Quotes are removed and -n is honoured, exactly as /bin/echo does --
         * this builtin shadows it, so fixing only the program fixed nothing.
         * `echo "udev.* /dev/null" >> f` used to write the quote marks into
         * the file, and there was no way to write a line containing a space
         * without them. On a machine whose only editor is `echo >>` and
         * `sed`, that decides whether a config file can be repaired at all. */
        static char ebuf[1024];
        g_copy(ebuf, rest, sizeof ebuf);
        char *ev[GARGS];
        int en = g_argv(ebuf, ev);
        int ei = 0, enl = 1;
        if (en > 0 && g_streq(ev[0], "-n")) { enl = 0; ei = 1; }

        static char outb[1024];
        u64 o = 0;
        for (; ei < en; ei++) {
            for (const char *q = ev[ei]; *q && o + 2 < sizeof outb; q++) outb[o++] = *q;
            if (ei + 1 < en && o + 2 < sizeof outb) outb[o++] = ' ';
        }
        if (enl && o + 1 < sizeof outb) outb[o++] = '\n';

        if (redirect_fd >= 0) {
            sysc(SYS_write, redirect_fd, (i64)outb, (i64)o);
            g_close(redirect_fd);
            redirect_fd = -1;
        } else {
            g_write(1, outb, o);
        }
        return 0;
    }
    if (g_streq(cmd, "help")) {
        g_putln("builtins:  cd  pwd  bind  unbind  echo  help");
        g_putln("           for i in a b c; do ... ; done      $i expands");
        g_putln("           echo text > file        redirect (append with >>)");
        g_putln("           a | b | c               pipelines");
        g_putln("files:     ls cat cp mv rm touch grep head wc stat chmod sed");
        g_putln("system:    ps ns mount umount chroot df uname whoami pkg");
        g_putln("network:   links <host>[/path]      try links wiki.nomnix.org");
        g_putln("");
        g_putln("the machine's own state is under /proc: try `cat /proc/self/ns`");
        g_putln("what differs from what was shipped: `pkg verify`");
        return 0;
    }

    if (redirect_fd >= 0) {
        /* Only echo can redirect for now: a child process writes to the
         * console through its own stdout and this shell cannot hand it a
         * file descriptor without a real fork/exec. Saying so is better than
         * silently dropping the output. */
        g_close(redirect_fd);
        redirect_fd = -1;
        g_putln("sh: only `echo` can redirect at the moment");
        return 1;
    }
    int rc = try_exec(cmd, rest);
    if (rc == -2) { g_puts(cmd); g_putln(": command not found"); return 127; }
    return rc == 0 ? 0 : 1;
}
