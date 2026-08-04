/* /sbin/init — pid 1.
 *
 * Reads /etc/inittab and runs the last non-comment line, exactly as Hamnix's
 * init2 does. It knows nothing else about booting: everything the machine
 * becomes is decided by files it reads at runtime, which is what makes this a
 * boot rather than a description of one.
 */
#include "gsys.h"

static char buf[8192];

void _start(void)
{
    g_putln("init: pid 1 starting");

    if (g_slurp("/etc/inittab", buf, sizeof buf) < 0) {
        g_putln("init: /etc/inittab: cannot read");
        g_exit(1);
    }

    /* the last non-comment, non-blank line wins */
    static char cmd[256];
    cmd[0] = 0;
    char *p = buf;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n') nl++;
        char save = *nl;
        *nl = 0;
        char *line = g_trim(p);
        if (*line && *line != '#') g_copy(cmd, line, sizeof cmd);
        *nl = save;
        p = *nl ? nl + 1 : nl;
    }

    if (!cmd[0]) {
        g_putln("init: /etc/inittab: nothing to run");
        g_exit(1);
    }

    /* split "prog arg" */
    static char prog[192], arg[192];
    int i = 0;
    while (cmd[i] && cmd[i] != ' ') i++;
    char save = cmd[i];
    cmd[i] = 0;
    g_copy(prog, cmd, sizeof prog);
    cmd[i] = save;
    g_copy(arg, save ? g_trim(cmd + i) : "", sizeof arg);

    /* If the child failed it has already said why, in its own words, on this
     * same console. Printing "init: /bin/rc: failed" over the top would bury
     * the only evidence the player gets, so exit quietly with its status. */
    i64 rc = g_spawn(prog, arg);
    g_exit(rc == 0 ? 0 : 1);
}
