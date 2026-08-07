/* sitecheck.c — the gate on the rules, run as `bf --sitecheck`.
 *
 * WHAT IT REFUSES TO ASSERT. It does not assert that a generated topology is
 * correct, because there is no generated topology. The player builds the
 * network; this checks that the world they build it in has real rules, and
 * that building badly hurts for reasons nobody wrote down.
 *
 * Every check below starts from an EMPTY site -- one ISP socket in the MDF
 * and nothing else -- and does the ordering, carrying, cabling and
 * configuring through the same calls the 3D view will make. The
 * interesting ones are:
 *
 *   - an empty site has no connectivity at all, and the first cable is what
 *     creates it. Nothing is reachable by default.
 *   - a switch refuses the port it has not got, and a full switch is a
 *     purchase, not an error message.
 *   - a subnet exhausts by arithmetic, and a DHCP pool that has run out
 *     hands out nothing.
 *   - a run past what copper carries does not come up, at a distance that
 *     came out of bld_cable_all() on a real tower.
 *   - one tenant cannot reach another except by a route the player built.
 *   - AND THE ONE THAT MATTERS MOST: a flat network is measurably worse
 *     than a segmented one, doing identical work. Not a rule. A number.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nom.h"
#include "site.h"
#include "session.h"

static int passed, total;

static bool has(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

/* ================================ A BOX, POWERED, FOR A GATE THAT IS NOT
 * MEASURING POWER.
 *
 * Every tower in this file is built to measure something about the NETWORK --
 * a vlan, a lease, a riser's copper, a day's work -- and every one of them
 * needs its kit switched on to measure anything at all. Power used to be free
 * for them: a box installed in a room took one of that room's wall sockets and
 * came up. It comes down a conduit now, and a gate that had to design a power
 * tree before it could ask about a DHCP pool would be a gate nobody could read.
 *
 * So this is install-and-feed, and it REFUNDS the conduit, because the price
 * of power is measured in check_conduits() and nowhere else -- every money
 * assertion in this file was written against a tower where power cost nothing,
 * and they are still asking the question they were written to ask.
 *
 * What it does NOT do is soften the model. site_feed() is the same call a
 * player makes, it takes the same refusals, and when the core and every strip
 * are full a gate gets -1 exactly as a player would. */
/* AND THE SAME FOR A TOWER BUILT BY TYPING. Several gates here build with
 * `order`/`move` script lines rather than site_install(), so gate_box() never
 * sees those boxes and they stand dark. This feeds whatever is not fed, once,
 * after the script has run -- same call, same refusals, refunded for the same
 * reason gate_box() refunds.
 *
 * It buys a strip when the core runs out of ways out, which is what a player
 * does and what the refusal tells them to do. That does add a device, so a
 * gate that counts devices or names them by index must not use this -- and
 * the one that measures power does not: check_conduits() pulls every run it
 * needs by hand, because there the pulling IS the subject. */
static void gate_feed_all(Site *s)
{
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

static int gate_box(Site *s, int kind, int room, const char *name)
{
    int d = site_install(s, kind, room, name);
    if (d < 0) return d;
    long money = s->money, spent = s->spent;
    /* AND A STRIP WHEN THE WAYS OUT RUN OUT, which is what a player does and
     * what the refusal tells them to do. A core has eight outputs and one of
     * them is the run the building came with, so a gate standing up twenty-one
     * machines really does fill it -- measured, nineteen of twenty-nine boxes
     * dark on eight runs before this loop existed. Each strip costs one output
     * and gives five, so it keeps up. */
    /* AND ANYTHING THAT LOST ITS BUTTON ON THE WAY GETS IT BACK. A machine
     * whose run was borrowed comes back fed and off, and the gate that
     * pressed its button did so before the borrowing. This remembers what was
     * running and puts it back afterwards. */
    bool was_running[SITE_MAX_DEV];
    for (int i = 0; i < s->ndev; i++) was_running[i] = s->dev[i].powered;
    /* SPEND THE LAST WAY OUT OF THE CORE ON A STRIP, NOT ON A LOAD.
     *
     * Displacing a load later works and costs more than it looks: pulling a
     * plug is an unclean stop, and an unclean stop takes the machine's
     * ADDRESS with it. The gate below then pinged a box whose own routing
     * table was empty -- PING_NO_ROUTE, with every box fed and running. So
     * the last output is kept for the thing that multiplies outputs. */
    {
        int pcore = site_dev_by_name(s, "core0");
        int free_out = 0;
        if (pcore >= 0)
            for (int p = 0; p < s->dev[pcore].nports; p++) {
                bool used = false;
                for (int r = 0; r < site_conduit_count(s); r++)
                    if (s->cond[r].live && s->cond[r].from == pcore &&
                        s->cond[r].fport == p) { used = true; break; }
                if (!used) free_out++;
            }
        if (pcore >= 0 && free_out == 1 && !site_dev_fed(s, d, NULL)) {
            char sn[NET_NAME_MAX];
            snprintf(sn, sizeof sn, "gs%d", s->ndev);
            int st = site_install(s, SDEV_STRIP, room, sn);
            if (st >= 0) site_feed(s, st);
        }
    }
    for (int tries = 0; tries < 12 && !site_dev_fed(s, d, NULL); tries++) {
        if (site_feed(s, d) >= 0) break;
        char sn[NET_NAME_MAX];
        snprintf(sn, sizeof sn, "gs%d", s->ndev);
        int st = site_install(s, SDEV_STRIP, room, sn);
        if (st < 0) break;
        if (site_feed(s, st) >= 0) continue;
        /* AND WHEN THERE IS NOWHERE TO PUT THE STRIP EITHER, MAKE ROOM.
         *
         * This is a real corner and finding it here is the gate earning its
         * keep: a core has eight outputs, and once loads are hanging off all
         * of them a strip cannot be fed -- so "buy a strip, it gives you five
         * more" is advice you cannot take. What a person does is pull one
         * load off the core, put the strip on that output, and hang the load
         * back off the strip. Two boxes fed by one output instead of one.
         *
         * The player-facing half of this is ticketed: `feed`'s refusal should
         * say it, and ideally offer it. */
        /* AND PREFER TO MOVE AN APPLIANCE. Pulling a plug switches a machine
         * off and putting it back does not switch it on -- that is D37 and it
         * is right -- so moving a running SERVER leaves it fed and dark
         * unless somebody presses the button again. A switch has no button:
         * it comes up with the socket it is plugged into. Measured: moving
         * whatever came first left three of twenty-one machines fed and off. */
        int moved = -1;
        for (int pass = 0; pass < 2 && moved < 0; pass++) {
            for (int r = 0; r < site_conduit_count(s); r++) {
                const SiteConduit *c = &s->cond[r];
                if (!c->live) continue;
                if (s->dev[c->from].kind != SDEV_POWERCORE) continue;
                if (s->dev[c->to].kind == SDEV_STRIP) continue;
                if (c->to == s->ws) continue;     /* not the day-one run */
                if (pass == 0 && site_kind_has_os(s->dev[c->to].kind)) continue;
                moved = c->to;
                site_unconduit(s, r);
                break;
            }
        }
        if (moved < 0) break;
        bool was_on = s->dev[moved].powered;
        if (site_feed(s, st) < 0) break;
        site_feed(s, moved);
        /* AND THE BOX WHOSE RUN WE BORROWED GETS ITS BUTTON BACK. Pulling a
         * plug switches a machine off -- that is the whole of D37 -- and
         * putting the plug back does not switch it on. Without this the
         * displaced machine was fed, dark, and pinged nothing: measured, 0 of
         * 21 dark and pc0 mains 1 powered 0. */
        if (was_on) site_power(s, moved, true);
    }
    for (int i = 0; i < s->ndev; i++)
        if (was_running[i] && !s->dev[i].powered && site_dev_fed(s, i, NULL))
            site_power(s, i, true);
    s->money = money;
    s->spent = spent;
    return d;
}

static void ck(const char *what, bool ok)
{
    total++;
    if (ok) passed++;
    printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
}

/* The tower every check below is built inside. Eleven floors, and a lettable
 * room a hundred and sixteen metres of cable tray from the MDF -- which is
 * what makes the copper limit reachable by building badly, and is asserted
 * rather than assumed. */
#define GATE_SEED  7008ull

static bool leasable(int kind)
{
    return kind == RM_OFFICE || kind == RM_RESIDENCE ||
           kind == RM_SERVER || kind == RM_RETAIL;
}

/* A room on this floor that somebody could work in. */
static int a_room(const Building *b, int floor)
{
    for (int i = 0; i < b->nrooms; i++)
        if (b->rooms[i].floor == floor && leasable(b->rooms[i].kind)) return i;
    return -1;
}

/* ------------------------------------------------------- an empty site */
/* Day one. There is a socket in the MDF and nothing else in the building. */
static void check_empty(const Building *b)
{
    printf("day one -- a building with nothing in it\n");
    Site s;
    if (!site_new(&s, b, GATE_SEED, 100000)) { ck("a site starts", false); return; }

    /* THREE devices exist on the first morning and the player bought none of
     * them: the ISP's socket on the MDF wall, their own workstation on the
     * desk in front of it with its lead in that socket, and the building's
     * power core down in the plant room. All three are given on the same
     * terms -- they were here before you were, none is for sale, and none is
     * yours to carry off. There is one link in the building and nobody paid
     * for it. Everything else is theirs to build. */
    ck("three things were here before you were: the handoff, your machine, "
       "and the power core",
       s.ndev == 3 && s.dev[s.uplink].kind == SDEV_UPLINK &&
       site_dev_by_name(&s, "core0") >= 0 &&
       s.dev[site_dev_by_name(&s, "core0")].kind == SDEV_POWERCORE &&
       site_workstation(&s) >= 0 &&
       s.dev[site_workstation(&s)].kind == SDEV_WORKSTATION &&
       s.dev[site_workstation(&s)].room == (uint16_t)bld_find(b, 0, RM_MDF));
    ck("the workstation is in the handoff's only port, on a lead that cost nothing",
       s.nlink == 1 && s.link[0].cost == 0 &&
       site_dev_cabled(&s, site_workstation(&s)) &&
       site_free_port(&s, s.uplink) < 0 &&
       site_port_factory(&s, s.uplink, 0) == 0);
    ck("it is running, plugged into the wall, and not for sale",
       s.dev[site_workstation(&s)].powered && s.dev[site_workstation(&s)].mains &&
       !site_kind_for_sale(SDEV_WORKSTATION) &&
       gate_box(&s, SDEV_WORKSTATION, bld_find(b, 0, RM_MDF), "ws2") < 0 &&
       site_order(&s, SDEV_WORKSTATION, "ws3") < 0);

    /* Buy a machine, put it in an office, give it an address. It is not
     * plugged into anything, so it can reach nothing -- including the socket
     * in the same building. */
    int room = a_room(b, 3);
    int pc = gate_box(&s, SDEV_PC, room, "pc1");
    /* A COMPUTER ARRIVES SWITCHED OFF. Nothing of it is on the network until
     * somebody presses the button, which is what site_power is. */
    site_power(&s, pc, true);
    site_addr(&s, pc, 0, net_ip(10, 0, 1, 10), net_mask_bits(24));
    site_gateway(&s, pc, net_ip(10, 0, 1, 1));
    int rtt = 0;
    ck("a machine with an address and no cable reaches nothing",
       pc >= 0 && net_ping(s.net, s.dev[pc].node, s.wan_isp, &rtt) != PING_OK &&
       net_port_state(s.net, s.dev[pc].node, 0) == PORT_NOCABLE);

    /* Now build the smallest network that works: a switch in the comms
     * cupboard, a router beside the handoff, and three cables. */
    int comms = bld_find(b, 3, RM_COMMS), mdf = bld_find(b, 0, RM_MDF);
    int sw = gate_box(&s, SDEV_SWITCH8, comms, "sw3");
    int rt = gate_box(&s, SDEV_ROUTER, mdf, "rt");
    int l0 = site_cable(&s, rt, 0, s.uplink, 0, CAB_CAT6);
    int l1 = site_cable(&s, rt, 1, sw, 0, CAB_CAT6);
    int l2 = site_cable(&s, pc, 0, sw, 1, CAB_CAT6);
    ck("three cables come up", l0 >= 0 && l1 >= 0 && l2 >= 0 &&
       site_link_state(&s, l0) == PORT_UP && site_link_state(&s, l1) == PORT_UP &&
       site_link_state(&s, l2) == PORT_UP);

    /* Cable is not connectivity. Nothing is configured yet. */
    ck("cable alone still reaches nothing: nothing is configured",
       net_ping(s.net, s.dev[pc].node, s.wan_isp, &rtt) != PING_OK);

    site_addr(&s, rt, 0, s.wan_you, s.wan_mask);
    site_addr(&s, rt, 1, net_ip(10, 0, 1, 1), net_mask_bits(24));
    site_forwarding(&s, rt, true);
    site_gateway(&s, rt, s.wan_isp);
    ck("configured, the machine on deck 3 reaches the internet",
       net_ping(s.net, s.dev[pc].node, s.wan_isp, &rtt) == PING_OK);

    /* And the cable is what carries it: pull it out and it stops. */
    site_uncable(&s, l1);
    ck("pull the riser cable and it stops, at once",
       net_ping(s.net, s.dev[pc].node, s.wan_isp, &rtt) != PING_OK);

    printf("    that cost %ld: %d m of cable and three boxes\n",
           s.spent, s.link[l0].metres + s.link[l1].metres + s.link[l2].metres);
    site_free(&s);
}

/* ---------------------------------------------------------- the limits */
static void check_ports(const Building *b)
{
    printf("\nthe port you have not got\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    int comms = bld_find(b, 4, RM_COMMS);
    int sw = gate_box(&s, SDEV_SWITCH8, comms, "sw4");

    /* Fill it. Seven machines and an uplink is what an eight-port switch is,
     * and the eighth machine has nowhere to go. */
    int room = a_room(b, 4);
    int filled = 0;
    for (int i = 0; i < 8; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "pc%d", i);
        int pc = gate_box(&s, SDEV_PC, room, nm);
        if (pc < 0) break;
        site_power(&s, pc, true);
        if (site_cable(&s, pc, 0, sw, i, CAB_CAT6) >= 0) filled++;
    }
    ck("an eight port switch takes eight cables and no more", filled == 8);

    int pc = gate_box(&s, SDEV_PC, room, "pc-too-many");
    site_power(&s, pc, true);
    int l = site_cable(&s, pc, 0, sw, 8, CAB_CAT6);
    ck("the ninth is refused, by port number",
       l < 0 && s.err == SITE_ENOPORT);
    ck("and there is no free port to put it in instead",
       site_free_port(&s, sw) < 0);
    l = site_cable(&s, pc, 0, sw, 3, CAB_CAT6);
    ck("nor can it share one: something is already in it",
       l < 0 && s.err == SITE_EBUSY);
    printf("    the fix is a purchase, not a message: a switch24 is %d\n",
           site_kind_price(SDEV_SWITCH24));
    site_free(&s);
}

static void check_addresses(const Building *b)
{
    printf("\nthe addresses you have not got\n");
    ck("a /24 holds 254 machines and a /29 holds 6",
       site_hosts_in_mask(net_mask_bits(24)) == 254 &&
       site_hosts_in_mask(net_mask_bits(29)) == 6 &&
       site_hosts_in_mask(net_mask_bits(16)) == 65534);

    /* And the same arithmetic, with machines in rooms. A player who put a
     * /29 on a floor because it was six desks at the time. */
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    int comms = bld_find(b, 5, RM_COMMS), room = a_room(b, 5);
    int sw = gate_box(&s, SDEV_SWITCH24, comms, "sw5");
    int rt = gate_box(&s, SDEV_ROUTER, comms, "rt5");
    site_cable(&s, rt, 0, sw, 0, CAB_CAT6);
    site_addr(&s, rt, 0, net_ip(10, 0, 5, 1), net_mask_bits(29));
    /* A pool of four, which is what is left of a /29 after the router and
     * two static machines. Nothing here says "four": the player typed it. */
    site_dhcpd(&s, rt, net_ip(10, 0, 5, 2), 4, net_mask_bits(29),
               net_ip(10, 0, 5, 1), net_ip(10, 0, 5, 1));

    int got = 0, refused = 0;
    for (int i = 0; i < 6; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "d%d", i);
        int pc = gate_box(&s, SDEV_PC, room, nm);
        site_power(&s, pc, true);
        site_cable(&s, pc, 0, sw, i + 1, CAB_CAT6);
        if (site_dhcp(&s, pc)) got++; else refused++;
    }
    ck("the pool hands out what it has and then stops", got == 4 && refused == 2);
    ck("and the machines that missed out have no address at all",
       net_if_get_addr(s.net, s.dev[s.ndev - 1].node, 0) == 0);
    printf("    four leases from a /29, and the seventh desk needs a "
           "renumbering, not a switch\n");
    site_free(&s);
}

static void check_copper(const Building *b)
{
    printf("\nthe metres you have not got\n");
    int mdf = bld_find(b, 0, RM_MDF);
    double *dm = nom_alloc(sizeof(double) * (size_t)b->nrooms);
    bld_cable_all(b, mdf, dm);
    int far = -1;
    for (int r = 0; r < b->nrooms; r++)
        if (leasable(b->rooms[r].kind) && dm[r] < BLD_INF &&
            (far < 0 || dm[r] > dm[far])) far = r;
    double d = far >= 0 ? dm[far] : 0;
    nom_free(dm);
    if (far < 0) { ck("the tower has a room to cable", false); return; }
    int floor = b->rooms[far].floor;
    printf("    the farthest lettable room is on deck %d, %.1f m of tray "
           "from the Engineering\n", floor, d);
    ck("a tall tower puts a room past what copper carries", d + SITE_PATCH_M > 100);

    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    int sw = gate_box(&s, SDEV_SWITCH24, mdf, "core");
    int pc = gate_box(&s, SDEV_PC, far, "topdeck");
    site_power(&s, pc, true);
    int l = site_cable(&s, pc, 0, sw, 1, CAB_CAT6);
    ck("the cable is sold, laid and paid for", l >= 0 && s.link[l].cost > 0);
    ck("and it does not come up, because it is too long",
       site_link_state(&s, l) == PORT_TOOLONG);
    printf("    %d m of cat6, %d spent, and the link light never comes on\n",
           s.link[l].metres, s.link[l].cost);

    /* The fix a real installer makes: a switch on that floor instead. */
    int comms = bld_find(b, floor, RM_COMMS);
    int fsw = gate_box(&s, SDEV_SWITCH8, comms, "swtop");
    site_uncable(&s, l);
    int l2 = site_cable(&s, pc, 0, fsw, 1, CAB_CAT6);
    int l3 = site_cable(&s, fsw, 0, sw, 2, CAB_CAT6);
    ck("a switch in that deck's cupboard fixes it, and both runs come up",
       site_link_state(&s, l2) == PORT_UP && site_link_state(&s, l3) == PORT_UP);
    printf("    %d m to the cupboard and %d m up the riser: both up\n",
           s.link[l2].metres, s.link[l3].metres);
    site_free(&s);
}

/* -------------------------------------- and the speed you have not bought
 *
 * A LINK RUNS AT THE SLOWEST OF THREE THINGS: the port at each end and the
 * cable between them. Until D27 only the cable had a say, so a cat 6 patch
 * lead to a desk negotiated ten gigabit because the run was short, and a
 * playtester said the obvious thing: *"cat6 gives 10 Gb ports at these
 * distances. That makes the desk-cable choice feel free."*
 *
 * These are short runs in one room -- a patch lead, three metres -- so
 * distance is out of the argument and what is left is the box somebody
 * bought. Every number below is read off `net_port_speed`, which is what
 * `show` prints. */
static void check_port_speed(const Building *b)
{
    printf("\nthe speed is the port, not just the cable\n");
    int mdf = bld_find(b, 0, RM_MDF);
    Site s;
    site_new(&s, b, GATE_SEED, 200000);
    int core = gate_box(&s, SDEV_SWITCH24, mdf, "core");
    int edge = gate_box(&s, SDEV_ROUTER, mdf, "edge");
    int srv  = gate_box(&s, SDEV_SERVER, mdf, "files");
    int sw8  = gate_box(&s, SDEV_SWITCH8, mdf, "little");
    if (core < 0 || edge < 0 || srv < 0 || sw8 < 0) {
        ck("four boxes in the Engineering", false); site_free(&s); return;
    }
    int last = site_kind_ports(SDEV_SWITCH24) - 1;   /* the SFP+ pair       */

    int l = site_cable(&s, edge, 1, core, last, CAB_CAT6);
    int mb_up = net_port_speed(s.net, s.dev[edge].node, 1);
    ck("cat6 between a router and a core switch's uplink is ten gigabit",
       l >= 0 && mb_up == 10000);

    int l2 = site_cable(&s, core, 1, srv, 0, CAB_CAT6);
    int mb_srv = net_port_speed(s.net, s.dev[srv].node, 0);
    ck("the same cat6 into a server's card is a gigabit, because the card is",
       l2 >= 0 && mb_srv == 1000);

    /* BOTH ENDS OF A WIRE ARE ONE WIRE. The switch's own port on that link
     * is a 10 Gb SFP+ cage facing a gigabit server; a playtester read
     * `show uplink` at 500Mb and `show edge` at 1000Mb for a single link to
     * the handoff and had no way to tell which number the frames obey.
     * Ethernet negotiates to the slower end, and this is also the rate
     * port_tx clocks bits at, so it was a modelling fault and not only a
     * printing one. */
    {
        /* The handoff is rate-limited to the circuit -- 500 Mb here -- and
         * the router's card does ten gigabit, so this is the one link in the
         * game whose two ends genuinely disagree about what they can do. */
        int l4 = site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
        int at_isp = net_port_speed(s.net, s.dev[s.uplink].node, 0);
        int at_rtr = net_port_speed(s.net, s.dev[edge].node, 0);
        ck("a slow end makes a slow link, and both ends of it say the same "
           "number",
           l4 >= 0 && at_isp > 0 && at_isp == at_rtr && at_isp < 1000);
        Buf sh = {0};
        site_cmd(&s, "show edge", &sh);
        ck("and the fast end says the constraint is at the other one",
           sh.p && strstr(sh.p, "the far end does") != NULL);
        buf_free(&sh);
        printf("    the handoff reads %d Mb and the router's end of the same "
               "wire reads %d Mb\n", at_isp, at_rtr);
    }

    int l3 = site_cable(&s, core, 2, sw8, 0, CAB_FIBRE);
    int mb_8 = net_port_speed(s.net, s.dev[sw8].node, 0);
    ck("and fibre into a cheap eight-port switch is a gigabit too",
       l3 >= 0 && mb_8 == 1000);
    printf("    the same room, the same three metres: %d Mb to the router, "
           "%d Mb to the server\n", mb_up, mb_srv);

    /* AND `show` SAYS WHICH NUMBER IS WHICH, without calling a server a
     * circuit. That line was written when the ISP handoff was the only
     * rate-limited port in the game, so it read "the cable carries 10000Mb;
     * the circuit is 1000Mb" about a machine that is not on a circuit and
     * never was. Since port speed comes from the kit, every box has one. */
    {
        Buf sh = {0};
        site_cmd(&s, "show files", &sh);
        ck("`show` names the cable's rate and the port's, and calls neither a "
           "circuit",
           sh.p && strstr(sh.p, "carries 10000Mb") &&
           strstr(sh.p, "this port does 1000Mb") &&
           strstr(sh.p, "circuit") == NULL);
        buf_free(&sh);
    }

    /* AND THE MONEY SAYS THE SAME THING. Fibre into a gigabit box costs
     * several times what the copper that does the identical gigabit costs,
     * which is the whole of "a wrong answer you can afford to make". */
    ck("and the fibre that bought no extra speed cost more than the cat6 that "
       "bought none either", s.link[l3].cost > s.link[l2].cost);
    printf("    %d for the fibre run, %d for the cat6 run, both at %d Mb\n",
           s.link[l3].cost, s.link[l2].cost, mb_8);

    /* CHEAP COPPER IS A ROUNDING ERROR ON A DROP AND REAL MONEY ON A RISER,
     * because the ends are a person and the metres are the drum. A game that
     * charged half price for cat 5 at a desk would be selling a saving with
     * no cost attached to it: a desk pulls nine megabits and a hundred
     * megabit drop carries that fine. */
    int drop5  = site_cable_price(CAB_CAT5, 20),  drop5e = site_cable_price(CAB_CAT5E, 20);
    int riser5 = site_cable_price(CAB_CAT5, 80), riser5e = site_cable_price(CAB_CAT5E, 80);
    ck("cat5 saves under a fifth on a twenty metre desk drop",
       drop5 * 5 > drop5e * 4 && drop5 < drop5e);
    ck("and over a quarter on an eighty metre riser, which is where it bites",
       riser5 * 4 < riser5e * 3);
    printf("    20 m drop: cat5 %d, cat5e %d.   80 m riser: cat5 %d, cat5e %d\n",
           drop5, drop5e, riser5, riser5e);
    site_free(&s);
}

/* ------------------------------------------- the box says the same thing
 * twice */
/* A server was sold with two sockets, `site` printed two, and the network
 * world gave it four -- so `netstat` inside the machine and `show` outside it
 * disagreed about the same box, and `show` on a one-port pc listed four ports
 * while the refusal for using one was correct and said "numbered 0 to 0". One
 * of those numbers has to be the number. */
static void check_boxes(const Building *b)
{
    printf("\nthe sockets on the back, counted the same way twice\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    int room = a_room(b, 2);
    bool agree = true;
    for (int k = SDEV_SWITCH8; k < SDEV_KIND_COUNT; k++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "box%d", k);
        /* The player's own workstation is not orderable and is already
         * standing in the MDF, so it is counted where it stands rather than
         * bought -- the holes on the back of it are checked the same way as
         * every other kind's. */
        /* THE POWER KINDS HAVE HOLES THAT ARE NOT NETWORK HOLES. A core has
         * eight ways out and a strip six, and not one of them carries a
         * frame: they take conduit. So they have no cards in the netstack on
         * purpose, and counting them here would be asserting that a socket
         * for a kettle lead is an ethernet port. What they DO have to agree
         * about is checked in check_conduits(). */
        if (k == SDEV_POWERCORE || k == SDEV_STRIP) continue;
        int d = (k == SDEV_WORKSTATION) ? site_workstation(&s)
                                        : gate_box(&s, k, room, nm);
        if (d < 0) { agree = false; continue; }
        if (net_node_ports(s.net, s.dev[d].node) != site_kind_ports(k) ||
            s.dev[d].nports != site_kind_ports(k)) agree = false;
        Buf o = {0};
        site_dump_dev(&s, d, &o);
        char want[64];
        snprintf(want, sizeof want, "port %d ", site_kind_ports(k) - 1);
        char toomany[64];
        snprintf(toomany, sizeof toomany, "port %d ", site_kind_ports(k));
        if (!strstr(o.p ? o.p : "", want) || strstr(o.p ? o.p : "", toomany))
            agree = false;
        buf_free(&o);
    }
    ck("the catalogue, the site, the netstack and `show` count the same holes",
       agree);

    /* A vlan on a router's port was accepted and did nothing at all: a host
     * reads its interface's tag and never its port's. */
    int rt = gate_box(&s, SDEV_ROUTER, room, "rtv");
    ck("a vlan on a router's port is refused, and names what to use instead",
       !site_port_vlan(&s, rt, 1, 10) && s.err == SITE_ENOTSW &&
       strstr(site_err_text(s.err), "subif") != NULL);

    /* Two addresses, on two sockets, on one box. This is the whole of F1. */
    ck("a router takes an address on its second socket without losing its first",
       site_addr(&s, rt, 0, net_ip(198, 51, 100, 2), net_mask_bits(30)) &&
       site_addr(&s, rt, 1, net_ip(10, 0, 1, 1), net_mask_bits(24)) &&
       net_if_get_addr(s.net, s.dev[rt].node, 0) == net_ip(198, 51, 100, 2) &&
       net_if_get_addr(s.net, s.dev[rt].node, 1) == net_ip(10, 0, 1, 1));
    ck("but not on a socket it has not got",
       !site_addr(&s, rt, 4, net_ip(10, 0, 4, 1), net_mask_bits(24)) &&
       s.err == SITE_EIFACE);
    ck("nor the broadcast address of its own /30",
       !site_addr(&s, rt, 0, net_ip(198, 51, 100, 3), net_mask_bits(30)) &&
       s.err == SITE_EADDR);
    site_free(&s);
}

/* --------------------------------------------------- switched off is off */
/* THE TWO OUTLET GATES ARE GONE, and this note stands where they stood.
 *
 * check_power() measured how many sockets a room was BUILT with -- a cupboard
 * on a spur, a let office wired for people, and the asymmetry that put the
 * decision where the equipment was and never on a floor of desks.
 * check_mains() measured what happened when a wall filled up: the next box in
 * was dark, its button said which of the two things was wrong, and another
 * socket could be had for money on a circuit that eventually refused.
 *
 * Thirty-one assertions, every one of them true and useful about the game as
 * it was, and not one of them a fact about the game now: "per room outlets
 * will go away, all things will be powered by the new conduit power system."
 *
 * check_conduits() is what replaced them, and it asks what the new model can
 * be wrong about -- whether a run is priced off the same graph copper is,
 * whether it knows what is behind it, whether a fork adds up, and whether a
 * run over its rating takes down everything behind it rather than only the
 * load that tipped it. The limit moved from "how many holes does this room
 * have" to "does the run I pulled still have headroom", and the gates moved
 * with it. */


static void check_plug_pulled(const Building *b)
{
    printf("\nthe plug, pulled by hand, on something that was running\n");
    Session ses;
    if (!session_start(&ses, GATE_SEED, 200000)) { ck("a session starts", false); return; }
    Buf o = {0};
    static const char *BUILD[] = {
        "buy server one", "buy server two",
        /* AND A RUN TO EACH, because a box you carried into a room is not
         * plugged into anything until you pull one -- which is the whole of
         * what this gate then pulls back out again. */
        "go goods", "carry one", "go mdf", "drop", "feed one", "power one on",
        "go goods", "carry two", "go mdf", "drop", "feed two", "power two on",
        "ups two", NULL
    };
    for (int i = 0; BUILD[i]; i++) session_line(&ses, BUILD[i], &o);
    int a = site_dev_by_name(&ses.s, "one");
    int c = site_dev_by_name(&ses.s, "two");
    if (a < 0 || c < 0 || !ses.mach[a] || !ses.mach[c]) {
        ck("two servers boot in the Engineering", false);
        goto done;
    }
    ck("two servers are up, one of them on a battery",
       ses.mach[a]->boot.running && ses.mach[c]->boot.running &&
       !ses.mach[a]->fs_dirty && !ses.mach[c]->fs_dirty && ses.s.dev[c].ups);

    buf_clear(&o);
    session_line(&ses, "mains one off", &o);
    ck("pulling the plug on a running machine stops it",
       !ses.s.dev[a].powered && !ses.s.dev[a].mains);
    ck("and it went down the way a blackout takes one down: unclean",
       ses.mach[a]->fs_dirty);
    ck("and the player is told, at the moment it happens, what they just did",
       has(o.p, "blackout with one machine in it"));
    buf_clear(&o);
    session_line(&ses, "events", &o);
    ck("and it is in `events` beside the weather, because it is the same event",
       has(o.p, "unplugged while it was running"));

    buf_clear(&o);
    session_line(&ses, "mains two off", &o);
    ck("the one on a battery is stopped too -- the load has nowhere to go",
       !ses.s.dev[c].powered);
    ck("but the battery shut it down in an orderly way, so there is nothing "
       "to check",
       !ses.mach[c]->fs_dirty);
    buf_clear(&o);
    session_line(&ses, "events", &o);
    ck("and the battery is what `events` says saved it",
       has(o.p, "battery shut it down cleanly"));

    /* AND THE RUN COMES OUT WITH IT, which is the only reason a player would
     * ever pull one on purpose: the way out of the core it was using is free
     * for something else. */
    ck("pulling the plug frees the way out of the core it was using",
       !site_dev_fed(&ses.s, site_dev_by_name(&ses.s, "one"), NULL));
done:
    buf_free(&o);
    session_end(&ses);
}

/* --------------------------------------------------------- the tenants */
static void check_tenants(const Building *b)
{
    printf("\ntwo tenants who share a deck and must not share a network\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    int comms = bld_find(b, 3, RM_COMMS), mdf = bld_find(b, 0, RM_MDF);

    /* Two rooms on floor 3 belonging to different people. */
    int ra = -1, rb = -1;
    for (int i = 0; i < b->nrooms; i++) {
        const Room *r = &b->rooms[i];
        if (r->floor != 3 || !r->tenant || !leasable(r->kind)) continue;
        if (ra < 0) ra = i;
        else if (r->tenant != b->rooms[ra].tenant && rb < 0) rb = i;
    }
    if (ra < 0 || rb < 0) { ck("the deck has two tenancies on it", false);
                            site_free(&s); return; }

    int sw = gate_box(&s, SDEV_SWITCH24, comms, "sw3");
    int rt = gate_box(&s, SDEV_ROUTER, mdf, "rt");
    int a  = gate_box(&s, SDEV_PC, ra, "theirs");
    int c  = gate_box(&s, SDEV_PC, rb, "ours");
    site_power(&s, a, true);
    site_power(&s, c, true);
    site_cable(&s, rt, 0, sw, 0, CAB_CAT6);
    site_cable(&s, a, 0, sw, 1, CAB_CAT6);
    site_cable(&s, c, 0, sw, 2, CAB_CAT6);

    /* The player segments them: one vlan each, one trunk to the router, one
     * subinterface per vlan. Every one of these is a call they made. */
    site_port_trunk(&s, sw, 0, 10);
    site_port_trunk(&s, sw, 0, 20);
    site_port_vlan(&s, sw, 1, 10);
    site_port_vlan(&s, sw, 2, 20);
    site_subif(&s, rt, 0, 10, net_ip(10, 0, 10, 1), net_mask_bits(24));
    site_subif(&s, rt, 0, 20, net_ip(10, 0, 20, 1), net_mask_bits(24));
    site_forwarding(&s, rt, true);
    site_addr(&s, a, 0, net_ip(10, 0, 10, 10), net_mask_bits(24));
    site_gateway(&s, a, net_ip(10, 0, 10, 1));
    site_addr(&s, c, 0, net_ip(10, 0, 20, 10), net_mask_bits(24));
    site_gateway(&s, c, net_ip(10, 0, 20, 1));

    uint8_t mac[6];
    ck("neither can ARP for the other: they share a switch, not a network",
       !net_arp_resolve(s.net, s.dev[a].node, net_ip(10, 0, 20, 10), mac));
    int rtt = 0;
    ck("but the route the player built does carry it",
       net_ping(s.net, s.dev[a].node, net_ip(10, 0, 20, 10), &rtt) == PING_OK);
    uint32_t hops[8];
    int nh = net_traceroute(s.net, s.dev[a].node, net_ip(10, 0, 20, 10), hops, 8);
    ck("and it goes through the router, which is the only way across",
       nh >= 2 && hops[0] == net_ip(10, 0, 10, 1));

    /* Take the router's leg out of one vlan and the neighbour disappears --
     * because the only path was the one that was built. */
    site_subif(&s, rt, 0, 20, 0, 0);
    ck("take the router out of one vlan and the way across is gone",
       net_ping(s.net, s.dev[a].node, net_ip(10, 0, 20, 10), &rtt) != PING_OK);
    site_free(&s);
}

/* =============================================== the money the world takes
 *
 * A blind playtester played forty-two days -- a month and a half -- and no
 * circuit charge was ever taken: `isp` said "500 Mb, 1540 a month" and
 * `spent` only ever equalled hardware plus copper. A bill that never arrives
 * is not a price, and the biggest recurring decision in the game (how much
 * circuit to buy, against how much traffic you keep off it by putting a
 * server on the floor) cost nothing either way. Their words on the whole
 * build were that the decisions "felt like mine but did not feel like
 * decisions that would come back for me."
 *
 * So: the circuit is billed, on the day of the month it is billed on, and
 * the copper is not refundable, because a route you can un-choose for free
 * is not a route you chose. */
/* A DAY WITH NOBODY SERVED, WHICH IS WHAT THIS SCENARIO IS ABOUT.
 *
 * This tower has a circuit and no network in it at all: the question is
 * whether the standing charge lands on the thirtieth day, and nothing else.
 * Since D27 a tenancy that has moved in and has no port at all is struck
 * after its fit-out, so on a site with no copper anywhere the third
 * complaint arrives in the first fortnight and `site_day` stops advancing --
 * which is the game working and is not the bill. So the strikes of
 * tenancies nobody promised anything to are cleared as the days turn,
 * exactly as `keep_measuring` does in core/loadcheck.c. The money, the rent
 * and the bill are untouched. */
static bool unserved_day(Site *s, SiteDay *r)
{
    bool v = site_day(s, r);
    s->over = 0;
    s->complaints = 0;
    for (int i = 0; i < s->ntenant; i++) {
        s->tenant[i].strikes = 0;
        s->tenant[i].complained = 0;
    }
    return v;
}

static void check_bills(const Building *b)
{
    printf("\nthe bills, which are what make a decision come back for you\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    long month = site_isp_price(s.isp_mb);
    ck("the tower starts with a circuit that has a price on it",
       s.isp_mb > 0 && month > 0);
    ck("and `isp` says when the next month is billed, not just what it costs",
       site_isp_days_to_bill(&s) == 30);

    long had = s.money, spent = s.spent;
    for (int i = 0; i < 29; i++) unserved_day(&s, NULL);
    ck("twenty-nine days pass and no standing charge has landed yet",
       s.spent == spent && site_isp_days_to_bill(&s) == 1);

    long before = s.money;
    SiteDay r;
    unserved_day(&s, &r);
    ck("and on the thirtieth the ISP bills the month, to the penny",
       r.bill == month && s.money == before + r.rent - month &&
       s.spent == spent + month);
    printf("    day %d: the %d Mb circuit billed %ld, and %ld had been taken "
           "in rent\n", s.day, s.isp_mb, r.bill, s.rent_taken);

    /* And it keeps coming. A month and a half is two of them. */
    long paid = month;
    for (int i = 0; i < 15; i++) { unserved_day(&s, &r); paid += r.bill; }
    ck("nothing about it was a one-off: it is a standing charge",
       s.spent == spent + paid && paid == month);
    for (int i = 0; i < 15; i++) { unserved_day(&s, &r); paid += r.bill; }
    ck("forty-five days is two months of circuit, not none",
       paid == month * 2 && s.spent == spent + paid);
    printf("    %d days played, %ld of circuit paid for, %ld spent in all\n",
           s.day, paid, s.spent);
    (void)had;

    /* THE COPPER IS NOT REFUNDABLE. Pulling a cable out gives the port back
     * and gives nothing else back, so the route somebody chose is a route
     * they paid for. */
    int mdf = bld_find(b, 0, RM_MDF);
    int sw = gate_box(&s, SDEV_SWITCH8, mdf, "sw");
    long m0 = s.money;
    int l = site_cable(&s, sw, 0, s.uplink, 0, CAB_CAT6);
    ck("a run costs money the moment it is laid",
       l >= 0 && s.money == m0 - s.link[l].cost && s.link[l].cost > 0);
    long m1 = s.money;
    site_uncable(&s, l);
    ck("and pulling it out gives the port back and not the money",
       s.money == m1 && site_link_state(&s, l) == PORT_NOCABLE);
    site_free(&s);
}

/* ======================================= two commands, one answer about desks
 *
 * Consecutive lines of a real playtest:
 *
 *     day 18: 1 in, 0 served, 0/20 desks up, ...
 *     service:  tenant 2  desks 20  up 20  addr 0
 *
 * Both were right and neither said so. The day line counts desks that did
 * work, which means a port with LINK on it AND an address on the card;
 * `service` prints link and address as separate columns. So the day line now
 * says "addressed", `service` explains its own columns, and this builds the
 * exact state that produced the contradiction -- twenty desks cabled, no
 * DHCP anywhere -- and then makes it agree by starting one. */
/* Where a floor's kit goes: the cupboard if there is one, else the riser,
 * else the room the tenant rents. Same rule core/loadcheck.c builds by. */
static int comms_on(const Building *b, int floor, int fallback)
{
    int r = bld_find(b, floor, RM_COMMS);
    if (r < 0) r = bld_find(b, floor, RM_RISER);
    if (r < 0) r = fallback;
    return r;
}

static void check_agreement(const Building *b)
{
    printf("\n`day` and `service` counting the same desks\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 200000);

    int mdf = bld_find(b, 0, RM_MDF);
    int rt = gate_box(&s, SDEV_ROUTER, mdf, "rt");
    site_cable(&s, rt, 0, s.uplink, 0, CAB_CAT6);
    site_addr(&s, rt, 0, s.wan_you, s.wan_mask);
    site_addr(&s, rt, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, rt, s.wan_isp);
    site_forwarding(&s, rt, true);

    /* Run days until somebody has moved in and brought their desks. */
    for (int i = 0; i < 400 && !s.tenant[0].moved; i++) site_day(&s, NULL);
    if (!s.tenant[0].moved) { ck("a tenancy moves in", false); site_free(&s); return; }

    int sw = gate_box(&s, SDEV_SWITCH24,
                          comms_on(b, s.tenant[0].floor, s.tenant[0].room), "sw");
    site_cable(&s, rt, 1, sw, 0, CAB_CAT6);
    int got = site_serve(&s, 0, sw, CAB_CAT5E);
    ck("twenty desks are cabled up and nothing is serving addresses",
       got > 1 && site_tenant_connected(&s, 0) == got &&
       site_tenant_addressed(&s, 0) == 0);

    Buf d = {0}, sv = {0};
    site_advance(&s, 1, &d);
    site_dump_service(&s, &sv);
    ck("`day` says which of the two numbers it means",
       d.p && strstr(d.p, "desks addressed") != NULL &&
       strstr(d.p, "desks up") == NULL);
    /* THE LEGEND MOVED, AND IT IS STILL REACHABLE. It used to be under
     * every reading of `service`; it is now `service ?`, and this check
     * asks the SHELL for it so that the path a player types is the path
     * the gate walks. */
    {
        Buf lg = {0};
        site_cmd(&s, "service ?", &lg);
        ck("and `service ?` says what its own two columns are",
           lg.p && strstr(lg.p, "up is desks whose port has LINK on it") &&
           strstr(lg.p, "only an addressed desk does any work"));
        buf_free(&lg);
    }
    ck("cabled and unaddressed, they disagree the way the words now promise",
       s.last.connected == 0 && site_tenant_connected(&s, 0) == got);

    /* Start a DHCP server and they agree, because the desks really ask. */
    site_dhcpd(&s, rt, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    site_day(&s, NULL);
    int addressed = site_tenant_addressed(&s, 0);
    ck("give them a dhcp server and the day line's count is the addr column",
       addressed > 1 && s.last.connected == addressed);
    printf("    %d desks with link, %d of them addressed, in both places\n",
           site_tenant_connected(&s, 0), addressed);

    /* WHOSE SERVER THEY ARE ACTUALLY ON. The fallback -- their own machine,
     * else one on their floor, else anything powered in the building -- is
     * correct and used to be silent, so a tenancy hairpinning six floors
     * down through a riser looked exactly like one that was not. */
    {
        int srv = gate_box(&s, SDEV_SERVER, mdf, "fs");
        site_cable(&s, rt, 2, srv, 0, CAB_CAT6);
        site_power(&s, srv, true);
        site_addr(&s, srv, 0, net_ip(10, 0, 1, 10), net_mask_bits(16));
        site_day(&s, NULL);
        sv.len = 0; if (sv.p) sv.p[0] = 0;
        site_dump_service(&s, &sv);
        /* POWERED AND ADDRESSED IS NOT SERVING, and this gate used to stop
         * one step short of finding that out. It installed a server, powered
         * it, addressed it, and asserted the files column named it -- which
         * it did, and the box was answering nothing, because nobody had run
         * `httpd` on it. Two blind playtesters in a row lost runs to exactly
         * this: every indicator green and 60 of 80 transfers quietly missing,
         * with `service` naming a box that `show` called "services: none" in
         * the same session. So the step is here now, in both states. */
        ck("a server with no httpd is named as the box that would do it, not "
           "as the one that did",
           s.tenant[0].files_dev < 0 && sv.p &&
           strstr(sv.p, "fs (no httpd)") != NULL);
        site_httpd(&s, srv, 80);
        site_day(&s, NULL);
        sv.len = 0; if (sv.p) sv.p[0] = 0;
        site_dump_service(&s, &sv);
        ck("`service` names the server a tenancy's people actually pulled off",
           s.tenant[0].files_dev == srv && sv.p && strstr(sv.p, "fs") != NULL);
        ck("and marks it when that server is not on their deck",
           s.dev[srv].floor != s.tenant[0].floor &&
           strstr(sv.p, "fs <-") != NULL &&
           strstr(sv.p, "served from another deck") != NULL);
        site_power(&s, srv, false);
        site_day(&s, NULL);
        sv.len = 0; if (sv.p) sv.p[0] = 0;
        site_dump_service(&s, &sv);
        ck("switch it off and it says nobody served them, rather than the last name",
           s.tenant[0].files_dev < 0 && strstr(sv.p, "fs") == NULL);
    }
    buf_free(&d);
    buf_free(&sv);
    site_free(&s);
}

/* ----------------------------- the floor server D27 recommends, on a vlan
 *
 * THE BUILD THE DOCUMENTATION RECOMMENDS, SILENTLY DISQUALIFIED. D27's
 * competent tower is "a vlan per deck on a subinterface of the router, a
 * switch per deck home-run to the core, a server in each deck's cupboard
 * doing that deck's DHCP and holding its files". A server doing several
 * vlans' DHCP MUST live on subinterfaces -- one address per segment -- and
 * file_server_for() asked for an address on interface 0 specifically, so
 * that server was skipped and its floor's tenancies pulled their files off a
 * server downstairs across the riser instead. A playtester watched it for
 * two days with the right box powered, addressed and serving ten metres
 * away, and could only find out by reading the C.
 *
 * The floor server here has NO address on eth0 at all, which is the point.
 */
static void check_floor_server(const Building *b)
{
    printf("\na deck's own server, addressed only on the deck's vlan\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 400000);

    int mdf = bld_find(b, 0, RM_MDF);
    int rt = gate_box(&s, SDEV_ROUTER, mdf, "rt");
    site_cable(&s, rt, 0, s.uplink, 0, CAB_CAT6);
    site_addr(&s, rt, 0, s.wan_you, s.wan_mask);
    site_gateway(&s, rt, s.wan_isp);
    site_forwarding(&s, rt, true);

    for (int i = 0; i < 400 && !s.tenant[0].moved; i++) site_day(&s, NULL);
    if (!s.tenant[0].moved) { ck("a tenancy moves in", false); site_free(&s); return; }
    int floor = s.tenant[0].floor;
    int comms = comms_on(b, floor, s.tenant[0].room);

    /* The basement server everybody's files were on before the floor got its
     * own: one card, one address, plain eth0. It is the WRONG answer for
     * this floor, and until this check it was the one the game picked. */
    int base = gate_box(&s, SDEV_SERVER, mdf, "basement");
    site_power(&s, base, true);
    site_addr(&s, base, 0, net_ip(10, 0, 0, 10), net_mask_bits(24));
    site_gateway(&s, base, net_ip(10, 0, 0, 1));
    site_httpd(&s, base, 80);
    int csw = gate_box(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, rt, 1, csw, 0, CAB_CAT6);
    site_cable(&s, base, 0, csw, 1, CAB_CAT6);
    site_addr(&s, rt, 1, net_ip(10, 0, 0, 1), net_mask_bits(24));

    /* The floor: its own switch, its own vlan, its own server in its own
     * cupboard, and the router's leg into that vlan for the way out. */
    const int V = 31;
    int fsw = gate_box(&s, SDEV_SWITCH24, comms, "fsw");
    site_cable(&s, csw, 2, fsw, 0, CAB_FIBRE);
    site_port_trunk(&s, csw, 2, V);
    site_port_trunk(&s, fsw, 0, V);
    site_subif(&s, rt, 1, V, net_ip(10, 0, 31, 1), net_mask_bits(24));
    site_port_trunk(&s, csw, 0, V);

    int fsrv = gate_box(&s, SDEV_SERVER, comms, "decksrv");
    site_power(&s, fsrv, true);
    site_cable(&s, fsrv, 0, fsw, 1, CAB_CAT6);
    site_port_trunk(&s, fsw, 1, V);
    /* ITS ONLY ADDRESS IS ON THE VLAN. No `site_addr` on eth0 anywhere. */
    site_subif(&s, fsrv, 0, V, net_ip(10, 0, 31, 10), net_mask_bits(24));
    site_httpd(&s, fsrv, 80);
    site_dhcpd(&s, fsrv, net_ip(10, 0, 31, 100), 40, net_mask_bits(24),
               net_ip(10, 0, 31, 1), net_ip(10, 0, 31, 1));

    ck("the deck server has no address on eth0 and one on its vlan",
       net_if_get_addr(s.net, s.dev[fsrv].node, 0) == 0 &&
       net_if_get_addr(s.net, s.dev[fsrv].node, 2) == net_ip(10, 0, 31, 10));

    int got = site_serve_vlan(&s, 0, fsw, CAB_CAT5E, V);
    ck("their desks are cabled into the deck's own vlan", got > 1);
    site_day(&s, NULL);
    ck("and the deck's own server is what gave them their addresses",
       site_tenant_addressed(&s, 0) == got);

    Buf sv = {0};
    site_dump_service(&s, &sv);
    ck("a server addressed only on a vlan subinterface is still a file server",
       s.tenant[0].files_dev == fsrv);
    ck("so `service` names it, and does not mark them as served off-deck",
       sv.p && strstr(sv.p, "decksrv") != NULL &&
       strstr(sv.p, "decksrv <-") == NULL);
    {
        Buf lg = {0};
        site_cmd(&s, "service ?", &lg);
        ck("and `service ?` says out loud that any address qualifies, not eth0",
           lg.p && strstr(lg.p, "ANY address it holds") != NULL);
        buf_free(&lg);
    }
    ck("their people really finished work over it",
       s.last.finished > 0 && s.tenant[0].finished > 0);
    /* AND IT IS THE FLOOR'S LEG THAT ANSWERED, not a hairpin through the
     * router to some other address of the same box. */
    ck("and the traffic never left the deck: the vlan's leg is what answered",
       net_port_busy_us(s.net, s.dev[fsrv].node, 0) > 0 &&
       net_port_busy_us(s.net, s.dev[base].node, 0) == 0);
    buf_free(&sv);
    site_free(&s);
}

/* ------------------------------------------------- flat versus segmented */
/* THE ONE THAT PROVES ARCHITECTURE MATTERS. The same twenty-one machines,
 * doing the same work, wired two ways. Nothing anywhere decides that a big
 * broadcast domain is bad: a switch floods what it does not know, every card
 * in the domain has to look at what was flooded, and this counts them. */
#define BIG 21

static void check_flat(const Building *b)
{
    printf("\nflat, or segmented -- the same %d machines doing the same work\n", BIG);
    int room[4];
    for (int f = 0; f < 4; f++) room[f] = a_room(b, f + 2);
    int comms = bld_find(b, 2, RM_COMMS);
    int pc[BIG];

    /* ONE flat network: a twenty-four port switch and everybody on it. */
    Site flat;
    site_new(&flat, b, GATE_SEED, 400000);
    int sw = gate_box(&flat, SDEV_SWITCH24, comms, "flat");
    for (int i = 0; i < BIG; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "pc%d", i);
        pc[i] = gate_box(&flat, SDEV_PC, room[i % 4], nm);
        site_power(&flat, pc[i], true);
        site_cable(&flat, pc[i], 0, sw, i, CAB_CAT6);
        site_addr(&flat, pc[i], 0, net_ip(10, 0, 0, 10 + i), net_mask_bits(24));
    }
    uint32_t addr[BIG];
    for (int i = 0; i < BIG; i++) addr[i] = net_ip(10, 0, 0, 10 + i);
    uint64_t frames_flat = 0;
    {
        for (int i = 0; i < flat.ndev; i++) {
            if (site_kind_is_switch(flat.dev[i].kind))
                net_fdb_flush(flat.net, flat.dev[i].node);
            else net_arp_flush(flat.net, flat.dev[i].node);
        }
        uint64_t before = site_host_frames(&flat);
        for (int i = 0; i < BIG; i++)
            for (int j = 0; j < BIG; j++) {
                if (i == j) continue;
                int rtt = 0;
                net_ping(flat.net, flat.dev[pc[i]].node, addr[j], &rtt);
            }
        frames_flat = site_host_frames(&flat) - before;
    }

    /* THE SAME WORK, segmented: three groups of seven, each on its own
     * switch and its own subnet, joined by a router. */
    Site seg;
    site_new(&seg, b, 400000 + GATE_SEED, 400000);
    int gsw[3], grt;
    grt = gate_box(&seg, SDEV_ROUTER, bld_find(b, 0, RM_MDF), "rt");
    int core = gate_box(&seg, SDEV_SWITCH8, bld_find(b, 0, RM_MDF), "core");
    site_cable(&seg, grt, 0, core, 0, CAB_CAT6);
    site_port_trunk(&seg, core, 0, 0);
    for (int g = 0; g < 3; g++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "sw%d", g);
        gsw[g] = gate_box(&seg, SDEV_SWITCH8, bld_find(b, g + 2, RM_COMMS), nm);
        site_cable(&seg, gsw[g], 0, core, g + 1, CAB_CAT6);
        site_port_trunk(&seg, core, g + 1, 10 + g);
        site_port_trunk(&seg, core, 0, 10 + g);
        site_port_trunk(&seg, gsw[g], 0, 10 + g);
        site_subif(&seg, grt, 0, 10 + g,
                   net_ip(10, 0, 10 + g, 1), net_mask_bits(24));
    }
    site_forwarding(&seg, grt, true);
    int spc[BIG];
    uint32_t saddr[BIG];
    for (int i = 0; i < BIG; i++) {
        int g = i / 7, k = i % 7;
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "pc%d", i);
        spc[i] = gate_box(&seg, SDEV_PC, room[g], nm);
        site_power(&seg, spc[i], true);
        site_cable(&seg, spc[i], 0, gsw[g], k + 1, CAB_CAT6);
        site_port_vlan(&seg, gsw[g], k + 1, 10 + g);
        saddr[i] = net_ip(10, 0, 10 + g, 10 + k);
        site_addr(&seg, spc[i], 0, saddr[i], net_mask_bits(24));
        site_gateway(&seg, spc[i], net_ip(10, 0, 10 + g, 1));
    }
    uint64_t frames_seg = 0;
    {
        for (int i = 0; i < seg.ndev; i++) {
            if (site_kind_is_switch(seg.dev[i].kind))
                net_fdb_flush(seg.net, seg.dev[i].node);
            else net_arp_flush(seg.net, seg.dev[i].node);
        }
        uint64_t before = site_host_frames(&seg);
        for (int i = 0; i < BIG; i++)
            for (int j = 0; j < BIG; j++) {
                if (i == j) continue;
                int rtt = 0;
                net_ping(seg.net, seg.dev[spc[i]].node, saddr[j], &rtt);
            }
        frames_seg = site_host_frames(&seg) - before;
    }

    /* Both networks have to WORK, or the comparison is meaningless. */
    int rtt = 0;
    ck("the flat one works", net_ping(flat.net, flat.dev[pc[0]].node,
                                      addr[BIG - 1], &rtt) == PING_OK);
    ck("the segmented one works", net_ping(seg.net, seg.dev[spc[0]].node,
                                           saddr[BIG - 1], &rtt) == PING_OK);
    printf("    flat:      %llu frames arrived at a network card\n",
           (unsigned long long)frames_flat);
    printf("    segmented: %llu\n", (unsigned long long)frames_seg);
    if (frames_seg)
        printf("    every machine on the flat network looked at %.1fx as much "
               "traffic that was none of its business\n",
               (double)frames_flat / (double)frames_seg);
    ck("the flat network makes every card look at measurably more",
       frames_flat > frames_seg + frames_seg / 4);
    printf("    and nothing in this program decided that. A switch floods "
           "what it has not learned.\n");
    site_free(&flat);
    site_free(&seg);
}

/* ---------------------------------------------------- demand, and a shell */
static void check_demand(const Building *b)
{
    printf("\nthe tenants who are coming\n");
    Site a, c;
    site_new(&a, b, GATE_SEED, 90);
    site_new(&c, b, GATE_SEED, 90);
    ck("the same seed brings the same tenants, in the same order",
       a.ntenant == c.ntenant &&
       memcmp(a.tenant, c.tenant, sizeof(SiteTenant) * (size_t)a.ntenant) == 0);
    bool sorted = true;
    int drops = 0, seg = 0;
    for (int i = 0; i < a.ntenant; i++) {
        if (i && a.tenant[i].day < a.tenant[i - 1].day) sorted = false;
        drops += a.tenant[i].drops;
        seg += a.tenant[i].own_segment;
    }
    ck("they arrive in order, over time", sorted && a.ntenant > 4);
    ck("and they want more ports than any one switch has",
       drops > site_kind_ports(SDEV_SWITCH24));
    ck("and more segments than one flat network can be",
       seg > 1);
    printf("    %d tenancies, %d drops, %d of them wanting their own segment:"
           " %d switches at least\n", a.ntenant, drops, seg, (drops + 22) / 23);

    /* MONEY IS A LIMIT TOO. A site with forty pounds in it cannot buy a
     * switch, and says so rather than quietly succeeding. */
    int sw = gate_box(&a, SDEV_SWITCH24, bld_find(b, 1, RM_COMMS), "sw");
    ck("and a site with ninety pounds in it cannot buy a switch",
       sw < 0 && a.err == SITE_EMONEY);
    site_free(&a);
    site_free(&c);
}

/* ---------------------------------------- a name server the player can use
 *
 * `dnsd <box>` used to start a server with an empty zone, no verb anywhere
 * in the tower to put a name in it and no forwarder, and answer the word
 * `serving`. Every query it ever got was NXDOMAIN, so the only working
 * resolver in the building was the ISP's -- out through the router, which is
 * the hairpin this game exists to teach people to avoid.
 */
static void check_dns_verbs(const Building *b)
{
    printf("\na name server of the player's own, in the words they type\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 200000);
    Buf o = {0};
    static const char *SCRIPT[] = {
        "order router edge",  "move edge d0.eng",
        "order server dns1",  "move dns1 d0.eng",
        "cable edge:1 dns1:0 cat6",
        "cable edge:0 uplink:0 cat6",
        "addr edge 198.51.100.2/30",
        "gw edge 198.51.100.1",
        "addr edge:1 10.0.0.1/24",
        "router edge on",
        "power dns1 on",
        "addr dns1 10.0.0.10/24",
        "gw dns1 10.0.0.1",
        NULL
    };
    /* FED AFTER EVERY LINE, not at the end: these scripts press the button
     * as they go -- `power dns1 on` -- and a box that is not fed yet refuses
     * it and is never asked again. */
    for (int i = 0; SCRIPT[i]; i++) { site_cmd(&s, SCRIPT[i], &o); gate_feed_all(&s); }

    /* THE WORD `serving` TOLD A PLAYER NOTHING. What matters about a name
     * server is how many names it holds and where it sends the rest, and
     * both of those are zero and nowhere on the day it starts. */
    buf_clear(&o);
    site_cmd(&s, "dnsd dns1", &o);
    ck("`dnsd` says what it will serve, not the bare word `serving`",
       has(o.p, "serves 0 names") && has(o.p, "forwards the rest nowhere") &&
       has(o.p, "NOWHERE TO ASK"));

    /* THE VERB THAT DID NOT EXIST. */
    buf_clear(&o);
    site_cmd(&s, "dns dns1 files.deck3 10.0.0.50", &o);
    ck("`dns <box> <name> <ip>` puts a name in the zone",
       has(o.p, "files.deck3 -> 10.0.0.50") && has(o.p, "serves 1 name"));

    buf_clear(&o);
    site_cmd(&s, "resolver edge 10.0.0.10", &o);
    buf_clear(&o);
    site_cmd(&s, "resolve edge files.deck3", &o);
    ck("and a box pointed at it resolves that name over the wire",
       has(o.p, "10.0.0.50"));

    /* NXDOMAIN IS AN ANSWER. It used to print `no answer`, which is what a
     * server that is not there prints, and the two repairs have nothing in
     * common. */
    buf_clear(&o);
    site_cmd(&s, "resolve edge nowhere.example", &o);
    ck("a name that does not exist says so, and says the server answered",
       has(o.p, "no such name") && has(o.p, "not a network fault") &&
       !has(o.p, "no answer"));

    /* THE HALF THAT STOPS THE HAIRPIN: give the floor's server somewhere to
     * ask, and it resolves the internet for the floor. */
    buf_clear(&o);
    site_cmd(&s, "resolver dns1 198.51.100.1", &o);
    buf_clear(&o);
    site_cmd(&s, "dnsd dns1", &o);
    ck("`dnsd` names the resolver it forwards to",
       has(o.p, "forwards the rest to 198.51.100.1"));
    buf_clear(&o);
    site_cmd(&s, "resolve edge wiki.nomnix.org", &o);
    ck("and a name it has never held comes back, forwarded and relayed",
       has(o.p, "10.0.2.20"));

    /* A resolver that is not there is silence, and silence says so. */
    buf_clear(&o);
    site_cmd(&s, "resolver edge 10.0.0.77", &o);
    buf_clear(&o);
    site_cmd(&s, "resolve edge wiki.nomnix.org", &o);
    ck("a resolver that is not there times out, and is not called NXDOMAIN",
       has(o.p, "timed out") && !has(o.p, "no such name"));

    buf_clear(&o);
    site_cmd(&s, "resolver edge 0.0.0.0", &o);
    buf_clear(&o);
    site_cmd(&s, "resolve edge wiki.nomnix.org", &o);
    ck("and a box with no resolver at all says that instead of timing out",
       has(o.p, "no resolver on edge"));

    buf_free(&o);
    site_free(&s);
}

/* ------------------------------------------ a drop counter, pointed at
 *
 * `ping edge 10.0.0.10` printing `no answer` when the far box's filter ate
 * the echo cost two playtesters ten minutes each, and one of them re-cut a
 * trunk to repair a routing fault that did not exist. The drop is correct
 * and documented; the silence about it was not.
 */
static void check_ping_blames_the_filter(const Building *b)
{
    printf("\nwhat a ping says when the far end refused it\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 200000);
    Buf o = {0};
    static const char *SCRIPT[] = {
        "order router edge",  "move edge d0.eng",
        "order server files", "move files d0.eng",
        "cable edge:1 files:0 cat6",
        "addr edge:1 10.0.0.1/24",
        "router edge on",
        "power files on",
        "addr files 10.0.0.10/24",
        "gw files 10.0.0.1",
        NULL
    };
    /* FED AFTER EVERY LINE, not at the end: these scripts press the button
     * as they go -- `power dns1 on` -- and a box that is not fed yet refuses
     * it and is never asked again. */
    for (int i = 0; SCRIPT[i]; i++) { site_cmd(&s, SCRIPT[i], &o); gate_feed_all(&s); }

    /* The shipped ruleset on a booted box: policy drop, plus 22 and 80. */
    int node = s.dev[site_dev_by_name(&s, "files")].node;
    net_fw_add(s.net, node, FW_IN, IP_PROTO_TCP, 22, 0, 0, FW_ACCEPT);
    net_fw_add(s.net, node, FW_IN, FW_ANY_PROTO, FW_ANY_PORT, 0, 0, FW_DROP);

    buf_clear(&o);
    site_cmd(&s, "ping files 10.0.0.1", &o);
    ck("the box with the filter can still ping out, which is what confused "
       "everybody",
       has(o.p, "reply in"));

    buf_clear(&o);
    site_cmd(&s, "ping edge 10.0.0.10", &o);
    ck("a ping the far end dropped names the box and its counter",
       has(o.p, "no answer") && has(o.p, "files") &&
       has(o.p, "packet filter counted") && has(o.p, "netstat -F"));

    /* AND IT SAYS NOTHING WHEN IT HAS NOTHING TO SAY. An address nobody has
     * is not a filter, and a diagnostic that blamed one would be worse than
     * the silence it replaced. */
    buf_clear(&o);
    site_cmd(&s, "ping edge 10.0.0.66", &o);
    ck("a ping to an address nobody holds blames no filter",
       !has(o.p, "packet filter counted"));

    net_fw_clear(s.net, node);
    buf_clear(&o);
    site_cmd(&s, "ping edge 10.0.0.10", &o);
    ck("and with the filter gone the same ping is answered",
       has(o.p, "reply in") && !has(o.p, "packet filter counted"));

    buf_free(&o);
    site_free(&s);
}

/* ------------------------------------------------- a pool, and a way out of it
 *
 * The same fault as the netcheck above, in the words a player types, plus
 * the two lines that did not exist: `dhcpd <box> off`, and `dhcpd <box>` to
 * find out what a box is serving before deciding.
 */
static void check_dhcp_scope(const Building *b)
{
    printf("\na dhcp pool, the segment it serves, and the way out of it\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 200000);
    Buf o = {0};
    static const char *SCRIPT[] = {
        "order router edge",   "move edge d0.eng",
        "order switch24 core", "move core d0.eng",
        "cable edge:1 core:0 cat6",
        "vlan core 1 11",
        "vlan core 2 13",
        "trunk core 0 11 13",
        "subif edge 1 11 10.11.0.1/24",
        "subif edge 1 13 10.13.0.1/24",
        "router edge on",
        NULL
    };
    /* FED AFTER EVERY LINE, not at the end: these scripts press the button
     * as they go -- `power dns1 on` -- and a box that is not fed yet refuses
     * it and is never asked again. */
    for (int i = 0; SCRIPT[i]; i++) { site_cmd(&s, SCRIPT[i], &o); gate_feed_all(&s); }

    /* THE LINE THAT USED TO POISON A TENANCY NOBODY HAD TOUCHED. */
    buf_clear(&o);
    site_cmd(&s, "dhcpd edge 10.11.0.100 20 24 10.11.0.1 10.11.0.1", &o);
    ck("`dhcpd` answers with the segment the pool landed on, not just `serving`",
       has(o.p, "10.11.0.100-10.11.0.119") && has(o.p, "eth1.11") &&
       has(o.p, "vlan 11"));

    buf_clear(&o);
    site_cmd(&s, "dhcpd edge 10.99.0.100 20 24 10.99.0.1 10.99.0.1", &o);
    ck("a pool for a subnet the box is not on is refused, and says what it "
       "does have",
       has(o.p, "serves the segment") && has(o.p, "eth1.11"));

    /* A desk on each vlan. The one on thirteen must get nothing from a
     * router that serves eleven, and the router's own answer says so. */
    int room = a_room(b, 2);
    int d11 = gate_box(&s, SDEV_PC, room, "d11");
    int d13 = gate_box(&s, SDEV_PC, room, "d13");
    site_power(&s, d11, true);
    site_power(&s, d13, true);
    site_cable(&s, d11, 0, site_dev_by_name(&s, "core"), 1, CAB_CAT5E);
    site_cable(&s, d13, 0, site_dev_by_name(&s, "core"), 2, CAB_CAT5E);
    buf_clear(&o);
    site_cmd(&s, "dhcp d11", &o);
    ck("the desk on the vlan it serves gets an address", has(o.p, "10.11.0.100"));
    buf_clear(&o);
    site_cmd(&s, "dhcp d13", &o);
    ck("the desk on the vlan it does not serve gets nothing, and is not "
       "given somebody else's subnet",
       has(o.p, "no lease") &&
       net_if_get_addr(s.net, s.dev[d13].node, 0) == 0);

    /* AND IT CAN BE STOPPED. There was no `dhcpd off`, a router has no power
     * button, and `count 0` did not stop it either. */
    buf_clear(&o);
    site_cmd(&s, "dhcpd edge 10.11.0.100 0 24 10.11.0.1 10.11.0.1", &o);
    ck("a pool of no addresses is refused rather than taken as a way to stop "
       "one", has(o.p, "serves nobody"));
    buf_clear(&o);
    site_cmd(&s, "dhcpd edge off", &o);
    ck("`dhcpd <box> off` stops it and says how much it stopped",
       has(o.p, "stops serving addresses") && has(o.p, "1 pool"));
    buf_clear(&o);
    site_cmd(&s, "dhcpd edge", &o);
    ck("and afterwards the box says it serves nothing",
       has(o.p, "serves no addresses"));
    int d11b = gate_box(&s, SDEV_PC, room, "d11b");
    site_power(&s, d11b, true);
    site_cable(&s, d11b, 0, site_dev_by_name(&s, "core"), 3, CAB_CAT5E);
    site_cmd(&s, "vlan core 3 11", &o);
    buf_clear(&o);
    site_cmd(&s, "dhcp d11b", &o);
    ck("a stopped server answers a discover with nothing at all",
       has(o.p, "no lease") &&
       net_if_get_addr(s.net, s.dev[d11b].node, 0) == 0);

    /* TWO LEGS ON ONE SEGMENT IS A CHOICE THE BOX MAKES SILENTLY.
     *
     * The pool lands on the interface whose address is inside it, and when
     * two of them are, the first wins. It has always printed WHICH one, and
     * that is what saved a playtester -- but a player with no reason to
     * suspect a choice was made has no reason to read that line. */
    site_cmd(&s, "subif edge 1 14 10.11.0.2/24", &o);
    buf_clear(&o);
    site_cmd(&s, "dhcpd edge 10.11.0.100 20 24 10.11.0.1 10.11.0.1", &o);
    ck("a pool that could have landed on two interfaces says the choice was "
       "ambiguous",
       has(o.p, "AMBIGUOUS") && has(o.p, "eth1.11") && has(o.p, "eth1.14"));
    ck("and names the line that takes one of them away again",
       has(o.p, "subif edge <nic> <vlan> off"));
    buf_clear(&o);
    site_cmd(&s, "subif edge 1 14 off", &o);
    ck("`subif <box> <nic> <vlan> off` is that line, and it works",
       has(o.p, "eth1.14 is gone"));
    buf_clear(&o);
    site_cmd(&s, "dhcpd edge 10.11.0.100 20 24 10.11.0.1 10.11.0.1", &o);
    ck("and with one leg on the segment the choice is no longer ambiguous",
       !has(o.p, "AMBIGUOUS") && has(o.p, "eth1.11"));
    site_cmd(&s, "dhcpd edge off", &o);

    /* And `show` names what a box serves, which it never did. */
    site_cmd(&s, "dhcpd edge 10.13.0.100 20 24 10.13.0.1 10.13.0.1", &o);
    buf_clear(&o);
    site_cmd(&s, "show edge", &o);
    ck("`show <box>` lists the services it is running and on which interface",
       has(o.p, "services:") && has(o.p, "dhcpd  10.13.0.100") &&
       has(o.p, "eth1.13"));
    buf_free(&o);
    site_free(&s);
}

/* --------------------------------------------- a verb that is short of words
 *
 * `dhcpd edge` answered "no such command: dhcpd (try help)". The verb is in
 * the help and is a verb; what was wrong was the number of arguments. That
 * is the same failure as a help text naming a command the machine has not
 * got, and the last round was spent eliminating that one -- so every verb is
 * swept here rather than the one that was reported.
 */
/* ------------------------------------------- the trunk line, word for word
 *
 * A PLAYTESTER ON DAY 62 TYPED THIS:
 *
 *     trunk core 22 11 12 13 14 15 16 17 18 19 20 21 22 23
 *     set
 *
 * Sixteen words into a parser that held twelve. Vlans 20 to 23 were dropped
 * on the floor, the verb answered "set", and nothing anywhere printed a
 * trunk's allowed list -- so the only symptom was two tenancies on floor 5
 * whose transfers stopped finishing, eight in-game days and two complaints
 * later.
 *
 * These checks are DATA PLANE wherever they can be: they do not count
 * tokens, they put a machine in the last vlan of a long line and ping across
 * the trunk. Counting tokens would pass on a parser that read all fourteen
 * numbers and then dropped them somewhere else -- which is exactly what the
 * 32-bit allowed mask did to every vlan above 32.
 */
static void check_trunk_line(const Building *b)
{
    printf("\nthe trunk line: every word of it, or none of it\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 400000);

    int mdf = bld_find(b, 0, RM_MDF);
    int comms = bld_find(b, 2, RM_COMMS);
    int room = a_room(b, 2);
    int csw = gate_box(&s, SDEV_SWITCH24, mdf, "core");
    int fsw = gate_box(&s, SDEV_SWITCH24, comms, "fsw");
    site_cable(&s, csw, 0, fsw, 0, CAB_FIBRE);

    /* Two machines in the FOURTEENTH vlan of the line below, one each side
     * of the trunk. Nothing else joins them. */
    int a = gate_box(&s, SDEV_PC, mdf, "a24");
    int c = gate_box(&s, SDEV_PC, room, "c24");
    site_power(&s, a, true);
    site_power(&s, c, true);
    site_cable(&s, a, 0, csw, 1, CAB_CAT6);
    site_cable(&s, c, 0, fsw, 1, CAB_CAT6);
    site_port_vlan(&s, csw, 1, 24);
    site_port_vlan(&s, fsw, 1, 24);
    site_addr(&s, a, 0, net_ip(10, 0, 24, 10), net_mask_bits(24));
    site_addr(&s, c, 0, net_ip(10, 0, 24, 11), net_mask_bits(24));

    Buf o = {0};
    const char *LINE = "trunk %s 0 11 12 13 14 15 16 17 18 19 20 21 22 23 24";
    char line[160];
    snprintf(line, sizeof line, LINE, "core");
    site_cmd(&s, line, &o);
    buf_clear(&o);
    snprintf(line, sizeof line, LINE, "fsw");
    site_cmd(&s, line, &o);

    int rtt = 0;
    ck("a fourteen-vlan trunk line really carries the fourteenth",
       net_ping(s.net, s.dev[a].node, net_ip(10, 0, 24, 11), &rtt) == PING_OK);
    ck("and the line answers with the list, not the word `set`",
       has(o.p, "allows 11-24") && !has(o.p, "set"));

    /* READ IT BACK OFF THE BOX. The setting the player could not see is the
     * reason eight days passed: `show core` said `trunk native 1` and
     * stopped, on a port whose allowed list was the whole fault. */
    buf_clear(&o);
    site_cmd(&s, "show core", &o);
    ck("`show <box>` prints what the trunk carries, on a port with no cable",
       has(o.p, "allows 11-24 (14 vlans)"));

    /* AND THE WAY BACK OFF. site_port_trunk only ever ORed, so a vlan put on
     * the wrong uplink could not be removed, only added to. */
    buf_clear(&o);
    site_cmd(&s, "trunk core 0 -24", &o);
    ck("`-<vlan>` takes one back off, and says what is left",
       has(o.p, "allows 11-23") && !has(o.p, "24"));
    ck("and the frames really stop crossing",
       net_ping(s.net, s.dev[a].node, net_ip(10, 0, 24, 11), &rtt) != PING_OK);
    buf_clear(&o);
    site_cmd(&s, "trunk core 0 24", &o);
    ck("put it back and they cross again",
       net_ping(s.net, s.dev[a].node, net_ip(10, 0, 24, 11), &rtt) == PING_OK);
    buf_clear(&o);
    site_cmd(&s, "trunk core 0 none", &o);
    ck("`none` empties the whole set",
       has(o.p, "carries nothing but the native vlan") &&
       net_ping(s.net, s.dev[a].node, net_ip(10, 0, 24, 11), &rtt) != PING_OK);
    buf_clear(&o);
    snprintf(line, sizeof line, LINE, "core");
    site_cmd(&s, line, &o);

    /* A VLAN ABOVE 32. `subif` has always taken 1..4094 and an access port
     * takes any number, but the trunk's allowed set was one uint32_t -- so
     * `trunk core 0 100` answered "set" about a trunk that could not carry
     * vlan 100 and never would. Same lie, one layer down. */
    {
        int a2 = gate_box(&s, SDEV_PC, mdf, "a100");
        int c2 = gate_box(&s, SDEV_PC, room, "c100");
        site_power(&s, a2, true);
        site_power(&s, c2, true);
        site_cable(&s, a2, 0, csw, 2, CAB_CAT6);
        site_cable(&s, c2, 0, fsw, 2, CAB_CAT6);
        site_port_vlan(&s, csw, 2, 100);
        site_port_vlan(&s, fsw, 2, 100);
        site_addr(&s, a2, 0, net_ip(10, 0, 100, 10), net_mask_bits(24));
        site_addr(&s, c2, 0, net_ip(10, 0, 100, 11), net_mask_bits(24));
        buf_clear(&o);
        site_cmd(&s, "trunk core 0 100", &o);
        buf_clear(&o);
        site_cmd(&s, "trunk fsw 0 100", &o);
        ck("a vlan above 32 crosses a trunk, the way `subif` always allowed",
           net_ping(s.net, s.dev[a2].node, net_ip(10, 0, 100, 11), &rtt) == PING_OK);
    }

    /* A LINE THAT DOES NOT FIT IS REFUSED WHOLE. Not obeyed as far as it
     * goes, and never answered "set". */
    {
        char big[1024];
        int l = snprintf(big, sizeof big, "trunk core 3");
        for (int v = 200; v < 280; v++)
            l += snprintf(big + l, sizeof big - (size_t)l, " %d", v);
        buf_clear(&o);
        site_cmd(&s, big, &o);
        ck("a line with more words than the parser holds says so",
           has(o.p, "more than") && has(o.p, "Nothing was done") &&
           !has(o.p, "allows") && !has(o.p, "set"));
        buf_clear(&o);
        site_cmd(&s, "show core", &o);
        ck("and no part of it was run: port 3 is not a trunk at all",
           !has(o.p, "port 3 "));
    }

    /* AND A TYPO IN THE MIDDLE TAKES THE WHOLE LINE WITH IT, rather than
     * setting the words before it and refusing at the word after. */
    buf_clear(&o);
    site_cmd(&s, "trunk core 0 30 wombat 31", &o);
    ck("a word that is not a vlan refuses the line and changes nothing",
       has(o.p, "wombat") && has(o.p, "Nothing was done"));
    buf_clear(&o);
    site_cmd(&s, "show core", &o);
    ck("neither the vlan before the typo nor the one after it was applied",
       !has(o.p, ",30") && !has(o.p, ",31") && has(o.p, "allows 11-24"));
    buf_clear(&o);
    site_cmd(&s, "trunk core 0 4095", &o);
    ck("and 4095 is refused, because 1..4094 is what a vlan id is",
       has(o.p, "1 to 4094") && has(o.p, "Nothing was done"));

    buf_free(&o);
    site_free(&s);
}

static void check_arity(const Building *b)
{
    printf("\nevery verb, handed too few words\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 400000);
    gate_box(&s, SDEV_SWITCH24, bld_find(b, 0, RM_MDF), "core");
    Buf o = {0};
    bool denied = false, silent = false;
    for (int i = 0; i < site_verb_count(); i++) {
        const char *v = site_verb_name(i);
        for (int k = 1; k <= site_verb_arity(i); k++) {
            char line[128];
            int l = snprintf(line, sizeof line, "%s", v);
            for (int j = 1; j < k; j++)
                l += snprintf(line + l, sizeof line - (size_t)l, " core");
            buf_clear(&o);
            site_cmd(&s, line, &o);
            if (has(o.p, "no such command")) {
                printf("    `%s` -> %s", line, o.p);
                denied = true;
            }
            if (!o.len) {
                printf("    `%s` answered nothing at all\n", line);
                silent = true;
            }
        }
    }
    ck("no verb answers a short line by denying it is a verb", !denied);
    ck("and none of them answers a short line with silence", !silent);

    /* The reported one, in the exact spelling that produced the denial. */
    buf_clear(&o);
    site_cmd(&s, "dhcpd", &o);
    ck("`dhcpd` on its own says what dhcpd wants",
       has(o.p, "dhcpd <box> <first> <count> <bits> <gw> <dns>") &&
       has(o.p, "dhcpd <box> off"));
    buf_clear(&o);
    site_cmd(&s, "frobnicate core", &o);
    ck("and a word that really is not a verb still says so",
       has(o.p, "no such command"));
    buf_free(&o);
    site_free(&s);
}

/* ------------------------------------------- the game contradicting itself
 *
 * A playtester drove the running game over a socket and caught `show` saying
 * both things about one box in one screen: the header said SWITCHED OFF and
 * nothing of it was on the network, and four lines later the trailer said it
 * was on the network. The reassuring half was the false one, which is the
 * worst direction for a report to be wrong in -- they would have walked away
 * believing the box was up.
 *
 * `links` was wrong about money in the same direction: it totalled the runs
 * still in the wall, called that the spend, and copper is deliberately not
 * refunded when it comes out. And it kept the pulled runs in the table, so
 * the list you scan for a cable to pull was padded with rows that cannot be
 * pulled.
 *
 * These are two-line checks. They would have caught all of it years of
 * playtests ago, which is the reason they are here now.
 */
/* -------------------------------- how much unhappiness ends a run */
/* It was three, whatever the size of the building. That was right when a
 * tower held three tenancies; D27's letting queue signs thirteen leases by
 * day sixty, and three of thirteen is seventy-seven per cent of your tenants
 * perfectly happy. It mattered more than it looks because tenancies fail
 * TOGETHER -- one overheated server takes a floor out on the same morning --
 * so an agent's competent build on seed 42 died on day 20 from a single heat
 * trip on day 17. */
static void check_tolerance(const Building *b)
{
    printf("\nhow many complaints the landlord will wear\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);

    /* Nobody in yet: the floor holds, because a building with three tenants
     * in it that all hate you is over however you count it. */
    ck("an empty building still ends at three", site_complaints_allowed(&s) == 3);

    /* Move them in by hand, so this measures the arithmetic and not the
     * letting schedule -- which is a different gate's business. */
    struct { int in, want; } CASE[] = {
        { 1, 3 }, { 3, 3 }, { 6, 3 }, { 9, 3 },
        { 12, 4 }, { 15, 5 }, { 21, 7 }
    };
    bool all = true;
    int most = 0;
    for (size_t c = 0; c < sizeof CASE / sizeof CASE[0]; c++) {
        if (CASE[c].in > s.ntenant) continue;
        for (int i = 0; i < s.ntenant; i++) s.tenant[i].moved = i < CASE[c].in;
        int got = site_complaints_allowed(&s);
        if (got != CASE[c].want) {
            printf("    %d tenancies in -> %d, wanted %d\n",
                   CASE[c].in, got, CASE[c].want);
            all = false;
        }
        most = CASE[c].in;
    }
    ck("a third of the building, rounded up, never fewer than three", all);
    ck("and the gate had enough tenancies on this seed to mean it", most >= 12);

    /* NOTHING BEFORE NINE MOVES, which is what keeps --loadcheck's curve --
     * the owner's "slow at three decks, breaking at five" -- untouched by
     * this change. */
    bool early_same = true;
    for (int n = 0; n <= 9 && n <= s.ntenant; n++) {
        for (int i = 0; i < s.ntenant; i++) s.tenant[i].moved = i < n;
        if (site_complaints_allowed(&s) != 3) early_same = false;
    }
    ck("nothing at nine tenancies or fewer moved, so the curve is untouched",
       early_same);

    /* AND THE PLAYER CAN READ IT. A hidden threshold is worse than a wrong
     * one: the number that ends your run has to be countable against the
     * stars in the column above it. */
    for (int i = 0; i < s.ntenant; i++) s.tenant[i].moved = i < 12;
    {
        Buf sv = {0};
        site_dump_service(&s, &sv);
        ck("`service` prints the number, and it is the one that ends the run",
           sv.p && strstr(sv.p, "4 filed complaints ends the run") != NULL &&
           site_complaints_allowed(&s) == 4);
        buf_free(&sv);
    }
    site_free(&s);
}

static void check_reports(const Building *b)
{
    printf("\nwhat the game says about itself\n");
    Site s; Buf o; buf_init(&o);
    site_new(&s, b, GATE_SEED, 100000);
    int mdf = bld_find(b, 0, RM_MDF);
    int srv = gate_box(&s, SDEV_SERVER, mdf, "files");
    int sw  = gate_box(&s, SDEV_SWITCH8, mdf, "core");
    site_power(&s, srv, true);
    int lu = site_cable(&s, sw, 0, s.uplink, 0, CAB_CAT6);
    int lf = site_cable(&s, sw, 1, srv, 0, CAB_CAT6);
    site_addr(&s, srv, 0, net_ip(10, 0, 1, 10), net_mask_bits(24));

    /* ONE SCREEN, ONE ANSWER. Power it down -- which is what the building's
     * own mains event does to it -- and the header and the trailer must not
     * disagree about whether any of it is on the network. */
    site_power(&s, srv, false);
    buf_clear(&o);
    site_cmd(&s, "show files", &o);
    ck("switched off, `show` says so in the header",
       has(o.p, "SWITCHED OFF") && has(o.p, "nothing of it is on the network"));
    ck("and its own trailer does not then say it is on the network",
       !has(o.p, "It is on the network"));

    /* Switched on again with the lead still in it, and it is on the network,
     * so the trailer may say so. */
    site_power(&s, srv, true);
    site_addr(&s, srv, 0, net_ip(10, 0, 1, 10), net_mask_bits(24));
    buf_clear(&o);
    site_cmd(&s, "show files", &o);
    ck("switched on with a lead in it and an address, it says it is on the "
       "network", has(o.p, "It is on the network") && !has(o.p, "SWITCHED OFF"));

    /* And a running box with nothing plugged into it is not on the network
     * either, whatever the light on the front says. */
    int lonely = gate_box(&s, SDEV_PC, a_room(b, 3), "lonely");
    site_power(&s, lonely, true);
    buf_clear(&o);
    site_cmd(&s, "show lonely", &o);
    ck("a running box with no lead in it does not claim to be on the network",
       !has(o.p, "It is on the network") && has(o.p, "services: none"));

    /* WHAT THE COPPER COST, against what the account says it cost. Pull one
     * of the two runs: the money does not come back, so the total `links`
     * prints must not come back either. */
    long before = s.spent;
    site_uncable(&s, lu);
    ck("pulling a cable refunds nothing", s.spent == before);

    int spent_on_cable = s.link[lu].cost + s.link[lf].cost;
    char want[64];
    snprintf(want, sizeof want, "%d spent on cable in all", spent_on_cable);
    buf_clear(&o);
    site_cmd(&s, "links", &o);
    ck("`links` totals what copper actually cost, not what is still live",
       has(o.p, want));
    ck("and it still says what is live, labelled as live",
       has(o.p, "m of cable in the building"));

    /* THE PULLED RUN IS OFF THE TABLE, AND THE INDICES DID NOT MOVE. The
     * surviving run is still link 1, and `uncable 1` still means that run. */
    char row[64];
    snprintf(row, sizeof row, "%2d  core:1", lf);
    ck("a pulled run is no longer a row you can try to pull",
       !has(o.p, "core:0") && has(o.p, row));
    /* Link 0 is the lead the building came with -- the workstation's, which
     * came out of uplink:0 the moment `core` was cabled to it -- so the two
     * runs this check laid are 1 and 2, and they are still 1 and 2. */
    ck("and the survivor kept its index, so `uncable <n>` still means what "
       "it meant", lu == 1 && lf == 2 && site_link_state(&s, lf) == PORT_UP);
    buf_clear(&o);
    char pull[32];
    snprintf(pull, sizeof pull, "uncable %d", lf);
    site_cmd(&s, pull, &o);
    ck("`uncable 2` pulls the run the table numbered 2",
       site_link_state(&s, lf) == PORT_NOCABLE);
    buf_clear(&o);
    site_cmd(&s, "links", &o);
    ck("with everything pulled, the table says so and still counts the money",
       has(o.p, "0 m of cable in the building") &&
       has(o.p, "3 pulled runs") && has(o.p, want));
    buf_free(&o);
    site_free(&s);
}

/* =============================================== the other way to buy metres
 *
 * D23 sold a permanent jack as the counterpart to the spool and the tower
 * never had one, which the first blind playtester of it caught: *"It is not
 * in `help` and no verb I tried creates one."* This is the gate on the thing
 * that was built instead of the promise.
 *
 * What it has to be, or it is only a dearer cable and the day-34 playtest's
 * verdict stands ("a bill I paid with a rule, not a bill I sweated"):
 *   - the same metres. bld_cable_all() through site_metres(), so the two
 *     prices on the screen are prices for one piece of copper;
 *   - dearer than the spool run, always, so jacking a room that only ever
 *     holds one box is money burnt and the player can find that out;
 *   - a fixed point in the ROOM. A lead is what a box costs after that,
 *     the lead comes out and the jack stays, and the panel port at the far
 *     end is gone for good -- including from `serve`;
 *   - and NOT THERE TODAY. Days, on the same clock the strikes are on. */
static void check_jack(const Building *b)
{
    printf("\nthe permanent jack, against the spool it is the counterpart to\n");
    Site s; Buf o; buf_init(&o);
    site_new(&s, b, GATE_SEED, 100000);
    int mdf = bld_find(b, 0, RM_MDF);
    int up  = a_room(b, 2);
    int core = gate_box(&s, SDEV_SWITCH24, mdf, "core");

    int m = site_metres(&s, up, mdf);
    ck("a jack is measured on the same tray metres the spool is",
       m > 0 && site_metres(&s, up, mdf) == m);
    ck("and priced from them, dearer than running the same metres once",
       site_jack_price(CAB_CAT5E, m) > site_cable_price(CAB_CAT5E, m));
    ck("and a lead into it afterwards is cheaper than either",
       site_jack_lead_price() < site_cable_price(CAB_CAT5E, m));
    /* THE BREAK-EVEN IS A REAL ONE. One box in that room and the spool won;
     * three boxes over the life of the run and the jack won. A mechanic
     * whose answer is the same every time is the one this feature exists to
     * stop being. */
    int spool1 = site_cable_price(CAB_CAT5E, m);
    int jack1  = site_jack_price(CAB_CAT5E, m) + site_jack_lead_price();
    int spool3 = spool1 * 3;
    int jack3  = site_jack_price(CAB_CAT5E, m) + site_jack_lead_price() * 3;
    ck("one box in the room and the spool is the cheaper answer", jack1 > spool1);
    ck("three boxes over the run and the jack is", jack3 < spool3);

    long before = s.money;
    int j = site_jack(&s, up, core, 22, CAB_CAT5E);
    ck("a jack goes in, and is charged in full on the day it is ordered",
       j == 0 && s.money == before - site_jack_price(CAB_CAT5E, m));
    ck("it is on the wall of the room, not on a box",
       s.jack[j].room == up && s.jack[j].metres == m);

    /* THE DAYS. This is the half money cannot buy back, and it is measured
     * against the same s->day the tenancy strike clock runs on. */
    ck("it takes the trade days, from the metres they have to pull",
       site_jack_days(m) >= 2 && s.jack[j].ready == s.day + site_jack_days(m));
    int sw = gate_box(&s, SDEV_SWITCH8, up, "fsw");
    ck("nothing plugs into it before the trade has been",
       site_patch(&s, j, sw, 0) < 0 && s.err == SITE_EEARLY);
    buf_clear(&o);
    site_cmd(&s, "jacks", &o);
    ck("and `jacks` says which day it will be a socket",
       has(o.p, "the trade comes on day"));
    while (s.day < s.jack[j].ready) site_day(&s, NULL);

    /* THE PANEL PORT IS GONE, from the moment it is ordered -- there is no
     * hole in it any more, and `serve` must not find one either. */
    ck("the port at the far end is held, and is not a free port",
       site_port_jack(&s, core, 22) == j && site_free_port(&s, core) != 22);
    ck("and copper off the spool cannot be run into it",
       site_cable(&s, core, 22, sw, 1, CAB_CAT5E) < 0 && s.err == SITE_EJACK);

    /* AND NEITHER END OF IT WALKS OFF. The pair is terminated on that
     * socket and screwed to a wall in another room, so the far box is where
     * it lives now -- which is the cost of `for good` and is said at the
     * moment the money leaves. */
    ck("the box the run is punched down into does not move again",
       !site_move(&s, core, a_room(b, 3)) && s.err == SITE_EJACK &&
       s.dev[core].room == mdf);
    ck("and both ends of a jack in one box is refused, as a loop is",
       site_patch(&s, j, core, 3) < 0 && s.err == SITE_EBUSY);

    /* A BOX IN THE ROOM PLUGS IN FOR A LEAD, and it is a real link on the
     * wire: the same metres, the same grade, the same port state. */
    before = s.money;
    int l = site_patch(&s, j, sw, 0);
    ck("a box standing in that room plugs in for the price of a lead",
       l >= 0 && s.money == before - site_jack_lead_price());
    ck("and it is a real link, on the jack's own metres and grade",
       l >= 0 && s.link[l].metres == m && s.link[l].kind == CAB_CAT5E &&
       site_link_state(&s, l) == PORT_UP && s.link[l].jack == j);

    /* AND A BOX THAT IS NOT IN THE ROOM DOES NOT, which is the whole of what
     * makes this a decision about a room rather than a discount. */
    int away = gate_box(&s, SDEV_SWITCH8, a_room(b, 3), "elsewhere");
    ck("a box in another room cannot reach it, however much it would like to",
       site_patch(&s, j, away, 0) < 0 && s.err == SITE_ENOROOM);

    /* THE PAYOFF, AND IT IS THE ONLY ONE. The box goes; the copper does not.
     * The same move off the spool is the whole run again, and site_uncable
     * has refunded nothing since the day it was written. */
    long spent_before = s.spent;
    site_uncable(&s, l);
    ck("the lead comes out and the jack is still in the wall",
       s.jack[j].link < 0 && site_port_jack(&s, core, 22) == j);
    ck("pulling a lead refunds nothing either", s.spent == spent_before);
    ck("and the box can leave the room now that nothing is in it",
       site_move(&s, sw, a_room(b, 3)));
    site_move(&s, sw, up);
    before = s.money;
    int l2 = site_patch(&s, j, sw, 1);
    ck("and the next box in that room is a lead, not a run",
       l2 >= 0 && s.money == before - site_jack_lead_price() &&
       site_link_state(&s, l2) == PORT_UP);

    /* WHAT THE PLAYER READS. `links` distinguishes copper in the wall from
     * money gone, and it has to distinguish this third thing too: copper in
     * the wall that is still yours. */
    buf_clear(&o);
    site_cmd(&s, "links", &o);
    ck("`links` says how many jacks are in the wall and what they cost",
       has(o.p, "jack in the wall") && has(o.p, "paid to have them put in"));
    ck("and marks the runs that are a lead into one",
       has(o.p, "a lead in j0"));
    buf_clear(&o);
    site_cmd(&s, "show core", &o);
    ck("`show` on the far box says which of its ports is punched down",
       has(o.p, "punched down to jack j0") && has(o.p, "for good"));
    buf_free(&o);
    site_free(&s);
}

/* =========================================== WHAT IT COSTS, BEFORE IT COSTS
 *
 * D28's correction, in the playtester's own words: *"There is no way to
 * measure a run before paying for it, so exercising the marginal-copper rule
 * is guess-and-pay at ~110 a guess."* They were right about more than that
 * rule -- cat5 against cat5e against cat6, spool against jack, this cupboard
 * against that one are all decisions D27 built and every one of them was made
 * blind. `quote` is the answer, and the only thing that makes it worth having
 * is that it is the SAME arithmetic that will charge for the run.
 *
 * So the load-bearing assertion in here is not that a quote prints a number.
 * It is that the number it prints is the number the invoice shows: quote a
 * run, run it, and compare the money. A quote that disagrees with the bill is
 * worse than no quote at all.
 */
static int metres_in(const char *s)
{
    /* "...: 95 m through the tray." -- read it back out of the words the
     * player reads, not out of a variable the test also set. */
    const char *p = s ? strstr(s, " m through the tray") : NULL;
    if (!p) return -1;
    while (p > s && p[-1] >= '0' && p[-1] <= '9') p--;
    return atoi(p);
}

/* The nearest and farthest lettable room on a floor, by TRAY metres from the
 * MDF. This is the fact the whole feature exists for: D28 measured floor 3
 * ranging from 39 m to 92 m and a player had no way to tell which room they
 * were looking at. */
static void far_and_near(const Site *s, const Building *b, int floor, int from,
                         int *far, int *near)
{
    int bf = -1, bn = -1, dfar = -1, dnear = 1 << 30;
    for (int i = 0; i < b->nrooms; i++) {
        if (b->rooms[i].floor != floor || !leasable(b->rooms[i].kind)) continue;
        int m = site_metres(s, from, i);
        if (m < 0) continue;
        if (m > dfar)  { dfar = m; bf = i; }
        if (m < dnear) { dnear = m; bn = i; }
    }
    *far = bf; *near = bn;
}

static void check_quote(const Building *b)
{
    printf("\nwhat a run would cost, asked before the money leaves\n");
    Site s; Buf o = {0};
    site_new(&s, b, GATE_SEED, 200000);
    int mdf = bld_find(b, 0, RM_MDF);
    int core = gate_box(&s, SDEV_SWITCH24, mdf, "core");

    /* ---- THE THING THE PLAYTESTER COULD NOT SEE. One floor, two rooms, and
     * the difference between them is the whole marginal-copper rule. */
    int far = -1, near = -1;
    far_and_near(&s, b, 3, mdf, &far, &near);
    int mfar = site_metres(&s, mdf, far), mnear = site_metres(&s, mdf, near);
    ck("one deck spans safe to marginal, which is why a room name tells "
       "you nothing",
       far >= 0 && near >= 0 && mfar >= SITE_COPPER_MARGIN_M &&
       mnear < SITE_COPPER_MARGIN_M);
    printf("    deck 3 from the Engineering: #%d is %d m and #%d is %d m\n",
           near, mnear, far, mfar);

    /* ---- THE METRES ARE bld_cable_all()'s, THROUGH site_metres(). */
    char line[80];
    snprintf(line, sizeof line, "quote core #%d", far);
    buf_clear(&o);
    site_cmd(&s, line, &o);
    ck("`quote` answers with the tray metres the run will be charged on",
       metres_in(o.p) == mfar);

    /* ---- EVERY PRICE IN IT IS THE FUNCTION THAT WILL CHARGE IT. Four
     * grades, both ways of buying the same metres, all read out of the text
     * the player sees. */
    bool priced = true, jacked = true;
    for (int k = 0; k < CAB_KIND_COUNT; k++) {
        char want[96];
        snprintf(want, sizeof want, "  %-6s  %11d", site_cable_name((CableKind)k),
                 site_cable_price((CableKind)k, mfar));
        if (!has(o.p, want)) {
            printf("    the quote's spool price for %s is not "
                   "site_cable_price()'s\n", site_cable_name((CableKind)k));
            priced = false;
        }
        snprintf(want, sizeof want, "%11d   %9d",
                 site_cable_price((CableKind)k, mfar),
                 site_jack_price((CableKind)k, mfar));
        if (!has(o.p, want) ||
            site_jack_price((CableKind)k, mfar) <=
            site_cable_price((CableKind)k, mfar)) {
            printf("    the quote's jack price for %s is not "
                   "site_jack_price()'s\n", site_cable_name((CableKind)k));
            jacked = false;
        }
    }
    ck("every grade is priced off the spool at site_cable_price()", priced);
    ck("and beside it as a jack, at site_jack_price() and always dearer",
       jacked);
    {
        char days[64];
        snprintf(days, sizeof days, "a jack is %d day", site_jack_days(mfar));
        ck("and the days the trade takes are site_jack_days() of the same metres",
           has(o.p, days) && has(o.p, "not a socket before then"));
    }

    /* ---- WHAT EACH GRADE WOULD COME UP AT, which since D27 is the cable AND
     * the kit. `core` is a switch24 and port 0 of it is a gigabit socket, so
     * cat6 and fibre buy nothing here and the quote says so. */
    ck("cat5 is a hundred megabit at any distance and the quote says so",
       site_cable_speed(CAB_CAT5, mfar) == 100 &&
       site_cable_speed(CAB_CAT5, 3) == 100);
    ck("cat6 is ten gigabit to 55 m and a gigabit past it, measured not stated",
       site_cable_speed(CAB_CAT6, 55) == 10000 &&
       site_cable_speed(CAB_CAT6, 56) == 1000);
    ck("copper of every grade carries a hundred metres and no more",
       site_cable_speed(CAB_CAT5E, 100) == 1000 &&
       site_cable_speed(CAB_CAT5E, 101) == 0 &&
       site_cable_speed(CAB_CAT6, 101) == 0 &&
       site_cable_speed(CAB_FIBRE, 101) == 10000);
    ck("and the port at the end has the last word, said where the grade is "
       "chosen",
       has(o.p, "core:0 does 1000 Mb whatever you plug into it"));

    /* ---- AND THE RULE THAT COULD NOT BE REACHED. */
    ck("a run past the margin says so, about this run, in metres",
       has(o.p, "is past the 90 m copper has margin for"));
    buf_clear(&o);
    snprintf(line, sizeof line, "quote core #%d", near);
    site_cmd(&s, line, &o);
    ck("and the room on the same deck that is not does not",
       metres_in(o.p) == mnear && !has(o.p, "copper has margin for"));

    /* ---- A QUOTE IS A QUOTE. Nothing is bought, nothing is booked, nothing
     * is charged, and nothing moves. */
    long money = s.money, spent = s.spent;
    int nl = s.nlink, nj = s.njack, nd = s.ndev;
    buf_clear(&o);
    site_cmd(&s, line, &o);
    ck("asking costs nothing: no money, no link, no jack, no box",
       s.money == money && s.spent == spent && s.nlink == nl &&
       s.njack == nj && s.ndev == nd);
    ck("and it says so, so nobody has to wonder", has(o.p, "nothing was bought"));

    /* ---- AND THE ONE THAT MATTERS: THE QUOTE IS THE BILL. */
    int sw = gate_box(&s, SDEV_SWITCH8, far, "sw3");
    buf_clear(&o);
    site_cmd(&s, "quote sw3:0 core:0", &o);
    int quoted_m = metres_in(o.p);
    int quoted_price = site_cable_price(CAB_CAT5E, quoted_m);
    long before = s.money;
    int l = site_cable(&s, sw, 0, core, 0, CAB_CAT5E);
    ck("the run the quote measured is the run the invoice measures",
       l >= 0 && s.link[l].metres == quoted_m && quoted_m == mfar);
    ck("and the price it quoted is the money that actually left",
       s.link[l].cost == quoted_price && before - s.money == quoted_price);
    printf("    quoted %d m of cat5e at %d; the bill was %d m at %d\n",
           quoted_m, quoted_price, s.link[l].metres, s.link[l].cost);
    /* And the speed it promised is the speed the port really came up at. */
    int cabmb = site_cable_speed(CAB_CAT5E, quoted_m);
    int kitmb = site_kind_port_mb(SDEV_SWITCH8, 0);
    ck("and the speed it promised is what net_port_speed reads off the port",
       net_port_speed(s.net, s.dev[sw].node, 0) ==
       (cabmb < kitmb ? cabmb : kitmb));

    /* ---- THE MARGIN IS core/siteday.c's NUMBER, NOT A SECOND COPY OF IT.
     * The quote prints ninety because SITE_COPPER_MARGIN_M says ninety, and
     * the behaviour lives in another file. So play it: put a floor of desks
     * behind the marginal run and behind a short one carrying the identical
     * frames, turn the days, and let `events` say which of the two the world
     * thinks is marginal. If that number ever moves in siteday.c and not
     * here, this fails. */
    int rt = gate_box(&s, SDEV_ROUTER, mdf, "rt");
    site_cable(&s, rt, 0, s.uplink, 0, CAB_CAT6);
    int shortm = site_cable(&s, rt, 1, core, 1, CAB_CAT6);
    site_addr(&s, rt, 0, s.wan_you, s.wan_mask);
    site_addr(&s, rt, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, rt, s.wan_isp);
    site_forwarding(&s, rt, true);
    site_dhcpd(&s, rt, net_ip(10, 0, 1, 1), 400, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    int who = -1;
    for (int i = 0; i < 400 && who < 0; i++) {
        unserved_day(&s, NULL);
        for (int t = 0; t < s.ntenant; t++)
            if (s.tenant[t].moved && s.tenant[t].floor == 3) who = t;
    }
    if (who >= 0) site_serve(&s, who, sw, CAB_CAT5E);
    bool warned = false, control = true;
    for (int i = 0; i < 40 && !warned; i++) {
        unserved_day(&s, NULL);
        buf_clear(&o);
        site_dump_events(&s, &o);
        if (has(o.p, "taking errors under load")) warned = true;
    }
    ck("the world agrees the marginal run is the marginal one: it takes "
       "errors",
       warned && has(o.p, "sw3"));
    /* THE CONTROL, and it is the same frames. Every desk behind sw3 pulls its
     * files through rt:1 to core:1 as well, so the short run carried the
     * identical load and the world never called it marginal. */
    if (has(o.p, "rt:1")) control = false;
    ck("and the short run carrying the identical frames never does",
       control && shortm >= 0 && s.link[shortm].metres < SITE_COPPER_MARGIN_M);
    printf("    %d m warned; %d m through the same traffic did not\n",
           s.link[l].metres, s.link[shortm].metres);

    buf_free(&o);
    site_free(&s);
}

/* Everything above is reachable from a pipe, or a blind playtester cannot
 * find any of it. This builds a working network out of nothing but lines of
 * text, and then asks the machine on floor two what it can see. */
static void check_shell(const Building *b)
{
    printf("\nthe whole thing, over a pipe\n");
    /* ORDERED AND CARRIED, because that is what buying hardware is. Every
     * one of these boxes is delivered to goods in on the ground floor and
     * moved from there; `move` is the line that stands for the walk, and
     * core/sessioncheck.c is where the walk itself is charged. */
    static const char *SCRIPT[] = {
        "order switch8 sw2",
        "move sw2 d2.comms",
        "order router rt",
        "move rt d0.eng",
        "order pc pc1",
        /* AND THE BUTTON COMES AFTER THE WALK, since D37. This line used to
         * sit above the `move` and switch a machine on while it was still
         * on a pallet under the roller door -- which worked, because until
         * there was a plug nothing in this game drew power from anywhere.
         * A pc in goods in is in its box; it is switched on in the room it
         * is carried to, out of a socket on that room's wall. */
        "move pc1 d2.office",
        "power pc1 on",
        "cable rt:0 uplink:0 cat6",
        "cable rt:1 sw2:0 cat6",
        "cable pc1:0 sw2:1 cat6",
        "addr rt 198.51.100.2/30",
        "addr rt:1 192.168.7.1/24",
        "router rt on",
        "gw rt 198.51.100.1",
        "addr pc1 192.168.7.10/24",
        "gw pc1 192.168.7.1",
        "resolver pc1 198.51.100.1",
        NULL
    };
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    bool understood = true;
    Buf o = {0};
    for (int i = 0; SCRIPT[i]; i++) {
        if (!site_cmd(&s, SCRIPT[i], &o)) understood = false;
        gate_feed_all(&s);
    }
    ck("nineteen lines of text order it, carry it in and make it work",
       understood);

    buf_clear(&o);
    site_cmd(&s, "ping pc1 198.51.100.1", &o);
    ck("and the machine it built can reach the internet",
       o.p && strstr(o.p, "reply") != NULL);

    buf_clear(&o);
    site_cmd(&s, "resolve pc1 wiki.nomnix.org", &o);
    ck("and resolve a name over UDP", o.p && strstr(o.p, "10.0.2.20") != NULL);

    buf_clear(&o);
    site_cmd(&s, "get pc1 10.0.2.20 /boot", &o);
    ck("and fetch a page over TCP through the router it configured",
       o.p && strstr(o.p, "HTTP 200") != NULL);

    /* A refusal is an answer, not a crash. */
    buf_clear(&o);
    site_cmd(&s, "cable pc1:0 sw2:7 cat6", &o);
    ck("a refusal comes back as words a player can read",
       o.p && strstr(o.p, "refused") != NULL);

    buf_clear(&o);
    ck("and a line nobody understands says so", !site_cmd(&s, "frobnicate", &o));
    buf_free(&o);
    site_free(&s);
}

/* ------------------------------------------------------------------ main */
/* ==================================================== THE FOUR INDUSTRIES
 *
 * Every tenancy in this game used to be the same business, so a floor only
 * ever asked one question -- is there enough throughput -- and a playtester
 * who reached day 62 said what that cost: *"I made the riser decision on
 * deck 1 and then repeated it on decks 2 and 3 without thinking."*
 *
 * These checks are the answer, and the thing they have to prove is not that
 * four words appear in `demand`. It is that THE RIGHT BUILD FOR ONE TENANCY
 * IS THE WRONG BUILD FOR THE ONE BESIDE IT: the same building, the same
 * copper, the same day, one of them perfectly served and the other one not,
 * for a reason that belongs to their trade.
 *
 * WHY TWO MORE BUILDINGS. The gate's own tower (7008) lets to an office
 * first and does not put a second trade on the ground for weeks, and a check
 * that has to run forty days of busy period to reach its scenario is a check
 * nobody runs. Seed 22 lets an office, a studio and a call centre onto ONE
 * floor inside thirteen days; seed 23 lets an office and a web host onto one
 * floor inside eight. Both are this generator's own letting queue -- nothing
 * below places a tenancy, chooses its trade or sets its size.
 */
typedef struct {
    Site s;
    int  mdf, rt, core, srv, nsw;
    int  sw[4];
} Tower;

/* THE FIRST TENANCY OF THAT TRADE ON THAT FLOOR, found rather than written
 * down, so that a change to the generator makes this check move rather than
 * lie. -1 when the seed has none. */
static int trade_on(const Site *s, int kind, int floor)
{
    for (int i = 0; i < s->ntenant; i++)
        if (s->tenant[i].kind == kind && s->tenant[i].floor == floor) return i;
    return -1;
}

/* A tower with a routed edge, a core switch and a switch per tenancy on one
 * floor, flat on 10.0.0.0/16 with the router serving addresses -- the build
 * a player has by the end of their first hour. `riser` is the drum they put
 * up the riser and `floor_files` decides whether the file server stands in
 * the floor's own cupboard or in the basement. Every line is a line a player
 * types, and the money is credited because this gate is about the network. */
static void tower_up(Tower *w, const Building *b, uint64_t seed, int comms,
                     CableKind riser, bool floor_files, int nsw)
{
    Site *s = &w->s;
    site_new(s, b, seed, 100000);
    site_credit(s, 900000);
    w->mdf = bld_find(b, 0, RM_MDF);
    w->rt = gate_box(s, SDEV_ROUTER, w->mdf, "edge");
    w->core = gate_box(s, SDEV_SWITCH24, w->mdf, "core");
    site_cable(s, w->rt, 0, s->uplink, 0, CAB_CAT6);
    site_cable(s, w->rt, 1, w->core, 0, CAB_CAT6);
    site_addr(s, w->rt, 0, s->wan_you, s->wan_mask);
    site_addr(s, w->rt, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(s, w->rt, s->wan_isp);
    site_forwarding(s, w->rt, true);
    site_dhcpd(s, w->rt, net_ip(10, 0, 1, 1), 250, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s->wan_isp);
    w->nsw = nsw;
    for (int i = 0; i < nsw; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "sw%d", i + 1);
        w->sw[i] = gate_box(s, SDEV_SWITCH24, comms, nm);
        site_cable(s, w->core, 1 + i, w->sw[i], 0, riser);
    }
    w->srv = gate_box(s, SDEV_SERVER, floor_files ? comms : w->mdf, "files");
    site_power(s, w->srv, true);
    if (floor_files) site_cable(s, w->sw[0], 22, w->srv, 0, CAB_CAT6);
    else             site_cable(s, w->core, 22, w->srv, 0, CAB_CAT6);
    site_addr(s, w->srv, 0, net_ip(10, 0, 0, 9), net_mask_bits(16));
    site_gateway(s, w->srv, net_ip(10, 0, 0, 1));
    site_httpd(s, w->srv, 80);
}

/* Run days until that tenancy is in, forgiving the ones this scenario never
 * promised anything to -- the same thing --loadcheck's keep_measuring does,
 * and for the same reason: a check that has to reach day thirteen cannot
 * have its run ended on day six by a tenancy it is not about. */
static void tower_until(Tower *w, int ti)
{
    for (int g = 0; g < 200 && !w->s.tenant[ti].moved; g++) {
        w->s.over = 0;
        w->s.complaints = 0;
        for (int i = 0; i < w->s.ntenant; i++) {
            w->s.tenant[i].strikes = 0;
            w->s.tenant[i].complained = 0;
        }
        site_day(&w->s, NULL);
    }
}

/* What share of what they were promised really happened. */
static int got_pct(const Site *s, int ti)
{
    const SiteTenant *t = &s->tenant[ti];
    return t->tried ? (t->finished * 100) / t->tried : 0;
}

/* ------------------------------------------------- an office and a studio
 * The same floor, the same copper, the same day. The office's files come off
 * a server ten metres away and never touch the landlord's circuit; the
 * studio's day exists entirely ON that circuit, upwards, which is the one
 * direction a riser sized for desks was never sized for. So the size of the
 * circuit is a decision that does not touch one of them and decides the
 * other -- a thing this game could not previously express at all, because
 * every tenancy in it pulled the same bytes in the same direction. */
static void check_industry_upload(void)
{
    printf("\nan office and a studio on one deck, wanting opposite things\n");
    Building b;
    if (!bld_generate(&b, 22ull)) { ck("seed 22 makes a building", false); return; }
    int comms = bld_find(&b, 1, RM_COMMS);
    if (comms < 0) comms = a_room(&b, 1);

    Tower w;
    tower_up(&w, &b, 22ull, comms, CAB_CAT5E, true, 3);
    Site *s = &w.s;
    int off = trade_on(s, TEN_OFFICE, 1), stu = trade_on(s, TEN_STUDIO, 1);
    if (off < 0 || stu < 0) {
        ck("seed 22 lets an office and a studio onto deck 1", false);
        site_free(s); bld_free(&b); return;
    }
    tower_until(&w, stu);
    site_serve(s, off, w.sw[0], CAB_CAT5E);
    site_serve(s, stu, w.sw[1], CAB_CAT5E);
    site_day(s, NULL);

    char line[120];
    snprintf(line, sizeof line, "on the 500 Mb circuit both trades are served "
             "(office %d%%, studio %d%%)", got_pct(s, off), got_pct(s, stu));
    ck(line, got_pct(s, off) >= 80 && got_pct(s, stu) >= 80);
    ck("and the studio's work really went UP, off the handoff's own port",
       s->tenant[stu].up_kb > 4096 &&
       net_port_busy_us(s->net, s->dev[s->uplink].node, 0) > 0);

    /* NOW BUY THE CHEAP CIRCUIT, which is the ordinary saving: the tower's
     * files are on the floor, almost nothing crosses the handoff, and a
     * hundred megabits looks like plenty. It is plenty -- for the office. */
    int off_was = got_pct(s, off);
    site_isp(s, 100);
    site_day(s, NULL);
    snprintf(line, sizeof line, "cut the circuit to 100 Mb and the studio is "
             "ruined by it -- UPWARDS (%ld KB up of %ld, %d%%)",
             s->tenant[stu].up_kb, s->tenant[stu].up_want_kb, got_pct(s, stu));
    ck(line, got_pct(s, stu) < 80 && s->tenant[stu].up_want_kb > 0 &&
             s->tenant[stu].up_kb < s->tenant[stu].up_want_kb);
    /* AND IT TAKES THE FLOOR WITH IT, which is the owner's sentence in one
     * measurement: *worth taking if you have built for it, ruinous if you
     * have not.* The office's own files never leave the floor and their day
     * still falls apart, because the suites next door are filling the only
     * way out of the building with a stream that cannot be slowed down. */
    snprintf(line, sizeof line, "and takes the OFFICE down with it, whose files "
             "never leave the deck (%d%% from %d%%)", got_pct(s, off), off_was);
    ck(line, got_pct(s, off) < off_was - 20);
    /* AND THE FIX IS A MONTHLY BILL RATHER THAN A CABLE, which is the only
     * recurring decision in this game and the one a studio makes expensive. */
    site_isp(s, 500);
    site_day(s, NULL);
    snprintf(line, sizeof line, "buy the circuit back and both are served again "
             "(office %d%%, studio %d%%)", got_pct(s, off), got_pct(s, stu));
    ck(line, got_pct(s, off) >= 80 && got_pct(s, stu) >= 80);
    site_isp(s, 100);
    site_day(s, NULL);

    Buf sv = {0};
    site_dump_service(s, &sv);
    ck("`service` says so in their own units -- streams, and the KB that went up",
       has(sv.p, "streams dropped") && has(sv.p, "they had to have") &&
       s->tenant[stu].up_want_kb == (long)SITE_STREAM_KB * SITE_STREAM_LEGS *
                                    site_tenant_addressed(s, stu));
    ck("and the trade is on the row, so it is not a mystery which is which",
       has(sv.p, "studio") && has(sv.p, "office"));
    buf_free(&sv);
    site_free(s);
    bld_free(&b);
}

/* ---------------------------------------------- an office and a web host
 * A web host's traffic arrives from OUTSIDE, into a machine that is theirs.
 * An office whose server is off is served by whatever else is powered, a
 * little slower, and nobody notices. A web host whose server is off is not
 * slower -- they are gone, and their lease says what that costs. */
static void check_industry_uptime(void)
{
    printf("\nan office and a web host, and what 'down' means to each\n");
    Building b;
    if (!bld_generate(&b, 23ull)) { ck("seed 23 makes a building", false); return; }
    int comms = bld_find(&b, 1, RM_COMMS);
    if (comms < 0) comms = a_room(&b, 1);

    Tower w;
    tower_up(&w, &b, 23ull, comms, CAB_CAT5E, true, 2);
    Site *s = &w.s;
    int off = trade_on(s, TEN_OFFICE, 1), host = trade_on(s, TEN_WEBHOST, 1);
    if (off < 0 || host < 0) {
        ck("seed 23 lets an office and a web host onto deck 1", false);
        site_free(s); bld_free(&b); return;
    }
    tower_until(&w, host);
    /* Their own machine, in their own room, because that is what `demand`
     * said they wanted and it is where their site lives. */
    int wsrv = gate_box(s, SDEV_SERVER, s->tenant[host].room, "wsrv");
    site_power(s, wsrv, true);
    site_cable(s, w.sw[1], 21, wsrv, 0, CAB_CAT6);
    site_addr(s, wsrv, 0, net_ip(10, 0, 0, 20), net_mask_bits(16));
    site_gateway(s, wsrv, net_ip(10, 0, 0, 1));
    site_httpd(s, wsrv, 80);
    site_serve(s, off, w.sw[0], CAB_CAT5E);
    site_serve(s, host, w.sw[1], CAB_CAT5E);
    /* Past the fit-out window, so that what follows is a service they have
     * been getting rather than one they never started. */
    for (int d = 0; d < 4; d++) site_day(s, NULL);

    char line[120];
    snprintf(line, sizeof line, "both are served while the path in works "
             "(office %d%%, host %d%%)", got_pct(s, off), got_pct(s, host));
    ck(line, got_pct(s, off) >= 80 && got_pct(s, host) >= 95);
    /* AND IT REALLY CAME FROM OUTSIDE. The visitors are TCP connections
     * opened BY the handoff, so they crossed the handoff's own card inwards
     * -- which is a fact about a port and not about anything counted here. */
    ck("what they are judged on is VISITORS -- exactly a day's worth of them, "
       "not their staff's transfers",
       s->tenant[host].tried == SITE_WEB_HITS &&
       net_port_busy_us(s->net, s->dev[s->uplink].node, 0) > 0);
    ck("and `service` names their own machine as the thing that answered",
       s->tenant[host].files_dev == wsrv);

    /* WHAT A BLACKOUT DOES TO EACH OF THEM. Their machine goes off; the
     * office's file server is untouched. */
    long before = s->money;
    site_power(s, wsrv, false);
    site_day(s, NULL);
    snprintf(line, sizeof line, "their machine off: the OFFICE is still served "
             "(%d%%)", got_pct(s, off));
    ck(line, got_pct(s, off) >= 80);
    snprintf(line, sizeof line, "and the web host is not slower, they are DOWN "
             "(%d%%)", got_pct(s, host));
    ck(line, got_pct(s, host) == 0 && s->tenant[host].down);
    ck("no other server in the building will answer for their site",
       s->tenant[host].files_dev < 0 && s->tenant[off].files_dev >= 0);
    ck("and a day down costs the landlord a day's rent BACK, not just the rent",
       s->last.sla == s->tenant[host].rent / 30 && s->last.sla > 0 &&
       s->money < before);
    Buf sv = {0};
    site_dump_service(s, &sv);
    ck("`service` says they answered nothing from the internet, in those words",
       has(sv.p, "answered NOTHING from the internet"));
    ck("and says what was handed back, and why", has(sv.p, "rent handed BACK"));
    buf_free(&sv);

    /* AND THE BATTERY IS THE DECISION. Switch it back on -- and address it
     * again, because a box that was switched off lost the addresses, the
     * routes and the sockets it was holding, which is what site_power says
     * it does and is why a machine coming back from a blackout is work. */
    site_power(s, wsrv, true);
    site_addr(s, wsrv, 0, net_ip(10, 0, 0, 20), net_mask_bits(16));
    site_gateway(s, wsrv, net_ip(10, 0, 0, 1));
    site_httpd(s, wsrv, 80);
    site_day(s, NULL);
    ck("switch it back on and they are served again, and the credit stops",
       got_pct(s, host) >= 95 && s->last.sla == 0);
    site_free(s);
    bld_free(&b);
}

/* ------------------------------------------------- an office and a voice
 * The one that could not be said at all before: a wire that is fine for
 * every transfer on it and unusable for the calls. A file transfer does not
 * notice a lost packet -- TCP asks again -- and a call cannot ask again,
 * because the moment that audio was for has gone. So the office behind a
 * riser is paid and the call centre behind the SAME riser has no business,
 * and no amount of bandwidth is the fix: it is where the bulk traffic goes.
 */
static void check_industry_voice(void)
{
    printf("\nan office and a call centre behind one riser\n");
    Building b;
    if (!bld_generate(&b, 22ull)) { ck("seed 22 makes a building", false); return; }
    int comms = bld_find(&b, 1, RM_COMMS);
    if (comms < 0) comms = a_room(&b, 1);

    /* THE PLANNED ANSWER FIRST: the files on the floor, so a floor's day
     * never crosses the riser at all. */
    Tower w;
    tower_up(&w, &b, 22ull, comms, CAB_CAT5E, true, 3);
    Site *s = &w.s;
    int off = trade_on(s, TEN_OFFICE, 1), voi = trade_on(s, TEN_VOICE, 1);
    if (off < 0 || voi < 0) {
        ck("seed 22 lets an office and a call centre onto deck 1", false);
        site_free(s); bld_free(&b); return;
    }
    tower_until(&w, voi);
    site_serve(s, off, w.sw[0], CAB_CAT5E);
    site_serve(s, voi, w.sw[2], CAB_CAT5E);
    site_day(s, NULL);
    char line[120];
    snprintf(line, sizeof line, "with the files on the deck both are served "
             "(office %d%%, calls %d%%)", got_pct(s, off), got_pct(s, voi));
    ck(line, got_pct(s, off) >= 80 && got_pct(s, voi) >= 80);
    ck("and the calls were real UDP through the stack, not a number beside it",
       s->tenant[voi].tried == site_tenant_addressed(s, voi) &&
       s->tenant[voi].tried > 0 && s->tenant[voi].bytes > 0);
    int good_conceal = s->tenant[voi].conceal_ppm;
    site_free(s);

    /* THE ORDINARY MISTAKE: the file server in the basement, so a floor's
     * whole day comes down the riser, and the cheapest drum in the catalogue
     * up it. The office survives it. The calls do not. */
    tower_up(&w, &b, 22ull, comms, CAB_CAT5, false, 3);
    s = &w.s;
    tower_until(&w, voi);
    site_serve(s, off, w.sw[0], CAB_CAT5E);
    site_serve(s, voi, w.sw[2], CAB_CAT5E);
    site_day(s, NULL);
    snprintf(line, sizeof line, "a hundred megabit riser to the basement ruins "
             "the CALLS (%d%% of %d of them)", got_pct(s, voi),
             s->tenant[voi].tried);
    ck(line, got_pct(s, voi) < 80 &&
             s->tenant[voi].tried == site_tenant_addressed(s, voi));
    snprintf(line, sizeof line, "and it is concealment that did it, measured on "
             "the streams (%d ppm against %d)",
             s->tenant[voi].conceal_ppm, good_conceal);
    ck(line, s->tenant[voi].conceal_ppm > good_conceal &&
             s->tenant[voi].conceal_ppm > 20000);
    Buf sv = {0};
    site_dump_service(s, &sv);
    ck("`service` blames the calls, in concealment and delay, not transfers",
       has(sv.p, "calls broke up") && has(sv.p, "concealed"));
    buf_free(&sv);
    site_free(s);
    bld_free(&b);
}

/* --------------------------------------------------- and the price of it */
static void check_industry_rent(const Building *b)
{
    printf("\nthe rent follows what they want, and says so before you sign\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    Buf d = {0};
    site_dump_demand(&s, &d);
    ck("`demand` names the trade of every tenancy, not just its drops",
       has(d.p, "trade") && has(d.p, "web host") && has(d.p, "studio") &&
       has(d.p, "voice") && has(d.p, "office"));
    ck("and says what each one will ask the network for, before the lease",
       has(d.p, "sustained UPLOAD") && has(d.p, "reachable INWARDS") &&
       has(d.p, "no loss, no jitter"));
    ck("and what each trade pays for the same square metres",
       has(d.p, "(300%)") && has(d.p, "(240%)") && has(d.p, "(170%)"));
    ck("and how a web host's outage is billed differently from a slow day",
       has(d.p, "day's rent BACK"));
    int nk[TEN_KIND_COUNT];
    memset(nk, 0, sizeof nk);
    for (int i = 0; i < s.ntenant; i++)
        if (s.tenant[i].kind < TEN_KIND_COUNT) nk[s.tenant[i].kind]++;
    ck("the tower lets to more than one trade, off its own seed",
       nk[TEN_OFFICE] > 0 && (nk[TEN_STUDIO] + nk[TEN_VOICE] + nk[TEN_WEBHOST]) > 2);
    ck("and the office is still the common case, which is what the curve is",
       nk[TEN_OFFICE] * 2 > s.ntenant);
    ck("a studio pays three times an office and a web host two and a bit",
       site_tenant_rent_pct(TEN_STUDIO) == 300 &&
       site_tenant_rent_pct(TEN_WEBHOST) == 240 &&
       site_tenant_rent_pct(TEN_OFFICE) == 100);
    /* THE PREMIUM IS REAL MONEY AND NOT A SENTENCE: two tenancies of the same
     * size in the same building do not pay the same rent if their trades
     * differ, and the ratio is the one printed above. */
    bool priced = false;
    for (int i = 0; i < s.ntenant && !priced; i++)
        for (int j = 0; j < s.ntenant; j++) {
            if (s.tenant[i].kind != TEN_OFFICE || s.tenant[j].kind != TEN_STUDIO) continue;
            if (s.tenant[i].drops != s.tenant[j].drops) continue;
            if (s.tenant[j].rent > s.tenant[i].rent * 2) { priced = true; break; }
        }
    ck("and two same-sized tenancies of different trades pay different rent",
       priced);
    buf_free(&d);
    site_free(&s);
}

/* ================================ THE ROOMS A TENANCY ACTUALLY OCCUPIES
 *
 * D35. `move_in` used to install every desk into `t->room`, the first room
 * of the tenancy, and it was invisible until another agent seated a person
 * at every desk: seed 7008's tenancy 1 holds eleven offices and a server
 * room, and all twenty people were in `#36` while ten offices stood empty.
 * A playtester: *"the building the letting agent describes and the building
 * you walk through aren't the same building."*
 *
 * What is asserted here is not "the desks are spread out" -- that is a
 * sentence. It is the three things spreading them was FOR:
 *
 *   the rooms they lease are the rooms they are in, and a server room is
 *   not one of them;
 *   the split follows the square metres the building generator produced,
 *   rather than any number typed in this project;
 *   and it is priced -- the same tenancy's nearest and farthest desk are a
 *   different run from the same cupboard, which is the first thing in this
 *   game that makes WITHIN-floor distance cost money.
 *
 * Plus the one that must not regress: nothing became unreachable. Every
 * desk still cables, still comes up, and is still walkable to.
 */
static void check_desk_rooms(const Building *b)
{
    printf("\nthe rooms a tenancy occupies, and what the copper to them costs\n");
    Tower w;
    int floor = 1;
    int comms = comms_on(b, floor, 0);
    tower_up(&w, b, GATE_SEED, comms, CAB_CAT6, true, 1);
    Site *s = &w.s;
    int ti = -1;
    for (int i = 0; i < s->ntenant; i++)
        if (s->tenant[i].floor == floor) { ti = i; break; }
    if (ti < 0) { ck("the gate's tower lets a tenancy on deck 1", false);
                  site_free(s); return; }
    tower_until(&w, ti);
    SiteTenant *t = &s->tenant[ti];

    /* What the letting agent says they hold. Counted off the building, not
     * off anything this file decides. */
    int held = 0, srv_rooms = 0;
    double biggest = 0, smallest = 1e9;
    int rbig = -1, rsmall = -1;
    for (int i = 0; i < b->nrooms; i++) {
        const Room *r = &b->rooms[i];
        if (r->tenant != t->tenant) continue;
        if (r->kind == RM_SERVER) { srv_rooms++; continue; }
        if (!leasable(r->kind)) continue;
        held++;
        double a = bld_room_area(r);
        if (a > biggest)  { biggest = a;  rbig = i; }
        if (a < smallest) { smallest = a; rsmall = i; }
    }
    ck("the gate's tenancy holds a spread of rooms and a server room",
       held >= 5 && srv_rooms >= 1 && rbig >= 0 && rsmall >= 0 &&
       biggest > smallest);
    printf("    tenancy %d holds %d rooms people sit in and %d server room(s); "
           "biggest #%d at %.0f m2, smallest #%d at %.0f m2\n",
           t->tenant, held, srv_rooms, rbig, biggest, rsmall, smallest);

    /* ---- THEY ARE IN THEM. Every leased room that takes people has at
     * least one desk in it, and every desk is in a room this tenancy holds. */
    int occupied = 0, in_server = 0, elsewhere = 0;
    int big_desks = 0, small_desks = 0;
    for (int i = 0; i < b->nrooms; i++) {
        int k = 0;
        for (int d = 0; d < t->ndesk; d++)
            if (s->dev[t->desk0 + d].room == i) k++;
        if (!k) continue;
        occupied++;
        if (b->rooms[i].kind == RM_SERVER) in_server += k;
        if (b->rooms[i].tenant != t->tenant) elsewhere += k;
        if (i == rbig)   big_desks = k;
        if (i == rsmall) small_desks = k;
    }
    ck("every room the tenancy leases to sit in has desks in it",
       occupied == held && held > 1);
    ck("and no desk of theirs stands in anybody else's room",
       elsewhere == 0);
    ck("and none of them is in their server room, which is for equipment",
       in_server == 0 && srv_rooms >= 1);
    ck("the desks are all still there: one per drop they asked for",
       t->ndesk == (int)t->drops);
    printf("    %d desks over %d rooms; the %.0f m2 room holds %d and the "
           "%.0f m2 room holds %d\n",
           t->ndesk, occupied, biggest, big_desks, smallest, small_desks);

    /* ---- AND THE SPLIT IS THE SQUARE METRES. A bigger room holds more
     * desks, and the biggest room holds at least twice what the smallest
     * does when it is at least twice the size. */
    ck("a bigger room takes more desks than a smaller one",
       big_desks > small_desks && small_desks >= 1);
    ck("and twice the deck area takes at least twice the people",
       biggest >= 2 * smallest && small_desks >= 1 &&
       big_desks >= 2 * small_desks);
    /* No pair of their rooms is the wrong way round: over the whole
     * tenancy, more square metres never means fewer desks. */
    bool monotone = true;
    for (int i = 0; i < b->nrooms && monotone; i++) {
        if (b->rooms[i].tenant != t->tenant ||
            !leasable(b->rooms[i].kind) || b->rooms[i].kind == RM_SERVER)
            continue;
        for (int j = 0; j < b->nrooms; j++) {
            if (b->rooms[j].tenant != t->tenant ||
                !leasable(b->rooms[j].kind) || b->rooms[j].kind == RM_SERVER)
                continue;
            int ki = 0, kj = 0;
            for (int d = 0; d < t->ndesk; d++) {
                if (s->dev[t->desk0 + d].room == i) ki++;
                if (s->dev[t->desk0 + d].room == j) kj++;
            }
            if (bld_room_area(&b->rooms[i]) > bld_room_area(&b->rooms[j]) &&
                ki < kj) { monotone = false; break; }
        }
    }
    ck("and no two of their rooms are the wrong way round", monotone);

    /* ---- WHICH IS WHAT MAKES WITHIN-FLOOR DISTANCE A PRICE. The nearest
     * and the farthest desk of ONE tenancy are different runs from the same
     * cupboard, and the difference is real money at the same grade. */
    int mnear = 1 << 30, mfar = -1, dnear = -1, dfar = -1;
    for (int d = 0; d < t->ndesk; d++) {
        int dev = t->desk0 + d;
        int m = site_metres(s, comms, s->dev[dev].room);
        if (m < 0) continue;
        if (m < mnear) { mnear = m; dnear = dev; }
        if (m > mfar)  { mfar  = m; dfar  = dev; }
    }
    ck("the tenancy's nearest and farthest desk are not the same run",
       dnear >= 0 && dfar >= 0 && mfar > mnear);
    ck("and the difference is metres a player would notice",
       mfar - mnear >= 10);
    ck("which is a different price for the identical drop",
       site_cable_price(CAB_CAT5E, mfar) > site_cable_price(CAB_CAT5E, mnear));
    printf("    from the deck's cupboard #%d: %s is %d m at %d, %s is %d m "
           "at %d\n", comms, s->dev[dnear].name, mnear,
           site_cable_price(CAB_CAT5E, mnear), s->dev[dfar].name, mfar,
           site_cable_price(CAB_CAT5E, mfar));

    /* ---- AND NOTHING BECAME UNREACHABLE. `serve` still cables every one of
     * them, every run is inside what copper carries, and every link comes
     * up -- a desk in the far office that does not link is a desk that is
     * not in the game. */
    long before = s->money;
    int done = site_serve(s, ti, w.sw[0], CAB_CAT5E);
    ck("`serve` still cables every desk, wherever in the tenancy it stands",
       done == t->ndesk);
    int up = 0, toolong = 0;
    for (int d = 0; d < t->ndesk; d++) {
        int st = net_port_state(s->net, s->dev[t->desk0 + d].node, 0);
        if (st == PORT_UP) up++;
        if (st == PORT_TOOLONG) toolong++;
    }
    ck("and every one of them comes up: none is over what copper carries",
       up == t->ndesk && toolong == 0);
    bool walkable = true;
    {
        double *wk = malloc(sizeof(double) * (size_t)b->nrooms);
        int lob = bld_find(b, floor, RM_LIFTLOBBY);
        if (lob < 0 || !wk || !bld_walk_all(b, lob, wk)) walkable = false;
        else for (int d = 0; d < t->ndesk; d++)
                 if (wk[s->dev[t->desk0 + d].room] >= BLD_INF) walkable = false;
        free(wk);
    }
    ck("and every desk is walkable to from the lift lobby", walkable);

    /* The bill is the sum of the real runs, not twenty copies of one run --
     * which is the whole point, and it is arithmetic rather than a claim. */
    long spent = before - s->money, want = 0;
    for (int d = 0; d < t->ndesk; d++)
        want += site_cable_price(CAB_CAT5E,
                                 site_metres(s, comms, s->dev[t->desk0 + d].room));
    ck("and the bill is every desk's own metres added up",
       spent == want && spent > 0);
    ck("which is not what twenty copies of the nearest run would cost",
       spent > (long)t->ndesk * site_cable_price(CAB_CAT5E, mnear));
    printf("    serving them from the cupboard cost %ld; the same desks all "
           "in the nearest room would be %ld\n",
           spent, (long)t->ndesk * site_cable_price(CAB_CAT5E, mnear));

    /* ---- AND IT IS THE SAME BUILDING EVERY TIME. Same seed, same desks,
     * same rooms: the apportionment takes no draw at all, so it cannot have
     * moved anything else's stream. */
    Tower w2;
    tower_up(&w2, b, GATE_SEED, comms, CAB_CAT6, true, 1);
    tower_until(&w2, ti);
    bool same = w2.s.tenant[ti].ndesk == t->ndesk;
    for (int d = 0; same && d < w2.s.tenant[ti].ndesk; d++)
        same = w2.s.dev[w2.s.tenant[ti].desk0 + d].room ==
               w.s.dev[t->desk0 + d].room;
    ck("the same seed puts the same desk in the same room, every time", same);
    site_free(&w2.s);
    site_free(s);
}

/* ==================================== ONE FACT, ONE PLACE: THE REPORTS AGREE
 *
 * This file's recurring defect, five times over in one day: a number computed
 * in two places, and the wrong one shipped. These are the three a playtest
 * that reached day 18 found, each pinned by the arithmetic rather than by the
 * words, so that the two halves cannot drift apart again.
 */

/* ------------------------------------------------- A. the dhcp diagnostic
 * `service` said "nothing is serving dhcp on their segment" at a player who
 * had just typed a correct `dhcpd` line, because it read `addressed == 0`
 * and never asked whether a pool existed or whether anybody had asked it for
 * anything. One `day` later all twenty desks held leases from that pool.
 * A desk asks when the busy period runs; the sentence has to know which of
 * those two states it is in. */
static void check_dhcp_diagnosis(const Building *b)
{
    printf("\nthe dhcp diagnostic, against what really happened\n");
    Tower w;
    int comms = comms_on(b, 1, 0);
    tower_up(&w, b, GATE_SEED, comms, CAB_CAT6, true, 1);
    Site *s = &w.s;
    int ti = -1;
    for (int i = 0; i < s->ntenant; i++)
        if (s->tenant[i].floor == 1) { ti = i; break; }
    if (ti < 0) { ck("the gate's tower lets a tenancy on deck 1", false);
                  site_free(s); return; }
    tower_until(&w, ti);
    /* tower_up's router holds a pool over the whole flat /16 these desks
     * land in: it is up, it is correct, and it has served nobody yet. */
    int got = site_serve(s, ti, w.sw[0], CAB_CAT5E);
    char why[240];
    site_tenant_why(s, ti, why, sizeof why);
    ck("cabled, with a live pool on their segment and no day run yet, they "
       "have no addresses", got > 0 && site_tenant_addressed(s, ti) == 0);
    ck("and `service` does NOT accuse the player of not serving dhcp",
       strstr(why, "nothing is serving dhcp") == NULL);
    ck("it says the leases are handed out when the day runs",
       strstr(why, "YET") != NULL && strstr(why, "when the day runs") != NULL);
    printf("    %s\n", why);
    site_day(s, NULL);
    ck("and one day later every one of those desks holds a lease from it",
       site_tenant_addressed(s, ti) == got);
    site_free(s);

    /* AND WHEN IT REALLY IS THE FAULT IT USED TO ALLEGE, it still says so --
     * off the same counter, because the desks asked and nothing answered. */
    tower_up(&w, b, GATE_SEED, comms, CAB_CAT6, true, 1);
    s = &w.s;
    tower_until(&w, ti);
    site_dhcpd_stop(s, w.rt);
    site_serve(s, ti, w.sw[0], CAB_CAT5E);
    site_day(s, NULL);
    site_tenant_why(s, ti, why, sizeof why);
    ck("with no pool anywhere, a day later they have asked and got nothing",
       site_tenant_addressed(s, ti) == 0 &&
       strstr(why, "asked for a lease and got nothing") != NULL);
    printf("    %s\n", why);
    site_free(s);
}

/* ------------------------------------------- B. the headline and the rows
 * `status` said "134/134 transfers finished" over `service` rows summing to
 * 80 + 18 = 98. Both were true: the headline counted every unit of work the
 * TOWER carried, including the two CRM transfers behind every call, and the
 * rows counted what each tenancy is JUDGED on. Nothing anywhere said so, and
 * both used the word "transfers".
 *
 * The assertion is arithmetic, not wording: the headline is the sum of the
 * rows. And it is run on a building where the two totals really do differ,
 * so reverting the fix fails this rather than passing it by luck. */
static void check_headline_sums_the_rows(void)
{
    printf("\nthe headline is the sum of the rows\n");
    Building b;
    if (!bld_generate(&b, 22ull)) { ck("seed 22 makes a building", false); return; }
    int comms = bld_find(&b, 1, RM_COMMS);
    if (comms < 0) comms = a_room(&b, 1);
    Tower w;
    tower_up(&w, &b, 22ull, comms, CAB_CAT5E, true, 3);
    Site *s = &w.s;
    int off = trade_on(s, TEN_OFFICE, 1), voi = trade_on(s, TEN_VOICE, 1);
    if (off < 0 || voi < 0) {
        ck("seed 22 lets an office and a call centre onto deck 1", false);
        site_free(s); bld_free(&b); return;
    }
    tower_until(&w, voi);
    site_serve(s, off, w.sw[0], CAB_CAT5E);
    site_serve(s, voi, w.sw[2], CAB_CAT5E);
    SiteDay r;
    site_day(s, &r);

    int rows_done = 0, rows_tried = 0;
    for (int i = 0; i < s->ntenant; i++) {
        if (!s->tenant[i].moved) continue;
        rows_done  += s->tenant[i].finished;
        rows_tried += s->tenant[i].tried;
    }
    int done = -1, tried = -1;
    const char *unit = NULL;
    site_day_work(s, &done, &tried, &unit);
    ck("site_day_work is the sum of the rows `service` prints",
       done == rows_done && tried == rows_tried && rows_tried > 0);
    ck("a mixed building's work is not called by one trade's unit",
       unit && strcmp(unit, "jobs") == 0);

    /* THE GAP IS REAL ON THIS BUILDING, so this check is not passing by
     * accident: the tower carried more than the tenancies were judged on. */
    ck("and the tower really carried more than that, so the two differ",
       r.sessions > rows_tried && r.finished != rows_done);
    printf("    rows %d/%d judged; the tower carried %d/%d in all\n",
           rows_done, rows_tried, r.finished, r.sessions);

    char want[64];
    Buf o = {0};
    site_dump_day(s, &o);
    snprintf(want, sizeof want, "%d of %d jobs", rows_done, rows_tried);
    ck("`status` prints exactly that, and not the tower's own total",
       has(o.p, want));
    buf_free(&o);

    /* And the `day` line, which is the other place it was printed. */
    Buf a = {0};
    site_advance(s, 1, &a);
    rows_done = rows_tried = 0;
    for (int i = 0; i < s->ntenant; i++) {
        if (!s->tenant[i].moved) continue;
        rows_done  += s->tenant[i].finished;
        rows_tried += s->tenant[i].tried;
    }
    snprintf(want, sizeof want, "%d/%d jobs done", rows_done, rows_tried);
    ck("and so does the line `day` prints", has(a.p, want));
    ck("neither of them calls a call a transfer",
       !has(a.p, "transfers") && rows_tried > 0);
    buf_free(&a);

    /* ONE TRADE IN THE BUILDING GETS ITS OWN WORD, out of the same function
     * `service`'s legend uses, so the two cannot drift either. */
    site_free(s);
    tower_up(&w, &b, 22ull, comms, CAB_CAT5E, true, 3);
    s = &w.s;
    tower_until(&w, off);
    site_serve(s, off, w.sw[0], CAB_CAT5E);
    site_day(s, NULL);
    unit = NULL;
    site_day_work(s, NULL, NULL, &unit);
    if (s->tenant[voi].tried == 0)
        ck("with only offices working, the work is counted in transfers",
           unit && strcmp(unit, site_tenant_kind_unit(TEN_OFFICE, true)) == 0);
    site_free(s);
    bld_free(&b);
}

/* ------------------------------------------- B2. the `worst` column, named
 * The same family, found by the agent on core/session.c and reported through
 * the coordinator: `service` prints `worst` and never said what it measures.
 * It is `ended - began` on a finished TRANSFER; the call loop never touches
 * it. So a call centre showed `worst 780ms` beside `demand`'s "a call dies
 * past 150 ms one way", and the only way the playtester could rule out the
 * contradiction was to sit at a desk and run `voice`, which said 3.0 ms.
 * This reproduces exactly that shape -- a voice tenancy whose calls are well
 * inside the delay budget and whose `worst` is not -- and asserts the page
 * says which is which. */
static void check_worst_is_wall_time(void)
{
    printf("\n`service` says what the worst column measures\n");
    Building b;
    if (!bld_generate(&b, 22ull)) { ck("seed 22 makes a building", false); return; }
    int comms = bld_find(&b, 1, RM_COMMS);
    if (comms < 0) comms = a_room(&b, 1);
    Tower w;
    tower_up(&w, &b, 22ull, comms, CAB_CAT5E, true, 3);
    Site *s = &w.s;
    int voi = trade_on(s, TEN_VOICE, 1);
    if (voi < 0) { ck("seed 22 lets a call centre onto deck 1", false);
                   site_free(s); bld_free(&b); return; }
    tower_until(&w, voi);
    site_serve(s, voi, w.sw[2], CAB_CAT5E);
    site_day(s, NULL);
    const SiteTenant *t = &s->tenant[voi];
    printf("    the call centre: worst %d ms, one-way delay %d ms, %d/%d calls\n",
           t->worst_ms, t->delay_ms, t->finished, t->tried);
    ck("a call centre's worst is bigger than the delay a call is allowed",
       t->worst_ms > SITE_VOICE_DELAY_MS);
    ck("while its calls were well inside it -- so the two do not compare",
       t->delay_ms < SITE_VOICE_DELAY_MS && t->tried > 0);
    Buf o = {0};
    site_cmd(s, "service ?", &o);
    ck("`service ?` says worst is wall time and not delay",
       has(o.p, "worst is WALL TIME and not delay"));
    ck("and that a voice tenancy's worst never comes off a call",
       has(o.p, "never comes off a call"));
    /* AND IT NO LONGER SENDS THEM TO A VERB THIS SHELL HAS NOT GOT. See
     * D43: `sit` and `voice` are Session verbs and the tower shell answers
     * "no such command" to both, so the sentence names `load` -- which is
     * here -- and says where the other two live. */
    ck("and sends them to a verb of THIS shell for the port that dropped it",
       has(o.p, "the port is `load`") &&
       has(o.p, "verbs of the SESSION and not of this shell"));
    buf_free(&o);
    site_free(s);
    bld_free(&b);
}

/* THE POWER MAP GATE IS GONE TOO, and it went with the page it was about.
 * `outlets` drew every room kit can live in and what its wall had; there is
 * no wall. What replaced the page is `conduits`, which draws every run and
 * what each is carrying against what it can, and check_conduits() is what
 * asserts about it. */

static void check_serve_vlan_remedy(const Building *b)
{
    printf("\nsaying `serve` again with the vlan on it is the whole remedy\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 400000);
    int mdf = bld_find(b, 0, RM_MDF);
    int rt = gate_box(&s, SDEV_ROUTER, mdf, "rt");
    site_cable(&s, rt, 0, s.uplink, 0, CAB_CAT6);
    site_addr(&s, rt, 0, s.wan_you, s.wan_mask);
    site_gateway(&s, rt, s.wan_isp);
    site_forwarding(&s, rt, true);
    for (int i = 0; i < 400 && !s.tenant[0].moved; i++) site_day(&s, NULL);
    if (!s.tenant[0].moved) { ck("a tenancy moves in", false); site_free(&s); return; }
    int floor = s.tenant[0].floor;
    int comms = comms_on(b, floor, s.tenant[0].room);

    /* A pool that answers ON ONE VLAN AND NOWHERE ELSE, so an address is
     * proof the port really moved into it. */
    const int V = 31;
    int csw = gate_box(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, rt, 1, csw, 0, CAB_CAT6);
    int fsw = gate_box(&s, SDEV_SWITCH24, comms, "fsw");
    site_cable(&s, csw, 2, fsw, 0, CAB_CAT6);
    site_port_trunk(&s, csw, 2, V);
    site_port_trunk(&s, fsw, 0, V);
    site_subif(&s, rt, 1, V, net_ip(10, 0, 31, 1), net_mask_bits(24));
    site_port_trunk(&s, csw, 0, V);
    site_dhcpd(&s, rt, net_ip(10, 0, 31, 100), 60, net_mask_bits(24),
               net_ip(10, 0, 31, 1), s.wan_isp);

    /* THE MISTAKE, as a player types it. */
    Buf o = {0};
    char line[64];
    snprintf(line, sizeof line, "serve %d fsw", s.tenant[0].tenant);
    site_cmd(&s, line, &o);
    const char *note = o.p ? strstr(o.p, "NOTE:") : NULL;
    const char *bill = o.p ? strstr(o.p, "desks have a port") : NULL;
    ck("`serve` with no vlan warns a +segment tenancy BEFORE the bill",
       s.tenant[0].own_segment && note && bill && note < bill);
    long spent = s.spent;
    int up = site_tenant_connected(&s, 0);
    site_day(&s, NULL);
    ck("and in the untagged default they get nothing off the vlan's pool",
       up > 0 && site_tenant_addressed(&s, 0) == 0);
    buf_free(&o);

    /* THE REMEDY: the same line, with the vlan on the end. One line. */
    Buf o2 = {0};
    snprintf(line, sizeof line, "serve %d fsw %d", s.tenant[0].tenant, V);
    site_cmd(&s, line, &o2);
    ck("saying it again with the vlan lays no copper and costs nothing",
       s.spent == spent && site_tenant_connected(&s, 0) == up);
    site_day(&s, NULL);
    ck("and every desk that was already patched is in the vlan now",
       site_tenant_addressed(&s, 0) == up);
    printf("    %d desks re-vlanned for 0, and all %d hold a lease off the "
           "vlan's pool\n", up, site_tenant_addressed(&s, 0));
    buf_free(&o2);
    site_free(&s);
}

/* WHAT DISCHARGES `+server`. A playtester put ONE server in a floor's comms
 * cupboard on three subinterfaces and all three of that floor's tenancies
 * were served off it and paid -- after spending 1,350 on the assumption that
 * they would not be. `demand` printed `+server` as a requirement and never
 * said what satisfies it, while it DID say the web host's opposite rule,
 * which made the ordinary case read as the unstated exception. The check is
 * that `demand` says it and that the machine really behaves that way. */
static void check_demand_says_what_a_server_is_for(const Building *b)
{
    printf("\n`demand` says what discharges +server, and the tower agrees\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    Buf d = {0};
    site_dump_demand(&s, &d);
    ck("`demand` still prints +server as a want", has(d.p, "+server"));
    ck("and now says a shared server on their deck discharges it",
       has(d.p, "WHAT DISCHARGES `+server`") && has(d.p, "their deck"));
    ck("and that a web host's origin is the exception, in their own room",
       has(d.p, "own room"));
    buf_free(&d);
    site_free(&s);

    /* AND THE BEHAVIOUR IT DESCRIBES IS THE ONE THE DAY REALLY RUNS: one
     * server, in the floor's cupboard, is the file server of every tenancy
     * on that floor that did any work. */
    Building b22;
    if (!bld_generate(&b22, 22ull)) { ck("seed 22 makes a building", false); return; }
    int comms = bld_find(&b22, 1, RM_COMMS);
    if (comms < 0) comms = a_room(&b22, 1);
    Tower w;
    tower_up(&w, &b22, 22ull, comms, CAB_CAT5E, true, 3);
    Site *t = &w.s;
    int off = trade_on(t, TEN_OFFICE, 1), voi = trade_on(t, TEN_VOICE, 1);
    if (off >= 0 && voi >= 0) {
        tower_until(&w, voi);
        /* The days it took them to move in included one of this building's
         * own mains failures, which is what a blackout does to a server
         * nobody put a battery under. Switch it back on: this check is
         * about which tenancies a running server serves. */
        site_power(t, w.srv, true);
        site_addr(t, w.srv, 0, net_ip(10, 0, 0, 9), net_mask_bits(16));
        site_gateway(t, w.srv, net_ip(10, 0, 0, 1));
        site_httpd(t, w.srv, 80);
        site_serve(t, off, w.sw[0], CAB_CAT5E);
        site_serve(t, voi, w.sw[2], CAB_CAT5E);
        site_day(t, NULL);
        int shared = 0;
        for (int i = 0; i < t->ntenant; i++)
            if (t->tenant[i].moved && t->tenant[i].files_dev == w.srv) shared++;
        ck("one server in the deck's cupboard is the file server of every "
           "tenancy on it", shared >= 2 && t->tenant[off].files_dev == w.srv);
        printf("    %d tenancies served off the one box in the cupboard\n", shared);
    } else {
        ck("seed 22 lets two tenancies onto deck 1", false);
    }
    site_free(t);
    bld_free(&b22);
}

/* ================================================ D43. ONE FACT, ONE ANSWER
 *
 * A blind playtester played nine sessions to day 70 and came back with ten
 * things the game says about itself that another command in the same session
 * disproves. That is this project's cardinal sin and its recurring one: a
 * fact computed in two places, and the wrong one shipped.
 *
 * Every check below is written so that it FAILS against HEAD. Where a
 * sentence had to change, the check asserts the sentence; where a NUMBER was
 * wrong, it asserts the number against the other place the game prints it,
 * so the two can never drift apart again without something going red.
 */
/* THE PRICE LIST IS THE COUNTER'S LIST OR IT IS A SECOND ONE.
 *
 * `catalogue` was added because a blind playtester could not price kit before
 * committing to it: "there is no way to see a price before you buy... that
 * undercuts the whole 'the opening is a decision' premise." The one route the
 * game named to its own price list -- `links halbert.co.uk/catalogue` -- did
 * not work, because `links` ignores its argument and prints the cable list.
 *
 * A price list that is typed out is worse than none: it goes stale silently
 * and it lies at the moment a player trusts it most. So this walks every kind
 * the shop sells and holds the printed row against the accessors the counter
 * charges from -- and buys one of each to prove the charged price is the
 * printed one, which is the only version of "the same table" that cannot be
 * argued with. */
static void check_catalogue(const Building *b)
{
    printf("\nthe catalogue against the counter\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 400000);
    Buf o = {0};
    site_cmd(&s, "catalogue", &o);
    const char *page = o.p ? o.p : "";
    int sold = 0, missing = 0, wrong = 0;
    char first[160] = {0};
    for (int k = 0; k < SDEV_KIND_COUNT; k++) {
        if (!site_kind_for_sale(k)) continue;
        sold++;
        const char *row = strstr(page, site_kind_name(k));
        if (!row) {
            missing++;
            if (!first[0])
                snprintf(first, sizeof first, "%s is for sale and not on the page",
                         site_kind_name(k));
            continue;
        }
        char want[64];
        snprintf(want, sizeof want, "%d", site_kind_price(k));
        const char *eol = strchr(row, '\n');
        size_t len = eol ? (size_t)(eol - row) : strlen(row);
        char line[200];
        snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line ? len : sizeof line - 1), row);
        if (!strstr(line, want)) {
            wrong++;
            if (!first[0])
                snprintf(first, sizeof first, "%s costs %d and its row says: %s",
                         site_kind_name(k), site_kind_price(k), line);
        }
    }
    ck("every kind the shop sells is on the catalogue page", missing == 0);
    ck("and the price printed beside it is site_kind_price()'s", wrong == 0);
    if (first[0]) printf("      %s\n", first);
    printf("    %d kinds priced\n", sold);

    /* AND THE COUNTER CHARGES IT. A printed price nobody tested against a
     * purchase is a number in a table. */
    long before = s.money;
    int bad_charge = 0;
    for (int k = 0; k < SDEV_KIND_COUNT; k++) {
        if (!site_kind_for_sale(k)) continue;
        long was = s.money;
        char cmd[64];
        snprintf(cmd, sizeof cmd, "order %s", site_kind_name(k));
        buf_clear(&o);
        site_cmd(&s, cmd, &o);
        if (was - s.money != site_kind_price(k)) bad_charge++;
    }
    ck("and one of each really costs what the page said", bad_charge == 0);
    printf("    one of everything: %ld\n", before - s.money);

    /* NOTHING WAS BOUGHT BY READING IT. */
    Site s2;
    site_new(&s2, b, GATE_SEED, 100000);
    long m0 = s2.money;
    int nd0 = s2.ndev;
    buf_clear(&o);
    site_cmd(&s2, "catalogue", &o);
    ck("and reading the catalogue costs nothing and installs nothing",
       s2.money == m0 && s2.ndev == nd0);
    site_free(&s2);

    buf_free(&o);
    site_free(&s);
}

/* ==================================================== THE CONDUIT TREE
 *
 * The owner's redirect, first piece: "making you have to run power conduits
 * to certain places just like you run ethernet... a power strip that allows
 * you to take a conduit and plug in multiple devices to the end of it.
 * Including other conduits so that you can fork a conduit... when you hover
 * over a conduit, it'll tell you its percent of utilisation. So you have to
 * run fresh conduits from the power core once they've hit a maximum load."
 *
 * Four things have to be true or it is decoration: the metres cost what
 * copper costs over the same ground, a run knows what is behind it, a fork
 * adds up, and a run over its rating takes everything behind it down. The
 * fourth is the one the whole station design rests on.
 */
/* AND THIS ONE BUILDS ITS OWN POWER, which is the point of it. Everywhere
 * else in this file a box arrives fed, because those gates are measuring the
 * network and gate_box() takes the ceremony away. Here the ceremony IS the
 * subject: a run has to be pulled, and what happens when it is not pulled --
 * or is pulled and overloaded -- is what is being asserted. So these are
 * site_install() and nothing feeds them but the lines below. */
static void check_conduits(const Building *b)
{
    printf("\nconduit: a tree from the core, and what it carries\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);

    int core = site_dev_by_name(&s, "core0");
    ck("the building came with a power core, in the plant room",
       core >= 0 && s.dev[core].kind == SDEV_POWERCORE &&
       s.b->rooms[s.dev[core].room].kind == RM_PLANT);
    ck("and it is not for sale, for the same reason the handoff is not",
       !site_kind_for_sale(SDEV_POWERCORE) && site_kind_for_sale(SDEV_STRIP));

    /* --- THE METRES ARE COPPER'S METRES. Same two rooms, same graph, same
     * price per metre: a conduit is not a second way of charging distance. */
    int room = a_room(b, 2);
    int sw = site_install(&s, SDEV_SWITCH24, room, "sw");
    /* OUTPUT 0 IS SPOKEN FOR: site_new() puts the run the building came
     * with on it -- the power half of the patch lead in the handoff, feeding
     * the workstation -- so a gate that wants a free output asks for one
     * rather than assuming the first. */
    int run = site_conduit(&s, core, 1, sw);
    int want_m = site_run_metres(&s, s.dev[core].room, s.dev[sw].room);
    ck("a run is priced by the metre off the building's own cable graph",
       run >= 0 && s.cond[run].metres == want_m && want_m > 0 &&
       s.cond[run].cost == site_cable_price(CAB_CAT6, want_m));
    printf("    core to the cupboard: %d m, %d paid\n",
           s.cond[run].metres, s.cond[run].cost);

    /* --- IT KNOWS WHAT IS BEHIND IT. */
    ck("and it carries what the thing on the end of it draws",
       site_conduit_load(&s, run) == site_kind_watts(SDEV_SWITCH24));

    /* --- ONE PLUG PER SOCKET, ONE LEAD PER BOX. Both ends of the same rule. */
    int again = site_conduit(&s, core, 1, sw);
    ck("an output that already has a run in it refuses a second",
       again < 0 && s.err == SITE_EBUSY);
    int sw2 = site_install(&s, SDEV_SWITCH8, room, "sw2");
    (void)sw2;
    int twice = site_conduit(&s, core, 2, sw);
    ck("and a box that is already fed refuses a second lead",
       twice < 0 && s.err == SITE_EBUSY);

    /* --- THE FORK. A strip takes one run in and gives several out, and an
     * output takes a load or another strip. */
    int st = site_install(&s, SDEV_STRIP, room, "st");
    int feed = site_conduit(&s, core, 2, st);
    int a = site_install(&s, SDEV_RACKSERVER, room, "a");
    int c = site_install(&s, SDEV_RACKSERVER, room, "c");
    int ra = site_conduit(&s, st, 1, a);
    int rc = site_conduit(&s, st, 2, c);
    ck("a strip forks a run: one in, several out",
       feed >= 0 && ra >= 0 && rc >= 0);
    ck("and the run feeding it carries everything behind it, added up",
       site_conduit_load(&s, feed) == 2 * site_kind_watts(SDEV_RACKSERVER));
    printf("    two rack servers behind one strip: %d W on the feed, %d%%\n",
           site_conduit_load(&s, feed), site_conduit_pct(&s, feed));
    /* the strip's input is not an output: you cannot run out of the way in */
    int backwards = site_conduit(&s, st, 0, sw2);
    ck("a strip's input is the way in and not another way out",
       backwards < 0 && s.err == SITE_EIFACE);

    /* --- AND THE TRIP, WHICH IS THE POINT. Everything up to the rating is
     * fed; the load that takes it over darkens everything behind that run,
     * not just itself. */
    int trip_run = -1;
    ck("everything on a run inside its rating is fed",
       site_dev_fed(&s, a, &trip_run) && site_dev_fed(&s, c, &trip_run) &&
       trip_run < 0);
    int e = site_install(&s, SDEV_RACKSERVER, room, "e");
    int re = site_conduit(&s, st, 3, e);
    ck("a third rack server takes that feed over what it carries",
       re >= 0 && site_conduit_pct(&s, feed) > 100);
    printf("    a third one: %d W on the feed, %d%%\n",
           site_conduit_load(&s, feed), site_conduit_pct(&s, feed));
    bool a_dark = !site_dev_fed(&s, a, &trip_run);
    int t2 = -1;
    bool e_dark = !site_dev_fed(&s, e, &t2);
    ck("and everything behind it goes dark, not just the one that tipped it",
       a_dark && e_dark && trip_run == feed && t2 == feed);
    ck("while a box on a different run off the core is untouched",
       site_dev_fed(&s, sw, &t2) && t2 < 0);

    /* --- AND TAKING SOMETHING OFF IT BRINGS THE REST BACK. That is the
     * repair, and it is the same move as running another from the core. */
    site_unconduit(&s, re);
    ck("take the third one off and the other two light again",
       site_dev_fed(&s, a, &t2) && site_dev_fed(&s, c, &t2) &&
       site_conduit_pct(&s, feed) <= 100);

    /* --- A BOX NOTHING FEEDS IS DARK, which is the day-one state of
     * everything in the building. */
    int lonely = site_install(&s, SDEV_SERVER, room, "lonely");
    ck("a box with no conduit to it is dark, and the core itself is not",
       !site_dev_fed(&s, lonely, &t2) && site_dev_fed(&s, core, &t2));

    /* --- AND RUNNING ONE WITHOUT PICKING THE END. "For the AI placing lines
     * we will need logic to automatically use the cable trays to connect
     * things." The metres were always the trays' -- site_run_metres() is
     * bld_cable_all() -- so what `feed` adds is the CHOICE a client driving
     * this over a socket cannot make: which output of which source is the
     * shortest honest run. It has to pick the nearest and charge the same. */
    {
        Site f;
        site_new(&f, b, GATE_SEED, 100000);
        int fcore = site_dev_by_name(&f, "core0");
        int near_room = a_room(b, 0), far_room = a_room(b, 5);
        int near_box = site_install(&f, SDEV_SWITCH8, near_room, "nb");
        /* a strip out at the far end, fed, so it is a candidate source */
        int fst = site_install(&f, SDEV_STRIP, far_room, "fst");
        site_conduit(&f, fcore, 1, fst);
        int far_box = site_install(&f, SDEV_SWITCH8, far_room, "fb");
        int rn = site_feed(&f, near_box);
        int rf = site_feed(&f, far_box);
        ck("`feed` runs conduit without being told which end to take",
           rn >= 0 && rf >= 0);
        /* the near box should come off the core, the far one off the strip
         * that is standing in its own room -- nearest, not first */
        ck("and it picks the nearest source along the trays, not the first one",
           f.cond[rn].from == fcore && f.cond[rf].from == fst);
        printf("    near box: %d m off %s.  far box: %d m off %s\n",
               f.cond[rn].metres, f.dev[f.cond[rn].from].name,
               f.cond[rf].metres, f.dev[f.cond[rf].from].name);
        /* the same metres and the same price as choosing by hand */
        ck("and the metres are the ones the cable graph gives for that pair",
           f.cond[rf].metres == site_run_metres(&f, f.dev[fst].room, f.dev[far_box].room) &&
           f.cond[rf].cost == site_cable_price(CAB_CAT6, f.cond[rf].metres));
        /* a strip nothing feeds is not a source: a limb dark from the moment
         * it is built is metres nobody should have been charged for */
        int orphan = site_install(&f, SDEV_STRIP, near_room, "orphan");
        int ob = site_install(&f, SDEV_SWITCH8, near_room, "ob");
        int ro = site_feed(&f, ob);
        ck("and it will not run off a strip that nothing feeds",
           ro >= 0 && f.cond[ro].from != orphan);
        /* and when every hole really is used it says so rather than picking
         * a bad one */
        int guard = 0;
        while (guard++ < 40) {
            char nm[NET_NAME_MAX];
            snprintf(nm, sizeof nm, "x%d", guard);
            int d2 = site_install(&f, SDEV_SWITCH4, near_room, nm);
            if (d2 < 0 || site_feed(&f, d2) < 0) break;
        }
        ck("and when every output in the building is in use it says which "
           "thing to buy",
           f.err == SITE_ENODEV && guard < 40);
        printf("    the core and one strip fed %d boxes before running out\n",
               site_conduit_count(&f) - 1);
        site_free(&f);
    }

    Buf o = {0};
    site_cmd(&s, "conduits", &o);
    ck("and `conduits` prints every run with what is on it",
       has(o.p, "core0:0") && has(o.p, "%") && has(o.p, "1500 W"));
    buf_free(&o);
    site_free(&s);
}

static void check_one_fact_two_answers(const Building *b)
{
    printf("\nD43: the reports the playtest caught contradicting themselves\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 200000);
    Buf o = {0};

    /* --- 1. A SWITCH THAT IS FULL AND EMPTY AT THE SAME TIME.
     * `show` counted ports whose netstack state was not NOCABLE, and an
     * unpowered switch has every port administratively down: a switch24
     * fresh off the pallet read `24/24 ports used` on the summary page,
     * `show core` under it printed twenty-four empty ports, and the number
     * did not move as the player cabled. */
    site_cmd(&s, "order switch24 core", &o);
    int core = site_dev_by_name(&s, "core");
    buf_clear(&o);
    site_cmd(&s, "show", &o);
    ck("a switch24 nobody has cabled reads 0/24 on the summary page",
       has(o.p, "0/24 ports used") && !has(o.p, "24/24 ports used"));
    ck("and it is the same count `serve` and `cable` walk",
       site_ports_used(&s, core) == 0 && site_ports_spare(&s, core) == 24);
    /* Carry it in, power it, put two leads in it, and BOTH pages move
     * together. This is the half a difficulty constant cannot fake: the
     * number is read off the link table either way. */
    site_move(&s, core, bld_find(b, 0, RM_MDF));
    site_mains(&s, core, true);
    site_cable(&s, core, 0, s.uplink, 0, CAB_CAT6);
    site_cmd(&s, "order pc pc9", &o);
    int pc = site_dev_by_name(&s, "pc9");
    site_move(&s, pc, bld_find(b, 0, RM_MDF));
    site_cable(&s, core, 1, pc, 0, CAB_CAT6);
    buf_clear(&o);
    site_cmd(&s, "show", &o);
    ck("two leads in it and the summary says 2/24, having moved as it was cabled",
       has(o.p, "2/24 ports used"));
    {
        Buf d = {0};
        site_cmd(&s, "show core", &d);
        /* The detail page and the summary are now one fact: the summary's
         * numerator is the ports the detail prints as up, and its remainder
         * is the sockets the detail says have nothing in them. */
        ck("and `show core` agrees: 24 sockets, 22 free for a lead",
           has(d.p, "24 sockets, numbered 0 to 23, 22 free for a lead") &&
           has(d.p, "22 more sockets on the back of it"));
        buf_free(&d);
    }

    /* --- 2. A SOCKET THAT IS ADVERTISED AND DOES NOT EXIST.
     * "1 socket" over "1 more socket on the back of it, with nothing in it"
     * reads as two, and cost a playtester a session trying to hang a router
     * off `uplink:1`. The header now says what the sockets are NUMBERED, so
     * the question they were really asking is answered on the page. */
    buf_clear(&o);
    site_cmd(&s, "show uplink", &o);
    ck("the handoff says it has one socket and what that socket is numbered",
       has(o.p, "1 socket, numbered 0 to 0"));
    ck("and there is no port 1 to cable to, exactly as the header now says",
       site_cable(&s, s.uplink, 1, pc, 0, CAB_CAT6) < 0 &&
       s.err == SITE_ENOPORT);
    /* AND THE FREE COUNT IS THE ONE A LEAD CAN GO INTO. A port punched down
     * to a jack is a pair terminated on a panel: `cable` and `serve` step
     * over it, so the header must too. */
    {
        int comms = bld_find(b, 1, RM_COMMS);
        int j = site_jack(&s, comms, core, 5, CAB_CAT5E);
        buf_clear(&o);
        site_cmd(&s, "show core", &o);
        ck("a port held for good by a jack is not counted as free for a lead",
           j >= 0 && has(o.p, "24 sockets, numbered 0 to 23, 21 free for a lead") &&
           site_ports_spare(&s, core) == 21);
    }

    /* --- 3. AN ERROR ABOUT SUBNETS FROM A COMMAND THAT TAKES MEGABITS. */
    buf_clear(&o);
    site_cmd(&s, "isp 0", &o);
    ck("`isp 0` is refused in megabits, not in network addresses",
       has(o.p, "MEGABITS") && !has(o.p, "broadcast address"));
    buf_clear(&o);
    site_cmd(&s, "isp -5", &o);
    ck("and so is `isp -5`", has(o.p, "MEGABITS") && !has(o.p, "subnet"));
    ck("while a real address error still says what it always said",
       has(site_err_text(SITE_EADDR), "broadcast address"));

    /* --- 4. `vlan` ACCEPTED 99999 WHILE `trunk` REFUSED 4095. One rule,
     * two answers, and the permissive one was the one that touched the
     * switch. */
    buf_clear(&o);
    site_cmd(&s, "vlan core 0 99999", &o);
    ck("`vlan core 0 99999` is refused in the same words `trunk` refuses 4095",
       has(o.p, "a vlan is a number from 1 to 4094") && !has(o.p, "set"));
    {
        /* The FULL port page, because `show <box>` hides an empty access
         * port and the two ports this pair is about have nothing in them. */
        Buf d = {0};
        site_dump_dev(&s, core, &d);
        ck("and the port was not changed behind the refusal",
           !has(d.p, "access vlan 99999"));
        buf_free(&d);
    }
    buf_clear(&o);
    site_cmd(&s, "trunk core 0 4095", &o);
    ck("`trunk core 0 4095` still refuses, so the two verbs agree",
       has(o.p, "a vlan is a number from 1 to 4094"));
    buf_clear(&o);
    site_cmd(&s, "vlan core 2 11", &o);
    {
        Buf d = {0};
        site_dump_dev(&s, core, &d);
        ck("and a vlan inside the range is still set, on the port itself",
           has(o.p, "set") && has(d.p, "access vlan 11"));
        buf_free(&d);
    }

    /* --- 6. `rooms f2` SILENTLY PRINTED FLOOR 0. atoi("f2") is 0, and
     * every other verb in the game takes `f2.something`. */
    buf_clear(&o);
    site_cmd(&s, "rooms 2", &o);
    bool two = has(o.p, "deck 2");
    buf_clear(&o);
    site_cmd(&s, "rooms d2", &o);
    ck("`rooms d2` and `rooms 2` are the same deck", two && has(o.p, "deck 2"));
    ck("and it is not deck 0 wearing deck 2's name", !has(o.p, "deck 0"));
    buf_clear(&o);
    site_cmd(&s, "rooms d99", &o);
    ck("a deck that does not exist is refused, not rounded to the ground deck",
       has(o.p, "there is no deck 99") && !has(o.p, "deck 0\n"));
    buf_clear(&o);
    site_cmd(&s, "rooms d2.comms", &o);
    ck("and a room name is refused here and sent to the verbs that take one",
       has(o.p, "is not a deck") && !has(o.p, "deck 0\n"));

    /* --- 7. THE DHCP POOL CAP, DELIVERED BY THE WRONG HALF OF ITS OWN
     * MESSAGE. A pool of 180 addresses was refused with a sentence that led
     * with "a pool of no addresses serves nobody". */
    {
        int rt = gate_box(&s, SDEV_ROUTER, bld_find(b, 0, RM_MDF), "rt9");
        site_cable(&s, rt, 0, core, 3, CAB_CAT6);
        site_addr(&s, rt, 0, net_ip(10, 9, 0, 1), net_mask_bits(24));
        for (int v = 0; v < 8; v++)
            site_subif(&s, rt, 0, 100 + v, net_ip(10, 20 + v, 0, 1),
                       net_mask_bits(24));
        int made = 0;
        for (int v = 0; v < 8 && made < 8; v++)
            if (site_dhcpd(&s, rt, net_ip(10, 20 + v, 0, 50), 180,
                           net_mask_bits(24), net_ip(10, 20 + v, 0, 1),
                           net_ip(10, 20 + v, 0, 1))) made++;
        buf_clear(&o);
        site_cmd(&s, "dhcpd rt9 10.9.0.50 180 24 10.9.0.1 10.9.0.1", &o);
        ck("eight pools go on one box and the ninth is refused",
           made == 8 && has(o.p, "already holds eight pools"));
        ck("and the refusal does not lead with a reason the line disproves",
           !has(o.p, "pool of no addresses"));
        buf_clear(&o);
        site_cmd(&s, "dhcpd rt9 10.9.0.50 0 24 10.9.0.1 10.9.0.1", &o);
        ck("a pool of no addresses is its own error, saying only that",
           has(o.p, "pool of no addresses") && !has(o.p, "eight pools"));
        buf_clear(&o);
        site_cmd(&s, "dhcpd", &o);
        ck("and the cap is in the help text `dhcpd` on its own prints",
           has(o.p, "EIGHT") || has(o.p, "eight"));
        buf_clear(&o);
        site_cmd(&s, "help", &o);
        ck("and on the help page, where the verb is documented",
           has(o.p, "EIGHT POOLS"));
    }

    /* --- 10. THE LEGEND IS SHORTER AND ALL OF IT IS STILL THERE. Measured,
     * not asserted: the short page against the legend, in lines. */
    {
        Buf sh = {0}, lg = {0};
        site_cmd(&s, "service", &sh);
        site_cmd(&s, "service ?", &lg);
        int shl = 0, lgl = 0;
        for (const char *p = sh.p; p && *p; p++) if (*p == '\n') shl++;
        for (const char *p = lg.p; p && *p; p++) if (*p == '\n') lgl++;
        printf("    `service` %d lines, `service ?` %d lines of legend\n",
               shl, lgl);
        ck("`service` no longer prints its whole legend under every reading",
           shl < 8 && !has(sh.p, "up is desks whose port has LINK on it"));
        ck("and every sentence of it is still reachable, in one word",
           lgl > 25 &&
           has(lg.p, "up is desks whose port has LINK on it") &&
           has(lg.p, "only an addressed desk does any work") &&
           has(lg.p, "done counts transfers for an office") &&
           has(lg.p, "nineteen in twenty") &&
           has(lg.p, "worst is WALL TIME and not delay") &&
           has(lg.p, "files is the server their people actually pulled off") &&
           has(lg.p, "ANY address it holds") &&
           has(lg.p, "<- is a tenancy being served from another deck"));
        ck("and the short page still prints the number that ends the run",
           has(sh.p, "filed complaints ends the run") &&
           has(sh.p, "service ?"));
        buf_free(&sh); buf_free(&lg);
    }
    {
        Buf sh = {0}, lg = {0};
        site_cmd(&s, "load", &sh);
        site_cmd(&s, "load ?", &lg);
        int shl = 0;
        for (const char *p = sh.p; p && *p; p++) if (*p == '\n') shl++;
        printf("    `load` %d lines\n", shl);
        ck("`load` keeps the one instruction and moves the arithmetic",
           has(sh.p, "READ THE DROPS AND THE PEAK QUEUE") &&
           !has(sh.p, "48 KB buffer is 394us") && has(sh.p, "load ?"));
        ck("and `load ?` has all of it",
           has(lg.p, "48 KB buffer is 394us") &&
           has(lg.p, "busy is the SHARE OF THE BUSY PERIOD") &&
           has(lg.p, "since it was cabled"));
        buf_free(&sh); buf_free(&lg);
    }
    buf_free(&o);
    site_free(&s);
}

/* --- 5, 8 and 9, which need a tenancy in the diary and a day on the clock. */
static void check_ambiguity_and_the_diary(void)
{
    printf("\nD43: a room the game chose, a tenancy with a date, a verb "
           "that exists\n");
    Building b;
    if (!bld_generate(&b, 22ull)) { ck("seed 22 makes a building", false); return; }
    Site s;
    site_new(&s, &b, 22ull, 200000);
    Buf o = {0};

    /* --- 5. `move` SILENTLY PICKED A ROOM. `help` offers `f2.office` and a
     * let floor has a dozen of them belonging to three tenants; the box
     * went into one of them and nothing said a choice had been made. The
     * shorthand stays -- it is how the tower gets built without a floor
     * plan -- and it now says when it was a choice. */
    int first = -1;
    int n2 = site_room_name_matches(&s, "d2.office", &first);
    printf("    d2.office matches %d rooms; d1.comms matches %d\n", n2,
           site_room_name_matches(&s, "d1.comms", NULL));
    ck("deck 2 really has more than one office for the shorthand to pick from",
       n2 > 1 && first >= 0);
    ck("and an unambiguous name still matches exactly one",
       site_room_name_matches(&s, "d1.comms", NULL) == 1 &&
       site_room_name_matches(&s, "d0.eng", NULL) == 1);
    site_cmd(&s, "order pc pcx", &o);
    buf_clear(&o);
    site_cmd(&s, "move pcx d2.office", &o);
    char want[64];
    snprintf(want, sizeof want, "picked one: #%d", first);
    ck("`move pcx d2.office` says which room it picked, and why that one",
       has(o.p, "matches") && has(o.p, want) && has(o.p, "lowest-numbered"));
    ck("and names the spelling that would not have been a guess",
       has(o.p, "`#<n>` names one for certain") && has(o.p, "rooms 2"));
    ck("and the box really is in the room the note named",
       s.dev[site_dev_by_name(&s, "pcx")].room == first);
    /* THE SHORTHAND IS NOT REMOVED, and an unambiguous one says nothing at
     * all -- a note on every `move f1.comms` would be the other failure. */
    buf_clear(&o);
    site_cmd(&s, "move pcx d1.comms", &o);
    ck("an unambiguous room name moves the box and prints no note",
       !has(o.p, "matches") && !has(o.p, "picked one") &&
       s.dev[site_dev_by_name(&s, "pcx")].room == bld_find(&b, 1, RM_COMMS));

    /* --- 8. `serve` BEFORE MOVE-IN DAY SAID THE WRONG THING. A tenancy
     * that will NEVER exist got a sentence naming the right verb; a tenancy
     * that arrives next week got "no such device", about a line with no
     * missing device in it. */
    int later = -1;
    for (int i = 0; i < s.ntenant; i++)
        if (!s.tenant[i].moved) { later = i; break; }
    if (later < 0) { ck("seed 22 has a tenancy still to come", false); }
    else {
        int sw = gate_box(&s, SDEV_SWITCH8, bld_find(&b, 0, RM_MDF), "sw9");
        (void)sw;
        char line[64];
        snprintf(line, sizeof line, "serve %d sw9 cat5e 30", s.tenant[later].tenant);
        buf_clear(&o);
        site_cmd(&s, line, &o);
        printf("    tenancy %d arrives on day %d; it is day %d\n",
               s.tenant[later].tenant, s.tenant[later].day, s.day);
        ck("`serve` on a tenancy that has not moved in says so, and names the day",
           has(o.p, "has not moved in yet") && has(o.p, "day"));
        ck("and does not claim a device is missing from a line that has one",
           !has(o.p, "no such device"));
        char day[32];
        snprintf(day, sizeof day, "day %d", s.tenant[later].day);
        ck("and the day it names is the day the lease really starts",
           has(o.p, day));
        /* The API underneath says the same thing rather than ENODEV, so
         * anything else that calls it gets the truth too. */
        ck("and the call underneath refuses with a tenancy error, not a device one",
           site_serve_vlan(&s, later, sw, CAB_CAT5E, 0) < 0 &&
           s.err == SITE_ENOTIN);
        /* AND THE ONE THAT WAS ALREADY RIGHT IS STILL RIGHT. */
        buf_clear(&o);
        site_cmd(&s, "serve 99 sw9", &o);
        ck("a tenancy that will never exist still gets its own sentence",
           has(o.p, "no tenancy 99"));
    }

    /* --- 9. THE GAME ADVERTISED TWO COMMANDS THAT DO NOT EXIST, for the
     * hardest problem in it. `sit` and `voice` are Session verbs; the tower
     * shell answers "no such command" to both, and `service` told the
     * player to type them. */
    {
        Buf sh = {0};
        ck("`sit` really is not a verb of this shell", !site_cmd(&s, "sit t3d0", &sh));
        buf_clear(&sh);
        ck("and neither is `voice`", !site_cmd(&s, "voice", &sh));
        buf_clear(&sh);
        site_cmd(&s, "service ?", &sh);
        ck("so `service ?` sends the player to `load`, which IS a verb here",
           has(sh.p, "the port is `load`"));
        ck("and says plainly where `sit` and `voice` live instead",
           has(sh.p, "verbs of the SESSION and not of this shell"));
        ck("and `load` is in the verb table it just named",
           site_cmd(&s, "load", &sh));
        buf_free(&sh);
    }
    buf_free(&o);
    site_free(&s);
    bld_free(&b);
}

/* ============================================ D43. THREE GRADES OF THE KIT
 *
 * The same playtest, on the shape of the game rather than on its reports:
 * *"Days 1-20 are too easy and slightly boring... there is no pressure at all
 * in the first three tenancies; the build is mechanical and there is no
 * decision in it."* There was one switch worth buying and one server worth
 * buying, and enough money to buy either without thinking about it.
 *
 * The thing these checks have to prove is not that three rows exist in a
 * table. It is that THE CHEAP ONE FAILS FOR A REASON THE INSTRUMENTS PRINT:
 * two towers, the same building, the same tenancy, the same desks, the same
 * day's work offered, differing in one purchase, and the cheap one drops
 * frames on a port whose buffer really overran. Nothing anywhere multiplies
 * anything by a grade, and if this check ever passes because somebody added
 * a difficulty constant it will be because they deleted the measurement.
 */
typedef struct { Site s; int rt, core, fsw, srv; } Grade;

/* One floor, one riser, one file server on the core, and the floor switch is
 * the parameter. Every line is a line a player types. */
static void grade_up(Grade *g, const Building *b, uint64_t seed, int comms,
                     int swkind)
{
    Site *s = &g->s;
    site_new(s, b, seed, 100000);
    site_credit(s, 900000);
    int mdf = bld_find(b, 0, RM_MDF);
    g->rt   = gate_box(s, SDEV_ROUTER, mdf, "edge");
    g->core = gate_box(s, SDEV_SWITCH24, mdf, "core");
    site_cable(s, g->rt, 0, s->uplink, 0, CAB_CAT6);
    site_cable(s, g->rt, 1, g->core, 0, CAB_CAT6);
    site_addr(s, g->rt, 0, s->wan_you, s->wan_mask);
    site_addr(s, g->rt, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(s, g->rt, s->wan_isp);
    site_forwarding(s, g->rt, true);
    site_dhcpd(s, g->rt, net_ip(10, 0, 1, 1), 250, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s->wan_isp);
    g->fsw = gate_box(s, swkind, comms, "fsw");
    site_cable(s, g->core, 1, g->fsw, 0, CAB_CAT6);
    g->srv = gate_box(s, SDEV_SERVER, comms, "files");
    site_power(s, g->srv, true);
    site_cable(s, g->core, 22, g->srv, 0, CAB_CAT6);
    site_addr(s, g->srv, 0, net_ip(10, 0, 0, 9), net_mask_bits(16));
    site_gateway(s, g->srv, net_ip(10, 0, 0, 1));
    site_httpd(s, g->srv, 80);
}

static void grade_until(Site *s, int ti)
{
    for (int d = 0; d < 200 && !s->tenant[ti].moved; d++) {
        s->over = 0; s->complaints = 0;
        for (int i = 0; i < s->ntenant; i++) {
            s->tenant[i].strikes = 0; s->tenant[i].complained = 0;
        }
        site_day(s, NULL);
    }
}

static void check_grades(void)
{
    printf("\nD43: three grades of switch and three of server, "
           "and the difference is measured\n");

    /* --- THE CATALOGUE. Three of each, priced in order, and the SPEC is
     * what differs -- sockets, what a socket clocks, what the disk is rated
     * for, whether a battery is in it. */
    ck("there are three grades of switch, cheapest first",
       site_kind_price(SDEV_SWITCH4) < site_kind_price(SDEV_SWITCH8) &&
       site_kind_price(SDEV_SWITCH8) < site_kind_price(SDEV_SWITCH24) &&
       site_kind_is_switch(SDEV_SWITCH4) && site_kind_for_sale(SDEV_SWITCH4));
    ck("and they differ in sockets and in what a socket clocks",
       site_kind_ports(SDEV_SWITCH4) == 4 &&
       site_kind_ports(SDEV_SWITCH8) == 8 &&
       site_kind_ports(SDEV_SWITCH24) == 24 &&
       site_kind_port_mb(SDEV_SWITCH4, 0) == 100 &&
       site_kind_port_mb(SDEV_SWITCH8, 0) == 1000 &&
       site_kind_port_mb(SDEV_SWITCH24, 23) == 10000);
    ck("there are three grades of server, cheapest first",
       site_kind_price(SDEV_MINITOWER) < site_kind_price(SDEV_SERVER) &&
       site_kind_price(SDEV_SERVER) < site_kind_price(SDEV_RACKSERVER) &&
       site_kind_is_server(SDEV_MINITOWER) &&
       site_kind_is_server(SDEV_RACKSERVER));
    ck("and they differ in card, in disk life and in whether a battery is in it",
       site_kind_port_mb(SDEV_MINITOWER, 0) == 100 &&
       site_kind_port_mb(SDEV_SERVER, 0) == 1000 &&
       site_kind_port_mb(SDEV_RACKSERVER, 0) == 10000 &&
       site_kind_disk_days(SDEV_MINITOWER) == 30 &&
       site_kind_disk_days(SDEV_SERVER) == 60 &&
       site_kind_disk_days(SDEV_RACKSERVER) == 120 &&
       !site_kind_has_ups(SDEV_SERVER) && site_kind_has_ups(SDEV_RACKSERVER));
    /* AND THE MONEY GOES THE RIGHT WAY. A grade that is dearer per socket
     * AND slower per socket would be a trap rather than a decision. */
    ck("the dear switch is dearer per socket and faster per socket, both",
       site_kind_price(SDEV_SWITCH24) / site_kind_ports(SDEV_SWITCH24) >
       0 && site_kind_price(SDEV_SWITCH4) / site_kind_ports(SDEV_SWITCH4) <
       site_kind_price(SDEV_SWITCH8) / site_kind_ports(SDEV_SWITCH8));

    /* AND THE SPEC IS SAID WHERE THE MONEY LEAVES. A grade a player has to
     * go and look up after buying it is not a decision they made. */
    {
        Building gb;
        if (bld_generate(&gb, GATE_SEED)) {
            Site g; site_new(&g, &gb, GATE_SEED, 100000);
            Buf o = {0};
            site_cmd(&g, "order rackserver r", &o);
            ck("`order` says the card, the disk rating and the battery, "
               "as it charges",
               has(o.p, "2 ports at 10000 Mb") &&
               has(o.p, "disk rated for 120 days") &&
               has(o.p, "battery in it") && has(o.p, "3400 paid"));
            buf_clear(&o);
            site_cmd(&g, "order switch4 s", &o);
            ck("and for the cheap end it says the hundred megabits it is",
               has(o.p, "4 ports at 100 Mb") && has(o.p, "45 paid"));
            buf_clear(&o);
            site_cmd(&g, "order switch24 c", &o);
            ck("and it still names the SFP+ pair on the box that has one",
               has(o.p, "top 2 at 10000 Mb"));
            buf_free(&o); site_free(&g); bld_free(&gb);
        }
    }

    /* --- THE MEASUREMENT. Two towers, one purchase apart. */
    Building b;
    if (!bld_generate(&b, 22ull)) { ck("seed 22 makes a building", false); return; }
    int comms = bld_find(&b, 1, RM_COMMS);
    if (comms < 0) comms = a_room(&b, 1);
    Grade cheap, dear;
    grade_up(&cheap, &b, 22ull, comms, SDEV_SWITCH4);
    grade_up(&dear,  &b, 22ull, comms, SDEV_SWITCH8);
    int ti = trade_on(&cheap.s, TEN_OFFICE, 1);
    if (ti < 0) {
        ck("seed 22 lets an office onto deck 1", false);
        site_free(&cheap.s); site_free(&dear.s); bld_free(&b); return;
    }
    grade_until(&cheap.s, ti);
    grade_until(&dear.s, ti);
    /* THE FLOOR THAT FILLED UP, WHICH IS THE BUILD THE README SAYS A PLAYER
     * REALLY MAKES: a second switch daisy-chained off the first when the
     * floor outgrows it. Six desks hang off an eight-port switch, and every
     * frame of theirs crosses the ONE riser port that is the difference
     * between these two towers. Identical desks, identical work, identical
     * everything except which box the riser lands in -- or this measures
     * nothing. */
    int ndesk = 7;
    {
        Grade *g[2]; g[0] = &cheap; g[1] = &dear;
        for (int k = 0; k < 2; k++) {
            Site *s = &g[k]->s;
            int leaf = gate_box(s, SDEV_SWITCH8, comms, "leaf");
            site_cable(s, g[k]->fsw, 1, leaf, 0, CAB_CAT6);
            for (int i = 0; i < ndesk; i++)
                site_cable(s, leaf, 1 + i, s->tenant[ti].desk0 + i, 0, CAB_CAT6);
        }
    }
    site_day(&cheap.s, NULL);
    site_day(&dear.s, NULL);
    int cmb = net_port_speed(cheap.s.net, cheap.s.dev[cheap.fsw].node, 0);
    int dmb = net_port_speed(dear.s.net,  dear.s.dev[dear.fsw].node, 0);
    /* THE DROPS ARE ON THE OTHER END OF THE RISER, and that is not a
     * detail -- it is the model. A frame is thrown away by the port that
     * cannot clock it out, and the frames coming DOWN to this floor are
     * clocked out by the core's port into a link the cheap box negotiated
     * down to a hundred megabits. `load` names it and `show core` gives the
     * reason; the player who bought the cheap switch has to follow the
     * cable, which is the same thing the README says about the whole game. */
    uint64_t cdrop = net_port_drops(cheap.s.net, cheap.s.dev[cheap.core].node, 1)
                   + net_port_drops(cheap.s.net, cheap.s.dev[cheap.fsw].node, 0);
    uint64_t ddrop = net_port_drops(dear.s.net, dear.s.dev[dear.core].node, 1)
                   + net_port_drops(dear.s.net, dear.s.dev[dear.fsw].node, 0);
    const SiteTenant *ct = &cheap.s.tenant[ti], *dt = &dear.s.tenant[ti];
    printf("    switch4 riser %d Mb: %d/%d done, %d ms worst, %llu frames lost\n",
           cmb, ct->finished, ct->tried, ct->worst_ms,
           (unsigned long long)cdrop);
    printf("    switch8 riser %d Mb: %d/%d done, %d ms worst, %llu frames lost\n",
           dmb, dt->finished, dt->tried, dt->worst_ms,
           (unsigned long long)ddrop);
    ck("the same desks offered the same work to both towers",
       ct->tried == dt->tried && ct->tried > 0);
    ck("the cheap switch's port really came up at a hundred megabits",
       cmb == 100 && dmb == 1000);
    /* AND THE PENALTY IS TIME, WHICH IS WHERE IT MOVED TO AND WHY.
     *
     * This asserted `ct->finished < dt->finished` -- the cheap tower gets
     * less work done -- and that was true while every port in the game held
     * the same 48 KB. Now that a switch24 holds 128 KB, the core's port into
     * the cheap riser BUFFERS the burst instead of dropping it, and the work
     * all finishes: 28 of 28 either way.
     *
     * That is not the lesson going away, it is the lesson moving, and it
     * moved to the truthful place. A fast port feeding a slow one with a deep
     * buffer in between does not lose the traffic, it DELAYS it -- that is
     * bufferbloat, and it is what the same pair of boxes does on a real
     * bench. The cheap tower's day takes 3079 ms against the dear one's
     * 1752: seventy-six percent longer, inside a four second busy period,
     * which is a tower one tenancy away from not finishing at all -- and on
     * this station it IS one transfer away: 27 of 28 against 28 of 28.
     *
     * SO THE ASSERTION IS <=, NOT ==. The dear tower must never finish less
     * work than the cheap one, and the cheap one must always be measurably
     * slower; whether the last transfer falls off the end as well depends on
     * the shape of the deck, and pinning it to one number would make this a
     * check on seed 22's geometry rather than on the buffer.
     *
     * So the assertion is the penalty that exists rather than the one that
     * used to. A voice tenancy behind that buffer is worse off than before,
     * not better, because delay is what voice cannot survive -- which is
     * exactly the trade a deep buffer makes in the real world. */
    ck("and the cheap tower took measurably longer over the same work",
       ct->finished <= dt->finished && ct->worst_ms > dt->worst_ms * 5 / 4);
    /* AND THE REASON IS PRINTED, IN WORDS, ON THE PORT. */
    {
        Buf o = {0};
        site_cmd(&cheap.s, "show core", &o);
        ck("and `show` gives the reason on the port rather than a verdict",
           cdrop >= ddrop && has(o.p, "Mb"));
        buf_free(&o);
        Buf l = {0};
        site_cmd(&cheap.s, "load", &l);
        ck("and `load` names that port as the busiest thing in the building",
           has(l.p, "fsw:0"));
        buf_free(&l);
    }
    /* --- PORT COUNT, the limit that bites first. A fresh switch4 with its
     * riser in port 0 has three holes left, `serve` fills them and stops,
     * and the refusal is the box rather than the budget. */
    {
        int tiny = gate_box(&cheap.s, SDEV_SWITCH4, comms, "tiny");
        site_cable(&cheap.s, cheap.core, 2, tiny, 0, CAB_CAT6);
        int before = site_ports_spare(&cheap.s, tiny);
        int got = site_serve(&cheap.s, ti, tiny, CAB_CAT5E);
        printf("    a switch4 with a riser in it has %d holes left and "
               "seats %d more of %d desks\n", before, got - ndesk,
               cheap.s.tenant[ti].ndesk);
        ck("a switch4 with its riser in it seats three more desks and stops",
           before == 3 && got - ndesk == 3 && cheap.s.err == SITE_ENOPORT &&
           got < cheap.s.tenant[ti].ndesk);
        ck("and the fix is the bigger box, not a bigger cheque",
           site_kind_ports(SDEV_SWITCH8) > site_kind_ports(SDEV_SWITCH4) &&
           site_kind_price(SDEV_SWITCH8) > site_kind_price(SDEV_SWITCH4));
    }
    /* --- AND THERE IS NO `upgrade` VERB. The path is the physical one. */
    {
        Buf o = {0};
        ck("there is no `upgrade` verb, and there is not going to be one",
           !site_cmd(&cheap.s, "upgrade fsw switch8", &o));
        buf_clear(&o);
        int better = site_order(&cheap.s, SDEV_SWITCH8, "fsw2");
        site_cmd(&cheap.s, "show fsw2", &o);
        ck("the better box is ordered, lands in goods in, and has to be carried",
           better >= 0 &&
           cheap.s.dev[better].room == site_goods_room(&cheap.s) &&
           has(o.p, "8 sockets, numbered 0 to 7"));
        buf_free(&o);
    }
    site_free(&cheap.s);
    site_free(&dear.s);

    /* --- THE DISK, WHICH IS THE OTHER AXIS AND IS NOT ABOUT FRAMES. Two
     * servers side by side in one building, doing the same work on the same
     * days: the cheap one crosses its rating first, because its rating is
     * half. `events` prints both numbers rather than one constant. */
    {
        Site s;
        site_new(&s, &b, 22ull, 100000);
        site_credit(&s, 900000);
        int mdf = bld_find(&b, 0, RM_MDF);
        int mini = gate_box(&s, SDEV_MINITOWER, mdf, "mini");
        int big  = gate_box(&s, SDEV_RACKSERVER, mdf, "rack");
        site_power(&s, mini, true);
        site_power(&s, big, true);
        ck("a rack server arrives with a battery in it and a minitower does not",
           s.dev[big].ups == 1 && s.dev[mini].ups == 0);
        Buf e = {0};
        site_dump_events(&s, &e);
        ck("and `events` prints what each box's own disk is rated for",
           has(e.p, "rated") && has(e.p, " 120d") && has(e.p, "  30d"));
        buf_free(&e);
        /* Run them until one of them says something. Nothing is rolled: the
         * wear is added from the day, and the two boxes get the same days. */
        int minisaid = -1, bigsaid = -1;
        for (int d = 1; d <= 200 && minisaid < 0; d++) {
            s.over = 0; s.complaints = 0;
            for (int i = 0; i < s.ntenant; i++) {
                s.tenant[i].strikes = 0; s.tenant[i].complained = 0;
            }
            site_power(&s, mini, true); site_power(&s, big, true);
            site_day(&s, NULL);
            if (minisaid < 0 && s.dev[mini].warned) minisaid = s.day;
            if (bigsaid < 0 && s.dev[big].warned) bigsaid = s.day;
        }
        printf("    the minitower's disk starts logging on day %d; "
               "the rack server's has not by then\n", minisaid);
        ck("the cheap disk is the one that starts logging sectors first",
           minisaid > 0 && bigsaid < 0);
        ck("and it is a rating, not a countdown: the same day wore both",
           s.dev[mini].wear > s.dev[big].wear ||
           site_kind_disk_days(SDEV_MINITOWER) <
           site_kind_disk_days(SDEV_RACKSERVER));
        site_free(&s);
    }
    bld_free(&b);
}

int site_selfcheck(void)
{
    passed = total = 0;
    Building b;
    if (!bld_generate(&b, GATE_SEED)) {
        printf("the gate's own tower would not generate\n");
        return 1;
    }
    printf("tower %llu: %d decks, %d rooms, %d tenancies\n\n",
           (unsigned long long)GATE_SEED, b.floors, b.nrooms, b.ntenants);

    check_empty(&b);
    check_ports(&b);
    check_addresses(&b);
    check_copper(&b);
    check_port_speed(&b);
    check_boxes(&b);
    check_plug_pulled(&b);
    check_tenants(&b);
    check_bills(&b);
    check_agreement(&b);
    check_floor_server(&b);
    check_flat(&b);
    check_demand(&b);
    check_dhcp_scope(&b);
    check_dns_verbs(&b);
    check_ping_blames_the_filter(&b);
    check_trunk_line(&b);
    check_arity(&b);
    check_reports(&b);
    check_tolerance(&b);
    check_jack(&b);
    check_quote(&b);
    check_shell(&b);
    check_industry_rent(&b);
    check_industry_upload();
    check_industry_uptime();
    check_industry_voice();
    check_desk_rooms(&b);
    /* ONE FACT, ONE PLACE: the three reports that disagreed with each other,
     * and the two decisions the same playtest asked for. */
    check_dhcp_diagnosis(&b);
    check_headline_sums_the_rows();
    check_worst_is_wall_time();
    check_serve_vlan_remedy(&b);
    check_demand_says_what_a_server_is_for(&b);
    /* D43: ten things the game said about itself that another command in
     * the same session disproved. */
    check_catalogue(&b);
    check_conduits(&b);
    check_one_fact_two_answers(&b);
    check_ambiguity_and_the_diary();
    /* D43: and the decision the first twenty days did not have. */
    check_grades();
    /* AND THAT A PERSON CAN PLAY ALL OF IT OVER A SOCKET, which is the
     * claim that had quietly stopped being true. See core/sessioncheck.c. */
    session_selfcheck(&passed, &total);

    /* WHAT A FULLY EQUIPPED TOWER COSTS, measured rather than guessed --
     * and printed from sizeof rather than from a sentence somebody typed
     * once, because the pools grew when the tenants' desks became real
     * cards and the sentence did not. Nothing here boots an operating
     * system: a booted Machine is 13.5 MB, and there are none in any of it. */
    printf("\na fully equipped tower: %.1f KB of building, %.1f KB of site, "
           "and one %.1f MB network\nworld shared by every device in it, "
           "preallocated. No booted machines.\n",
           sizeof(Building) / 1024.0, sizeof(Site) / 1024.0,
           net_world_bytes() / (1024.0 * 1024.0));

    bld_free(&b);
    printf("\n%d/%d site checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
