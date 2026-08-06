/* loadcheck.c — the calibration, run as `bf --loadcheck`.
 *
 * THE QUESTION THIS GATE ANSWERS, and it is the one the brief made the
 * deliverable: *"A naive build -- everything flat on one switch, one subnet,
 * cheap copper -- must start to feel slow around three floors and genuinely
 * break by about five. A thought-through build -- segmented per floor or per
 * tenant, a router doing real work, uplinks sized for what is behind them --
 * must carry substantially further. Measure this and print the numbers."*
 *
 * So it plays both, from the same seed, into the same building, with the
 * same tenants asking for the same work, and prints where each one falls
 * over. Nothing in here is a difficulty constant: the two builds differ only
 * in the topology the player would have typed, and every number printed was
 * counted off a port during a busy period that really happened.
 *
 * WHAT "FALLS OVER" MEANS. Fewer than four fifths of the building's people
 * got their day's work done inside the busy period. That is the same rule
 * site_day() uses to decide whether a tenant pays, so the gate and the game
 * are measuring one thing.
 *
 * IF THE NAIVE BUILD SURVIVES TO NINE FLOORS THE CURVE IS WRONG, and this
 * gate says so out loud and fails, rather than shipping a difficulty curve
 * that is not there.
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

#define LOAD_SEED  7008ull
#define STEPS      9      /* tenancies to grow through                      */

/* ---------------------------------------------------------------- helpers */
/* Where a floor's kit goes: the comms cupboard if the building has one on
 * that floor, otherwise the riser, otherwise the tenant's own room. The
 * generator does not promise a cupboard on every floor and a player would
 * put the switch wherever there was somewhere to put it. */
static int comms_on(const Building *b, int floor, int fallback)
{
    int r = bld_find(b, floor, RM_COMMS);
    if (r < 0) r = bld_find(b, floor, RM_RISER);
    if (r < 0) r = fallback;
    return r;
}

static int put(Site *s, int kind, int room, const char *name)
{
    int d = site_install(s, kind, room, name);
    return d;
}

/* Run days until this tenancy has moved in and has desks. */
static void keep_measuring(Site *s);
static void until_moved(Site *s, int ti)
{
    for (int guard = 0; guard < 400 && !s->tenant[ti].moved; guard++) {
        keep_measuring(s);
        site_day(s, NULL);
    }
}

/* THE CALIBRATION IS A MEASUREMENT, NOT A PLAYTHROUGH. A build that has
 * fallen over would have its lease ended by the third complaint, and then
 * there would be no numbers for the floors past it -- which are exactly the
 * numbers the brief asked for. So the harness pays the tenants' complaints
 * off between steps and keeps growing, and says so. Nothing else about the
 * day is touched: the frames, the drops and the work done are whatever they
 * were. The run-over rule itself is checked separately, in check_complaints,
 * where it is played rather than measured. */
static void keep_measuring(Site *s)
{
    s->over = 0;
    s->complaints = 0;
    for (int i = 0; i < s->ntenant; i++) {
        s->tenant[i].strikes = 0;
        s->tenant[i].complained = 0;
    }
}

/* One flat tower, built by the same lines in the same order every time, and
 * played for one day past the first tenancy's arrival. The determinism check
 * runs it twice and compares. */
static SiteDay flat_run(Building *b);

/* ------------------------------------------------------------ the numbers */
typedef struct {
    int  floors;          /* tenancies connected so far                     */
    int  desks;
    int  finished, sessions;
    int  pct;
    int  worst_ms;
    char hot[40];
    int  hot_util;
    unsigned long long drops;
} Step;

static void record(const Site *s, const SiteDay *r, int floors, Step *st)
{
    st->floors = floors;
    st->desks = r->connected;
    st->finished = r->finished;
    st->sessions = r->sessions;
    st->pct = r->sessions ? (r->finished * 100) / r->sessions : 0;
    st->worst_ms = r->worst_ms;
    snprintf(st->hot, sizeof st->hot, "%s", r->hot);
    st->hot_util = r->hot_util;
    st->drops = (unsigned long long)r->drops;
}

static void show(const char *what, const Step *st, int n)
{
    printf("\n%s\n", what);
    printf("  floors  desks   work done   slowest   busiest port        util   frames lost\n");
    for (int i = 0; i < n; i++) {
        if (!st[i].sessions) continue;
        printf("  %5d  %5d   %4d/%-4d %3d%%  %6dms   %-18s %4d%%  %11llu\n",
               st[i].floors, st[i].desks, st[i].finished, st[i].sessions,
               st[i].pct, st[i].worst_ms, st[i].hot, st[i].hot_util, st[i].drops);
    }
}

/* The first floor count at which fewer than four fifths of the building's
 * people got their work done, or 0 if it never happened. */
static int broke_at(const Step *st, int n)
{
    for (int i = 0; i < n; i++)
        if (st[i].sessions && st[i].pct < 80) return st[i].floors;
    return 0;
}
/* And the first at which it is visibly not comfortable any more. */
static int slow_at(const Step *st, int n)
{
    for (int i = 0; i < n; i++)
        if (st[i].sessions && (st[i].pct < 97 || st[i].hot_util >= 80)) return st[i].floors;
    return 0;
}

/* ====================================================================== the
 * NAIVE BUILD. One router at the door, one switch in the MDF, a switch per
 * floor hung off it, one flat 10.0.0.0/16 for the whole tower, cheap copper,
 * and no server anywhere -- so every file anybody opens comes down the
 * landlord's circuit. This is what somebody builds who has never had to
 * unbuild one, and every line of it is a line the player would really type.
 */
static void naive(Building *b, Step *st)
{
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);            /* the gate is about the network     */

    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);

    int next_core_port = 1;
    for (int i = 0; i < STEPS; i++) {
        until_moved(&s, i);
        int room = comms_on(b, s.tenant[i].floor, s.tenant[i].room);
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "sw%d", i + 1);
        int sw = put(&s, SDEV_SWITCH24, room, nm);
        if (sw < 0) break;
        if (site_cable(&s, core, next_core_port++, sw, 0, CAB_CAT5E) < 0) break;
        site_serve(&s, i, sw, CAB_CAT5E);
        SiteDay r;
        keep_measuring(&s);
        site_day(&s, &r);
        record(&s, &r, i + 1, &st[i]);
    }
    site_free(&s);
}

/* ====================================================================== the
 * THOUGHT-THROUGH BUILD. The same tenants, the same building, the same
 * money. A vlan per floor, terminated on a subinterface of the router down
 * one trunk, so a broadcast on floor four is not floor one's problem. A
 * server on each floor doing that floor's DHCP and holding that floor's
 * files, so the traffic that is between a desk and its files never leaves
 * the switch it is plugged into. Fibre from the MDF to each floor, because
 * an uplink is sized for what is behind it.
 *
 * Nobody wrote down that this would be better. It is better because the
 * frames go somewhere else.
 */
static void planned(Building *b, Step *st)
{
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);

    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    /* The router's LAN side is a trunk. One cable, one vlan per floor on it. */
    site_cable(&s, edge, 1, core, 0, CAB_FIBRE);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_port_trunk(&s, core, 0, 0);

    int next_core_port = 1;
    for (int i = 0; i < STEPS; i++) {
        until_moved(&s, i);
        int vlan = 10 + i;
        int room = comms_on(b, s.tenant[i].floor, s.tenant[i].room);
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "sw%d", i + 1);
        int sw = put(&s, SDEV_SWITCH24, room, nm);
        if (sw < 0) break;
        int cp = next_core_port++;
        if (site_cable(&s, core, cp, sw, 0, CAB_FIBRE) < 0) break;

        /* The tag, end to end: the router's subinterface, the core's trunk,
         * the floor switch's trunk, and every access port on the floor. */
        site_subif(&s, edge, 1, vlan, net_ip(10, vlan, 0, 1), net_mask_bits(24));
        site_port_trunk(&s, core, 0, vlan);
        site_port_trunk(&s, core, cp, vlan);
        site_port_trunk(&s, sw, 0, vlan);
        for (int p = 1; p < site_kind_ports(SDEV_SWITCH24); p++)
            site_port_vlan(&s, sw, p, vlan);

        /* THE TENANCY'S OWN SERVER, in the tenancy's own room -- which is
         * how it becomes theirs, because site_install takes a device's owner
         * from the room it is standing in. It does that floor's DHCP and
         * holds that floor's files, so the traffic between a desk and the
         * thing it is opening never leaves the switch they share.
         *
         * Putting it in the comms cupboard instead would make it the
         * LANDLORD'S server, and the second tenancy to arrive on the same
         * floor would then be sent across the trunk and through the router
         * to reach the first tenancy's files -- which measurably costs, and
         * is the kind of thing this gate is for. */
        snprintf(nm, sizeof nm, "srv%d", i + 1);
        int srv = put(&s, SDEV_SERVER, s.tenant[i].room, nm);
        if (srv < 0) break;
        if (site_cable(&s, sw, 1, srv, 0, CAB_CAT5E) < 0) break;
        site_power(&s, srv, true);
        site_addr(&s, srv, 0, net_ip(10, vlan, 0, 2), net_mask_bits(24));
        site_gateway(&s, srv, net_ip(10, vlan, 0, 1));
        site_dhcpd(&s, srv, net_ip(10, vlan, 0, 10), 200, net_mask_bits(24),
                   net_ip(10, vlan, 0, 1), s.wan_isp);
        site_httpd(&s, srv, 80);

        site_serve(&s, i, sw, CAB_CAT5E);
        SiteDay r;
        keep_measuring(&s);
        site_day(&s, &r);
        record(&s, &r, i + 1, &st[i]);
    }
    site_free(&s);
}

/* ============================================== the assertions on the loop
 * Everything the brief asked --loadcheck to prove, each of it done rather
 * than described. These run on small worlds so the gate stays quick; the
 * calibration above is the slow half and it runs once.
 */
static void check_clock(Building *b)
{
    printf("the clock, and the rent\n");
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    ck("a site starts on day zero with nobody in", s.day == 0 && s.ntenant > 0);
    int first = s.tenant[0].day;
    site_advance(&s, first - 1, NULL);
    ck("nobody has moved in the day before the first tenancy arrives",
       s.day == first - 1 && !s.tenant[0].moved);
    site_day(&s, NULL);
    ck("and on their day they are in, with a desk for every drop",
       s.tenant[0].moved && s.tenant[0].ndesk == s.tenant[0].drops);
    ck("their desks have cards and nothing plugged into them",
       site_tenant_connected(&s, 0) == 0);

    /* Build them something. */
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);

    long before = s.money;
    site_day(&s, NULL);
    ck("a tenancy with no port pays nothing and does not complain",
       s.money == before && s.tenant[0].complained == 0);

    int sw = put(&s, SDEV_SWITCH24, comms_on(b, s.tenant[0].floor, s.tenant[0].room), "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5E);
    int got = site_serve(&s, 0, sw, CAB_CAT5E);
    ck("copper to a tenancy's desks connects as many as the switch has holes",
       got > 0 && got <= site_kind_ports(SDEV_SWITCH24) - 1);
    ck("and every metre of it was charged for", s.money < before);

    /* Without a DHCP server the desks have cards and no addresses. */
    before = s.money;
    site_day(&s, NULL);
    ck("desks with nowhere to get an address get none",
       site_tenant_addressed(&s, 0) == 0);
    ck("and a tenancy whose people cannot work pays no rent", s.money == before);

    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    before = s.money;
    SiteDay r;
    site_day(&s, &r);
    ck("with a server on the segment they ask for one and get one",
       site_tenant_addressed(&s, 0) > 0);
    ck("their people really use the network: frames, not a counter",
       r.frames > 1000 && r.bytes > 1024 * 1024);
    ck("the work finishes and the rent arrives", r.finished > 0 && s.money > before);
    ck("rent is a thirtieth of a month, for the day it worked",
       r.rent == s.tenant[0].rent / 30);

    site_free(&s);

    /* DETERMINISM. The same seed, built by the same lines in the same order,
     * must move the same frames on the same day -- and it is compared by
     * playing it twice rather than by trusting that it would. */
    SiteDay a = flat_run(b), c = flat_run(b);
    ck("the same seed on the same day moves the same number of frames",
       a.frames == c.frames && a.bytes == c.bytes && a.frames > 0);
    ck("and the same work finishes, and the same rent arrives",
       a.finished == c.finished && a.sessions == c.sessions && a.rent == c.rent);
}

/* A link asked for more than it can carry, on purpose, with the smallest
 * world that does it: two desks' worth of traffic down a hundred megabit
 * run. The drop must land on the port, the latency must rise, and both must
 * be readable with the tools that already exist. */
static void check_capacity(Building *b)
{
    printf("\nload that hurts honestly\n");
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);

    until_moved(&s, 1);
    int room = comms_on(b, s.tenant[0].floor, s.tenant[0].room);
    /* THE CHEAP DRUM. Two tenancies -- thirty-eight desks -- behind one
     * hundred megabit run, which is the cheapest line in the catalogue and
     * the most ordinary mistake there is. */
    int sw = put(&s, SDEV_SWITCH24, room, "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5);
    site_serve(&s, 0, sw, CAB_CAT5E);
    site_serve(&s, 1, sw, CAB_CAT5E);

    /* A quiet baseline, from a desk, before anybody has done any work: this
     * is what the wire does when nothing else is on it. */
    int quiet = 0, probe_desk = s.tenant[0].desk0 + 1;
    for (int i = 0; i < s.tenant[0].ndesk; i++)
        net_dhcp_client(s.net, s.dev[s.tenant[0].desk0 + i].node, 0);
    /* Twice: the first one pays for an ARP round trip and would be
     * measuring the resolution, not the wire. */
    net_ping(s.net, s.dev[probe_desk].node, net_ip(10, 0, 0, 1), &quiet);
    net_ping(s.net, s.dev[probe_desk].node, net_ip(10, 0, 0, 1), &quiet);
    uint64_t d0 = net_port_drops(s.net, s.dev[core].node, 1);
    uint64_t q0 = net_port_qdrops(s.net, s.dev[core].node, 1);

    SiteDay r;
    site_day(&s, &r);
    site_day(&s, &r);
    uint64_t d1 = net_port_drops(s.net, s.dev[core].node, 1);
    uint64_t q1 = net_port_qdrops(s.net, s.dev[core].node, 1);
    int util = (int)((net_port_busy_us(s.net, s.dev[core].node, 1) * 100)
                     / (SITE_BUSY_MS * 1000ull));

    /* Eighty-odd per cent and not a hundred, because a link that is losing
     * frames is a link whose senders have backed off: TCP never gets to sit
     * at line rate once it is dropping, and a gate that demanded a hundred
     * would be demanding the stack lie. */
    char line[96];
    snprintf(line, sizeof line,
             "a hundred megabit run under two floors of desks fills up (%d%%)", util);
    ck(line, util >= 75);
    ck("and the port it fills up on is the one that drops", d1 > d0);
    ck("and the drops are the egress buffer, counted as such", q1 > q0);
    /* The evidence, in the words a player reads. */
    Buf ports = {0};
    net_dump_ports_used(s.net, s.dev[core].node, &ports);
    ck("`show` on that box says so, with the reason in words",
       ports.p && strstr(ports.p, "egress buffer full") != NULL);
    buf_free(&ports);

    /* LATENCY, MEASURED. A ping across the full link while it is loaded is
     * slower than the same ping on the same wire when it is not, because the
     * echo really is behind other people's frames. */
    ck("a ping on a wire that is not congested is quick", quiet <= 8);

    /* LATENCY, MEASURED, WITH THE TOOL THE PLAYER HAS. The same echo across
     * the same wire, sent while the port has other people's frames in front
     * of it, comes back later -- because it really is behind them. Nothing
     * adds a penalty; the frame is queued and the queue has a length. */
    {
        /* Fill the hundred megabit run the way the tenants fill it: several
         * desks pulling a file at once. Then ping across it. */
        int rtt = 0;
        uint64_t q = 0;
        int probe[8];
        const char *req = "GET /n/1024 HTTP/1.0\r\nHost: f\r\n\r\n";
        /* A TEN MEGABIT CIRCUIT, which the landlord could really have
         * bought, with eight desks pulling a megabyte each down it. The
         * handoff's egress buffer fills and stays full, and an echo behind
         * it waits exactly as long as the frames in front of it take. */
        site_isp(&s, 10);
        int quiet_isp = 0;
        net_ping(s.net, s.dev[probe_desk].node, s.wan_isp, &quiet_isp);
        net_ping(s.net, s.dev[probe_desk].node, s.wan_isp, &quiet_isp);
        for (int i = 0; i < 8; i++)
            probe[i] = net_tcp_connect(s.net, s.dev[s.tenant[0].desk0 + 2 + i].node,
                                       net_ip(198, 51, 100, 1), 80);
        for (int t = 0; t < 3000; t++) {
            net_step(s.net, 1);
            for (int i = 0; i < 8; i++) {
                if (probe[i] < 0) continue;
                if (net_tcp_state(s.net, probe[i]) == TCP_ESTABLISHED) {
                    uint8_t bb[1024];
                    if (net_tcp_recv(s.net, probe[i], bb, sizeof bb) <= 0)
                        net_tcp_send(s.net, probe[i], req, (int)strlen(req));
                    while (net_tcp_recv(s.net, probe[i], bb, sizeof bb) > 0) { }
                }
            }
            q = net_port_queue_us(s.net, s.dev[s.uplink].node, 0);
            if (q > 15000) break;
        }
        quiet = quiet_isp;
        net_ping(s.net, s.dev[probe_desk].node, s.wan_isp, &rtt);
        for (int i = 0; i < 8; i++) if (probe[i] >= 0) net_tcp_close(s.net, probe[i]);
        char l2[110];
        snprintf(l2, sizeof l2, "and the same ping with %llums of queue in front of "
                 "it takes longer (%dms vs %dms)",
                 (unsigned long long)(q / 1000), rtt, quiet);
        ck(l2, rtt > quiet);
    }

    /* Take the load away and the same ping is quick again. */
    for (int i = 0; i < s.tenant[0].ndesk; i++)
        net_port_admin(s.net, s.dev[s.tenant[0].desk0 + i].node, 0, false);
    site_day(&s, &r);
    int calm = 0;
    net_ping(s.net, s.dev[sw].node, net_ip(10, 0, 0, 1), &calm);
    ck("with the desks unplugged the same wire is quick again", calm <= 4);
    ck("and the port counter still remembers what it threw away",
       net_port_qdrops(s.net, s.dev[core].node, 1) >= q1);
    site_free(&s);
}

/* A tenancy that is being starved complains, and one that is not does not. */
static void check_complaints(Building *b)
{
    printf("\ncomplaints, and when they are not filed\n");
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    until_moved(&s, 0);
    int sw = put(&s, SDEV_SWITCH24, comms_on(b, s.tenant[0].floor, s.tenant[0].room), "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5E);
    site_serve(&s, 0, sw, CAB_CAT5E);

    site_advance(&s, 6, NULL);
    ck("a tenancy that is being served files nothing",
       s.complaints == 0 && s.tenant[0].strikes == 0);
    ck("and pays every day it works", s.rent_taken > 0);

    /* NOW STARVE THEM, and starve them with a decision rather than a flag:
     * the landlord downgrades the circuit to ten megabits. Twenty desks
     * asking for a file each is several times that, so the same people doing
     * the same work stop finishing it -- and every frame that goes missing
     * goes missing on the handoff's port, where `netstat -P` shows it. */
    site_isp(&s, 10);
    site_day(&s, NULL);
    ck("one bad day is not a complaint", s.complaints == 0 && s.tenant[0].strikes == 1);
    site_day(&s, NULL);
    ck("nor two", s.complaints == 0 && s.tenant[0].strikes == 2);
    site_day(&s, NULL);
    ck("three days in a row is", s.complaints == 1 && s.tenant[0].complained);
    site_free(&s);
}

/* The flat tower the determinism check plays twice. Every line is one the
 * player would type, in the order they would type it. */
static SiteDay flat_run(Building *b)
{
    Site s;
    site_new(&s, b, LOAD_SEED, 60000);
    site_credit(&s, 400000);
    int mdf = bld_find(b, 0, RM_MDF);
    int edge = put(&s, SDEV_ROUTER, mdf, "edge");
    int core = put(&s, SDEV_SWITCH24, mdf, "core");
    site_cable(&s, edge, 0, s.uplink, 0, CAB_CAT5E);
    site_cable(&s, edge, 1, core, 0, CAB_CAT5E);
    site_addr(&s, edge, 0, s.wan_you, s.wan_mask);
    site_addr(&s, edge, 1, net_ip(10, 0, 0, 1), net_mask_bits(16));
    site_gateway(&s, edge, s.wan_isp);
    site_forwarding(&s, edge, true);
    site_dhcpd(&s, edge, net_ip(10, 0, 1, 1), 200, net_mask_bits(16),
               net_ip(10, 0, 0, 1), s.wan_isp);
    until_moved(&s, 0);
    int sw = put(&s, SDEV_SWITCH24,
                 comms_on(b, s.tenant[0].floor, s.tenant[0].room), "sw1");
    site_cable(&s, core, 1, sw, 0, CAB_CAT5E);
    site_serve(&s, 0, sw, CAB_CAT5E);
    SiteDay r;
    site_day(&s, &r);
    site_free(&s);
    return r;
}

/* ===================================================================== main */
int load_selfcheck(void)
{
    Building b;
    if (!bld_generate(&b, LOAD_SEED)) {
        printf("seed %llu makes no building\n", (unsigned long long)LOAD_SEED);
        return 1;
    }
    passed = total = 0;

    check_clock(&b);
    check_capacity(&b);
    check_complaints(&b);

    /* ------------------------------------------------------ the calibration */
    printf("\nTHE CALIBRATION. The same building, the same tenants, the same\n"
           "work, built two ways. Every number below was counted off a port.\n");
    Step nv[STEPS], pl[STEPS];
    memset(nv, 0, sizeof nv);
    memset(pl, 0, sizeof pl);
    naive(&b, nv);
    planned(&b, pl);
    show("NAIVE: one flat 10.0.0.0/16, cheap copper, no server -- every file\n"
         "anybody opens comes down the landlord's circuit.", nv, STEPS);
    show("PLANNED: a vlan per floor down one trunk, fibre to each floor, and a\n"
         "server on each floor holding that floor's files and doing its DHCP.",
         pl, STEPS);

    int nb = broke_at(nv, STEPS), ns = slow_at(nv, STEPS);
    int pb = broke_at(pl, STEPS), grown = 0;
    for (int i = 0; i < STEPS; i++) if (nv[i].sessions) grown = nv[i].floors;
    if (ns) printf("\nthe naive build is visibly working hard at %d floor%s",
                   ns, ns == 1 ? "" : "s");
    else printf("\nthe naive build never even works hard in %d floors", grown);
    if (nb) printf(" and falls over at %d.\n", nb);
    else printf(" and never falls over in %d.\n", grown);
    if (pb) printf("the planned build falls over at %d floors.\n", pb);
    else printf("the planned build carries all %d floors it was grown to.\n", grown);
    printf("the same tenants, the same money, the same building: the difference\n"
           "is where the frames go.\n\n");

    /* THE GATE ON THE CURVE ITSELF. */
    ck("a naive build is comfortable on one floor", nv[0].sessions && nv[0].pct >= 95);
    ck("a naive build is visibly working hard by three floors",
       ns > 0 && ns <= 4);
    ck("a naive build has fallen over by five",
       nb > 0 && nb <= 5);
    ck("and it did not fall over at two, which would be a different game",
       nb >= 3);
    ck("a planned build carries every floor the naive one could not",
       pb == 0 || pb > nb + 2);
    /* And it is not carrying them by doing less work. */
    int nvd = 0, pld = 0;
    for (int i = 0; i < STEPS; i++) { nvd += nv[i].desks; pld += pl[i].desks; }
    ck("with the same desks doing the same work", pld >= nvd);

    bld_free(&b);
    printf("\n%d/%d load checks pass\n", passed, total);
    return passed == total ? 0 : 1;
}
