/* /bin/stat — what the kernel thinks a path is. */
#include "gsys.h"
static char arg[256];
void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    if (g_argv(arg, v) < 1) { g_putln("usage: stat <path>"); g_exit(1); }
    NomStat st;
    if (g_stat(v[0], &st) != 0) {
        g_puts("stat: "); g_puts(v[0]);
        g_putln(": no such file (or a symlink pointing at nothing)");
        g_exit(1);
    }
    g_puts("path  "); g_putln(v[0]);
    g_puts("kind  ");
    g_putln(st.kind == NOM_KIND_DIR ? "directory" :
            st.kind == NOM_KIND_LINK ? "symlink" :
            st.kind == NOM_KIND_DEV ? "device" : "file");
    g_puts("mode  "); g_putoct((unsigned)st.mode, 4);
    g_puts(st.mode & 0111 ? "  (executable)" : "  (not executable)");
    g_puts("\n");
    g_puts("size  "); g_putn(st.size); g_puts("\n");
    g_exit(0);
}
