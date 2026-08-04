/* /bin/ls — list a directory, with modes, so permission damage is visible. */
#include "gsys.h"
static char arg[256], name[256], full[512];
void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int n = g_argv(arg, v);
    /* Flags are accepted and ignored. This listing is already long-form, so
     * -l is what you get either way -- but reporting "ls: -la: not found"
     * reads as if the shell tried to run -la as a command, which confused a
     * playtester who quite reasonably typed `ls -la`. */
    const char *dir = ".";
    for (int i = 0; i < n; i++) {
        if (v[i][0] == '-' && v[i][1]) continue;
        dir = v[i];
        break;
    }

    NomStat st;
    if (g_stat(dir, &st) != 0) { g_puts("ls: "); g_puts(dir); g_putln(": not found"); g_exit(1); }
    if (st.kind != NOM_KIND_DIR) { g_putln(dir); g_exit(0); }

    for (int i = 0; i < 512; i++) {
        if (g_readdir(dir, i, name) < 0) break;
        g_copy(full, dir, sizeof full);
        if (!g_streq(dir, "/")) g_cat(full, "/", sizeof full);
        g_cat(full, name, sizeof full);
        /* A symlink is shown with its target. The wiki says the commonest
         * thing people miss is a dead /boot/vmlinuz, and that `ls` will make
         * it look healthy -- which was true and is a bad kind of true when
         * the fix is one character of output away. */
        static char tgt[192];
        i64 tl = g_readlink(full, tgt, sizeof tgt);

        NomStat s2;
        if (g_stat(full, &s2) == 0) {
            g_puts(s2.kind == NOM_KIND_DIR ? "d" : s2.kind == NOM_KIND_LINK ? "l" : "-");
            g_putoct((unsigned)s2.mode, 4);
            g_puts("  ");
            g_putn(s2.size);
            g_puts("\t");
        } else {
            /* stat failing on a name that readdir just returned means a
             * dangling link -- worth showing, not worth hiding. */
            g_puts("l????     ?\t");   /* dangling: readlink still works */
        }
        g_puts(name);
        if (tl > 0) { g_puts(" -> "); g_puts(tgt); }
        g_putln("");
    }
    g_exit(0);
}
