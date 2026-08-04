/* /usr/sbin/netd — the network daemon.
 *
 * Reads its configuration at startup and then stays running. If the config is
 * missing it says so and exits, which is a service failing to start rather
 * than a service that was never there -- a different fault with a different
 * fix.
 */
#include "gsys.h"
static char conf[2048];
static const char *CONF[] = { "/etc/net/interfaces", 0 };

static void publish(void)
{
    /* the first non-comment line of the config, as loaded */
    static char state[256];
    state[0] = 0;
    char *q = conf;
    while (*q) {
        char *nl = q; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        char *t = g_trim(q);
        if (*t && *t != '#') { g_copy(state, t, sizeof state); *nl = save; break; }
        *nl = save; q = *nl ? nl + 1 : nl;
    }
    /* Two lines: which file was loaded, and what it said. The kernel compares
     * the second against the file named by the first, which is how "running
     * with a stale configuration" becomes a state the machine can notice
     * rather than a thing only a person could spot. */
    int fd = g_open("/run/netd.state", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        /* A daemon that cannot write its state file is not running properly,
         * whatever it thinks. This used to return quietly, which meant that
         * deleting /run -- something a careless cleanup really does -- left
         * every service reporting itself healthy while the machine had no
         * idea what any of them had loaded. Silence here is how a fault stops
         * being a fault. */
        g_puts("netd: ");
        g_puts("/run/netd.state");
        g_putln(": cannot write state -- refusing to start");
        g_exit(1);
    }
    sysc(SYS_write, fd, (i64)CONF[0], (i64)g_strlen(CONF[0]));
    sysc(SYS_write, fd, (i64)"\n", 1);
    sysc(SYS_write, fd, (i64)state, (i64)g_strlen(state));
    sysc(SYS_write, fd, (i64)"\n", 1);
    g_close(fd);
}

static const char *KEY = "iface";
void _start(void)
{
    for (int i = 0; CONF[i]; i++) {
        if (g_slurp(CONF[i], conf, sizeof conf) < 0) {
            g_puts("netd: ");
            g_puts(CONF[i]);
            g_putln(": cannot read -- refusing to start");
            g_exit(1);
        }
    }
    /* The config is there. Is it USABLE? A file that exists and does not say
     * the one thing this daemon needs is a completely different fault from a
     * file that is missing, and it fails later and less obviously. */
    {
        int ok = 0;
        char *q = conf;
        while (*q) {
            char *nl = q; while (*nl && *nl != '\n') nl++;
            char save = *nl; *nl = 0;
            char *t = g_trim(q);
            if (*t && *t != '#') {
                u64 k = 0;
                while (KEY[k] && t[k] == KEY[k]) k++;
                if (!KEY[k]) ok = 1;      /* empty KEY: any real line will do */
            }
            *nl = save; q = *nl ? nl + 1 : nl;
            if (ok) break;
        }
        if (!ok) {
            g_puts("netd: ");
            g_puts(CONF[0]);
            g_putln(": no interface is configured -- refusing to start");
            g_exit(1);
        }
    }

    /* DOES THE DEVICE EXIST? The config names an interface; udev is what
     * decides what interfaces are called. Configuring eth0 on a machine where
     * udev has named the device something else fails in a way that looks like
     * nothing at all is wrong -- both files are valid, both are what somebody
     * intended, and they disagree. */
    {
        static char rules[2048], want[64];
        want[0] = 0;
        /* the name from our own config: "iface eth0" */
        {
            char *q = conf;
            while (*q) {
                char *nl = q; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                char *t = g_trim(q);
                if (t[0] == 'i' && t[1] == 'f' && t[2] == 'a' && t[3] == 'c' &&
                    t[4] == 'e' && (t[5] == ' ' || t[5] == '\t')) {
                    char *w = t + 5;
                    while (*w == ' ' || *w == '\t') w++;
                    u64 k = 0;
                    while (w[k] && w[k] != ' ' && w[k] != '\t' && k < sizeof want - 1) {
                        want[k] = w[k]; k++;
                    }
                    want[k] = 0;
                }
                *nl = save; q = *nl ? nl + 1 : nl;
                if (want[0]) break;
            }
        }
        if (want[0] &&
            g_slurp("/etc/udev/rules.d/50-default.rules", rules, sizeof rules) >= 0) {
            /* NAME="..." on the net rule */
            static char named[64];
            named[0] = 0;
            char *q = rules;
            while (*q) {
                char *nl = q; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                char *t = g_trim(q);
                if (g_contains(t, "net") && g_contains(t, "NAME=")) {
                    char *n2 = t;
                    while (*n2 && !(n2[0] == 'N' && n2[1] == 'A' && n2[2] == 'M' &&
                                    n2[3] == 'E' && n2[4] == '=')) n2++;
                    if (*n2) {
                        n2 += 5;
                        if (*n2 == '"') n2++;
                        u64 k = 0;
                        while (n2[k] && n2[k] != '"' && k < sizeof named - 1) {
                            named[k] = n2[k]; k++;
                        }
                        named[k] = 0;
                    }
                }
                *nl = save; q = *nl ? nl + 1 : nl;
                if (named[0]) break;
            }
            if (named[0] && !g_streq(named, want)) {
                g_puts("netd: ");
                g_puts(CONF[0]);
                g_puts(": configures ");
                g_puts(want);
                g_puts(", but udev names this machine's network device ");
                g_puts(named);
                g_putln("");
                g_putln("  (see /etc/udev/rules.d/50-default.rules)");
                g_putln("  refusing to start: there is no such interface");
                g_exit(1);
            }
        }
    }

    /* Publish what was actually loaded. The file on disk says what the
     * machine is SUPPOSED to do; this says what the running process is
     * actually doing, and the two drift the moment somebody edits a config
     * and does not reload. That gap is invisible without this. */
    publish();

    /* Up. A daemon spends its life here, looking occasionally to see whether
     * anyone has asked it to re-read its configuration. */
    for (;;) {
        if (g_sigpend() == SIG_HUP) {
            if (g_slurp(CONF[0], conf, sizeof conf) >= 0) publish();
        }
    }
}
