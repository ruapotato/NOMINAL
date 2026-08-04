/* bfstub.c — TEMPORARY BRIDGE.
 *
 * natives.c still references two symbols from the station simulation that D17
 * deletes: sim_log() and wreck_mount(). The break-fix build does not link the
 * station, so these stand in until that code is removed for real. When the
 * station goes, delete this file — if it is still here and the station is
 * gone, something is calling into a game that no longer exists.
 */
#include <stdarg.h>
#include <stdio.h>
#include "nom.h"

void sim_log(Sim *s, const char *fmt, ...) { (void)s; (void)fmt; }

bool wreck_mount(Sim *s, const char *host, const char *at, char *err, size_t errsz)
{
    (void)s; (void)host; (void)at;
    snprintf(err, errsz, "no such file server");
    return false;
}
