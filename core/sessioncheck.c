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
#include <stdlib.h>
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
/* ================================== POWER, FOR A GATE NOT MEASURING POWER
 *
 * Every tower in this file is built by typing -- `buy`, `deliver`, `drop` --
 * and every one of them is measuring something else: a lease, a vlan, a
 * console, a day's rent. Power used to come free with the room: a box put
 * down took one of that room's wall sockets and came up. It comes down a run
 * you pull now, and a gate that had to design a power tree before it could
 * ask about a DHCP pool would be a gate nobody could read.
 *
 * So after every line, anything standing in the building with nothing feeding
 * it gets a run, refunded -- the price of power is measured in
 * check_conduits() in core/sitecheck.c and nowhere else, and every money
 * assertion in this file was written against a tower where power cost
 * nothing. site_feed() is the player's own call and takes the player's own
 * refusals; what is skipped is only the typing.
 *
 * AND ONE GATE TURNS IT OFF, because one gate is about exactly this: a box
 * with no power in it, the owner's own "if it's not booting, it shouldn't
 * offer a prompt at all". check_dead_console() sets AUTOPOWER false, pulls
 * its own run when it is ready, and puts it back. */
static bool AUTOPOWER = true;

static void autopower(Session *ses)
{
    if (!AUTOPOWER) return;
    Site *s = &ses->s;
    long money = s->money, spent = s->spent;
    for (int i = 0; i < s->ndev; i++) {
        int k = s->dev[i].kind;
        if (k == SDEV_UPLINK || k == SDEV_POWERCORE || k == SDEV_DESK ||
            k == SDEV_STRIP) continue;
        if (site_dev_fed(s, i, NULL)) continue;
        if (site_feed(s, i) < 0 && s->err == SITE_ENODEV) {
            char sn[NET_NAME_MAX];
            snprintf(sn, sizeof sn, "gs%d", s->ndev);
            int st = site_install(s, SDEV_STRIP, s->dev[i].room, sn);
            if (st >= 0 && site_feed(s, st) >= 0) site_feed(s, i);
        }
    }
    s->money = money;
    s->spent = spent;
}

static const char *say(Session *ses, const char *line, Buf *o)
{
    buf_clear(o);
    session_line(ses, line, o);
    autopower(ses);
    if (!o->len) buf_puts(o, "");
    return o->p ? o->p : "";
}

static bool has(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

/* THE MACHINE THE PLAYER SITS AT, found the way a player finds it: by the name
 * printed in `look`. core has site_workstation() for this and it is used
 * everywhere else; this gate asks by name deliberately, so that the whole
 * section below COMPILES against a tree that has no workstation in its model
 * and fails there on the assertions rather than at the linker. D37's power
 * gate was written the same way and for the same reason. */
static int ws_dev(const Session *ses)
{
    for (int i = 0; i < ses->s.ndev; i++)
        if (strcmp(ses->s.dev[i].name, "ws") == 0) return i;
    return -1;
}

/* ONE BOX'S LINE OUT OF A ROOM FULL OF THEM. `look` prints a line per device,
 * and since D41 the MDF has the player's own workstation standing in it and
 * running -- so "does this output claim an OS is running" stopped being a
 * question about the box under test. Copies the line `name` starts, so the
 * assertion is about that box and no other. */
static const char *dev_row(const char *out, const char *name, char *buf, size_t cap)
{
    buf[0] = 0;
    if (!out) return buf;
    char want[80];
    snprintf(want, sizeof want, "    %s ", name);
    const char *p = strstr(out, want);
    if (!p) return buf;
    const char *e = strchr(p, '\n');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= cap) n = cap - 1;
    memcpy(buf, p, n);
    buf[n] = 0;
    return buf;
}

/* The tray metres out of a quote, read back out of the words the player
 * reads rather than out of a variable this file also set. */
static int metres_of(const char *s)
{
    const char *p = s ? strstr(s, " m through the tray") : NULL;
    if (!p) return -1;
    while (p > s && p[-1] >= '0' && p[-1] <= '9') p--;
    return atoi(p);
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

    /* AND WITH YOUR OWN MACHINE, in the same room, cabled to that handoff.
     * Two devices on the first morning and the player bought neither. */
    ck("you start in the Engineering, on the ground deck, with the ISP handoff",
       ses.b.rooms[ses.room].kind == RM_MDF && ses.b.rooms[ses.room].floor == 0 &&
       ses.s.ndev == 3 && ses.s.dev[ses.s.uplink].room == ses.room);
    ck("and the machine you sit at is standing in that room too",
       ws_dev(&ses) >= 0 && ses.s.dev[ws_dev(&ses)].room == ses.room &&
       has(say(&ses, "look", &o), "workstation"));

    static const char *VERB[] = {
        "where", "look", "map", "go", "lift", "open", "buy", "carry", "drop",
        "spool", "plug", "unplug", "cable", "uncable", "show", "links", "money",
        "demand", "rooms", "frames", "help",
        /* THE COUNTERPART TO THE SPOOL, which D23 and the README both sold
         * and the tower did not have: the first blind playtester of it went
         * looking for the verb and there was none. */
        "jack", "patch", "jacks",
        /* D31: the people at the desks, and the chair you can sit in. */
        "desks", "sit", "stand",
        /* D37 and after: the plug, and the tree it now hangs off. `outlet`
         * and `outlets` were here and are gone with the wall they were about;
         * what replaced them is a run you pull and a page that says what each
         * run is carrying. Every action the 3D window can offer over a power
         * lead has to have a word here. */
        "mains", "conduit", "unconduit", "conduits", "feed", NULL
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

    /* On the first morning every hole in the building is full: the handoff
     * has one and the workstation is in it, on the lead the building came
     * with. So the port an agent can name without seeing it is that one, and
     * `look` has to say both halves -- that it is used, and that it is still
     * where the first switch goes. A player who read "all 1 ports used" and
     * nothing else would conclude, reasonably and wrongly, that there is
     * nowhere in this building to plug anything in. */
    ck("`look` names a port an agent can address without seeing it",
       has(say(&ses, "look", &o), "all 1 ports used") &&
       has(o.p, "uplink:0 has the lead the building came with in it, to ws") &&
       has(o.p, "ws:0 has the lead the building came with in it, to uplink"));
    ck("and a port named that way really takes a cable",
       has(say(&ses, "spool cat5e", &o), "m of cat5e") &&
       has(say(&ses, "plug uplink:0", &o), "one end into uplink port 0"));
    say(&ses, "spool back", &o);

    ck("`where` says the deck, the room, the money and the metres walked",
       has(say(&ses, "where", &o), "Engineering") && has(o.p, "walked") &&
       has(o.p, "decks in service"));

    ck("`demand` ends by doing the arithmetic on what the tower will need",
       has(say(&ses, "demand", &o), "drops in all") &&
       has(o.p, "twenty-four port switches"));

    /* AND THE ARITHMETIC USES THE PORTS A SWITCH REALLY SEATS DESKS ON.
     *
     * It divided by 23 -- every port but one, kept for the riser -- and since
     * D27 ports 22 and 23 of a switch24 are its SFP+ pair, the only ten
     * gigabit holes in the building and where the riser and the floor's
     * server want to be. A playtester trusted the footer, bought two
     * switches for a floor that needed three, and found out when `serve`
     * stopped halfway. The number is derived from site_kind_ports() here so
     * that a change to the catalogue cannot leave the planning advice behind.
     */
    {
        char want[80];
        snprintf(want, sizeof want, "a switch24 seats %d desks",
                 site_kind_ports(SDEV_SWITCH24) - 2);
        const char *d = say(&ses, "demand", &o);
        ck("and says how many desks a switch really seats, from the catalogue",
           has(d, want));
        ck("and warns that `serve` will spend the SFP+ pair on desks",
           has(d, "fills from port 0 up"));
    }

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
        "get", "httpd", "dnsd", "ups", "disk",
        "jack", "patch", "jacks",
        /* D32. What a run would cost, before the money leaves. Both halves
         * matter here too: the tower help has to NAME it, and it has to
         * answer at the prompt -- which is where a player is standing, with
         * a drum in their hands, when they want it. */
        "quote",
        /* D31. The verb that walks you round to the complainant's side of the
         * problem. It is in this list rather than the one above because both
         * halves matter: the tower help has to NAME it, and it has to answer
         * where the help says it does. */
        "desks", "sit", "stand",
        /* D37. The wall. Same two directions and the same reason: a player
         * standing over a box that will not switch on has to be able to find
         * the words for the plug in the page they are already reading. */
        "mains", "conduit", "conduits", "feed", NULL
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
    /* TWO VIEWS OF THE NUMBER THAT ENDS YOUR RUN, AND THEY HAVE TO AGREE.
     * `status` said "Three ends the run" from a literal while `service`
     * computed it, so a player with fourteen tenancies read three in one
     * place and five in the other, about the same rule, in the same session.
     * Both are asked here, and both are compared against the function. */
    {
        char want[64];
        snprintf(want, sizeof want, "%d ends the run",
                 site_complaints_allowed(&ses.s));
        ck("`status` prints the complaint threshold the model actually uses",
           has(say(&ses, "status", &o), want));
        snprintf(want, sizeof want, "%d filed complaints ends the run",
                 site_complaints_allowed(&ses.s));
        ck("and `service` prints the same number as `status`",
           has(say(&ses, "service", &o), want));
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

    /* A RISER IS A ROOM YOU CAN WALK INTO, AND THE LADDER IS THE WAY UP.
     *
     * It used to be a shaft nobody could enter, and this gate asserted the
     * refusal. The owner walked into one in the window -- the 3D has drawn
     * its doorway all along -- and found it empty: "there's a room in called
     * riser, that seems to be an empty elevator shaft... potentially the
     * riser room should be left kind of a corridor where you run cables. But
     * with a ladder so you can actually climb up and down."
     *
     * So the model agrees with the window now. What is asserted instead is
     * the thing that makes the ladder honest: it is the DEAREST way between
     * two floors, in walked metres, because you climb it a rung at a time.
     * It is there for somebody who is already in the riser following a cable,
     * not as a route anybody would send a journey through -- and the numbers
     * say that rather than a rule saying it. */
    int riser = -1;
    for (int i = 0; i < ses.b.nrooms; i++)
        if (ses.b.rooms[i].kind == RM_RISER) { riser = i; break; }
    char cmd[64];
    snprintf(cmd, sizeof cmd, "go #%d", riser);
    say(&ses, cmd, &o);
    ck("you can walk into a riser, and end up standing in it",
       riser >= 0 && ses.room == riser);

    /* the riser above it, and the stairwell above the stairwell, measured on
     * the same building with the same walker */
    int riser_up = -1, stair0 = -1, stair_up = -1;
    for (int i = 0; i < ses.b.nrooms; i++) {
        if (ses.b.rooms[i].floor != 1) continue;
        if (ses.b.rooms[i].kind == RM_RISER) riser_up = i;
        if (ses.b.rooms[i].kind == RM_STAIR) stair_up = i;
    }
    for (int i = 0; i < ses.b.nrooms; i++)
        if (ses.b.rooms[i].floor == 0 && ses.b.rooms[i].kind == RM_STAIR) stair0 = i;
    double *dm = nom_alloc(sizeof(double) * (size_t)ses.b.nrooms);
    double ladder = BLD_INF, stairs = BLD_INF;
    if (riser >= 0 && riser_up >= 0 && bld_walk_all(&ses.b, riser, dm))
        ladder = dm[riser_up];
    if (stair0 >= 0 && stair_up >= 0 && bld_walk_all(&ses.b, stair0, dm))
        stairs = dm[stair_up];
    nom_free(dm);
    ck("the deck above is reachable up the ladder", ladder < BLD_INF);
    ck("and the ladder is dearer than the stairs, per storey",
       ladder < BLD_INF && stairs < BLD_INF && ladder > stairs);

    int was = ses.room;
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
    ck("a room kind on this deck is a spelling of that room",
       ses.b.rooms[ses.room].kind == RM_MDF && ses.walked > far);

    /* The lift, which is the one thing `open` gates. Same words as lift.gd.
     * NOT THE TOP DECK ANY MORE, and the reason is the whole of the bridge:
     * the top of the station is in service on day one because the crew are
     * already sitting on it. A dark deck is one of the ones in the MIDDLE. */
    int top = ses.b.floors - 1;
    int dark = ses.floors;          /* the next one up, and nobody has paid */
    if (dark >= top) dark = -1;
    if (dark > 0) {
        snprintf(cmd, sizeof cmd, "lift %d", dark);
        ck("the lift refuses a deck nobody has put in service",
           has(say(&ses, cmd, &o), "not in service") && has(o.p, "not lit"));
    }

    /* THE BRIDGE, WHICH IS THE POINT. Day one, no money spent, and the lift
     * stops at the top of the station -- because the crew are up there at
     * consoles with nothing plugged into them, and the run from Engineering
     * to the top of the riser is the first job in the game. If this ever
     * says "not in service" then the bridge is a label again. */
    long before_lift = ses.walked;
    snprintf(cmd, sizeof cmd, "lift %d", top);
    ck("the bridge is in service on day one, with every deck under it dark",
       ses_deck_open(&ses, top) && top == ses_bridge_deck(&ses) &&
       !has(say(&ses, cmd, &o), "not in service") &&
       ses.b.rooms[ses.room].floor == top && ses.walked > before_lift);
    ck("and it is the bridge that is up there, not another deck of offices",
       ses.b.fkind[top] == FL_BRIDGE &&
       bld_find(&ses.b, top, RM_BRIDGE) >= 0);
    ck("nobody rents the bridge: it is not let space and never was",
       bld_find(&ses.b, top, RM_OFFICE) < 0 &&
       ses.b.rooms[bld_find(&ses.b, top, RM_BRIDGE)].tenant == 0);
    /* Back down to where the rest of this check thinks it is standing. */
    snprintf(cmd, sizeof cmd, "lift %d", 0);
    say(&ses, cmd, &o);
    snprintf(cmd, sizeof cmd, "go #%d", dst);
    say(&ses, cmd, &o);

    snprintf(cmd, sizeof cmd, "lift %d", ses.b.floors + 4);
    ck("and a deck the building has not got",
       has(say(&ses, cmd, &o), "does not pass deck"));

    /* A FLOOR COMING INTO SERVICE COSTS SOMETHING AND HAPPENS SOMEWHERE.
     * `open` used to be free and typeable from anywhere, so there was no
     * reason not to open the whole tower in the first minute. */
    int before = ses.floors;
    long had = ses.s.money;
    ck("`open` from another deck is refused, and says which stairs to take",
       has(say(&ses, "open", &o), "not in service and you are on deck") &&
       has(o.p, "the stairs") && has(o.p, "it will cost") &&
       ses.floors == before && ses.s.money == had);

    snprintf(cmd, sizeof cmd, "go #%d", bld_find(&ses.b, before, RM_STAIR));
    say(&ses, cmd, &o);
    ck("and standing on it is a walk up the stairs, charged in metres",
       ses.b.rooms[ses.room].floor == before && ses.walked > far);

    long walked_up = ses.walked;
    ck("`open` puts the next deck in service and says what is on it",
       has(say(&ses, "open", &o), "in service") && ses.floors == before + 1);
    ck("and it is paid for: the landlord's fit-out comes out of the budget",
       ses.s.money < had && ses.s.spent >= had - ses.s.money);
    printf("    deck %d cost %ld to commission, and %ld m of stairs\n",
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
    /* The handoff and the workstation were both there before this; the switch
     * is the first thing anybody bought. */
    int bought = site_dev_by_name(&ses.s, "sw1");
    /* Four devices now: the handoff, the workstation, the power core and the
     * switch -- and only the last of those was bought. */
    ck("kit is charged for and delivered to goods in, not to your feet",
       ses.s.ndev == 4 && bought > 0 && ses.s.money == 100000 - 120 &&
       ses.s.dev[bought].room == (uint16_t)site_goods_room(&ses.s) &&
       ses.s.dev[bought].room != (uint16_t)ses.room);

    say(&ses, "go goods", &o);
    say(&ses, "carry sw1", &o);
    say(&ses, "go mdf", &o);
    say(&ses, "drop", &o);
    /* THE BOX BY NAME, NOT BY INDEX. `dev[1]` was the workstation when there
     * were two given devices and is the power core now that there are three:
     * a test that counts on the order of a list is a test that breaks the
     * next time anything is added to the front of it. */
    ck("and carrying it to the Engineering is what puts it in the Engineering",
       ses.room == mdf && ses.s.dev[bought].room == (uint16_t)mdf);

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
    ck("the tower has a goods in, on the ground deck, and it is not the Engineering",
       goods >= 0 && ses.b.rooms[goods].kind == RM_GOODS &&
       ses.b.rooms[goods].floor == 0 && goods != ses.room);

    /* Order it from the top of the building. It still lands downstairs. */
    open_next_floor(&ses, &o);
    say(&ses, "lift 2", &o);
    say(&ses, "go comms", &o);
    int up = ses.room;
    const char *bought = say(&ses, "buy switch24 core", &o);
    int d = site_dev_by_name(&ses.s, "core");
    ck("a box ordered from deck two is delivered to the ground deck",
       d > 0 && ses.s.dev[d].room == (uint16_t)goods && ses.room == up &&
       has(bought, "goods in"));
    ck("and the answer says how far away that is, in metres of building",
       has(bought, "m from here") && has(bought, "carry core"));

    ck("it is not where you are, so you cannot reach it",
       has(say(&ses, "addr core 10.0.1.1/24", &o), "and you are not"));
    ck("nor carry it from another deck",
       has(say(&ses, "carry core", &o), "and you are not") && ses.carrying < 0);

    /* Fetch it. */
    say(&ses, "go goods", &o);
    ck("`go goods` finds the delivery from anywhere in the tower",
       ses.room == goods);
    ck("and it is in that room, which is where `look` says it is",
       has(say(&ses, "look", &o), "core") && has(o.p, "roller door"));
    /* AND THE LINE ABOUT IT DOES NOT CONTRADICT ITSELF.
     *
     * A switch in goods in, in its box, with nothing in it, printed
     * "24/24 ports used   next free port core:0" -- one line, one box, two
     * counts, in the room every delivery lands in. The port count was read
     * off the NETSTACK, where a switch with no power in it has its ports
     * administratively down rather than empty; the free port came off the
     * site's link table, which is where a lead in a hole is recorded. One
     * source now, and it is the link table. */
    {
        char row[256];
        const char *r = dev_row(say(&ses, "look", &o), "core", row, sizeof row);
        char none[32];
        snprintf(none, sizeof none, "0/%d ports used", ses.s.dev[d].nports);
        ck("a box nobody has cabled says no ports are used, and says it once",
           row[0] && has(r, none) && has(r, "next free port core:0"));
    }

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
       has(say(&ses, "drop", &o), "is in d2 comms cupboard") &&
       ses.carrying < 0 && ses.s.dev[d].room == (uint16_t)up);

    /* THE MEASUREMENT. The same cable, from where the van left it and from
     * where the player carried it to. Nothing here decides which is shorter:
     * bld_cable_all() does, on this tower's own tray. */
    int mdf = bld_find(&ses.b, 0, RM_MDF);
    int from_goods = site_metres(&ses.s, goods, mdf);
    int from_comms = site_metres(&ses.s, up, mdf);
    printf("    a run to the Engineering is %d m from goods in and %d m from the "
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
    /* AND THE LEAD THE BUILDING CAME WITH COMES OUT, because the handoff has
     * one hole and the workstation was in it. It is said out loud at the
     * moment it happens, and link 0 -- the factory lead -- is now pulled. */
    ck("a cable comes up between the handoff and the box that was carried up",
       ses.s.nlink == 2 && site_link_state(&ses.s, 1) == PORT_UP);
    ck("and it says the lead the building came with came out to make room",
       has(o.p, "the lead the building came with comes out") &&
       has(o.p, "ws is off the network now") &&
       site_link_state(&ses.s, 0) == PORT_NOCABLE &&
       !site_dev_cabled(&ses.s, ws_dev(&ses)));
    /* The drum is still in your hands after a run: put it back, or the
     * refusal you get is about the drum and not about the box. */
    say(&ses, "spool back", &o);
    ck("and now it will not be picked up: there is a cable in it",
       has(say(&ses, "carry core", &o), "cable in it") && ses.carrying < 0);
    ck("`uncable` frees it, and it can be carried again",
       has(say(&ses, "uncable 1", &o), "pulled out") &&
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
    printf("\nan empty Engineering to a working network and a shell on a server\n");
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
        /* WHAT A REFUSAL LOOKS LIKE, and it is not the word "cannot" on its
         * own. `power files on` prints that machine's REAL boot log, and one
         * of the image's decoys is an fstab line for a disk that is not there
         * -- `mountall: /etc/fstab:8: cannot mount /dev/sdb1 on /media`. That
         * is a true sentence from an operating system about itself, not the
         * building refusing a line this script typed. The session's own
         * refusals are these, and "you cannot" is how it says the reaching
         * one ("`go core` first -- you cannot reach into another room"). */
        if (has(r, "no such command") || has(r, "refused") ||
            has(r, "I do not know how to") || has(r, "you cannot") ||
            has(r, "You cannot")) {
            printf("    `%s` -> %s", SCRIPT[i], r);
            clean = false;
        }
    }
    ck("a delivery fetched, cabled and configured by somebody who cannot see",
       clean);

    /* Four links: the lead the building came with, which `plug uplink:0`
     * pulled to make room, and the three runs this script laid. */
    ck("every link came up", ses.s.nlink == 4 &&
       site_link_state(&ses.s, 0) == PORT_NOCABLE &&
       site_link_state(&ses.s, 1) == PORT_UP &&
       site_link_state(&ses.s, 2) == PORT_UP &&
       site_link_state(&ses.s, 3) == PORT_UP);

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
        char row[256];
        /* THAT BOX'S OWN LINE. The player's workstation is standing in this
         * room, running, and saying so -- so the question has to be asked of
         * `files` rather than of the room. */
        const char *r = dev_row(say(&ses, "look", &o), "files", row, sizeof row);
        ck("a box whose boot failed does NOT claim an OS is running on it",
           row[0] && !has(r, "[an OS is running on it]"));
        ck("it says it is on and where the boot stopped instead",
           has(r, "switched on, but its boot stopped at"));
        ck("and the site still says it is powered, because it is",
           ses.s.dev[d].powered != 0);
    }
done:
    buf_free(&o);
    session_end(&ses);
}

/* ------------------------------------ what the person in the chair says */
/* THREE BUGS IN ONE SENTENCE, and a playtester hit all three in one sitting.
 *
 * It said "on this deck" while reading `tried`, which is per TENANCY: their
 * studio said the floor was dead while the web host on the same floor served
 * 24 of 24 visitors. It tested `!tried` BEFORE `complained`, so a tenancy
 * that had filed and never had one working desk -- the worst state there is
 * -- got the neutral line and never mentioned filing. And it counted in an
 * office's units: a call centre said "0 of 18 things we tried finished" on a
 * day `service` was saying "18 of 18 calls broke up".
 *
 * The states are forced here rather than played into, because reaching all
 * four by building and breaking a tower would take a hundred days and would
 * still not reach them reliably. */
static void check_desk_complaint(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nwhat the person whose chair it is actually says\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 200000)) { ck("a session starts", false); return; }
    Buf o = {0};

    int ti = -1;
    for (int guard = 0; guard < 400 && ti < 0; guard++) {
        say(&ses, "day 1", &o);
        for (int i = 0; i < ses.s.ntenant; i++)
            if (ses.s.tenant[i].moved && ses.s.tenant[i].ndesk > 0) { ti = i; break; }
    }
    if (ti < 0) { ck("a tenancy moves in", false); goto done; }

    {
        SiteTenant *t = &ses.s.tenant[ti];
        int d = t->desk0;
        char go[64], line[64];
        snprintf(go, sizeof go, "go %s", ses.s.dev[d].name);
        snprintf(line, sizeof line, "sit %s", ses.s.dev[d].name);

        /* 1. FILED, AND NOTHING HAS EVER WORKED. The case that used to be
         *    unreachable, and the one a player most needs the truth about. */
        t->complained = 1; t->strikes = 5; t->tried = 0; t->finished = 0;
        say(&ses, go, &o);
        const char *r = say(&ses, line, &o);
        ck("a tenancy that has filed says so, even with nothing working",
           has(r, "we filed with the landlord"));
        ck("and says THIS OFFICE, not this deck -- decks hold two or three "
           "tenancies", has(r, "in this office") && !has(r, "on this deck"));
        say(&ses, "stand", &o);

        /* 2. FILED, AND SOME OF IT WORKS -- counted in the trade's own unit. */
        t->kind = TEN_VOICE; t->tried = 18; t->finished = 0;
        say(&ses, go, &o);
        r = say(&ses, line, &o);
        ck("a call centre counts CALLS, not \"things we tried\"",
           has(r, "calls") && !has(r, "things we tried"));
        say(&ses, "stand", &o);

        /* 3. AND AN OFFICE STILL COUNTS TRANSFERS, so the unit is the trade's
         *    and not one word swapped for another everywhere. */
        t->kind = TEN_OFFICE; t->complained = 0; t->strikes = 2;
        t->tried = 80; t->finished = 60;
        say(&ses, go, &o);
        r = say(&ses, line, &o);
        ck("an office counts transfers", has(r, "transfers"));
        ck("and the unit comes from the same helper `service` uses",
           strcmp(site_tenant_kind_unit(TEN_VOICE, true), "calls") == 0 &&
           strcmp(site_tenant_kind_unit(TEN_OFFICE, true), "transfers") == 0);
        say(&ses, "stand", &o);
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
        ck("you can walk to a tenant's desk: their deck is not sealed off",
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

        /* AND THE OTHER DIRECTION, which the rule above quietly created.
         * site_move() reassigned ownership from whatever room a box was put
         * down in, so a playtester who bought a switch24 and carried it into
         * a let office to serve the desks in it -- the thing the game
         * recommends -- had it confiscated the moment they set it down. Four
         * hundred pounds and the run, gone, and it made D28's own named
         * mistake (the floor's switch put in the office) irreversible,
         * because the documented fix is to carry the box somewhere cooler. */
        say(&ses, "buy switch24 mine", &o);
        say(&ses, "go goods", &o);
        say(&ses, "carry mine", &o);
        char to[64];
        snprintf(to, sizeof to, "go #%d", room);
        say(&ses, to, &o);
        say(&ses, "drop", &o);
        int mine = -1;
        for (int i = 0; i < ses.s.ndev; i++)
            if (strcmp(ses.s.dev[i].name, "mine") == 0) { mine = i; break; }
        ck("a box you bought, set down in a let room, is still yours",
           mine >= 0 && ses.s.dev[mine].room == (uint16_t)room &&
           ses.s.dev[mine].tenant == 0);
        ck("and you can pick it up again and carry it back out",
           has(say(&ses, "carry mine", &o), "you pick mine up"));
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
    /* Link 0 is the lead the building came with, still in the handoff and
     * untouched by any of this: nothing here cables anything to uplink:0. */
    const char *r = say(&ses, "cable core:0 edge:9", &o);
    ck("a run to a port that box has not got does not make a cable",
       has(r, "numbered 0 to 3") && ses.s.nlink == 1);
    ck("and it leaves no end in a socket for the next line to eat",
       has(r, "comes back out of core port 0") && ses.cab_dev < 0);

    ck("so the next run is the one that was asked for",
       has(say(&ses, "cable core:1 edge:0", &o), "core:1 to edge:0") &&
       ses.s.nlink == 2 && ses.s.link[1].aport == 1);

    /* And an end put in BY HAND is not silently consumed either. */
    say(&ses, "plug core:3", &o);
    ck("an end left in by hand stops the macro rather than being used",
       has(say(&ses, "cable core:4 edge:1", &o), "already in core port 3") &&
       ses.s.nlink == 2);
    ck("both ends of one run in the same box is refused where the loop would be",
       has(say(&ses, "plug core:4", &o), "both ends would be in core") &&
       ses.s.nlink == 2);
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
    /* AND A SERIAL LEAD DOES NOT PRESS THE BUTTON FOR YOU -- nor does it
     * hand over a prompt on a machine that is not running. The lead goes in;
     * what comes back is nothing, and no Machine is installed, because
     * nothing has booted. */
    {
        const char *r = say(&ses, "plug probe", &o);
        char pr[64];
        session_prompt(&ses, pr, sizeof pr);
        ck("and a serial lead does not press the button for you",
           has(r, "nothing comes back") && has(r, "power probe on") &&
           ses.mach[pc] == NULL);
        ck("and it does not offer a prompt on a machine that is not running",
           !has(pr, "root@") && !has(pr, "#") && has(pr, "no console"));
        say(&ses, "unplug", &o);
    }

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
    char row[256];
    /* Asked of `probe`'s own line: the workstation is in this room too, and
     * it is running. */
    const char *lk = dev_row(say(&ses, "look", &o), "probe", row, sizeof row);
    ck("`look` does not claim an OS is running on a box that is switched off",
       row[0] && !has(lk, "an OS is running") && has(lk, "SWITCHED OFF"));
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
       has(say(&ses, "dns files store.deck1 10.0.1.77", &o),
           "store.deck1 -> 10.0.1.77"));
    ck("and says where it went, because for some boxes there is no disk",
       has(o.p, "/etc/net/services"));
    ck("`dnsd <box>` says what it will serve rather than the word `serving`",
       has(say(&ses, "dnsd files", &o), "serves 1 name"));
    say(&ses, "resolver desk1 10.0.1.10", &o);
    ck("a desk pointed at it resolves that name over real copper",
       has(say(&ses, "resolve desk1 store.deck1", &o), "10.0.1.77"));
    say(&ses, "power files off", &o);
    say(&ses, "power files on", &o);
    ck("and after the power cut the zone is still there, off the disk",
       has(say(&ses, "dnsd files", &o), "store.deck1") &&
       has(o.p, "10.0.1.77"));
    ck("and the desk still resolves it, having been told nothing",
       has(say(&ses, "resolve desk1 store.deck1", &o), "10.0.1.77"));
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
    ck("`go Engineering` works, and the prompt, `look` and `rooms` all print Engineering",
       has(say(&ses, "go Engineering", &o), "you walk") &&
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
        "the lift to a deck nobody has put in service" },
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
    ck("standing in a room, the prompt is the deck and the room",
       has(body, "Engineering") && has(body, "d0"));

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

/* ============ THE TEXT PRINTED *AROUND* A GUEST SHELL, AND WHERE IT LIES ===
 *
 * `--mancheck` runs every command example in every manual page, every package
 * README and every page of the in-game wiki on a booted machine, and fails if
 * a named command does not exist. It cannot see any of this. The sentences
 * session.c prints AROUND a guest shell -- the banner when a lead goes in,
 * the hints beside a prompt, the crash cart's own help page -- are outside
 * that gate, and a day-18 playtest found that is exactly where the remaining
 * lies were living. Their words: *"this class of hint (session text printed
 * around a guest shell) is outside the gate and is where the remaining lies
 * live."*
 *
 * Four of them, all one bug wearing four hats: THE SESSION PRINTS A LIST OF
 * THINGS YOU CAN TYPE WITHOUT KNOWING WHICH PROMPT YOU ARE AT.
 *
 *   A. `rescue` from the no-console prompt printed "`plug srv1` for a shell
 *      on the live system" and then promoted the session to `root@srv1#` --
 *      telling you to type a command to reach where you already were, and
 *      the command is a tower verb the guest has never heard of.
 *   B. `eject`, `power off` and `power on` are all `command not found` at
 *      `root@srv1#`, which is correct -- they are things a person does
 *      standing in the room -- and nothing said so.
 *   C. `eject` alone meant the LEAD at the no-console prompt and asked
 *      "eject which box?" in the room, while that prompt's own help lists it
 *      under "the live medium on the cart".
 *   D. `subif <box> <nic>` wants a socket NUMBER and every other line in the
 *      game names that interface `eth0`.
 *
 * check_verbs() above already has the shape of the answer for the TOWER
 * prompt: walk a list of verbs and assert each is NAMED in that prompt's help
 * AND ANSWERS at that prompt, in both directions, so neither half can drift.
 * This extends it to the three prompts nobody gated -- the management line,
 * the serial shell on a machine, and the `(no console)` state that landed
 * with power -- and adds the direction those three need and the tower prompt
 * does not:
 *
 *   a verb NAMED in the help of a prompt must ANSWER at that prompt, and
 *   a verb that will NOT answer there must not be named there without
 *   saying where it does work.
 */

/* The word a guest kernel uses for a program it has not got. */
static bool notfound(const char *out, const char *verb)
{
    char miss[64];
    snprintf(miss, sizeof miss, "%s: command not found", verb);
    return has(out, miss);
}

/* Verbs of the BUILDING: things a person does standing in a room, which are
 * therefore not programs on anybody's server and never will be. Each one is
 * checked to be honestly refused at a guest prompt -- not shadowed, and not
 * left as a bare "command not found" with no idea where it went. */
static const char *TOWER_ONLY[] = {
    "plug", "eject", "rescue", "power", "mains", "conduit", "conduits",
    "carry", "drop", "go", "buy", "deliver", "cable", "uncable", "quote",
    "jack", "patch", "jacks", "spool", "day", "serve", "service", "status",
    "load", "isp", "events", "demand", "money", "frames", "rooms", "map",
    "lift", "desks", "sit", "stand", "where", "look", "show", "addr", "gw",
    "subif", "vlan", "trunk", "dhcpd", "resolver", "dnsd", NULL
};

/* And the ones the machine really does have. `links` is the browser, `open`
 * launches a desktop application and `httpd` is the web server: a guest
 * prompt must go on running all three, so this gate would catch a fix to B
 * that shadowed the machine instead of speaking after it. */
static const char *GUEST_OWNS[] = { "links", "open", "httpd", NULL };

static void check_around_the_shell(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthe text printed AROUND a guest shell, which no other gate sees\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 200000)) { ck("a session starts", false); return; }
    Buf o = {0};
    char pr[96];

    /* ------------------------------------------- 1. the management line */
    say(&ses, "plug uplink", &o);
    session_prompt(&ses, pr, sizeof pr);
    if (!has(pr, "mgmt@")) { ck("a lead into the handoff is a management line", false); goto done; }
    Buf mh = {0};
    buf_puts(&mh, say(&ses, "help", &o));
    static const char *MGMT[] = {
        "addr", "gw", "router", "subif", "vlan", "trunk", "dhcpd", "dhcp",
        "resolver", "ping", "trace", "resolve", "dnsd", "dns", "get",
        "day", "serve", "isp", "events", "ups", "disk",
        "status", "service", "load", "show", "links", "rooms", "demand",
        "money", "frames", "where", "help", "unplug", NULL
    };
    bool mnamed = true, manswers = true;
    for (int i = 0; MGMT[i]; i++) {
        if (!has(mh.p, MGMT[i])) {
            printf("    the management line's help does not name `%s`\n", MGMT[i]);
            mnamed = false;
        }
        const char *a = say(&ses, MGMT[i], &o);
        if (has(a, "no such command")) {
            printf("    `%s` is named on the management line and is not a verb "
                   "there\n", MGMT[i]);
            manswers = false;
        }
    }
    ck("the management line's help names every verb it takes", mnamed);
    ck("and every one of them answers AT the management line", manswers);
    /* And it does not send anybody to a verb of the room. You cannot pick a
     * box up with a lead in it, so `carry` at this prompt would be a lie. */
    /* And it does not offer a verb of the ROOM in its command column. You
     * cannot pick a box up with a lead in it, so `carry` here would be a lie
     * -- and the column is where a player looks, which is why the marker is
     * a line start rather than the word anywhere in the prose. */
    ck("and its command column does not offer a verb of the ROOM",
       !has(mh.p, "\ncarry ") && !has(mh.p, "\ndrop ") && !has(mh.p, "\nspool "));
    buf_free(&mh);
    say(&ses, "unplug", &o);

    /* ------------------------------- 2. the serial shell on a machine */
    say(&ses, "buy server srv1", &o);
    {
        int mdf = bld_find(&ses.b, 0, RM_MDF);
        char dl[48];
        snprintf(dl, sizeof dl, "deliver srv1 #%d", mdf);
        say(&ses, dl, &o);
    }
    say(&ses, "power srv1 on", &o);
    say(&ses, "plug srv1", &o);
    session_prompt(&ses, pr, sizeof pr);
    if (!has(pr, "root@srv1")) { ck("a lead into a booted server is a shell", false); goto done; }

    Buf sh = {0};
    buf_puts(&sh, say(&ses, "help", &o));
    /* THE PROGRAMS IT NAMES HAVE TO BE ON THE MACHINE. This is `--mancheck`'s
     * rule applied to the one page --mancheck cannot read. */
    static const char *SHELLPROG[] = {
        "ip addr", "netstat -r", "ping 127.0.0.1", "traceroute 127.0.0.1",
        "ss", "arp", "tcpdump", "svc", "ps", "dmesg", "fsck /dev/sda1",
        "pkg verify", "man pkg", NULL
    };
    bool progs = true;
    for (int i = 0; SHELLPROG[i]; i++) {
        char verb[32], *sp;
        snprintf(verb, sizeof verb, "%s", SHELLPROG[i]);
        if ((sp = strchr(verb, ' '))) *sp = 0;
        if (!has(sh.p, verb)) {
            printf("    the shell's own help does not name `%s`\n", verb);
            progs = false;
        }
        if (notfound(say(&ses, SHELLPROG[i], &o), verb)) {
            printf("    the shell's help names `%s` and the machine has not got "
                   "it\n", verb);
            progs = false;
        }
    }
    ck("every program the shell's own help names is really on the machine", progs);

    /* THE HALF NOTHING CHECKED. A tower verb typed here is `command not
     * found`, honestly, and until this gate nothing made the session say
     * WHERE it does work -- so a player told to type `power off` by one line
     * of the game was answered by another with a dead end. */
    bool located = true;
    for (int i = 0; TOWER_ONLY[i]; i++) {
        const char *a = say(&ses, TOWER_ONLY[i], &o);
        if (!notfound(a, TOWER_ONLY[i])) continue;   /* it did something: fine */
        if (!has(a, "TOWER verb") || !has(a, "unplug")) {
            printf("    `%s` at root@srv1# is a dead end: %s", TOWER_ONLY[i], a);
            located = false;
        }
    }
    ck("a verb of the building typed at a guest shell says which prompt it "
       "belongs to", located);

    /* AND THE MACHINE STILL ANSWERS FIRST. Nothing above may shadow a real
     * program, which is the way this fix could have been got wrong. */
    bool owns = true;
    for (int i = 0; GUEST_OWNS[i]; i++)
        if (notfound(say(&ses, GUEST_OWNS[i], &o), GUEST_OWNS[i])) {
            printf("    `%s` is a real program on this OS and the session ate "
                   "it\n", GUEST_OWNS[i]);
            owns = false;
        }
    ck("and a real program on the machine still runs, unshadowed", owns);

    ck("the shell's help says out loud that the building's verbs are not "
       "programs",
       has(sh.p, "not a program") || has(sh.p, "does not work here") ||
       has(sh.p, "DOES NOT WORK HERE"));
    ck("and it names the word that gets back to the prompt they do work at",
       has(sh.p, "unplug"));
    buf_free(&sh);

    /* ------------------------- 3. the `(no console)` prompt, and `eject` */
    say(&ses, "rm /boot/vmnomuz", &o);
    say(&ses, "unplug", &o);
    say(&ses, "power srv1 off", &o);
    say(&ses, "power srv1 on", &o);
    say(&ses, "plug srv1", &o);
    session_prompt(&ses, pr, sizeof pr);
    if (!has(pr, "no console")) { ck("a box with no login gives no prompt", false); goto done; }

    Buf nh = {0};
    buf_puts(&nh, say(&ses, "help", &o));
    static const char *NOCON[] = { "power", "mains",
                                   "rescue", "eject", "show", "look", "where",
                                   "unplug", NULL };
    bool nnamed = true, nanswers = true;
    for (int i = 0; NOCON[i]; i++) {
        if (!has(nh.p, NOCON[i])) {
            printf("    the no-console help does not name `%s`\n", NOCON[i]);
            nnamed = false;
        }
        if (strcmp(NOCON[i], "unplug") == 0) continue;   /* it ends the state */
        const char *a = say(&ses, NOCON[i], &o);
        if (has(a, "not running anything that could read")) {
            printf("    `%s` is named on the no-console help and is unheard "
                   "there\n", NOCON[i]);
            nanswers = false;
        }
        /* AND NONE OF THEM MAY QUIETLY END THE STATE. A verb listed as
         * something you do to the box, that actually puts the lead back on
         * the cart, is the same lie in the other direction. `power on` and
         * `rescue` ARE allowed to end it -- upwards, onto a login that came
         * up this line -- and that is a different thing from being dropped
         * back into the room. */
        session_prompt(&ses, pr, sizeof pr);
        if (!has(pr, "no console") && !has(pr, "root@")) {
            printf("    `%s` is named as a thing you do to the box and it put "
                   "the lead back\n", NOCON[i]);
            nanswers = false;
        }
        /* Put the box back where this section found it: kernel gone, powered,
         * nothing on the wire. Whatever the verb did to it, the next one is
         * asked at the same prompt. */
        if (!has(pr, "no console")) {
            say(&ses, "unplug", &o);
            say(&ses, "eject srv1", &o);
            say(&ses, "plug srv1", &o);
        }
    }
    ck("the no-console help names every verb that prompt takes", nnamed);
    ck("and every one of them is heard there, and none of them is `unplug` "
       "in disguise", nanswers);
    buf_free(&nh);

    /* `eject` MEANS THE STICK AT EVERY PROMPT THAT TAKES IT, which is the
     * whole of finding C. It used to mean the medium in the room and the lead
     * here, with one help page listing it under the medium. */
    {
        const char *e = say(&ses, "eject", &o);
        session_prompt(&ses, pr, sizeof pr);
        ck("bare `eject` at the no-console prompt is the STICK, not the lead",
           !has(e, "lead back on the cart") && has(pr, "no console"));
    }
    /* And the room's own refusal says which of the two it is, rather than
     * "eject which box?" beside a help page that lists it bare. */
    say(&ses, "unplug", &o);
    {
        const char *e = say(&ses, "eject", &o);
        ck("and in the room it says what it wants and that it is not the lead",
           has(e, "eject <box>") && has(e, "unplug"));
    }
    say(&ses, "plug srv1", &o);

    /* ------ 4. the banner around the promotion, which is finding A itself */
    {
        const char *r = say(&ses, "rescue srv1", &o);
        session_prompt(&ses, pr, sizeof pr);
        ck("`rescue` from the no-console prompt puts you on the live system",
           has(pr, "root@srv1") && has(r, "UP at target"));
        /* THE LIE: a banner telling you to type `plug srv1` at `root@srv1#`,
         * where `plug` is a tower verb the guest has never heard of. */
        ck("and it does NOT tell you to plug in a lead that is already in",
           !has(r, "`plug srv1` for a shell"));
        ck("and it says the lead is already in and the line is the live one",
           has(r, "already in srv1"));
        ck("and it says the way back out is at the rack, not at this prompt",
           has(r, "unplug") && has(r, "eject srv1"));
    }
    say(&ses, "unplug", &o);
    say(&ses, "eject srv1", &o);

    /* ------------------------------- 5. the nic is a number, said up front */
    {
        const char *h = say(&ses, "help", &o);
        ck("the tower help says a subif's nic is a socket NUMBER, not `eth0`",
           has(h, "subif") && has(h, "SOCKET NUMBER"));
        ck("and the help's own example is one the model accepts",
           has(h, "subif srv2 0 21"));
        ck("and the spelling every other output uses is refused, as the help "
           "says",
           has(say(&ses, "subif srv1 eth0 21 10.0.21.1/24", &o),
               "socket number"));
    }

done:
    buf_free(&o);
    session_end(&ses);
}

/* ================= THE OPENING TEXT COUNTS, IT DOES NOT PROMISE ===========
 *
 * `help` opened with *"On day one it holds exactly one thing: the ISP's
 * socket on the wall of the Engineering."* True of a session started over the socket
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

    /* AND THE OTHER WORLD IS A REAL GAME, NOT THREE LINES TYPED HERE.
     *
     * This used to type `order router edge / switch24 core / server files`
     * itself and assert the total came to 2400 -- a third copy of the
     * starting kit, living in a gate, describing a delivery that had already
     * changed twice. It starts a GAME now, through the same door the window
     * and `--towersh` come in by, so what is asserted is what a player really
     * finds in goods in on day one. */
    session_end(&ses);
    if (!session_new_game(&ses, GATE_SEED, SITE_OPENING_MONEY)) {
        ck("a new game starts", false);
        buf_free(&o);
        return;
    }
    long spent = ses.s.spent;
    ck("a new game's delivery is a switch4 and a minitower, 505 the pair",
       spent == site_kind_price(SDEV_SWITCH4) + site_kind_price(SDEV_MINITOWER));

    /* IN GOODS IN, which is the half a player walks to. */
    int in_goods = 0;
    int goods = bld_find(&ses.b, 0, RM_GOODS);
    for (int i = 0; i < ses.s.ndev; i++)
        if (ses.s.dev[i].room == goods && ses.s.dev[i].tenant == 0 &&
            ses.s.dev[i].kind != SDEV_UPLINK) in_goods++;
    ck("and it is standing in goods in, not at your feet", in_goods == 2);

    h = say(&ses, "help", &o);
    ck("and the same help names both, where they are, and what each cost",
       has(h, "core") && has(h, "files") && has(h, "goods in") &&
       has(h, "45 already paid") && has(h, "460 already paid"));
    ck("and it says how much of the budget has already gone on them",
       has(h, "505 of the budget"));

    /* AND THE PLAYER WHO NEVER TYPES `help`. The intro is the first thing a
     * blind tester reads and it named goods in without saying what was in it. */
    say(&ses, "desk", &o);
    const char *in = say(&ses, "tower", &o);
    ck("the way in names the delivery already on the deck of goods in",
       has(in, "ALREADY A DELIVERY") && has(in, "core") && has(in, "files") &&
       has(in, "do not order those again"));

    buf_free(&o);
    session_end(&ses);
}

/* ==================== `cable` PUTS YOU BACK WHERE YOU ASKED ===============
 *
 * *"I queued seven fibre runs from the Engineering; the first succeeded and walked me
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
        say(&ses, "go d1.comms", &o);
        say(&ses, "drop", &o);
        made++;
    }
    say(&ses, "go mdf", &o);
    ck("three boxes carried into the cupboard upstairs, and you back in the Engineering",
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
            printf("    run %d left you in room %d, not the Engineering\n", i, ses.room);
            home = false;
        }
    }
    ck("three runs typed one after another from the Engineering, and all three come up",
       all && ses.s.nlink == 4);   /* + the lead the building came with */
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
    say(&ses, "go d1.comms", &o); say(&ses, "drop", &o);
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

/* ------------------- one box to a cupboard, walked and typed, side by side
 *
 * WHAT THIS IS FOR, because the obvious reading is wrong. `deliver` is not a
 * convenience: a day-30 playtester measured 40% of their commands as
 * `lift 0 / go goods / carry X / lift N / go comms / drop` and called it
 * filler, and that playtester was an agent on a socket. The owner's reading
 * is the correct one -- those are *"actions the user will do walking around
 * in the 3d space"*. For the human there is nothing to delete; carrying a box
 * up two floors is the game. `deliver` exists so that a tester who cannot
 * hold a movement key can still play the half of this game that is legs,
 * because blind playtesting is the only quality mechanism this project has.
 *
 * WHICH MAKES THIS CHECK THE WHOLE JUSTIFICATION. The two sessions below are
 * the same seed and the same three switches into the same cupboard: one
 * played with the movement verbs, one typed. If the money, the metres or the
 * room differ by anything at all, the verb is not parity, it is an advantage
 * the keyboard does not have -- and it should be deleted rather than tuned.
 *
 * The lift is why this is measured rather than assumed. `lift 3` is NOT the
 * same metres as walking up three flights: do_lift() charges the walk to this
 * floor's lift lobby and the ride itself is free, which is the whole reason
 * anybody puts a switch on the eighth floor. A `deliver` that took the stairs
 * would have been DEARER than the hands, and just as wrong. */
static void check_deliver_played(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthree switches to one deck: walked, and typed\n");

    /* Six movement commands a box, exactly as the playtester typed them. */
    static const char *LONG[] = {
        "buy switch24 sw1", "buy switch24 sw2", "buy switch24 sw3",
        "go goods",  "carry sw1", "lift 1", "go comms", "drop",
        "lift 0", "go goods", "carry sw2", "lift 1", "go comms", "drop",
        "lift 0", "go goods", "carry sw3", "lift 1", "go comms", "drop",
        NULL
    };
    Session a;
    if (!session_start(&a, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    int an = 0;
    for (int i = 0; LONG[i]; i++) { say(&a, LONG[i], &o); an++; }
    int comms = a.room;
    ck("walked, three switches end up in the deck one comms cupboard",
       a.b.rooms[comms].kind == RM_COMMS && a.b.rooms[comms].floor == 1 &&
       site_dev_by_name(&a.s, "sw1") > 0 &&
       a.s.dev[site_dev_by_name(&a.s, "sw1")].room == (uint16_t)comms &&
       a.s.dev[site_dev_by_name(&a.s, "sw2")].room == (uint16_t)comms &&
       a.s.dev[site_dev_by_name(&a.s, "sw3")].room == (uint16_t)comms);
    printf("    %d lines, %ld m walked, %ld spent\n", an, a.walked, a.s.spent);

    /* The same job, with the walking typed instead of held down. */
    static const char *SHORT[] = {
        "buy switch24 sw1", "buy switch24 sw2", "buy switch24 sw3",
        "deliver sw1 sw2 sw3 d1.comms", NULL
    };
    Session b;
    if (!session_start(&b, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    int bn = 0;
    for (int i = 0; SHORT[i]; i++) { say(&b, SHORT[i], &o); bn++; }
    ck("typed, the same three end up in the same cupboard",
       b.room == comms &&
       b.s.dev[site_dev_by_name(&b.s, "sw1")].room == (uint16_t)comms &&
       b.s.dev[site_dev_by_name(&b.s, "sw2")].room == (uint16_t)comms &&
       b.s.dev[site_dev_by_name(&b.s, "sw3")].room == (uint16_t)comms);
    printf("    %d lines, %ld m walked, %ld spent\n", bn, b.walked, b.s.spent);

    /* THE THREE ASSERTIONS THE VERB EXISTS TO SATISFY. Parity, or nothing. */
    ck("and it costs the same money -- not one penny of it is a discount",
       a.s.spent == b.s.spent && a.s.money == b.s.money);
    ck("and the same metres of building, lift rides and all",
       a.walked == b.walked && a.walked > 0);
    ck("and leaves you standing where the sixth movement command left you",
       b.room == comms && b.b.rooms[b.room].floor == 1);
    /* The days too: a delivery is legs, and legs do not turn the calendar,
     * either way round. */
    ck("and on the same day, because walking has never advanced the clock",
       a.s.day == b.s.day);

    buf_free(&o);
    session_end(&a);
    session_end(&b);
}

/* --------------------------------- and it refuses everything `carry` refuses
 *
 * A shorthand that could do something the hands cannot is a second game. Each
 * of these is a refusal the long form gives, asked of the one-liner, and the
 * assertion is that the WORLD DID NOT MOVE: no metres, no money, and the box
 * still where it was. That last one is what `cable` learned the hard way --
 * a refused line that had already taken a drum off the shelf left the player
 * unable to pick anything up. */
static void check_deliver_refuses(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthe shorthand refuses what the hands refuse\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    open_next_floor(&ses, &o);
    say(&ses, "go mdf", &o);
    say(&ses, "buy switch24 core", &o);
    say(&ses, "buy switch24 sw1", &o);
    say(&ses, "buy pc spare", &o);

    long walked = ses.walked, money = ses.s.money;
    ck("a box that does not exist is refused, and nobody walked anywhere",
       has(say(&ses, "deliver nosuchbox d1.comms", &o), "no box called nosuchbox") &&
       ses.walked == walked && ses.s.money == money);
    ck("a room that does not exist is refused the same way",
       has(say(&ses, "deliver core nosuchroom", &o), "no room or box called") &&
       ses.walked == walked);
    ck("and naming one box twice, because it only needs carrying once",
       has(say(&ses, "deliver core core d1.comms", &o), "named twice") &&
       ses.walked == walked);

    /* Hands full. */
    say(&ses, "go goods", &o);
    say(&ses, "carry spare", &o);
    walked = ses.walked;
    ck("a box already in your hands stops the line before it starts",
       has(say(&ses, "deliver core d1.comms", &o), "both your hands are on it") &&
       ses.carrying == site_dev_by_name(&ses.s, "spare") && ses.walked == walked);
    say(&ses, "drop", &o);

    /* A drum is the other thing that takes both hands. */
    say(&ses, "spool cat6", &o);
    walked = ses.walked;
    ck("so does a drum of cable, and the drum is still in your hands after",
       has(say(&ses, "deliver core d1.comms", &o), "drum of cable") &&
       ses.spool_kind >= 0 && ses.walked == walked);
    say(&ses, "spool back", &o);

    /* The ISP's handoff is the ISP's, and it is in the MDF, so this one
     * walks you there first -- which is what `go mdf` then `carry uplink`
     * does too, for the same metres. */
    ck("the ISP handoff is on their wall and does not come with you",
       has(say(&ses, "deliver uplink d1.comms", &o), "the handoff is the ISP's") &&
       ses.carrying < 0);

    /* A BOX WITH COPPER IN IT, and where the refusal happens matters.
     * `go core` then `carry core` charges the walk and THEN says there is a
     * cable in the back of it, because that is when a person finds out. So
     * does this: the metres are spent, the box has not moved, and the words
     * are the long form's. A shorthand that had checked from the doorway
     * would be cheaper than the hands in the one case the player got it
     * wrong, which is the one case it must not be. */
    say(&ses, "deliver core sw1 d1.comms", &o);
    int comms = ses.room;
    say(&ses, "cable core:0 sw1:0 cat5e", &o);
    say(&ses, "spool back", &o);
    say(&ses, "go mdf", &o);
    long spent = ses.s.spent;
    walked = ses.walked;
    ck("a cabled box is refused, in the long form's own words",
       has(say(&ses, "deliver core mdf", &o), "on the end of a cable") &&
       ses.s.spent == spent &&
       ses.s.dev[site_dev_by_name(&ses.s, "core")].room == (uint16_t)comms);
    ck("and it found out where a person finds out: at the box, walk paid for",
       ses.walked > walked && ses.room == (int)ses.s.dev[site_dev_by_name(&ses.s, "core")].room);

    /* A tenant's own kit. Move a tenancy in and try to walk off with a desk. */
    int desk = -1;
    for (int d = 0; d < 40 && desk < 0; d++) {
        say(&ses, "day", &o);
        for (int i = 0; i < ses.s.ndev; i++)
            if (ses.s.dev[i].tenant != 0) { desk = i; break; }
    }
    if (desk >= 0) {
        char c[64];
        snprintf(c, sizeof c, "deliver %s mdf", ses.s.dev[desk].name);
        int was = ses.s.dev[desk].room;
        ck("and a tenant's own computer stays on the tenant's own desk",
           has(say(&ses, c, &o), "belongs to the tenant") &&
           ses.s.dev[desk].room == (uint16_t)was);
    } else ck("and a tenant's own computer stays on the tenant's own desk", false);

    /* AND WHEN IT CANNOT FINISH, IT STOPS WHERE A PERSON WOULD STOP. Two
     * boxes named, the second of them cabled: the first is delivered and the
     * line stops with it there, rather than silently unwinding a trip that
     * really happened. */
    say(&ses, "go mdf", &o);
    say(&ses, "buy switch8 pair1", &o);
    say(&ses, "buy switch8 pair2", &o);
    say(&ses, "go goods", &o);
    say(&ses, "carry pair2", &o);
    say(&ses, "go d1.comms", &o);
    say(&ses, "drop", &o);
    say(&ses, "cable pair2:0 sw1:1 cat5e", &o);
    say(&ses, "spool back", &o);
    say(&ses, "go mdf", &o);
    int mdf = ses.room;
    const char *half = say(&ses, "deliver pair1 pair2 mdf", &o);
    ck("half a delivery is half done, not undone: the first box arrived",
       ses.s.dev[site_dev_by_name(&ses.s, "pair1")].room == (uint16_t)mdf &&
       has(half, "on the end of a cable"));
    ck("and the second is exactly where the copper left it",
       ses.s.dev[site_dev_by_name(&ses.s, "pair2")].room == (uint16_t)comms);

    buf_free(&o);
    session_end(&ses);
}

/* ------------------- the two ends of a tagged link, and the sentence between
 *
 * THE MEASUREMENT. A day-30 playtester named the real burden in this game and
 * it is not typing: *"the bookkeeping around a tenancy is five places to get
 * right -- `vlan 13` = tenant 3 = `10.0.3.0/24` = `subif edge 1 13` = `trunk
 * core 2 13` = `trunk sw2b 23 13` -- and the game checks none of them against
 * each other."* Both commands answer "set" and neither mentions the other.
 *
 * What follows does not assert that a warning is PRINTED. It asserts that the
 * warning is TRUE, by playing both sides of it: a real host on a real vlan
 * pings a real router across a real switch, and it fails while the note is on
 * the screen and succeeds the moment the trunk carries the tag. That is the
 * rule this project runs on -- every technical claim true of this machine,
 * verified by running it -- and a note that said something false at the
 * moment of a mistake would be worse than no note at all. */
static void check_tag_hop(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nwhat one end of a tagged link says about the other\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *SETUP[] = {
        /* Built with the movement verbs on purpose: this check is about the
         * note, and it must fail on a tree that has the note missing rather
         * than on one that could not carry a box to the MDF. */
        "buy router edge", "buy switch24 core", "buy pc t14",
        "go goods", "carry edge", "go mdf", "drop",
        "go goods", "carry core", "go mdf", "drop",
        "go goods", "carry t14", "go mdf", "drop",
        "cable edge:1 core:2 cat6",
        "cable t14:0 core:5 cat5e",
        "spool back",
        "power t14 on",
        "vlan core 5 14",
        "addr t14 10.0.14.9/24",
        NULL
    };
    for (int i = 0; SETUP[i]; i++) say(&ses, SETUP[i], &o);

    const char *sub = say(&ses, "subif edge 1 14 10.0.14.1/24", &o);
    ck("a subinterface on a card whose far port does not carry the tag says so",
       has(sub, "wears vlan 14") && has(sub, "does not carry vlan 14") &&
       has(sub, "trunk core 2 14"));

    /* AND THE NOTE IS TRUE. Not "the game printed a warning" -- the frame
     * really does not get there, and this is the frame. */
    int t14 = site_dev_by_name(&ses.s, "t14");
    int rtt = 0;
    PingResult before = net_ping(ses.s.net, ses.s.dev[t14].node,
                                 net_ip(10, 0, 14, 1), &rtt);
    ck("and it is true: a host in vlan 14 cannot reach its gateway across it",
       before != PING_OK);

    const char *tr = say(&ses, "trunk core 2 14", &o);
    ck("letting the vlan across the trunk stops the note being printed",
       !has(tr, "does not carry vlan 14"));
    PingResult after = net_ping(ses.s.net, ses.s.dev[t14].node,
                                net_ip(10, 0, 14, 1), &rtt);
    ck("and stops it being true, in the same move: the ping crosses now",
       after == PING_OK);

    /* THE SAME DISAGREEMENT, NOTICED FROM THE SWITCH. `trunk` is typed on the
     * box at the other end from `subif`, and a player who takes a vlan back
     * off a trunk has just broken the subinterface riding on it. */
    const char *off = say(&ses, "trunk core 2 -14", &o);
    ck("and taking it back off says which subinterface that just cut off",
       has(off, "wears vlan 14") && has(off, "does not carry vlan 14"));
    ck("which is true as well: the ping stops crossing again",
       net_ping(ses.s.net, ses.s.dev[t14].node, net_ip(10, 0, 14, 1), &rtt) != PING_OK);

    /* AND IT DOES NOT CRY WOLF. A link that works gets no note, ever: a
     * warning printed beside a correct configuration teaches a player to
     * stop reading them, which is worse than printing nothing. */
    say(&ses, "trunk core 2 14", &o);
    const char *quiet = say(&ses, "subif edge 1 14 10.0.14.1/24", &o);
    ck("a subinterface whose trunk does carry it is not warned about",
       !has(quiet, "NOTE") && has(quiet, "eth1.14"));
    const char *plain = say(&ses, "addr t14 10.0.14.9/24", &o);
    ck("nor is an ordinary address on an access port",
       !has(plain, "NOTE"));

    buf_free(&o);
    session_end(&ses);
}

/* ------------------------------- the same riser, bought both ways, played
 *
 * A playtest that reached day 34 said the thing this feature has to answer:
 * *"Cable is a bill I paid with a rule, not a bill I sweated. I made the
 * riser decision on deck 1 and then repeated it on decks 2 and 3 without
 * thinking."* A jack that is only a dearer cable does not fix that. What
 * follows is the same run bought both ways by a person over a pipe, and the
 * assertions are that BOTH answers are wrong somewhere:
 *
 *   - the jack is dearer, and it is not there for days, so the floor that
 *     needed a switch this afternoon wanted the spool;
 *   - and the spool is charged again every time the box moves, so the floor
 *     that has been rebuilt twice wanted the jack.
 *
 * Neither of those is a number anybody tuned. The first is site_jack_days()
 * against the same `day` the strike clock runs on; the second is
 * site_uncable() refunding nothing, which it has done since it was written.
 */
static void check_jack_played(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nthe same riser, off the spool and out of the wall\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *SETUP[] = {
        "buy switch24 core", "go goods", "carry core", "go mdf", "drop", NULL
    };
    for (int i = 0; SETUP[i]; i++) say(&ses, SETUP[i], &o);
    open_next_floor(&ses, &o);
    say(&ses, "go d1.comms", &o);
    int cupboard = ses.room;
    int metres = site_metres(&ses.s, cupboard, ses.s.dev[0].room);

    /* BOOKED FROM THE ROOM IT GOES IN, and both prices printed at the moment
     * the money leaves -- the same place D27 puts the negotiated speed, for
     * the same reason: it is the only moment the player is thinking about
     * this decision. */
    const char *bought = say(&ses, "jack core:22 cat5e", &o);
    char spool_price[32], jack_price[32];
    snprintf(spool_price, sizeof spool_price, "spool is %d",
             site_cable_price(CAB_CAT5E, metres));
    snprintf(jack_price, sizeof jack_price, "%d paid",
             site_jack_price(CAB_CAT5E, metres));
    ck("`jack` books one from the room you are standing in, priced by distance",
       ses.s.njack == 1 && has(bought, jack_price));
    ck("and prints what the same metres would have cost off the spool",
       has(bought, spool_price) &&
       site_jack_price(CAB_CAT5E, metres) > site_cable_price(CAB_CAT5E, metres));
    ck("and says which day it stops being a booking and starts being a socket",
       has(bought, "the trade comes on day"));
    ck("`look` in that room says there is copper on the wall",
       has(say(&ses, "look", &o), "jacks in the wall"));

    /* THE FLOOR THAT NEEDED IT THIS AFTERNOON. This is the wrong answer, and
     * the player finds out by trying. */
    static const char *KIT[] = {
        "buy switch8 fsw", "go goods", "carry fsw", "go d1.comms", "drop", NULL
    };
    for (int i = 0; KIT[i]; i++) say(&ses, KIT[i], &o);
    const char *early = say(&ses, "patch fsw:0", &o);
    ck("a jack booked today is not a socket today, and says so in days",
       has(early, "refused") && has(early, "Copper off the spool is in your") &&
       ses.s.nlink == 1);   /* the lead the building came with, and nothing else */

    char day[16];
    snprintf(day, sizeof day, "day %d", site_jack_days(metres));
    say(&ses, day, &o);
    long before = ses.s.money;
    const char *in = say(&ses, "patch fsw:0", &o);
    ck("once the trade has been, a box in that room plugs in for a lead",
       has(in, "already in the wall") && ses.s.nlink == 2 &&
       ses.s.money == before - site_jack_lead_price());
    ck("and the link is up, on the metres that are in the wall",
       site_link_state(&ses.s, 1) == PORT_UP && ses.s.link[1].metres == metres);

    /* THE FLOOR THAT GETS REBUILT. Take the switch out, put it back -- which
     * is a `move`, and a move off the spool is the whole run again. */
    say(&ses, "uncable 1", &o);
    ck("the lead comes out and the jack does not",
       has(o.p, "The jack is still in the wall") && ses.s.njack == 1);
    say(&ses, "carry fsw", &o);
    say(&ses, "go mdf", &o);
    say(&ses, "go d1.comms", &o);
    say(&ses, "drop", &o);
    before = ses.s.money;
    say(&ses, "patch fsw:0", &o);
    ck("and the box that came back costs a lead, not a run",
       ses.s.money == before - site_jack_lead_price());

    /* THE ARITHMETIC OF THE WHOLE DECISION, in the money that really left
     * the account, against the same three connections off the spool. */
    int jack_way  = site_jack_price(CAB_CAT5E, metres) + site_jack_lead_price() * 3;
    int spool_way = site_cable_price(CAB_CAT5E, metres) * 3;
    printf("    %d m riser: three connections cost %d jacked, %d spooled\n",
           metres, jack_way, spool_way);
    ck("three connections over the life of the run and the jack was right",
       jack_way < spool_way);
    ck("one connection and it was not, by the fit-out and the lead",
       site_jack_price(CAB_CAT5E, metres) + site_jack_lead_price() >
       site_cable_price(CAB_CAT5E, metres));

    /* AND THE PORT AT THE FAR END IS NOT A PORT ANY MORE, which is the cost
     * nobody counts until a floor runs out of holes. */
    say(&ses, "go mdf", &o);
    ck("`show` on the core says which socket the jack took for good",
       has(say(&ses, "show core", &o), "punched down to jack j0"));
    ck("and `links` separates copper in the wall from money gone",
       has(say(&ses, "links", &o), "paid to have them put in") &&
       has(o.p, "a lead in j0"));

    buf_free(&o);
    session_end(&ses);
}

/* ======================================= the quote, from where you stand
 *
 * D28 recorded a playtester at day 62 who could not exercise the
 * marginal-copper rule because nothing in the game measures a run before you
 * pay for it: *"guess-and-pay at ~110 a guess."* The same blindness covered
 * every cable decision D27 built.
 *
 * This is that verb played the way a person plays it: standing in the room,
 * with the drum in their hands, asking what the far end costs -- and then
 * paying for it and comparing. The assertion that carries the feature is the
 * last one: THE QUOTE IS THE BILL. A quote that disagrees with the invoice is
 * worse than no quote.
 */
static void check_quote_played(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nwhat the run costs, asked from the room you are standing in\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    static const char *SETUP[] = {
        "buy switch24 core", "go goods", "carry core", "go mdf", "drop", NULL
    };
    for (int i = 0; SETUP[i]; i++) say(&ses, SETUP[i], &o);

    /* THE TWO ROOMS ON ONE FLOOR THAT LOOK THE SAME AND ARE NOT. Floor 3 of
     * this seed's tower runs from sixty metres to ninety-five from the MDF,
     * and `rooms 3` prints both of them as "office". */
    int mdf = ses.room, far = -1, near = -1, dfar = -1, dnear = 1 << 30;
    for (int i = 0; i < ses.b.nrooms; i++) {
        const Room *r = &ses.b.rooms[i];
        if (r->floor != 3 || r->kind != RM_OFFICE) continue;
        int m = site_metres(&ses.s, mdf, i);
        if (m < 0) continue;
        if (m > dfar)  { dfar = m; far = i; }
        if (m < dnear) { dnear = m; near = i; }
    }
    char line[64];
    snprintf(line, sizeof line, "quote #%d", far);
    long money = ses.s.money, walked = ses.walked;
    int where = ses.room;
    const char *q = say(&ses, line, &o);
    ck("`quote <room>` answers from the room you are standing in",
       has(q, "a run from d0 Engineering") && has(q, "through the tray"));
    ck("and asking costs no money, no metres of your legs, and does not move "
       "you",
       ses.s.money == money && ses.walked == walked && ses.room == where &&
       ses.s.nlink == 1 && ses.s.njack == 0);   /* only the day-one lead */

    /* THE DECISION IT EXISTS TO INFORM. Two offices on one floor: the far one
     * is past the margin and the near one is not, and before this verb the
     * only way to find out was to pay. */
    char want[64];
    snprintf(want, sizeof want, "%d m through the tray", dfar);
    ck("the far office on that deck is past what copper has margin for",
       has(q, want) && dfar >= SITE_COPPER_MARGIN_M &&
       has(q, "copper has margin for"));
    snprintf(line, sizeof line, "quote #%d", near);
    q = say(&ses, line, &o);
    snprintf(want, sizeof want, "%d m through the tray", dnear);
    ck("and the near one, which `rooms 3` prints identically, is not",
       has(q, want) && dnear < SITE_COPPER_MARGIN_M &&
       !has(q, "copper has margin for"));
    printf("    #%d is %d m and #%d is %d m, and both of them say `office`\n",
           near, dnear, far, dfar);

    /* WHAT EACH GRADE BUYS OVER THAT DISTANCE, which is the other half of the
     * blindness: over sixty metres cat6 is a gigabit, exactly as cat5e is,
     * for more money -- and the quote is where a player can see that before
     * spending it. */
    ck("every grade is priced and speeded on one screen, cheapest first",
       has(q, "grade   off the spool   as a jack   it comes up at") &&
       site_cable_speed(CAB_CAT6, dnear) == site_cable_speed(CAB_CAT5E, dnear) &&
       site_cable_price(CAB_CAT6, dnear) > site_cable_price(CAB_CAT5E, dnear));

    /* BETWEEN TWO NAMED ENDS, from the chair, which is the other spelling. */
    say(&ses, "go f0.goods", &o);
    snprintf(line, sizeof line, "quote core #%d", far);
    q = say(&ses, line, &o);
    ck("`quote <a> <b>` quotes two named ends from wherever you happen to be",
       has(q, "a run from core:0 in d0 Engineering") && metres_of(q) == dfar &&
       ses.b.rooms[ses.room].kind == RM_GOODS);
    ck("a name that is neither a box nor a room is refused in words",
       has(say(&ses, "quote nowhere", &o), "there is no room or box called"));

    /* AND THE ONE THAT MATTERS. Buy it, put it there, run it, compare. */
    static const char *KIT[] = {
        "buy switch8 fsw", "go goods", "carry fsw", NULL
    };
    for (int i = 0; KIT[i]; i++) say(&ses, KIT[i], &o);
    snprintf(line, sizeof line, "go #%d", far);
    say(&ses, line, &o);
    say(&ses, "drop", &o);
    q = say(&ses, "quote fsw core", &o);
    int qm = metres_of(q);
    int qp = site_cable_price(CAB_CAT5E, qm);
    snprintf(want, sizeof want, "  cat5e   %11d", qp);
    ck("standing at the box, the quote prices the run in every grade",
       qm == dfar && has(q, want));
    money = ses.s.money;
    say(&ses, "cable fsw:0 core:0 cat5e", &o);
    /* Link 0 is the lead the building came with; link 1 is this run. */
    ck("and the run it quoted is the run that was laid, to the metre",
       ses.s.nlink == 2 && ses.s.link[1].metres == qm);
    ck("and the price it quoted is the money that actually left the account",
       ses.s.link[1].cost == qp && money - ses.s.money == qp);
    printf("    quoted %d m of cat5e at %d; paid %d for %d m\n",
           qm, qp, ses.s.link[1].cost, ses.s.link[1].metres);
    /* AND THE SPEED. The quote said what it would come up at; `show` reads
     * the port. */
    int fsw = site_dev_by_name(&ses.s, "fsw");
    char mb[32];
    int came_up = net_port_speed(ses.s.net, ses.s.dev[fsw].node, 0);
    snprintf(mb, sizeof mb, "%d Mb", came_up);
    ck("and the speed it promised is the speed the port really came up at",
       came_up > 0 && has(q, mb) &&
       came_up == site_cable_speed(CAB_CAT5E, qm));

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
    printf("\na deck server on vlan subinterfaces, across a power cut\n");
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

/* ------------------- a server addressed ONLY on vlans, twice across the mains
 *
 * A playtester reached day 62 and reported the thing that turned a six-box
 * repair morning into six repairs and six workarounds: every floor server
 * built the way D27 recommends -- addressed on tagged subinterfaces and
 * nothing else -- would not come back from a power cut. Nothing was damaged.
 * `pkg verify` was clean on all of them. sync_disk writes no stanza for a
 * card that has no address, quite rightly, so the first line of the file was
 * `iface eth0.12`, and netd took that as the name of a CARD, compared it
 * against the name udev gives the machine's device, and refused to start:
 *
 *     netd: /etc/net/interfaces: configures eth0.12, but udev names this
 *           machine's network device eth0
 *       refusing to start: there is no such interface
 *     [DOWN at services]
 *
 * The only fix available in the game was to open the file and insert a bare
 * `iface eth0` above it -- and the NEXT tower-side verb rewrote the file and
 * took it out again, so the workaround did not survive the thing it was a
 * workaround for. That is why the second half of this is here: a power cut,
 * then a config verb, then another power cut.
 *
 * The fault was netd's, not sync_disk's. `eth0.12` is a tagged subinterface
 * OF eth0 and naming it names the card underneath it; the file was telling
 * the truth and the daemon was misreading it. The second socket on the back
 * of a two-socket server is the same mistake in another spelling, so it is
 * checked here too -- while `iface eth1` on a box that HAS one socket must
 * still fail, because that is a fault the breaker deals and a repair the
 * player has to be able to find. */
static void check_vlan_only_server(int *passed, int *total)
{
    P = passed; T = total;
    printf("\na server addressed only on vlans, across two power cuts\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};
    static const char *SCRIPT[] = {
        "buy switch24 core", "buy server srv6", "buy pc desk1",
        "go goods", "carry core",  "go mdf", "drop",
        "go goods", "carry srv6",  "go mdf", "drop",
        "go goods", "carry desk1", "go mdf", "drop",
        "cable core:1 srv6:0 cat6",
        "cable core:2 desk1:0 cat6",
        "power srv6 on",
        "power desk1 on",
        "subif srv6 0 12 10.12.0.1/24",
        "gw srv6 10.12.0.254",
        "trunk core 1 12",
        "vlan core 2 12",
        "dhcpd srv6 10.12.0.100 20 24 10.12.0.1 10.12.0.1",
        "httpd srv6",
        NULL
    };
    for (int i = 0; SCRIPT[i]; i++) say(&ses, SCRIPT[i], &o);

    say(&ses, "plug srv6", &o);
    const char *conf = say(&ses, "cat /etc/net/interfaces", &o);
    ck("its whole configuration is one subinterface, with no card stanza at all",
       has(conf, "iface eth0.12") && !has(conf, "iface eth0\n"));
    say(&ses, "unplug", &o);
    ck("and it serves a desk on that vlan while it is up",
       has(say(&ses, "dhcp desk1", &o), "10.12.0.100"));

    /* ONE. The mains goes, and the box has to come back off its own disk. */
    say(&ses, "power srv6 off", &o);
    say(&ses, "power srv6 on", &o);
    const char *boot = say(&ses, "plug srv6", &o);
    ck("after the power cut it finishes booting",
       has(boot, "[UP at target]") && !has(boot, "DOWN at services"));
    ck("and netd is running, not dead in a respawn loop",
       has(say(&ses, "svc", &o), "net              running"));
    ck("and its own kernel has the subinterface and the address back",
       has(say(&ses, "ip addr", &o), "eth0.12") && has(o.p, "10.12.0.1/24"));
    say(&ses, "unplug", &o);
    ck("and the tower sees it addressed",
       has(say(&ses, "show srv6", &o), "10.12.0.1/24"));
    ck("and it is serving the pool that was riding on the vlan",
       has(say(&ses, "dhcp desk1", &o), "10.12.0.10"));  /* .100 or .101: the
                                                          * pool is what
                                                          * matters here, not
                                                          * which lease */

    /* TWO. A tower-side verb rewrites the whole file -- which is what erased
     * the playtester's hand-edited workaround -- and then the mains goes
     * again. This is the half that makes the difference between a fix and a
     * thing the player has to retype after every command they use. */
    say(&ses, "resolver srv6 198.51.100.1", &o);
    say(&ses, "plug srv6", &o);
    ck("a config verb rewrites the file and it still names only the vlan",
       has(say(&ses, "cat /etc/net/interfaces", &o), "iface eth0.12"));
    say(&ses, "unplug", &o);
    say(&ses, "power srv6 off", &o);
    say(&ses, "power srv6 on", &o);
    const char *boot2 = say(&ses, "plug srv6", &o);
    ck("and after the config verb and a second power cut it still boots",
       has(boot2, "[UP at target]") && !has(boot2, "DOWN at services"));
    ck("with netd running and the resolver the tower gave it",
       has(say(&ses, "svc", &o), "net              running") &&
       has(say(&ses, "cat /etc/resolv.conf", &o), "198.51.100.1"));
    say(&ses, "unplug", &o);
    ck("and still serving the desk, which is what the box is for",
       has(say(&ses, "dhcp desk1", &o), "10.12.0.10"));

    /* THE SECOND SOCKET, same mistake in another spelling: a floor server
     * cabled into port 1 and addressed there is describing a card the kernel
     * really found, whatever udev's one rule names. */
    say(&ses, "buy server srv7", &o);
    say(&ses, "spool back", &o);
    say(&ses, "go goods", &o); say(&ses, "carry srv7", &o);
    say(&ses, "go mdf", &o); say(&ses, "drop", &o);
    say(&ses, "cable core:4 srv7:1 cat6", &o);
    say(&ses, "power srv7 on", &o);
    say(&ses, "addr srv7:1 10.0.7.10/24", &o);
    say(&ses, "power srv7 off", &o);
    say(&ses, "power srv7 on", &o);
    const char *b3 = say(&ses, "plug srv7", &o);
    ck("a box addressed on its second socket comes back up too",
       has(b3, "[UP at target]") && !has(b3, "DOWN at services"));
    ck("and its config names that socket and nothing else",
       has(say(&ses, "cat /etc/net/interfaces", &o), "iface eth1") &&
       has(o.p, "10.0.7.10"));
    /* AND THE FAULT THIS CHECK EXISTS FOR IS STILL A FAULT. Edit the config
     * to name a card this box does not have and netd must refuse, or the
     * breaker's renamed-interface fault becomes undiagnosable. */
    say(&ses, "ed /etc/net/interfaces 1c \"iface eth9\" . w", &o);
    say(&ses, "unplug", &o);
    say(&ses, "power srv7 off", &o);
    const char *b4 = say(&ses, "power srv7 on", &o);
    ck("but a card the machine does not have still stops the boot, loudly",
       has(b4, "DOWN at services") && has(b4, "eth9") &&
       has(b4, "there is no such interface"));
    /* AND THE BOX IS REALLY DOWN, NOT DOWN ONLY IN THE BOOT LOG -- and it is
     * down at SERVICES, which since D37 is the one failed stage that still
     * has a login behind it. The root filesystem mounted, init came up, and a
     * unit did not, so there is a getty on this line and `pkg reinstall` is a
     * repair a person can perform on it. `plug` says both facts, because a
     * player who reads only the prompt would think the box was fine. */
    {
        const char *r = say(&ses, "plug srv7", &o);
        char pr[64];
        session_prompt(&ses, pr, sizeof pr);
        ck("and the box is really down, not down only in the boot log",
           has(r, "DOWN at services"));
        ck("and the lead says why there is a prompt on a box that is down",
           has(r, "there IS a login on this line") &&
           has(r, "What did not is a service") && has(pr, "root@srv7"));
    }
    say(&ses, "unplug", &o);

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

/* ================================================== sitting at somebody's desk
 *
 * D31. A tenancy's complaint is a fact about their machines, and until this
 * existed the only way to read it was a number in `service`. What is checked
 * here is the whole of the claim: that the desk is a REAL machine whose tools
 * agree with the wire, that its complaint is legible from its own console,
 * that sitting at it changes nothing about the network, and -- the one that
 * would quietly cost gigabytes if it stopped being true -- that exactly one
 * of these machines can exist at a time and that standing up frees it.
 */
/* WHERE THE SESSION SAYS YOU ARE, asked the way a player asks it: off the
 * prompt. Deliberately not `ses.where == SES_SEAT` -- this file has to be
 * compilable against the tree as it was before the verb existed, so that
 * "this check fails without the feature" can be demonstrated rather than
 * asserted, and an enum member that does not exist yet is a build error
 * rather than a failing check. */
static bool seated_at(const Session *ses, const char *name)
{
    char p[64];
    session_prompt(ses, p, sizeof p);
    return strncmp(p, "desk:", 5) == 0 && has(p, name);
}

static bool on_your_feet(const Session *ses)
{
    char p[64];
    session_prompt(ses, p, sizeof p);
    return strncmp(p, "desk:", 5) != 0;
}

static int desk_machines(const Session *ses)
{
    int n = 0;
    for (int i = 0; i < ses->s.ndev; i++)
        if (ses->s.dev[i].kind == SDEV_DESK && ses->mach[i]) n++;
    return n;
}

/* Run the clock to the first tenancy with desks in a room, and stand in it. */
static int desks_are_in(Session *ses, Buf *o)
{
    int ti = -1;
    for (int guard = 0; guard < 400 && ti < 0; guard++) {
        say(ses, "day 1", o);
        for (int i = 0; i < ses->s.ntenant; i++)
            if (ses->s.tenant[i].moved && ses->s.tenant[i].ndesk) { ti = i; break; }
    }
    return ti;
}

static void check_sit(int *passed, int *total)
{
    P = passed; T = total;
    printf("\nwalking to a tenant's desk and using their computer\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 100000)) { ck("a session starts", false); return; }
    Buf o = {0};

    ck("`desks` before anybody moves in says the building is empty of people",
       has(say(&ses, "desks", &o), "not a desk in the building"));

    int ti = desks_are_in(&ses, &o);
    if (ti < 0) { ck("a tenancy moves in with desks", false); goto done; }
    const SiteTenant *t = &ses.s.tenant[ti];
    int d0 = t->desk0;
    char first[NET_NAME_MAX];
    snprintf(first, sizeof first, "%s", ses.s.dev[d0].name);

    /* ---- THE MODEL IS WHAT SAYS WHERE A PERSON SITS. D23's rule: the 3D
     * view is never the source of truth, so everything the view needs about
     * a person and their machine has to come out of a socket. */
    {
        char line[32];
        snprintf(line, sizeof line, "desks %d", t->tenant);
        const char *r = say(&ses, line, &o);
        char room[48];
        snprintf(room, sizeof room, "#%d", ses.s.dev[d0].room);
        ck("`desks <tenant>` names every desk, the room it is in and its state",
           has(r, first) && has(r, room) && has(r, "no link"));
        /* A name, and the same name every time: a floor of numbered cards is
         * not a floor of people, and the view has to read the nameplate off
         * the model rather than invent one. */
        Buf again = {0};
        buf_puts(&again, r);
        const char *r2 = say(&ses, line, &o);
        ck("and the person at each desk is the same person on the second ask",
           again.p && *again.p && has(r2, first) && strcmp(again.p, r2) == 0);
        buf_free(&again);
    }

    /* ---- YOU HAVE TO BE IN THEIR OFFICE, like everything else in this game. */
    {
        char line[32];
        snprintf(line, sizeof line, "sit %s", first);
        const char *r = say(&ses, line, &o);
        ck("sitting at a desk on another deck is refused and names the room",
           has(r, "and you are not") && has(r, "their office") &&
           on_your_feet(&ses));
    }

    /* ---- AND IT HAS TO BE SOMEBODY'S DESK. Your own kit gets the cart. */
    say(&ses, "sit uplink", &o);
    ck("`sit` on your own kit is refused and points at the crash cart",
       has(o.p, "not somebody's desk") && has(o.p, "plug uplink"));

    /* ---- Now cable them, so they have LINK and no address: the commonest
     * unhappy tenancy in the game, and the one this feature is for. */
    {
        char line[64];
        say(&ses, "buy switch24 dsw", &o);
        say(&ses, "go goods", &o);
        say(&ses, "carry dsw", &o);
        snprintf(line, sizeof line, "go %s", first);
        say(&ses, line, &o);
        say(&ses, "drop", &o);
        snprintf(line, sizeof line, "serve %d dsw", t->tenant);
        say(&ses, line, &o);
        say(&ses, "day 4", &o);
    }
    ck("the tenancy is now striking with copper in every desk and no address",
       ses.s.tenant[ti].strikes > 0 &&
       net_port_state(ses.s.net, ses.s.dev[d0].node, 0) == PORT_UP &&
       !net_if_get_addr(ses.s.net, ses.s.dev[d0].node, 0));

    /* ---- SITTING DOWN. */
    {
        char line[32];
        snprintf(line, sizeof line, "sit %s", first);
        const char *r = say(&ses, line, &o);
        ck("you can sit down at a desk in the room you are standing in",
           seated_at(&ses, first) && has(r, "chair"));
        ck("and it says whose machine it is and that nothing you leave stays",
           has(r, "not yours") && has(r, "nothing you leave on it stays"));
        char p[64];
        session_prompt(&ses, p, sizeof p);
        ck("the prompt names the desk and does not claim a user that has no "
           "account", has(p, first) && !has(p, "root@"));
    }
    ck("a booted operating system exists for the desk you are sat at, and one",
       desk_machines(&ses) == 1);

    /* ---- THE COMPLAINT, READ OFF THEIR OWN MACHINE. Nothing below is a
     * string this game wrote about the desk: every line is a program running
     * on an emulated processor reading state the kernel really holds. */
    ck("`ip addr` on their machine says the card has no address",
       has(say(&ses, "ip addr", &o), "eth0") && has(o.p, "no address"));
    ck("and /etc/net/interfaces says it asked -- so it asked and got nothing",
       has(say(&ses, "cat /etc/net/interfaces", &o), "address dhcp"));
    /* The image ships /etc/resolv.conf naming 10.0.2.3, which is a host in
     * the break-fix world and not in this building at all. A resolver comes
     * with a lease and this machine has not got one. */
    ck("and the resolver file does not name a box out of another game's world",
       has(say(&ses, "cat /etc/resolv.conf", &o), "No lease has arrived") &&
       !has(o.p, "10.0.2.3"));
    ck("and a ping off their machine fails for the reason it really fails for",
       has(say(&ses, "ping 198.51.100.1", &o), "unreachable"));
    ck("the shell is the machine's: a command it has not got is not found",
       has(say(&ses, "notaprogram", &o), "command not found"));

    /* ---- Every tool the seat's own help offers has to be on the machine.
     * Same rule the shell help is held to, arriving from the same direction. */
    {
        Buf h = {0};
        buf_puts(&h, say(&ses, "help", &o));
        static const char *PROG[] = { "ip", "ping", "traceroute", "netstat",
                                      "ss", "arp", "tcpdump", "svc", "ps",
                                      "dmesg", "cat", NULL };
        bool exists = true, listed = true;
        for (int i = 0; PROG[i]; i++) {
            if (has(say(&ses, PROG[i], &o), "command not found")) {
                printf("    the seat help names `%s` and their machine has no "
                       "such program\n", PROG[i]);
                exists = false;
            }
            if (!has(h.p, PROG[i])) {
                printf("    the seat help stopped naming `%s`\n", PROG[i]);
                listed = false;
            }
        }
        ck("every program the seat's help names is really on their machine",
           exists);
        ck("and the seat's help still names every one of them", listed);
        buf_free(&h);
    }

    /* ---- AND THE OTHER DIRECTION, WHICH IS THE SAME HOLE THE CRASH CART HAD.
     * See check_around_the_shell(): this is a guest shell too, so a verb of
     * the building typed here is `command not found` and has to say which
     * prompt it belongs to -- and the word out of this one is `stand`, not
     * `unplug`. */
    {
        bool located = true;
        for (int i = 0; TOWER_ONLY[i]; i++) {
            /* `sit` and `stand` are the seat's own two words and the session
             * takes them before the machine ever sees them. */
            if (strcmp(TOWER_ONLY[i], "stand") == 0) continue;
            const char *a = say(&ses, TOWER_ONLY[i], &o);
            if (!notfound(a, TOWER_ONLY[i])) continue;
            if (!has(a, "TOWER verb") || !has(a, "stand")) {
                printf("    `%s` in somebody's chair is a dead end: %s",
                       TOWER_ONLY[i], a);
                located = false;
            }
        }
        ck("a verb of the building typed in somebody's chair says where it "
           "works", located);
    }

    /* ---- STANDING UP GIVES THE MACHINE BACK, and that is the memory cap.
     * A booted Machine measures 18 MB of resident memory on this build and a
     * full tower is 176 desks. If this check ever fails, the feature costs
     * three gigabytes. */
    {
        const char *r = say(&ses, "stand", &o);
        ck("`stand` gets you up and says the machine goes back to being theirs",
           on_your_feet(&ses) && has(r, "theirs again"));
    }
    ck("and no operating system is left behind for any desk in the building",
       desk_machines(&ses) == 0);

    /* Twenty desks, one after another: the cost is one machine, never twenty,
     * because a person has one backside and sits in one chair. */
    {
        int most = 0;
        for (int j = 0; j < ses.s.tenant[ti].ndesk && j < 20; j++) {
            char line[32];
            snprintf(line, sizeof line, "sit %s", ses.s.dev[d0 + j].name);
            say(&ses, line, &o);
            if (desk_machines(&ses) > most) most = desk_machines(&ses);
            say(&ses, "stand", &o);
        }
        ck("sitting at every desk of a tenancy in turn never holds more than one",
           most == 1 && desk_machines(&ses) == 0);
    }

    /* AND MOVING STRAIGHT FROM ONE CHAIR TO THE NEXT still holds one, which
     * is the shape of the cap that could most easily go wrong: `sit` from
     * inside the seat stands you up first. */
    if (ses.s.tenant[ti].ndesk > 1) {
        char line[32];
        snprintf(line, sizeof line, "sit %s", first);
        say(&ses, line, &o);
        snprintf(line, sizeof line, "sit %s", ses.s.dev[d0 + 1].name);
        say(&ses, line, &o);
        ck("moving straight to the next desk holds one machine, not two",
           desk_machines(&ses) == 1 && seated_at(&ses, ses.s.dev[d0 + 1].name));
        say(&ses, "stand", &o);
        ck("and standing up from that one leaves nothing behind either",
           desk_machines(&ses) == 0 && on_your_feet(&ses));
    }

    /* ---- AND IT IS A DIAGNOSTIC, NOT AN INTERVENTION. Waking their machine
     * must not renew a lease, re-point a resolver or otherwise move the
     * network the player is being judged on -- a player who made a striking
     * tenancy worse by looking at it would never look again. */
    {
        say(&ses, "buy router drt", &o);
        say(&ses, "go goods", &o);
        say(&ses, "carry drt", &o);
        char line[64];
        snprintf(line, sizeof line, "go %s", first);
        say(&ses, line, &o);
        say(&ses, "drop", &o);
        say(&ses, "cable drt dsw cat6", &o);
        say(&ses, "addr drt:0 10.9.1.1/24", &o);
        say(&ses, "router drt on", &o);
        say(&ses, "dhcpd drt 10.9.1.100 40 24 10.9.1.1 10.9.1.1", &o);
        say(&ses, "day 1", &o);
        uint32_t before = net_if_get_addr(ses.s.net, ses.s.dev[d0].node, 0);
        uint32_t gw_before = net_get_gateway(ses.s.net, ses.s.dev[d0].node);
        uint32_t ns_before = net_get_resolver(ses.s.net, ses.s.dev[d0].node);
        ck("with a pool on the segment the desks really do get addresses",
           before != 0);
        snprintf(line, sizeof line, "sit %s", first);
        say(&ses, line, &o);
        char ip[20];
        net_fmt_ip(before, ip, sizeof ip);
        ck("and `ip addr` on the desk prints the address the SITE says it has",
           has(say(&ses, "ip addr", &o), ip));
        ck("and /etc/net/interfaces says the address is a lease, not a decision",
           has(say(&ses, "cat /etc/net/interfaces", &o), "lease") &&
           has(o.p, ip));
        say(&ses, "stand", &o);
        ck("standing up leaves the desk's address, gateway and resolver alone",
           net_if_get_addr(ses.s.net, ses.s.dev[d0].node, 0) == before &&
           net_get_gateway(ses.s.net, ses.s.dev[d0].node) == gw_before &&
           net_get_resolver(ses.s.net, ses.s.dev[d0].node) == ns_before);
    }

done:
    buf_free(&o);
    session_end(&ses);
}

/* ================================ D37. THE FIRST FIVE MINUTES, AS A GATE ==
 *
 * The owner walked into his own tower and hit both halves of this within
 * minutes: *"The server in the default rack isn't booting, but it's also not
 * plugged into any power"*, and on the crash cart *"if the thing's not
 * powered on, it shouldn't offer a prompt at all... Potentially maybe a
 * no-connection prompt that gives you the option to attempt to power cycle
 * whatever you're attached to. That way you can watch boot up messages."*
 *
 * They are one job. A box with no power gives you nothing down a serial
 * lead, and that nothing is the diagnosis. This plays the whole of it in the
 * order a person hits it, over the same session a socket gets. */
static void check_dead_console(int *passed, int *total)
{
    P = passed; T = total;
    printf("\na serial lead into a box with no power in it\n");
    AUTOPOWER = false;          /* this gate IS about a box nothing feeds */
    Session ses;
    if (!session_start(&ses, GATE_SEED, 200000)) { ck("a session starts", false); return; }
    Buf o = {0};

    /* A BOX NOTHING FEEDS, which is now the ordinary state of every box that
     * has just been delivered. It used to take filling a cupboard's wall
     * until the room had no socket left; power comes down a run you pull, so
     * a box you have not pulled one to is dark and that is the whole of it.
     * The room does not come into it any more, which is why this is four
     * lines instead of fourteen. */
    int comms = -1;
    for (int i = 0; i < ses.b.nrooms; i++)
        if (ses.b.rooms[i].kind == RM_COMMS && ses.b.rooms[i].floor == 1) comms = i;
    if (comms < 0) { ck("the tower has a comms cupboard on deck 1", false); goto done; }
    char room[24];
    snprintf(room, sizeof room, "#%d", comms);
    say(&ses, "buy server srv1", &o);
    char dl[64];
    snprintf(dl, sizeof dl, "deliver srv1 %s", room);
    const char *dropped = say(&ses, dl, &o);
    int d = site_dev_by_name(&ses.s, "srv1");
    if (d < 0) { ck("a server is delivered into the cupboard", false); goto done; }

    ck("a box carried into a room with no conduit to it is not plugged in",
       !ses.s.dev[d].mains && !ses.s.dev[d].powered);
    ck("and `look` in that room says so about the box on the deck",
       has(say(&ses, "look", &o), "NOT PLUGGED IN"));
    (void)dropped;

    /* THE OWNER'S EXACT MOMENT: a server that will not start. */
    const char *btn = say(&ses, "power srv1 on", &o);
    ck("pressing the button does nothing, and the game says nothing happened",
       has(btn, "nothing happens") && !ses.s.dev[d].powered);
    ck("and it says WHICH of the two reasons a box does not start it is",
       has(btn, "NOTHING IS FEEDING IT"));

    /* AND THE LEAD. No prompt, no history, and the way out of it. */
    const char *lead = say(&ses, "plug srv1", &o);
    char pr[64];
    session_prompt(&ses, pr, sizeof pr);
    ck("a serial lead into it offers no prompt at all",
       !has(pr, "root@") && !has(pr, "#") && has(pr, "no console"));
    ck("what it offers instead is the silence that is really on the wire",
       has(lead, "nothing comes back") &&
       has(lead, "A serial line carries what the far end sends"));
    ck("and no machine was installed, because nothing booted",
       ses.mach[d] == NULL);
    ck("typing a command at it is not refused -- it is unheard",
       has(say(&ses, "uname -a", &o), "not running anything that could read"));
    ck("and the button still does nothing from the console line either",
       has(say(&ses, "power on", &o), "nothing happens"));

    /* THE LEAD COMES OUT FIRST. While the handset is on a box's console every
     * line typed goes to THAT MACHINE, so `feed` was being offered to a
     * server that is not running anything that could read it -- which is the
     * right answer to the wrong question, and exactly what the gate above
     * asserts about `uname -a`. A player pulls the lead before they go and
     * do something to the building.
     */
    say(&ses, "unplug", &o);

    /* AND THE WAY OUT OF IT IS A RUN. `mains <box> on` used to be the move
     * and there is no wall to plug into any more, so it refuses and names
     * the two verbs that do work -- because a player who has been typing
     * `mains` for a fortnight deserves the sentence rather than a no. */
    const char *nowall = say(&ses, "mains srv1 on", &o);
    ck("`mains on` refuses, and names the verb that replaced it",
       has(nowall, "conduit") && !ses.s.dev[d].mains);
    long money = ses.s.money;
    const char *ran = say(&ses, "feed srv1", &o);
    ck("`feed` pulls a run to it, off the nearest source with a hole in it",
       has(ran, "conduit") && ses.s.money < money);
    ck("and the plug is in: the box is fed and its run says what it carries",
       ses.s.dev[d].mains && has(say(&ses, "conduits", &o), "srv1"));

    /* AND NOW THE BOOT MESSAGES COME UP THE LINE, which is what the owner
     * asked the no-connection prompt to be for. The lead goes back in first,
     * because it was pulled to run the conduit. */
    say(&ses, "plug srv1", &o);
    const char *boot = say(&ses, "power on", &o);
    session_prompt(&ses, pr, sizeof pr);
    ck("the button works now, and the boot comes up the lead as it happens",
       has(boot, "zbios") && has(boot, "UP at target") && ses.s.dev[d].powered);
    ck("and only THEN does the line become a shell, with a prompt on it",
       has(pr, "root@srv1") && ses.mach[d] && ses.mach[d]->boot.running);
    ck("and it is the real machine, answering off its own disk",
       has(say(&ses, "cat /etc/hostname", &o), "srv1"));
    say(&ses, "unplug", &o);

    /* AND THE OTHER HALF OF THE SAME RULE: A BOX THAT IS POWERED AND DID NOT
     * BOOT. Break the bootloader's kernel from the box's own shell, cycle it,
     * and there is nothing on the wire again -- because no root filesystem
     * was ever mounted, so there is no userspace to have a getty in. This is
     * the case the owner meant by *"if it's not booting, it shouldn't offer a
     * prompt at all"*, and it is different from a box whose netd would not
     * stay up, which does have a login on it. */
    say(&ses, "plug srv1", &o);
    say(&ses, "rm /boot/vmnomuz", &o);
    say(&ses, "unplug", &o);
    say(&ses, "power srv1 off", &o);
    const char *again = say(&ses, "power srv1 on", &o);
    ck("a box whose kernel is gone comes up and stops, with the reason on "
       "the line",
       has(again, "/boot/vmnomuz: not found") && has(again, "DOWN at kernel"));
    {
        const char *r = say(&ses, "plug srv1", &o);
        session_prompt(&ses, pr, sizeof pr);
        ck("and a lead into it offers no login, because there is nowhere for "
           "one to be",
           has(r, "no login on the other end") && !has(pr, "root@") &&
           has(pr, "no console"));
        ck("and it does not replay the boot log, which the wire has no "
           "memory of",
           !has(r, "zbios") && has(r, "no memory of what it carried"));
        ck("and it points at the two things that are left: boot it again, or "
           "the medium",
           has(r, "power srv1 off") && has(r, "rescue srv1"));
        say(&ses, "unplug", &o);
    }

    /* AND YOU DO NOT WALK OFF WITH A RUNNING SERVER. Picking a machine up
     * starts with pulling its plug out, and that is a verb of its own. */
    ck("`carry` on a running machine is refused, and names the shutdown",
       has(say(&ses, "carry srv1", &o), "power srv1 off") &&
       ses.carrying < 0);
done:
    AUTOPOWER = true;           /* every other gate wants its tower lit */
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
    check_desk_complaint(passed, total);
    check_booted(passed, total);
    check_power(passed, total);
    check_dead_console(passed, total);
    check_services(passed, total);
    check_refusals(passed, total);
    check_prompt(passed, total);
    check_around_the_shell(passed, total);
    check_inventory(passed, total);
    check_cable_batch(passed, total);
    check_deliver_played(passed, total);
    check_deliver_refuses(passed, total);
    check_tag_hop(passed, total);
    check_jack_played(passed, total);
    check_quote_played(passed, total);
    check_vlan_server_reboot(passed, total);
    check_vlan_only_server(passed, total);
    check_documented(passed, total);
    check_sit(passed, total);
    return 0;
}
