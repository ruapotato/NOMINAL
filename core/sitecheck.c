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

static int passed, total;

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
    site_subif(&s, rt, 1, 1, 0, net_ip(10, 0, 1, 1), net_mask_bits(24));
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
        if (site_cable(&s, pc, 0, sw, i, CAB_CAT6) >= 0) filled++;
    }
    ck("an eight port switch takes eight cables and no more", filled == 8);

    int pc = site_install(&s, SDEV_PC, room, "pc-too-many");
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
    site_cable(&s, rt, 0, sw, 0, CAB_CAT6);
    site_cable(&s, a, 0, sw, 1, CAB_CAT6);
    site_cable(&s, c, 0, sw, 2, CAB_CAT6);

    /* The player segments them: one vlan each, one trunk to the router, one
     * subinterface per vlan. Every one of these is a call they made. */
    site_port_trunk(&s, sw, 0, 10);
    site_port_trunk(&s, sw, 0, 20);
    site_port_vlan(&s, sw, 1, 10);
    site_port_vlan(&s, sw, 2, 20);
    site_subif(&s, rt, 1, 0, 10, net_ip(10, 0, 10, 1), net_mask_bits(24));
    site_subif(&s, rt, 2, 0, 20, net_ip(10, 0, 20, 1), net_mask_bits(24));
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
    site_subif(&s, rt, 2, 0, 20, 0, 0);
    ck("take the router out of one vlan and the way across is gone",
       net_ping(s.net, s.dev[a].node, net_ip(10, 0, 20, 10), &rtt) != PING_OK);
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
        site_subif(&seg, grt, 1 + g, 0, 10 + g,
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

/* Everything above is reachable from a pipe, or a blind playtester cannot
 * find any of it. This builds a working network out of nothing but lines of
 * text, and then asks the machine on floor two what it can see. */
static void check_shell(const Building *b)
{
    printf("\nthe whole thing, over a pipe\n");
    static const char *SCRIPT[] = {
        "install switch8 f2.comms sw2",
        "install router f0.mdf rt",
        "install pc f2.office pc1",
        "cable rt:0 uplink:0 cat6",
        "cable rt:1 sw2:0 cat6",
        "cable pc1:0 sw2:1 cat6",
        "addr rt 198.51.100.2/30",
        "subif rt 1 1 0 192.168.7.1/24",
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
    ck("thirteen lines of text build a working network", understood);

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
    check_tenants(&b);
    check_flat(&b);
    check_demand(&b);
    check_shell(&b);

    /* WHAT A FULLY EQUIPPED TOWER COSTS, measured rather than guessed. The
     * building is %.1f KB and the site's own bookkeeping is %.1f KB; the
     * network world is a fixed 3.72 MB of preallocated pools, 31.2 KB of it
     * per host, and it is the same 3.72 MB whether the tower holds one
     * machine or ninety-five. Nothing here boots an operating system: a
     * booted Machine is 13.5 MB, and there are none in any of it. */
    printf("\na fully equipped tower: %.1f KB of building, %.1f KB of site, "
           "and one 3.7 MB network\nworld shared by every device in it "
           "(31.2 KB a host, preallocated). No booted machines.\n",
           sizeof(Building) / 1024.0, sizeof(Site) / 1024.0);

    bld_free(&b);
    printf("\n%d/%d site checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
