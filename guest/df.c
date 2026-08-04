/* /bin/df — how much room is left, and what is mounted where.
 *
 * Space first, because that is what df is for and because a full disk is a
 * fault no amount of verifying will find: every file is exactly what it
 * should be, there is simply nowhere to put the next one.
 */
#include "gsys.h"
static char t[2048];
void _start(void)
{
    i64 used = sysc(SYS_dfused, 0, 0, 0);
    i64 cap  = sysc(SYS_dfused, 1, 0, 0);
    if (cap > 0) {
        g_putln("FILESYSTEM       SIZE     USED    AVAIL  USE%");
        g_puts("/dev/sda1     ");
        g_putn(cap / 1024);  g_puts("K   ");
        g_putn(used / 1024); g_puts("K   ");
        g_putn((cap - used) / 1024); g_puts("K   ");
        g_putn(cap ? (used * 100 / cap) : 0);
        g_putln("%");
        g_putln("");
    }
    i64 n = sysc(SYS_mounts, (i64)t, sizeof t, 0);
    g_putln("FILESYSTEM        MOUNTED ON");
    if (n > 0) g_write(1, t, (u64)n); else g_putln("(nothing mounted)");
    g_exit(0);
}
