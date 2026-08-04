/* /bin/head — the first ten lines. */
#include "gsys.h"
static char arg[256], buf[65536];
void _start(void){
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    if (g_argv(arg, v) < 1) { g_putln("usage: head <file>"); g_exit(1); }
    i64 n = g_slurp(v[0], buf, sizeof buf);
    if (n < 0) { g_puts("head: "); g_puts(v[0]); g_putln(": cannot read"); g_exit(1); }
    int lines = 0;
    for (i64 i = 0; i < n && lines < 10; i++) { g_write(1, buf + i, 1); if (buf[i] == '\n') lines++; }
    g_exit(0);
}
