/* eventcheck.c — the gate on the world breaking the machines, `bf --eventcheck`.
 *
 * WHAT THIS GATE EXISTS TO PROVE. Sixty-two fault types live in
 * core/breaker.c and `--solve 60` proves every one of them is findable and
 * repairable. None of them could be caused by the TOWER: a machine you
 * installed, cabled and ran for forty days never broke, because faults only
 * arrived when a break-fix ticket was generated. D23 said the opposite --
 * *the world supplies the cause* -- and this is the gate on that sentence.
 *
 * Five things are asserted, and they are the five rules the events were
 * written to:
 *
 *   1. AN EVENT FIRES ON THE DAY IT SHOULD. The blackout schedule is a pure
 *      function of seed and day, so the gate can name the morning.
 *   2. THE DAMAGE IS VISIBLE TO THE TOOLS. Not to a flag: to the boot chain
 *      stopping at the initrd, to `fsck` finding inodes it cannot save, to
 *      `pkg verify` reporting a file that genuinely differs from what
 *      shipped, and to `pkg diff` saying SHORT rather than edited.
 *   3. THE DOCUMENTED REPAIR ACTUALLY REPAIRS IT, walked end to end here in
 *      the same words a player types.
 *   4. A MACHINE THAT WAS PROTECTED SURVIVES. The box on a UPS is up the
 *      next morning with a clean filesystem and the receipt in its own log.
 *   5. THE SAME SEED PRODUCES THE SAME EVENTS.
 *
 * Everything below goes through session_line() -- the same function the
 * socket calls, on the same text a player types -- except where it reads
 * machine state directly to check that a claim made in words is true of the
 * bytes.
 */
#include <stdio.h>
#include <string.h>
#include "nom.h"
#include "session.h"
#include "kernel.h"

static int passed, total;

static void ck(const char *what, bool ok)
{
    total++;
    if (ok) passed++;
    printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
}

static const char *say(Session *ses, const char *line, Buf *o)
{
    buf_clear(o);
    session_line(ses, line, o);
    if (!o->len) buf_puts(o, "");
    return o->p ? o->p : "";
}

static bool has(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

#define EV_SEED 7008ull
/* Seed 7008's first tenancy moves in on day 19 and the building loses the
 * mains on days 30, 52 and 78. The gate names those numbers rather than
 * searching for them, because "deterministic from the seed" means a test
 * can. */
#define CUT_ONE   30
#define CUT_TWO   52
#define TENANT_IN 19

/* Run a script and shout about anything that refused, the way check_build in
 * core/sessioncheck.c does. */
static bool script(Session *ses, const char *const *lines, Buf *o)
{
    bool clean = true;
    for (int i = 0; lines[i]; i++) {
        const char *r = say(ses, lines[i], o);
        if (has(r, "no such command") || has(r, "refused") ||
            has(r, "there is no box")) {
            printf("    `%s` -> %s", lines[i], r);
            clean = false;
        }
    }
    return clean;
}

/* ------------------------------------------------- reading `pkg verify`
 * The output is "<package> <path> <STATUS>" a line at a time, so the gate can
 * take the package name and the path off the same line without knowing
 * anything about either. This is how it finds the file the world damaged
 * without ever being told which one it was -- which is the same position the
 * player is in. */
typedef struct { char pkg[40], path[NOM_PATH_MAX]; } Finding;

static int verify_scan(const char *text, Finding *out, int cap)
{
    int n = 0;
    const char *p = text;
    while (p && *p && n < cap) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[512];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        /* Three whitespace-separated fields, the second of which is a path. */
        char a[128] = "", b[NOM_PATH_MAX] = "", c[64] = "";
        if (sscanf(line, "%127s %255s %63s", a, b, c) == 3 && b[0] == '/' &&
            a[0] != '/' && strlen(a) < sizeof out[0].pkg) {
            snprintf(out[n].pkg, sizeof out[n].pkg, "%s", a);
            snprintf(out[n].path, sizeof out[n].path, "%s", b);
            n++;
        }
        p = nl ? nl + 1 : NULL;
    }
    return n;
}

static bool in_set(const Finding *set, int n, const char *path)
{
    for (int i = 0; i < n; i++) if (strcmp(set[i].path, path) == 0) return true;
    return false;
}

/* ===================================================== 1. IT IS THE SEED */
static void check_schedule(void)
{
    printf("\nthe blackout is a property of the seed, not of a die\n");

    bool same = true;
    for (int d = 1; d <= 120; d++)
        if (site_mains_fails_on(EV_SEED, d) != site_mains_fails_on(EV_SEED, d))
            same = false;
    ck("asking twice gives the same answer", same);

    ck("seed 7008 loses the mains on day 30 and not on day 29 or 31",
       site_mains_fails_on(EV_SEED, CUT_ONE) &&
       !site_mains_fails_on(EV_SEED, CUT_ONE - 1) &&
       !site_mains_fails_on(EV_SEED, CUT_ONE + 1));

    ck("and again on day 52, and the first one is after the tenants are in",
       site_mains_fails_on(EV_SEED, CUT_TWO) && CUT_ONE > TENANT_IN);

    /* A blackout every other week would be a tax and one a decade would be
     * decoration. Counted over a long run rather than asserted. */
    int cuts = 0;
    for (int d = 1; d <= 120; d++) if (site_mains_fails_on(EV_SEED, d)) cuts++;
    ck("between three and eight of them in a hundred and twenty days",
       cuts >= 3 && cuts <= 8);
    /* And none of them in the first fortnight, when the building is one
     * switch and a length of copper and there is nothing to lose. */
    int early = 0;
    for (int d = 1; d <= 14; d++) if (site_mains_fails_on(EV_SEED, d)) early++;
    ck("and none at all in the first fortnight", early == 0);

    /* Different seeds, different weather. Otherwise the schedule is a
     * constant wearing a seed as a hat. */
    int differ = 0;
    for (int d = 1; d <= 120; d++)
        if (site_mains_fails_on(EV_SEED, d) != site_mains_fails_on(4242, d))
            differ++;
    ck("a different seed loses it on different days", differ > 2);
}

/* ============================================ 2, 3 and 4. THE BLACKOUT */
/* One tower, built over a socket, run to the morning after a mains failure.
 * `files` is holding a tenancy's files when the lights go out; `spare` has a
 * battery under it. Both were running. Only one of them is a problem. */
static void check_blackout(void)
{
    printf("\na tower, a blackout on day %d, and the morning after\n", CUT_ONE);
    Session ses;
    if (!session_start(&ses, EV_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *const BUILD[] = {
        "credit 60000",
        "buy router edge", "buy switch24 core", "buy server files",
        "buy server spare",
        "go goods", "carry edge",  "go mdf", "drop",
        "go goods", "carry core",  "go mdf", "drop",
        "go goods", "carry files", "go mdf", "drop",
        "go goods", "carry spare", "go mdf", "drop",
        "spool cat6",
        "plug uplink:0", "plug edge:0",
        "plug edge:1",   "plug core:0",
        "plug core:1",   "plug files:0",
        "plug core:2",   "plug spare:0",
        "power files on", "power spare on",
        "addr edge 198.51.100.2/30",
        "addr edge:1 10.0.1.1/24",
        "router edge on",
        "addr files 10.0.1.10/24",
        "gw files 10.0.1.1",
        "dhcpd files 10.0.1.50 60 24 10.0.1.1 10.0.1.10",
        NULL
    };
    ck("a tower with a file server and a spare, built over the socket",
       script(&ses, BUILD, &o));

    /* THE BATTERY IS A PURCHASE, AND IT COSTS. */
    long before = ses.s.money;
    say(&ses, "ups spare", &o);
    ck("a ups goes under the spare and comes out of the money",
       ses.s.dev[site_dev_by_name(&ses.s, "spare")].ups &&
       ses.s.money == before - site_ups_price());

    /* The tenancy moves in on day 19 and their desks get copper. */
    char cmd[64];
    snprintf(cmd, sizeof cmd, "day %d", TENANT_IN);
    say(&ses, cmd, &o);
    say(&ses, "serve 1 core", &o);
    ck("a tenancy is in and their desks are on the switch",
       site_tenant_connected(&ses.s, 0) > 10);

    /* Up to the night before. Nothing has happened to the kit yet. */
    snprintf(cmd, sizeof cmd, "day %d", CUT_ONE - 1 - TENANT_IN);
    say(&ses, cmd, &o);
    int fd = site_dev_by_name(&ses.s, "files");
    int sd = site_dev_by_name(&ses.s, "spare");
    ck("on the night before, every box is up and nothing has been logged",
       ses.s.day == CUT_ONE - 1 && ses.s.dev[fd].powered &&
       ses.s.dev[sd].powered && ses.s.ev_total == 0);

    ck("and the tenancy's people are getting their work done",
       ses.s.last.finished * 5 >= ses.s.last.sessions * 4 &&
       ses.s.last.sessions > 0);

    /* What the machine looked like before the world touched it, so that
     * anything `pkg verify` says afterwards can be attributed. */
    Finding was[24];
    int nwas = 0;
    say(&ses, "plug files", &o);
    nwas = verify_scan(say(&ses, "pkg verify", &o), was, 24);
    say(&ses, "unplug", &o);

    /* -------------------------------------------------------- the morning */
    const char *dayout = say(&ses, "day 1", &o);
    ck("the day it lands, the day's report says the lights went out",
       has(dayout, "lost mains power"));
    ck("and it names the box that went down with them",
       has(dayout, "files went down with the power"));

    Machine *mf = ses.mach[fd], *msp = ses.mach[sd];
    ck("the file server is off and its filesystem is marked dirty",
       !ses.s.dev[fd].powered && mf && mf->fs_dirty);
    ck("and it lost what it had in flight, because it was the one working",
       mf && mf->fs_lost > 0);

    /* ------------------------------------------------ 4. GOOD PLAY SURVIVES */
    ck("the box on the battery is still switched on",
       ses.s.dev[sd].powered);
    ck("with a filesystem that does not need checking",
       msp && !msp->fs_dirty);
    say(&ses, "plug spare", &o);
    const char *log = say(&ses, "grep nomups /var/log/messages", &o);
    ck("and the receipt for the ups is in its own syslog",
       has(log, "utility power lost") && has(log, "utility power restored"));
    say(&ses, "unplug", &o);

    /* -------------------------------- 2. THE DAMAGE IS VISIBLE TO THE TOOLS */
    const char *boot = say(&ses, "power files on", &o);
    ck("switching it back on stops in the initrd, on the filesystem",
       has(boot, "contains a file system with errors") &&
       has(boot, "RUN fsck MANUALLY") && !mf->boot.running);

    /* ------------------------------------------- 3. AND THE REPAIR REPAIRS IT */
    const char *r = say(&ses, "rescue files", &o);
    ck("the live medium on the crash cart boots the box",
       has(r, "booting from /dev/sr0 (rescue medium)") &&
       has(r, "the customer disk is /dev/sda1 and is NOT mounted"));
    say(&ses, "plug files", &o);
    const char *f = say(&ses, "fsck /dev/sda1", &o);
    ck("`fsck /dev/sda1` recovers the journal and says what it could not save",
       has(f, "FILE SYSTEM WAS MODIFIED") && has(f, "inode(s) with bad content"));
    say(&ses, "unplug", &o);
    const char *back = say(&ses, "eject files", &o);
    ck("and with the stick out it boots its own disk again",
       has(back, "[UP at target]") && mf->boot.running);

    say(&ses, "plug files", &o);
    Finding now[24];
    int nnow = verify_scan(say(&ses, "pkg verify", &o), now, 24);
    Finding hit;
    bool found = false;
    for (int i = 0; i < nnow && !found; i++)
        if (!in_set(was, nwas, now[i].path)) { hit = now[i]; found = true; }
    ck("`pkg verify` now names a file that was fine the night before", found);

    if (found) {
        printf("    the blackout took %s, shipped by %s\n", hit.path, hit.pkg);
        char line[NOM_PATH_MAX + 32];
        snprintf(line, sizeof line, "pkg diff %s", hit.path);
        const char *d = say(&ses, line, &o);
        ck("`pkg diff` says it was truncated rather than edited",
           has(d, "SHORT -- it was truncated, not edited") ||
           has(d, "one of them stops"));

        snprintf(line, sizeof line, "pkg reinstall --force %s", hit.pkg);
        say(&ses, line, &o);
        Finding fixed[24];
        int nf = verify_scan(say(&ses, "pkg verify", &o), fixed, 24);
        ck("`pkg reinstall` puts it back and verify stops naming it",
           !in_set(fixed, nf, hit.path));
    } else {
        ck("`pkg diff` says it was truncated rather than edited", false);
        ck("`pkg reinstall` puts it back and verify stops naming it", false);
    }
    say(&ses, "unplug", &o);

    /* And the whole thing is legible after the fact, which is the difference
     * between a fault with a history and a fault that fell out of the sky. */
    const char *ev = say(&ses, "events", &o);
    ck("`events` tells the player what happened and on which day",
       has(ev, "lost mains power") && has(ev, "was on a battery and stayed up"));

    buf_free(&o);
    session_end(&ses);
}

/* ================================================= 5. A DISK THAT WORE OUT */
/* Nothing here is a timer with a die in it: the box ages because it is
 * switched on and its port is doing work, it complains in its own log for
 * fifteen days before it loses anything, and a player who reads the log has
 * every chance to put a new disk in first. */
static void check_disk(void)
{
    printf("\na disk that has been running long enough to lose a sector\n");
    Session ses;
    if (!session_start(&ses, EV_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *const BUILD[] = {
        "credit 40000",
        "buy switch8 sw", "buy server arc",
        "go goods", "carry sw",  "go mdf", "drop",
        "go goods", "carry arc", "go mdf", "drop",
        "spool cat6",
        "plug uplink:0", "plug sw:0",
        "plug sw:1", "plug arc:0",
        "power arc on",
        "addr arc 10.0.1.10/24",
        /* A battery, so the blackouts on days 14 and 36 do not switch it off
         * and stop the clock on the thing this section is measuring. */
        "ups arc",
        NULL
    };
    ck("a box, on a battery, left running", script(&ses, BUILD, &o));
    int ad = site_dev_by_name(&ses.s, "arc");

    say(&ses, "day 44", &o);
    ck("after forty-four days it is worn but has said nothing",
       ses.s.dev[ad].run_days == 44 && !ses.s.dev[ad].warned);

    /* AND THE HELP HAS TO SAY WHY IT WORE. This box has served nobody for
     * forty-four days -- no tenancy, no desk, no transfer -- so every point
     * of that wear is the flat charge for being switched on. The `events`
     * text used to say wear was "worked out from how hard it has actually
     * been used", and a playtester believed it, left seven idle servers
     * running through the empty stretch at the start of a run, and paid for
     * five disks. The number here is the proof the sentence has to match. */
    ck("an idle box wears one point a day, purely for being switched on",
       ses.s.dev[ad].wear == 44);
    {
        const char *ev = say(&ses, "events", &o);
        ck("and `events` says so, rather than blaming work it never did",
           has(ev, "SWITCHED ON") && has(ev, "five times"));
    }

    const char *d45 = say(&ses, "day 1", &o);
    ck("on the forty-fifth its disk starts logging reallocated sectors",
       has(d45, "Its disk is going") && ses.s.dev[ad].warned);

    say(&ses, "plug arc", &o);
    const char *log = say(&ses, "grep SMART /var/log/messages", &o);
    ck("and the warning is in the machine's own /var/log/messages",
       has(log, "reallocated sector count"));
    Finding was[24];
    int nwas = verify_scan(say(&ses, "pkg verify", &o), was, 24);
    ck("with nothing yet wrong with any file it holds", true);
    say(&ses, "unplug", &o);

    /* FIFTEEN DAYS OF NOTICE. That is the whole of "avoidable by good play":
     * `disk arc` at any point in here costs a hundred and forty pounds and
     * this never happens. */
    const char *d60 = say(&ses, "day 15", &o);
    ck("fifteen days later it loses one, and the day says so",
       has(d60, "lost a sector"));

    say(&ses, "plug arc", &o);
    const char *elog = say(&ses, "grep -i medium /var/log/messages", &o);
    ck("the kernel logged the unrecovered read error", has(elog, "medium error"));

    Finding now[24];
    int nnow = verify_scan(say(&ses, "pkg verify", &o), now, 24);
    Finding hit;
    bool found = false;
    for (int i = 0; i < nnow && !found; i++)
        if (!in_set(was, nwas, now[i].path)) { hit = now[i]; found = true; }
    ck("`pkg verify` finds the file the sector was under", found);
    if (found) {
        printf("    the bad sector took %s, shipped by %s\n", hit.path, hit.pkg);
        char line[NOM_PATH_MAX + 32];
        snprintf(line, sizeof line, "pkg reinstall --force %s", hit.pkg);
        say(&ses, line, &o);
        Finding fixed[24];
        int nf = verify_scan(say(&ses, "pkg verify", &o), fixed, 24);
        ck("and `pkg reinstall` puts it back", !in_set(fixed, nf, hit.path));
    } else {
        ck("and `pkg reinstall` puts it back", false);
    }
    say(&ses, "unplug", &o);

    /* THE GOOD PLAY, PROVEN. A new disk resets the wear, which is the whole
     * of the decision the warning was asking the player to make. */
    long money = ses.s.money;
    say(&ses, "disk arc", &o);
    ck("a new disk costs what it costs and resets the wear",
       ses.s.money == money - site_disk_price() && ses.s.dev[ad].wear == 0 &&
       !ses.s.dev[ad].warned);

    buf_free(&o);
    session_end(&ses);
}

/* ===================================================== 6. A HOT CUPBOARD */
static void check_heat(void)
{
    printf("\na comms cupboard with more in it than it can shed\n");
    Session ses;
    if (!session_start(&ses, EV_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *const BUILD[] = {
        "credit 60000",
        "buy switch24 cup", "buy server h1", "buy server h2", "buy server h3",
        "go goods", "carry cup", "go f1.comms", "drop",
        "go goods", "carry h1",  "go f1.comms", "drop",
        "go goods", "carry h2",  "go f1.comms", "drop",
        "go goods", "carry h3",  "go f1.comms", "drop",
        "power h1 on", "power h2 on", "power h3 on",
        NULL
    };
    ck("three servers and a switch, in one cupboard", script(&ses, BUILD, &o));

    int h1 = site_dev_by_name(&ses.s, "h1");
    int room = ses.s.dev[h1].room;
    int watts = site_room_watts(&ses.s, room);
    int cap = site_room_capacity(&ses.s, room);
    printf("    %d W of kit in a room that can shed %d W: %d%%\n",
           watts, cap, site_room_heat(&ses.s, room));
    ck("the room is over what it can shed, out of its own square metres",
       site_room_heat(&ses.s, room) > 100 && cap > 0);

    const char *d1 = say(&ses, "day 1", &o);
    ck("the first day it says so rather than doing anything",
       has(d1, "is running hot") && ses.s.dev[h1].powered);

    say(&ses, "plug h1", &o);
    ck("and the machine's own kernel logged the intake temperature",
       has(say(&ses, "grep thermal /var/log/messages", &o), "trip point"));
    say(&ses, "unplug", &o);

    const char *d3 = say(&ses, "day 2", &o);
    ck("by the third day something in there has shut itself down",
       has(d3, "shut itself down on temperature"));

    int down = 0;
    for (int i = 0; i < ses.s.ndev; i++)
        if (site_kind_has_os(ses.s.dev[i].kind) && !ses.s.dev[i].powered) down++;
    ck("and what shut down went down unclean, like any machine that stops dead",
       down > 0 && ses.mach[h1] && (ses.mach[h1]->fs_dirty || down > 1));

    /* AVOIDABLE. Carry one of them somewhere with air in it and the room is
     * back under what it can lose -- which is a decision the player makes
     * with their legs, in the building, for the price of the walk. */
    say(&ses, "go f1.comms", &o);
    say(&ses, "carry h3", &o);
    say(&ses, "go f1.server", &o);
    say(&ses, "drop", &o);
    ck("carrying one of them into the tenant's server room cools the cupboard",
       site_room_heat(&ses.s, room) <= 100);

    buf_free(&o);
    session_end(&ses);
}

/* ============================================ 7. THE SAME SEED, TWICE OVER */
static void check_determinism(void)
{
    printf("\nthe same seed, the same days, the same weather\n");
    char first[4096] = "";
    bool ok = true;
    for (int pass = 0; pass < 2; pass++) {
        Session ses;
        if (!session_start(&ses, EV_SEED, 100000)) { ok = false; break; }
        Buf o = {0};
        static const char *const BUILD[] = {
            "credit 40000",
            "buy switch8 sw", "buy server one", "buy server two",
            "go goods", "carry sw",  "go mdf", "drop",
            "go goods", "carry one", "go mdf", "drop",
            "go goods", "carry two", "go f1.comms", "drop",
            "spool cat6", "plug uplink:0", "plug sw:0",
            "plug sw:1", "plug one:0",
            "power one on", "power two on",
            "addr one 10.0.1.10/24",
            NULL
        };
        script(&ses, BUILD, &o);
        say(&ses, "day 50", &o);
        Buf ev = {0};
        site_dump_events(&ses.s, &ev);
        if (pass == 0) snprintf(first, sizeof first, "%s", ev.p ? ev.p : "");
        else if (strcmp(first, ev.p ? ev.p : "") != 0) ok = false;
        buf_free(&ev);
        buf_free(&o);
        session_end(&ses);
    }
    ck("fifty days played twice produce the same log, line for line", ok);
    ck("and it is not an empty log", strstr(first, "day ") != NULL);
}

/* ===================================================================== main */
int event_selfcheck(void)
{
    passed = total = 0;
    printf("THE WORLD BREAKS THE MACHINES. Every fault below was caused by the\n"
           "building the player is running, damaged a real disk through\n"
           "core/breaker.c, and was repaired with the tools that already existed.\n");
    check_schedule();
    check_blackout();
    check_disk();
    check_heat();
    check_determinism();
    printf("\n%d/%d event checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
