/* /sbin/svcinit — bring up the services in /etc/services.d.
 *
 * Reads every .svc unit, honours `enabled` and `runlevel`, orders by `after`,
 * and starts what is left. The unit grammar is Hamnix's: `key: value` per
 * line, '#' comments.
 *
 * A required unit that will not start takes the boot down. That is the whole
 * reason a chmod on one daemon is a ticket.
 */
#include "gsys.h"

#define UNITS 32

static char names[UNITS][64];
static char execs[UNITS][160];
static char afters[UNITS][64];
static char descs[UNITS][96];
static char unitname[UNITS][64];
static int  started[UNITS];
static int  nunits;

static char body[4096];
static char level[16];
static char crit[UNITS][8];
static char restart[UNITS][16];
static int  failed_critical;

/* Pull `key` out of a `key: value` config body into `out`. */
static void get(const char *b, const char *key, char *out, u64 cap, const char *dflt)
{
    g_copy(out, dflt, cap);
    u64 kl = g_strlen(key);
    const char *p = b;
    while (*p) {
        const char *nl = p;
        while (*nl && *nl != '\n') nl++;
        const char *s = p;
        while (*s == ' ' || *s == '\t') s++;
        if (*s != '#') {
            u64 i = 0;
            while (i < kl && s + i < nl && s[i] == key[i]) i++;
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

static int wanted_at_level(const char *rl)
{
    const char *p = rl;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == level[0] && (p[1] == ' ' || p[1] == 0)) return 1;
        while (*p && *p != ' ') p++;
    }
    return 0;
}

void _start(void)
{
    if (g_getarg(level, sizeof level) <= 0) g_copy(level, "3", sizeof level);

    static char name[128], path[256];
    for (int i = 0; i < 512; i++) {
        i64 n = g_readdir("/etc/services.d", i, name);
        if (n < 0) break;
        if (!g_endswith(name, ".svc")) continue;
        if (nunits >= UNITS) break;

        g_copy(path, "/etc/services.d/", sizeof path);
        g_cat(path, name, sizeof path);
        if (g_slurp(path, body, sizeof body) < 0) {
            g_puts("svcinit: ");
            g_puts(path);
            g_putln(": cannot read");
            g_exit(1);
        }

        static char en[16], rl[32];
        get(body, "enabled", en, sizeof en, "yes");
        if (!g_streq(en, "yes")) continue;
        get(body, "runlevel", rl, sizeof rl, "3");
        if (!wanted_at_level(rl)) continue;

        int u = nunits++;
        g_copy(unitname[u], name, sizeof unitname[u]);
        get(body, "name",        names[u],  sizeof names[u],  name);
        /* `critical: yes` means the machine is not usable without it. Anything
         * else is reported and stepped over -- a box where sshd is down is a
         * different (and lesser) problem than a box that will not boot, and
         * conflating them would be wrong. */
        get(body, "critical",    crit[u],   sizeof crit[u],   "no");
        get(body, "restart",     restart[u], sizeof restart[u], "no");
        get(body, "exec",        execs[u],  sizeof execs[u],  "");
        get(body, "after",       afters[u], sizeof afters[u], "");
        get(body, "description", descs[u],  sizeof descs[u],  "");
    }

    for (int round = 0; round < UNITS + 1; round++) {
        int moved = 0;
        for (int u = 0; u < nunits; u++) {
            if (started[u]) continue;
            if (afters[u][0]) {
                int ready = 0;
                for (int v = 0; v < nunits; v++)
                    if (started[v] && g_streq(names[v], afters[u])) ready = 1;
                if (!ready) continue;
            }
            if (!execs[u][0]) {
                g_puts("svcinit: ");
                g_puts(unitname[u]);
                g_putln(": no exec line");
                g_exit(1);
            }
            /* Actually START it. A service that is merely present and
             * executable has not started; one that reads a missing config
             * and exits has started and failed, which is a different fault
             * with a different fix. */
            /* on-failure is the usual policy; a service that says nothing
             * gets no restart, which is the safe reading. */
            int pol = 0;
            if (g_streq(restart[u], "on-failure")) pol = 1;
            else if (g_streq(restart[u], "always")) pol = 2;
            i64 rc = g_svcstart(execs[u], names[u], pol);
            int bad = (rc != 0);
            const char *why = ": failed to start";
            if (bad) {
                g_puts("svcinit: ");
                g_puts(names[u]);
                g_puts(": ");
                g_puts(execs[u]);
                g_puts(why);
                if (g_streq(crit[u], "yes")) {
                    g_putln("  [critical]");
                    failed_critical = 1;
                    g_exit(1);
                }
                g_putln("  [degraded, continuing]");
                started[u] = 1;
                moved = 1;
                continue;
            }
            g_puts("svcinit: started ");
            g_puts(names[u]);
            if (descs[u][0]) { g_puts(" -- "); g_puts(descs[u]); }
            g_puts("\n");
            started[u] = 1;
            moved = 1;
        }
        if (!moved) break;
    }

    for (int u = 0; u < nunits; u++) {
        if (started[u]) continue;
        g_puts("svcinit: ");
        g_puts(names[u]);
        g_puts(": waiting for ");
        g_putln(afters[u][0] ? afters[u] : "?");
        if (g_streq(crit[u], "yes")) g_exit(1);
    }
    g_exit(failed_critical ? 1 : 0);
}
