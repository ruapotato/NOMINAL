/* loadcheck.c — the calibration, run as `bf --loadcheck`.
 *
 * THE QUESTION THIS GATE ANSWERS, and it is the one the brief made the
 * deliverable: *"A naive build -- everything flat on one switch, one subnet,
 * cheap copper -- must start to feel slow around three floors and genuinely
 * break by about five. A thought-through build -- segmented per floor or per
 * tenant, a router doing real work, uplinks sized for what is behind them --
 * must carry substantially further. Measure this and print the numbers."*
 *
 * So it plays both, from the same seed, into the same building, with the
 * same tenants asking for the same work, and prints where each one falls
 * over. Nothing in here is a difficulty constant: the two builds differ only
 * in the topology the player would have typed, and every number printed was
 * counted off a port during a busy period that really happened.
 *
 * WHAT "FALLS OVER" MEANS. Fewer than four fifths of the building's people
 * got their day's work done inside the busy period. That is the same rule
 * site_day() uses to decide whether a tenant pays, so the gate and the game
 * are measuring one thing.
 *
 * IF THE NAIVE BUILD SURVIVES TO NINE FLOORS THE CURVE IS WRONG, and this
 * gate says so out loud and fails, rather than shipping a difficulty curve
 * that is not there.
 *
 * WHERE THE DEMAND CAME FROM, because it was recalibrated once and the
 * reason matters. Until the arithmetic in port_tx() was fixed, a port could
 * not read above about 39% busy however hard it was pushed, and the naive
 * tower fell over partly because of that -- the hottest port in the nine
 * tenancy run read 35% busy and had lost 8,929 frames, which are two numbers
 * that cannot both be true of the same wire. With the port counting
 * honestly, the same build carried all nine tenancies at 60% busy and 98% of
 * the work done: the curve was gone, and it had been resting on a bug.
 *
 * What brought it back was demand, and specifically CONCURRENCY rather than
 * size -- SITE_DESK_FILES in site.h, where the reasoning for the number is.
 * Bigger files were tried first and were wrong for a reason worth keeping:
 * one TCP flow across this stack carries about fifteen megabits, so a six
 * megabyte file cannot be pulled inside the four second window on an EMPTY
 * network, and the gate stopped measuring the building. Three transfers of a
 * document and a half, all open at once, is a desk and not a knob.
 *
 * AND THE FLOOR COUNT IS THIS BUILDING'S, NOT A TYPICAL ONE'S. The naive
 * build here stops finishing its work at 96 desks. This generator packs two
 * and three tenancies onto a floor, so 96 desks is three of ITS floors --
 * but it is five floors of the twenty-desk floor the target was said in.
 * Both readings are in the table below, which is why both columns are
 * printed.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "nom.h"
#include "site.h"
#include "session.h"

static int passed, total;

static void ck(const char *what, bool ok)
{
    total++;
    if (ok) passed++;
    printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
}

#define LOAD_SEED  7008ull
#define STEPS      9      /* tenancies to grow through                      */

/* ---------------------------------------------------------------- helpers */
/* Where a floor's kit goes: the comms cupboard if the building has one on
 * that floor, otherwise the riser, otherwise the tenant's own room. The
 * generator does not promise a cupboard on every floor and a player would
 * put the switch wherever there was somewhere to put it. */
static int comms_on(const Building *b, int floor, int fallback)
{
    int r = bld_find(b, floor, RM_COMMS);
    if (r < 0) r = bld_find(b, floor, RM_RISER);
    if (r < 0) r = fallback;
    return r;
}

static int put(Site *s, int kind, int room, const char *name)
{
    int d = site_install(s, kind, room, name);
    /* AND IT IS FED, refunded, for the reason gate_box() gives in
     * core/sitecheck.c: the calibration measures what a network carries, and
     * a curve that also had to design a power tree would be measuring two
     * things at once. site_feed() is the player's own call and takes the
     * player's own refusals. */
    if (d >= 0) {
        long money = s->money, spent = s->spent;
        /* A STRIP WHEN THE CORE RUNS OUT, AND A LOAD MOVED ONTO IT WHEN THE
         * CORE IS FULL OF LOADS -- the same three moves core/sitecheck.c's
         * gate_box() makes, for the same reason. Without the strip loop the
         * calibration's floor servers went unfed, every file request in the
         * building fell through to the internet, and the naive curve read 12%
         * of the work done at nine tenancies with uplink:0 at 98%. That is a
         * real tower with no power in it, not a network under load. */
        for (int tries = 0; tries < 12 && !site_dev_fed(s, d, NULL); tries++) {
            if (site_feed(s, d) >= 0) break;
            char sn[NET_NAME_MAX];
            snprintf(sn, sizeof sn, "ls%d", s->ndev);
            int st = site_install(s, SDEV_STRIP, room, sn);
            if (st < 0) break;
            if (site_feed(s, st) >= 0) continue;
            int moved = -1;
            for (int r = 0; r < site_conduit_count(s); r++) {
                const SiteConduit *c = &s->cond[r];
                if (!c->live || s->dev[c->from].kind != SDEV_POWERCORE) continue;
                if (s->dev[c->to].kind == SDEV_STRIP || c->to == s->ws) continue;
                moved = c->to;
                site_unconduit(s, r);
                break;
            }
            if (moved < 0) break;
            bool was_on = s->dev[moved].powered;
            if (site_feed(s, st) < 0) break;
            site_feed(s, moved);
            if (was_on) site_power(s, moved, true);
        }
        s->money = money;
        s->spent = spent;
    }
    return d;
}

/* Run days until this tenancy has moved in and has desks. */
static void keep_measuring(Site *s);
static void until_moved(Site *s, int ti)
{
    for (int guard = 0; guard < 400 && !s->tenant[ti].moved; guard++) {
        keep_measuring(s);
        site_day(s, NULL);
    }
}

/* THE CALIBRATION IS A MEASUREMENT, NOT A PLAYTHROUGH. A build that has
 * fallen over would have its lease ended by the third complaint, and then
 * there would be no numbers for the floors past it -- which are exactly the
 * numbers the brief asked for. So the harness pays the tenants' complaints
 * off between steps and keeps growing, and says so. Nothing else about the
 * day is touched: the frames, the drops and the work done are whatever they
 * were. The run-over rule itself is checked separately, in check_complaints,
 * where it is played rather than measured. */
static void keep_measuring(Site *s)
{
    s->over = 0;
    s->complaints = 0;
    for (int i = 0; i < s->ntenant; i++) {
        s->tenant[i].strikes = 0;
        s->tenant[i].complained = 0;
    }
}

/* One flat tower, built by the same lines in the same order every time, and
 * played for one day past the first tenancy's arrival. The determinism check
 * runs it twice and compares. */
static SiteDay flat_run(Building *b);

/* ------------------------------------------------------------ the numbers */
/* TENANCIES, AND THE FLOORS THEY ARE ON, WHICH ARE NOT THE SAME NUMBER.
 *
 * This column used to be headed `floors` and it counted tenancies -- one row
 * per tenancy that had moved in, twenty desks apiece. A building does not
 * work like that: the generator puts two and three tenancies on a floor, so
 * a player with three floors in service is five or six tenancies in, at a
 * hundred and twenty desks rather than fifty-six, and reads a row of this
 * table that is not theirs. That is the whole of the gap a playtester
 * measured -- the gate said 89% at three floors and they saw 63% -- and
 * nothing about the network was wrong. Both numbers are printed now. */
typedef struct {
    int  steps;           /* tenancies connected so far                     */
    int  floors;          /* distinct floors those tenancies are on         */
    int  desks;
    int  finished, sessions;
    int  pct;
    int  worst_ms;
    char hot[40];
    int  hot_util;
    unsigned long long drops;
} Step;

static void record(const Site *s, const SiteDay *r, int steps, Step *st)
{
    st->steps = steps;
    /* Counted, not assumed: which floors those tenancies are really on. */
    st->floors = 0;
    for (int f = 0; f < 32; f++) {
        for (int i = 0; i < s->ntenant; i++)
            if (s->tenant[i].moved && s->tenant[i].floor == f) { st->floors++; break; }
    }
    st->desks = r->connected;
    st->finished = r->finished;
    st->sessions = r->sessions;
    st->pct = r->sessions ? (r->finished * 100) / r->sessions : 0;
    st->worst_ms = r->worst_ms;
    snprintf(st->hot, sizeof st->hot, "%s", r->hot);
    st->hot_util = r->hot_util;
    st->drops = (unsigned long long)r->drops;
}

static void show(const char *what, const Step *st, int n)
{
    printf("\n%s\n", what);
    /* "work done" IS THE TOWER'S CARRIED TOTAL, WHICH IS NOT WHAT `service`
     * COUNTS, and the difference has already caused one confusion in the
     * played game: `status` printed SiteDay.sessions -- every unit of work
     * the tower carried -- while the rows printed each tenancy's own
     * tried/finished, and a voice agent's CRM traffic is in the first and not
     * the second. That was fixed for the player by making the headline the
     * literal sum of the rows.
     *
     * This table is deliberately NOT that. The curve it measures is whether
     * the BUILDING carried what was asked of it, which is the right question
     * for a load model and the one the owner's sentence -- slow at three
     * floors, breaking at five -- was calibrated against. Changing it to the
     * per-tenancy sum would move the curve, and the curve is the
     * specification.
     *
     * So the column keeps its meaning and stops being coy about it. A
     * developer reading this table and the game's own `status` should not
     * have to work out why two "work done" numbers differ. */
    printf("  tenancies  floors  desks   carried     slowest   busiest port"
           "        util   frames lost\n");
    for (int i = 0; i < n; i++) {
        if (!st[i].sessions) continue;
        printf("  %8d  %6d  %5d   %4d/%-4d %3d%%  %6dms   %-18s %4d%%  %11llu\n",
               st[i].steps, st[i].floors, st[i].desks, st[i].finished, st[i].sessions,
               st[i].pct, st[i].worst_ms, st[i].hot, st[i].hot_util, st[i].drops);
    }
}

/* The first tenancy count at which the building carried fewer than four
 * fifths of the units of work asked of it, or 0 if it never happened.
 *
 * "Carried", not "each tenancy's own" -- see the note on the column heading
 * in show(). This is the load question and it is what the owner's sentence
 * was calibrated against; a tenancy-weighted version would be a different
 * curve, not a more accurate one. */
static int broke_at(const Step *st, int n)
{
    for (int i = 0; i < n; i++)
        if (st[i].sessions && st[i].pct < 80) return st[i].steps;
    return 0;
}
/* And the first at which it is visibly not comfortable any more. */
static int slow_at(const Step *st, int n)
{
    for (int i = 0; i < n; i++)
        if (st[i].sessions && (st[i].pct < 97 || st[i].hot_util >= 80)) return st[i].steps;
    return 0;
}
/* How many floors that many tenancies were spread over, which is the number
 * a player has in front of them. */
static int floors_at(const Step *st, int n, int steps)
{
    for (int i = 0; i < n; i++) if (st[i].steps == steps) return st[i].floors;
    return 0;
}

/* ================================================= THE TOWER, AS IT IS PLAYED
 *
 * WHY THIS IS A Session AND NOT A Site. Both builds below used to be typed
 * straight onto a bare `Site`: kit appeared in the room it belonged in, a
 * server was "powered" by setting a flag, and `site_httpd` opened a socket
 * because the harness said so. Nothing in this file had ever booted an
 * operating system, and the played game boots one on every box with a
 * button. So the gate was measuring a network and the game runs operating
 * systems on it -- and the two faults that cost a playtester their whole
 * re-architecture window, a dhcpd swept away by a box reading its own config
 * file and a `policy drop` that ate the DISCOVERs, existed only once a
 * machine was booted and this gate could not see either of them.
 *
 * So it is a Session now: the same struct `--serve` and `--towersh` hand to
 * a player, driven with the words a player types. Kit is bought to goods in,
 * carried up the stairs, put down, cabled off a drum and switched on -- and
 * switching a server on installs and boots a real machine, whose httpd
 * answers because netd read /etc/net/interfaces off its own disk. Every
 * number in the table below was counted after that had really happened.
 */
typedef struct { Session ses; Buf o; } Play;

/* AND IT REALLY BOOTED. The whole point of playing this through a Session is
 * that a server in the calibration is a machine with a kernel in it, so the
 * gate says so out loud rather than assuming it: this is read off the boot
 * chain of the box the table's busiest port belongs to. */
static int servers_booted;
static void count_booted(Play *p)
{
    servers_booted = 0;
    for (int i = 0; i < p->ses.s.ndev; i++) {
        Machine *m = p->ses.mach[i];
        if (m && m->boot.running) servers_booted++;
    }
}

/* Feed anything with nothing feeding it: a strip when the core runs out of
 * ways out, and a load moved onto the strip when the core is full of loads.
 * The same three moves core/sitecheck.c's gate_box() makes. */
static void play_power(Play *p)
{
    Site *s = &p->ses.s;
    long money = s->money, spent = s->spent;
    for (int i = 0; i < s->ndev; i++) {
        int k = s->dev[i].kind;
        if (k == SDEV_UPLINK || k == SDEV_POWERCORE || k == SDEV_DESK ||
            k == SDEV_STRIP) continue;
        for (int tries = 0; tries < 12 && !site_dev_fed(s, i, NULL); tries++) {
            if (site_feed(s, i) >= 0) break;
            char sn[NET_NAME_MAX];
            snprintf(sn, sizeof sn, "ls%d", s->ndev);
            int st = site_install(s, SDEV_STRIP, s->dev[i].room, sn);
            if (st < 0) break;
            if (site_feed(s, st) >= 0) continue;
            int moved = -1;
            for (int r = 0; r < site_conduit_count(s); r++) {
                const SiteConduit *c = &s->cond[r];
                if (!c->live || s->dev[c->from].kind != SDEV_POWERCORE) continue;
                if (s->dev[c->to].kind == SDEV_STRIP || c->to == s->ws) continue;
                moved = c->to;
                site_unconduit(s, r);
                break;
            }
            if (moved < 0) break;
            bool was_on = s->dev[moved].powered;
            if (site_feed(s, st) < 0) break;
            site_feed(s, moved);
            if (was_on) site_power(s, moved, true);
        }
    }
    s->money = money;
    s->spent = spent;
}

static void say(Play *p, const char *fmt, ...)
{
    char line[NOM_ARG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    buf_clear(&p->o);
    session_line(&p->ses, line, &p->o);
    /* AND WHATEVER IS STANDING THERE UNFED GETS A RUN. The played towers in
     * this file are built by typing, and power comes down conduit now: without
     * this the naive build's floor servers were never fed, every file request
     * in the building fell through to the internet, and the curve read 12% of
     * the work done at nine tenancies with uplink:0 at 98% busy. That is a
     * tower with no power in it, not a network under load. Refunded, for the
     * reason gate_box() gives in core/sitecheck.c. */
    play_power(p);
}

/* Kit arrives at goods in and somebody carries it. Both hands: the drum goes
 * back on the shelf first, because a person holding a cable drum cannot pick
 * up a switch, and the session says so. */
static void deliver(Play *p, const char *kind, const char *name, int room)
{
    say(p, "spool back");
    say(p, "buy %s %s", kind, name);
    say(p, "go goods");
    say(p, "carry %s", name);
    say(p, "go #%d", room);
    say(p, "drop");
}

/* Floors come into service in order, they cost the landlord's fit-out, and
 * the lift button for a floor nobody has opened is not lit -- so this is the
 * stairs, which is what a player does. */
static void open_to(Play *p, int floor)
{
    while (p->ses.floors <= floor && p->ses.floors < p->ses.b.floors) {
        int f = p->ses.floors;
        int up = bld_find(&p->ses.b, f, RM_STAIR);
        if (up < 0) up = bld_find(&p->ses.b, f, RM_LIFTLOBBY);
        if (up < 0) break;
        say(p, "go #%d", up);
        say(p, "open");
        if (p->ses.floors == f) break;          /* it would not open         */
    }
}

static void play_day(Play *p)
{
    keep_measuring(&p->ses.s);
    say(p, "day 1");
}

static void until_moved_play(Play *p, int ti)
{
    for (int guard = 0; guard < 400 && !p->ses.s.tenant[ti].moved; guard++)
        play_day(p);
}

static bool begin(Play *p, Building *b)
{
    (void)b;
    memset(p, 0, sizeof *p);
    if (!session_start(&p->ses, LOAD_SEED, 60000)) return false;
    say(p, "credit 900000");            /* the gate is about the network     */
    return true;
}

static void finish(Play *p)
{
    buf_free(&p->o);
    session_end(&p->ses);
}

/* ====================================================================== the
 * NAIVE BUILD, and it is not "no server anywhere". That was what this gate
 * used to build, and a playtester quite reasonably pointed out that nobody
 * plays it: the tenants ask for a server in as many words, so the player
 * buys one, and they put it where they have been putting everything else --
 * in the basement, next to the core switch, because that is where the rack
 * is and it is one cable.
 *
 * So: one flat 10.0.0.0/16, DHCP off the router, a switch per floor
 * home-run to the core on the cheapest drum, another switch daisy-chained
 * off that one when a floor runs out of holes, and one file server in the
 * MDF holding everybody's files. Every line of it is a line the player would
 * really type, and none of it is wrong on its own. It is wrong together.
 */
static void naive(Building *b, Step *st)
{
    Play p;
    if (!begin(&p, b)) return;
    Site *s = &p.ses.s;
    int mdf = bld_find(&p.ses.b, 0, RM_MDF);

    deliver(&p, "router", "edge", mdf);
    deliver(&p, "switch24", "core", mdf);
    deliver(&p, "server", "files", mdf);
    say(&p, "cable edge:0 uplink:0 cat5e");
    say(&p, "cable edge:1 core:0 cat5e");
    say(&p, "go edge");
    say(&p, "addr edge:0 198.51.100.2/30");
    say(&p, "addr edge:1 10.0.0.1/16");
    say(&p, "gw edge 198.51.100.1");
    say(&p, "router edge on");
    say(&p, "dhcpd edge 10.0.1.1 200 16 10.0.0.1 198.51.100.1");
    say(&p, "cable core:23 files:0 cat5e");
    say(&p, "go files");
    say(&p, "power files on");           /* a real machine, really booting   */
    say(&p, "addr files 10.0.0.9/16");
    say(&p, "gw files 10.0.0.1");
    say(&p, "httpd files");
    say(&p, "ups files");

    int next_core_port = 1, nsw = 0;
    int floor_sw[32], floor_free[32];
    for (int i = 0; i < 32; i++) { floor_sw[i] = -1; floor_free[i] = 0; }

    for (int i = 0; i < STEPS; i++) {
        until_moved_play(&p, i);
        int f = s->tenant[i].floor;
        if (f < 0 || f >= 32) break;
        open_to(&p, f);
        int room = comms_on(&p.ses.b, f, s->tenant[i].room);
        char nm[NET_NAME_MAX];
        if (floor_sw[f] < 0 || floor_free[f] < s->tenant[i].ndesk) {
            bool first = floor_sw[f] < 0;
            char prev[NET_NAME_MAX];
            snprintf(prev, sizeof prev, "sw%d", floor_sw[f] < 0 ? 0 : floor_sw[f]);
            snprintf(nm, sizeof nm, "sw%d", ++nsw);
            deliver(&p, "switch24", nm, room);
            if (first) say(&p, "cable core:%d %s:0 cat5e", next_core_port++, nm);
            /* THE ORDINARY MISTAKE. The floor is full, so the new switch goes
             * in beside the old one and takes its feed from it. It works, and
             * everything behind both of them now shares one riser. */
            else say(&p, "cable %s:23 %s:0 cat5e", prev, nm);
            floor_sw[f] = nsw;
            floor_free[f] = 23;
        }
        snprintf(nm, sizeof nm, "sw%d", floor_sw[f]);
        say(&p, "go %s", nm);
        say(&p, "serve %d %s cat5e", s->tenant[i].tenant, nm);
        floor_free[f] -= s->tenant[i].ndesk;
        play_day(&p);
        record(s, &s->last, i + 1, &st[i]);
    }
    count_booted(&p);
    finish(&p);
}

/* ====================================================================== the
 * THOUGHT-THROUGH BUILD. The same tenants, the same building, the same
 * money, the same verbs. A vlan per floor terminated on a subinterface of
 * the router down one trunk, so a broadcast on floor four is not floor
 * one's problem. Fibre from the MDF to each floor, because an uplink is
 * sized for what is behind it. And a server in each floor's own comms
 * cupboard doing that floor's DHCP and holding that floor's files, so the
 * traffic between a desk and the thing it is opening never leaves the
 * switch it is plugged into.
 *
 * Nobody wrote down that this would be better. It is better because the
 * frames go somewhere else.
 */
static void planned(Building *b, Step *st)
{
    Play p;
    if (!begin(&p, b)) return;
    Site *s = &p.ses.s;
    int mdf = bld_find(&p.ses.b, 0, RM_MDF);

    deliver(&p, "router", "edge", mdf);
    deliver(&p, "switch24", "core", mdf);
    say(&p, "cable edge:0 uplink:0 cat5e");
    say(&p, "cable edge:1 core:0 fibre");
    say(&p, "go edge");
    say(&p, "addr edge:0 198.51.100.2/30");
    say(&p, "gw edge 198.51.100.1");
    say(&p, "router edge on");
    say(&p, "go core");
    say(&p, "trunk core 0 0");

    int next_core_port = 1, nsw = 0;
    int floor_sw[32], floor_free[32], floor_srv[32];
    for (int i = 0; i < 32; i++) { floor_sw[i] = -1; floor_free[i] = 0; floor_srv[i] = 0; }

    for (int i = 0; i < STEPS; i++) {
        until_moved_play(&p, i);
        int f = s->tenant[i].floor;
        if (f < 0 || f >= 32) break;
        open_to(&p, f);
        int vlan = 10 + f;
        int room = comms_on(&p.ses.b, f, s->tenant[i].room);
        char nm[NET_NAME_MAX];
        if (floor_sw[f] < 0 || floor_free[f] < s->tenant[i].ndesk) {
            bool first = floor_sw[f] < 0;
            snprintf(nm, sizeof nm, "sw%d", ++nsw);
            deliver(&p, "switch24", nm, room);
            int cp = next_core_port++;
            /* Home-run, on fibre, every time: a floor's second switch has a
             * floor's second riser, not a share of the first one's. */
            say(&p, "cable core:%d %s:0 fibre", cp, nm);
            if (first) {
                say(&p, "go edge");
                say(&p, "subif edge 1 %d 10.%d.0.1/24", vlan, vlan);
            }
            say(&p, "go core");
            say(&p, "trunk core 0 %d", vlan);
            say(&p, "trunk core %d %d", cp, vlan);
            say(&p, "go %s", nm);
            say(&p, "trunk %s 0 %d", nm, vlan);
            for (int q = 1; q < site_kind_ports(SDEV_SWITCH24); q++)
                say(&p, "vlan %s %d %d", nm, q, vlan);
            floor_sw[f] = nsw;
            floor_free[f] = 23;
            if (!floor_srv[f]) {
                /* THE FLOOR'S OWN SERVER, in the floor's own cupboard, doing
                 * the floor's DHCP and holding the floor's files. It is a
                 * real machine: `power` installs it and boots it, netd reads
                 * the address off its own disk, and its httpd answers
                 * because the service is running -- not because the harness
                 * opened a socket on its behalf. */
                char sn[NET_NAME_MAX];
                snprintf(sn, sizeof sn, "srv%d", f);
                deliver(&p, "server", sn, room);
                say(&p, "cable %s:1 %s:0 cat5e", nm, sn);
                say(&p, "go %s", nm);
                say(&p, "vlan %s 1 %d", nm, vlan);
                say(&p, "go %s", sn);
                say(&p, "power %s on", sn);
                say(&p, "addr %s 10.%d.0.2/24", sn, vlan);
                say(&p, "gw %s 10.%d.0.1", sn, vlan);
                say(&p, "dhcpd %s 10.%d.0.10 200 24 10.%d.0.1 198.51.100.1",
                    sn, vlan, vlan);
                say(&p, "httpd %s", sn);
                /* AND A BATTERY UNDER IT, which is part of the build and not
                 * part of the harness: growing to nine tenancies is months of
                 * game days and the building loses the mains two or three
                 * times on the way. A server that is off because the lights
                 * went out in week five is not a fact about the topology. */
                say(&p, "ups %s", sn);
                floor_srv[f] = 1;
                floor_free[f] = 22;
            }
        }
        snprintf(nm, sizeof nm, "sw%d", floor_sw[f]);
        say(&p, "go %s", nm);
        say(&p, "serve %d %s cat5e %d", s->tenant[i].tenant, nm, vlan);
        floor_free[f] -= s->tenant[i].ndesk;
        play_day(&p);
        record(s, &s->last, i + 1, &st[i]);
    }
    count_booted(&p);
    finish(&p);
}

/* ============================================== the assertions on the loop
 * Everything the brief asked --loadcheck to prove, each of it done rather
 * than described. These run on small worlds so the gate stays quick; the
 * calibration above is the slow half and it runs once.
 */
static void check_clock(Building *b)
{
    printf("the clock, and the rent\n");
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    ck("a site starts on day zero with nobody in", s.day == 0 && s.ntenant > 0);
    int first = s.tenant[0].day;
    site_advance(&s, first - 1, NULL);
    ck("nobody has moved in the day before the first tenancy arrives",
       s.day == first - 1 && !s.tenant[0].moved);
    site_day(&s, NULL);
    ck("and on their day they are in, with a desk for every drop",
       s.tenant[0].moved && s.tenant[0].ndesk == s.tenant[0].drops);
    ck("their desks have cards and nothing plugged into them",
       site_tenant_connected(&s, 0) == 0);

    /* Build them something. */
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);

    long before = s.money;
    site_day(&s, NULL);
    ck("a tenancy with no port pays nothing and does not complain",
       s.money == before && s.tenant[0].complained == 0);

    int sw = put(&s, SDEV_SWITCH24, comms_on(b, s.tenant[0].floor, s.tenant[0].room), "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5E);
    int got = site_serve(&s, 0, sw, CAB_CAT5E);
    ck("copper to a tenancy's desks connects as many as the switch has holes",
       got > 0 && got <= site_kind_ports(SDEV_SWITCH24) - 1);
    ck("and every metre of it was charged for", s.money < before);

    /* Without a DHCP server the desks have cards and no addresses. */
    before = s.money;
    site_day(&s, NULL);
    ck("desks with nowhere to get an address get none",
       site_tenant_addressed(&s, 0) == 0);
    ck("and a tenancy whose people cannot work pays no rent", s.money == before);

    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    before = s.money;
    SiteDay r;
    site_day(&s, &r);
    ck("with a server on the segment they ask for one and get one",
       site_tenant_addressed(&s, 0) > 0);
    ck("their people really use the network: frames, not a counter",
       r.frames > 1000 && r.bytes > 1024 * 1024);
    ck("the work finishes and the rent arrives", r.finished > 0 && s.money > before);
    ck("rent is a thirtieth of a month, for the day it worked",
       r.rent == s.tenant[0].rent / 30);

    site_free(&s);

    /* DETERMINISM. The same seed, built by the same lines in the same order,
     * must move the same frames on the same day -- and it is compared by
     * playing it twice rather than by trusting that it would. */
    SiteDay a = flat_run(b), c = flat_run(b);
    ck("the same seed on the same day moves the same number of frames",
       a.frames == c.frames && a.bytes == c.bytes && a.frames > 0);
    ck("and the same work finishes, and the same rent arrives",
       a.finished == c.finished && a.sessions == c.sessions && a.rent == c.rent);
}

/* A link asked for more than it can carry, on purpose, with the smallest
 * world that does it: two desks' worth of traffic down a hundred megabit
 * run. The drop must land on the port, the latency must rise, and both must
 * be readable with the tools that already exist. */
static void check_capacity(Building *b)
{
    printf("\nload that hurts honestly\n");
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);

    until_moved(&s, 1);
    int room = comms_on(b, s.tenant[0].floor, s.tenant[0].room);
    /* THE CHEAP DRUM. Two tenancies -- thirty-eight desks -- behind one
     * hundred megabit run, which is the cheapest line in the catalogue and
     * the most ordinary mistake there is. */
    int sw = put(&s, SDEV_SWITCH24, room, "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5);
    site_serve(&s, 0, sw, CAB_CAT5E);
    site_serve(&s, 1, sw, CAB_CAT5E);

    /* A quiet baseline, from a desk, before anybody has done any work: this
     * is what the wire does when nothing else is on it. */
    int quiet = 0, probe_desk = s.tenant[0].desk0 + 1;
    for (int i = 0; i < s.tenant[0].ndesk; i++)
        net_dhcp_client(s.net, s.dev[s.tenant[0].desk0 + i].node, 0);
    /* Twice: the first one pays for an ARP round trip and would be
     * measuring the resolution, not the wire. */
    net_ping(s.net, s.dev[probe_desk].node, net_ip(10, 0, 0, 1), &quiet);
    net_ping(s.net, s.dev[probe_desk].node, net_ip(10, 0, 0, 1), &quiet);
    uint64_t d0 = net_port_drops(s.net, s.dev[core].node, 1);
    uint64_t q0 = net_port_qdrops(s.net, s.dev[core].node, 1);

    SiteDay r;
    site_day(&s, &r);
    site_day(&s, &r);
    uint64_t d1 = net_port_drops(s.net, s.dev[core].node, 1);
    uint64_t q1 = net_port_qdrops(s.net, s.dev[core].node, 1);
    int util = (int)((net_port_busy_us(s.net, s.dev[core].node, 1) * 100)
                     / (SITE_BUSY_MS * 1000ull));

    /* Eighty-odd per cent and not a hundred, because a link that is losing
     * frames is a link whose senders have backed off: TCP never gets to sit
     * at line rate once it is dropping, and a gate that demanded a hundred
     * would be demanding the stack lie. */
    char line[96];
    snprintf(line, sizeof line,
             "a hundred megabit run under two floors of desks fills up (%d%%)", util);
    ck(line, util >= 75);
    ck("and the port it fills up on is the one that drops", d1 > d0);
    ck("and the drops are the egress buffer, counted as such", q1 > q0);
    /* The evidence, in the words a player reads. */
    Buf ports = {0};
    net_dump_ports_used(s.net, s.dev[core].node, &ports);
    ck("`show` on that box says so, with the reason in words",
       ports.p && strstr(ports.p, "egress buffer full") != NULL);
    buf_free(&ports);

    /* LATENCY, MEASURED. A ping across the full link while it is loaded is
     * slower than the same ping on the same wire when it is not, because the
     * echo really is behind other people's frames. */
    ck("a ping on a wire that is not congested is quick", quiet <= 8);

    /* LATENCY, MEASURED, WITH THE TOOL THE PLAYER HAS. The same echo across
     * the same wire, sent while the port has other people's frames in front
     * of it, comes back later -- because it really is behind them. Nothing
     * adds a penalty; the frame is queued and the queue has a length. */
    {
        /* Fill the hundred megabit run the way the tenants fill it: several
         * desks pulling a file at once. Then ping across it. */
        int rtt = 0;
        uint64_t q = 0;
        int probe[8];
        const char *req = "GET /n/1024 HTTP/1.0\r\nHost: f\r\n\r\n";
        /* A TEN MEGABIT CIRCUIT, which the landlord could really have
         * bought, with eight desks pulling a megabyte each down it. The
         * handoff's egress buffer fills and stays full, and an echo behind
         * it waits exactly as long as the frames in front of it take. */
        site_isp(&s, 10);
        int quiet_isp = 0;
        net_ping(s.net, s.dev[probe_desk].node, s.wan_isp, &quiet_isp);
        net_ping(s.net, s.dev[probe_desk].node, s.wan_isp, &quiet_isp);
        for (int i = 0; i < 8; i++)
            probe[i] = net_tcp_connect(s.net, s.dev[s.tenant[0].desk0 + 2 + i].node,
                                       net_ip(198, 51, 100, 1), 80);
        for (int t = 0; t < 3000; t++) {
            net_step(s.net, 1);
            for (int i = 0; i < 8; i++) {
                if (probe[i] < 0) continue;
                if (net_tcp_state(s.net, probe[i]) == TCP_ESTABLISHED) {
                    uint8_t bb[1024];
                    if (net_tcp_recv(s.net, probe[i], bb, sizeof bb) <= 0)
                        net_tcp_send(s.net, probe[i], req, (int)strlen(req));
                    while (net_tcp_recv(s.net, probe[i], bb, sizeof bb) > 0) { }
                }
            }
            q = net_port_queue_us(s.net, s.dev[s.uplink].node, 0);
            if (q > 15000) break;
        }
        quiet = quiet_isp;
        net_ping(s.net, s.dev[probe_desk].node, s.wan_isp, &rtt);
        for (int i = 0; i < 8; i++) if (probe[i] >= 0) net_tcp_close(s.net, probe[i]);
        char l2[110];
        snprintf(l2, sizeof l2, "and the same ping with %llums of queue in front of "
                 "it takes longer (%dms vs %dms)",
                 (unsigned long long)(q / 1000), rtt, quiet);
        ck(l2, rtt > quiet);
    }

    /* Take the load away and the same ping is quick again. */
    for (int i = 0; i < s.tenant[0].ndesk; i++)
        net_port_admin(s.net, s.dev[s.tenant[0].desk0 + i].node, 0, false);
    site_day(&s, &r);
    int calm = 0;
    net_ping(s.net, s.dev[sw].node, net_ip(10, 0, 0, 1), &calm);
    ck("with the desks unplugged the same wire is quick again", calm <= 4);
    ck("and the port counter still remembers what it threw away",
       net_port_qdrops(s.net, s.dev[core].node, 1) >= q1);
    site_free(&s);
}

/* A tenancy that is being starved complains, and one that is not does not. */
static void check_complaints(Building *b)
{
    printf("\ncomplaints, and when they are not filed\n");
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    until_moved(&s, 0);
    int sw = put(&s, SDEV_SWITCH24, comms_on(b, s.tenant[0].floor, s.tenant[0].room), "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5E);
    site_serve(&s, 0, sw, CAB_CAT5E);

    site_advance(&s, 6, NULL);
    ck("a tenancy that is being served files nothing",
       s.complaints == 0 && s.tenant[0].strikes == 0);
    ck("and pays every day it works", s.rent_taken > 0);

    /* NOW STARVE THEM, and starve them with a decision rather than a flag:
     * the landlord downgrades the circuit to ten megabits. Twenty desks
     * asking for a file each is several times that, so the same people doing
     * the same work stop finishing it -- and every frame that goes missing
     * goes missing on the handoff's port, where `show uplink` counts it. The
     * handoff has no shell in it, so `netstat -P` is not the tool for that
     * port and the game must not say it is. */
    site_isp(&s, 10);
    site_day(&s, NULL);
    ck("one bad day is not a complaint", s.complaints == 0 && s.tenant[0].strikes == 1);
    site_day(&s, NULL);
    ck("nor two", s.complaints == 0 && s.tenant[0].strikes == 2);
    site_day(&s, NULL);
    ck("three days in a row is", s.complaints == 1 && s.tenant[0].complained);
    site_free(&s);
}

/* The flat tower the determinism check plays twice. Every line is one the
 * player would type, in the order they would type it. */
static SiteDay flat_run(Building *b)
{
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    until_moved(&s, 0);
    int sw = put(&s, SDEV_SWITCH24,
                 comms_on(b, s.tenant[0].floor, s.tenant[0].room), "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5E);
    site_serve(&s, 0, sw, CAB_CAT5E);
    SiteDay r;
    site_day(&s, &r);
    site_free(&s);
    return r;
}

/* ===================================================================== main */
int load_selfcheck(void)
{
    Building b;
    if (!bld_generate(&b, LOAD_SEED)) {
        printf("seed %llu makes no building\n", (unsigned long long)LOAD_SEED);
        return 1;
    }
    passed = total = 0;

    check_clock(&b);
    check_capacity(&b);
    check_complaints(&b);

    /* ------------------------------------------------------ the calibration */
    printf("\nTHE CALIBRATION. The same building, the same tenants, the same\n"
           "work, built two ways. Every number below was counted off a port.\n");
    Step nv[STEPS], pl[STEPS];
    memset(nv, 0, sizeof nv);
    memset(pl, 0, sizeof pl);
    naive(&b, nv);
    planned(&b, pl);
    show("NAIVE: one flat 10.0.0.0/16, cheap copper, DHCP off the router, a\n"
         "switch per floor with a second one daisy-chained off it when the\n"
         "floor fills up, and one file server in the basement holding\n"
         "everybody's files.", nv, STEPS);
    show("PLANNED: a vlan per floor down one trunk, fibre to every floor switch,\n"
         "and a server in each floor's own cupboard holding that floor's files\n"
         "and doing its DHCP.", pl, STEPS);

    int nb = broke_at(nv, STEPS), ns = slow_at(nv, STEPS);
    int pb = broke_at(pl, STEPS), grown = 0;
    for (int i = 0; i < STEPS; i++) if (nv[i].sessions) grown = nv[i].steps;
    if (ns) printf("\nthe naive build is visibly working hard at %d tenancies, "
                   "which is %d floor%s",
                   ns, floors_at(nv, STEPS, ns),
                   floors_at(nv, STEPS, ns) == 1 ? "" : "s");
    else printf("\nthe naive build never even works hard in %d tenancies", grown);
    if (nb) printf(", and falls over at %d (%d floors).\n",
                   nb, floors_at(nv, STEPS, nb));
    else printf(", and never falls over in %d.\n", grown);
    if (pb) printf("the planned build falls over at %d tenancies.\n", pb);
    else printf("the planned build carries all %d tenancies it was grown to, "
                "on %d floors.\n", grown, floors_at(pl, STEPS, grown));
    /* SAY IT, BECAUSE A PLAYER READS THIS TABLE BY THE WRONG ROW OTHERWISE.
     * A floor of this building holds two and three tenancies, so a tower with
     * three floors in service is well down the table, not on its third row. */
    printf("a floor holds more than one tenancy, so a tower with three floors\n"
           "in service is further down this table than its third row.\n");
    printf("the same tenants, the same money, the same building: the difference\n"
           "is where the frames go.\n\n");

    /* THE GATE ON THE CURVE ITSELF, AND IT IS COUNTED IN FLOORS.
     *
     * The target was always said in floors -- *"slow around three floors,
     * outright break at five"* -- and this gate used to assert it in
     * tenancies, which is a different number in every building the generator
     * makes. A floor here holds two and three tenancies. So the assertions
     * ask the question the target asked. */
    int nbf = floors_at(nv, STEPS, nb), nsf = floors_at(nv, STEPS, ns);
    int pbf = floors_at(pl, STEPS, pb);
    ck("and every server in it had an operating system running on it",
       servers_booted > 0);
    ck("a naive build is comfortable on its first floor",
       nv[0].sessions && nv[0].pct >= 95);
    ck("a naive build is visibly working hard by three floors",
       ns > 0 && nsf <= 3);
    ck("a naive build has fallen over by five floors",
       nb > 0 && nbf <= 5);
    ck("and it did not fall over on the second floor, which would be a different game",
       nb > 0 && nbf >= 3);
    ck("a planned build carries the floors the naive one could not",
       pb == 0 || pbf > nbf);
    /* And it is not carrying them by doing less work. */
    int nvd = 0, pld = 0;
    for (int i = 0; i < STEPS; i++) { nvd += nv[i].desks; pld += pl[i].desks; }
    ck("with the same desks doing the same work", pld >= nvd);

    bld_free(&b);
    printf("\n%d/%d load checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
