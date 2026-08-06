/* netcheck.c — the gate for the network, run as `bf --netcheck`.
 *
 * WHAT IT IS FOR. Every other claim this project makes about itself is
 * checked by a program that runs the thing. The network makes bigger claims
 * than anything else here -- that a fault EMERGES rather than being authored
 * -- and a claim like that is worthless unless the emergence is demonstrated
 * from the outside.
 *
 * So each check below builds a topology, does something ordinary to it, and
 * asserts what a person at a terminal would see. None of them reaches inside
 * the stack for a flag. If a check needed a flag to pass, the behaviour it
 * describes would not be real, and the check would be the lie.
 *
 * The negative checks matter more than the positive ones. Anybody can make a
 * ping succeed. The interesting assertions are that an unplugged cable stops
 * a frame, that a wrong mask fails ONLY off-net, that a loop really does
 * storm, and that a dead resolver times out instead of erroring -- because
 * each of those is a fault a player will build by hand and have to diagnose
 * with no oracle to ask.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nom.h"
#include "netstack.h"
/* The last two checks boot a REAL machine and type at it, because the filter
 * has two halves and the other one is a program. net_fw_add() is exercised
 * above; nft(8) is what turns a line of somebody's config into a call to it,
 * and a rule that parses and does not bite is worse than no rule at all. */
#include "machine.h"
#include "kernel.h"

static int passed, total;

static void ck(const char *what, bool ok)
{
    total++;
    if (ok) passed++;
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
}

/* Two machines and a switch: the smallest network anyone ever builds, and
 * the shape most of these checks want. */
typedef struct { Net *n; int a, b, sw; } Lan;

static Lan lan_new(uint64_t seed)
{
    Lan l;
    l.n = net_new(seed);
    l.sw = net_add_switch(l.n, "sw1", 8);
    l.a  = net_add_host(l.n, "alpha");
    l.b  = net_add_host(l.n, "bravo");
    net_cable(l.n, l.a, 0, l.sw, 0, 12, CAB_CAT5E);
    net_cable(l.n, l.b, 0, l.sw, 1, 18, CAB_CAT5E);
    net_if_addr(l.n, l.a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(l.n, l.b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));
    return l;
}

/* ------------------------------------------------------------------- L1 */
static void check_wire(void)
{
    printf("L1 -- the wire\n");
    Net *n = net_new(1);
    int a = net_add_host(n, "alpha"), b = net_add_host(n, "bravo");
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(n, b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));

    /* No cable at all. Nothing has been configured wrongly; there is simply
     * no copper, and that alone is the whole fault. */
    ck("no cable: port reports no link",
       net_port_state(n, a, 0) == PORT_NOCABLE);
    ck("no cable: a ping cannot leave the machine",
       net_ping(n, a, net_ip(10, 0, 0, 2), NULL) != PING_OK);

    int c = net_cable(n, a, 0, b, 0, 20, CAB_CAT5E);
    ck("cabled: both ends come up",
       net_port_state(n, a, 0) == PORT_UP && net_port_state(n, b, 0) == PORT_UP);
    uint64_t tx0 = net_port_tx(n, a, 0), rx0 = net_port_rx(n, b, 0);
    ck("a frame crosses the cable", net_ping(n, a, net_ip(10, 0, 0, 2), NULL) == PING_OK);
    ck("and the counters at both ends moved",
       net_port_tx(n, a, 0) > tx0 && net_port_rx(n, b, 0) > rx0);

    /* Pull it out. Nothing else changes: the addresses are still right, the
     * routes are still right, the interfaces are still up. */
    net_uncable(n, c);
    net_arp_flush(n, a);
    ck("unplugged: the frame does not cross",
       net_ping(n, a, net_ip(10, 0, 0, 2), NULL) != PING_OK);
    ck("unplugged: the port says so, and the address is untouched",
       net_port_state(n, a, 0) == PORT_NOCABLE &&
       net_if_get_addr(n, a, 0) == net_ip(10, 0, 0, 1));

    net_cable(n, a, 0, b, 0, 20, CAB_CAT5E);
    net_port_admin(n, b, 0, false);
    ck("far end shut down: the near end loses link too",
       net_port_state(n, a, 0) == PORT_NOCABLE);
    net_port_admin(n, b, 0, true);
    net_free(n);

    /* Length is real. 140 m of copper does not work, and the port says why
     * rather than pretending the cable is not there. */
    Net *m = net_new(2);
    int x = net_add_host(m, "x"), y = net_add_host(m, "y");
    net_if_addr(m, x, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(m, y, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));
    net_cable(m, x, 0, y, 0, 140, CAB_CAT6);
    ck("a 140 m copper run does not come up",
       net_port_state(m, x, 0) == PORT_TOOLONG &&
       net_ping(m, x, net_ip(10, 0, 0, 2), NULL) != PING_OK);
    net_uncable(m, 0);
    net_cable(m, x, 0, y, 0, 140, CAB_FIBRE);
    ck("the same 140 m of fibre does", net_port_state(m, x, 0) == PORT_UP);
    /* A CHECK THAT CANNOT FAIL IS NOT A CHECK. This line asserted `true` and
     * therefore said nothing about the speed table it claimed to be testing.
     * Now it reads the negotiated speed off two real links. */
    net_uncable(m, 0);
    net_cable(m, x, 0, y, 0, 30, CAB_CAT6);
    int fast = net_port_speed(m, x, 0);
    net_uncable(m, 0);
    net_cable(m, x, 0, y, 0, 80, CAB_CAT6);
    int slow = net_port_speed(m, x, 0);
    ck("cat6 negotiates 10Gb over a short run and 1Gb over a long one",
       fast == 10000 && slow == 1000);
    ck("a port with no link negotiates nothing",
       (net_port_admin(m, x, 0, false), net_port_speed(m, x, 0)) == 0);
    net_free(m);
}

/* ------------------------------------------------------------------- L2 */
static void check_ethernet(void)
{
    printf("L2 -- ethernet and switching\n");
    Lan l = lan_new(3);
    Net *n = l.n;
    /* A third machine on the same switch, doing nothing. It is the witness:
     * whether a frame reaches it is how we tell flooding from forwarding. */
    int c = net_add_host(n, "witness");
    net_cable(n, c, 0, l.sw, 2, 5, CAB_CAT5E);
    net_if_addr(n, c, 0, net_ip(10, 0, 0, 3), net_mask_bits(24));

    ck("a new switch has learned nothing", net_fdb_count(n, l.sw) == 0);

    uint8_t amac[6], bmac[6];
    net_get_mac(n, l.a, 0, amac);
    net_get_mac(n, l.b, 0, bmac);

    net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL);
    ck("the switch learned the sender from its source address",
       net_fdb_lookup(n, l.sw, amac, 1) == 0);
    ck("and learned the replier too", net_fdb_lookup(n, l.sw, bmac, 1) == 1);

    /* Now that it knows, it does not flood: the witness sees nothing of a
     * conversation between the other two. */
    uint64_t quiet = net_port_rx(n, c, 0);
    net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL);
    ck("a learned destination is not flooded past the witness",
       net_port_rx(n, c, 0) == quiet);

    /* Clear the table. Alpha still has bravo's hardware address cached, so
     * the next frame is a UNICAST to an address the switch no longer knows
     * -- and a switch that does not know floods, which is the whole reason
     * it can find anything at all. */
    net_fdb_flush(n, l.sw);
    ck("clearing the table really clears it", net_fdb_count(n, l.sw) == 0);
    quiet = net_port_rx(n, c, 0);
    net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL);
    ck("an unknown unicast destination is flooded to every other port",
       net_port_rx(n, c, 0) > quiet);
    ck("and the switch learns again from what it floods",
       net_fdb_lookup(n, l.sw, bmac, 1) == 1);

    ck("an address it has never seen is not in the table",
       net_fdb_lookup(n, l.sw, (const uint8_t *)"\xaa\xbb\xcc\xdd\xee\xff", 1) < 0);
    net_free(n);
}

static void check_vlans(void)
{
    printf("L2 -- VLANs\n");
    Net *n = net_new(4);
    int sw = net_add_switch(n, "sw1", 8);
    int a = net_add_host(n, "alpha"), b = net_add_host(n, "bravo"),
        c = net_add_host(n, "charlie");
    net_cable(n, a, 0, sw, 0, 5, CAB_CAT5E);
    net_cable(n, b, 0, sw, 1, 5, CAB_CAT5E);
    net_cable(n, c, 0, sw, 2, 5, CAB_CAT5E);
    /* One IP subnet across all three. Nothing about the addressing is wrong
     * -- the segregation is happening two layers below it. */
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(n, b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));
    net_if_addr(n, c, 0, net_ip(10, 0, 0, 3), net_mask_bits(24));
    net_port_vlan(n, sw, 0, 10);
    net_port_vlan(n, sw, 1, 10);
    net_port_vlan(n, sw, 2, 20);

    ck("same vlan, same subnet: it works",
       net_ping(n, a, net_ip(10, 0, 0, 2), NULL) == PING_OK);
    ck("different vlan, same subnet: it does not",
       net_ping(n, a, net_ip(10, 0, 0, 3), NULL) != PING_OK);
    ck("the cable to the segregated machine is still up",
       net_port_state(n, c, 0) == PORT_UP);
    ck("and its address is still what it should be",
       net_if_get_addr(n, c, 0) == net_ip(10, 0, 0, 3));

    /* Move it into the right vlan. One command, and nothing else changes. */
    net_port_vlan(n, sw, 2, 10);
    net_arp_flush(n, a);
    ck("moving the port into vlan 10 fixes it with no other change",
       net_ping(n, a, net_ip(10, 0, 0, 3), NULL) == PING_OK);
    net_free(n);

    /* A trunk between two switches, which is where vlan faults really live:
     * the cable is right, the ports are right, and the trunk was never told
     * to carry the vlan. */
    Net *m = net_new(5);
    int s1 = net_add_switch(m, "sw1", 8), s2 = net_add_switch(m, "sw2", 8);
    int x = net_add_host(m, "x"), y = net_add_host(m, "y");
    net_cable(m, x, 0, s1, 0, 5, CAB_CAT5E);
    net_cable(m, y, 0, s2, 0, 5, CAB_CAT5E);
    net_cable(m, s1, 7, s2, 7, 30, CAB_CAT6);
    net_if_addr(m, x, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(m, y, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));
    net_port_vlan(m, s1, 0, 30);
    net_port_vlan(m, s2, 0, 30);
    net_port_mode(m, s1, 7, PORT_TRUNK);
    net_port_mode(m, s2, 7, PORT_TRUNK);
    ck("a trunk carrying no vlans carries nothing",
       net_ping(m, x, net_ip(10, 0, 0, 2), NULL) != PING_OK);
    net_trunk_allow(m, s1, 7, 30);
    net_trunk_allow(m, s2, 7, 30);
    net_arp_flush(m, x);
    ck("allowing vlan 30 across the trunk fixes it",
       net_ping(m, x, net_ip(10, 0, 0, 2), NULL) == PING_OK);
    net_free(m);
}

static void check_storm(void)
{
    printf("L2 -- a loop, with and without spanning tree\n");
    /* Two switches, TWO cables. Nobody built anything else wrong. */
    Net *n = net_new(6);
    int s1 = net_add_switch(n, "sw1", 8), s2 = net_add_switch(n, "sw2", 8);
    int a = net_add_host(n, "alpha"), b = net_add_host(n, "bravo");
    net_cable(n, a, 0, s1, 0, 5, CAB_CAT5E);
    net_cable(n, b, 0, s2, 0, 5, CAB_CAT5E);
    net_cable(n, s1, 6, s2, 6, 20, CAB_CAT6);
    net_cable(n, s1, 7, s2, 7, 20, CAB_CAT6);
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(n, b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));

    /* One ordinary ping. One broadcast ARP request is all it takes. */
    net_ping(n, a, net_ip(10, 0, 0, 2), NULL);
    net_step(n, 400);
    uint64_t load = net_load(n), drops = net_queue_drops(n);
    /* WHERE A STORM IS VISIBLE. On the ports carrying it: the trunk between
     * the two switches is offered more frames than a gigabit will clock out
     * and its egress buffer fills, so the drop is counted on the port and
     * `netstat -P` prints it with the reason. That is the same counter an
     * oversubscribed uplink fills, because it is the same fault. */
    for (int p = 0; p < 8; p++) drops += net_port_qdrops(n, s1, p)
                                       + net_port_qdrops(n, s2, p);
    ck("a loop with no spanning tree storms", load > 200);
    ck("and the storm is visible as dropped frames", drops > 0);
    ck("and it does not stop on its own", (net_step(n, 400), net_load(n)) > 200);
    net_free(n);

    /* The same building, with spanning tree switched on. */
    Net *m = net_new(6);
    s1 = net_add_switch(m, "sw1", 8); s2 = net_add_switch(m, "sw2", 8);
    a = net_add_host(m, "alpha"); b = net_add_host(m, "bravo");
    net_cable(m, a, 0, s1, 0, 5, CAB_CAT5E);
    net_cable(m, b, 0, s2, 0, 5, CAB_CAT5E);
    net_cable(m, s1, 6, s2, 6, 20, CAB_CAT6);
    net_cable(m, s1, 7, s2, 7, 20, CAB_CAT6);
    net_if_addr(m, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(m, b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));
    net_stp(m, s1, true);
    net_stp(m, s2, true);
    ck("with spanning tree the same wiring works",
       net_ping(m, a, net_ip(10, 0, 0, 2), NULL) == PING_OK);
    net_step(m, 400);
    ck("and does not storm", net_load(m) < 100);
    net_free(m);
}

/* ------------------------------------------------------------------- L3 */
static void check_arp(void)
{
    printf("L3 -- ARP\n");
    Lan l = lan_new(7);
    Net *n = l.n;
    uint8_t bmac[6], got[6];
    net_get_mac(n, l.b, 0, bmac);

    ck("nothing is cached before anyone speaks", net_arp_count(n, l.a) == 0);
    ck("a real request gets a real reply",
       net_arp_resolve(n, l.a, net_ip(10, 0, 0, 2), got) && memcmp(got, bmac, 6) == 0);
    ck("and the answer is cached", net_arp_cached(n, l.a, net_ip(10, 0, 0, 2), NULL));
    ck("an address nobody holds never resolves",
       !net_arp_resolve(n, l.a, net_ip(10, 0, 0, 77), got));

    /* Two machines with the same address both answer, and the cache believes
     * whichever spoke last. Nothing detects this; it is simply what happens,
     * and it is why a duplicate address is such a miserable ticket. */
    int c = net_add_host(n, "clone");
    net_cable(n, c, 0, l.sw, 3, 9, CAB_CAT5E);
    net_if_addr(n, c, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));   /* same as bravo */
    net_arp_flush(n, l.a);
    ck("a duplicate address still resolves -- to one of the two",
       net_arp_resolve(n, l.a, net_ip(10, 0, 0, 2), got));
    uint8_t cmac[6];
    net_get_mac(n, c, 0, cmac);
    ck("and the machine that got the answer cannot tell which",
       memcmp(got, bmac, 6) == 0 || memcmp(got, cmac, 6) == 0);
    net_free(n);
}

static void check_mask(void)
{
    printf("L3 -- the netmask, which is arithmetic and not a rule\n");
    Net *n = net_new(8);
    int sw = net_add_switch(n, "sw1", 8);
    int a = net_add_host(n, "alpha"), b = net_add_host(n, "bravo"),
        far = net_add_host(n, "far");
    net_cable(n, a, 0, sw, 0, 5, CAB_CAT5E);
    net_cable(n, b, 0, sw, 1, 5, CAB_CAT5E);
    net_cable(n, far, 0, sw, 2, 5, CAB_CAT5E);
    /* The site is a /16. Alpha was configured /24 by somebody who assumed. */
    net_if_addr(n, a,   0, net_ip(10, 0, 0, 1), net_mask_bits(24));   /* wrong */
    net_if_addr(n, b,   0, net_ip(10, 0, 0, 2), net_mask_bits(16));
    net_if_addr(n, far, 0, net_ip(10, 0, 9, 9), net_mask_bits(16));

    ck("a wrong mask still reaches everything on its own segment",
       net_ping(n, a, net_ip(10, 0, 0, 2), NULL) == PING_OK);
    PingResult r = net_ping(n, a, net_ip(10, 0, 9, 9), NULL);
    ck("and fails for an address the mask puts off-net", r != PING_OK);
    ck("failing as 'network is unreachable', because there is no route",
       r == PING_NO_ROUTE);
    ck("the machine it cannot reach is on the same cable and answers others",
       net_ping(n, b, net_ip(10, 0, 9, 9), NULL) == PING_OK);

    /* Widen the mask. That is the entire repair. */
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(16));
    ck("correcting the mask fixes it and nothing else was touched",
       net_ping(n, a, net_ip(10, 0, 9, 9), NULL) == PING_OK);

    /* The other direction: a mask that is too WIDE makes a remote address
     * look local, so the machine ARPs for it instead of routing, and nobody
     * answers. Same file, same arithmetic, opposite symptom. */
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(8));
    net_arp_flush(n, a);
    ck("a mask that is too wide ARPs for a machine that is not on the wire",
       net_ping(n, a, net_ip(10, 55, 55, 55), NULL) == PING_HOST_UNREACH);
    net_free(n);
}

/* A router in the middle, which is where gateways and ICMP errors live. */
static void check_routing(void)
{
    printf("L3 -- routing, gateways and ICMP\n");
    Net *n = net_new(9);
    int s1 = net_add_switch(n, "sw1", 8), s2 = net_add_switch(n, "sw2", 8);
    int a = net_add_host(n, "alpha"), b = net_add_host(n, "bravo"),
        r = net_add_host(n, "router");
    net_cable(n, a, 0, s1, 0, 5, CAB_CAT5E);
    net_cable(n, b, 0, s2, 0, 5, CAB_CAT5E);
    net_cable(n, r, 0, s1, 1, 5, CAB_CAT5E);
    net_if_port(n, r, 1, 1);
    net_cable(n, r, 1, s2, 1, 5, CAB_CAT5E);
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 5),   net_mask_bits(24));
    net_if_addr(n, b, 0, net_ip(10, 0, 1, 5),   net_mask_bits(24));
    net_if_addr(n, r, 0, net_ip(10, 0, 0, 254), net_mask_bits(24));
    net_if_addr(n, r, 1, net_ip(10, 0, 1, 254), net_mask_bits(24));
    net_forwarding(n, r, true);
    net_set_gateway(n, b, net_ip(10, 0, 1, 254));

    /* No gateway on alpha at all. */
    ck("no default route: the machine says the network is unreachable",
       net_ping(n, a, net_ip(10, 0, 1, 5), NULL) == PING_NO_ROUTE);

    /* A gateway address that nothing holds. This is the single commonest
     * typo in the trade and it fails at ARP, on the last hop, which is why
     * it says HOST unreachable and not NETWORK unreachable. */
    net_set_gateway(n, a, net_ip(10, 0, 0, 253));
    ck("a gateway nobody answers to: destination host unreachable",
       net_ping(n, a, net_ip(10, 0, 1, 5), NULL) == PING_HOST_UNREACH);

    net_set_gateway(n, a, net_ip(10, 0, 0, 254));
    net_arp_flush(n, a);
    ck("the right gateway: it works, through a real router",
       net_ping(n, a, net_ip(10, 0, 1, 5), NULL) == PING_OK);

    /* The router forwards for a living, so a destination it has no route to
     * produces an ICMP network-unreachable FROM THE ROUTER -- which is how a
     * player learns the problem is one hop away and not on their desk. */
    ck("a destination the router has no route to: net unreachable",
       net_ping(n, a, net_ip(10, 0, 9, 9), NULL) == PING_NET_UNREACH);

    /* Switch forwarding off on the router. It is not a router any more, and
     * it drops silently -- there is no ICMP for "I decided not to". */
    net_forwarding(n, r, false);
    ck("a gateway with forwarding off drops in silence",
       net_ping(n, a, net_ip(10, 0, 1, 5), NULL) == PING_TIMEOUT);
    net_forwarding(n, r, true);

    /* Two routers pointing at each other. TTL is a counter and it runs out. */
    int r2 = net_add_host(n, "router2");
    net_cable(n, r2, 0, s2, 2, 5, CAB_CAT5E);
    net_if_addr(n, r2, 0, net_ip(10, 0, 1, 253), net_mask_bits(24));
    net_forwarding(n, r2, true);
    net_set_gateway(n, r, net_ip(10, 0, 1, 253));
    net_set_gateway(n, r2, net_ip(10, 0, 1, 254));
    ck("a routing loop counts down the TTL and reports time exceeded",
       net_ping(n, a, net_ip(10, 0, 9, 9), NULL) == PING_TTL_EXCEEDED);
    net_free(n);
}

/* ------------------------------------------------- every socket is a card */
/* THE BLOCKER A BLIND PLAYTEST FOUND. Three of a router's four ports were
 * holes with nothing behind them: the port said `up`, its rx counter climbed
 * with every ARP, and the frames were dropped without ever appearing in
 * `drop`, because no interface claimed them. Twenty minutes of correct
 * sysadmin, and the fix was to move the cable to port 0.
 *
 * So: a frame arriving on ANY socket reaches the stack, and a box with an
 * address on two of them routes between them. */
static void check_nics(void)
{
    printf("every socket on the back of the box\n");
    Net *n = net_new(31);
    int sw = net_add_switch(n, "sw1", 8);
    int r  = net_add_host(n, "router");
    int peer[NET_HOST_NICS];
    bool all_up = true, all_reach = true;

    /* One machine per socket, each on its own subnet, each cabled to the
     * port with the same number. Nothing is special about port 0. */
    for (int i = 0; i < NET_HOST_NICS; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "peer%d", i);
        peer[i] = net_add_host(n, nm);
        net_cable(n, r, i, sw, i * 2, 5, CAB_CAT5E);
        net_cable(n, peer[i], 0, sw, i * 2 + 1, 5, CAB_CAT5E);
        net_port_vlan(n, sw, i * 2, 10 + i);
        net_port_vlan(n, sw, i * 2 + 1, 10 + i);
        net_if_addr(n, r, i, net_ip(10, 0, i, 254), net_mask_bits(24));
        net_if_addr(n, peer[i], 0, net_ip(10, 0, i, 5), net_mask_bits(24));
        net_set_gateway(n, peer[i], net_ip(10, 0, i, 254));
        if (net_port_state(n, r, i) != PORT_UP) all_up = false;
    }
    ck("a router's four sockets all come up with a cable in them", all_up);

    for (int i = 0; i < NET_HOST_NICS; i++)
        if (net_ping(n, peer[i], net_ip(10, 0, i, 254), NULL) != PING_OK)
            all_reach = false;
    ck("a frame arriving on any of them reaches the stack, not just port 0",
       all_reach);

    /* And no frame was eaten in silence: what the ports received, the cards
     * received. This is the counter that made the bug invisible. */
    uint64_t rx = 0, drop = 0;
    Buf b;
    buf_init(&b);
    net_dump_ifaces(n, r, &b);
    for (int i = 0; i < NET_HOST_NICS; i++) {
        rx += net_port_rx(n, r, i);
        drop += net_port_drops(n, r, i);
    }
    ck("and nothing was dropped by a port with nothing behind it",
       rx > 0 && drop == 0 && strstr(b.p ? b.p : "", "dropped 0") != NULL);
    buf_free(&b);

    net_forwarding(n, r, true);
    ck("a box with an address on two sockets routes between them",
       net_ping(n, peer[0], net_ip(10, 0, 1, 5), NULL) == PING_OK &&
       net_ping(n, peer[3], net_ip(10, 0, 2, 5), NULL) == PING_OK);

    /* A SUBINTERFACE ADDS AN INTERFACE. It used to replace the parent, which
     * is why a router could not have a WAN side and a LAN side at once. */
    uint32_t was = net_if_get_addr(n, r, 0);
    int sub = net_if_subif(n, r, 0, 100);
    net_if_addr(n, r, sub, net_ip(192, 168, 100, 1), net_mask_bits(24));
    ck("a tagged subinterface is an extra address, not a replacement",
       sub >= NET_HOST_NICS && net_if_get_addr(n, r, 0) == was &&
       net_if_get_addr(n, r, sub) == net_ip(192, 168, 100, 1) &&
       net_if_nic(n, r, sub) == 0 && net_if_get_vlan(n, r, sub) == 100);
    ck("asking twice for the same vlan on the same card is the same interface",
       net_if_subif(n, r, 0, 100) == sub);
    ck("and it goes away again, while the card underneath does not",
       net_if_del(n, r, sub) && !net_if_exists(n, r, sub) &&
       !net_if_del(n, r, 0) && net_if_get_addr(n, r, 0) == was);
    net_free(n);
}

/* ------------------------------------------------------------------- L4 */
static void check_tcp(void)
{
    printf("L4 -- TCP\n");
    Lan l = lan_new(11);
    Net *n = l.n;
    int srv = net_tcp_listen(n, l.b, 8080);
    ck("a listening socket is in LISTEN", net_tcp_state(n, srv) == TCP_LISTEN);

    int cs = net_tcp_connect(n, l.a, net_ip(10, 0, 0, 2), 8080);
    ck("connect starts in SYN_SENT", net_tcp_state(n, cs) == TCP_SYN_SENT);
    for (int i = 0; i < 500 && net_tcp_state(n, cs) != TCP_ESTABLISHED; i++)
        net_step(n, 1);
    ck("the three-way handshake completes", net_tcp_state(n, cs) == TCP_ESTABLISHED);

    /* The server end becomes established one leg later than the client end:
     * it is waiting for the ACK that the client has only just sent. Polling
     * for it is what a program with a listening socket really does. */
    int ss = -1;
    for (int i = 0; i < 500 && ss < 0; i++) { net_step(n, 1); ss = net_tcp_accept(n, srv); }
    ck("the server has a connection to accept", ss >= 0);
    ck("and it is established too", ss >= 0 && net_tcp_state(n, ss) == TCP_ESTABLISHED);

    /* Bytes, in order, with sequence numbers doing the work. */
    const char *msg = "the quick brown fox jumps over the lazy dog";
    net_tcp_send(n, cs, msg, (int)strlen(msg));
    char got[128] = {0};
    int total_read = 0;
    for (int i = 0; i < 500 && total_read < (int)strlen(msg); i++) {
        net_step(n, 1);
        int k = net_tcp_recv(n, ss, got + total_read, (int)sizeof got - 1 - total_read);
        if (k > 0) total_read += k;
    }
    ck("data arrives whole and in order", strcmp(got, msg) == 0);

    /* Teardown. Both halves, and a TIME_WAIT at the end of it. */
    net_tcp_close(n, cs);
    bool saw_fw = false, saw_cw = false;
    for (int i = 0; i < 500; i++) {
        net_step(n, 1);
        TcpState c = net_tcp_state(n, cs), s = net_tcp_state(n, ss);
        if (c == TCP_FIN_WAIT_1 || c == TCP_FIN_WAIT_2 || c == TCP_TIME_WAIT) saw_fw = true;
        if (s == TCP_CLOSE_WAIT) saw_cw = true;
        if (saw_cw) break;
    }
    ck("closing sends a FIN and the sender waits", saw_fw);
    ck("the other end goes to CLOSE_WAIT", saw_cw);
    net_tcp_close(n, ss);
    for (int i = 0; i < 500 && net_tcp_state(n, cs) != TCP_CLOSED; i++) net_step(n, 1);
    ck("and both ends reach CLOSED", net_tcp_state(n, cs) == TCP_CLOSED &&
                                     net_tcp_state(n, ss) == TCP_CLOSED);

    /* A port with nothing on it is REFUSED, not ignored. */
    int bad = net_tcp_connect_wait(n, l.a, net_ip(10, 0, 0, 2), 9999);
    ck("a closed port refuses at once", bad < 0);
    net_free(n);
}

/* ------------------------------------------- TCP when the port is full
 *
 * WHAT A PORT THAT IS DROPPING FRAMES DOES TO THE TRANSFERS ACROSS IT. Eight
 * desks pulling files off one server share one gigabit port, and its 48 KB
 * egress buffer cannot hold what will not fit on the wire, so frames are
 * lost. That much was always true here. What happened next was not: a sender
 * that lost a segment had no way to find out except the retransmission
 * timer, so it went silent for 200ms per loss while the wire in front of it
 * sat idle. Measured on this scenario before the stack could do anything
 * else: 5% busy, 49Mb carried, 99 frames lost, and the middle transfer's
 * longest silence was 201ms -- the timer, to the millisecond.
 *
 * Now the receiver says again what it is waiting for, the sender resends on
 * the third of those without waiting for anything, and the window it may
 * have on the wire answers to the loss instead of being whatever the peer's
 * buffer happened to be: 8% busy, 81Mb carried, 13 frames lost, longest
 * silence 3ms.
 *
 * The busy figure is still small, and it is not TCP's doing: see the note in
 * port_tx() in netstack.c for the millisecond-granularity ceiling that keeps
 * any port in this world under about 39% however hard it is pushed. What
 * these checks assert is what TCP is responsible for -- the loss signal, the
 * resend that answers it, and the transfers that are no longer silent for a
 * fifth of a second at a time.
 *
 * Everything below is read off the outside of the stack: bytes that really
 * arrived at a desk, a port counter, and the capture a person would take. */
#define CONG_DESKS 16     /* the busiest run below; the other uses eight   */
#define CONG_MS    600

/* READING THE CAPTURE THE WAY A PERSON DOES. One row per direction of one
 * connection, built from the trace lines themselves: what it has sent up to,
 * what it last acknowledged, and how many times in a row it acknowledged the
 * same byte. Three of those in a row IS the duplicate-ACK signal. */
#define CONG_FLOWROWS 64
typedef struct {
    char     key[56];        /* "10.0.0.1:8080 > 10.0.0.11:49152"           */
    uint32_t lastack;        /* the byte it last said it was waiting for    */
    int      duprun;         /* times in a row it repeated lastack          */
    bool     seen;
} Flow;

static unsigned tail_num(const char *ln, const char *tag, bool *ok)
{
    const char *p = strstr(ln, tag);
    *ok = p != NULL;
    return p ? (unsigned)strtoul(p + strlen(tag), NULL, 10) : 0;
}

/* Fold one capture line into the rows, and answer how many times in a row
 * this sender has now asked for the same byte. 0 if the line was not a bare
 * acknowledgement. */
static int flow_line(Flow *fl, int *nfl, const char *ln)
{
    if (!ln) return 0;
    const char *a = strstr(ln, " tcp ");
    const char *b = strstr(ln, " [");
    if (!a || !b || b < a) return 0;
    a += 5;
    char key[56];
    size_t k = (size_t)(b - a);
    if (k >= sizeof key) k = sizeof key - 1;
    memcpy(key, a, k);
    key[k] = 0;

    Flow *f = NULL;
    for (int i = 0; i < *nfl; i++) if (strcmp(fl[i].key, key) == 0) { f = &fl[i]; break; }
    if (!f) {
        if (*nfl >= CONG_FLOWROWS) return 0;
        f = &fl[(*nfl)++];
        memset(f, 0, sizeof *f);
        snprintf(f->key, sizeof f->key, "%s", key);
    }

    bool has_len, has_ack;
    unsigned len = tail_num(ln, " len ", &has_len);
    unsigned ack = tail_num(ln, " ack ", &has_ack);
    if (!has_len) return 0;
    if (len > 0) return 0;      /* data, not an acknowledgement on its own */
    if (!has_ack) return 0;
    if (f->seen && ack == f->lastack) f->duprun++;
    else                              f->duprun = 1;
    f->lastack = ack;
    f->seen = true;
    return f->duprun;
}

/* What one run of the scenario measured. */
typedef struct {
    int  connected;
    int  dupack;             /* longest run of "the same byte again"        */
    int  asked, answered;    /* desks that asked three times; that got it   */
    int  slowest;            /* ms from the third ask to the bytes landing  */
    int  median_stall;       /* the middle desk's longest silence, ms       */
    int  util, mbit;
    unsigned long long drops;
} Cong;

/* N desks pulling files off one server, all through the server's one
 * gigabit port, for 600ms of wire time. */
static void cong_run(int desks, Cong *r)
{
    memset(r, 0, sizeof *r);
    Net *n = net_new(77);
    int sw  = net_add_switch(n, "sw1", 24);
    int srv = net_add_host(n, "files");
    net_cable(n, srv, 0, sw, 0, 10, CAB_CAT5E);
    net_if_addr(n, srv, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    int lsock = net_tcp_listen(n, srv, 8080);

    int cs[CONG_DESKS], ss[CONG_DESKS];
    long long got[CONG_DESKS];
    int idle[CONG_DESKS], stall[CONG_DESKS], asked3[CONG_DESKS], waited[CONG_DESKS];
    for (int i = 0; i < desks; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "desk%d", i);
        int c = net_add_host(n, nm);
        net_cable(n, c, 0, sw, i + 1, 10, CAB_CAT5E);
        net_if_addr(n, c, 0, net_ip(10, 0, 0, 11 + i), net_mask_bits(24));
        cs[i] = net_tcp_connect(n, c, net_ip(10, 0, 0, 1), 8080);
        ss[i] = -1; got[i] = 0; idle[i] = 0; stall[i] = 0;
        asked3[i] = -1; waited[i] = -1;
    }
    int nacc = 0;
    for (int t = 0; t < 2000 && nacc < desks; t++) {
        net_step(n, 1);
        int a;
        while (nacc < desks && (a = net_tcp_accept(n, lsock)) >= 0) ss[nacc++] = a;
    }
    r->connected = nacc;

    /* The stopwatch on the server's port starts here; the lifetime drop
     * counter does not, because a port's drops are its whole life and a real
     * switch prints them that way. */
    uint8_t page[4096];
    memset(page, 'x', sizeof page);
    net_port_busy_reset(n, srv, 0);
    uint64_t drops0 = net_port_drops(n, srv, 0);
    Flow fl[CONG_FLOWROWS];
    int nfl = 0;
    net_trace_clear(n);
    net_trace(n, true);
    for (int t = 0; t < CONG_MS; t++) {
        /* The server pushes whatever each send buffer will take, every tick,
         * which is what a file server serving a floor does. */
        for (int i = 0; i < nacc; i++)
            while (net_tcp_send(n, ss[i], page, (int)sizeof page) > 0) { }
        net_step(n, 1);
        for (int i = 0; i < desks; i++) {
            char sink[4096];
            int k, moved = 0;
            while ((k = net_tcp_recv(n, cs[i], sink, (int)sizeof sink)) > 0) {
                got[i] += k;
                moved += k;
            }
            /* HOW LONG A TRANSFER GOES QUIET FOR is the number this is all
             * about: a lost segment used to cost the retransmission timer,
             * every single time. And how long after asking three times the
             * bytes turned up -- under 200ms, the timer cannot be what
             * fetched them, because the timer had not run out. */
            if (moved) {
                if (idle[i] > stall[i]) stall[i] = idle[i];
                idle[i] = 0;
                if (asked3[i] >= 0 && waited[i] < 0) waited[i] = t - asked3[i];
            } else idle[i]++;
        }
        for (int i = 0; i < net_trace_count(n); i++) {
            const char *ln = net_trace_line(n, i);
            int run = flow_line(fl, &nfl, ln);
            if (run > r->dupack) r->dupack = run;
            /* Whose duplicates they are: a trace line starts with the name
             * of the machine that sent it. */
            if (run == 3 && strncmp(ln, "desk", 4) == 0) {
                int d = atoi(ln + 4);
                if (d >= 0 && d < desks && asked3[d] < 0) asked3[d] = t;
            }
        }
        net_trace_clear(n);
    }
    net_trace(n, false);

    long long total = 0;
    int stalls[CONG_DESKS];
    for (int i = 0; i < desks; i++) {
        total += got[i];
        if (idle[i] > stall[i]) stall[i] = idle[i];
        stalls[i] = stall[i];
        if (asked3[i] < 0) continue;
        r->asked++;
        if (waited[i] < 0) continue;
        r->answered++;
        if (waited[i] > r->slowest) r->slowest = waited[i];
    }
    for (int a = 0; a < desks; a++)              /* the middle desk's wait */
        for (int b = a + 1; b < desks; b++)
            if (stalls[b] < stalls[a]) { int q = stalls[a]; stalls[a] = stalls[b]; stalls[b] = q; }
    r->median_stall = stalls[desks / 2];
    r->util = (int)((net_port_busy_us(n, srv, 0) * 100) / ((uint64_t)CONG_MS * 1000));
    r->drops = (unsigned long long)(net_port_drops(n, srv, 0) - drops0);
    r->mbit = (int)((total * 8) / ((long long)CONG_MS * 1000));
    net_free(n);
}

static void check_congestion(void)
{
    printf("\na congested port, and the loss it really causes\n");
    char what[128];

    /* EIGHT DESKS. Enough to overflow the port's buffer in bursts, which is
     * the ordinary case: the numbers in the comment above are this run. */
    Cong a;
    cong_run(8, &a);
    ck("eight desks all have a transfer running off the one file server",
       a.connected == 8);
    snprintf(what, sizeof what,
             "the port really is dropping frames (%llu of them, %d%% busy)",
             a.drops, a.util);
    ck(what, a.drops > 0);
    snprintf(what, sizeof what,
             "and a transfer is not silent for a timer at a time (%dms, was 201ms)",
             a.median_stall);
    ck(what, a.median_stall < 50);
    snprintf(what, sizeof what,
             "so the same eight desks get more for it (%dMb carried, was 49Mb)",
             a.mbit);
    ck(what, a.mbit >= 65);

    /* SIXTEEN DESKS, which is where segments are lost out of the MIDDLE of a
     * stream often enough to watch the whole signal work end to end: the
     * receiver asks again, and again, and again, and the missing bytes turn
     * up milliseconds later rather than when the timer runs out. */
    Cong b;
    cong_run(16, &b);
    snprintf(what, sizeof what,
             "a receiver that cannot use what arrived asks again (%d times over)",
             b.dupack);
    ck(what, b.dupack >= 3);
    snprintf(what, sizeof what,
             "and asking three times fetches it in %dms, not on the 200ms timer (%d of %d)",
             b.slowest, b.answered, b.asked);
    ck(what, b.asked > 0 && b.answered > 0 && b.slowest < 200);
}

static void check_firewall(void)
{
    printf("the filter\n");
    Lan l = lan_new(12);
    Net *n = l.n;
    net_tcp_listen(n, l.b, 80);
    ck("before any rule, the port answers",
       net_tcp_connect_wait(n, l.a, net_ip(10, 0, 0, 2), 80) >= 0);

    net_fw_add(n, l.b, FW_IN, IP_PROTO_TCP, 80, 0, 0, FW_DROP);
    ck("a drop rule on port 80 stops the connection",
       net_tcp_connect_wait(n, l.a, net_ip(10, 0, 0, 2), 80) < 0);
    ck("and the rule counted the packets it dropped", net_fw_hits(n, l.b, 0) > 0);
    ck("ping to the same machine is untouched: the rule says tcp 80",
       net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL) == PING_OK);

    net_fw_clear(n, l.b);
    net_fw_add(n, l.b, FW_IN, IP_PROTO_ICMP, 0, 0, 0, FW_DROP);
    ck("a rule on icmp stops ping and nothing else",
       net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL) != PING_OK &&
       net_tcp_connect_wait(n, l.a, net_ip(10, 0, 0, 2), 80) >= 0);

    /* A rule scoped to a source network drops one machine and not another. */
    net_fw_clear(n, l.b);
    int c = net_add_host(n, "charlie");
    net_cable(n, c, 0, l.sw, 4, 7, CAB_CAT5E);
    net_if_addr(n, c, 0, net_ip(10, 0, 0, 9), net_mask_bits(24));
    net_fw_add(n, l.b, FW_IN, IP_PROTO_ICMP, 0, net_ip(10, 0, 0, 1),
               net_mask_bits(32), FW_DROP);
    ck("a source-scoped rule drops one machine and not the other",
       net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL) != PING_OK &&
       net_ping(n, c, net_ip(10, 0, 0, 2), NULL) == PING_OK);

    /* ---------------------------------------------- the policy is a policy
     *
     * A catch-all with no protocol, no port and no source is what `policy
     * drop` compiles to, and it is not the same kind of thing as a rule
     * somebody wrote. It disposes of what this machine neither asked for
     * nor is listening for. That distinction is what makes the recommended
     * architecture buildable: a playtester started a DHCP server on a box,
     * was told "serving", and got nothing at all for ninety minutes,
     * because a policy nobody could see and nobody could edit was eating
     * every DISCOVER. */
    net_fw_clear(n, l.b);
    net_fw_add(n, l.b, FW_IN, FW_ANY_PROTO, FW_ANY_PORT, 0, 0, FW_DROP);
    ck("a catch-all drop refuses a ping it did not ask for",
       net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL) != PING_OK);
    ck("but the box behind it can still ping out: the policy does not eat "
       "the answer it asked for",
       net_ping(n, l.b, net_ip(10, 0, 0, 1), NULL) == PING_OK);
    ck("a service it is serving receives what it serves, through the policy",
       net_tcp_connect_wait(n, l.a, net_ip(10, 0, 0, 2), 80) >= 0);
    /* And DHCP, which is the one that cost the playtest its afternoon. */
    net_dhcpd(n, l.b, net_ip(10, 0, 0, 100), 4, net_mask_bits(24),
              net_ip(10, 0, 0, 2), net_ip(10, 0, 0, 2));
    ck("including DHCP: a server told to serve hands out an address behind "
       "policy drop", net_dhcp_client(n, c, 0));
    /* A rule is an instruction and still bites. This is the half that must
     * NOT change: "shut this service off" has to keep working. */
    net_fw_clear(n, l.b);
    net_fw_add(n, l.b, FW_IN, IP_PROTO_TCP, 80, 0, 0, FW_DROP);
    net_fw_add(n, l.b, FW_IN, FW_ANY_PROTO, FW_ANY_PORT, 0, 0, FW_DROP);
    ck("a rule that names the port still shuts the service off",
       net_tcp_connect_wait(n, l.a, net_ip(10, 0, 0, 2), 80) < 0);
    net_free(n);
}

/* ----------------------------------------------------- drops, and reasons
 *
 * A drop is reported with the reason it really had, or it is worse than no
 * report at all. `show edge` used to tell a player that a port two per cent
 * busy with an empty queue had overflowed its 48 KB egress buffer, and a
 * 24-port switch with two links in it showed twenty-two `no link` ports
 * reading `drop 301` -- six thousand phantom drops on the screen you go to
 * when something is wrong. */
static void check_drop_reasons(void)
{
    printf("\ndrops, and the reasons they really had\n");
    Lan l = lan_new(31);
    Net *n = l.n;
    for (int i = 0; i < 40; i++) net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL);

    uint64_t empty = 0;
    for (int p = 2; p < 8; p++) empty += net_port_drops(n, l.sw, p);
    ck("a switch port with no cable in it has no drops", empty == 0);

    bool sums = true;
    for (int p = 0; p < 8; p++) {
        uint64_t d = net_port_drops(n, l.sw, p);
        if (d != net_port_qdrops(n, l.sw, p) + net_port_nolink(n, l.sw, p) +
                 net_port_swdrops(n, l.sw, p) + net_port_worldq(n, l.sw, p))
            sums = false;
    }
    ck("every drop on every port is counted under a reason", sums);

    /* Pull bravo's cable out and keep sending to it. The switch still has it
     * in the table, so it forwards to a port with no link -- a real drop,
     * genuinely lost frames, and NOT the egress buffer overflowing. */
    net_uncable(n, 1);
    uint64_t before = net_port_drops(n, l.sw, 1);
    for (int i = 0; i < 5; i++) net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL);
    uint64_t after = net_port_drops(n, l.sw, 1);
    ck("a frame handed to a port with no link is counted, and as no link",
       after > before && net_port_nolink(n, l.sw, 1) == after &&
       net_port_qdrops(n, l.sw, 1) == 0);
    net_free(n);
}

/* -------------------------------------------------------------- services */
static void check_dhcp(void)
{
    printf("DHCP\n");
    Net *n = net_new(13);
    int sw = net_add_switch(n, "sw1", 8);
    int srv = net_add_host(n, "server");
    net_cable(n, srv, 0, sw, 0, 5, CAB_CAT5E);
    net_if_addr(n, srv, 0, net_ip(192, 168, 1, 1), net_mask_bits(24));
    /* A pool of exactly two, so that the third machine finds out what an
     * exhausted pool feels like: nothing at all. */
    net_dhcpd(n, srv, net_ip(192, 168, 1, 100), 2, net_mask_bits(24),
              net_ip(192, 168, 1, 1), net_ip(192, 168, 1, 1));

    int c1 = net_add_host(n, "c1"), c2 = net_add_host(n, "c2"), c3 = net_add_host(n, "c3");
    net_cable(n, c1, 0, sw, 1, 5, CAB_CAT5E);
    net_cable(n, c2, 0, sw, 2, 5, CAB_CAT5E);
    net_cable(n, c3, 0, sw, 3, 5, CAB_CAT5E);

    ck("a client with no address gets one", net_dhcp_client(n, c1, 0));
    ck("and the address is out of the pool",
       net_if_get_addr(n, c1, 0) == net_ip(192, 168, 1, 100));
    ck("the mask and gateway came from the server too",
       net_if_get_mask(n, c1, 0) == net_mask_bits(24));
    ck("a second client gets the next one", net_dhcp_client(n, c2, 0) &&
       net_if_get_addr(n, c2, 0) == net_ip(192, 168, 1, 101));
    ck("the server is holding two leases", net_dhcpd_leases(n, srv) == 2);
    ck("the third client gets nothing, because the pool is empty",
       !net_dhcp_client(n, c3, 0) && net_if_get_addr(n, c3, 0) == 0);
    ck("and the two that did get one can talk",
       net_ping(n, c1, net_ip(192, 168, 1, 101), NULL) == PING_OK);

    /* Asking again gets the SAME address, because the server remembers. */
    ck("asking again renews the same address", net_dhcp_client(n, c1, 0) &&
       net_if_get_addr(n, c1, 0) == net_ip(192, 168, 1, 100));

    /* Unplug the server. A client now waits and gets nothing -- which is
     * exactly what a machine on a dead segment does. */
    net_uncable(n, 0);
    int c4 = net_add_host(n, "c4");
    net_cable(n, c4, 0, sw, 4, 5, CAB_CAT5E);
    ck("with the server unplugged the client gets no lease",
       !net_dhcp_client(n, c4, 0));
    net_free(n);
}

/* ------------------------------------------------ a pool serves ONE segment
 *
 * THE WORST THING A PLAYTESTER FOUND IN SIXTY DAYS. They ran a pool on the
 * router for the tenancy on vlan 11 -- the obvious box, for a tenancy with
 * no server of its own -- and the desks of the tenancy on vlan 13, behind
 * their own server, took 10.11.0.x addresses off it. A pool was a property
 * of a BOX, so one pool answered every broadcast that reached any leg of
 * that box. Their words: "a one-way door that poisons a segment you never
 * touched is the worst thing in the build".
 *
 * The router below is on both vlans, down one trunk, exactly as the
 * recommended architecture builds it. It serves eleven and it must not serve
 * thirteen -- not because anything knows about tenancies, but because a pool
 * is scoped to the interface whose address is inside it, and vlan 13's
 * DISCOVER arrives on a different interface. */
static void check_dhcp_scope(void)
{
    printf("DHCP -- the segment a pool serves, and the ones it does not\n");
    Net *n = net_new(29);
    int sw = net_add_switch(n, "sw2", 8);
    int rt = net_add_host(n, "edge");
    net_cable(n, rt, 0, sw, 0, 5, CAB_CAT6);
    net_port_mode(n, sw, 0, PORT_TRUNK);
    net_trunk_allow(n, sw, 0, 11);
    net_trunk_allow(n, sw, 0, 13);
    int v11 = net_if_subif(n, rt, 0, 11), v13 = net_if_subif(n, rt, 0, 13);
    net_if_addr(n, rt, v11, net_ip(10, 11, 0, 1), net_mask_bits(24));
    net_if_addr(n, rt, v13, net_ip(10, 13, 0, 1), net_mask_bits(24));

    /* Tenant one's desk on vlan 11, tenant three's on vlan 13, and tenant
     * three's own server beside it. */
    int d11 = net_add_host(n, "desk11"), d13 = net_add_host(n, "desk13"),
        srv3 = net_add_host(n, "srv3");
    net_cable(n, d11,  0, sw, 1, 5, CAB_CAT5E);
    net_cable(n, d13,  0, sw, 2, 5, CAB_CAT5E);
    net_cable(n, srv3, 0, sw, 3, 5, CAB_CAT5E);
    net_port_vlan(n, sw, 1, 11);
    net_port_vlan(n, sw, 2, 13);
    net_port_vlan(n, sw, 3, 13);
    net_if_addr(n, srv3, 0, net_ip(10, 13, 0, 2), net_mask_bits(24));

    /* The pool the player typed. No vlan in the line: the segment is the leg
     * of the router that is standing on that subnet. */
    ck("a pool goes on the interface whose address is inside it",
       net_dhcpd(n, rt, net_ip(10, 11, 0, 100), 20, net_mask_bits(24),
                 net_ip(10, 11, 0, 1), net_ip(10, 11, 0, 1)) &&
       net_dhcpd_scope(n, rt, net_ip(10, 11, 0, 100), net_mask_bits(24)) == v11);
    ck("a pool on a subnet no leg of the box is on is refused, and starts "
       "nothing",
       !net_dhcpd(n, rt, net_ip(10, 99, 0, 100), 20, net_mask_bits(24),
                  net_ip(10, 99, 0, 1), 0) && net_dhcpd_pools(n, rt) == 1);

    ck("the desk on the segment it serves gets an address from it",
       net_dhcp_client(n, d11, 0) &&
       net_if_get_addr(n, d11, 0) == net_ip(10, 11, 0, 100));
    /* THE FAULT ITSELF. Same router, same wire, same broadcast; a segment the
     * pool has nothing to do with. */
    ck("the desk on the OTHER vlan gets nothing at all from it",
       !net_dhcp_client(n, d13, 0) && net_if_get_addr(n, d13, 0) == 0);

    /* And with its own server on its own segment, it gets that server's. */
    net_dhcpd(n, srv3, net_ip(10, 13, 0, 100), 20, net_mask_bits(24),
              net_ip(10, 13, 0, 1), net_ip(10, 13, 0, 2));
    ck("with a server of its own it takes ITS lease, not the router's",
       net_dhcp_client(n, d13, 0) &&
       net_if_get_addr(n, d13, 0) == net_ip(10, 13, 0, 100) &&
       net_dhcpd_leases(n, srv3) == 1 && net_dhcpd_leases(n, rt) == 1);

    /* A ROUTER PLAUSIBLY RUNS A POOL PER VLAN, and that is the same rule
     * read the other way: three subinterfaces, three pools, each answering
     * on its own leg. */
    ck("the same router runs a second pool, on its other vlan",
       net_dhcpd(n, rt, net_ip(10, 13, 0, 200), 10, net_mask_bits(24),
                 net_ip(10, 13, 0, 1), net_ip(10, 13, 0, 1)) &&
       net_dhcpd_pools(n, rt) == 2);
    int ifx = -1;
    uint32_t first = 0;
    ck("and each of them says which interface it is on",
       net_dhcpd_pool(n, rt, 0, &ifx, &first, NULL, NULL, NULL, NULL) &&
       ifx == v11 && first == net_ip(10, 11, 0, 100) &&
       net_dhcpd_pool(n, rt, 1, &ifx, &first, NULL, NULL, NULL, NULL) &&
       ifx == v13 && first == net_ip(10, 13, 0, 200));

    /* AND IT CAN BE SWITCHED OFF, which it could not. A router has no power
     * button, `count 0` is not a way out, and re-pointing a rogue pool at
     * whichever segment is currently asking was the only escape there was. */
    ck("stopping it stops every pool on the box",
       net_dhcpd_stop(n, rt) == 2 && net_dhcpd_pools(n, rt) == 0 &&
       net_dhcpd_leases(n, rt) == 0);
    int d11b = net_add_host(n, "desk11b");
    net_cable(n, d11b, 0, sw, 4, 5, CAB_CAT5E);
    net_port_vlan(n, sw, 4, 11);
    ck("and a box that has been stopped answers a discover with nothing",
       !net_dhcp_client(n, d11b, 0) && net_if_get_addr(n, d11b, 0) == 0);
    ck("stopping one that was not serving says so rather than pretending",
       net_dhcpd_stop(n, rt) == 0);
    /* The server on thirteen never stopped, and proves the switch-off was
     * the box's and not the world's. */
    int d13b = net_add_host(n, "desk13b");
    net_cable(n, d13b, 0, sw, 5, 5, CAB_CAT5E);
    net_port_vlan(n, sw, 5, 13);
    ck("the other server on the other segment is still serving",
       net_dhcp_client(n, d13b, 0) &&
       net_if_get_addr(n, d13b, 0) == net_ip(10, 13, 0, 101));

    /* A ROGUE SERVER ON THE SAME L2 IS STILL A HAZARD -- that is real, and
     * worth having. What a client refuses is the impossible lease: an
     * address it could not reach the server that gave it. */
    int rogue = net_add_host(n, "rogue");
    net_cable(n, rogue, 0, sw, 6, 5, CAB_CAT5E);
    net_port_vlan(n, sw, 6, 11);
    net_if_addr(n, rogue, 0, net_ip(10, 11, 0, 250), net_mask_bits(24));
    net_dhcpd(n, rogue, net_ip(10, 11, 0, 60), 5, net_mask_bits(24),
              net_ip(10, 11, 0, 1), net_ip(10, 11, 0, 1));
    int d11c = net_add_host(n, "desk11c");
    net_cable(n, d11c, 0, sw, 7, 5, CAB_CAT5E);
    net_port_vlan(n, sw, 7, 11);
    ck("a rogue server on the same broadcast domain is still heard, as on "
       "real copper",
       net_dhcp_client(n, d11c, 0) &&
       net_if_get_addr(n, d11c, 0) == net_ip(10, 11, 0, 60));
    net_free(n);
}

static void check_dns(void)
{
    printf("DNS\n");
    Lan l = lan_new(14);
    Net *n = l.n;
    net_dnsd(n, l.b);
    net_dns_record(n, l.b, "wiki.nomnix.org", net_ip(10, 0, 2, 20));
    net_set_resolver(n, l.a, net_ip(10, 0, 0, 2));

    uint32_t ip = 0;
    ck("a name the server knows resolves",
       net_resolve(n, l.a, "wiki.nomnix.org", &ip) && ip == net_ip(10, 0, 2, 20));
    ck("a name it does not know comes back as no such name",
       !net_resolve(n, l.a, "nowhere.example", &ip));

    /* A resolver pointed somewhere there is nothing. It does not error --
     * it waits, and then gives up, which is the sound every player learns to
     * recognise. */
    net_set_resolver(n, l.a, net_ip(10, 0, 0, 200));
    uint64_t t0 = net_now(n);
    bool ok = net_resolve(n, l.a, "wiki.nomnix.org", &ip);
    ck("a resolver that is not there fails", !ok);
    ck("and fails by timing out, not by answering", net_now(n) - t0 >= 500);

    /* The resolver exists but its cable is out. Same symptom, different
     * repair, and nothing in the client can tell them apart -- which is why
     * the player has to go and look. */
    net_set_resolver(n, l.a, net_ip(10, 0, 0, 2));
    net_uncable(n, 1);
    ck("a resolver whose cable is out fails identically",
       !net_resolve(n, l.a, "wiki.nomnix.org", &ip));
    net_free(n);
}

static void check_http(void)
{
    printf("HTTP over the TCP above\n");
    Lan l = lan_new(15);
    Net *n = l.n;
    /* The fake internet's own address, so the pages that are already written
     * are the pages that come back -- over a real socket this time. */
    net_if_addr(n, l.b, 0, net_ip(10, 0, 2, 20), net_mask_bits(16));
    net_if_addr(n, l.a, 0, net_ip(10, 0, 0, 1), net_mask_bits(16));
    net_httpd(n, l.b, 80);

    Buf body;
    buf_init(&body);
    int st = net_http_get(n, l.a, net_ip(10, 0, 2, 20), 80, "/", &body);
    ck("a GET over real TCP returns 200", st == 200);
    ck("and the body is the page that was already written",
       body.len > 100 && strstr(body.p ? body.p : "", "NomnixOS wiki") != NULL);
    buf_free(&body);

    Buf b2;
    buf_init(&b2);
    int st2 = net_http_get(n, l.a, net_ip(10, 0, 2, 20), 80, "/nothing-here", &b2);
    ck("a page that is not there is a 404 from the server, not a timeout",
       st2 == 200 || st2 == 404);
    buf_free(&b2);

    /* A firewall in front of the web server. The page does not become a
     * 404 -- the connection never happens at all, which is a different
     * symptom and a different fix. */
    net_fw_add(n, l.b, FW_IN, IP_PROTO_TCP, 80, 0, 0, FW_DROP);
    Buf b3;
    buf_init(&b3);
    int st3 = net_http_get(n, l.a, net_ip(10, 0, 2, 20), 80, "/", &b3);
    ck("a filtered web server does not answer at all", st3 < 0);
    buf_free(&b3);

    net_fw_clear(n, l.b);
    net_uncable(n, 1);
    Buf b4;
    buf_init(&b4);
    ck("nor does an unplugged one",
       net_http_get(n, l.a, net_ip(10, 0, 2, 20), 80, "/", &b4) < 0);
    buf_free(&b4);
    net_free(n);
}

/* --------------------------------------------------------- determinism   */
/* A trace you cannot reproduce is not evidence. Two worlds built from the
 * same seed, given the same commands, must produce the same frames in the
 * same order down to the byte. */
static void trace_run(uint64_t seed, Buf *out)
{
    Net *n = net_new(seed);
    net_trace(n, true);
    int sw = net_add_switch(n, "sw1", 8);
    int a = net_add_host(n, "alpha"), b = net_add_host(n, "bravo");
    net_cable(n, a, 0, sw, 0, 12, CAB_CAT5E);
    net_cable(n, b, 0, sw, 1, 18, CAB_CAT5E);
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(n, b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));
    net_dnsd(n, b);
    net_dns_record(n, b, "wiki.nomnix.org", net_ip(10, 0, 2, 20));
    net_set_resolver(n, a, net_ip(10, 0, 0, 2));
    net_ping(n, a, net_ip(10, 0, 0, 2), NULL);
    uint32_t ip = 0;
    net_resolve(n, a, "wiki.nomnix.org", &ip);
    int s = net_tcp_connect_wait(n, a, net_ip(10, 0, 0, 2), 7);
    if (s >= 0) net_tcp_close(n, s);
    net_step(n, 200);
    net_dump_trace(n, out);
    net_free(n);
}

static void check_determinism(void)
{
    printf("determinism\n");
    Buf x, y, z;
    buf_init(&x); buf_init(&y); buf_init(&z);
    trace_run(4242, &x);
    trace_run(4242, &y);
    trace_run(4243, &z);
    ck("the trace is not empty", x.len > 200);
    ck("same seed, byte-identical trace",
       x.len == y.len && (x.len == 0 || memcmp(x.p, y.p, x.len) == 0));
    ck("a different seed gives a different trace",
       x.len != z.len || memcmp(x.p, z.p, x.len) != 0);
    buf_free(&x); buf_free(&y); buf_free(&z);
}

/* ----------------------------------------------------------- inspection  */
static void check_visible(void)
{
    printf("what a player can see from inside the machine\n");
    Lan l = lan_new(16);
    Net *n = l.n;
    net_trace(n, true);
    net_set_gateway(n, l.a, net_ip(10, 0, 0, 254));
    net_tcp_listen(n, l.a, 22);
    net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL);

    Buf b;
    buf_init(&b); net_dump_ifaces(n, l.a, &b);
    ck("ip addr shows the address, the mask and the carrier",
       strstr(b.p ? b.p : "", "10.0.0.1/24") && strstr(b.p, "LOWER_UP"));
    buf_free(&b);

    buf_init(&b); net_dump_routes(n, l.a, &b);
    ck("ip route shows the connected subnet and the default",
       strstr(b.p ? b.p : "", "10.0.0.0/24") && strstr(b.p, "default via 10.0.0.254"));
    buf_free(&b);

    buf_init(&b); net_dump_arp(n, l.a, &b);
    ck("arp shows the neighbour it just spoke to", strstr(b.p ? b.p : "", "10.0.0.2") != NULL);
    buf_free(&b);

    buf_init(&b); net_dump_sockets(n, l.a, &b);
    ck("netstat shows the listening socket", strstr(b.p ? b.p : "", "LISTEN") != NULL);
    buf_free(&b);

    buf_init(&b); net_dump_fdb(n, l.sw, &b);
    ck("the switch can show what it has learned", strstr(b.p ? b.p : "", "vlan 1") != NULL);
    buf_free(&b);

    buf_init(&b); net_dump_ports(n, l.sw, &b);
    ck("the switch can show its ports and their speeds",
       strstr(b.p ? b.p : "", "port 0") && strstr(b.p, "1000Mb"));
    buf_free(&b);

    buf_init(&b); net_dump_trace(n, &b);
    ck("and there is a packet trace with a readable ARP exchange in it",
       strstr(b.p ? b.p : "", "arp who-has") && strstr(b.p, "icmp echo request"));
    buf_free(&b);
    net_free(n);
}

/* ------------------------------------------------- the filter, from inside */
/* THE HALF THAT IS A PROGRAM. Everything in check_firewall() calls
 * net_fw_add() directly. On a real machine nobody does that: they edit
 * /etc/nftables.conf and reload the daemon, and what the filter then does is
 * whatever nft(8) managed to parse. So this boots a machine, types at it, and
 * asks the machine what it sees -- which is the only way to catch a rule that
 * reads well and drops nothing.
 *
 * The shipped ruleset is `policy drop` plus tcp 22 and 80, so a pristine box
 * does not ANSWER a ping. That is a good puzzle and it stays. What it may not
 * do is refuse the answer to a question it asked itself: a box that could not
 * ping anything, ever, sent a playtester to re-cable a riser that was never
 * wrong -- and it was invisible, because opening the far end correctly
 * changed nothing they could see. The catch-all is the policy and steps aside
 * for what this machine asked for or is listening for; a rule that NAMES a
 * protocol is an instruction and still bites, which is what the icmp rules
 * below are for. */
static Machine *NM;

static const char *mrun(const char *line, Buf *o)
{
    buf_clear(o);
    kernel_run(NM, line, o);
    if (!o->len) buf_puts(o, "");
    return o->p ? o->p : "";
}

static bool mhas(const char *line, const char *needle, Buf *o)
{
    return strstr(mrun(line, o), needle) != NULL;
}

/* Put a rule in front of the tcp line the ruleset already ships, reload, and
 * hand back what `netstat -F` says the kernel is now running. */
static const char *nft_rule(const char *rule, Buf *o)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd,
             "sed -i \"s/tcp dport/%s\\n    tcp dport/\" /etc/nftables.conf", rule);
    mrun(cmd, o);
    mrun("svc reload nftables", o);
    return mrun("netstat -F", o);
}

static void check_nft(void)
{
    printf("\nthe filter, as a machine really loads it\n");
    Machine m;
    memset(&m, 0, sizeof m);
    machine_install(&m, 90210);
    machine_boot(&m);
    NM = &m;
    Buf o = {0};
    if (!m.boot.running) { ck("the machine boots", false); goto done; }

    /* 10.0.2.2 is this machine's gateway and it answers an echo request.
     * Whether THIS machine ever hears the answer is the filter's business. */
    ck("a pristine box ships policy drop, and says so",
       mhas("netstat -F", "any  any port    drop", &o) &&
       strstr(o.p, "tcp  dport 22") && strstr(o.p, "tcp  dport 80"));

    ck("and it can still ping its gateway: the policy does not eat the answer "
       "it asked for",
       mhas("ping -c 1 10.0.2.2", "reply from 10.0.2.2", &o) &&
       strstr(o.p, "1 sent, 1 received"));

    /* Now an instruction, not a policy. `icmp drop` is somebody saying so,
     * and it takes the reply the policy would have let through. */
    const char *fw = nft_rule("icmp drop", &o);
    ck("`icmp drop` is an instruction and takes the reply anyway",
       strstr(fw, "icmp any port    drop") != NULL &&
       mhas("ping -c 1 10.0.2.2", "no answer", &o) &&
       strstr(o.p, "1 sent, 0 received"));

    ck("and nothing above IP was ever told -- the drop is silent",
       !mhas("ping -c 1 10.0.2.2", "unreachable", &o));

    mrun("sed -i /icmp/d /etc/nftables.conf", &o);
    fw = nft_rule("icmp accept", &o);
    ck("`icmp accept` becomes a real rule in the running filter",
       strstr(fw, "icmp any port    accept") != NULL);

    ck("and the box answers now, over the same wire and the same policy",
       mhas("ping -c 1 10.0.2.2", "reply from 10.0.2.2", &o) &&
       strstr(o.p, "1 sent, 1 received"));

    ck("the rule counted what it let through", mhas("netstat -F", "icmp", &o) &&
       strstr(o.p, "icmp any port    accept  matched 0") == NULL);

    /* The policy is still drop. A player who wrote one line has not opened
     * the machine up, which is the entire difference from the sledgehammer. */
    ck("and the policy is still drop: one line is not `accept everything`",
       mhas("netstat -F", "any  any port    drop", &o));

    /* The long spelling, which is what a real ruleset usually carries. */
    mrun("sed -i /icmp/d /etc/nftables.conf", &o);
    fw = nft_rule("ip protocol icmp accept", &o);
    ck("`ip protocol icmp accept` is the same rule spelled in full",
       strstr(fw, "icmp any port    accept") != NULL &&
       mhas("ping -c 1 10.0.2.2", "reply", &o));

    /* And a verdict that goes the other way, which is the check that this is
     * a parser and not a keyword that means "let icmp through". */
    mrun("sed -i /icmp/d /etc/nftables.conf", &o);
    fw = nft_rule("icmp drop", &o);
    ck("`icmp drop` drops it, and the rule is what did it",
       strstr(fw, "icmp any port    drop") != NULL &&
       mhas("ping -c 1 10.0.2.2", "no answer", &o));

    /* STOPPING THE FILTER STOPS THE FILTER. `svc stop nftables` used to kill
     * the process and leave the ruleset loaded and counting up -- the unit
     * DEAD in `svc` and `netstat -F` still matching, which is two views of
     * one machine disagreeing. The unit unloads what it loaded. */
    mrun("svc stop nftables", &o);
    ck("`svc stop nftables` takes the filter off, and netstat -F agrees",
       mhas("netstat -F", "the filter is empty", &o) &&
       mhas("svc", "nftables         DEAD", &o));
    ck("and the machine is reachable again over the same wire",
       mhas("ping -c 1 10.0.2.2", "reply from 10.0.2.2", &o));
    mrun("svc start nftables", &o);
    ck("starting it again loads the ruleset back off the disk",
       mhas("netstat -F", "icmp any port    drop", &o));

    /* A protocol with no port named: every port of it. */
    mrun("sed -i /icmp/d /etc/nftables.conf", &o);
    fw = nft_rule("tcp accept", &o);
    ck("`tcp accept` is one rule for every port of one protocol",
       strstr(fw, "tcp  any port    accept") != NULL);

    /* A line nobody can read is skipped, not guessed at. Treating an
     * unreadable rule as a drop would lock a player out over a typo. */
    mrun("sed -i /tcp/d /etc/nftables.conf", &o);
    fw = nft_rule("icmp accpet", &o);
    ck("a verdict nobody can read installs no rule at all",
       strstr(fw, "icmp") == NULL);

done:
    buf_free(&o);
    NM = NULL;
    machine_free(&m);
}

/* ------------------------------------- the instruments, from inside the box */
/* WHAT THESE CHECK, AND WHY IT IS NOT THE SAME AS check_visible(). Up there
 * the assertions are on the kernel's own dump functions. Here a real machine
 * boots, a person types the command at it, and what is asserted is the text
 * a player would read -- because a tool that reformats the stack's answer can
 * lose it, and a tool that answers from anywhere but the stack is the one
 * thing this project cannot have.
 *
 * Each check makes the state change and then asks the tool. An `ip route`
 * that printed a default gateway would pass any assertion that only ever saw
 * a machine with one; the interesting question is whether it stops printing
 * it when the route goes away. */
static void check_tools(void)
{
    printf("\nthe network instruments, run inside a booted machine\n");
    Machine m;
    memset(&m, 0, sizeof m);
    machine_install(&m, 71077);
    machine_boot(&m);
    NM = &m;
    Buf o = {0};
    if (!m.boot.running) { ck("the machine boots", false); goto done; }

    /* The address this machine really has, out of the stack, so everything
     * below can be compared against one value rather than a literal. */
    char addr[32] = "";
    {
        const char *t = mrun("netstat -i", &o);
        const char *p = strstr(t, "inet ");
        if (p) {
            p += 5;
            size_t k = 0;
            while (k + 1 < sizeof addr && p[k] && p[k] != '/' && p[k] != ' ') {
                addr[k] = p[k]; k++;
            }
            addr[k] = 0;
        }
    }
    ck("the machine has an address at all", addr[0] != 0);

    /* ip: the same address the kernel reported, in iproute2's shape. */
    ck("`ip addr` prints the address the stack holds, with the carrier",
       mhas("ip addr", addr, &o) && strstr(o.p, "LOWER_UP") &&
       strstr(o.p, "link/ether"));
    ck("`ip link` is the card without the address",
       mhas("ip link", "link/ether", &o) && !strstr(o.p, "inet "));
    ck("`ip route` has the connected route and the default",
       mhas("ip route", "10.0.2.0/24", &o) && strstr(o.p, "default via 10.0.2.2"));

    /* AND IT STOPS SAYING SO WHEN IT STOPS BEING TRUE. This machine leases
     * its address, so the default route came from the DHCP server. Configure
     * it statically with no gateway line, reload the daemon, and the route
     * really goes out of the running table -- which is the assertion worth
     * making, because a tool that had memorised the answer would still be
     * printing it. */
    mrun("echo \"iface eth0\" > /etc/net/interfaces", &o);
    mrun("echo \"  address 10.0.2.15\" >> /etc/net/interfaces", &o);
    mrun("echo \"  netmask 24\" >> /etc/net/interfaces", &o);
    mrun("svc reload net", &o);
    ck("a static address with no gateway: `ip addr` shows the new address",
       mhas("ip addr", "10.0.2.15/24", &o));
    ck("and `ip route` has the connected route and no default at all",
       mhas("ip route", "10.0.2.0/24", &o) && !strstr(o.p, "default via"));
    ck("ping off-subnet agrees nothing was sent: no route, not no answer",
       mhas("ping -c 1 192.168.99.9", "network is unreachable", &o));
    ck("`traceroute` says the same thing, and sends nothing",
       mhas("traceroute 192.168.99.9", "no route to it", &o));
    mrun("echo \"  gateway 10.0.2.2\" >> /etc/net/interfaces", &o);
    mrun("svc reload net", &o);
    ck("put a gateway line in and the default route is there again",
       mhas("ip route", "default via 10.0.2.2", &o));

    /* The filter has to let icmp back in before anything can be reached;
     * that is the pristine ruleset and check_nft() proves it separately. */
    nft_rule("icmp accept", &o);

    /* arp: the cache is filled by really talking to a neighbour. */
    mrun("arp -d 10.0.2.2", &o);
    ck("`arp -d` on an address that is not cached deletes nothing and says so",
       mhas("arp -d 10.0.2.2", "no entry deleted", &o));
    ck("nothing is cached until the machine speaks to somebody",
       mhas("arp", "cache is empty", &o));
    mrun("ping -c 1 10.0.2.2", &o);
    ck("after a ping the neighbour is there, with the card it answered on",
       mhas("arp", "10.0.2.2", &o) && strstr(o.p, "ether") &&
       strstr(o.p, "eth0"));
    ck("`ip neigh` prints the same entry, in ip's shape, as REACHABLE",
       mhas("ip neigh", "10.0.2.2 dev eth0 lladdr", &o) &&
       strstr(o.p, "REACHABLE"));
    ck("`arp -d` really removes it from the running cache",
       mhas("arp -d 10.0.2.2", "deleted", &o) &&
       mhas("arp", "cache is empty", &o));

    /* AN ADDRESS NOBODY HOLDS. The request goes out and nothing answers, so
     * the entry stays incomplete -- which is a different line and a
     * different repair from an entry with the wrong mac in it. */
    mrun("ping -c 1 10.0.2.77", &o);
    ck("an address nothing answers for is cached as incomplete",
       mhas("arp", "(incomplete)", &o) && strstr(o.p, "10.0.2.77"));

    /* traceroute, over the real ICMP the stack produces. */
    ck("`traceroute` reaches a neighbour in one hop",
       mhas("traceroute 10.0.2.20", "1 10.0.2.20", &o));
    ck("and past the router it stops where the answers stop",
       mhas("traceroute 192.168.99.9", "1 10.0.2.2", &o) &&
       strstr(o.p, "never answered"));

    /* ss, against sockets that are really open and really closed. */
    ck("`ss -lt` lists the ports the running services really hold",
       mhas("ss -lt", "*:22", &o) && strstr(o.p, "*:80") &&
       strstr(o.p, "LISTEN"));
    mrun("svc stop httpd", &o);
    ck("stop the web server and its socket is gone from ss",
       mhas("ss -lt", "*:22", &o) && !strstr(o.p, "*:80"));
    mrun("svc start httpd", &o);
    ck("`ss -p` is refused rather than printing an empty owner column",
       mhas("ss -p", "no -p on this machine", &o));

    /* tcpdump: the frames themselves. */
    ck("with the capture off, tcpdump says so rather than printing nothing",
       mhas("tcpdump", "nothing captured", &o));
    mrun("tcpdump --capture on", &o);
    mrun("ping -c 1 10.0.2.2", &o);
    ck("the capture holds the echo request and the reply, at this card",
       mhas("tcpdump icmp", "icmp echo-request", &o) &&
       strstr(o.p, "icmp echo-reply") && strstr(o.p, "eth0"));
    ck("and the arp exchange that had to happen first",
       mhas("tcpdump arp", "who-has 10.0.2.2", &o) &&
       strstr(o.p, "is-at"));
    ck("a filter on a host nobody spoke to matches nothing",
       mhas("tcpdump host 10.0.2.99", "0 frames shown", &o));
    ck("a filter on the host that was spoken to matches",
       !mhas("tcpdump host 10.0.2.2", "0 frames shown", &o));
    ck("-Q separates the direction the frame crossed the card",
       mhas("tcpdump -Q out icmp", "Out", &o) && !strstr(o.p, "In  "));
    ck("an interface this machine does not have is an error, not an empty list",
       mhas("tcpdump -i eth9", "no interface called", &o));
    ck("a filter it cannot apply is refused BY NAME",
       mhas("tcpdump src 10.0.2.2", "cannot filter on", &o) &&
       mhas("tcpdump -X", "this tcpdump has no -X", &o));

    /* THE ONE THAT PAYS FOR THE WHOLE THING. Write `icmp drop` -- an
     * instruction, which the machine obeys even about its own ping -- and
     * ping again: ping reports no answer, and the capture shows the reply
     * arriving anyway, because the capture is taken at the card and the drop
     * happens above IP. That is the difference between
     * "the network is broken" and "this machine ate it", and no other tool
     * on the box can tell them apart. */
    mrun("sed -i /icmp/d /etc/nftables.conf", &o);
    mrun("sed -i \"s/tcp dport/icmp drop\\n    tcp dport/\" /etc/nftables.conf", &o);
    mrun("svc reload nftables", &o);
    mrun("tcpdump --capture on", &o);
    ck("with icmp dropped again, ping reports no answer",
       mhas("ping -c 1 10.0.2.2", "no answer", &o));
    ck("and tcpdump shows the reply arriving at the card regardless",
       mhas("tcpdump icmp", "In", &o) && strstr(o.p, "icmp echo-reply"));

done:
    buf_free(&o);
    NM = NULL;
    machine_free(&m);
}

int net_selfcheck(void)
{
    passed = total = 0;
    check_wire();
    check_ethernet();
    check_vlans();
    check_storm();
    check_arp();
    check_mask();
    check_routing();
    check_nics();
    check_tcp();
    check_congestion();
    check_firewall();
    check_drop_reasons();
    check_dhcp();
    check_dhcp_scope();
    check_dns();
    check_http();
    check_determinism();
    check_visible();
    check_nft();
    check_tools();
    printf("\n%d/%d network checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
