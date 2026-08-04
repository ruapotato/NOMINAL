/* /bin/uname — what am I running on. */
#include "gsys.h"
static char b[256];
void _start(void){
    g_puts("Hamnix ");
    if (g_slurp("/etc/hostname", b, sizeof b) > 0) g_puts(g_trim(b)); else g_puts("(unknown)");
    g_putln(" 6.4.11 rv64 nominal");
    g_exit(0);
}
