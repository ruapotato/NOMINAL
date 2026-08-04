/* /bin/sed — substitute in place.
 *
 *   sed s/old/new/ file        print the result
 *   sed -i s/old/new/ file     write it back
 *
 * Deliberately one expression and no regex: the job here is fixing a line in
 * a config from a rescue shell, which is a substitution and nothing more.
 * Anything cleverer would be a worse tool for that job and a much bigger one.
 */
#include "gsys.h"

static char arg[512], buf[65536], out[65536];

void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int n = g_argv(arg, v);

    int inplace = 0, ai = 0;
    if (n > 0 && g_streq(v[0], "-i")) { inplace = 1; ai = 1; }
    if (n < ai + 2) {
        g_putln("usage: sed [-i] s/old/new/ <file>");
        g_exit(1);
    }
    char *expr = v[ai], *file = v[ai + 1];

    if (expr[0] != 's' || !expr[1]) {
        g_putln("sed: only s/old/new/ is supported");
        g_exit(1);
    }
    char sep = expr[1];
    char *from = expr + 2;
    char *to = from;
    while (*to && *to != sep) to++;
    if (!*to) { g_putln("sed: unterminated expression"); g_exit(1); }
    *to++ = 0;
    char *end = to;
    while (*end && *end != sep) end++;
    *end = 0;

    i64 len = g_slurp(file, buf, sizeof buf);
    if (len < 0) { g_puts("sed: "); g_puts(file); g_putln(": cannot read"); g_exit(1); }

    u64 fl = g_strlen(from), tl = g_strlen(to);
    if (fl == 0) { g_putln("sed: nothing to replace"); g_exit(1); }

    u64 o = 0;
    int hits = 0;
    for (u64 i = 0; i < (u64)len; ) {
        u64 k = 0;
        while (k < fl && i + k < (u64)len && buf[i + k] == from[k]) k++;
        if (k == fl) {
            for (u64 j = 0; j < tl && o + 1 < sizeof out; j++) out[o++] = to[j];
            i += fl;
            hits++;
        } else if (o + 1 < sizeof out) {
            out[o++] = buf[i++];
        } else break;
    }
    out[o] = 0;

    if (!inplace) { g_write(1, out, o); g_exit(0); }

    int fd = g_open(file, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { g_puts("sed: "); g_puts(file); g_putln(": cannot write"); g_exit(1); }
    sysc(SYS_write, fd, (i64)out, (i64)o);
    g_close(fd);
    g_puts("sed: ");
    g_putn(hits);
    g_puts(" replacement(s) in ");
    g_putln(file);
    g_exit(0);
}
