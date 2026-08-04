/* /bin/grep — find a substring in a file. Plain text, no regex: the job here
 * is finding a line in a config, not parsing a language. */
#include "gsys.h"
static char arg[256], buf[65536], line[1024];
void _start(void){
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    if (g_argv(arg, v) < 2) { g_putln("usage: grep <text> <file>"); g_exit(1); }
    i64 n = g_slurp(v[1], buf, sizeof buf);
    if (n < 0) { g_puts("grep: "); g_puts(v[1]); g_putln(": cannot read"); g_exit(1); }
    u64 pl = g_strlen(v[0]);
    int hits = 0;
    char *p = buf;
    while (*p) {
        char *nl = p; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        g_copy(line, p, sizeof line);
        *nl = save; p = *nl ? nl + 1 : nl;
        for (u64 i = 0; line[i]; i++) {
            u64 k = 0;
            while (k < pl && line[i+k] == v[0][k]) k++;
            if (k == pl) { g_putln(line); hits++; break; }
        }
    }
    g_exit(hits ? 0 : 1);
}
