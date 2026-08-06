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
/* Seed 7008's first tenancy moves in on day 1 and the building loses the
 * mains on days 30, 52 and 78. The gate names those numbers rather than
 * searching for them, because "deterministic from the seed" means a test
 * can.
 *
 * TENANT_IN WAS 19 UNTIL D27, when the letting queue in site.c replaced a
 * schedule whose first tenancy on this seed could not arrive before day
 * nineteen. Nothing about these scenarios needed the wait; they needed a
 * tenancy in before the blackout on day thirty, and they still have one. */
#define CUT_ONE   30
#define CUT_TWO   52
#define TENANT_IN 1

/* DAYS PASS, AND THE TENANCIES THIS GATE IS NOT ABOUT ARE FORGIVEN.
 *
 * This file measures what the WEATHER does to a machine: a blackout, a dirty
 * filesystem, a worn disk. It builds one small tower with one tenancy cabled
 * to it and then runs the clock for weeks. Since D27 the letting queue keeps
 * signing leases while that clock runs, and a tenancy nobody has cabled is
 * struck after its fit-out -- so three of them file, the lease is not
 * renewed, and `day` stops advancing halfway to the blackout. That is the
 * game working correctly and it is not what this gate is measuring.
 *
 * So the clock is turned one day at a time and the strikes belonging to
 * tenancies this scenario never promised anything to are cleared, exactly as
 * `keep_measuring` does in core/loadcheck.c and for the same reason. The
 * tenancy the scenario DID cable keeps everything: its work, its rent and
 * its strikes are whatever the network really gave it, which is the number
 * the blackout checks read. */
static void days(Session *ses, int n, Buf *o)
{
    for (int d = 0; d < n; d++) {
        say(ses, "day 1", o);
        ses->s.over = 0;
        ses->s.complaints = 0;
        for (int i = 0; i < ses->s.ntenant; i++)
            if (site_tenant_connected(&ses->s, i) == 0) {
                ses->s.tenant[i].strikes = 0;
                ses->s.tenant[i].complained = 0;
            }
    }
}

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
/* The STATUS word is kept too, because it is a claim about the bytes and this
 * gate is here to check claims: a blackout leaves a file that is the shipped
 * file and then stops, a worn sector leaves one of the right length with a
 * hole in the middle, and verify has to call those two different things. */
typedef struct { char pkg[40], path[NOM_PATH_MAX], status[64]; } Finding;

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
            snprintf(out[n].status, sizeof out[n].status, "%s", c);
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
/* THREE SERVERS DOWN, AND THREE DIFFERENT MORNINGS. This is the D28 gate.
 *
 * A playtester met exactly this event on day 30 of a real run and called that
 * hour *"worth the preceding twenty-five days"* -- and then said what was
 * wrong with it:
 *
 *   "Three servers down from one cause was three instances of the same
 *   puzzle. Three servers down where one is a heat trip, one is a worn disk
 *   and one is a truncated fstab would have been the game this engine is
 *   obviously capable of."
 *
 * So the tower this gate builds is the one a competent player builds -- a
 * switch and a file server in each of three floors' comms cupboards, home-run
 * to a core switch in the MDF -- and the mains failure on day 30 takes all
 * three servers down at once. What is asserted is that the three of them are
 * three DIFFERENT faults, that each is found with the tools rather than
 * guessed at, and that the documented repair repairs each one.
 *
 * Two more boxes sit in the MDF and are the controls. `spare` has a battery
 * under it: it is up the next morning with a clean filesystem and the receipt
 * in its own syslog. `idle` has neither a battery nor a cable to anything that
 * would give it work: it goes down like the rest, comes back dirty, and fsck
 * is the whole repair -- which is what proves the other three are diagnosis
 * rather than a reflex to reinstall everything on every box.
 */
#define BLACK_SERVERS 3

typedef struct {
    const char *name;
    int         dev;
    Finding     was[24];
    int         nwas;
    Finding     hit;
    bool        found;
    char        boot[8192];      /* what the console said when it came back */
} BlackBox;

static void bb_snapshot(Session *ses, BlackBox *b, Buf *o)
{
    char line[64];
    /* Walk to it first. Every one of these is a person pushing a crash cart
     * up to a box in a room, and a `plug` typed from another floor is
     * refused -- which is the rule, and getting it wrong here once made this
     * gate read a shell it was not standing in front of. */
    snprintf(line, sizeof line, "go %s", b->name);
    say(ses, line, o);
    snprintf(line, sizeof line, "plug %s", b->name);
    say(ses, line, o);
    b->nwas = verify_scan(say(ses, "pkg verify", o), b->was, 24);
    say(ses, "unplug", o);
}

/* fsck, then boot its own disk, then read the packages. The same three moves
 * a player makes, in the same words, on each box in turn. */
static void bb_recover(Session *ses, BlackBox *b, Buf *o)
{
    char line[NOM_PATH_MAX + 64];
    snprintf(line, sizeof line, "go %s", b->name);
    say(ses, line, o);
    /* The button first: a box the mains switched off is a box nobody has
     * switched back on, and the crash cart cannot rescue a machine that is
     * not drawing power. This is the first thing the player does. */
    snprintf(line, sizeof line, "power %s on", b->name);
    say(ses, line, o);
    snprintf(line, sizeof line, "rescue %s", b->name);
    say(ses, line, o);
    snprintf(line, sizeof line, "plug %s", b->name);
    say(ses, line, o);
    say(ses, "fsck /dev/sda1", o);
    say(ses, "unplug", o);
    snprintf(line, sizeof line, "eject %s", b->name);
    snprintf(b->boot, sizeof b->boot, "%s", say(ses, line, o));

    snprintf(line, sizeof line, "plug %s", b->name);
    say(ses, line, o);
    Finding now[24];
    int nnow = verify_scan(say(ses, "pkg verify", o), now, 24);
    b->found = false;
    for (int i = 0; i < nnow && !b->found; i++)
        if (!in_set(b->was, b->nwas, now[i].path)) { b->hit = now[i]; b->found = true; }
    say(ses, "unplug", o);
}

static void check_blackout(void)
{
    printf("\na tower, a blackout on day %d, and three different mornings\n", CUT_ONE);
    Session ses;
    if (!session_start(&ses, EV_SEED, 200000)) { ck("a session starts", false); return; }
    Buf o = {0};

    /* A PLANNED TOWER, BUILT OVER THE SOCKET. One flat subnet is enough for
     * what this gate measures -- the segmentation is --loadcheck's business --
     * but the SERVERS ARE ON THE FLOORS THEY SERVE, because that is what makes
     * three of them carry a floor's files each and therefore what makes three
     * of them have something in flight at 04:12. */
    static const char *const BUILD[] = {
        "credit 120000",
        "buy router edge", "buy switch24 core",
        "spool back", "go goods", "carry edge", "go mdf", "drop",
        "spool back", "go goods", "carry core", "go mdf", "drop",
        "go f2.stairwell", "open", "go f3.stairwell", "open",
        "buy switch24 sw1", "buy switch24 sw2", "buy switch24 sw3",
        "buy server srv1", "buy server srv2", "buy server srv3",
        "buy server spare", "buy server idle",
        "spool back", "go goods", "carry sw1",  "go f1.comms", "drop",
        "spool back", "go goods", "carry srv1", "go f1.comms", "drop",
        "spool back", "go goods", "carry sw2",  "go f2.comms", "drop",
        "spool back", "go goods", "carry srv2", "go f2.comms", "drop",
        "spool back", "go goods", "carry sw3",  "go f3.comms", "drop",
        "spool back", "go goods", "carry srv3", "go f3.comms", "drop",
        "spool back", "go goods", "carry spare", "go mdf", "drop",
        "spool back", "go goods", "carry idle",  "go mdf", "drop",
        "go mdf",
        "cable uplink:0 edge:0 cat6",
        "cable edge:1 core:22 cat6",
        "cable core:0 sw1:23 fibre",
        "cable core:1 sw2:23 fibre",
        "cable core:2 sw3:23 fibre",
        "spool back",
        "addr edge 198.51.100.2/30", "addr edge:1 10.0.1.1/24",
        "gw edge 198.51.100.1", "router edge on",
        "go f1.comms", "cable sw1:22 srv1:0 cat5e", "spool back",
        "power srv1 on", "addr srv1 10.0.1.10/24", "gw srv1 10.0.1.1",
        "dhcpd srv1 10.0.1.50 200 24 10.0.1.1 10.0.1.10", "httpd srv1",
        "go f2.comms", "cable sw2:22 srv2:0 cat5e", "spool back",
        "power srv2 on", "addr srv2 10.0.1.11/24", "gw srv2 10.0.1.1",
        "httpd srv2",
        "go f3.comms", "cable sw3:22 srv3:0 cat5e", "spool back",
        "power srv3 on", "addr srv3 10.0.1.12/24", "gw srv3 10.0.1.1",
        "httpd srv3",
        /* The two controls, powered and cabled and given no work to do. */
        "go mdf", "cable core:3 spare:0 cat5e", "cable core:4 idle:0 cat5e",
        "spool back", "power spare on", "power idle on",
        NULL
    };
    ck("a switch and a file server in each of three floors' cupboards",
       script(&ses, BUILD, &o));

    /* THE BATTERY IS A PURCHASE, AND IT COSTS. */
    long before = ses.s.money;
    say(&ses, "ups spare", &o);
    ck("a ups goes under one of them and comes out of the money",
       ses.s.dev[site_dev_by_name(&ses.s, "spare")].ups &&
       ses.s.money == before - site_ups_price());

    /* The letting queue on this seed puts a tenancy on floor 1 on day 1, one
     * on floor 2 on day 6 and one on floor 3 on day 20. Each gets copper to
     * its own floor's switch, so each floor's server holds that floor's
     * files -- which is what `service` prints in its `files` column. */
    days(&ses, TENANT_IN, &o);
    say(&ses, "go f1.comms", &o);
    say(&ses, "serve 1 sw1 cat5e", &o);
    days(&ses, 5, &o);
    say(&ses, "go f2.comms", &o);
    say(&ses, "serve 2 sw2 cat5e", &o);
    days(&ses, 14, &o);
    say(&ses, "go f3.comms", &o);
    say(&ses, "serve 5 sw3 cat5e", &o);
    ck("three tenancies, one on each floor, with copper to their own switch",
       site_tenant_connected(&ses.s, 0) > 10 &&
       site_tenant_connected(&ses.s, 1) > 10 &&
       site_tenant_connected(&ses.s, 4) > 10);

    BlackBox bx[BLACK_SERVERS] = {
        { "srv1", -1, {{0}}, 0, {{0}}, false, "" },
        { "srv2", -1, {{0}}, 0, {{0}}, false, "" },
        { "srv3", -1, {{0}}, 0, {{0}}, false, "" },
    };
    for (int i = 0; i < BLACK_SERVERS; i++)
        bx[i].dev = site_dev_by_name(&ses.s, bx[i].name);
    int sd = site_dev_by_name(&ses.s, "spare");
    int id = site_dev_by_name(&ses.s, "idle");

    /* Up to the night before. */
    days(&ses, CUT_ONE - 1 - TENANT_IN - 5 - 14, &o);
    ck("on the night before, every box is up and nothing has been logged",
       ses.s.day == CUT_ONE - 1 && ses.s.dev[bx[0].dev].powered &&
       ses.s.dev[bx[1].dev].powered && ses.s.dev[bx[2].dev].powered &&
       ses.s.ev_total == 0);
    ck("and all three tenancies' people are getting their work done",
       ses.s.last.tenants_served == 3);
    /* THE THREE SERVERS REALLY DID DO WORK YESTERDAY, which is the thing the
     * casualties are dealt off. Asserted rather than assumed, because a gate
     * whose three boxes were all idle would pass the variety check below by
     * dealing nothing to any of them. */
    ck("and each of the three floors pulled its files off its own floor's box",
       ses.s.tenant[0].files_dev == bx[0].dev &&
       ses.s.tenant[1].files_dev == bx[1].dev &&
       ses.s.tenant[4].files_dev == bx[2].dev);

    /* What each machine looked like before the world touched it, so that
     * anything `pkg verify` says afterwards can be attributed. */
    for (int i = 0; i < BLACK_SERVERS; i++) bb_snapshot(&ses, &bx[i], &o);
    BlackBox ib = { "idle", id, {{0}}, 0, {{0}}, false, "" };
    bb_snapshot(&ses, &ib, &o);

    /* -------------------------------------------------------- the morning */
    static char dayout[16384];
    snprintf(dayout, sizeof dayout, "%s", say(&ses, "day 1", &o));
    ck("the day it lands, the day's report says the lights went out",
       has(dayout, "lost mains power"));
    ck("and it names every box that went down with them",
       has(dayout, "srv1 went down with the power") &&
       has(dayout, "srv2 went down with the power") &&
       has(dayout, "srv3 went down with the power"));

    bool alldown = true, alldirty = true;
    for (int i = 0; i < BLACK_SERVERS; i++) {
        if (ses.s.dev[bx[i].dev].powered) alldown = false;
        Machine *m = ses.mach[bx[i].dev];
        if (!m || !m->fs_dirty) alldirty = false;
    }
    ck("all three are off, and all three have a filesystem to check",
       alldown && alldirty);

    /* ------------------------------------------------ 4. GOOD PLAY SURVIVES */
    ck("the box on the battery is still switched on", ses.s.dev[sd].powered);
    ck("with a filesystem that does not need checking",
       ses.mach[sd] && !ses.mach[sd]->fs_dirty);
    say(&ses, "go spare", &o);
    say(&ses, "plug spare", &o);
    {
        const char *log = say(&ses, "grep nomups /var/log/messages", &o);
        ck("and the receipt for the ups is in its own syslog",
           has(log, "utility power lost") && has(log, "utility power restored"));
    }
    say(&ses, "unplug", &o);

    /* THE CONTROL. The box that was doing nothing lost nothing: it is dirty,
     * because the plug came out of it too, and that is all. */
    ck("the box that had no work in flight lost nothing to lose",
       ses.mach[id] && ses.mach[id]->fs_dirty && ses.mach[id]->fs_lost == 0);
    bb_recover(&ses, &ib, &o);
    ck("fsck is its whole repair: it boots, and `pkg verify` names nothing new",
       has(ib.boot, "[UP at target]") && !ib.found);

    /* ---------------------------------- 2. THE DAMAGE IS VISIBLE TO THE TOOLS */
    say(&ses, "go srv1", &o);
    const char *boot = say(&ses, "power srv1 on", &o);
    ck("switching one back on stops in the initrd, on the filesystem",
       has(boot, "contains a file system with errors") &&
       has(boot, "RUN fsck MANUALLY") && !ses.mach[bx[0].dev]->boot.running);

    /* ------------------------------------------- 3. AND THE REPAIR REPAIRS IT */
    {
        say(&ses, "go srv1", &o);
        say(&ses, "rescue srv1", &o);
        const char *r = say(&ses, "plug srv1", &o);
        ck("the live medium on the crash cart boots the box",
           has(r, "[UP at target]"));
        const char *f = say(&ses, "fsck /dev/sda1", &o);
        ck("`fsck /dev/sda1` recovers the journal and says what it could not save",
           has(f, "FILE SYSTEM WAS MODIFIED") && has(f, "inode(s) with bad content"));

        /* THE MEDIUM IS READ-ONLY AND SAYS SO, AND USED TO ACCEPT WRITES.
         * A player repairing a box edits what they believe is the customer's
         * file, is told it was written, and has changed a live image that
         * vanishes at the next boot. Being refused is what sends them to the
         * mount the banner already prints -- so both halves are asserted
         * here: the disc turns a write down, and the mounted disk takes it. */
        say(&ses, "ed /etc/hostname 1c \"scribble\" . w", &o);
        ck("a write to the disc's own /etc is refused, and says nothing was written",
           has(o.p, "cannot write") && has(o.p, "NOTHING was written"));
        say(&ses, "mount /dev/sda1 /mnt", &o);
        const char *w = say(&ses, "ed /mnt/etc/hostname 1c \"reached-the-disk\" . w",
                            &o);
        ck("but the customer's disk, mounted at /mnt, takes one",
           has(w, "bytes written"));
        ck("and reading it back off the disk gives what was written",
           has(say(&ses, "cat /mnt/etc/hostname", &o), "reached-the-disk"));
        say(&ses, "umount /mnt", &o);
        say(&ses, "unplug", &o);
        snprintf(bx[0].boot, sizeof bx[0].boot, "%s", say(&ses, "eject srv1", &o));
        say(&ses, "plug srv1", &o);
        Finding now[24];
        int nnow = verify_scan(say(&ses, "pkg verify", &o), now, 24);
        for (int i = 0; i < nnow && !bx[0].found; i++)
            if (!in_set(bx[0].was, bx[0].nwas, now[i].path))
                { bx[0].hit = now[i]; bx[0].found = true; }
        say(&ses, "unplug", &o);
    }
    bb_recover(&ses, &bx[1], &o);
    bb_recover(&ses, &bx[2], &o);

    ck("`pkg verify` names a file on each of them that was fine the night before",
       bx[0].found && bx[1].found && bx[2].found);
    for (int i = 0; i < BLACK_SERVERS; i++)
        if (bx[i].found)
            printf("    %s came back with %s %s, shipped by %s\n", bx[i].name,
                   bx[i].hit.path, bx[i].hit.status, bx[i].hit.pkg);

    /* ================= THE SENTENCE THIS WHOLE RECORD EXISTS FOR ==========
     *
     * Three servers, one cause, three different faults. At HEAD every box a
     * blackout took down was handed the same fault_unclean_shutdown, so every
     * one of these lines said TRUNCATED -- three files, one puzzle, and the
     * second and third repair were the first one typed again. Different FILES
     * is not the claim and never was; different KINDS of damage is, so the
     * status word is what is counted. */
    bool distinct = true;
    int kinds = 0;
    for (int i = 0; i < BLACK_SERVERS; i++) {
        if (!bx[i].found) { distinct = false; continue; }
        bool seen = false;
        for (int j = 0; j < i; j++)
            if (bx[j].found && strcmp(bx[i].hit.status, bx[j].hit.status) == 0)
                seen = true;
        if (!seen) kinds++;
        for (int j = i + 1; j < BLACK_SERVERS; j++)
            if (bx[j].found && strcmp(bx[i].hit.path, bx[j].hit.path) == 0)
                distinct = false;
    }
    ck("three different files, and more than one KIND of damage between them",
       distinct && kinds >= 2);

    bool missing = false, trunc = false;
    for (int i = 0; i < BLACK_SERVERS; i++) {
        if (!bx[i].found) continue;
        if (strcmp(bx[i].hit.status, "MISSING") == 0) missing = true;
        if (strcmp(bx[i].hit.status, "TRUNCATED") == 0) trunc = true;
    }
    ck("one of them lost a file outright and another had one cut short",
       missing && trunc);

    /* AND EVERY ONE OF THEM IS FINDABLE RATHER THAN GUESSABLE. The boot chain
     * stops at the stage that is actually wrong and NAMES the file, or the
     * service that reads it does, in the machine's own console output. */
    int named = 0;
    for (int i = 0; i < BLACK_SERVERS; i++)
        if (bx[i].found && strstr(bx[i].boot, bx[i].hit.path)) named++;
    ck("and the console names the file `pkg verify` names, on at least two",
       named >= 2);
    printf("    the console named the damaged file itself on %d of %d\n",
           named, BLACK_SERVERS);

    /* AND SAYS WHICH WAY IT DIFFERS, which is the sentence that decides what
     * the player does next. A file the power cut half-wrote is a strict prefix
     * of what shipped and verify has to read it as damage and point at the
     * repair, rather than call it somebody's edit. */
    {
        int t = -1;
        for (int i = 0; i < BLACK_SERVERS; i++)
            if (bx[i].found && strcmp(bx[i].hit.status, "TRUNCATED") == 0) t = i;
        ck("verify calls the half-written file TRUNCATED, not somebody's edit",
           t >= 0);
        if (t >= 0) {
            char line[NOM_PATH_MAX + 64];
            snprintf(line, sizeof line, "go %s", bx[t].name);
            say(&ses, line, &o);
            snprintf(line, sizeof line, "plug %s", bx[t].name);
            say(&ses, line, &o);
            static char vtext[16384];
            snprintf(vtext, sizeof vtext, "%s", say(&ses, "pkg verify", &o));
            ck("and the summary says a forced reinstall is what puts it back",
               strstr(vtext, "cut short") != NULL &&
               strstr(vtext, "pkg reinstall --force") != NULL);
            ck("while admitting a hand-deleted tail would look identical",
               strstr(vtext, "by hand would leave the same bytes") != NULL);
            snprintf(line, sizeof line, "pkg diff %s", bx[t].hit.path);
            const char *d = say(&ses, line, &o);
            ck("`pkg diff` says it was truncated rather than edited",
               has(d, "SHORT -- it was truncated, not edited") ||
               has(d, "one of them stops"));
            say(&ses, "unplug", &o);
        } else {
            ck("and the summary says a forced reinstall is what puts it back", false);
            ck("while admitting a hand-deleted tail would look identical", false);
            ck("`pkg diff` says it was truncated rather than edited", false);
        }
    }

    /* AND THE DOCUMENTED REPAIR REPAIRS EACH OF THE THREE, walked end to end
     * in the words a player types -- a plain reinstall for the file that is
     * gone, a forced one for the file that was cut short, because that is what
     * `pkg verify` told them to do in each case. */
    int fixed = 0, up = 0;
    for (int i = 0; i < BLACK_SERVERS; i++) {
        if (!bx[i].found) continue;
        char line[NOM_PATH_MAX + 64];
        snprintf(line, sizeof line, "go %s", bx[i].name);
        say(&ses, line, &o);
        snprintf(line, sizeof line, "plug %s", bx[i].name);
        say(&ses, line, &o);
        snprintf(line, sizeof line, "pkg reinstall %s%s",
                 strcmp(bx[i].hit.status, "MISSING") == 0 ? "" : "--force ",
                 bx[i].hit.pkg);
        say(&ses, line, &o);
        Finding now[24];
        int nnow = verify_scan(say(&ses, "pkg verify", &o), now, 24);
        if (!in_set(now, nnow, bx[i].hit.path) ||
            strcmp(bx[i].hit.status, "MISSING") == 0) fixed++;
        say(&ses, "unplug", &o);
        snprintf(line, sizeof line, "power %s off", bx[i].name);
        say(&ses, line, &o);
        snprintf(line, sizeof line, "power %s on", bx[i].name);
        if (has(say(&ses, line, &o), "[UP at target]")) up++;
    }
    ck("the repair `pkg verify` named puts every one of the three files back",
       fixed == BLACK_SERVERS);
    ck("and all three boot to target again afterwards", up == BLACK_SERVERS);

    /* And the whole thing is legible after the fact, which is the difference
     * between a fault with a history and a fault that fell out of the sky. */
    const char *ev = say(&ses, "events", &o);
    ck("`events` tells the player what happened and on which day",
       has(ev, "lost mains power") && has(ev, "was on a battery and stayed up"));

    buf_free(&o);
    session_end(&ses);
}

/* ============================ THE REPAIR TOOL AND THE MACHINE'S OWN NAME ===
 *
 * A playtester force-reinstalled a package on a server they had called srv3
 * and the box came back called node-4097. That behaviour is correct and this
 * gate says so in the only way that counts, by running it: /etc/hostname is
 * shipped by the `filesystem` package -- each machine's factory identity, the
 * way aaa_base ships /etc/HOSTNAME -- the tower writes the player's name over
 * it when the box is switched on, and `--force` means exactly what it says.
 *
 * What was missing was the sentence. A line about a .pkgsave is not "this
 * machine is now called something else", and the rename is otherwise silent
 * until the next thing that prints the name. So: the warning is checked, and
 * then every claim the warning makes is checked against the machine. */
static void check_rename(void)
{
    printf("\na forced reinstall, and the name the machine answers to\n");
    Session ses;
    if (!session_start(&ses, EV_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *const BUILD[] = {
        "credit 60000", "buy server srv3",
        "go goods", "carry srv3", "go mdf", "drop",
        "power srv3 on", NULL
    };
    ck("a server bought, carried in and switched on", script(&ses, BUILD, &o));

    say(&ses, "plug srv3", &o);
    ck("the operating system on it answers to the name the player gave the box",
       has(say(&ses, "uname -n", &o), "srv3"));

    /* The tower's name IS a local modification of a package file, which is
     * why a plain reinstall leaves it alone -- and says that it is. */
    const char *plain = say(&ses, "pkg reinstall filesystem", &o);
    ck("a plain reinstall keeps it, and says it is keeping it",
       has(plain, "keeping locally modified /etc/hostname") &&
       !has(plain, "RENAMED") &&
       has(say(&ses, "uname -n", &o), "srv3"));

    static char forced[8192];
    snprintf(forced, sizeof forced, "%s",
             say(&ses, "pkg reinstall --force filesystem", &o));
    ck("--force overwrites it and says the machine has been RENAMED",
       strstr(forced, "THIS MACHINE HAS BEEN RENAMED") != NULL);
    ck("naming both the name it had and the name it has now",
       strstr(forced, "`srv3`") != NULL && strstr(forced, "node-") != NULL);

    const char *u = say(&ses, "uname -n", &o);
    ck("and the warning is true: the machine answers to the factory name now",
       has(u, "node-") && !has(u, "srv3"));

    /* And the way back that the warning offers really is a way back. */
    say(&ses, "cp /etc/hostname.pkgsave /etc/hostname", &o);
    ck("the .pkgsave the warning names puts the player's name back",
       has(say(&ses, "uname -n", &o), "srv3"));

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

    days(&ses, 44, &o);
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
    days(&ses, 14, &o);
    const char *d60 = say(&ses, "day 1", &o);
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
    /* AND DOES NOT CALL IT A TRUNCATION. The blackout leaves a file that is
     * the shipped file and then stops; a sector that will not read back
     * leaves one of exactly the right length with five hundred and twelve
     * bytes of zeroes in the middle of it. Verify now separates those two,
     * and this is the check that keeps the separation honest -- a rule that
     * called every difference a truncation would pass the blackout check
     * above and be worthless. */
    ck("a hole in the middle is CHANGED, not TRUNCATED",
       found && strcmp(hit.status, "CHANGED") == 0);
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

    /* ================== D28. AND THE SECOND SECTOR, ON A DISK NOBODY REPLACED
     *
     * The first loss is kept off the files the boot chain reads on purpose, so
     * the box comes up and the player can get a shell on it. That courtesy is
     * right once and wrong twice: by now this disk has logged SMART warnings
     * for a fortnight, lost a file, been named in `events`, and `disk arc` has
     * been a hundred and forty pounds away the whole time. So the second one
     * is allowed to take /etc/fstab, and the boot chain stops at the stage
     * that is really wrong. That is D23's *a disk nobody replaced -> the
     * truncated file the boot log names*, with fifteen days of notice in front
     * of it and another fifteen after.
     *
     * WITHOUT THIS the second loss lands on another ordinary /etc file and the
     * box boots exactly as it did before, so a disk could be ignored for ever
     * at the price of one `pkg reinstall` a fortnight. */
    Finding pre2[24];
    say(&ses, "plug arc", &o);
    int npre2 = verify_scan(say(&ses, "pkg verify", &o), pre2, 24);
    say(&ses, "unplug", &o);
    days(&ses, 14, &o);
    const char *d75 = say(&ses, "day 1", &o);
    ck("fifteen days later, unreplaced, it loses another one",
       has(d75, "lost another sector"));
    ck("and the day says this one was under something the boot reads",
       has(d75, "something the boot reads"));

    say(&ses, "plug arc", &o);
    Finding now2[24];
    int nnow2 = verify_scan(say(&ses, "pkg verify", &o), now2, 24);
    Finding hit2;
    bool found2 = false;
    for (int i = 0; i < nnow2 && !found2; i++)
        if (!in_set(pre2, npre2, now2[i].path)) { hit2 = now2[i]; found2 = true; }
    say(&ses, "unplug", &o);
    /* The list is breaker.c's own boot_critical() set, named here so that the
     * claim in the event text -- "something the boot reads" -- is checked
     * against the path rather than believed. */
    static const char *const BOOTFILES[] = {
        "/etc/fstab", "/etc/passwd", "/etc/shadow", "/etc/group",
        "/etc/inittab", "/etc/ld.so.conf", "/etc/shells", "/etc/rc.",
        "/etc/services.d/", "/etc/zbl", "/etc/net/interfaces", NULL
    };
    bool critical = false;
    for (int i = 0; found2 && BOOTFILES[i]; i++)
        if (strncmp(hit2.path, BOOTFILES[i], strlen(BOOTFILES[i])) == 0)
            critical = true;
    ck("and `pkg verify` names a file the boot chain itself reads", critical);
    if (found2) printf("    the second sector took %s, shipped by %s\n",
                       hit2.path, hit2.pkg);

    /* AND IT IS STILL A REPAIR THE TOOLS CAN DO. Nothing new was added: the
     * same forced reinstall, on the package verify named. */
    if (found2) {
        char line[NOM_PATH_MAX + 40];
        say(&ses, "plug arc", &o);
        snprintf(line, sizeof line, "pkg reinstall --force %s", hit2.pkg);
        say(&ses, line, &o);
        Finding fx[24];
        int nfx = verify_scan(say(&ses, "pkg verify", &o), fx, 24);
        ck("and the same forced reinstall puts that one back too",
           !in_set(fx, nfx, hit2.path));
        say(&ses, "unplug", &o);
        say(&ses, "power arc off", &o);
        ck("after which the box boots to target again",
           has(say(&ses, "power arc on", &o), "[UP at target]"));
    } else {
        ck("and the same forced reinstall puts that one back too", false);
        ck("after which the box boots to target again", false);
    }

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


/* ============================== D28. 7. A RUN WITH NO MARGIN LEFT IN IT ====
 *
 * D23 named this one in the list of things the world ought to be able to do
 * and nothing ever did it: *a cable run past interference -> errors that only
 * appear under traffic.*
 *
 * The tower this builds is one wrong decision: the floor's switch put in the
 * office WITH the desks rather than in the comms cupboard, and home-run to the
 * core in copper. The building generator measures that run at ninety-five
 * metres -- copper carries a hundred, and the last ten of them are the margin
 * -- and it is carrying a whole floor's file and web traffic to a server in
 * the basement. Nothing here chose ninety-five: it is the tray distance from
 * f0 MDF to f3 office in this seed's building.
 *
 * WHAT MAKES IT A DECISION RATHER THAN A TAX. The same run to a printer nobody
 * uses never degrades, because the errors are measured off the traffic. The
 * game charges by the metre and prints the length at the moment the money
 * leaves, so the player was told what they were buying. Fibre runs two
 * kilometres. And the fix is `uncable` and pull it again.
 */
static void check_copper(void)
{
    printf("\nninety-five metres of copper under a floor of desks\n");
    Session ses;
    if (!session_start(&ses, EV_SEED, 200000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *const BUILD[] = {
        "credit 120000",
        "buy router edge", "buy switch24 core", "buy server files",
        "spool back", "go goods", "carry edge",  "go mdf", "drop",
        "spool back", "go goods", "carry core",  "go mdf", "drop",
        "spool back", "go goods", "carry files", "go mdf", "drop",
        "go f2.stairwell", "open", "go f3.stairwell", "open",
        "buy switch24 far",
        /* IN THE OFFICE, WITH THE DESKS. Which is where somebody who has
         * never had to unbuild one puts it. */
        "spool back", "go goods", "carry far", "go f3.office", "drop",
        "go mdf",
        "cable uplink:0 edge:0 cat6",
        "cable edge:1 core:22 cat6",
        "cable core:1 files:0 cat5e",
        "addr edge 198.51.100.2/30", "addr edge:1 10.0.1.1/24",
        "gw edge 198.51.100.1", "router edge on",
        "power files on", "addr files 10.0.1.10/24", "gw files 10.0.1.1",
        "dhcpd files 10.0.1.50 200 24 10.0.1.1 10.0.1.10", "httpd files",
        /* A battery, so the blackout on day 30 does not switch the file
         * server off in the middle of what this section is measuring. */
        "ups files",
        "cable core:0 far:23 cat5e",
        "spool back",
        NULL
    };
    ck("a floor's switch in the office, home-run to the core in copper",
       script(&ses, BUILD, &o));

    int cd = site_dev_by_name(&ses.s, "core");
    int marginal = -1;
    for (int i = 0; i < ses.s.nlink; i++)
        if (ses.s.link[i].a == cd && ses.s.link[i].aport == 0) marginal = i;
    ck("the building measures that run at ninety-five metres of cat5e",
       marginal >= 0 && ses.s.link[marginal].metres == 95 &&
       ses.s.link[marginal].kind == CAB_CAT5E);
    ck("and it comes up at a gigabit, because it is inside what copper carries",
       net_port_speed(ses.s.net, ses.s.dev[cd].node, 0) == 1000);

    /* The floor-3 tenancy moves in on day 20 and gets copper to that switch.
     * Their files are in the basement, so every byte of a floor's work goes
     * over the ninety-five metres. */
    days(&ses, 20, &o);
    say(&ses, "go f3.office", &o);
    say(&ses, "serve 5 far cat5e", &o);
    days(&ses, 1, &o);
    ck("a floor of desks behind it, and their day's work finishes",
       ses.s.tenant[4].tried > 0 &&
       ses.s.tenant[4].finished * 5 >= ses.s.tenant[4].tried * 4);

    /* -------------------------------------------- and then it starts to go */
    int warned_on = 0, slowed_on = 0;
    for (int d = 0; d < 40 && !slowed_on; d++) {
        const char *r = say(&ses, "day 1", &o);
        if (!warned_on && has(r, "taking errors under load")) warned_on = ses.s.day;
        if (has(r, "has retrained to 100 Mb")) slowed_on = ses.s.day;
        ses.s.over = 0;
        ses.s.complaints = 0;
        for (int i = 0; i < ses.s.ntenant; i++)
            if (site_tenant_connected(&ses.s, i) == 0) {
                ses.s.tenant[i].strikes = 0;
                ses.s.tenant[i].complained = 0;
            }
    }
    printf("    it was named in `events` on day %d and retrained on day %d\n",
           warned_on, slowed_on);
    ck("`events` says it is taking errors under load, days before anything else",
       warned_on > 0 && slowed_on > warned_on);
    /* Read off the far end's own port rather than out of the Site's book on
     * the run, so that this gate compiles and fails against a clean HEAD
     * checkout that has no such book. */
    int fd = site_dev_by_name(&ses.s, "far");
    ck("and then the run retrains itself down to a hundred megabits",
       slowed_on > 0 && net_port_speed(ses.s.net, ses.s.dev[fd].node, 23) == 100);

    ck("which is what the port really clocks now, not a note in a log",
       net_port_speed(ses.s.net, ses.s.dev[cd].node, 0) == 100);
    const char *load = say(&ses, "load", &o);
    ck("`load` prints the new speed against the port's own name",
       has(load, "core:0") && has(load, "100Mb"));
    const char *ev = say(&ses, "events", &o);
    ck("and `events` names both ends of the run and how long it is",
       has(ev, "core:0") && has(ev, "far:23") && has(ev, "95 m"));

    /* AND IT COSTS THE TENANCY BEHIND IT, which is the whole reason a speed
     * is worth reading. A hundred megabits under a floor of desks is the
     * number --loadcheck has asserted since D25 is not enough.
     *
     * One more day, because the retrain happens overnight -- the world runs
     * AFTER the busy period, so the day it retrained was still a gigabit day
     * and the first slow day is the one after it. */
    days(&ses, 1, &o);
    ck("the floor behind it stops getting its work done",
       ses.s.tenant[4].finished * 5 < ses.s.tenant[4].tried * 4);

    /* AND IT IS FIXABLE WITH WHAT EXISTS. Pull it and run it in fibre, which
     * has no such budget -- and the port comes back at what the kit can do,
     * because the rates are reapplied from the catalogue every day for every
     * live run rather than remembered on a port. */
    {
        char line[40];
        snprintf(line, sizeof line, "uncable %d", marginal);
        say(&ses, "go core", &o);
        say(&ses, line, &o);
        say(&ses, "cable core:0 far:23 fibre", &o);
        say(&ses, "day 1", &o);
        ck("pulled and run again in fibre, the port is a gigabit again",
           net_port_speed(ses.s.net, ses.s.dev[cd].node, 0) == 1000);
        ses.s.over = 0;
        ses.s.complaints = 0;
    }

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
        /* AND ONE OF THE SAME KIND OF BOX SOMEWHERE WITH AIR IN IT, which is
         * the control for the wear check below. Same server, same day, same
         * amount of work -- none -- and a different room. */
        "buy server cool",
        "go goods", "carry cool", "go mdf", "drop",
        "go f1.comms",
        "power h1 on", "power h2 on", "power h3 on",
        "go mdf", "power cool on", "go f1.comms",
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

    /* D28. AND IT COSTS SOMETHING FROM THAT FIRST DAY, which is the other half
     * of what the playtester asked for: days 0 to 25 were "admin", and the
     * heat rule was the world's only early lever -- it said a thing, waited
     * three days, and then usually did nothing at all, because a room at 110%
     * warns and never trips. A disk running above its rated ambient wears
     * faster; that is the one thing every field study of the things agrees
     * on. So the box in the cupboard is measurably older than the identical
     * box in the MDF after ONE day, out of a decision the player made when
     * they chose the room. */
    {
        int cl = site_dev_by_name(&ses.s, "cool");
        printf("    one day on: the hot box is %d days of disk, the cool one %d\n",
               ses.s.dev[h1].wear, ses.s.dev[cl].wear);
        ck("and a disk in the hot room is already ageing faster than one that "
           "is not", ses.s.dev[h1].wear > ses.s.dev[cl].wear &&
           ses.s.dev[cl].wear == 1);
    }

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
        days(&ses, 50, &o);
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
    check_rename();
    check_disk();
    check_copper();
    check_heat();
    check_determinism();
    printf("\n%d/%d event checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
