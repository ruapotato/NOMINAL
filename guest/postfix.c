/* /usr/sbin/postfix — the mail daemon.
 *
 * Reads its configuration at startup and then stays running. If the config is
 * missing it says so and exits, which is a service failing to start rather
 * than a service that was never there -- a different fault with a different
 * fix.
 */
#include "gsys.h"
static char conf[2048];
void _start(void)
{
    static const char *CONF[] = { "/etc/postfix/main.cf", 0 };
    for (int i = 0; CONF[i]; i++) {
        if (g_slurp(CONF[i], conf, sizeof conf) < 0) {
            g_puts("postfix: ");
            g_puts(CONF[i]);
            g_putln(": cannot read -- refusing to start");
            g_exit(1);
        }
    }
    for (;;) { }
}
