/* wreck.c — derelicts, and the foreign hardware aboard them.
 *
 * This is the loop the whole game is meant to hang off:
 *
 *   your scrubber is dying and you have N ticks of air
 *     -> you mount a wreck:  mount wreck-01 /n/wreck
 *     -> /n/wreck/dev is full of devices you do not recognise, reporting
 *        fields you do not understand, in units that are not yours
 *     -> you probe them until you work out which one is a scrubber
 *     -> you write a shim that translates it
 *     -> you bind it over /dev/scrubber
 *     -> and the watchdog you wrote three hours ago and have not touched since
 *        keeps working, with no idea it is now driving alien hardware
 *
 * The point of the foreign vocabulary is not to be obtuse. It is that the
 * translation layer is the interesting program to write, and that a namespace
 * is what makes the translation invisible to everything downstream.
 */
#include "nom.h"
#include <string.h>
#include <stdio.h>

/* Foreign devices report in their own units and their own words. */
enum {
    WD_THM = 900,     /* atmosphere plant: reports partial pressure in kPa */
    WD_PWR,           /* power tap: reports in "khl", 1 khl = 0.7 MW       */
    WD_IDENT,         /* the only thing that is legible at first glance    */
    WD_LOCK           /* refuses everything until you write the right word */
};

typedef struct {
    bool   unlocked;
    double kpa;        /* the plant's own idea of atmosphere */
    double khl;        /* available foreign power            */
    bool   running;
} Wreck;

static Wreck g_wreck = { false, 21.4, 6.2, false };

/* 21.4 kPa of O2 partial pressure is about 100% of a breathable atmosphere;
 * the player has to work that conversion out from the ident file and probing. */
double wreck_o2_percent(void)  { return g_wreck.running ? g_wreck.kpa / 0.214 : 0.0; }
bool   wreck_is_running(void)  { return g_wreck.running; }
bool   wreck_is_unlocked(void) { return g_wreck.unlocked; }

static IoStatus wreck_read(VNode *n, Buf *out, void *ctx)
{
    Sim *s = (Sim *)ctx;
    switch (n->id) {
    case WD_IDENT:
        buf_puts(out,
            "VESSEL   kel-morrin survey tender, hull 4471\n"
            "STATUS   adrift, atmosphere plant still turning\n"
            "NOTE     units are not yours. pressure is kPa. power is khl.\n"
            "NOTE     the plant answers to 'sequence' before it answers to anything else.\n");
        return IO_OK;

    case WD_LOCK:
        buf_printf(out, "sequence %s\n", g_wreck.unlocked ? "accepted" : "required");
        return IO_OK;

    case WD_THM:
        if (!g_wreck.unlocked) {
            snprintf(s->fs.err, sizeof s->fs.err, "thm-04: sequence required");
            return IO_ERR;
        }
        /* deliberately NOT the vocabulary your own devices use */
        buf_puts(out, "unit thm-04\n");
        buf_puts(out, "mode "); buf_puts(out, g_wreck.running ? "turning" : "idle"); buf_putc(out, '\n');
        buf_puts(out, "partial "); buf_putnum(out, g_wreck.kpa, 3); buf_puts(out, " kPa\n");
        buf_puts(out, "draw "); buf_putnum(out, 2.1, 2); buf_puts(out, " khl\n");
        return IO_OK;

    case WD_PWR:
        if (!g_wreck.unlocked) {
            snprintf(s->fs.err, sizeof s->fs.err, "aux-loop: sequence required");
            return IO_ERR;
        }
        buf_puts(out, "available "); buf_putnum(out, g_wreck.khl, 2); buf_puts(out, " khl\n");
        buf_puts(out, "note 1 khl is 0.7 of your MW\n");
        return IO_OK;
    }
    return IO_ERR;
}

static IoStatus wreck_write(VNode *n, const char *data, size_t len, void *ctx)
{
    Sim *s = (Sim *)ctx;
    char cmd[64];
    size_t l = len < sizeof cmd - 1 ? len : sizeof cmd - 1;
    memcpy(cmd, data, l); cmd[l] = 0;
    while (l && (cmd[l-1] == '\n' || cmd[l-1] == '\r' || cmd[l-1] == ' ')) cmd[--l] = 0;

    switch (n->id) {
    case WD_LOCK:
        /* The sequence is printed in the ident file of every kel-morrin hull.
         * Finding it is a `cat`, not a puzzle box. */
        if (strcmp(cmd, "kel-morrin") == 0) {
            g_wreck.unlocked = true;
            sim_log(s, "wreck: sequence accepted, foreign devices readable");
            return IO_OK;
        }
        snprintf(s->fs.err, sizeof s->fs.err, "sequence rejected");
        return IO_ERR;

    case WD_THM:
        if (!g_wreck.unlocked) { snprintf(s->fs.err, sizeof s->fs.err, "thm-04: sequence required"); return IO_ERR; }
        if (strcmp(cmd, "turn") == 0)  { g_wreck.running = true;  sim_log(s, "thm-04: plant turning"); return IO_OK; }
        if (strcmp(cmd, "still") == 0) { g_wreck.running = false; sim_log(s, "thm-04: plant still"); return IO_OK; }
        snprintf(s->fs.err, sizeof s->fs.err, "thm-04: accepts 'turn' or 'still'");
        return IO_ERR;
    }
    snprintf(s->fs.err, sizeof s->fs.err, "%s: read-only", n->name);
    return IO_ERR;
}

/* Mount a derelict's tree at `at`. In the fiction this is 9P over a docking
 * umbilical; mechanically it is the same thing — someone else's file server
 * appearing in your namespace. */
bool wreck_mount(Sim *s, const char *name, const char *at, char *err, size_t errsz)
{
    if (strcmp(name, "wreck-01") != 0 && strcmp(name, "kel-morrin") != 0) {
        snprintf(err, errsz, "%s: no such host in range (try 'wreck-01')", name);
        return false;
    }
    char base[NOM_PATH_MAX];
    snprintf(base, sizeof base, "%s", at);

    char p[NOM_PATH_MAX];
    snprintf(p, sizeof p, "%s/ident", base);
    vfs_mkdev(&s->fs, p, wreck_read, NULL, WD_IDENT);
    snprintf(p, sizeof p, "%s/dev/sequence", base);
    vfs_mkdev(&s->fs, p, wreck_read, wreck_write, WD_LOCK);
    snprintf(p, sizeof p, "%s/dev/thm-04", base);
    vfs_mkdev(&s->fs, p, wreck_read, wreck_write, WD_THM);
    snprintf(p, sizeof p, "%s/dev/aux-loop", base);
    vfs_mkdev(&s->fs, p, wreck_read, NULL, WD_PWR);

    sim_log(s, "mounted %s at %s", name, base);
    return true;
}

void wreck_reset(void)
{
    g_wreck.unlocked = false;
    g_wreck.running = false;
    g_wreck.kpa = 21.4;
    g_wreck.khl = 6.2;
}
