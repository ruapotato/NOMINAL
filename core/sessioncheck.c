/* sessioncheck.c — the gate on the SESSION, run as part of `bf --sitecheck`.
 *
 * core/sitecheck.c checks the rules of the world. This checks that a person
 * can PLAY them over a socket, because that is the claim D23 makes and it is
 * the one that had quietly stopped being true: the tower was only reachable
 * from a second binary mode, so nobody could walk the building and get a
 * shell in the same session, and a blind playtester -- which is the only
 * quality mechanism this project has ever had -- could test one half or the
 * other and never the seam.
 *
 * So the interesting assertion here is not that a verb exists. It is that a
 * SCRIPT with no eyes, starting in an empty MDF, can end up with a working
 * network and a root shell on a machine at the end of copper it paid for --
 * and that walking somewhere you cannot walk is refused rather than quietly
 * allowed. Everything below goes through session_line(), the same function
 * the socket calls, on the same text a player types.
 */
#include <stdio.h>
#include <string.h>
#include "nom.h"
#include "session.h"
/* For the one check that has to damage a real machine to see what `look`
 * says about it: powered on is not the same as booted. */
#include "machine.h"

static int *P, *T;

static void ck(const char *what, bool ok)
{
    (*T)++;
    if (ok) (*P)++;
    printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
}

/* One line in, the whole answer out. */
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

#define GATE_SEED  7008ull

/* PUTTING A FLOOR INTO SERVICE IS A WALK AND A BILL, so every check below
 * that needs the floor above has to do both. See do_open() in session.c. */
static void open_next_floor(Session *ses, Buf *o)
{
    char cmd[64];
    snprintf(cmd, sizeof cmd, "go #%d", bld_find(&ses->b, ses->floors, RM_STAIR));
    say(ses, cmd, o);
    say(ses, "open", o);
}

/* ------------------------------------------------- the verbs are all there */
/* A verb that answers "no such command" is a verb a blind tester cannot use,
 * and the 3D shell has an action for every one of these. */
static void check_verbs(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthe verbs a person has hands for\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    ck("you start in the MDF, on the ground floor, with the ISP handoff",
       ses.b.rooms[ses.room].kind == RM_MDF && ses.b.rooms[ses.room].floor == 0 &&
       ses.s.ndev == 1 && ses.s.dev[ses.s.uplink].room == ses.room);

    static const char *VERB[] = {
        "where", "look", "map", "go", "lift", "open", "buy", "carry", "drop",
        "spool", "plug", "unplug", "cable", "uncable", "show", "links", "money",
        "demand", "rooms", "frames", "help", NULL
    };
    bool all = true;
    for (int i = 0; VERB[i]; i++)
        if (has(say(&ses, VERB[i], &o), "no such command")) {
            printf("    `%s` is not a verb\n", VERB[i]);
            all = false;
        }
    ck("every verb the 3D shell has an action for answers to a word", all);

    ck("`help` describes the actual game rather than listing commands",
       has(say(&ses, "help", &o), "IT DEPARTMENT") &&
       has(o.p, "spool") && has(o.p, "MANAGEMENT LINE"));

    ck("`look` names what is in the room and the ways out of it",
       has(say(&ses, "look", &o), "uplink") && has(o.p, "ways out"));

    ck("`look` names a port an agent can address without seeing it",
       has(say(&ses, "look", &o), "next free port uplink:0"));

    ck("`where` says the floor, the room, the money and the metres walked",
       has(say(&ses, "where", &o), "MDF") && has(o.p, "walked") &&
       has(o.p, "floors in service"));

    ck("`demand` ends by doing the arithmetic on what the tower will need",
       has(say(&ses, "demand", &o), "drops in all") &&
       has(o.p, "twenty-four port switches"));

    /* ============================== THE HELP HAS TO BE TRUE OF THE MACHINE
     *
     * A blind playtester's verdict on this build was "three of the game's
     * four help texts describe commands that do not exist", and the worse
     * half of it was the other direction: they could not find how to advance
     * a day. They tried `next`, `advance`, `sleep`, `end day`, `night` and
     * `tomorrow`, and found `day` only by plugging a lead into the handoff,
     * where a DIFFERENT help text lists it.
     *
     * So both directions are gated. Every verb below must be NAMED in the
     * tower help and must ANSWER at the tower prompt, and neither half is
     * allowed to drift without this failing. */
    static const char *LOOP[] = {
        "day", "serve", "service", "status", "load", "isp", "events",
        "get", "httpd", "dnsd", "ups", "disk", NULL
    };
    const char *h = say(&ses, "help", &o);
    Buf help = {0};
    buf_puts(&help, h);
    bool named = true, answers = true;
    for (int i = 0; LOOP[i]; i++) {
        /* Named in the command column of the help, not merely mentioned in a
         * sentence somewhere: "\n  <verb>" is where a player looks. */
        char marker[32];
        snprintf(marker, sizeof marker, "\n  %s ", LOOP[i]);
        if (!has(help.p, marker)) {
            printf("    the tower help does not name `%s`\n", LOOP[i]);
            named = false;
        }
        const char *a = say(&ses, LOOP[i], &o);
        if (has(a, "no such command")) {
            printf("    `%s` is named in the help and is not a verb here\n", LOOP[i]);
            answers = false;
        }
    }
    ck("the tower help names the clock, the money and the services too",
       named);
    ck("and every one of them answers at the tower prompt, not just the "
       "handoff", answers);

    /* AND IT DOES NOT SEND ANYBODY TO A PROGRAM THEY CANNOT RUN. `netstat`
     * lives on a machine with an operating system in it; a switch, a router
     * and the handoff are appliances with a management line and no shell,
     * and the ports that drop are on those. */
    /* A VERB HANDED NO BOX SAYS WHAT IT WANTS, not merely that it wanted
     * something. `dhcpd` used to answer "no such command" at the tower
     * prompt and "dhcpd which box?" once that was fixed, and neither of them
     * tells somebody who cannot see the box what to type next. */
    static const char *DEVV[] = { "dhcpd", "addr", "subif", "trunk", "gw",
                                  "resolver", "httpd", "get", NULL };
    bool spelled = true;
    for (int i = 0; DEVV[i]; i++) {
        const char *a = say(&ses, DEVV[i], &o);
        char want[32];
        snprintf(want, sizeof want, "%s <", DEVV[i]);
        if (has(a, "no such command") || !has(a, want)) {
            printf("    `%s` on its own does not say what it wants: %s", DEVV[i], a);
            spelled = false;
        }
    }
    ck("a verb handed no box says what the verb wants, in the verb's own "
       "spelling", spelled);

    ck("the tower help does not name a program the tower prompt has not got",
       !has(help.p, "netstat") && has(say(&ses, "netstat -P", &o),
                                     "no such command"));
    buf_free(&help);

    buf_free(&o);
    session_end(&ses);
}

/* ------------------------------------------------------- position matters */
static void check_walking(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nwalking, and the places you cannot walk\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    /* A riser is a shaft. bld_walk_all() has never joined one to anything a
     * person can stand in, so this refusal is geometry rather than a rule. */
    int riser = -1;
    for (int i = 0; i < ses.b.nrooms; i++)
        if (ses.b.rooms[i].kind == RM_RISER) { riser = i; break; }
    char cmd[64];
    snprintf(cmd, sizeof cmd, "go #%d", riser);
    int was = ses.room;
    ck("walking into a riser is refused, and says why",
       riser >= 0 && has(say(&ses, cmd, &o), "no way to walk") &&
       ses.room == was);

    ck("walking to a room that is not there is refused by name",
       has(say(&ses, "go f9.nowhere", &o), "no room or box") && ses.room == was);

    /* Somewhere real, on this floor. */
    int dst = -1;
    for (int i = 0; i < ses.b.nrooms; i++)
        if (ses.b.rooms[i].floor == 0 && ses.b.rooms[i].kind == RM_GOODS) dst = i;
    if (dst < 0) for (int i = 0; i < ses.b.nrooms; i++)
        if (ses.b.rooms[i].floor == 0 && ses.b.rooms[i].kind == RM_LOBBY) dst = i;
    snprintf(cmd, sizeof cmd, "go #%d", dst);
    say(&ses, cmd, &o);
    ck("walking somewhere real moves you and charges metres of building",
       dst >= 0 && ses.room == dst && ses.walked > 0 && has(o.p, "you walk"));

    long far = ses.walked;
    say(&ses, "go mdf", &o);
    ck("a room kind on this floor is a spelling of that room",
       ses.b.rooms[ses.room].kind == RM_MDF && ses.walked > far);

    /* The lift, which is the one thing `open` gates. Same words as lift.gd. */
    int top = ses.b.floors - 1;
    snprintf(cmd, sizeof cmd, "lift %d", top);
    ck("the lift refuses a floor nobody has put in service",
       has(say(&ses, cmd, &o), "not in service") && has(o.p, "not lit"));

    snprintf(cmd, sizeof cmd, "lift %d", ses.b.floors + 4);
    ck("and a floor the building has not got",
       has(say(&ses, cmd, &o), "does not pass floor"));

    /* A FLOOR COMING INTO SERVICE COSTS SOMETHING AND HAPPENS SOMEWHERE.
     * `open` used to be free and typeable from anywhere, so there was no
     * reason not to open the whole tower in the first minute. */
    int before = ses.floors;
    long had = ses.s.money;
    ck("`open` from another floor is refused, and says which stairs to take",
       has(say(&ses, "open", &o), "not in service and you are on floor") &&
       has(o.p, "the stairs") && has(o.p, "it will cost") &&
       ses.floors == before && ses.s.money == had);

    snprintf(cmd, sizeof cmd, "go #%d", bld_find(&ses.b, before, RM_STAIR));
    say(&ses, cmd, &o);
    ck("and standing on it is a walk up the stairs, charged in metres",
       ses.b.rooms[ses.room].floor == before && ses.walked > far);

    long walked_up = ses.walked;
    ck("`open` puts the next floor in service and says what is on it",
       has(say(&ses, "open", &o), "in service") && ses.floors == before + 1);
    ck("and it is paid for: the landlord's fit-out comes out of the budget",
       ses.s.money < had && ses.s.spent >= had - ses.s.money);
    printf("    floor %d cost %ld to commission, and %ld m of stairs\n",
           before, had - ses.s.money, ses.walked - far);

    /* Come back down, so the lift below is a lift ride and not a no-op. */
    snprintf(cmd, sizeof cmd, "lift 0");
    say(&ses, cmd, &o);
    far = walked_up;
    snprintf(cmd, sizeof cmd, "lift %d", before);
    say(&ses, cmd, &o);
    ck("and then the lift takes you to it",
       ses.b.rooms[ses.room].floor == before);

    ck("the walk to the lift on the way is charged too", ses.walked > far);

    buf_free(&o);
    session_end(&ses);
}

/* ------------------------------------ you can only touch what is in reach */
static void check_reach(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nreaching a machine means being in the room with it\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    /* Ordered, delivered, and carried to the MDF, which is the only way a
     * box gets anywhere. check_goods below is where that is picked apart;
     * here it is just the setup for reaching one. */
    int mdf = ses.room;
    say(&ses, "buy switch8 sw1", &o);
    ck("kit is charged for and delivered to goods in, not to your feet",
       ses.s.ndev == 2 && ses.s.money == 100000 - 120 &&
       ses.s.dev[1].room == (uint16_t)site_goods_room(&ses.s) &&
       ses.s.dev[1].room != (uint16_t)ses.room);

    say(&ses, "go goods", &o);
    say(&ses, "carry sw1", &o);
    say(&ses, "go mdf", &o);
    say(&ses, "drop", &o);
    ck("and carrying it to the MDF is what puts it in the MDF",
       ses.room == mdf && ses.s.dev[1].room == (uint16_t)mdf);

    /* Take a walk, and everything about that switch goes out of reach. */
    for (int i = 0; i < ses.b.nrooms; i++)
        if (ses.b.rooms[i].floor == 0 && ses.b.rooms[i].kind == RM_LOBBY)
            { char c[32]; snprintf(c, sizeof c, "go #%d", i); say(&ses, c, &o); break; }
    bool moved = ses.room != mdf;

    ck("configuring a box in another room is refused, and says where it is",
       moved && has(say(&ses, "addr sw1 10.0.9.1/24", &o), "and you are not") &&
       has(o.p, "go sw1"));

    ck("plugging the cart into a box in another room is refused",
       has(say(&ses, "plug sw1", &o), "and you are not"));

    say(&ses, "spool cat6", &o);
    ck("and so is putting a cable end in it",
       has(say(&ses, "plug sw1:0", &o), "cannot reach into another room"));

    ck("`go <box>` walks you to the room a box is in",
       has(say(&ses, "go sw1", &o), "you walk") && ses.room == mdf);

    ck("and then the same command works",
       has(say(&ses, "plug sw1:0", &o), "one end into sw1 port 0"));

    ck("a port the box has not got is refused with the numbers it does have",
       has(say(&ses, "plug sw1:19", &o), "numbered 0 to 7"));

    say(&ses, "plug uplink:0", &o);
    ck("and a port that already has a cable in it names the way out",
       has(say(&ses, "plug uplink:0", &o), "already has a cable") &&
       has(o.p, "uncable"));

    buf_free(&o);
    session_end(&ses);
}

/* ------------------------------------------------------------- goods in */
/* THE CLAIM: "it arrives, and it arrives SOMEWHERE -- goods in, not your
 * inventory. You carry it to where it needs to go."
 *
 * That was in the README for a fortnight while `buy` installed the box in
 * whatever room the player happened to be standing in, which made the floor
 * plan scenery: every room was equally close to the loading bay, so where
 * you put a switch cost nothing but copper. These checks are the difference
 * between the two games, and the last one is the point of the mechanic --
 * carrying a box nearer really is cheaper, in metres of cable, measured by
 * the building rather than asserted here. */
static void check_goods(int *passed, int *total)
{
    P = passed; T = total;
    printf("\ngoods in: a delivery is the start of a job\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    int goods = site_goods_room(&ses.s);
    ck("the tower has a goods in, on the ground floor, and it is not the MDF",
       goods >= 0 && ses.b.rooms[goods].kind == RM_GOODS &&
       ses.b.rooms[goods].floor == 0 && goods != ses.room);

    /* Order it from the top of the building. It still lands downstairs. */
    open_next_floor(&ses, &o);
    say(&ses, "lift 2", &o);
    say(&ses, "go comms", &o);
    int up = ses.room;
    const char *bought = say(&ses, "buy switch24 core", &o);
    int d = site_dev_by_name(&ses.s, "core");
    ck("a box ordered from floor two is delivered to the ground floor",
       d > 0 && ses.s.dev[d].room == (uint16_t)goods && ses.room == up &&
       has(bought, "goods in"));
    ck("and the answer says how far away that is, in metres of building",
       has(bought, "m from here") && has(bought, "carry core"));

    ck("it is not where you are, so you cannot reach it",
       has(say(&ses, "addr core 10.0.1.1/24", &o), "and you are not"));
    ck("nor carry it from another floor",
       has(say(&ses, "carry core", &o), "and you are not") && ses.carrying < 0);

    /* Fetch it. */
    say(&ses, "go goods", &o);
    ck("`go goods` finds the delivery from anywhere in the tower",
       ses.room == goods);
    ck("and it is in that room, which is where `look` says it is",
       has(say(&ses, "look", &o), "core") && has(o.p, "roller door"));

    long walked = ses.walked;
    ck("you can pick it up", has(say(&ses, "carry core", &o), "you pick core up") &&
       ses.carrying == d);

    say(&ses, "buy switch8 spare", &o);
    int sp = site_dev_by_name(&ses.s, "spare");
    ck("but only one at a time: both hands are on it",
       has(say(&ses, "carry spare", &o), "both your hands") && ses.carrying == d);
    ck("and a drum of cable is a thing you cannot hold as well",
       has(say(&ses, "spool cat6", &o), "both hands too") && ses.spool_kind < 0);
    ck("nor plug a lead into anything while holding it",
       has(say(&ses, "plug spare", &o), "Put it down first"));

    /* Carry it up. The metres are the building's, not this file's. */
    say(&ses, "lift 2", &o);
    const char *w = say(&ses, "go comms", &o);
    ck("it goes where you go, and the walk is charged",
       has(w, "carrying core") && ses.s.dev[d].room == (uint16_t)ses.room &&
       ses.walked > walked);

    ck("`drop` is what puts it in the room",
       has(say(&ses, "drop", &o), "is in f2 comms cupboard") &&
       ses.carrying < 0 && ses.s.dev[d].room == (uint16_t)up);

    /* THE MEASUREMENT. The same cable, from where the van left it and from
     * where the player carried it to. Nothing here decides which is shorter:
     * bld_cable_all() does, on this tower's own tray. */
    int mdf = bld_find(&ses.b, 0, RM_MDF);
    int from_goods = site_metres(&ses.s, goods, mdf);
    int from_comms = site_metres(&ses.s, up, mdf);
    printf("    a run to the MDF is %d m from goods in and %d m from the "
           "cupboard it was carried to\n", from_goods, from_comms);
    ck("and where it ended up is what the copper is measured from",
       from_goods > 0 && from_comms > 0 && from_goods != from_comms);

    /* A box on the end of a cable does not move, and that is the object
     * rather than a rule: the cable is bought, laid and in the socket. */
    say(&ses, "go mdf", &o);
    say(&ses, "spool cat6", &o);
    say(&ses, "plug uplink:0", &o);
    say(&ses, "go core", &o);
    say(&ses, "plug core:0", &o);
    ck("a cable comes up between the handoff and the box that was carried up",
       ses.s.nlink == 1);
    /* The drum is still in your hands after a run: put it back, or the
     * refusal you get is about the drum and not about the box. */
    say(&ses, "spool back", &o);
    ck("and now it will not be picked up: there is a cable in it",
       has(say(&ses, "carry core", &o), "cable in it") && ses.carrying < 0);
    ck("`uncable` frees it, and it can be carried again",
       has(say(&ses, "uncable 0", &o), "pulled out") &&
       has(say(&ses, "carry core", &o), "you pick core up"));

    /* And the one thing in the building that was never bought. */
    say(&ses, "drop", &o);
    say(&ses, "go mdf", &o);
    ck("the ISP's handoff is not yours to carry anywhere",
       has(say(&ses, "carry uplink", &o), "not yours to move"));

    (void)sp;
    buf_free(&o);
    session_end(&ses);
}

/* --------------------------------------------- the whole build, blind */
/* THE ONE THAT MATTERS. If this cannot be done over a socket then the game
 * cannot be playtested, and D23 says that means it will rot. */
static void check_build(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nan empty MDF to a working network and a shell on a server\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    /* THE WHOLE JOB, and the first six lines of it are a delivery being
     * fetched. Three boxes, three trips, because a person carries one box:
     * this is the shape the README describes and it has to be typeable by
     * somebody who cannot see the building. */
    static const char *SCRIPT[] = {
        "buy router edge",
        "buy switch24 core",
        "buy server files",
        "go goods",
        "carry edge",  "go mdf", "drop",
        "go goods", "carry core", "go mdf", "drop",
        "go goods", "carry files", "go mdf", "drop",
        "spool cat6",
        "plug uplink:0", "plug edge:0",
        "plug edge:1",   "plug core:0",
        "plug core:1",   "plug files:0",
        "power files on",
        "addr edge 198.51.100.2/30",
        "addr edge:1 10.0.1.1/24",
        "router edge on",
        "addr files 10.0.1.10/24",
        "gw files 10.0.1.1",
        NULL
    };
    bool clean = true;
    for (int i = 0; SCRIPT[i]; i++) {
        const char *r = say(&ses, SCRIPT[i], &o);
        if (has(r, "no such command") || has(r, "refused") || has(r, "cannot")) {
            printf("    `%s` -> %s", SCRIPT[i], r);
            clean = false;
        }
    }
    ck("a delivery fetched, cabled and configured by somebody who cannot see",
       clean);

    ck("every link came up", ses.s.nlink == 3 &&
       site_link_state(&ses.s, 0) == PORT_UP &&
       site_link_state(&ses.s, 1) == PORT_UP &&
       site_link_state(&ses.s, 2) == PORT_UP);

    /* AND WHAT ANSWERS IS THE OPERATING SYSTEM, not the box. The server is
     * running, it has the address the player gave it, the router can ARP it
     * -- and the echo request is dropped by the ruleset the image ships,
     * which is counted on the card rather than vanishing. Nobody wrote that
     * interaction; it is a real filter meeting a real packet. */
    say(&ses, "ping edge 10.0.1.10", &o);
    ck("the server is on the wire and its own filter is what refuses the ping",
       !has(o.p, "reply") &&
       has(say(&ses, "show files", &o), "10.0.1.10/24") &&
       has(o.p, "RX 0  TX 2  dropped 1"));

    /* THE SEAM. Pressing the button on a server boots a real machine, and the
     * address it configures its card with came off its own disk -- written
     * there from what the player told the network, because two places to hold
     * one fact is a fault nobody built. The serial lead reads the console it
     * already has; it is not what starts it. */
    const char *boot = say(&ses, "plug files", &o);
    ck("a serial lead in a running server is a shell on it",
       has(boot, "serial console on files") && ses.where == SES_SHELL);

    ck("and it is that machine's shell, not somebody else's",
       has(say(&ses, "cat /etc/hostname", &o), "files"));

    ck("its address came off its own disk, which the player's network wrote",
       has(say(&ses, "cat /etc/net/interfaces", &o), "address 10.0.1.10") &&
       has(o.p, "gateway 10.0.1.1"));

    ck("the machine's own kernel agrees, having applied it",
       has(say(&ses, "netstat -i", &o), "10.0.1.10/24"));

    /* Its firewall drops everything it was not asked for, which is why it
     * does not answer a ping until somebody changes that. Nobody wrote that
     * interaction: it is the shipped ruleset meeting a real filter. */
    say(&ses, "sed -i \"s/policy drop/policy accept/\" /etc/nftables.conf", &o);
    say(&ses, "svc restart nftables", &o);
    say(&ses, "unplug", &o);
    ck("`unplug` puts you back in the room", ses.where == SES_BODY);

    ck("and the box answers now, over three cables and a router",
       has(say(&ses, "ping files 198.51.100.1", &o), "reply"));

    say(&ses, "plug files", &o);
    ck("and it has ARPed its gateway across the copper the player paid for",
       has(say(&ses, "netstat -A", &o), "10.0.1.1"));
    say(&ses, "unplug", &o);

    ck("`desk` hands the line back to the support bench",
       has(say(&ses, "desk", &o), "workstation") && ses.where == SES_DESK);

    ck("and at the desk the tower takes no words at all",
       !session_line(&ses, "look", &o));

    buf_free(&o);
    session_end(&ses);
}

/* --------------------------------------------- powered on is not booted */
/* A playtester switched five servers back on the morning after a mains
 * failure, read "[an OS is running on it]" on every one from `look`, and
 * found out only from `plug` that all five had stopped at the initrd unable
 * to mount a dirty root. That is exactly the morning you are triaging, so it
 * is the worst possible moment for the line to be generous. */
static void check_booted(int *passed, int *total)
{
    P = passed; T = total;
    printf("\npowered on is not the same as booted\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    static const char *SCRIPT[] = {
        "buy server files", "go goods", "carry files", "go mdf", "drop",
        "power files on", NULL
    };
    for (int i = 0; SCRIPT[i]; i++) say(&ses, SCRIPT[i], &o);

    int d = -1;
    for (int i = 0; i < ses.s.ndev; i++)
        if (strcmp(ses.s.dev[i].name, "files") == 0) { d = i; break; }
    if (d < 0 || !ses.mach[d]) { ck("a server powers on and gets a machine", false); goto done; }

    ck("a healthy box that is on says an OS is running on it",
       ses.mach[d]->boot.running &&
       has(say(&ses, "look", &o), "[an OS is running on it]"));

    /* Break its disk the way the world does, and boot it again. Nothing
     * about the site changes -- still switched on, still in the room. Only
     * the boot fails. */
    {
        char what[512];
        machine_break(ses.mach[d], 99, 1, what, sizeof what);
        machine_boot(ses.mach[d]);
    }
    if (ses.mach[d]->boot.running) {
        ck("the damage stopped the boot", false);
        goto done;
    }
    {
        const char *r = say(&ses, "look", &o);
        ck("a box whose boot failed does NOT claim an OS is running on it",
           !has(r, "[an OS is running on it]"));
        ck("it says it is on and where the boot stopped instead",
           has(r, "switched on, but its boot stopped at"));
        ck("and the site still says it is powered, because it is",
           ses.s.dev[d].powered != 0);
    }
done:
    buf_free(&o);
    session_end(&ses);
}

/* ----------------------------------------- whose computer that is */
/* `carry t3d0` used to work. The model had no objection -- a desk is not
 * cabled and not bolted to a wall -- and site_move() reassigns ownership to
 * whatever room the thing lands in, so walking a tenant's computer down to
 * the MDF quietly made it the landlord's. You are the building's IT, not
 * theirs; the copper is yours and the machine on the desk is not. */
static void check_tenant_kit(int *passed, int *total)
{
    P = passed; T = total;
    printf("\na tenant's computer is a tenant's computer\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    /* BEFORE THE CLOCK RUNS, nobody is in. `serve` answered a tenancy that
     * had not moved in with "refused: no such device", and a playtester spent
     * several minutes looking for a typo in a box name that was correct and
     * standing in the room with them. */
    {
        int early = -1;
        for (int i = 0; i < ses.s.ntenant; i++)
            if (!ses.s.tenant[i].moved) { early = i; break; }
        if (early >= 0) {
            char line[64];
            snprintf(line, sizeof line, "serve %d uplink cat6",
                     ses.s.tenant[early].tenant);
            const char *r = say(&ses, line, &o);
            ck("a tenancy that has not moved in is told so, not called a missing box",
               has(r, "does not move in until day") && !has(r, "no such device"));
            ck("and it says which day, and where the list of days is",
               has(r, "`demand`"));
        }
    }

    /* Run the clock until somebody has moved in and put desks in a room. */
    int ti = -1;
    for (int guard = 0; guard < 400 && ti < 0; guard++) {
        say(&ses, "day 1", &o);
        for (int i = 0; i < ses.s.ntenant; i++)
            if (ses.s.tenant[i].moved && ses.s.tenant[i].ndesk > 0) { ti = i; break; }
    }
    if (ti < 0) { ck("a tenancy moves in within four hundred days", false); goto done; }

    {
        int d = ses.s.tenant[ti].desk0;
        int floor = ses.s.dev[d].floor;
        const char *nm = ses.s.dev[d].name;

        /* Walk to it. This matters: without it the refusal you get is the
         * reachability one -- "you are not standing in front of it" -- and
         * the check would pass while proving nothing about ownership. */
        char line[64];
        snprintf(line, sizeof line, "go %s", nm);
        say(&ses, line, &o);
        int room = ses.s.dev[d].room;
        ck("you can walk to a tenant's desk: their floor is not sealed off",
           ses.room == room);

        snprintf(line, sizeof line, "carry %s", nm);
        const char *r = say(&ses, line, &o);
        ck("but their desk is not yours to pick up",
           ses.carrying < 0 && !has(r, "you pick"));
        ck("and the refusal is about whose it is, not about where you stand",
           has(r, "belongs to the tenant") && !has(r, "and you are not"));
        ck("and it stays in their room, still theirs",
           ses.s.dev[d].room == (uint16_t)room && ses.s.dev[d].tenant != 0);
        (void)floor;
    }

done:
    buf_free(&o);
    session_end(&ses);
}

/* ------------------------------------------- a run that did not happen */
/* `cable` is a macro for four things a person does, and when the fourth
 * refused it left the first end in a socket. The NEXT `cable` picked it up
 * without a word: `cable core:4 edge:0` answered `link 4: core:3 to core:4`,
 * a switch cabled to itself, which is a shape the game refuses when you ask
 * for it directly -- and what came out of it was a broadcast storm with no
 * line in the transcript to blame. */
static void check_dangling(int *passed, int *total)
{
    P = passed; T = total;
    printf("\na cable that was never run leaves nothing behind\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    say(&ses, "buy switch8 core", &o);
    say(&ses, "buy router edge", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry core", &o); say(&ses, "go mdf", &o); say(&ses, "drop", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry edge", &o); say(&ses, "go mdf", &o); say(&ses, "drop", &o);

    /* A run that cannot finish: the far end is a port that box has not got. */
    const char *r = say(&ses, "cable core:0 edge:9", &o);
    ck("a run to a port that box has not got does not make a cable",
       has(r, "numbered 0 to 3") && ses.s.nlink == 0);
    ck("and it leaves no end in a socket for the next line to eat",
       has(r, "comes back out of core port 0") && ses.cab_dev < 0);

    ck("so the next run is the one that was asked for",
       has(say(&ses, "cable core:1 edge:0", &o), "core:1 to edge:0") &&
       ses.s.nlink == 1 && ses.s.link[0].aport == 1);

    /* And an end put in BY HAND is not silently consumed either. */
    say(&ses, "plug core:3", &o);
    ck("an end left in by hand stops the macro rather than being used",
       has(say(&ses, "cable core:4 edge:1", &o), "already in core port 3") &&
       ses.s.nlink == 1);
    ck("both ends of one run in the same box is refused where the loop would be",
       has(say(&ses, "plug core:4", &o), "both ends would be in core") &&
       ses.s.nlink == 1);
    buf_free(&o);
    session_end(&ses);
}

/* --------------------------------------------------- the power button */
/* THE DEEPEST ONE. A machine that had never been switched on answered a
 * ping, and then booting it made it LESS reachable, because its own firewall
 * finally started. */
static void check_power(int *passed, int *total)
{
    P = passed; T = total;
    printf("\na box that is not running answers nothing\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    say(&ses, "buy switch8 core", &o);
    say(&ses, "buy pc probe", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry core", &o); say(&ses, "go mdf", &o); say(&ses, "drop", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry probe", &o); say(&ses, "go mdf", &o); say(&ses, "drop", &o);
    say(&ses, "cable core:0 uplink:0", &o);
    say(&ses, "cable core:1 probe:0", &o);
    int pc = site_dev_by_name(&ses.s, "probe");

    ck("a pc arrives switched off, and `show` says so where a player looks",
       pc >= 0 && !ses.s.dev[pc].powered &&
       has(say(&ses, "show probe", &o), "SWITCHED OFF"));
    ck("an off box will not take an address",
       has(say(&ses, "addr probe 10.0.1.30/24", &o), "switched off"));
    ck("and a serial lead does not press the button for you",
       has(say(&ses, "plug probe", &o), "power probe on") &&
       ses.where == SES_BODY && ses.mach[pc] == NULL);

    const char *on = say(&ses, "power probe on", &o);
    ck("powering it on is what boots the operating system in it",
       has(on, "zbios") && has(on, "UP at target") && ses.mach[pc] != NULL);
    ck("and only then does an address stick",
       has(say(&ses, "addr probe 10.0.1.30/24", &o), "10.0.1.30/24"));

    /* What it answers is now its own kernel's business, and the ruleset the
     * image ships drops what nobody asked for. Change the file, restart the
     * service, and the same ping gets through -- and nothing in this program
     * decided either outcome. */
    say(&ses, "addr uplink 10.0.1.1/24", &o);
    ck("its own filter is what refuses the ping, not the game",
       !has(say(&ses, "ping uplink 10.0.1.30", &o), "reply"));
    say(&ses, "plug probe", &o);
    say(&ses, "sed -i \"s/policy drop/policy accept/\" /etc/nftables.conf", &o);
    say(&ses, "svc restart nftables", &o);
    say(&ses, "unplug", &o);
    ck("and its own filter is what lets it through",
       has(say(&ses, "ping uplink 10.0.1.30", &o), "reply"));

    say(&ses, "power probe off", &o);
    ck("switched off, it is silent again and its address went with the power",
       !has(say(&ses, "ping uplink 10.0.1.30", &o), "reply") &&
       net_if_get_addr(ses.s.net, ses.s.dev[pc].node, 0) == 0);

    /* AND `look` MUST NOT SAY THE OPPOSITE OF `show` IN THE SAME ROOM.
     *
     * dev_line() printed "[an OS is running on it]" whenever ses->mach[i] was
     * allocated -- which it is from the first power-on until the session ends,
     * because power-off never frees it. So `look` claimed a box was up while
     * `show <box>` two lines later said it was switched off and serving
     * nothing. A playtester who read the first one and not the second would
     * have gone looking for a network fault on a machine that had no power. */
    const char *lk = say(&ses, "look", &o);
    ck("`look` does not claim an OS is running on a box that is switched off",
       !has(lk, "an OS is running") && has(lk, "SWITCHED OFF"));
    ck("and `show` says the same thing about the same box in the same room",
       has(say(&ses, "show probe", &o), "SWITCHED OFF"));
    say(&ses, "power probe on", &o);
    ck("switched on again, both of them say it is running",
       has(say(&ses, "look", &o), "an OS is running") &&
       !has(say(&ses, "show probe", &o), "SWITCHED OFF"));

    /* THE TOOLS A REPAIR NEEDS, named where somebody holding a serial lead
     * will read them. The playtester repaired a mains-damaged filesystem
     * through this console and only knew to type `fsck` because the initrd
     * had told them to: the console's own help listed ip, netstat, ping and
     * eight more, and not one of the four that fix a broken box. Both halves
     * are gated -- named in the help, and really in the image. */
    say(&ses, "plug probe", &o);
    Buf sh = {0};
    buf_puts(&sh, say(&ses, "help", &o));
    ck("the console help names the four tools a repair actually needs",
       has(sh.p, "fsck /dev/sda1") && has(sh.p, "pkg verify") &&
       has(sh.p, "pkg diff") && has(sh.p, "pkg reinstall"));
    ck("and it says netstat -F is how you read the filter, not `nft`",
       has(sh.p, "netstat -F") && has(sh.p, "not a way to ask it anything"));
    ck("and every one of the four is really a program on this machine",
       !has(say(&ses, "fsck /dev/sda1", &o), "command not found") &&
       !has(say(&ses, "pkg verify", &o), "command not found") &&
       !has(say(&ses, "pkg diff /etc/hosts", &o), "command not found") &&
       !has(say(&ses, "netstat -F", &o), "command not found"));
    ck("and `man fsck` is a real page, for the one tool the initrd names",
       !has(say(&ses, "man fsck", &o), "no manual entry"));
    buf_free(&sh);
    say(&ses, "unplug", &o);
    buf_free(&o);
    session_end(&ses);
}

/* --------------------------------------- what a box serves, across the mains
 *
 * A mains failure, an fsck and three reboots, and `dhcpd`, `dnsd` and
 * `httpd` were gone from all three servers -- with no indication anywhere.
 * `show <box>` did not list services at all, and on the box `svc` still said
 * httpd was running and `ss` still showed :80 listening, which was true of
 * the operating system and not true of the tower. A machine reporting a
 * service running while the tower serves nothing from it is the worst class
 * of bug this project can have.
 *
 * The address survives a power cut because it is written on the disk. A
 * service the player configured is a decision of exactly the same kind, so
 * it is written there too and netd starts it again.
 */
static void check_services(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nwhat a box serves, and whether it still serves it in the morning\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    static const char *SCRIPT[] = {
        "buy switch24 core", "buy server files", "buy pc desk1",
        "go goods", "carry core",  "go mdf", "drop",
        "go goods", "carry files", "go mdf", "drop",
        "go goods", "carry desk1", "go mdf", "drop",
        "cable core:1 files:0 cat6",
        "cable core:2 desk1:0 cat6",
        "power files on",
        "power desk1 on",
        "addr files 10.0.1.10/24",
        "dhcpd files 10.0.1.100 20 24 10.0.1.10 10.0.1.10",
        "dnsd files",
        "httpd files",
        NULL
    };
    for (int i = 0; SCRIPT[i]; i++) say(&ses, SCRIPT[i], &o);

    ck("`show <box>` says what a box is serving, which it never did",
       has(say(&ses, "show files", &o), "services:") &&
       has(o.p, "dhcpd  10.0.1.100-10.0.1.119 on eth0") &&
       has(o.p, "dnsd") && has(o.p, "httpd"));
    ck("and a desk on that segment really gets an address from it",
       has(say(&ses, "dhcp desk1", &o), "10.0.1.100"));
    ck("and a page really comes back over TCP",
       has(say(&ses, "get desk1 10.0.1.10 /", &o), "HTTP"));

    /* THE POWER CUT. Off, on, and nothing else typed. */
    say(&ses, "power files off", &o);
    ck("switched off it serves nothing, because nothing of it is running",
       has(say(&ses, "show files", &o), "SWITCHED OFF"));
    say(&ses, "power files on", &o);

    ck("switched on again it is serving what it was serving",
       has(say(&ses, "show files", &o), "dhcpd  10.0.1.100-10.0.1.119") &&
       has(o.p, "dnsd") && has(o.p, "httpd"));
    ck("and it says so because it read it off its own disk",
       has(say(&ses, "dhcpd files", &o), "10.0.1.100-10.0.1.119"));
    ck("a desk that asks after the power cut gets its address back",
       has(say(&ses, "dhcp desk1", &o), "10.0.1.100"));
    ck("and the page comes back too: the tower serves what the box says it "
       "serves", has(say(&ses, "get desk1 10.0.1.10 /", &o), "HTTP"));

    /* A ZONE IS A DECISION, AND A DECISION GOES ON THE DISK.
     *
     * `dnsd <box>` used to start a name server nothing could fill: no verb
     * anywhere put a record in it, so it answered `no such host` to every
     * query in the building for the rest of the run. Now there is a verb --
     * and a name given to a server has to survive the power going off in
     * exactly the way an address does, or the tower's own resolver comes
     * back from a mains failure denying every machine in the building. */
    ck("`dns <box> <name> <ip>` gives that server a name of its own",
       has(say(&ses, "dns files store.floor1 10.0.1.77", &o),
           "store.floor1 -> 10.0.1.77"));
    ck("and says where it went, because for some boxes there is no disk",
       has(o.p, "/etc/net/services"));
    ck("`dnsd <box>` says what it will serve rather than the word `serving`",
       has(say(&ses, "dnsd files", &o), "serves 1 name"));
    say(&ses, "resolver desk1 10.0.1.10", &o);
    ck("a desk pointed at it resolves that name over real copper",
       has(say(&ses, "resolve desk1 store.floor1", &o), "10.0.1.77"));
    say(&ses, "power files off", &o);
    say(&ses, "power files on", &o);
    ck("and after the power cut the zone is still there, off the disk",
       has(say(&ses, "dnsd files", &o), "store.floor1") &&
       has(o.p, "10.0.1.77"));
    ck("and the desk still resolves it, having been told nothing",
       has(say(&ses, "resolve desk1 store.floor1", &o), "10.0.1.77"));
    /* NXDOMAIN IS AN ANSWER, AND IT IS NOT SILENCE. */
    ck("a name that server has never held is `no such name`, not `no answer`",
       has(say(&ses, "resolve desk1 nowhere.example", &o), "no such name") &&
       !has(o.p, "no answer"));

    /* And the other direction: stopping it has to survive too, or the next
     * boot starts a pool the player switched off. */
    say(&ses, "dhcpd files off", &o);
    say(&ses, "power files off", &o);
    say(&ses, "power files on", &o);
    ck("a pool the player stopped stays stopped across the power too",
       has(say(&ses, "dhcpd files", &o), "serves no addresses"));
    ck("and the box says it is serving nothing rather than pretending",
       !has(say(&ses, "show files", &o), "dhcpd  10.0.1.100"));
    buf_free(&o);
    session_end(&ses);
}

/* ================================================ four help texts, one truth
 *
 * There are four surfaces that tell a player what they can do -- the tower
 * prompt, the management line on an appliance, the shell on a machine, and
 * the README -- and a playtester found them disagreeing with each other and
 * with the machine. This checks the three that are inside the program: that
 * every verb a help text names answers where it names it, that the room name
 * the game PRINTS is a room name the game TAKES, and that the shell help
 * does not list a program that is not in the image.
 */
static void check_help(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthe help texts, against the machine they describe\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    /* ---- THE MANAGEMENT LINE. Every verb in its own help has to work on it. */
    say(&ses, "plug uplink", &o);
    Buf mh = {0};
    buf_puts(&mh, say(&ses, "help", &o));
    ck("plugging into the handoff gives a management line with its own help",
       ses.where == SES_MGMT && has(mh.p, "day ") && has(mh.p, "unplug"));

    static const char *MGMT[] = {
        "show", "links", "rooms", "demand", "day", "status", "service",
        "load", "isp", "events", "money", "where", "help", NULL
    };
    bool ok_mgmt = true, in_mgmt_help = true;
    for (int i = 0; MGMT[i]; i++) {
        if (has(say(&ses, MGMT[i], &o), "no such command")) {
            printf("    `%s` is in the management help and not on the line\n", MGMT[i]);
            ok_mgmt = false;
        }
        if (!has(mh.p, MGMT[i])) {
            printf("    the management help stopped naming `%s`\n", MGMT[i]);
            in_mgmt_help = false;
        }
    }
    ck("every verb the management help names answers on the management line",
       ok_mgmt);
    ck("and the management help still names all of them", in_mgmt_help);
    buf_free(&mh);
    say(&ses, "unplug", &o);

    /* ---- THE SHELL ON A MACHINE. `help` listed `ip` and `route`; `route` is
     * not a program in the image and never was, so a player who typed the
     * second thing the help offered got "command not found" from a real
     * shell -- which is the machine telling the truth about a help text that
     * was not. Every name below is run, and the answer has to come from the
     * program rather than from the shell failing to find it. */
    say(&ses, "buy pc probe", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry probe", &o);
    say(&ses, "go mdf", &o);
    say(&ses, "drop", &o);
    say(&ses, "power probe on", &o);
    say(&ses, "plug probe", &o);
    Buf sh = {0};
    buf_puts(&sh, say(&ses, "help", &o));
    ck("a serial lead into a running pc is a real shell, with its own help",
       ses.where == SES_SHELL && has(sh.p, "REAL SHELL"));

    static const char *PROG[] = {
        "ip", "netstat", "ping", "traceroute", "ss", "arp", "tcpdump",
        "svc", "ps", "dmesg", "cat", "man", NULL
    };
    bool exists = true, listed = true;
    for (int i = 0; PROG[i]; i++) {
        char line[64];
        snprintf(line, sizeof line, "%s", PROG[i]);
        if (has(say(&ses, line, &o), "command not found")) {
            printf("    the shell help names `%s` and the image has no such "
                   "program\n", PROG[i]);
            exists = false;
        }
        if (!has(sh.p, PROG[i])) {
            printf("    the shell help stopped naming `%s`\n", PROG[i]);
            listed = false;
        }
    }
    ck("every program the shell help names is really in the image", exists);
    ck("and the shell help names every one it is checked for", listed);
    /* The one that was wrong. It is `ip route` on this machine. */
    ck("and `route`, which the help used to offer, is honestly not found",
       has(say(&ses, "route", &o), "command not found") &&
       !has(sh.p, "  route") && has(sh.p, "ip addr | link | route | neigh"));
    buf_free(&sh);
    say(&ses, "unplug", &o);

    /* ---- THE NAME THE GAME PRINTS IS THE NAME THE GAME TAKES. */
    say(&ses, "go goods", &o);
    ck("`go MDF` works, and the prompt, `look` and `rooms` all print MDF",
       has(say(&ses, "go MDF", &o), "you walk") &&
       ses.b.rooms[ses.room].kind == RM_MDF);
    say(&ses, "go GOODS", &o);
    ck("and so does any other room the game shouts at you in capitals",
       ses.b.rooms[ses.room].kind == RM_GOODS &&
       has(say(&ses, "go F0.MDF", &o), "you walk") &&
       ses.b.rooms[ses.room].kind == RM_MDF);
    ck("a room that really is not there is still refused by name",
       has(say(&ses, "go NOWHERE", &o), "no room or box called NOWHERE"));

    buf_free(&o);
    session_end(&ses);
}

/* ============================ A REFUSAL HAS TO SAY IT REFUSED ==============
 *
 * THE BUG. `carry core` with a drum of cable in your hands answered:
 *
 *     you have a drum of cable in your hands. `spool back` puts it on the shelf.
 *
 * -- a true sentence about the world, a fix to type, and not one word saying
 * the carry had not happened. A blind playtester read it as a confirmation
 * twice. The second time they walked to the MDF, `drop` said "you are not
 * carrying anything", and by then they had cabled `core:1 files:0` to a
 * server still sitting on the floor of goods in: a wasted 36 m of cat6 and a
 * link to a box in the wrong room.
 *
 * The shape that works was already in the same verb, for a box with a cable
 * in it: `refused: it has a cable in it -- unplug it first`, then the
 * diagnostic, then the fix. Three parts, and the FIRST one is the one that
 * was missing everywhere else.
 *
 * So every refusal the tower's own verbs produce is run here and has to say
 * three things: that it was refused, what the world is instead, and something
 * to type. And it has to be TRUE that nothing happened -- a message that says
 * "refused" over a state that changed is worse than the bug it replaced, so
 * the state is compared on both sides of every line. */
static void check_refusals(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nevery refusal says the thing did not happen\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    say(&ses, "buy switch8 sw1", &o);
    say(&ses, "buy switch8 sw2", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry sw1", &o); say(&ses, "go mdf", &o); say(&ses, "drop", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry sw2", &o); say(&ses, "go mdf", &o); say(&ses, "drop", &o);

    /* Each line is one the game must refuse, run from the state the setup
     * before it puts the session in. */
    static const struct { const char *setup, *line, *why; } R[] = {
      { "spool cat6",  "carry sw1",
        "carrying a box with a drum of cable in your hands" },
      { "spool back|carry sw1", "carry sw2",
        "carrying a second box when both hands are on the first" },
      { NULL,          "plug sw2",
        "putting the cart's lead in while carrying a box" },
      { NULL,          "spool cat6",
        "taking a drum while carrying a box" },
      { "drop",        "plug sw1:0",
        "putting a cable end in with no drum in your hands" },
      { "spool cat6",  "plug sw1:99",
        "a port the box has not got" },
      { "plug sw1:0",  "spool fibre",
        "a fresh drum in the middle of a run" },
      { "spool back|spool cat6|plug sw1:0|plug sw2:0", "plug sw1:0",
        "a second end into a port that already has a cable" },
      { NULL,          "lift 9",
        "the lift to a floor nobody has put in service" },
      { NULL,          "rescue sw1",
        "the rescue stick into a box with no drive" },
      { "spool back|go goods", "cable sw1:2 sw2:2",
        "`cable` when you are standing in neither room" },
      { NULL, NULL, NULL }
    };
    bool said_no = true, has_fix = true, inert = true;
    for (int i = 0; R[i].line; i++) {
        if (R[i].setup) {
            char sc[160];
            snprintf(sc, sizeof sc, "%s", R[i].setup);
            char *p = sc;
            for (;;) {
                char *bar = strchr(p, '|');
                if (bar) *bar = 0;
                say(&ses, p, &o);
                if (!bar) break;
                p = bar + 1;
            }
        }
        /* Everything the line could have changed. */
        int was_room = ses.room, was_carry = ses.carrying, was_link = ses.s.nlink;
        int was_spool = ses.spool_kind, was_cab = ses.cab_dev;
        long was_money = ses.s.money;
        const char *a = say(&ses, R[i].line, &o);
        if (!has(a, "refused")) {
            printf("    `%s` (%s) does not say it refused:\n      %s",
                   R[i].line, R[i].why, a);
            said_no = false;
        }
        if (!strchr(a, '`')) {
            printf("    `%s` refuses and names nothing to type instead\n", R[i].line);
            has_fix = false;
        }
        /* The lift and `cable` are allowed to have walked you nowhere; none
         * of them may have moved a box, laid copper or spent a penny. */
        if (ses.carrying != was_carry || ses.s.nlink != was_link ||
            ses.s.money != was_money || ses.spool_kind != was_spool ||
            ses.cab_dev != was_cab || ses.room != was_room) {
            printf("    `%s` said no and changed the world anyway\n", R[i].line);
            inert = false;
        }
    }
    ck("every refusal in the tower's verbs says it was refused", said_no);
    ck("and every one of them names something to type next", has_fix);
    ck("and a refused line really did nothing: no box, no copper, no money",
       inert);

    /* THE ONE FROM THE TRANSCRIPT, end to end, because the harm was not the
     * wording -- it was walking off believing you had the box. */
    say(&ses, "go mdf", &o);
    say(&ses, "spool cat6", &o);
    const char *r = say(&ses, "carry sw1", &o);
    ck("the carry that started this says refused, and names the drum and the fix",
       has(r, "refused") && has(r, "drum of cable") && has(r, "spool back") &&
       ses.carrying < 0);
    ck("and the walk afterwards does not pretend you are carrying it",
       !has(say(&ses, "go goods", &o), "carrying") &&
       has(say(&ses, "drop", &o), "not carrying anything"));

    buf_free(&o);
    session_end(&ses);
}

/* ======================= THE PROMPT SAYS WHICH MACHINE ====================
 *
 * A playtester driving the running 3D over the socket: *"Inside a shell on a
 * box, `ls /` and `dmesg` are now going to a different machine and the prompt
 * is unchanged. For a text player driving this over a socket with no screen,
 * that is the single most dangerous piece of missing state."*
 *
 * session_prompt() was already right; game/scripts/wire.gd derived its own
 * from the room alone and never asked. It asks now, through ses_prompt() on
 * the extension. GDScript cannot be gated from here, so what is gated is the
 * thing wire.gd now calls: that the four places a line can be going produce
 * four visibly different prompts, and that each one NAMES the box when the
 * words are going into a box. If this ever collapses back to one string the
 * fix in wire.gd silently un-fixes itself. */
static void check_prompt(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthe prompt says which machine your words are going to\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    char body[96], mgmt[96], shell[96], desk[96];

    session_prompt(&ses, body, sizeof body);
    ck("standing in a room, the prompt is the floor and the room",
       has(body, "MDF") && has(body, "f0"));

    say(&ses, "plug uplink", &o);
    session_prompt(&ses, mgmt, sizeof mgmt);
    ck("on an appliance's management line it names the appliance",
       ses.where == SES_MGMT && has(mgmt, "uplink") && strcmp(mgmt, body) != 0);
    say(&ses, "unplug", &o);

    say(&ses, "buy pc probe", &o);
    say(&ses, "go goods", &o); say(&ses, "carry probe", &o);
    say(&ses, "go mdf", &o); say(&ses, "drop", &o);
    say(&ses, "power probe on", &o);
    say(&ses, "plug probe", &o);
    session_prompt(&ses, shell, sizeof shell);
    ck("at a real shell on a real machine it names THAT machine",
       ses.where == SES_SHELL && has(shell, "probe") &&
       strcmp(shell, body) != 0 && strcmp(shell, mgmt) != 0);
    printf("    body %-16s mgmt %-16s shell %s\n", body, mgmt, shell);

    /* The exact failure the playtester hit: the words go somewhere else and
     * the line in front of them is the same three characters. */
    ck("so a client with no screen can tell a shell from a room",
       strcmp(shell, body) != 0);
    say(&ses, "unplug", &o);
    say(&ses, "desk", &o);
    session_prompt(&ses, desk, sizeof desk);
    ck("and the desk is a fourth prompt, not the third one again",
       strcmp(desk, body) != 0 && strcmp(desk, shell) != 0);

    buf_free(&o);
    session_end(&ses);
}

/* ================= THE OPENING TEXT COUNTS, IT DOES NOT PROMISE ===========
 *
 * `help` opened with *"On day one it holds exactly one thing: the ISP's
 * socket on the wall of the MDF."* True of a session started over the socket
 * and FALSE of the one the 3D window starts, which pre-orders a router, a
 * switch24 and a server into goods in and has spent 2400 doing it. A
 * playtester believed the sentence, re-bought two of the three, and lost
 * another 1050 -- and there is no `sell`, so it stayed lost.
 *
 * The fix is not a better sentence: two starting states cannot both be
 * described by one constant. The help WALKS THE DEVICE TABLE. So this check
 * runs it in both worlds -- empty, and with the 3D's own three lines typed --
 * and the answer has to be right in each. */
static void check_inventory(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthe opening text counts what is there rather than promising\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    const char *h = say(&ses, "help", &o);
    ck("in a session that really is empty, the help says so and names nothing "
       "else",
       has(h, "uplink") && !has(h, "switch24  in") && has(h, "0 of the budget"));
    ck("and it warns that money does not come back, because there is no `sell`",
       has(say(&ses, "help", &o), "NO `sell`"));

    /* game/scripts/tower.gd's _ses_start(), line for line. A keyboard player
     * in the window cannot type `buy`, so the delivery is how they get any
     * kit at all -- it stays, and the text stops lying about it. */
    say(&ses, "order router edge", &o);
    say(&ses, "order switch24 core", &o);
    say(&ses, "order server files", &o);
    long spent = ses.s.spent;
    ck("the 3D window's own starting kit costs 2400 and is already paid for",
       spent == 2400);

    h = say(&ses, "help", &o);
    ck("and now the same help names all three, where they are, and what each cost",
       has(h, "edge") && has(h, "core") && has(h, "files") &&
       has(h, "goods in") && has(h, "650 already paid") &&
       has(h, "1350 already paid"));
    ck("and it says how much of the budget has already gone on them",
       has(h, "2400 of the budget"));

    /* AND THE PLAYER WHO NEVER TYPES `help`. The intro is the first thing a
     * blind tester reads and it named goods in without saying what was in it. */
    say(&ses, "desk", &o);
    const char *in = say(&ses, "tower", &o);
    ck("the way in names the delivery already on the floor of goods in",
       has(in, "ALREADY A DELIVERY") && has(in, "edge") && has(in, "files") &&
       has(in, "do not order those again"));

    buf_free(&o);
    session_end(&ses);
}

/* ==================== `cable` PUTS YOU BACK WHERE YOU ASKED ===============
 *
 * *"I queued seven fibre runs from the MDF; the first succeeded and walked me
 * to f1, and the other six all failed with 'you are in neither room'."*
 *
 * `cable` is a macro for four things a person does, and the fourth left them
 * standing at the far end -- so the macro itself made every subsequent use of
 * it impossible without an interleaved `go`. It walks back now, and it pays
 * for the walk both ways, which is what a person laying six runs out of one
 * cupboard really does with their legs. */
static void check_cable_batch(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nsix runs out of one cupboard, without a `go` in between\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *SETUP[] = {
        "buy switch24 core", "go goods", "carry core", "go mdf", "drop", NULL
    };
    for (int i = 0; SETUP[i]; i++) say(&ses, SETUP[i], &o);
    open_next_floor(&ses, &o);
    say(&ses, "go mdf", &o);
    int mdf = ses.room;

    /* Three desks upstairs, so the far end is genuinely another room. */
    int made = 0;
    for (int i = 0; i < 3; i++) {
        char nm[16], c[64];
        snprintf(nm, sizeof nm, "pc%d", i);
        snprintf(c, sizeof c, "buy pc %s", nm);
        say(&ses, c, &o);
        say(&ses, "go goods", &o);
        snprintf(c, sizeof c, "carry %s", nm);
        say(&ses, c, &o);
        say(&ses, "go f1.comms", &o);
        say(&ses, "drop", &o);
        made++;
    }
    say(&ses, "go mdf", &o);
    ck("three boxes carried into the cupboard upstairs, and you back in the MDF",
       made == 3 && ses.room == mdf);

    long walked = ses.walked;
    bool all = true, home = true;
    for (int i = 0; i < 3; i++) {
        char c[64];
        snprintf(c, sizeof c, "cable core:%d pc%d:0 cat6", i, i);
        const char *a = say(&ses, c, &o);
        if (!has(a, "the port comes up")) {
            printf("    run %d did not come up: %s", i, a);
            all = false;
        }
        if (ses.room != mdf) {
            printf("    run %d left you in room %d, not the MDF\n", i, ses.room);
            home = false;
        }
    }
    ck("three runs typed one after another from the MDF, and all three come up",
       all && ses.s.nlink == 3);
    ck("because every one of them walks you back to the room you typed it in",
       home && ses.room == mdf);
    ck("and the walk back is charged, both ways: it is legs, not a teleport",
       ses.walked > walked);
    printf("    three runs out of one cupboard cost %ld m of walking\n",
           ses.walked - walked);

    /* THE DRUM DOES NOT REFILL ITSELF BETWEEN RUNS.
     *
     * `cable a b cat5e` names the grade on every line, and naming it took a
     * fresh drum off the shelf -- so a drum with 288 m left on it printed
     * "you have 305 m of cat5e on the spool" and then took the run off 305.
     * `spool` afterwards agreed with the new number, so the state was
     * self-consistent and the count was still a lie, six lines in a row. The
     * drum is free (it is the RUN that is charged, and D27 says so) and it
     * is meant to be: what it may not do is quietly reset. */
    say(&ses, "spool back", &o);
    say(&ses, "spool cat5e", &o);
    int start = ses.spool_left;
    ck("a fresh drum is a whole drum", start > 0);
    say(&ses, "buy pc pcx", &o);
    say(&ses, "go goods", &o); say(&ses, "carry pcx", &o);
    say(&ses, "go f1.comms", &o); say(&ses, "drop", &o);
    say(&ses, "go mdf", &o);
    const char *again = say(&ses, "cable core:9 pcx:0 cat5e", &o);
    ck("naming the grade you are already holding is not a trip to the store",
       has(again, "you already have the cat5e drum") &&
       !has(again, "you have 305 m"));
    ck("and the metres came off the drum that was in your hands",
       ses.spool_left > 0 && ses.spool_left < start);
    ck("so `spool` and the run agree about what is left",
       has(say(&ses, "spool", &o), "m of cat5e on the spool"));
    {
        char want[64];
        snprintf(want, sizeof want, "%d m of cat5e", ses.spool_left);
        ck("and the number it prints is the number it counted down to",
           has(o.p, want));
    }
    /* Asking for a DIFFERENT grade really is a trip to the store, and it
     * says the old drum went back rather than pretending it never existed. */
    const char *swap = say(&ses, "spool cat6", &o);
    ck("a different grade swaps the drum, out loud",
       has(swap, "cat5e drum goes back on the shelf") &&
       has(swap, "m of cat6 on the spool") && ses.spool_left == start);

    buf_free(&o);
    session_end(&ses);
}

/* ---------------------------- a floor server on vlans, across the mains
 *
 * THE WORST THING A PLAYTEST HAS FOUND IN THIS GAME. Every `addr`, `gw` and
 * `subif` prints "(written onto its disk: it has an OS and netd reads that
 * file)". After a mains failure and a repair, a floor server came back with
 * `cat /etc/net/interfaces` naming an address its own kernel did not have,
 * no subinterfaces at all, and every DHCP pool gone -- on the morning of a
 * blackout, which is the morning you are least able to spare it.
 *
 * Two mechanisms, and this scenario needs both:
 *
 *   1. sync_disk wrote `iface eth0` and nothing else, so the subinterfaces
 *      and their addresses lived in memory and went with the power.
 *   2. netsite's attach() skips its work when the files it watches have not
 *      changed -- but switching a box OFF empties its node from outside that
 *      file, and the disk is unchanged, so the box came back having applied
 *      nothing at all. THAT is why the shell below is opened before the
 *      power cut and not after: a session that never made a syscall on the
 *      box leaves the cache cold and the bug hidden, which is exactly how
 *      the existing power-cut scenario passed while a player was losing
 *      three tenancies' rent to it.
 */
static void check_vlan_server_reboot(int *passed, int *total)
{
    P = passed; T = total;
    printf("\na floor server on vlan subinterfaces, across a power cut\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    static const char *SCRIPT[] = {
        "buy switch24 core", "buy server srv2", "buy pc desk1",
        "go goods", "carry core",  "go mdf", "drop",
        "go goods", "carry srv2",  "go mdf", "drop",
        "go goods", "carry desk1", "go mdf", "drop",
        "cable core:1 srv2:0 cat6",
        "cable core:3 srv2:1 cat6",
        "cable core:2 desk1:0 cat6",
        "power srv2 on",
        "power desk1 on",
        "addr srv2 10.12.0.10/24",
        "gw srv2 10.12.0.1",
        "subif srv2 1 13 10.13.0.1/24",
        "trunk core 3 13",
        "vlan core 2 13",
        "dhcpd srv2 10.13.0.100 20 24 10.13.0.1 10.13.0.1",
        "httpd srv2",
        NULL
    };
    for (int i = 0; SCRIPT[i]; i++) say(&ses, SCRIPT[i], &o);

    ck("a pool on a vlan subinterface serves a desk on that vlan",
       has(say(&ses, "dhcp desk1", &o), "10.13.0.100"));
    ck("and the box says which leg it is answering on",
       has(say(&ses, "dhcpd srv2", &o), "eth1.13"));

    /* THE DISK, READ ON THE BOX'S OWN CONSOLE. This is the claim `subif`
     * makes every time it is typed. */
    say(&ses, "plug srv2", &o);
    const char *conf = say(&ses, "cat /etc/net/interfaces", &o);
    ck("the subinterface is on its own disk, where `subif` said it put it",
       has(conf, "iface eth1.13") && has(conf, "10.13.0.1"));
    ck("and so is the card underneath it, with the gateway",
       has(conf, "iface eth0") && has(conf, "10.12.0.10") &&
       has(conf, "gateway 10.12.0.1"));
    /* A real syscall, which is what warms the cache the bug hid behind. */
    ck("and the kernel in it agrees, having applied the same file",
       has(say(&ses, "ip addr", &o), "eth1.13") && has(o.p, "10.13.0.1/24"));
    say(&ses, "unplug", &o);

    /* THE MAINS GOES. Off, on, and nothing else typed. */
    say(&ses, "power srv2 off", &o);
    say(&ses, "power srv2 on", &o);

    const char *sh = say(&ses, "show srv2", &o);
    ck("it comes back with the address its own disk names",
       has(sh, "10.12.0.10/24"));
    ck("and with the subinterface, which nothing else in the world remembered",
       has(sh, "eth1.13") && has(sh, "10.13.0.1/24"));
    ck("and serving the pool that was riding on it",
       has(say(&ses, "dhcpd srv2", &o), "10.13.0.100-10.13.0.119") &&
       has(o.p, "eth1.13"));
    ck("and a desk that asks gets its address back, which is the whole point",
       has(say(&ses, "dhcp desk1", &o), "10.13.0.100"));

    /* AND `look` HAS TO SEE AN ADDRESS THAT IS NOT ON eth0. It read
     * interface 0 and nothing else, so the very machine D27 recommends
     * building was listed in its own room as "no address" while `show` two
     * lines later printed the address it plainly had. */
    say(&ses, "buy server srv3", &o);
    say(&ses, "spool back", &o);          /* both hands, and one box */
    say(&ses, "go goods", &o); say(&ses, "carry srv3", &o);
    say(&ses, "go mdf", &o); say(&ses, "drop", &o);
    say(&ses, "power srv3 on", &o);
    say(&ses, "subif srv3 0 13 10.13.0.5/24", &o);
    const char *lk = say(&ses, "look", &o);
    ck("`look` sees an address on a subinterface, and names the interface",
       has(lk, "10.13.0.5/24 on eth0.13") && !has(lk, "no address"));

    /* AND A SUBINTERFACE CAN BE TAKEN AWAY, which no verb could do: a
     * playtester had to park stale ones on an unused subnet to stop `dhcpd`
     * binding to them. */
    const char *off = say(&ses, "subif srv2 1 13 off", &o);
    ck("`subif <box> <nic> <vlan> off` takes one away and says what went",
       has(off, "eth1.13 is gone") && has(off, "card underneath"));
    ck("and the pool that was answering on it went with it, not on paper only",
       has(say(&ses, "dhcpd srv2", &o), "serves no addresses"));
    ck("and it stays gone across the power, because the disk lost it too",
       (say(&ses, "power srv2 off", &o), say(&ses, "power srv2 on", &o),
        !has(say(&ses, "show srv2", &o), "eth1.13")) &&
       has(o.p, "10.12.0.10/24"));

    buf_free(&o);
    session_end(&ses);
}

/* ============ THE HELP'S CLAIMS, AGAINST THE MACHINE THAT MAKES THEM ======
 *
 * Five things a playtester had to read the C source to find out, each of
 * which cost them real time. Documenting them is half the job; the other half
 * is that the documentation must be TRUE, so where a claim can be run, this
 * runs it and compares.
 *
 * ONE OF THE FIVE WAS WRONG, and it was the playtester's own diagnosis:
 * *"The three-day clock appears to start when a tenancy moves in... so a
 * tenancy that arrives on a day you are not standing in its comms cupboard is
 * one day from a complaint before you can do anything."* It does not. The
 * strike branch in core/siteday.c is gated on `t->tried > 0 || t->strikes > 0`
 * and `tried` only counts work attempted by a desk with LINK *and* an
 * ADDRESS. A tenancy you have never touched cannot be struck at all. That is
 * checked below by playing it, because the fix was to write the true rule
 * down and a true rule nobody gates is a rule that drifts. */
static void check_documented(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nwhat the help now explains, run against the machine\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    Buf h = {0};
    buf_puts(&h, say(&ses, "help", &o));

    /* ---- `serve`, which was one line for the most important verb in the game. */
    ck("`serve` explains the ports it takes, the default cable and the vlan",
       has(h.p, "NEXT FREE PORT") && has(h.p, "defaults to CAT5E") &&
       has(h.p, "does NOT need the vlan") && has(h.p, "nowhere to go"));

    /* ---- DHCP across vlans, which was unbuildable without reading netstack.c. */
    ck("`dhcpd` explains that a pool binds to the interface on that subnet",
       has(h.p, "ONE POOL PER SEGMENT") &&
       has(h.p, "SEVERAL VLANS BY BEING TOLD"));

    /* ---- The drum. Free to take, charged by the metre when the link is made. */
    ck("the spool says the drum is free and the run is not",
       has(h.p, "DRUM IS FREE AND THE RUN IS NOT"));
    long had = ses.s.money;
    say(&ses, "spool cat6", &o);
    ck("and that is true: taking a drum costs nothing", ses.s.money == had);
    say(&ses, "spool back", &o);
    ck("and putting it back refunds nothing, because nothing was charged",
       ses.s.money == had);

    /* ---- Rent, and why `status` can read 0 with the building full of desks. */
    ck("the help says rent is paid per served day and needs an address",
       has(h.p, "RENT IS PAID FOR A DAY'S WORK") &&
       has(h.p, "four fifths") && has(h.p, "LINK *and* an ADDRESS"));

    /* ---- The strike clock.
     *
     * THIS SCENARIO USED TO ASSERT THE OPPOSITE, and it was rewritten in D27
     * rather than weakened. It ran the clock twenty days past a move-in with
     * no copper anywhere in the building and checked that NOTHING happened:
     * "a tenancy you have never cabled takes no strike, however long you
     * leave it". That was the code's behaviour and it made the service half
     * of the game optional -- an agent ran two hundred days with seven
     * tenancies moved in and unserved, drew no complaint, and had to go
     * overdrawn deliberately to see a run end. So the rule changed: a
     * tenancy gets three days of fit-out and is then struck like anybody
     * else. What is asserted here is strictly more than before -- the grace
     * period is real AND it ends AND the ending is a complaint. */
    ck("the help says a tenancy nobody cabled is struck after its fit-out",
       has(h.p, "THREE\nDAYS of fit-out") && has(h.p, "strikes IN A ROW") &&
       has(h.p, "sixth day after they move"));
    buf_free(&h);

    int ti = -1;
    for (int guard = 0; guard < 400 && ti < 0; guard++) {
        say(&ses, "day 1", &o);
        for (int i = 0; i < ses.s.ntenant; i++)
            if (ses.s.tenant[i].moved) { ti = i; break; }
    }
    if (ti < 0) { ck("a tenancy moves in within four hundred days", false); goto done; }
    {
        /* The morning they move in, and for their fit-out, nobody is on the
         * phone. Their move-in day is the day the session is on right now. */
        int in_day = ses.s.tenant[ti].day;
        ck("the day they move in, a tenancy nobody has cabled has no strike",
           ses.s.day == in_day && ses.s.tenant[ti].strikes == 0);
        say(&ses, "day 3", &o);
        ck("and three days of fit-out later it still has none",
           ses.s.tenant[ti].strikes == 0 && ses.s.tenant[ti].complained == 0);
        say(&ses, "day 1", &o);
        ck("the fourth day with not one desk able to work is a strike",
           ses.s.tenant[ti].strikes == 1);
        say(&ses, "day 2", &o);
        ck("and the sixth day is a complaint, so ignoring a tenancy costs",
           ses.s.tenant[ti].complained == 1 && ses.s.complaints >= 1);
        int struck = 0;
        for (int i = 0; i < ses.s.ntenant; i++)
            if (ses.s.tenant[i].moved && ses.s.tenant[i].strikes) struck++;
        printf("    %d tenancies moved into a building with no copper in it; "
               "%d have a strike\n", ti + 1, struck);
    }
    /* And rent really is zero, which is the other half of the same fact and
     * the thing that read as a bug. */
    ck("and nothing was taken in rent, because nobody did a day's work",
       ses.s.rent_taken == 0);

done:
    buf_free(&o);
    session_end(&ses);
}

int session_selfcheck(int *passed, int *total)
{
    check_verbs(passed, total);
    check_help(passed, total);
    check_walking(passed, total);
    check_reach(passed, total);
    check_goods(passed, total);
    check_build(passed, total);
    check_dangling(passed, total);
    check_tenant_kit(passed, total);
    check_booted(passed, total);
    check_power(passed, total);
    check_services(passed, total);
    check_refusals(passed, total);
    check_prompt(passed, total);
    check_inventory(passed, total);
    check_cable_batch(passed, total);
    check_vlan_server_reboot(passed, total);
    check_documented(passed, total);
    return 0;
}
