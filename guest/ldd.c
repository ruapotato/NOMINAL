/* /usr/bin/ldd — what does this program need, and can it be found?
 *
 * The one tool that turns a library fault from guesswork into reading. Every
 * other route to the same answer is indirect: run the program and read the
 * loader's complaint, or reinstall packages until one of them helps. ldd
 * asks the question directly, for a binary you have not run and may not be
 * able to run.
 *
 * It resolves the same way the loader does -- /etc/ld.so.conf, in order --
 * and compares the version the library declares against the version the
 * program asked for. That is deliberate: an ldd that disagrees with what
 * happens when you actually run the thing is worse than no ldd at all, so it
 * reads the dependency list out of the ELF through the same code the loader
 * uses rather than keeping its own idea of the format.
 */
#include "gsys.h"

static char needs[512];
static char conf[512];
static char libbuf[4096];

/* The version a library declares about itself, from its first line:
 * "\x7fELF (stub) zlib 1.3" -> "1.3". Same rule the loader applies. */
static int lib_version(const char *path, char *out, u64 outsz)
{
    out[0] = 0;
    i64 n = g_slurp(path, libbuf, sizeof libbuf);
    if (n < 0) return 0;
    u64 e = 0;
    while (e < (u64)n && libbuf[e] != '\n') e++;
    libbuf[e] = 0;
    /* the last space-separated word of the first line */
    char *last = libbuf;
    for (char *q = libbuf; *q; q++) if (*q == ' ') last = q + 1;
    g_copy(out, last, outsz);
    return out[0] != 0;
}

/* Walk /etc/ld.so.conf in order, exactly as the loader does. A library that
 * exists but sits in a directory nobody lists is not found, and saying so is
 * the whole point of this tool. */
static int find_lib(const char *soname, char *out, u64 outsz)
{
    if (g_slurp("/etc/ld.so.conf", conf, sizeof conf) < 0)
        g_copy(conf, "/lib\n/usr/lib\n", sizeof conf);
    char *p = conf;
    while (*p) {
        char *nl = p; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        char *dir = g_trim(p);
        if (*dir && *dir != '#') {
            static char cand[256];
            g_copy(cand, dir, sizeof cand);
            g_cat(cand, "/", sizeof cand);
            g_cat(cand, soname, sizeof cand);
            NomStat st;
            if (g_stat(cand, &st) == 0) { g_copy(out, cand, outsz); *nl = save; return 1; }
        }
        *nl = save;
        p = *nl ? nl + 1 : nl;
    }
    return 0;
}

void _start(void)
{
    static char arg[256];
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    if (g_argv(arg, v) < 1) { g_putln("usage: ldd <program>"); g_exit(1); }

    i64 got = sysc(SYS_needs, (i64)v[0], (i64)needs, sizeof needs - 1);
    if (got < 0) {
        g_puts("ldd: "); g_puts(v[0]);
        g_putln(": cannot read (is it there, and is it a program?)");
        g_exit(1);
    }
    needs[got] = 0;
    if (!got) { g_putln("\tstatically linked"); g_exit(0); }

    int bad = 0;
    char *p = needs;
    while (*p) {
        char *nl = p; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        static char line[160];
        g_copy(line, p, sizeof line);
        *nl = save; p = *nl ? nl + 1 : nl;

        char *t = g_trim(line);
        if (!*t) continue;
        /* "libz.so.1 1.3" */
        static char soname[96], want[64];
        soname[0] = want[0] = 0;
        u64 i = 0, k = 0;
        while (t[i] && t[i] != ' ' && k < sizeof soname - 1) soname[k++] = t[i++];
        soname[k] = 0;
        while (t[i] == ' ') i++;
        k = 0;
        while (t[i] && t[i] != ' ' && k < sizeof want - 1) want[k++] = t[i++];
        want[k] = 0;

        static char path[256], have[64];
        g_puts("\t");
        g_puts(soname);
        if (!find_lib(soname, path, sizeof path)) {
            g_putln(" => not found");
            bad++;
            continue;
        }
        g_puts(" => ");
        g_puts(path);
        if (want[0] && lib_version(path, have, sizeof have)) {
            g_puts(" (");
            g_puts(have);
            g_puts(")");
            /* Same comparison the loader makes: newer satisfies older. */
            int hmaj = 0, hmin = 0, wmaj = 0, wmin = 0;
            const char *q = have;
            while (*q >= '0' && *q <= '9') hmaj = hmaj * 10 + (*q++ - '0');
            if (*q == '.') { q++; while (*q >= '0' && *q <= '9') hmin = hmin * 10 + (*q++ - '0'); }
            q = want;
            while (*q >= '0' && *q <= '9') wmaj = wmaj * 10 + (*q++ - '0');
            if (*q == '.') { q++; while (*q >= '0' && *q <= '9') wmin = wmin * 10 + (*q++ - '0'); }
            if (hmaj < wmaj || (hmaj == wmaj && hmin < wmin)) {
                g_puts("  -- TOO OLD, this program needs ");
                g_puts(want);
                bad++;
            }
        }
        g_puts("\n");
    }
    g_exit(bad ? 1 : 0);
}
