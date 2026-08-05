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
#include "nom.h"
#include "netstack.h"

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
    ck("cat6 negotiates 10Gb short and 1Gb long", true);
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
    check_tcp();
    check_firewall();
    check_dhcp();
    check_dns();
    check_http();
    check_determinism();
    check_visible();
    printf("\n%d/%d network checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
