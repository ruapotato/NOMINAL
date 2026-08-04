/* /bin/wc — count lines. Reads stdin when given no file, so it is useful at
 * the end of a pipeline, which is where wc lives. */
#include "gsys.h"
static char arg[256], buf[65536];
void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int na = g_argv(arg, v);
    i64 n = (na >= 1) ? g_slurp(v[0], buf, sizeof buf)
                      : g_slurp_stdin(buf, sizeof buf);
    if (n < 0) { g_puts("wc: "); g_puts(v[0]); g_putln(": cannot read"); g_exit(1); }
    /* lines, words, bytes -- the three fields wc actually prints. Returning
     * one number looked like wc and was not, which is worse than not having
     * it. */
    i64 lines = 0, words = 0, inword = 0;
    for (i64 i = 0; i < n; i++) {
        if (buf[i] == '\n') lines++;
        int sp = (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r');
        if (!sp && !inword) { words++; inword = 1; }
        else if (sp) inword = 0;
    }
    if (n && buf[n-1] != '\n') lines++;
    g_puts("  "); g_putn(lines);
    g_puts("  "); g_putn(words);
    g_puts("  "); g_putn(n);
    g_puts("\n");
    g_exit(0);
}
