/* /bin/chroot — make a mounted filesystem the root.
 *
 * After this, /etc is the customer's /etc and /bin/sh is the customer's shell.
 * That is the whole point: you stop looking at their disk from outside and
 * start running inside it, so the tools you use are theirs and the paths in
 * their config mean what they mean to them.
 */
#include "gsys.h"
static char arg[256];
void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    if (g_argv(arg, v) < 1) { g_putln("usage: chroot <dir>"); g_exit(1); }
    if (sysc(SYS_chroot, (i64)v[0], 0, 0) != 0) {
        g_puts("chroot: "); g_puts(v[0]);
        g_putln(": not a directory (is anything mounted there?)");
        g_exit(1);
    }
    g_puts("chroot: root is now "); g_putln(v[0]);
    g_exit(0);
}
