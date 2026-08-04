/* /bin/sed — substitute in place.
 *
 *   sed s/old/new/ file        print the result
 *   sed -i s/old/new/ file     write it back
 *   sed -i /text/d file        delete every line containing `text`
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
        g_putln("usage: sed [-i] s/old/new/ <file>   or   sed [-i] /text/d <file>");
        g_exit(1);
    }
    char *expr = v[ai], *file = v[ai + 1];

    /* /text/d -- delete every line containing `text`.
     *
     * Without this there was no way to remove a line from a file at all. A
     * playtester needed to delete one bad entry from /etc/fstab, had nothing
     * that could, and reinstalled the whole `filesystem` package to do it --
     * blowing away seven other files to get rid of one line. Deleting a line
     * is half of what anyone uses sed for on a broken machine. */
    if (expr[0] == '/') {
        char sepd = '/';
        char *pat = expr + 1;
        char *pe = pat;
        while (*pe && *pe != sepd) pe++;
        if (*pe != sepd || pe[1] != 'd' || pe[2]) {
            g_putln("sed: expected /text/d");
            g_exit(1);
        }
        *pe = 0;
        if (!*pat) { g_putln("sed: nothing to match"); g_exit(1); }

        i64 dlen = g_slurp(file, buf, sizeof buf);
        if (dlen < 0) { g_puts("sed: "); g_puts(file); g_putln(": cannot read"); g_exit(1); }
        u64 pl = g_strlen(pat), o2 = 0;
        int gone = 0;
        u64 i2 = 0;
        while (i2 < (u64)dlen) {
            u64 e2 = i2;
            while (e2 < (u64)dlen && buf[e2] != '\n') e2++;
            int match = 0;
            for (u64 q = i2; q + pl <= e2 && !match; q++) {
                u64 k2 = 0;
                while (k2 < pl && buf[q + k2] == pat[k2]) k2++;
                if (k2 == pl) match = 1;
            }
            if (match) gone++;
            else {
                for (u64 q = i2; q <= e2 && q < (u64)dlen && o2 + 1 < sizeof out; q++)
                    out[o2++] = buf[q];
                if (e2 >= (u64)dlen && o2 + 1 < sizeof out) out[o2++] = '\n';
            }
            i2 = e2 < (u64)dlen ? e2 + 1 : (u64)dlen;
        }
        out[o2] = 0;
        if (!inplace) { g_write(1, out, o2); g_exit(0); }
        int dfd = g_open(file, O_WRONLY | O_CREAT | O_TRUNC);
        if (dfd < 0) { g_puts("sed: "); g_puts(file); g_putln(": cannot write"); g_exit(1); }
        sysc(SYS_write, dfd, (i64)out, (i64)o2);
        g_close(dfd);
        g_puts("sed: "); g_putn(gone); g_puts(" line(s) deleted from ");
        g_putln(file);
        g_exit(0);
    }

    if (expr[0] != 's' || !expr[1]) {
        g_putln("sed: only s/old/new/ and /text/d are supported");
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
