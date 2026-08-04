/* /bin/touch — create an empty file if it is not there. */
#include "gsys.h"
static char arg[256];
void _start(void){
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    if (g_argv(arg, v) < 1) { g_putln("usage: touch <path>"); g_exit(1); }
    int fd = g_open(v[0], O_WRONLY | O_CREAT);
    if (fd < 0) { g_puts("touch: "); g_puts(v[0]); g_putln(": cannot create"); g_exit(1); }
    g_close(fd);
    g_exit(0);
}
