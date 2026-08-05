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

    int before = ses.floors;
    ck("`open` puts the next floor in service and says what is on it",
       has(say(&ses, "open", &o), "in service") && ses.floors == before + 1);

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
    say(&ses, "open", &o);
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

int session_selfcheck(int *passed, int *total)
{
    check_verbs(passed, total);
    check_walking(passed, total);
    check_reach(passed, total);
    check_goods(passed, total);
    check_build(passed, total);
    return 0;
}
