/* /usr/bin/links — the text browser.
 *
 * Resolution is done HERE, by reading the machine's own /etc/hosts and then
 * falling back to the nameserver in /etc/resolv.conf. That is deliberate: it
 * makes both files load-bearing, so "I can reach it by address but not by
 * name" is a real state of this machine and a real thing to diagnose.
 */
#define NOM_NEEDS_LIBZ   /* gzips what it writes */
#include "gsys.h"

static char arg[256], hosts[4096], resolv[256], page[65536], ipbuf[64];

/* Look a name up in /etc/hosts: "<address> <name> [alias...]" per line. */
static int hosts_lookup(const char *name, char *out, u64 cap)
{
    if (g_slurp("/etc/hosts", hosts, sizeof hosts) < 0) return 0;
    char *p = hosts;
    while (*p) {
        char *nl = p; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        static char line[256];
        g_copy(line, p, sizeof line);
        *nl = save; p = *nl ? nl + 1 : nl;
        char *t = g_trim(line);
        if (!*t || *t == '#') continue;
        char *v[GARGS];
        int n = g_argv(t, v);
        for (int i = 1; i < n; i++)
            if (g_streq(v[i], name)) { g_copy(out, v[0], cap); return 1; }
    }
    return 0;
}

static int have_nameserver(void)
{
    if (g_slurp("/etc/resolv.conf", resolv, sizeof resolv) < 0) return 0;
    char *p = resolv;
    while (*p) {
        char *nl = p; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        char *t = g_trim(p);
        int ok = (t[0]=='n'&&t[1]=='a'&&t[2]=='m'&&t[3]=='e'&&t[4]=='s'&&
                  t[5]=='e'&&t[6]=='r'&&t[7]=='v'&&t[8]=='e'&&t[9]=='r');
        *nl = save; p = *nl ? nl + 1 : nl;
        if (ok) return 1;
    }
    return 0;
}

void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    if (g_argv(arg, v) < 1) {
        g_putln("usage: links <host>[/path]");
        g_putln("try:   links wiki.nomnix.org");
        g_exit(1);
    }

    /* split host from path */
    static char host[128], path[192];
    char *u = v[0];
    if (u[0]=='h'&&u[1]=='t'&&u[2]=='t'&&u[3]=='p'&&u[4]==':'&&u[5]=='/'&&u[6]=='/') u += 7;
    u64 i = 0;
    while (u[i] && u[i] != '/') i++;
    for (u64 k = 0; k < i && k + 1 < sizeof host; k++) host[k] = u[k];
    host[i < sizeof host - 1 ? i : sizeof host - 1] = 0;
    g_copy(path, u[i] ? u + i : "/", sizeof path);

    /* an address needs no resolving */
    int numeric = (host[0] >= '0' && host[0] <= '9');
    if (numeric) {
        g_copy(ipbuf, host, sizeof ipbuf);
    } else if (hosts_lookup(host, ipbuf, sizeof ipbuf)) {
        /* found in /etc/hosts */
    } else if (have_nameserver() && g_dns(host, ipbuf, sizeof ipbuf) > 0) {
        /* found by the nameserver */
    } else {
        g_puts("links: cannot resolve ");
        g_putln(host);
        if (!have_nameserver())
            g_putln("       (no nameserver in /etc/resolv.conf, and it is not in /etc/hosts)");
        else
            g_putln("       (not in /etc/hosts and the nameserver does not know it)");
        g_exit(1);
    }

    i64 n = g_http(ipbuf, path, page);
    if (n < 0) {
        g_puts("links: nothing responded at ");
        g_putln(ipbuf);
        g_exit(1);
    }
    g_write(1, page, (u64)n);
    g_exit(0);
}
