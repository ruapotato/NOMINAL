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
#include <string.h>
#include "nom.h"
#include "site.h"
#include "session.h"

static int passed, total;

static bool has(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
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

    ck("one device exists, and it is the ISP's handoff",
       s.ndev == 1 && s.dev[s.uplink].kind == SDEV_UPLINK && s.nlink == 0);

    /* Buy a machine, put it in an office, give it an address. It is not
     * plugged into anything, so it can reach nothing -- including the socket
     * in the same building. */
    int room = a_room(b, 3);
    int pc = site_install(&s, SDEV_PC, room, "pc1");
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
    int sw = site_install(&s, SDEV_SWITCH8, comms, "sw3");
    int rt = site_install(&s, SDEV_ROUTER, mdf, "rt");
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
    ck("configured, the machine on floor 3 reaches the internet",
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
    int sw = site_install(&s, SDEV_SWITCH8, comms, "sw4");

    /* Fill it. Seven machines and an uplink is what an eight-port switch is,
     * and the eighth machine has nowhere to go. */
    int room = a_room(b, 4);
    int filled = 0;
    for (int i = 0; i < 8; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "pc%d", i);
        int pc = site_install(&s, SDEV_PC, room, nm);
        if (pc < 0) break;
        site_power(&s, pc, true);
        if (site_cable(&s, pc, 0, sw, i, CAB_CAT6) >= 0) filled++;
    }
    ck("an eight port switch takes eight cables and no more", filled == 8);

    int pc = site_install(&s, SDEV_PC, room, "pc-too-many");
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
    int sw = site_install(&s, SDEV_SWITCH24, comms, "sw5");
    int rt = site_install(&s, SDEV_ROUTER, comms, "rt5");
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
        int pc = site_install(&s, SDEV_PC, room, nm);
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
    printf("    the farthest lettable room is on floor %d, %.1f m of tray "
           "from the MDF\n", floor, d);
    ck("a tall tower puts a room past what copper carries", d + SITE_PATCH_M > 100);

    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    int sw = site_install(&s, SDEV_SWITCH24, mdf, "core");
    int pc = site_install(&s, SDEV_PC, far, "topfloor");
    site_power(&s, pc, true);
    int l = site_cable(&s, pc, 0, sw, 1, CAB_CAT6);
    ck("the cable is sold, laid and paid for", l >= 0 && s.link[l].cost > 0);
    ck("and it does not come up, because it is too long",
       site_link_state(&s, l) == PORT_TOOLONG);
    printf("    %d m of cat6, %d spent, and the link light never comes on\n",
           s.link[l].metres, s.link[l].cost);

    /* The fix a real installer makes: a switch on that floor instead. */
    int comms = bld_find(b, floor, RM_COMMS);
    int fsw = site_install(&s, SDEV_SWITCH8, comms, "swtop");
    site_uncable(&s, l);
    int l2 = site_cable(&s, pc, 0, fsw, 1, CAB_CAT6);
    int l3 = site_cable(&s, fsw, 0, sw, 2, CAB_CAT6);
    ck("a switch in that floor's cupboard fixes it, and both runs come up",
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
    int core = site_install(&s, SDEV_SWITCH24, mdf, "core");
    int edge = site_install(&s, SDEV_ROUTER, mdf, "edge");
    int srv  = site_install(&s, SDEV_SERVER, mdf, "files");
    int sw8  = site_install(&s, SDEV_SWITCH8, mdf, "little");
    if (core < 0 || edge < 0 || srv < 0 || sw8 < 0) {
        ck("four boxes in the MDF", false); site_free(&s); return;
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

    int l3 = site_cable(&s, core, 2, sw8, 0, CAB_FIBRE);
    int mb_8 = net_port_speed(s.net, s.dev[sw8].node, 0);
    ck("and fibre into a cheap eight-port switch is a gigabit too",
       l3 >= 0 && mb_8 == 1000);
    printf("    the same room, the same three metres: %d Mb to the router, "
           "%d Mb to the server\n", mb_up, mb_srv);

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
        int d = site_install(&s, k, room, nm);
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
    int rt = site_install(&s, SDEV_ROUTER, room, "rtv");
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
/* The deepest inconsistency a playtest found: a box that had never been
 * powered on answered a ping, because the address went onto its network node
 * the moment the player typed it. */
static void check_power(const Building *b)
{
    printf("\na box that is not running\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    int mdf = bld_find(b, 0, RM_MDF);
    int sw = site_install(&s, SDEV_SWITCH8, mdf, "sw");
    int rt = site_install(&s, SDEV_ROUTER, mdf, "rt");
    int pc = site_install(&s, SDEV_PC, mdf, "probe");
    site_cable(&s, rt, 0, sw, 0, CAB_CAT6);
    site_cable(&s, pc, 0, sw, 1, CAB_CAT6);
    site_addr(&s, rt, 0, net_ip(10, 0, 1, 1), net_mask_bits(24));

    ck("a pc arrives switched off and a switch has no button at all",
       !s.dev[pc].powered && s.dev[sw].powered && s.dev[rt].powered &&
       !site_power(&s, sw, false) && s.err == SITE_ENOBTN);
    ck("an off box will not take an address: there is nothing in it to hold one",
       !site_addr(&s, pc, 0, net_ip(10, 0, 1, 30), net_mask_bits(24)) &&
       s.err == SITE_EOFF);
    int rtt = 0;
    ck("and it answers nothing, with a cable in it and a router beside it",
       net_ping(s.net, s.dev[rt].node, net_ip(10, 0, 1, 30), &rtt) != PING_OK &&
       net_if_get_addr(s.net, s.dev[pc].node, 0) == 0);

    ck("powered on, it takes one and answers",
       site_power(&s, pc, true) &&
       site_addr(&s, pc, 0, net_ip(10, 0, 1, 30), net_mask_bits(24)) &&
       net_ping(s.net, s.dev[rt].node, net_ip(10, 0, 1, 30), &rtt) == PING_OK);

    /* And what was in its memory was in its memory. */
    site_power(&s, pc, false);
    ck("switched off again, the address goes with the power and it is silent",
       net_if_get_addr(s.net, s.dev[pc].node, 0) == 0 &&
       net_ping(s.net, s.dev[rt].node, net_ip(10, 0, 1, 30), &rtt) != PING_OK);
    site_free(&s);
}

/* --------------------------------------------------------- the tenants */
static void check_tenants(const Building *b)
{
    printf("\ntwo tenants who share a floor and must not share a network\n");
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
    if (ra < 0 || rb < 0) { ck("the floor has two tenancies on it", false);
                            site_free(&s); return; }

    int sw = site_install(&s, SDEV_SWITCH24, comms, "sw3");
    int rt = site_install(&s, SDEV_ROUTER, mdf, "rt");
    int a  = site_install(&s, SDEV_PC, ra, "theirs");
    int c  = site_install(&s, SDEV_PC, rb, "ours");
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
    int sw = site_install(&s, SDEV_SWITCH8, mdf, "sw");
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
    int rt = site_install(&s, SDEV_ROUTER, mdf, "rt");
    site_cable(&s, rt, 0, s.uplink, 0, CAB_CAT6);
    site_addr(&s, rt, 0, s.wan_you, s.wan_mask);
    site_addr(&s, rt, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, rt, s.wan_isp);
    site_forwarding(&s, rt, true);

    /* Run days until somebody has moved in and brought their desks. */
    for (int i = 0; i < 400 && !s.tenant[0].moved; i++) site_day(&s, NULL);
    if (!s.tenant[0].moved) { ck("a tenancy moves in", false); site_free(&s); return; }

    int sw = site_install(&s, SDEV_SWITCH24,
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
    ck("and `service` says what its own two columns are",
       sv.p && strstr(sv.p, "up is desks whose port has LINK on it") &&
       strstr(sv.p, "only an addressed desk does any work"));
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
        int srv = site_install(&s, SDEV_SERVER, mdf, "fs");
        site_cable(&s, rt, 2, srv, 0, CAB_CAT6);
        site_power(&s, srv, true);
        site_addr(&s, srv, 0, net_ip(10, 0, 1, 10), net_mask_bits(16));
        site_day(&s, NULL);
        sv.len = 0; if (sv.p) sv.p[0] = 0;
        site_dump_service(&s, &sv);
        ck("`service` names the server a tenancy's people actually pulled off",
           s.tenant[0].files_dev == srv && sv.p && strstr(sv.p, "fs") != NULL);
        ck("and marks it when that server is not on their floor",
           s.dev[srv].floor != s.tenant[0].floor &&
           strstr(sv.p, "fs <-") != NULL &&
           strstr(sv.p, "being served from another floor") != NULL);
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
    int sw = site_install(&flat, SDEV_SWITCH24, comms, "flat");
    for (int i = 0; i < BIG; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "pc%d", i);
        pc[i] = site_install(&flat, SDEV_PC, room[i % 4], nm);
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
    grt = site_install(&seg, SDEV_ROUTER, bld_find(b, 0, RM_MDF), "rt");
    int core = site_install(&seg, SDEV_SWITCH8, bld_find(b, 0, RM_MDF), "core");
    site_cable(&seg, grt, 0, core, 0, CAB_CAT6);
    site_port_trunk(&seg, core, 0, 0);
    for (int g = 0; g < 3; g++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "sw%d", g);
        gsw[g] = site_install(&seg, SDEV_SWITCH8, bld_find(b, g + 2, RM_COMMS), nm);
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
        spc[i] = site_install(&seg, SDEV_PC, room[g], nm);
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
    int sw = site_install(&a, SDEV_SWITCH24, bld_find(b, 1, RM_COMMS), "sw");
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
        "order router edge",  "move edge f0.mdf",
        "order server dns1",  "move dns1 f0.mdf",
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
    for (int i = 0; SCRIPT[i]; i++) site_cmd(&s, SCRIPT[i], &o);

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
    site_cmd(&s, "dns dns1 files.floor3 10.0.0.50", &o);
    ck("`dns <box> <name> <ip>` puts a name in the zone",
       has(o.p, "files.floor3 -> 10.0.0.50") && has(o.p, "serves 1 name"));

    buf_clear(&o);
    site_cmd(&s, "resolver edge 10.0.0.10", &o);
    buf_clear(&o);
    site_cmd(&s, "resolve edge files.floor3", &o);
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
        "order router edge",  "move edge f0.mdf",
        "order server files", "move files f0.mdf",
        "cable edge:1 files:0 cat6",
        "addr edge:1 10.0.0.1/24",
        "router edge on",
        "power files on",
        "addr files 10.0.0.10/24",
        "gw files 10.0.0.1",
        NULL
    };
    for (int i = 0; SCRIPT[i]; i++) site_cmd(&s, SCRIPT[i], &o);

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
        "order router edge",   "move edge f0.mdf",
        "order switch24 core", "move core f0.mdf",
        "cable edge:1 core:0 cat6",
        "vlan core 1 11",
        "vlan core 2 13",
        "trunk core 0 11 13",
        "subif edge 1 11 10.11.0.1/24",
        "subif edge 1 13 10.13.0.1/24",
        "router edge on",
        NULL
    };
    for (int i = 0; SCRIPT[i]; i++) site_cmd(&s, SCRIPT[i], &o);

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
    int d11 = site_install(&s, SDEV_PC, room, "d11");
    int d13 = site_install(&s, SDEV_PC, room, "d13");
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
    int d11b = site_install(&s, SDEV_PC, room, "d11b");
    site_power(&s, d11b, true);
    site_cable(&s, d11b, 0, site_dev_by_name(&s, "core"), 3, CAB_CAT5E);
    site_cmd(&s, "vlan core 3 11", &o);
    buf_clear(&o);
    site_cmd(&s, "dhcp d11b", &o);
    ck("a stopped server answers a discover with nothing at all",
       has(o.p, "no lease") &&
       net_if_get_addr(s.net, s.dev[d11b].node, 0) == 0);

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
static void check_arity(const Building *b)
{
    printf("\nevery verb, handed too few words\n");
    Site s;
    site_new(&s, b, GATE_SEED, 100000);
    site_credit(&s, 400000);
    site_install(&s, SDEV_SWITCH24, bld_find(b, 0, RM_MDF), "core");
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
static void check_reports(const Building *b)
{
    printf("\nwhat the game says about itself\n");
    Site s; Buf o; buf_init(&o);
    site_new(&s, b, GATE_SEED, 100000);
    int mdf = bld_find(b, 0, RM_MDF);
    int srv = site_install(&s, SDEV_SERVER, mdf, "files");
    int sw  = site_install(&s, SDEV_SWITCH8, mdf, "core");
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
    int lonely = site_install(&s, SDEV_PC, a_room(b, 3), "lonely");
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
    ck("and the survivor kept its index, so `uncable <n>` still means what "
       "it meant", lf == 1 && site_link_state(&s, lf) == PORT_UP);
    buf_clear(&o);
    site_cmd(&s, "uncable 1", &o);
    ck("`uncable 1` pulls the run the table numbered 1",
       site_link_state(&s, lf) == PORT_NOCABLE);
    buf_clear(&o);
    site_cmd(&s, "links", &o);
    ck("with everything pulled, the table says so and still counts the money",
       has(o.p, "0 m of cable in the building") &&
       has(o.p, "2 pulled runs") && has(o.p, want));
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
        "move sw2 f2.comms",
        "order router rt",
        "move rt f0.mdf",
        "order pc pc1",
        "power pc1 on",
        "move pc1 f2.office",
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
    for (int i = 0; SCRIPT[i]; i++)
        if (!site_cmd(&s, SCRIPT[i], &o)) understood = false;
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
int site_selfcheck(void)
{
    passed = total = 0;
    Building b;
    if (!bld_generate(&b, GATE_SEED)) {
        printf("the gate's own tower would not generate\n");
        return 1;
    }
    printf("tower %llu: %d floors, %d rooms, %d tenancies\n\n",
           (unsigned long long)GATE_SEED, b.floors, b.nrooms, b.ntenants);

    check_empty(&b);
    check_ports(&b);
    check_addresses(&b);
    check_copper(&b);
    check_port_speed(&b);
    check_boxes(&b);
    check_power(&b);
    check_tenants(&b);
    check_bills(&b);
    check_agreement(&b);
    check_flat(&b);
    check_demand(&b);
    check_dhcp_scope(&b);
    check_dns_verbs(&b);
    check_ping_blames_the_filter(&b);
    check_arity(&b);
    check_reports(&b);
    check_shell(&b);
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
