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
        get(procbuf, "name", nm, sizeof nm, "");
        get(procbuf, "state", st, sizeof st, "");
        if (g_streq(nm, exec) && g_streq(st, "running")) return 1;
    }
    return 0;
}

void _start(void)
{
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

        const char *state;
        if (!g_streq(en, "yes"))        state = "disabled";
        else if (is_running(exec))      state = "running";
        else                            state = "DEAD";

        g_puts(nm);
        for (u64 k = g_strlen(nm); k < 17; k++) g_puts(" ");
        g_puts(state);
        for (u64 k = g_strlen(state); k < 11; k++) g_puts(" ");
        g_putln(exec);
    }
    g_putln("");
    g_putln("DEAD means the unit is enabled and nothing is running it.");
    g_exit(0);
}
