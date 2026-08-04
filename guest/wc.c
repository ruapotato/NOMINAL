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
    i64 lines = 0;
    for (i64 i = 0; i < n; i++) if (buf[i] == '\n') lines++;
    if (n && buf[n-1] != '\n') lines++;
    g_putn(lines);
    g_puts("\n");
    g_exit(0);
}
