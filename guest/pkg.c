/* /usr/bin/pkg — the package database, and the repair verb.
 *
 *   pkg list                 what is installed
 *   pkg owns <path>          which package would I be reinstalling
 *   pkg verify [name]        which files differ from what was shipped
 *   pkg reinstall <name>     put them back, from the repository
 *
 * verify works by hashing each installed file and comparing against the
 * manifest in /var/lib/pkg/<name>/files. The manifest is ON THE DISK, so it
 * can itself be damaged -- and when it is, verify says so rather than
 * reporting a clean system, because a check that cannot fail is worthless.
 */
#include "gsys.h"

static char arg[256], manifest[8192], filebuf[65536], path[256];

static int read_manifest(const char *pkg)
{
    g_copy(path, "/var/lib/pkg/", sizeof path);
    g_cat(path, pkg, sizeof path);
    g_cat(path, "/files", sizeof path);
    return g_slurp(path, manifest, sizeof manifest) >= 0;
}

/* one manifest line: "<mode> <hash> <path>" */
static int split3(char *line, char **a, char **b, char **c)
{
    char *v[GARGS];
    if (g_argv(line, v) < 3) return 0;
    *a = v[0]; *b = v[1]; *c = v[2];
    return 1;
}

static unsigned long parse_hex(const char *s)
{
    unsigned long v = 0;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else break;
        v = v * 16 + d;
    }
    return v;
}

static unsigned parse_oct(const char *s)
{
    unsigned v = 0;
    for (; *s >= '0' && *s <= '7'; s++) v = v * 8 + (unsigned)(*s - '0');
    return v;
}

/* Every finding names the package that owns it, because the next thing the
 * player does is reinstall something and the whole point is reinstalling the
 * RIGHT thing. A verify that only prints paths makes you look the owner up
 * by hand, every time. */
static void finding(const char *pkg, const char *path, const char *what)
{
    g_puts(pkg);
    for (u64 k = g_strlen(pkg); k < 16; k++) g_puts(" ");
    g_puts(path);
    for (u64 k = g_strlen(path); k < 34; k++) g_puts(" ");
    g_putln(what);
}

static int verify_one(const char *pkg, int *bad)
{
    if (!read_manifest(pkg)) {
        g_puts("pkg: "); g_puts(pkg);
        g_putln(": manifest missing or unreadable -- cannot verify this package");
        (*bad)++;
        return 0;
    }
    char *p = manifest;
    while (*p) {
        char *nl = p; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        static char line[300];
        g_copy(line, p, sizeof line);
        *nl = save; p = *nl ? nl + 1 : nl;
        char *t = g_trim(line);
        if (!*t || *t == '#') continue;
        char *mode, *hash, *fp;
        if (!split3(t, &mode, &hash, &fp)) continue;

        NomStat st;
        if (g_stat(fp, &st) != 0) {
            finding(pkg, fp, "MISSING"); (*bad)++;
            continue;
        }
        i64 n = g_slurp(fp, filebuf, sizeof filebuf);
        if (n < 0) { finding(pkg, fp, "UNREADABLE"); (*bad)++; continue; }
        unsigned long h = g_hash(filebuf, (u64)n);
        if (h != parse_hex(hash)) {
            finding(pkg, fp, "CHANGED"); (*bad)++;
        } else if ((unsigned)st.mode != parse_oct(mode)) {
            static char msg[48];
            g_copy(msg, "MODE is ", sizeof msg);
            finding(pkg, fp, "");
            g_puts("                 mode is "); g_putoct((unsigned)st.mode, 4);
            g_puts(", package shipped "); g_putoct(parse_oct(mode), 4); g_puts("\n");
            (*bad)++;
        }
    }
    return 1;
}

static void each_package(void (*fn)(const char *))
{
    static char name[64];
    for (int i = 0; i < 128; i++) {
        if (g_readdir("/var/lib/pkg", i, name) < 0) break;
        fn(name);
    }
}

static int g_bad;
static void verify_cb(const char *n) { verify_one(n, &g_bad); }

static void list_cb(const char *n)
{
    static char p2[128], ver[256];
    g_copy(p2, "/var/lib/pkg/", sizeof p2);
    g_cat(p2, n, sizeof p2);
    g_cat(p2, "/version", sizeof p2);
    g_puts(n);
    for (u64 k = g_strlen(n); k < 18; k++) g_puts(" ");
    if (g_slurp(p2, ver, sizeof ver) > 0) g_puts(g_trim(ver));
    g_puts("\n");
}

void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int n = g_argv(arg, v);
    if (n < 1) { g_putln("usage: pkg list|owns|verify|reinstall"); g_exit(1); }

    if (g_streq(v[0], "list")) { each_package(list_cb); g_exit(0); }

    if (g_streq(v[0], "owns")) {
        if (n < 2) { g_putln("usage: pkg owns <path>"); g_exit(1); }
        static char name[64];
        for (int i = 0; i < 128; i++) {
            if (g_readdir("/var/lib/pkg", i, name) < 0) break;
            if (!read_manifest(name)) continue;
            char *p = manifest;
            while (*p) {
                char *nl = p; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                static char line[300];
                g_copy(line, p, sizeof line);
                *nl = save; p = *nl ? nl + 1 : nl;
                char *a, *b, *c;
                char *t = g_trim(line);
                if (!*t || !split3(t, &a, &b, &c)) continue;
                if (g_streq(c, v[1])) { g_putln(name); g_exit(0); }
            }
        }
        g_putln("no package owns that path");
        g_exit(1);
    }

    if (g_streq(v[0], "verify")) {
        g_bad = 0;
        if (n >= 2) verify_one(v[1], &g_bad);
        else        each_package(verify_cb);
        if (!g_bad) g_putln("all files match their packages");
        else { g_puts("\n"); g_putn(g_bad); g_putln(" file(s) differ. `pkg reinstall <package>` puts them back."); }
        g_exit(g_bad ? 1 : 0);
    }

    if (g_streq(v[0], "reinstall")) {
        if (n < 2) { g_putln("usage: pkg reinstall <name>"); g_exit(1); }
        if (!read_manifest(v[1])) {
            g_puts("pkg: "); g_puts(v[1]); g_putln(": no such package");
            g_exit(1);
        }
        int done = 0, failed = 0;
        char *p = manifest;
        while (*p) {
            char *nl = p; while (*nl && *nl != '\n') nl++;
            char save = *nl; *nl = 0;
            static char line[300];
            g_copy(line, p, sizeof line);
            *nl = save; p = *nl ? nl + 1 : nl;
            char *mode, *hash, *fp;
            char *t = g_trim(line);
            if (!*t || !split3(t, &mode, &hash, &fp)) continue;
            /* Pull the pristine bytes from the repository, which is not on
             * this disk -- that is why this works on a wrecked machine. */
            i64 got = g_repo(v[1], fp, filebuf);
            if (got < 0) { g_puts("  cannot fetch "); g_putln(fp); failed++; continue; }
            int fd = g_open(fp, O_WRONLY | O_CREAT | O_TRUNC);
            if (fd < 0) { g_puts("  cannot write "); g_putln(fp); failed++; continue; }
            sysc(SYS_write, fd, (i64)filebuf, got);
            g_close(fd);
            sysc(1035, (i64)fp, (i64)parse_oct(mode), 0);   /* SYS_chmod */
            done++;
        }
        g_puts(v[1]); g_puts(": "); g_putn(done); g_puts(" files restored");
        if (failed) { g_puts(", "); g_putn(failed); g_puts(" failed"); }
        g_puts("\n");
        g_exit(failed ? 1 : 0);
    }

    g_puts("pkg: unknown command: "); g_putln(v[0]);
    g_exit(1);
}
