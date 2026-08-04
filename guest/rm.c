/* /bin/rm — remove a file. No undo, as ever. */
#include "gsys.h"
static char arg[256];
void _start(void){
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int n = g_argv(arg, v);
    if (n < 1) { g_putln("usage: rm <path>..."); g_exit(1); }
    int bad = 0;
    for (int i = 0; i < n; i++) {
        if (sysc(SYS_unlink, (i64)v[i], 0, 0) != 0) {
            g_puts("rm: "); g_puts(v[i]); g_putln(": cannot remove"); bad = 1;
        }
    }
    g_exit(bad);
}
