/* /bin/df — what is mounted where. */
#include "gsys.h"
static char t[2048];
void _start(void){
    i64 n = sysc(SYS_mounts, (i64)t, sizeof t, 0);
    g_putln("FILESYSTEM        MOUNTED ON");
    if (n > 0) g_write(1, t, (u64)n); else g_putln("(nothing mounted)");
    g_exit(0);
}
