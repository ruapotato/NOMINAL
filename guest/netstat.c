/* /bin/netstat — what is listening, and on what.
 *
 * Derived, never declared. The address comes from /etc/net/interfaces, and a
 * port is listed only if the service that opens it is ACTUALLY RUNNING --
 * read out of /proc, the same place `ps` reads. So a web server that died
 * shows nothing, and a web server whose config was edited shows the port the
 * config now says.
 *
 * That is the whole reason it earns a place: it answers "is it actually
 * listening" with evidence rather than intention.
 */
#include "gsys.h"

static char buf[4096];
static char procbuf[2048];
static char pname[64];

/* first value of "key" in a config, ignoring case of the key's first letter */
static int field(const char *body, const char *key, char *out, u64 outsz)
{
    u64 kl = g_strlen(key);
    for (const char *p = body; *p; ) {
        const char *nl = p; while (*nl && *nl != '\n') nl++;
        const char *t = p;
        while (*t == ' ' || *t == '\t') t++;
        u64 k = 0;
        while (k < kl && t[k] == key[k]) k++;
        if (k == kl) {
            const char *q = t + kl;
            while (*q == ' ' || *q == '\t' || *q == '=' || *q == ':') q++;
            u64 o = 0;
            while (q < nl && *q != ' ' && *q != '\n' && o < outsz - 1) out[o++] = *q++;
            out[o] = 0;
            if (out[0]) return 1;
        }
        p = *nl ? nl + 1 : nl;
    }
    return 0;
}

static int running(const char *exec)
{
    static char pdir[96];
    for (int i = 0; i < 256; i++) {
        if (g_readdir("/proc", i, pname) < 0) break;
        g_copy(pdir, "/proc/", sizeof pdir);
        g_cat(pdir, pname, sizeof pdir);
        g_cat(pdir, "/status", sizeof pdir);
        if (g_slurp(pdir, procbuf, sizeof procbuf) < 0) continue;
        static char nm[128], stt[32];
        if (!field(procbuf, "name", nm, sizeof nm)) continue;
        field(procbuf, "state", stt, sizeof stt);
        if (g_streq(nm, exec) && g_streq(stt, "running")) return 1;
    }
    return 0;
}

void _start(void)
{
    static char addr[64] = "0.0.0.0";
    if (g_slurp("/etc/net/interfaces", buf, sizeof buf) >= 0)
        field(buf, "address", addr, sizeof addr);
    /* A machine on dhcp has no address until it has one. Printing "dhcp:22"
     * reads as a hostname; `*` is what every netstat prints for "any". */
    if (addr[0] < '0' || addr[0] > '9') g_copy(addr, "*", sizeof addr);

    g_putln("PROTO  LOCAL ADDRESS          STATE       SERVICE");

    int any = 0;
    /* ssh */
    if (running("/usr/sbin/sshd")) {
        static char port[16] = "22";
        if (g_slurp("/etc/ssh/sshd_config", buf, sizeof buf) >= 0)
            field(buf, "Port", port, sizeof port);
        g_puts("tcp    "); g_puts(addr); g_puts(":"); g_puts(port);
        for (u64 k = g_strlen(addr) + g_strlen(port) + 1; k < 22; k++) g_puts(" ");
        g_putln(" LISTEN      sshd");
        any = 1;
    }
    /* http */
    if (running("/usr/sbin/httpd")) {
        static char port[16] = "80";
        if (g_slurp("/etc/httpd/httpd.conf", buf, sizeof buf) >= 0)
            field(buf, "Listen", port, sizeof port);
        g_puts("tcp    "); g_puts(addr); g_puts(":"); g_puts(port);
        for (u64 k = g_strlen(addr) + g_strlen(port) + 1; k < 22; k++) g_puts(" ");
        g_putln(" LISTEN      httpd");
        any = 1;
    }
    /* mail */
    if (running("/usr/sbin/postfix")) {
        g_puts("tcp    "); g_puts(addr); g_putln(":25            LISTEN      postfix");
        any = 1;
    }
    if (!any)
        g_putln("(nothing is listening -- no network service is running)");

    g_putln("");
    g_puts("interface  "); g_putln(addr[0] ? addr : "not configured");
    g_exit(0);
}
