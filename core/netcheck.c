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
/* Put a booted machine on a node of a net this file built, so that the last
 * voice checks are a person typing on the desk rather than a call into the
 * stack. core/session.c does exactly this when somebody pulls a chair out. */
void netsite_pin(Machine *m, struct Net *n, int node);

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

    /* THE ALLOWED SET IS A SET, AND IT CAN BE READ AND UNSET.
     *
     * It was one uint32_t: vlans 1..32 and silence for everything else,
     * while a subinterface has always been allowed to wear 1..4094. So a
     * trunk told to carry vlan 300 said nothing and carried nothing. And
     * there was no way to read it back and no way to take a vlan off. */
    net_port_vlan(m, s1, 0, 300);
    net_port_vlan(m, s2, 0, 300);
    net_arp_flush(m, x);
    ck("vlan 300 does not cross a trunk that was not told about it",
       net_ping(m, x, net_ip(10, 0, 0, 2), NULL) != PING_OK);
    ck("and telling it about 300 is accepted, not swallowed",
       net_trunk_allow(m, s1, 7, 300) && net_trunk_allow(m, s2, 7, 300));
    net_arp_flush(m, x);
    ck("a vlan above 32 crosses the trunk like any other",
       net_ping(m, x, net_ip(10, 0, 0, 2), NULL) == PING_OK);
    ck("4094 is a vlan and 4095 is not",
       net_trunk_allow(m, s1, 7, 4094) && !net_trunk_allow(m, s1, 7, 4095) &&
       !net_trunk_allow(m, s1, 7, 0));
    ck("the trunk reads back the ids it was given, in order",
       net_trunk_allows(m, s1, 7, 30) && net_trunk_allows(m, s1, 7, 300) &&
       !net_trunk_allows(m, s1, 7, 31));
    {
        int got[8], k = net_trunk_allowed(m, s1, 7, got, 8);
        ck("and counts them: three vlans, ascending",
           k == 3 && got[0] == 30 && got[1] == 300 && got[2] == 4094);
        k = net_trunk_allowed(m, s1, 7, got, 1);
        ck("a caller with room for one is told there are three",
           k == 3 && got[0] == 30);
    }
    ck("denying one takes it back off, and the frames stop",
       net_trunk_deny(m, s1, 7, 300) && !net_trunk_allows(m, s1, 7, 300) &&
       net_ping(m, x, net_ip(10, 0, 0, 2), NULL) != PING_OK);
    ck("and the vlans beside it are untouched",
       net_trunk_allows(m, s1, 7, 30) && net_trunk_allows(m, s1, 7, 4094));
    net_trunk_clear(m, s1, 7);
    ck("clearing empties the set without changing the native vlan",
       net_trunk_allowed(m, s1, 7, NULL, 0) == 0 &&
       net_port_state(m, s1, 7) == PORT_UP);
    {
        Buf o = {0};
        net_trunk_allow(m, s1, 7, 11);
        net_trunk_allow(m, s1, 7, 12);
        net_trunk_allow(m, s1, 7, 13);
        net_trunk_allow(m, s1, 7, 20);
        net_dump_trunk(m, s1, 7, &o);
        ck("and it prints as a switch prints it: runs collapsed, ids in order",
           o.p && strstr(o.p, "native 1 allows 11-13,20 (4 vlans)") != NULL);
        buf_free(&o);
    }
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
    /* The trunk between them is the cheap drum, because a pair of unmanaged
     * switches with no spanning tree in them is the kit that is on it. */
    net_cable(n, s1, 6, s2, 6, 20, CAB_CAT5);
    net_cable(n, s1, 7, s2, 7, 20, CAB_CAT5);
    net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(n, b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));

    /* One ordinary ping. One broadcast ARP request is all it takes. */
    net_ping(n, a, net_ip(10, 0, 0, 2), NULL);
    /* WHERE A STORM IS VISIBLE. On the ports carrying it, as time on the
     * wire: the stopwatch on one trunk starts once the ping is answered, so
     * everything it counts from here is the echo of a conversation that
     * finished.
     *
     * THIS CHECK USED TO ASSERT DROPPED FRAMES and it no longer can, which
     * is worth writing down rather than quietly retuning. A storm was
     * visible as drops because every frame a port was offered inside one
     * millisecond arrived at the same instant, so a port could accept one
     * bufferful a tick and threw the rest away -- see the model note in
     * port_tx(). With the offer instant advancing at line rate, these two
     * switches with two hosts on them circulate about 139 frames a
     * millisecond, which is half of a hundred megabit run and not more than
     * it: nothing overflows, and asserting that it did would be asserting an
     * artefact. What a storm really costs here is the wire, so that is what
     * is measured -- and it is the number `load` and `netstat -P` print, so
     * it is a number the player is really looking at. */
    net_port_busy_reset(n, s1, 6);
    net_step(n, 400);
    uint64_t load = net_load(n);
    int stormpct = (int)(net_port_busy_us(n, s1, 6) * 100 / (400 * 1000));
    char sl[110];
    ck("a loop with no spanning tree storms", load > 200);
    snprintf(sl, sizeof sl, "and the storm is visible on the wire it is going "
             "round: the trunk is %d%% busy with nobody talking", stormpct);
    ck(sl, stormpct >= 25);
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
 * WHAT A PORT THAT IS DROPPING FRAMES DOES TO THE TRANSFERS ACROSS IT. N
 * desks pull files off one server, and the server is behind a hundred
 * megabit run, so past a certain number of desks its egress buffer cannot
 * hold what will not fit on the wire and frames are lost. What used to
 * happen next was the fault: a sender that lost a segment had no way to find
 * out except the retransmission timer, so it went silent for 200ms per loss
 * while the wire in front of it sat idle. Reno -- duplicate acknowledgements
 * and a fast retransmit on the third of them -- is what answers that, and
 * these checks are the measurement of it.
 *
 * WHY THE NUMBERS HERE ARE NOT THE ONES THAT USED TO BE IN THIS COMMENT.
 * This scenario used to put eight desks behind a GIGABIT port and call it
 * congested, and it really did drop frames -- for an arithmetic reason
 * rather than one on the wire. Every frame a port was offered inside one
 * millisecond arrived at the same instant, so a port could accept one
 * bufferful a tick, 48 KB, and threw away everything handed over after it;
 * no port in this world could read above about 39% busy however hard it was
 * pushed, and this run read 8%. With the offer instant advancing at line
 * rate (the model note in port_tx()), one gigabit carries sixteen desks
 * pulling files without losing anything, which is true and is what the game
 * should say. So the bottleneck is now where it really lives: the cheapest
 * drum in the catalogue, and the two runs below are the same wire on either
 * side of what it holds.
 *
 * Everything below is read off the outside of the stack: bytes that really
 * arrived at a desk, a port counter, and the capture a person would take. */
#define CONG_DESKS 16     /* the array is sized for the largest run tried  */
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
    /* THE SERVER IS ON THE OLD COPPER, and that is the whole scenario: the
     * desks are on gigabit legs and the thing they are all pulling from is
     * behind a hundred megabit run. It used to be on cat5e here, and back
     * then a gigabit port congested at eight desks -- but it congested for
     * an arithmetic reason (everything offered inside a millisecond arrived
     * at the same instant, so a port could accept one bufferful a tick and
     * no more) and not for a reason on the wire. With that fixed, one
     * gigabit really does carry sixteen desks pulling files, which is true
     * and is what the game should say. So the congestion is put where it
     * really lives: on the cheapest drum in the catalogue. */
    net_cable(n, srv, 0, sw, 0, 10, CAB_CAT5);
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

/* ------------------------------------- what the utilisation number means
 *
 * THE NUMBER `load` PRINTS IS READ AND ACTED ON, so it has to be the thing
 * it is named after: the fraction of the second that this port spent
 * clocking bits. This asserts it end to end and in the open -- a known
 * number of bytes is put on a wire of a known speed, and the busy figure is
 * compared against the division. It is the one check that would have caught
 * the ceiling described in port_tx(): under that model a port could not read
 * above about 39% whatever it was handed, so the second half of this would
 * have failed by a factor of two and the third by a factor of three. */
static void check_utilisation(void)
{
    printf("\nwhat the utilisation number means\n");
    char what[128];
    /* Two boxes, one gigabit run, and datagrams that are simply handed to
     * the port -- no window, no congestion control, nothing between the
     * sender and the wire to argue about the rate. */
    for (int pct = 25; pct <= 100; pct += 25) {
        Net *n = net_new(9);
        int a = net_add_host(n, "alpha"), b = net_add_host(n, "bravo");
        net_cable(n, a, 0, b, 0, 10, CAB_CAT5E);   /* 1000Mb                */
        net_if_addr(n, a, 0, net_ip(10, 0, 0, 1), net_mask_bits(24));
        net_if_addr(n, b, 0, net_ip(10, 0, 0, 2), net_mask_bits(24));
        int s = net_udp_open(n, a, 4000);
        net_udp_open(n, b, 4000);
        net_ping(n, a, net_ip(10, 0, 0, 2), NULL);      /* resolve first    */
        net_port_busy_reset(n, a, 0);

        /* A 1400-byte datagram is a 1442-byte frame, which port_tx clocks
         * out in 12us of a gigabit. Handing over k of them per millisecond
         * asks for k*1.2% of the wire; 83 of them is the whole of it. */
        uint8_t buf[1400];
        memset(buf, 'x', sizeof buf);
        int per_ms = (83 * pct) / 100;
        const int ms = 200;
        for (int t = 0; t < ms; t++) {
            for (int k = 0; k < per_ms; k++)
                net_udp_send(n, s, net_ip(10, 0, 0, 2), 4000, buf, (int)sizeof buf);
            net_step(n, 1);
        }
        int util = (int)(net_port_busy_us(n, a, 0) * 100 / ((uint64_t)ms * 1000));
        snprintf(what, sizeof what,
                 "a gigabit port offered %d%% of a gigabit reads %d%% busy",
                 pct, util);
        /* Within three points: the frame time is rounded up to the whole
         * microsecond in port_tx, and per_ms is a whole number of frames. */
        ck(what, util >= pct - 3 && util <= pct + 1);
        net_free(n);
    }
}

static void check_congestion(void)
{
    printf("\na congested port, and the loss it really causes\n");
    char what[128];

    /* EIGHT DESKS, WHICH THE WIRE JUST CARRIES. Eight transfers is a little
     * under a hundred megabits, so the port runs at the top of its range and
     * TCP finds that rate without losing anything: 98% busy, 93Mb through,
     * no drops at all. That is the honest shape of a link that is exactly
     * big enough, and it is worth asserting because for a long time this
     * stack could not produce it -- a port could not read above 39% however
     * hard it was pushed, so "full" and "overflowing" were the same picture.
     * They are two pictures now and this is the first one. */
    Cong a;
    cong_run(8, &a);
    ck("eight desks all have a transfer running off the one file server",
       a.connected == 8);
    snprintf(what, sizeof what,
             "the hundred megabit run they share really fills up (%d%% busy)",
             a.util);
    ck(what, a.util >= 90);
    snprintf(what, sizeof what,
             "and it carries what a hundred megabit run carries (%dMb)", a.mbit);
    ck(what, a.mbit >= 85 && a.mbit <= 100);
    snprintf(what, sizeof what,
             "and a wire that is full but not overflowing loses nothing (%llu drops)",
             a.drops);
    ck(what, a.drops == 0);

    /* TWELVE DESKS, WHICH IT DOES NOT. Half as much again as the wire holds
     * is where segments start being lost out of the MIDDLE of a stream, and
     * that is the whole signal working end to end: the buffer overflows, the
     * receiver asks again and again and again, and the missing bytes turn up
     * milliseconds later rather than when the timer runs out.
     *
     * TWELVE AND NOT SIXTEEN, and the reason is measured rather than
     * convenient. At sixteen desks -- sixty per cent more than the wire
     * holds -- whole windows go missing, and a sender with nothing left in
     * flight has no acknowledgements coming back to count, so the
     * retransmission timer is the only thing that can recover it: the
     * slowest recovery at sixteen desks is 201ms, which IS the timer, and
     * every desk's median silence is 204ms. That is correct go-back-N
     * behaviour under heavy loss and not a fault. Fast retransmit is the
     * thing being checked here, so the run is the one where the loss is
     * light enough for duplicate acknowledgements to exist. */
    Cong b;
    cong_run(12, &b);
    snprintf(what, sizeof what,
             "half as much again as it holds does overflow it (%llu frames lost)",
             b.drops);
    ck(what, b.drops > 0);
    snprintf(what, sizeof what,
             "a receiver that cannot use what arrived asks again (%d times over)",
             b.dupack);
    ck(what, b.dupack >= 3);
    snprintf(what, sizeof what,
             "and asking three times fetches it in %dms, not on the 200ms timer (%d of %d)",
             b.slowest, b.answered, b.asked);
    ck(what, b.asked > 0 && b.answered == b.asked && b.slowest < 200);
    snprintf(what, sizeof what,
             "so a transfer is not silent for a timer at a time (%dms, was 201ms)",
             b.median_stall);
    ck(what, b.median_stall < 50);
}

/* =================================================================== voice
 *
 * THE POINT OF THIS WHOLE SECTION IN ONE SENTENCE: a call and a file
 * transfer are hurt by different things, so a network that is fine for one
 * can be useless for the other, and nothing in the stack is allowed to know
 * that in advance.
 *
 * The building below is the smallest one in which that is true. A core
 * switch in the basement with the file server and the phone system on
 * gigabit legs; a floor switch upstairs with the desks and one handset on
 * it; and between them one riser of the cheap copper, which negotiates a
 * hundred megabits. The riser is not undersized for the voice -- one call
 * is 86 kilobits, a thousandth of it -- and it is not undersized for one
 * transfer either. It is undersized for eight desks all pulling at once,
 * and the call is on the wrong side of that.
 *
 * The same call is measured three times over: on an idle riser, while the
 * floor pulls its files, and after they stop. Nothing about the call
 * changes between the three. Everything about the numbers does.
 */
#define VOICE_DESKS 20
typedef struct {
    Net *n;
    int core, floorsw, files, pbx, handset, deskphone;
    int desk[VOICE_DESKS], cs[VOICE_DESKS], ss[VOICE_DESKS];
    int lsock, nacc, nd;
    int riser;                /* the call across the riser                 */
    int inhouse;              /* a call that never touches it              */
} Voice;

static void voice_build(Voice *v, int desks, int circuit_mb)
{
    memset(v, 0, sizeof *v);
    v->nd = desks;
    Net *n = v->n = net_new(31);
    v->core    = net_add_switch(n, "core", 16);
    v->floorsw = net_add_switch(n, "floor3", 24);
    /* THE RISER, and it is the ordinary drum: 35 metres of cat5, which comes
     * up at a hundred megabits. Every other leg here is cat5e at a gigabit,
     * so this is the only narrow place in the building. */
    net_cable(n, v->core, 0, v->floorsw, 0, 35, CAB_CAT5);
    /* Or a circuit somebody is paying by the megabit for, which is what the
     * ISP handoff is: the same port, told what it may clock. */
    if (circuit_mb) net_port_rate(n, v->core, 0, circuit_mb);

    v->files     = net_add_host(n, "files");
    v->pbx       = net_add_host(n, "pbx");
    v->deskphone = net_add_host(n, "phone1");
    net_cable(n, v->files,     0, v->core, 1, 8, CAB_CAT5E);
    net_cable(n, v->pbx,       0, v->core, 2, 8, CAB_CAT5E);
    net_cable(n, v->deskphone, 0, v->core, 3, 8, CAB_CAT5E);
    net_if_addr(n, v->files,     0, net_ip(10, 0, 0, 1), net_mask_bits(24));
    net_if_addr(n, v->pbx,       0, net_ip(10, 0, 0, 2), net_mask_bits(24));
    net_if_addr(n, v->deskphone, 0, net_ip(10, 0, 0, 3), net_mask_bits(24));

    v->handset = net_add_host(n, "phone3");
    net_cable(n, v->handset, 0, v->floorsw, 1, 12, CAB_CAT5E);
    net_if_addr(n, v->handset, 0, net_ip(10, 0, 0, 4), net_mask_bits(24));
    for (int i = 0; i < v->nd; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "desk%d", i);
        v->desk[i] = net_add_host(n, nm);
        net_cable(n, v->desk[i], 0, v->floorsw, i + 2, 10, CAB_CAT5E);
        net_if_addr(n, v->desk[i], 0, net_ip(10, 0, 0, 11 + i), net_mask_bits(24));
        v->cs[i] = v->ss[i] = -1;
    }
    v->lsock = net_tcp_listen(n, v->files, 8080);
    /* Resolve the addresses before anything is measured. A first packet held
     * back waiting for an ARP reply is a real delay and it is not the delay
     * being measured here. */
    net_ping(n, v->pbx, net_ip(10, 0, 0, 4), NULL);
    net_ping(n, v->pbx, net_ip(10, 0, 0, 3), NULL);
    for (int i = 0; i < v->nd; i++)
        net_ping(n, v->files, net_ip(10, 0, 0, 11 + i), NULL);
    v->riser   = net_voice_call(n, v->pbx, v->handset,   net_ip(10, 0, 0, 4));
    v->inhouse = net_voice_call(n, v->pbx, v->deskphone, net_ip(10, 0, 0, 3));
}

/* Run the world for `ms`, with the file server pushing into every accepted
 * connection on every tick and every desk draining what arrived -- which is
 * what a floor pulling its files off a server does. With no connections
 * open, this is just time passing on an empty wire. */
static void voice_spin(Voice *v, int ms)
{
    uint8_t page[4096];
    char sink[4096];
    memset(page, 'x', sizeof page);
    for (int t = 0; t < ms; t++) {
        for (int i = 0; i < v->nacc; i++)
            if (v->ss[i] >= 0)
                while (net_tcp_send(v->n, v->ss[i], page, (int)sizeof page) > 0) { }
        net_step(v->n, 1);
        int a;
        while (v->nacc < v->nd && (a = net_tcp_accept(v->n, v->lsock)) >= 0)
            v->ss[v->nacc++] = a;
        for (int i = 0; i < v->nd; i++)
            if (v->cs[i] >= 0)
                while (net_tcp_recv(v->n, v->cs[i], sink, (int)sizeof sink) > 0) { }
    }
}

static void check_voice(void)
{
    printf("\nvoice: a call is not a transfer\n");
    char what[160];
    Voice v;
    voice_build(&v, VOICE_DESKS, 0);
    Net *n = v.n;

    /* -------------------------------------------------- one: an idle riser */
    voice_spin(&v, 1000);
    VoiceStats idle, idle2;
    net_voice_stats(n, v.riser, &idle);
    net_voice_stats(n, v.inhouse, &idle2);
    snprintf(what, sizeof what,
             "a G.711 call is real datagrams on the wire (%u sent, %u arrived)",
             idle.sent, idle.received);
    ck(what, idle.sent >= 45 && idle.received >= idle.sent - 2);
    snprintf(what, sizeof what,
             "and 20ms apart, so a second of it is fifty packets (%u in 1000ms)",
             idle.sent);
    ck(what, idle.sent >= 48 && idle.sent <= 52);
    snprintf(what, sizeof what,
             "on an empty riser it loses nothing (%u lost, %u late)",
             idle.lost, idle.late);
    ck(what, idle.lost == 0 && idle.late == 0);
    snprintf(what, sizeof what,
             "and arrives evenly: jitter %u.%ums", idle.jitter_us / 1000,
             (idle.jitter_us / 100) % 10);
    ck(what, idle.jitter_us < 2000);
    snprintf(what, sizeof what,
             "one way is the path's own cost and no more (%u.%ums, best %u.%ums)",
             idle.delay_avg_us / 1000, (idle.delay_avg_us / 100) % 10,
             idle.delay_min_us / 1000, (idle.delay_min_us / 100) % 10);
    ck(what, idle.delay_avg_us < 6000 &&
             idle.delay_avg_us >= idle.delay_min_us);
    {
        Buf o; buf_init(&o);
        net_voice_verdict(n, v.riser, &o);
        ck("and the verdict on it is that the call is clear",
           strstr(o.p, "verdict: clear") != NULL);
        buf_free(&o);
    }

    /* ------------------------------------------- two: the floor pulls files */
    net_voice_reset(n, v.riser);
    net_voice_reset(n, v.inhouse);
    for (int i = 0; i < v.nd; i++)
        v.cs[i] = net_tcp_connect(n, v.desk[i], net_ip(10, 0, 0, 1), 8080);
    uint64_t q0 = net_port_qdrops(n, v.core, 0);
    net_port_busy_reset(n, v.core, 0);
    voice_spin(&v, 1500);
    VoiceStats load, load2;
    net_voice_stats(n, v.riser, &load);
    net_voice_stats(n, v.inhouse, &load2);
    int util = (int)(net_port_busy_us(n, v.core, 0) * 100 / 1500000ull);
    snprintf(what, sizeof what,
             "twenty desks pulling files fill the riser they share (%d%% busy, "
             "%llu frames dropped)", util,
             (unsigned long long)(net_port_qdrops(n, v.core, 0) - q0));
    ck(what, util >= 90 && net_port_qdrops(n, v.core, 0) > q0);
    snprintf(what, sizeof what,
             "the SAME call now loses audio (%u of %u, was 0)",
             load.concealed, load.expected);
    ck(what, load.concealed > 0 && load.expected > 0);
    snprintf(what, sizeof what,
             "enough of it to hear: %d.%d%% concealed, over the 1%% that is audible",
             load.conceal_ppm / 10000, (load.conceal_ppm / 1000) % 10);
    ck(what, load.conceal_ppm >= 10000);
    snprintf(what, sizeof what,
             "and it arrives unevenly now: jitter %u.%ums, was %u.%ums",
             load.jitter_us / 1000, (load.jitter_us / 100) % 10,
             idle.jitter_us / 1000, (idle.jitter_us / 100) % 10);
    ck(what, load.jitter_us > idle.jitter_us * 2 + 200);
    snprintf(what, sizeof what,
             "and later: %u.%ums one way, was %u.%ums",
             load.delay_avg_us / 1000, (load.delay_avg_us / 100) % 10,
             idle.delay_avg_us / 1000, (idle.delay_avg_us / 100) % 10);
    ck(what, load.delay_avg_us > idle.delay_avg_us);

    /* THE PART THAT IS NOT A THROUGHPUT NUMBER. Every transfer is still
     * moving; the wire is busy, not broken; and the call is the thing that
     * is unusable. If the only measure in this game were bytes carried,
     * nothing here would show up at all. */
    long long moved = 0;
    for (int i = 0; i < v.nd; i++) if (v.ss[i] >= 0) moved++;
    snprintf(what, sizeof what,
             "meanwhile every transfer is still running (%lld of %d connected)",
             moved, v.nd);
    ck(what, moved == v.nd);

    /* WHOSE FAULT IT IS, named by the stack rather than deduced by a person.
     * The attribution is recorded on this call's own packets as they were
     * queued and thrown away, so it names the riser port and not merely the
     * busiest port in the building. */
    snprintf(what, sizeof what,
             "the stack names the port that threw the audio away (%s port %d, %u frames)",
             load.drop_node >= 0 ? net_node_name(n, load.drop_node) : "-",
             load.drop_port, load.drops);
    ck(what, load.drop_node == v.core && load.drop_port == 0 && load.drops > 0);
    {
        Buf o; buf_init(&o);
        net_voice_verdict(n, v.riser, &o);
        ck("and says so in words, with the port in them",
           (strstr(o.p, "verdict: poor") || strstr(o.p, "verdict: unusable")) &&
           strstr(o.p, "core port 0") != NULL);
        buf_free(&o);
    }

    /* The listing a tool would print: both of this phone system's calls, on
     * the same page, with the one that is in trouble reading differently
     * from the one that is not. */
    {
        Buf o; buf_init(&o);
        net_dump_voice(n, v.pbx, &o);
        int lines = 0;
        for (const char *q = o.p; (q = strchr(q, '\n')) != NULL; q++) lines++;
        ck("both of the pbx's calls are listed, and only its calls",
           lines == 3 && strstr(o.p, "phone3") && strstr(o.p, "phone1") &&
           !strstr(o.p, "desk"));
        buf_free(&o);
    }

    /* THE CONTROL. The second call goes from the same phone system to a
     * handset on the core switch, so its audio never crosses the riser. It
     * is running through the same busy building, on the same stack, at the
     * same instant, and it is fine -- which is the proof that what ruined
     * the first one was the shared port and not the hour of the day. */
    snprintf(what, sizeof what,
             "a call that does NOT cross that port is untouched (%u lost, %u late)",
             load2.lost, load2.late);
    ck(what, load2.lost == 0 && load2.late == 0 && load2.received > 60);

    /* ----------------------------------------------- three: they stop again */
    for (int i = 0; i < v.nd; i++) {
        if (v.cs[i] >= 0) net_tcp_close(n, v.cs[i]);
        if (v.ss[i] >= 0) net_tcp_close(n, v.ss[i]);
        v.cs[i] = v.ss[i] = -1;
    }
    v.nacc = 0;
    voice_spin(&v, 200);
    net_voice_reset(n, v.riser);
    voice_spin(&v, 1000);
    VoiceStats after;
    net_voice_stats(n, v.riser, &after);
    snprintf(what, sizeof what,
             "the transfers stop and the call recovers on its own (%u lost, "
             "jitter %u.%ums)", after.lost, after.jitter_us / 1000,
             (after.jitter_us / 100) % 10);
    ck(what, after.lost == 0 && after.late == 0 && after.jitter_us < 2000);
    {
        Buf o; buf_init(&o);
        net_voice_verdict(n, v.riser, &o);
        ck("and the verdict goes back to clear, with nobody resetting anything",
           strstr(o.p, "verdict: clear") != NULL);
        buf_free(&o);
    }

    /* ------------------------------------------------- a call to nowhere */
    int dead = net_voice_call(n, v.pbx, v.files, net_ip(10, 0, 9, 9));
    voice_spin(&v, 400);
    VoiceStats ds;
    net_voice_stats(n, dead, &ds);
    ck("a call to an address the routing cannot reach is a stream at 100% loss",
       ds.sent > 0 && ds.received == 0);
    {
        Buf o; buf_init(&o);
        net_voice_verdict(n, dead, &o);
        ck("and the verdict says not one packet arrived, not that it is quiet",
           strstr(o.p, "verdict: dead") != NULL);
        buf_free(&o);
    }
    net_voice_stop(n, dead);

    /* ------------------------------- the calls belong to the machines */
    int before = net_voice_count(n);
    net_close_all(n, v.handset);
    snprintf(what, sizeof what,
             "rebooting a phone hangs up on it (%d calls, was %d)",
             net_voice_count(n), before);
    ck(what, net_voice_count(n) == before - 1);
    net_free(n);
}

/* ---------------------------------------------- the other way a call dies
 *
 * The riser above kills a call by THROWING PACKETS AWAY: at a hundred
 * megabits the whole 48 KB buffer is under four milliseconds deep, so the
 * audio that gets through is barely later than it was and the audio that
 * does not is simply gone. That is what congestion looks like on a LAN, and
 * it is the only shape of it a building of gigabit copper can make.
 *
 * A circuit somebody is paying for by the megabit is a different illness out
 * of the same buffer. 48 KB is 65ms deep at six megabits and 196ms at two,
 * so the packets that survive come out of it a fifth of a second later than
 * they went in -- and the two cases below are the two ways that ruins a
 * call, neither of which loses a single packet by itself:
 *
 *   - a queue that is always full delays every packet by the SAME amount,
 *     which loses nothing, jitters by nothing, and makes a conversation
 *     that people talk over. G.114 puts the limit at 150ms one way.
 *   - a queue that is sometimes full and sometimes not delays them by
 *     DIFFERENT amounts, and a receiver holding 60ms of audio to smooth the
 *     wire out has nowhere to put one that arrives after its turn. It
 *     arrived. It is still silence.
 *
 * Both are the same arithmetic in port_tx as the riser. Only the rate
 * differs, and the rate is what somebody paid for. */
static void check_voice_circuit(void)
{
    printf("\nvoice: a narrow circuit, where late is as bad as lost\n");
    char what[160];

    /* ------------------------------------------- one: a standing queue */
    Voice v;
    voice_build(&v, 4, 2);          /* four desks over two megabits */
    Net *n = v.n;
    voice_spin(&v, 300);
    VoiceStats clear;
    net_voice_stats(n, v.riser, &clear);
    snprintf(what, sizeof what,
             "an idle two megabit circuit carries a call perfectly well "
             "(%u.%ums one way, %u lost)", clear.delay_avg_us / 1000,
             (clear.delay_avg_us / 100) % 10, clear.lost);
    ck(what, clear.lost == 0 && clear.late == 0 && clear.delay_avg_us < 6000);

    for (int i = 0; i < v.nd; i++)
        v.cs[i] = net_tcp_connect(n, v.desk[i], net_ip(10, 0, 0, 1), 8080);
    voice_spin(&v, 500);
    net_voice_reset(n, v.riser);
    voice_spin(&v, 3000);
    VoiceStats s;
    net_voice_stats(n, v.riser, &s);
    snprintf(what, sizeof what,
             "four desks fill it, and the queue stands full (%u.%ums on %s port %d)",
             s.queue_us / 1000, (s.queue_us / 100) % 10,
             s.queue_node >= 0 ? net_node_name(n, s.queue_node) : "-", s.queue_port);
    ck(what, s.queue_us > 150000 && s.queue_node == v.core && s.queue_port == 0);
    /* THE ONE THAT NO THROUGHPUT MEASURE CAN SEE. Not a packet lost, not a
     * packet late, jitter of two milliseconds -- and the call is unusable,
     * because every word takes a fifth of a second to arrive and the two
     * people talk over each other. A file transfer does not notice this at
     * all; it is the same bytes, slightly later. */
    snprintf(what, sizeof what,
             "not one packet is lost or late (%u lost, %u late, jitter %u.%ums)",
             s.lost, s.late, s.jitter_us / 1000, (s.jitter_us / 100) % 10);
    ck(what, s.lost == 0 && s.late == 0 && s.jitter_us < 10000);
    snprintf(what, sizeof what,
             "and the call is still unusable: %u.%ums one way, past G.114's 150",
             s.delay_avg_us / 1000, (s.delay_avg_us / 100) % 10);
    ck(what, s.delay_avg_us > 150000);
    snprintf(what, sizeof what,
             "and the BEST it ever managed was %u.%ums: the queue never drains",
             s.delay_min_us / 1000, (s.delay_min_us / 100) % 10);
    ck(what, s.delay_min_us > 130000 && clear.delay_avg_us < 6000);
    {
        Buf o; buf_init(&o);
        net_voice_verdict(n, v.riser, &o);
        ck("the verdict names the delay and the port, with nothing lost to blame",
           strstr(o.p, "talk over each other") != NULL &&
           strstr(o.p, "core port 0") != NULL);
        buf_free(&o);
    }
    net_free(n);

    /* ------------------------------------------ two: a queue that swings */
    voice_build(&v, 8, 4);          /* eight desks over four megabits */
    n = v.n;
    voice_spin(&v, 300);
    for (int i = 0; i < v.nd; i++)
        v.cs[i] = net_tcp_connect(n, v.desk[i], net_ip(10, 0, 0, 1), 8080);
    voice_spin(&v, 500);
    net_voice_reset(n, v.riser);
    voice_spin(&v, 3000);
    VoiceStats t;
    net_voice_stats(n, v.riser, &t);
    snprintf(what, sizeof what,
             "eight desks over four megabits make it swing instead (%u.%ums "
             "best, %u.%ums worst)", t.delay_min_us / 1000,
             (t.delay_min_us / 100) % 10, t.delay_max_us / 1000,
             (t.delay_max_us / 100) % 10);
    ck(what, t.delay_max_us - t.delay_min_us > 60000);
    snprintf(what, sizeof what,
             "which is jitter a receiver can measure: %u.%ums",
             t.jitter_us / 1000, (t.jitter_us / 100) % 10);
    ck(what, t.jitter_us > 3000);
    /* THE ARITHMETIC THAT TURNS A LATE PACKET INTO BAD AUDIO. Nothing
     * decides that these are bad. They arrived after the millisecond at
     * which the receiver had to play them, and there is only one thing a
     * receiver can do with a sound whose moment has gone. */
    snprintf(what, sizeof what,
             "%u packets ARRIVED and are still silence: they missed their turn "
             "in the %dms buffer", t.late, NET_VOICE_JITTER_MS);
    ck(what, t.late > 0);
    snprintf(what, sizeof what,
             "more audio is lost to lateness than to loss (%u late, %u lost)",
             t.late, t.lost);
    ck(what, t.late > t.lost);
    snprintf(what, sizeof what,
             "%u of %u audio frames concealed, and only %u packets were "
             "actually dropped", t.concealed, t.expected, t.lost);
    ck(what, t.concealed == t.late + t.lost && t.concealed > t.lost);
    {
        Buf o; buf_init(&o);
        net_voice_verdict(n, v.riser, &o);
        const char *ver = strstr(o.p, "verdict:");
        ck("and the verdict blames the lateness, not the loss",
           ver && strstr(ver, "too late to play") && strstr(ver, "core port 0"));
        buf_free(&o);
    }
    net_free(n);
}

/* ============ voice: what is left to read after the calls have ended ======
 *
 * THE DEAD END THIS SECTION EXISTS TO CLOSE. A playtester sat down at a call
 * centre agent's desk on a day the tenancy scored 0 of 18 calls with 29% of
 * its audio concealed. `ping` was 3 of 3 at 8ms with no loss, `traceroute`
 * was two clean hops, `ip addr` read 20,175 packets and 0 dropped. Every one
 * of those answers was true: the desk's own card dropped nothing, because
 * the audio was thrown away on somebody else's port -- and the calls were
 * over, so there was no stream left to read.
 *
 * Everything above this comment measures a call while it is UP, which is the
 * one time nobody is sitting in that chair. So the checks below hang the
 * calls up first, exactly as the end of a busy period does, and then ask the
 * machines what they can still say.
 */
static void check_voice_log(void)
{
    printf("\nvoice: what the desk can still read after the calls are over\n");
    char what[200];
    Voice v;
    voice_build(&v, VOICE_DESKS, 0);
    Net *n = v.n;
    VoiceLog g, p;
    Buf o;

    /* -------------------------------------------------- nothing to report */
    ck("a machine that has never been on a call holds no record of one",
       net_voice_log(n, v.files, &g) && !g.any &&
       g.out.calls == 0 && g.in.calls == 0);
    buf_init(&o);
    net_dump_voice_log(n, v.files, &o);
    ck("and says so, rather than printing an empty table",
       strstr(o.p ? o.p : "", "has not been on a call") != NULL);
    buf_free(&o);

    /* ------------------------------------------- a call that is still up */
    voice_spin(&v, 200);
    ck("a call in progress is not in the record: it has not ended yet",
       net_voice_log(n, v.handset, &g) && !g.any);
    buf_init(&o);
    net_dump_voice_log(n, v.handset, &o);
    ck("and the record points at the live view instead of inventing one",
       strstr(o.p ? o.p : "", "in progress") != NULL);
    buf_free(&o);

    /* ------------------------------------- and now the floor pulls files */
    for (int i = 0; i < v.nd; i++)
        v.cs[i] = net_tcp_connect(n, v.desk[i], net_ip(10, 0, 0, 1), 8080);
    voice_spin(&v, 1500);
    /* THE LANDLORD'S VIEW, taken while the stream is still alive. This is
     * what `service` reads, and it is the number the desk has to agree with:
     * a tool that answered something else would be a second opinion, which
     * is the one thing this project does not allow. */
    VoiceStats live;
    bool got = net_voice_stats(n, v.riser, &live);
    ck("the loaded riser really ruins the call being measured",
       got && live.concealed > 0 && live.expected > 0);

    /* THE HANG-UP. This is what the end of a busy period does to every call
     * in the building, and until now it took the evidence with it. */
    net_voice_stop(n, v.riser);
    net_voice_stop(n, v.inhouse);
    VoiceStats gone;
    ck("hanging up destroys the stream: a live reading is now impossible",
       !net_voice_active(n, v.riser) && !net_voice_stats(n, v.riser, &gone));

    ck("but the machine that was listening kept the count",
       net_voice_log(n, v.handset, &g) && g.any && g.in.calls == 1);
    snprintf(what, sizeof what,
             "and it is the same measurement, not a second opinion (%u of %u "
             "concealed, both)", g.in.concealed, g.in.expected);
    ck(what, g.in.concealed == live.concealed &&
             g.in.expected == live.expected &&
             g.in.conceal_ppm == live.conceal_ppm);
    ck("the machine that was TALKING kept the far end's report of it",
       net_voice_log(n, v.pbx, &p) && p.out.calls == 2 && p.in.calls == 0);
    snprintf(what, sizeof what,
             "and its two calls are summed, not overwritten (%u packets over "
             "%u calls)", p.out.sent, p.out.calls);
    ck(what, p.out.sent >= live.sent);

    /* THE PLAYTESTER'S BLIND SPOT, asserted rather than described: the desk's
     * own card is clean. That is why nothing already on the machine could
     * have said a word about it. */
    snprintf(what, sizeof what,
             "the receiving card itself dropped NOTHING (%llu), which is why "
             "netstat -P could not say",
             (unsigned long long)net_port_drops(n, v.handset, 0));
    ck(what, net_port_drops(n, v.handset, 0) == 0 && g.in.concealed > 0);

    /* IT MUST SURVIVE THE BUSY PERIOD BEING OVER. Time passes, the transfers
     * finish, and the record is still the record. */
    voice_spin(&v, 500);
    VoiceLog later;
    ck("time passing does not erase it: this is a record, not a reading",
       net_voice_log(n, v.handset, &later) &&
       later.in.concealed == g.in.concealed &&
       later.in.calls == g.in.calls);

    /* AND IT MUST NAME THE CAUSE. */
    buf_init(&o);
    net_dump_voice_log(n, v.handset, &o);
    ck("the record names the port that did it, off the stream's own tag",
       strstr(o.p ? o.p : "", "core port 0") != NULL);
    ck("and puts a verdict in words on the worst call of the run",
       strstr(o.p ? o.p : "", "verdict:") != NULL);
    ck("it says which direction the bad call was, because they differ",
       strstr(o.p ? o.p : "", "one it received") != NULL);
    ck("and it says where the outbound numbers came from, which no endpoint "
       "can measure alone",
       strstr(o.p ? o.p : "", "as the far end reported") != NULL);
    buf_free(&o);

    /* A NEW RUN CLEARS THE OLD ONE. Yesterday's calls are what is wanted, and
     * a counter that added today's to them would be a lifetime total nobody
     * asked for. */
    net_ping(n, v.pbx, net_ip(10, 0, 0, 4), NULL);
    int again = net_voice_call(n, v.pbx, v.handset, net_ip(10, 0, 0, 4));
    ck("dialling again opens a new run and clears the last one",
       again >= 0 && net_voice_log(n, v.handset, &g) && !g.any &&
       g.in.calls == 0);
    net_voice_stop(n, again);
    net_free(n);

    /* ---------------------------------------------------------- the good day
     *
     * THE JUDGEMENT CALL, ASSERTED. A tool that only speaks when things are
     * bad teaches a player to ignore it when it is silent, and "the calls off
     * this desk were clear, so the fault is not the network under it" is a
     * diagnosis worth being able to reach from the chair. */
    Voice q;
    voice_build(&q, VOICE_DESKS, 0);
    voice_spin(&q, 1000);
    net_voice_stop(q.n, q.riser);
    net_voice_stop(q.n, q.inhouse);
    buf_init(&o);
    net_dump_voice_log(q.n, q.handset, &o);
    ck("a desk whose calls were fine says so, in the same words and unasked",
       strstr(o.p ? o.p : "", "verdict: clear") != NULL);
    ck("and it still prints the counters, so `nothing missing` is evidence",
       strstr(o.p ? o.p : "", "concealed") != NULL &&
       net_voice_log(q.n, q.handset, &g) && g.in.calls == 1 &&
       g.in.concealed == 0);
    buf_free(&o);

    /* ------------------------------------------------- and from the chair
     *
     * The whole point is a person typing on the machine, so the last checks
     * are a real booted machine pinned to that node, running the real
     * program off its own disk. */
    {
        Machine m;
        memset(&m, 0, sizeof m);
        machine_install(&m, 4242);
        machine_boot(&m);
        netsite_pin(&m, q.n, q.handset);
        Buf b = {0};
        kernel_run(&m, "voice", &b);
        ck("`voice` on the machine itself prints the record off the kernel",
           b.p && strstr(b.p, "verdict: clear") && strstr(b.p, "dir  calls"));
        buf_clear(&b);
        kernel_run(&m, "voice -l", &b);
        ck("`voice -l` asks the live question and honestly answers `no calls`",
           b.p && strstr(b.p, "no calls"));
        buf_clear(&b);
        kernel_run(&m, "voice -Z", &b);
        ck("and a flag it does not have is refused by name, not ignored",
           b.p && strstr(b.p, "no such option"));
        buf_free(&b);
        machine_free(&m);
    }
    net_free(q.n);
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

    /* AND THE COUNT IS PER POOL. The box holds one lease, and it came out of
     * the vlan 11 range: the vlan 13 range on the same box has issued nothing
     * and must say so. Printing the box's total against every range made a
     * router with a pool per vlan tell a player that seven empty segments each
     * had every desk in the building on them. */
    ck("each pool counts the leases IT issued, not the box's",
       net_dhcpd_leases(n, rt) == 1 &&
       net_dhcpd_pool_leases(n, rt, 0) == 1 &&
       net_dhcpd_pool_leases(n, rt, 1) == 0);
    ck("and a pool that does not exist has issued nothing",
       net_dhcpd_pool_leases(n, rt, 2) == 0 &&
       net_dhcpd_pool_leases(n, rt, -1) == 0);

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

/* ------------------------------------------------------- a server of ours */
/* WHAT `dnsd <box>` PRODUCED BEFORE THIS: a name server with an empty zone,
 * no verb anywhere in the tower to put a record in it, and no forwarder --
 * so it answered NXDOMAIN to every query in the building for the rest of the
 * run, and the only working resolver was the ISP's, out through the router.
 * A playtester at day 85 concluded the firewall was eating udp/53. It was
 * not; there was simply nothing to answer with. */
static void check_dns_server(void)
{
    printf("a name server of the player's own\n");
    Lan l = lan_new(23);
    Net *n = l.n;
    /* alpha is the client, bravo is our floor's resolver, and up is the
     * upstream one bravo forwards to -- three real machines on real copper. */
    int up = net_add_host(n, "isp");
    net_cable(n, up, 0, l.sw, 2, 9, CAB_CAT5E);
    net_if_addr(n, up, 0, net_ip(10, 0, 0, 9), net_mask_bits(24));
    net_dnsd(n, up);
    net_dns_record(n, up, "wiki.nomnix.org", net_ip(10, 0, 2, 20));

    net_dnsd(n, l.b);
    net_dns_record(n, l.b, "files.floor3", net_ip(10, 0, 0, 50));
    net_set_resolver(n, l.a, net_ip(10, 0, 0, 2));

    uint32_t ip = 0;
    ck("a record the tower gave it is served",
       net_resolve(n, l.b == -1 ? l.a : l.a, "files.floor3", &ip) &&
       ip == net_ip(10, 0, 0, 50));
    ck("a name it has not got, with no forwarder, is NXDOMAIN and not silence",
       net_resolve_ex(n, l.a, "wiki.nomnix.org", &ip) == RESOLVE_NXDOMAIN);

    /* THE HALF THAT MAKES A FLOOR'S OWN RESOLVER WORTH HAVING. Give it
     * somewhere to ask and the same query comes back answered, off the
     * upstream server, over the wire, with no client anywhere reconfigured. */
    net_set_resolver(n, l.b, net_ip(10, 0, 0, 9));
    ck("the forwarder is the resolver that box was configured with",
       net_dns_forwarder(n, l.b) == net_ip(10, 0, 0, 9));
    ip = 0;
    ck("and a name it has not got now comes back from upstream",
       net_resolve(n, l.a, "wiki.nomnix.org", &ip) && ip == net_ip(10, 0, 2, 20));
    ck("a name nobody has is still NXDOMAIN, forwarded and relayed back",
       net_resolve_ex(n, l.a, "nowhere.example", &ip) == RESOLVE_NXDOMAIN);
    ck("its own records still win without leaving the box",
       net_resolve(n, l.a, "files.floor3", &ip) && ip == net_ip(10, 0, 0, 50));

    /* A BOX POINTED AT ITSELF IS NOT A FORWARDER, and if it were it would
     * ask itself the question it could not answer until the world stopped. */
    net_set_resolver(n, l.b, net_ip(10, 0, 0, 2));
    ck("a resolver pointed at its own address forwards nowhere",
       net_dns_forwarder(n, l.b) == 0);
    ck("and answers NXDOMAIN rather than looping",
       net_resolve_ex(n, l.a, "wiki.nomnix.org", &ip) == RESOLVE_NXDOMAIN);

    /* The upstream is there and cannot be reached. The client sees silence,
     * which is what a forwarder with nowhere to go really produces. */
    net_set_resolver(n, l.b, net_ip(10, 0, 0, 200));
    ck("a forwarder that is not there times the client out",
       net_resolve_ex(n, l.a, "wiki.nomnix.org", &ip) == RESOLVE_TIMEOUT);

    /* A zone is a set of names, not a log of them: the tower writes this
     * file out and reads it back at every boot. */
    net_dns_record(n, l.b, "files.floor3", net_ip(10, 0, 0, 51));
    ck("setting a name twice changes it and does not duplicate it",
       net_dns_record_count(n, l.b) == 1);
    char nm[64] = "";
    uint32_t rip = 0;
    ck("and the zone reads back, which is how it reaches a disk",
       net_dns_record_at(n, l.b, 0, nm, sizeof nm, &rip) &&
       strcmp(nm, "files.floor3") == 0 && rip == net_ip(10, 0, 0, 51) &&
       !net_dns_record_at(n, l.b, 1, nm, sizeof nm, &rip));
    net_dnsd_stop(n, l.b);
    ck("stopping the server takes the zone with it, as a power cut would",
       !net_dnsd_running(n, l.b) && net_dns_record_count(n, l.b) == 0);
    net_free(n);
}

/* WHAT A BOX HAS THROWN AWAY, which is the counter a diagnostic needs to be
 * able to say "the far end refused it" without guessing. It counts the rules
 * that drop, and not the ones that accept -- `net_fw_hits` counts both, and
 * a hint built on that would call an accepted packet a refusal. */
static void check_fw_drops(void)
{
    printf("the drop counter a diagnostic can read\n");
    Lan l = lan_new(24);
    Net *n = l.n;
    net_fw_add(n, l.b, FW_IN, IP_PROTO_TCP, 22, 0, 0, FW_ACCEPT);
    net_fw_add(n, l.b, FW_IN, FW_ANY_PROTO, FW_ANY_PORT, 0, 0, FW_DROP);
    ck("nothing has been dropped yet", net_fw_drops(n, l.b) == 0);
    net_tcp_close(n, net_tcp_connect_wait(n, l.a, net_ip(10, 0, 0, 2), 22));
    ck("a port it serves is accepted and counts as no drop",
       net_fw_drops(n, l.b) == 0);
    net_ping(n, l.a, net_ip(10, 0, 0, 2), NULL);
    ck("an echo it did not ask for is dropped, and counted",
       net_fw_drops(n, l.b) >= 1);
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
    check_utilisation();
    check_congestion();
    check_voice();
    check_voice_circuit();
    check_voice_log();
    check_firewall();
    check_drop_reasons();
    check_dhcp();
    check_dhcp_scope();
    check_dns();
    check_dns_server();
    check_fw_drops();
    check_http();
    check_determinism();
    check_visible();
    check_nft();
    check_tools();
    printf("\n%d/%d network checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
