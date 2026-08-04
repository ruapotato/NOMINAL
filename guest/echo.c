/* /bin/echo — print the arguments. A real program as well as a shell builtin,
 * because a builtin cannot be a stage in a pipeline. */
#include "gsys.h"
static char arg[1024];
void _start(void)
{
    g_getarg(arg, sizeof arg);
    g_putln(arg);
    g_exit(0);
}
