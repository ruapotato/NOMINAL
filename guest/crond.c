/* /usr/sbin/crond — the cron daemon.
 *
 * Reads its configuration at startup and then stays running. If the config is
 * missing it says so and exits, which is a service failing to start rather
 * than a service that was never there -- a different fault with a different
 * fix.
 */
#include "gsys.h"
static char conf[2048];
/* Empty means "any line that is not a comment". crontab lines start with a
 * schedule, and requiring a literal "*" matched none of them -- so cron sat
 * in a respawn loop on every healthy machine, which a playtester spotted and
 * charitably called cosmetic. It was not. */
static const char *KEY = "";
void _start(void)
{
    static const char *CONF[] = { "/etc/crontab", 0 };
    for (int i = 0; CONF[i]; i++) {
        if (g_slurp(CONF[i], conf, sizeof conf) < 0) {
            g_puts("crond: ");
            g_puts(CONF[i]);
            g_putln(": cannot read -- refusing to start");
            g_exit(1);
        }
    }
    /* The config is there. Is it USABLE? A file that exists and does not say
     * the one thing this daemon needs is a completely different fault from a
     * file that is missing, and it fails later and less obviously. */
    {
        int ok = 0;
        char *q = conf;
        while (*q) {
            char *nl = q; while (*nl && *nl != '\n') nl++;
            char save = *nl; *nl = 0;
            char *t = g_trim(q);
            if (*t && *t != '#') {
                u64 k = 0;
                while (KEY[k] && t[k] == KEY[k]) k++;
                if (!KEY[k]) ok = 1;      /* empty KEY: any real line will do */
            }
            *nl = save; q = *nl ? nl + 1 : nl;
            if (ok) break;
        }
        if (!ok) {
            g_puts("crond: ");
            g_puts(CONF[0]);
            g_putln(": no jobs -- refusing to start");
            g_exit(1);
        }
    }

    for (;;) { }
}
