/* /usr/bin/svc — what is actually running.
 *
 * `ps` shows processes; this shows SERVICES, which is a different question.
 * A service can be defined and disabled, defined and running, or defined and
 * dead in a loop the boot console scrolled past twenty lines ago. On a
 * machine that boots and is still wrong, this is where you look first.
 */
#include "gsys.h"

static char body[4096], name[128], path[192], procbuf[512], pname[128];

static void get(const char *b, const char *k, char *out, u64 cap, const char *dflt)
{
    g_copy(out, dflt, cap);
    u64 kl = g_strlen(k);
    const char *p = b;
    while (*p) {
        const char *nl = p; while (*nl && *nl != '\n') nl++;
        const char *s = p;
        while (*s == ' ' || *s == '\t') s++;
        if (*s != '#') {
            u64 i = 0;
            while (i < kl && s + i < nl && s[i] == k[i]) i++;
            if (i == kl && s + i < nl && (s[i] == ':' || s[i] == '=')) {
                const char *v = s + i + 1;
                while (v < nl && (*v == ' ' || *v == '\t')) v++;
                u64 j = 0;
                while (v + j < nl && j + 1 < cap) { out[j] = v[j]; j++; }
                while (j && (out[j-1] == ' ' || out[j-1] == '\r')) j--;
                out[j] = 0;
                return;
            }
        }
        p = *nl ? nl + 1 : nl;
    }
}

/* /proc uses "key value", the unit files use "key: value". Reusing the unit
 * parser on /proc matched nothing and reported every running service as DEAD
 * -- a diagnostic tool that lies is worse than no tool. */
static void proc_field(const char *b, const char *k, char *out, u64 cap)
{
    out[0] = 0;
    u64 kl = g_strlen(k);
    const char *p = b;
    while (*p) {
        const char *nl = p; while (*nl && *nl != '\n') nl++;
        u64 i = 0;
        while (i < kl && p + i < nl && p[i] == k[i]) i++;
        if (i == kl && p + i < nl && p[i] == ' ') {
            const char *v = p + i + 1;
            u64 j = 0;
            while (v + j < nl && j + 1 < cap) { out[j] = v[j]; j++; }
            out[j] = 0;
            return;
        }
        p = *nl ? nl + 1 : nl;
    }
}

/* Is this exec currently a live process? /proc is the truth; the unit file is
 * only an intention. */
static int is_running(const char *exec)
{
    static char pdir[64];
    for (int i = 0; i < 256; i++) {
        if (g_readdir("/proc", i, pname) < 0) break;
        g_copy(pdir, "/proc/", sizeof pdir);
        g_cat(pdir, pname, sizeof pdir);
        g_cat(pdir, "/status", sizeof pdir);
        if (g_slurp(pdir, procbuf, sizeof procbuf) < 0) continue;
        static char nm[128], st[32];
        proc_field(procbuf, "name", nm, sizeof nm);
        proc_field(procbuf, "state", st, sizeof st);
        if (g_streq(nm, exec) && g_streq(st, "running")) return 1;
    }
    return 0;
}

/* Rewrite one `key: value` line of a unit file, in place. */
static int set_field(const char *unit, const char *key, const char *val)
{
    static char up[192], nb[4096];
    g_copy(up, "/etc/services.d/", sizeof up);
    g_cat(up, unit, sizeof up);
    g_cat(up, ".svc", sizeof up);
    i64 n = g_slurp(up, body, sizeof body);
    if (n < 0) return 0;
    u64 o = 0, kl = g_strlen(key);
    int hit = 0;
    u64 i = 0;
    while (i < (u64)n) {
        u64 e = i; while (e < (u64)n && body[e] != '\n') e++;
        u64 k = 0;
        while (k < kl && i + k < e && body[i + k] == key[k]) k++;
        if (k == kl && i + kl < e && body[i + kl] == ':') {
            for (u64 q = 0; q < kl && o + 1 < sizeof nb; q++) nb[o++] = key[q];
            if (o + 2 < sizeof nb) { nb[o++] = ':'; nb[o++] = ' '; }
            for (const char *q = val; *q && o + 1 < sizeof nb; q++) nb[o++] = *q;
            hit = 1;
        } else {
            for (u64 q = i; q < e && o + 1 < sizeof nb; q++) nb[o++] = body[q];
        }
        if (o + 1 < sizeof nb) nb[o++] = '\n';
        i = e < (u64)n ? e + 1 : (u64)n;
    }
    if (!hit) return 0;
    int fd = g_open(up, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    sysc(SYS_write, fd, (i64)nb, (i64)o);
    g_close(fd);
    return 1;
}

void _start(void)
{
    /* `svc disable <name>` used to ignore its argument entirely, print the
     * whole table, and exit clean -- a silent no-op that a playtester
     * reasonably read as a broken command. Either it does the thing or it
     * says it cannot; doing neither is the one unacceptable answer. */
    static char arg[192];
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int argn = g_argv(arg, v);
    if (argn >= 1) {
        /* `svc status <name>` -- why is THIS one unhappy.
         *
         * The table could say running or DEAD and nothing else, so on a
         * machine that boots with a service quietly down there was no way to
         * ask the follow-up question. The kernel has always recorded what the
         * service said when it died and how many times it was restarted. */
        if (g_streq(v[0], "status")) {
            if (argn < 2) { g_putln("usage: svc status <name>"); g_exit(1); }
            static char info[1024];
            i64 n2 = sysc(SYS_svcinfo, (i64)v[1], (i64)info, sizeof info - 1);
            if (n2 <= 0) {
                g_puts("svc: "); g_puts(v[1]);
                g_putln(": nothing by that name has been started on this boot.");
                g_putln("  `svc` lists the units; a unit that is disabled or in");
                g_putln("  another runlevel was never started, so there is");
                g_putln("  nothing to report about it.");
                g_exit(1);
            }
            info[n2] = 0;
            g_write(1, info, (u64)n2);
            g_exit(0);
        }

        int off = g_streq(v[0], "disable");
        if (off || g_streq(v[0], "enable")) {
            if (argn < 2) {
                g_puts("usage: svc "); g_puts(v[0]); g_putln(" <name>");
                g_exit(1);
            }
            if (!set_field(v[1], "enabled", off ? "no" : "yes")) {
                g_puts("svc: "); g_puts(v[1]);
                g_putln(": no such unit in /etc/services.d");
                g_exit(1);
            }
            g_puts("svc: "); g_puts(v[1]);
            g_putln(off ? " disabled -- it will not start at the next boot"
                        : " enabled -- it will start at the next boot");
            g_putln("(the unit file is a package file: `pkg verify` will now");
            g_putln(" report it as CHANGED, which is correct -- you changed it)");
            g_exit(0);
        }
        g_puts("svc: unknown command: "); g_putln(v[0]);
        g_putln("usage: svc  |  svc status <name>  |  svc enable <name>  |  svc disable <name>");
        g_exit(1);
    }

    int any_dead = 0;
    g_putln("SERVICE          STATE      EXEC");
    for (int i = 0; i < 256; i++) {
        if (g_readdir("/etc/services.d", i, name) < 0) break;
        if (!g_endswith(name, ".svc")) continue;
        g_copy(path, "/etc/services.d/", sizeof path);
        g_cat(path, name, sizeof path);
        if (g_slurp(path, body, sizeof body) < 0) continue;

        static char nm[64], exec[160], en[16], rl[32];
        get(body, "name",     nm,   sizeof nm,   name);
        get(body, "exec",     exec, sizeof exec, "(none)");
        get(body, "enabled",  en,   sizeof en,   "yes");
        get(body, "runlevel", rl,   sizeof rl,   "3");

        /* A service that is not meant to run at this runlevel is not dead,
         * it is simply not here -- calling it DEAD sends the player looking
         * for a fault that does not exist. */
        int here = 0;
        for (const char *q = rl; *q; q++)
            if (*q == '3') here = 1;

        /* A UNIT WITH NO PROGRAM IN IT IS NOT A DEAD SERVICE.
         *
         * A unit whose only job is a `bind` starts nothing, so nothing is
         * running it, so this reported it DEAD -- and the legend underneath
         * then told the player to go and find out why a service had failed.
         * There is no service. Saying what it actually does is the difference
         * between a clue and a wild goose chase, and what it does is the
         * whole fault when one of these turns up unowned. */
        static char bnd[192];
        get(body, "bind", bnd, sizeof bnd, "");

        const char *state;
        if (!g_streq(en, "yes"))        state = "disabled";
        else if (!here)                 state = "not at rl3";
        else if (bnd[0] && g_streq(exec, "(none)")) state = "namespace";
        else if (is_running(exec))      state = "running";
        else                          { state = "DEAD"; any_dead = 1; }

        g_puts(nm);
        for (u64 k = g_strlen(nm); k < 17; k++) g_puts(" ");
        g_puts(state);
        for (u64 k = g_strlen(state); k < 11; k++) g_puts(" ");
        if (bnd[0] && g_streq(exec, "(none)")) {
            g_puts("bind ");
            g_putln(bnd);
        } else {
            g_putln(exec);
        }
    }
    /* Only explain DEAD when something is. The legend on every healthy
     * machine is furniture the eye stops seeing. */
    if (any_dead) {
        g_putln("");
        g_putln("DEAD means the unit is enabled and nothing is running it.");
    }
    g_exit(0);
}
