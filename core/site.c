/* site.c — ordering, carrying, cabling and configuring. See site.h.
 *
 * Every function here is an operation a person performs, and every one of
 * them can refuse. Nothing in this file decides whether anything is
 * reachable: it puts boxes in rooms, lays cable of a length the building
 * computed, and writes addresses onto interfaces. What happens after that is
 * frames, in core/netstack.c, and nobody here gets to overrule them.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "site.h"

/* ---------------------------------------------------------- the catalogue */
/* Ports and prices. The port count is the interesting number: it is the
 * limit a growing floor hits first, before addresses and long before
 * bandwidth, and it is the one the player can see coming and still fails to
 * plan for. */
/* AND WHAT THE SOCKETS ON THE BACK OF IT WILL CLOCK, which until D27 nothing
 * in this game had an opinion about: the link speed came from the CABLE
 * alone, so a cat 6 patch lead to a desk negotiated ten gigabit because the
 * run was under fifty-five metres. A playtester named the consequence
 * exactly: *"Port speed is decided by cable grade, and cat6 gives 10 Gb
 * ports at these distances. That makes the desk-cable choice feel free."*
 *
 * It is free because it is wrong. A link runs at the slowest of the three
 * things in it -- the port at each end and the copper between -- and the
 * ports are a property of the box somebody bought. A desk has a gigabit card
 * in it. A four hundred pound twenty-four port switch is a gigabit switch
 * with an SFP+ PAIR on the end of it, and that pair is the only ten gigabit
 * copper or glass in this catalogue apart from the router's own sockets.
 *
 * `slow_mb` is what an ordinary socket on this box does, and `nfast` is how
 * many of the TOP-numbered ones are the ten gigabit uplinks. netstack takes
 * the minimum of the port rate and the cable, so nothing here can make a
 * link faster than the copper -- only slower, which is what a real port
 * does. And the decision this restores is a real one: ten gigabit has to be
 * BOUGHT and it has to be LANDED ON THE RIGHT HOLE. Cat 6 into a desk is
 * money burnt; cat 6 between a router and a core switch's uplink is ten
 * gigabit for a third of the price of fibre, right up until the run passes
 * fifty-five metres and it quietly becomes a gigabit. */
/* AND WHETHER THE SHOP SELLS IT, which is a separate fact from the price and
 * was inferred from it until D41. Three of these seven cost the landlord
 * nothing and none of the three is his to order: the handoff belongs to the
 * ISP, a desk belongs to the tenant, and the workstation was already in the
 * MDF when he got the keys. `sale` is that fact written down, so the
 * catalogue page and site_install()'s refusal ask the same question. */
/* AND THE TWO COLUMNS THE GRADES ADDED. `disk` is the days of average use the
 * platter is rated for, 0 for a box with no disk in it, and it is read by
 * core/siteday.c's wear loop rather than by a constant that was the same for
 * every box in the building. `ups` is whether one arrives with a battery
 * under it. Neither is a multiplier and neither is consulted by anything that
 * carries a frame: the grade difference the player will actually feel is
 * `slow_mb` and `ports`, doing arithmetic in netstack. */
/* AND `buf_kb`, WHICH IS CAPACITY AS OPPOSED TO SPEED.
 *
 * The owner: "we should have the switches with more parts be able to handle
 * more capacity not just be more ports." He is right and the measurement
 * agrees with him. Every port in this game held the same 48 KB, and a planned
 * tower's server ports sat pegged at 406us of queue while DROPPING at twenty
 * percent utilisation -- because 48 KB at a gigabit is 393us of wire plus one
 * frame in service, so those ports were not busy, they were FULL. Two blind
 * playtesters lost runs to that and neither could act on it: voice at 3.7%
 * concealed with 4ms of delay and 19us of jitter is not late audio, it is
 * lost audio, and no amount of circuit fixes a full buffer. `demand` already
 * told them so and the game had nothing to sell them.
 *
 * Bandwidth is how fast a queue DRAINS. This is how much burst it can hold
 * while it drains, and they are different purchases. A switch4 and a switch24
 * used to differ only in how many holes they have; now the dear one rides out
 * an opening rush the cheap one drops, which is the difference a person who
 * has bought both would expect. */
static const struct {
    const char *name; int ports; int price; int slow_mb; int nfast; bool sale;
    int disk; bool ups; int buf_kb;
} KIT[SDEV_KIND_COUNT] = {
    /* the ISP's socket. Not for sale, and site_isp() rate-limits it to the
     * circuit the landlord has actually bought. */
    { "uplink",   1,    0, 10000, 0, false,   0, false,  48 },
    /* THE CHEAP GRADE, AND IT IS CHEAP FOR A REASON YOU CAN MEASURE. Four
     * holes at a hundred megabits. The buffer on the back of a port is the
     * same 48 KB it is on everything else in this game, which at 100 Mb is
     * 3.9 ms of wire to fill and ten times as long to drain -- so a floor of
     * desks fetching at once overruns it where a gigabit port would not, and
     * `load` prints the drops and `show` gives the reason in words. Nobody
     * wrote "the cheap switch is worse" anywhere. */
    { "switch4",  4,   45,   100, 0, true,    0, false,  16 },
    { "switch8",  8,  120,  1000, 0, true,    0, false,  48 },  /* all copper, a gigabit each */
    { "switch24", 24, 400,  1000, 2, true,    0, false, 128 },  /* ...and its SFP+ pair, 22 and 23   */
    { "router",   4,  650, 10000, 0, true,    0, false, 128 },  /* four sockets; as many vlans as you like */
    { "pc",       1,  480,  1000, 0, true,   60, false,  48 },
    /* THE SMALL-OFFICE SERVER. One hundred-megabit card and a disk rated for
     * half the life. It will hold a floor's files on day three for a third
     * of the money, and it is the box a second tenancy on that floor
     * outgrows -- which is the decision, because on day three there is no
     * second tenancy on that floor. */
    { "minitower",1,  460,   100, 0, true,   30, false,  16 },
    { "server",   2, 1350,  1000, 0, true,   60, false,  64 },  /* a gigabit NIC, and it is the one
                                                            * a flat tower falls over on        */
    /* AND THE ONE THAT UNBINDS IT. Two ten-gigabit cards, a disk rated for
     * twice the life, and a battery in it -- so a mains failure is a box
     * that stayed up rather than a filesystem to check. Two and a half
     * times the money, and the ten gigabit is only ten gigabit if the copper
     * and the port at the other end are: cat6 under 55 m, or fibre. */
    { "rackserver",2,3400, 10000, 2, true,  120, true,  256 },
    /* A DESK IS NOT FOR SALE. It is the tenant's own computer and it costs
     * the landlord nothing; what the landlord sells is the port it is
     * plugged into and the network behind that port. It is here in the
     * catalogue anyway because it is a device in the site with a card in it
     * and a name, and everything else in this file has to be able to say so. */
    { "desk",     1,    0,  1000, 0, false,  60, false, 48 },
    /* AND NEITHER IS THE PLAYER'S OWN WORKSTATION, for the same kind of
     * reason and a different one: it is theirs already. One gigabit socket,
     * an operating system, and it is standing in the MDF on the morning of
     * day one with its lead in the handoff. Everything else about it is a pc.  */
    { "workstation", 1, 0,  1000, 0, false, 60, false, 48 },
};

int site_kind_port_mb(int kind, int port)
{
    if (kind < 0 || kind >= SDEV_KIND_COUNT) return 1000;
    if (port >= KIT[kind].ports - KIT[kind].nfast) return 10000;
    return KIT[kind].slow_mb;
}

const char *site_kind_name(int kind)
{
    return (kind >= 0 && kind < SDEV_KIND_COUNT) ? KIT[kind].name : "?";
}
/* HOW MUCH BURST A PORT OF THIS KIND HOLDS, in bytes. See the note on KIT[]:
 * this is capacity, which is a different purchase from speed. */
int site_kind_port_buffer(int kind)
{
    if (kind < 0 || kind >= SDEV_KIND_COUNT) return 0;
    return KIT[kind].buf_kb * 1024;
}

int site_kind_ports(int kind)
{
    return (kind >= 0 && kind < SDEV_KIND_COUNT) ? KIT[kind].ports : 0;
}
int site_kind_price(int kind)
{
    return (kind >= 0 && kind < SDEV_KIND_COUNT) ? KIT[kind].price : 0;
}
bool site_kind_is_switch(int kind)
{
    return kind == SDEV_SWITCH4 || kind == SDEV_SWITCH8 ||
           kind == SDEV_SWITCH24;
}
bool site_kind_is_server(int kind)
{
    return kind == SDEV_MINITOWER || kind == SDEV_SERVER ||
           kind == SDEV_RACKSERVER;
}
int site_kind_disk_days(int kind)
{
    return (kind >= 0 && kind < SDEV_KIND_COUNT) ? KIT[kind].disk : 0;
}
bool site_kind_has_ups(int kind)
{
    return (kind >= 0 && kind < SDEV_KIND_COUNT) && KIT[kind].ups;
}
bool site_kind_for_sale(int kind)
{
    return (kind >= 0 && kind < SDEV_KIND_COUNT) && KIT[kind].sale;
}

/* ------------------------------------------------------ the industries
 * One table, because the name, the sentence a player reads before signing
 * the lease, and the money they pay are three faces of one fact -- and this
 * project has been bitten twice by the same fact living in two places (the
 * `demand` footer that told a player a switch24 seats 23 desks, and the
 * complaint threshold that read "Three" in `status` and a computed number in
 * `service`). The rent premium is what that industry pays for the same
 * square metres, as a percentage of an office. */
static const struct {
    const char *name;
    const char *wants;
    int         rent_pct;
} TRADE[TEN_KIND_COUNT] = {
    { "office",   "throughput at nine, and patient",   100 },
    { "web host", "uptime, and reachable INWARDS",     240 },
    { "voice",    "no loss, no jitter. Not bandwidth", 170 },
    { "studio",   "sustained UPLOAD, all of it",       300 },
};

const char *site_tenant_kind_name(int k)
{
    return (k >= 0 && k < TEN_KIND_COUNT) ? TRADE[k].name : "?";
}
const char *site_tenant_kind_wants(int k)
{
    return (k >= 0 && k < TEN_KIND_COUNT) ? TRADE[k].wants : "?";
}
/* WHAT THIS TRADE COUNTS. `tried` and `finished` are one pair of integers for
 * every tenancy, and they mean a different thing in each: an office finishes
 * transfers, a call centre finishes CALLS, a web host serves visitors, a
 * studio lands uploads. `service` already says so in its legend, and then the
 * person at the desk said "0 of 18 things we tried finished" to a playtester
 * on a day the row above was carefully saying "18 of 18 calls broke up". One
 * word, in one place, so the two cannot drift. */
const char *site_tenant_kind_unit(int k, bool plural)
{
    switch (k) {
    case TEN_VOICE:   return plural ? "calls"    : "call";
    case TEN_WEBHOST: return plural ? "visitors" : "visitor";
    case TEN_STUDIO:  return plural ? "uploads"  : "upload";
    default:          return plural ? "transfers": "transfer";
    }
}
/* THE HEADLINE IS THE SUM OF THE ROWS, and this is the only place either is
 * added up. See site.h: `day` and `status` printed SiteDay.sessions -- every
 * unit of work the tower carried -- under the word "transfers", beside a
 * `service` page whose rows are each tenancy's own judged units. On a tower
 * with one office and one call centre that read "134 transfers" over rows
 * summing to 98, the difference being two CRM transfers behind every call,
 * which nothing anywhere said. Summing the rows makes the two numbers one
 * number, and a check asserts they are still the same arithmetic. */
void site_day_work(const Site *s, int *done, int *tried, const char **unit)
{
    int d = 0, tr = 0, kind = -1;
    bool mixed = false;
    for (int i = 0; i < s->ntenant; i++) {
        const SiteTenant *t = &s->tenant[i];
        if (!t->moved || t->tried <= 0) continue;
        d += t->finished;
        tr += t->tried;
        if (kind < 0) kind = t->kind;
        else if (kind != t->kind) mixed = true;
    }
    if (done)  *done  = d;
    if (tried) *tried = tr;
    /* One trade in the building gets its own word; a mix gets a word that is
     * true of all four, because "134 transfers" at a call centre is exactly
     * the sentence this exists to stop being written. */
    if (unit) *unit = (kind < 0 || mixed) ? "jobs" : site_tenant_kind_unit(kind, true);
}

int site_tenant_rent_pct(int k)
{
    return (k >= 0 && k < TEN_KIND_COUNT) ? TRADE[k].rent_pct : 100;
}
bool site_kind_has_os(int kind)
{
    return kind == SDEV_PC || kind == SDEV_SERVER ||
           kind == SDEV_MINITOWER || kind == SDEV_RACKSERVER ||
           kind == SDEV_WORKSTATION;
}
int site_kind_by_name(const char *name)
{
    for (int i = 0; i < SDEV_KIND_COUNT; i++)
        if (strcmp(KIT[i].name, name) == 0) return i;
    return -1;
}

/* Cable is priced by the metre, which is the whole reason the two distances
 * in building.h are different numbers: the player is choosing between
 * carrying the box further and paying for more copper.
 *
 * AND BY THE RUN, which is where most of the money in a real building goes
 * and where none of it went until D27. A playtester closed a run with 94,087
 * in hand having spent 28,927 of which cable was 22%, and said: *"I never
 * once had to choose the cheaper run."* They were right, and the reason is
 * that this table priced the DRUM. A drum is the cheap part. What costs is
 * the person: pulling the run, terminating both ends, punching it down and
 * testing it, and that cost lands per RUN and not per metre -- which is why
 * `ends` is now the larger half of a desk drop and why fibre, whose ends are
 * a pair of optics rather than a plug, is dear before it is a metre long.
 *
 * AND THE LABOUR IS THE SAME WHATEVER IS ON THE DRUM, which is what puts the
 * grade decision where it belongs. Pulling and terminating a run costs a
 * person the same hour whether the jacket says 5 or 6, so the three copper
 * grades have almost the same `ends` and differ almost entirely by the
 * metre. The consequence is the point: on a twenty metre desk drop the
 * cheapest copper saves about a seventh, which is not worth the risk and
 * should not be -- a desk pulls nine megabits and a hundred megabit drop
 * carries it fine, so a game that punished cat 5 at a desk would be lying.
 * On an eighty metre riser it saves nearly a third, and that is the run
 * where a hundred megabit takes a floor of desks down with it. Cheap copper
 * is a temptation exactly where it is a mistake, and free where it is not.
 *
 * So all three halves of the decision now cost. How FAR the box is from the
 * desks is the per-metre. How MANY runs the topology needs -- a second
 * switch on the floor against twenty drops home-run to the basement -- is
 * the ends. WHICH GRADE is the per-metre again, on the runs that are long.
 * And `site_uncable` still refunds nothing, so a run laid badly is paid for
 * twice. */
static const struct { const char *name; int per_100m; int ends; } SPOOL[CAB_KIND_COUNT] = {
    { "cat5e",  55,  75 },
    { "cat6",   95,  80 },
    /* Fibre's ends are not a plug: they are a pair of optics, and that is
     * most of what a fibre run costs before it is a metre long. */
    { "fibre", 260, 340 },
    /* The cheapest drum on the shelf, and a hundred megabit for the rest of
     * its life. The saving is in the metres, so it is a rounding error on a
     * desk drop and real money on a riser -- which is the one place it takes
     * a floor of desks with it. This file's own load gate has measured a
     * hundred megabit run under two floors of desks filling to 97% since
     * D25; until D27 no player could buy the drum that does it. */
    { "cat5",   20,  70 },
};
const char *site_cable_name(CableKind k)
{
    return (k >= 0 && k < CAB_KIND_COUNT) ? SPOOL[k].name : "?";
}
int site_cable_price(CableKind k, int metres)
{
    if (k < 0 || k >= CAB_KIND_COUNT || metres < 0) return 0;
    return (metres * SPOOL[k].per_100m + 99) / 100 + SPOOL[k].ends;
}

/* ------------------------------------------------------------- the jack */
/* THE SAME METRES, BOUGHT THE OTHER WAY, and the numbers are chosen so that
 * the comparison is a real one rather than a tax.
 *
 * A jack is the same pull, the same drum and the same person, plus the part
 * that makes it permanent: a faceplate on the wall, a panel port punched down
 * and labelled at the far end, and the pair tested and certified. That is
 * JACK_FIT on top of the spool price of the identical run, so a jack is
 * ALWAYS dearer than spooling the run once, and there is no reading of the
 * price list where jacking a room you will only ever put one box in was the
 * cheap answer.
 *
 * What it buys back is JACK_LEAD: a factory patch lead, the price the ends of
 * a run cost before D27 made them a job. Every box that stands in that room
 * afterwards -- the second switch when the floor fills, the same switch after
 * somebody carried it out and back, the server that replaces it -- reaches
 * the far end for that, for good, because the copper is in the wall and is
 * not pulled out when the box goes.
 *
 * On the 42 m riser D27 measured: 99 off the spool, 189 for a jack, and 12 a
 * lead after that. One box in that room and the jack cost you 102 for
 * nothing. Three boxes over the life of the run and the spool cost 297 and
 * the jack 225. The break-even is between two and three, which is exactly
 * where a decision about a floor that might grow ought to sit. */
#define JACK_FIT   90    /* faceplate, panel port, punch down, test, label   */
#define JACK_LEAD  12    /* a made-up lead off the shelf, both ends moulded  */
int site_jack_price(CableKind k, int metres)
{
    if (k < 0 || k >= CAB_KIND_COUNT || metres < 0) return 0;
    return site_cable_price(k, metres) + JACK_FIT;
}
int site_jack_lead_price(void) { return JACK_LEAD; }
/* AND THE DAYS, which are the half of this the money cannot reach. Somebody
 * has to come, and a longer pull is more of their time: a day to book them,
 * and a day per forty metres of tray. A jack in the room you are standing in
 * is two days; the 42 m riser is three; a 91 m run across the building is
 * four. A tenancy moves in, unpacks for three days and files on the sixth --
 * so a jack ordered the day they move in is a socket in time and a jack
 * ordered the day they start striking is not. */
int site_jack_days(int metres)
{
    if (metres < 0) metres = 0;
    return 1 + (metres + 39) / 40;
}

/* ------------------------------------------------ what the copper carries */
/* MEASURED, NOT RESTATED. How far each grade reaches and what it settles at
 * over that distance are rules in core/netstack.c and they are private to it
 * -- correctly, because the frames obey them. So the only honest way to
 * answer "what would this run negotiate?" before laying it is to lay one:
 * two switches and a cable in a world of their own, and read the same
 * net_port_speed() that `show` and `load` print off a real port. Nothing here
 * knows that cat 6 stops doing ten gigabit at fifty-five metres or that
 * copper stops at a hundred, and nothing here can therefore disagree.
 *
 * A whole Net is not a small object, so all four grades are done in one
 * world and the world is thrown away at the end of the call. */
void site_cable_speeds(int metres, int *out)
{
    for (int i = 0; i < CAB_KIND_COUNT; i++) out[i] = 0;
    if (metres < 0) return;
    Net *n = net_new(1);
    if (!n) return;
    int a = net_add_switch(n, "a", CAB_KIND_COUNT);
    int b = net_add_switch(n, "b", CAB_KIND_COUNT);
    /* One at a time, and pulled out again: four cables between the same two
     * switches at once would be a loop, and a loop is not what is being
     * asked about. */
    if (a >= 0 && b >= 0)
        for (int i = 0; i < CAB_KIND_COUNT; i++) {
            int c = net_cable(n, a, 0, b, 0, metres, (CableKind)i);
            if (c < 0) continue;
            out[i] = net_port_speed(n, a, 0);
            net_uncable(n, c);
        }
    net_free(n);
}
int site_cable_speed(CableKind k, int metres)
{
    int mb[CAB_KIND_COUNT];
    if (k < 0 || k >= CAB_KIND_COUNT) return 0;
    site_cable_speeds(metres, mb);
    return mb[k];
}

const char *site_err_text(int e)
{
    switch (e) {
    case SITE_OK:       return "done";
    case SITE_ENODEV:   return "no such device";
    case SITE_ENOROOM:  return "no such room";
    case SITE_ENOPORT:  return "that box has not got that port";
    case SITE_EBUSY:    return "something is already plugged into that port";
    case SITE_ENOROUTE: return "there is no cable route between those rooms";
    case SITE_ESPACE:   return "the site is full";
    case SITE_EMONEY:   return "not enough money";
    case SITE_EIFACE:   return "no such interface on that box";
    case SITE_ECABLED:  return "it has a cable in it -- unplug it first";
    case SITE_EFIXED:   return "that is not yours to move";
    case SITE_EADDR:    return "that is the network or broadcast address of "
                               "its own subnet, not a machine's";
    case SITE_EVLAN:    return "a vlan is a number from 1 to 4094";
    case SITE_ENOTSW:   return "only a switch has ports with vlans on them -- "
                               "on a router, `subif <box> <nic> <vlan> <ip>`";
    case SITE_EOFF:     return "it is switched off, and a box that is not "
                               "running is not on the network";
    case SITE_ENOBTN:   return "it has no power button -- it comes up with the "
                               "socket it is plugged into";
    case SITE_ESEG:     return "no interface of that box is on that pool's "
                               "subnet -- a pool serves the segment the box "
                               "is standing on";
    /* ONE ERROR, ONE THING THAT HAPPENED. These were one code and one
     * sentence, and the sentence led with the reason that was false: a pool
     * of 180 addresses refused for being the ninth on the box was told
     * about pools of no addresses first. */
    case SITE_EPOOL:    return "a pool of no addresses serves nobody -- "
                               "`dhcpd <box> <first> <count> <bits> <gw> "
                               "<dns>`, and <count> is how many addresses";
    case SITE_EPOOLS:   return "that box already holds eight pools, which is "
                               "all one box holds -- `dhcpd <box>` lists them "
                               "and `dhcpd <box> off` stops them all";
    case SITE_EZONE:    return "that name server already holds sixty-four "
                               "names, which is all a zone here has room for";
    case SITE_EEARLY:   return "the trade has not been yet -- that jack is a "
                               "day in the diary, not a socket on the wall";
    case SITE_EJACK:    return "that socket is punched down to a jack and is "
                               "not a free port -- the pair is terminated";
    case SITE_ENOMAINS: return "there is no free outlet on that room's wall -- "
                               "`outlet` puts another socket in, `outlets` says "
                               "which rooms have one free";
    case SITE_EUNPLUGGED: return "it is not plugged into anything -- there is no "
                                 "lead from it to a wall socket. `mains <box> on`";
    case SITE_ECIRCUIT: return "that room is on one final circuit and it is "
                               "full -- there is no more power to bring into "
                               "it";
    /* THE CIRCUIT IS MEGABITS. See SITE_EMBIT in site.h: this used to
     * borrow SITE_EADDR and answer `isp 0` with a sentence about subnets. */
    case SITE_EMBIT:    return "the circuit is a number of MEGABITS and the "
                               "smallest the ISP will sell is 10 -- `isp` on "
                               "its own says what you have now";
    case SITE_ENOTIN:   return "that tenancy has not moved in yet";
    }
    return "?";
}

/* ------------------------------------------------------------ addressing */
int site_hosts_in_mask(uint32_t mask)
{
    int bits = net_mask_len(mask);
    if (bits >= 31 || bits <= 0) return 0;   /* a /31 has no host addresses */
    long n = 1L << (32 - bits);
    return (int)(n - 2);                     /* network and broadcast: not  */
}

/* --------------------------------------------------------------- day one */
/* Where the ISP hands over. 198.51.100.0/30 is documentation space and this
 * is documentation: two usable addresses, theirs and yours. */
#define WAN_THEIRS  198, 51, 100, 1
#define WAN_YOURS   198, 51, 100, 2

/* The zone and the pages of the in-game internet. Content, in net_sites.c. */
int  net_site_hosts(int i, const char **host, const char **ip);

/* Both defined below, and both are day-one work: the workstation is installed
 * the way any box is installed, and its lead is run the way any lead is run. */
static int install_dev(Site *s, int kind, int room, const char *name);
static int cable_run(Site *s, int a, int aport, int b, int bport, CableKind k,
                     int m, int cost, int jack);

bool site_new(Site *s, const Building *b, uint64_t seed, long budget)
{
    memset(s, 0, sizeof *s);
    s->b = b;
    s->seed = seed;
    s->uplink = -1;
    s->ws = -1;
    s->yielded = -1;
    s->money = budget;
    if (!b || b->floors <= 0) return false;
    int mdf = bld_find(b, 0, RM_MDF);
    if (mdf < 0) return false;

    s->net = net_new(seed ^ 0x5171e0ull);
    if (!s->net) return false;

    /* THE ONE THING THAT EXISTS. A socket on a wall in the MDF with a router
     * on the far side of it that belongs to somebody else. It is already
     * addressed, it already answers, and it is already the only way out of
     * the building -- and nothing in the tower is plugged into it. */
    SiteDev *d = &s->dev[s->ndev];
    memset(d, 0, sizeof *d);
    d->kind = SDEV_UPLINK;
    d->room = (uint16_t)mdf;
    d->floor = 0;
    d->nports = 1;
    d->powered = 1;            /* somebody else's router, and it is running */
    /* AND IT IS IN A SOCKET ON THE MDF WALL, which is one of the MDF's
     * outlets gone before the player has bought anything. It was there
     * before they were and site_mains() refuses to pull it out, the same
     * sentence site_move already makes about carrying it off. */
    d->mains = 1;
    snprintf(d->name, sizeof d->name, "uplink");
    d->node = net_add_host_nics(s->net, d->name, d->nports);
    if (d->node < 0) return false;
    s->uplink = s->ndev++;

    s->wan_isp  = net_ip(WAN_THEIRS);
    s->wan_you  = net_ip(WAN_YOURS);
    s->wan_mask = net_mask_bits(30);
    net_if_addr(s->net, d->node, 0, s->wan_isp, s->wan_mask);
    net_forwarding(s->net, d->node, true);
    /* The way back into the tower is whatever the player puts on the other
     * end of the handoff. Until then these routes point at an address nobody
     * has claimed, which is honest: the ISP has provisioned a /30 and you
     * have not connected anything to it. */
    net_route_add(s->net, d->node, net_ip(10, 0, 0, 0), net_mask_bits(8),
                  s->wan_you, -1);
    net_route_add(s->net, d->node, net_ip(192, 168, 0, 0), net_mask_bits(16),
                  s->wan_you, -1);

    /* The ISP also resolves names and serves the web, because one box on the
     * far side of a /30 is all the internet this game needs, and the pages
     * already exist in net_sites.c. */
    net_dnsd(s->net, d->node);
    for (int i = 0; ; i++) {
        const char *h = NULL, *ip = NULL;
        if (!net_site_hosts(i, &h, &ip)) break;
        uint32_t a = 0;
        if (!net_parse_ip(ip, &a)) continue;
        net_dns_record(s->net, d->node, h, a);
        net_if_alias(s->net, d->node, a);
    }
    net_httpd(s->net, d->node, 80);

    /* WHAT THE CIRCUIT CARRIES, which is not what the fibre in the street
     * carries. The handoff is rate-limited to what the landlord has bought,
     * because a media converter on a wall is exactly that, and it is why a
     * tower with gigabit copper in every riser can still be starved of the
     * internet. `isp <mb>` buys a bigger one and the ISP charges for it. */
    s->day = 0;
    s->isp_mb = SITE_ISP_MB_DEFAULT;
    net_port_rate(s->net, d->node, 0, s->isp_mb);

    /* ------------------------------------------- AND THE MACHINE YOU SIT AT
     *
     * The second thing that exists, and until D41 it did not exist here at
     * all: the window drew a desk in the MDF and ran the desktop on a machine
     * that was on nobody's network. So the shop could not be taken away by
     * anything the player did to their own building, and the owner's escape
     * -- *"if your core switch dies, you can wire up your main box to use the
     * uplink"* -- was something you could only perform with a pc you had
     * bought.
     *
     * WHERE IT STANDS is the MDF. The owner said "the server room" and this
     * building generator does not make one on the ground floor: floor 0 is
     * goods in, the MDF, a comms cupboard, plant and the riser. The MDF is
     * the building's frame room -- it is where the handoff is, it is wired
     * for a frame with eight sockets, and it is the room a landlord's own
     * machine would be in. It also makes the day-one lead a three metre patch
     * lead between two boxes on the same wall rather than a fiction.
     *
     * AND IT IS IN THE HANDOFF'S ONLY PORT. That is the whole shape of it:
     * the first switch the player buys cannot reach the internet until they
     * have unplugged their own machine and re-cabled it, so they perform the
     * escape route forwards, on day one, before they need it. The lead costs
     * nothing because it was there before they were. */
    int ws = install_dev(s, SDEV_WORKSTATION, mdf, "ws");
    if (ws < 0) return false;
    s->ws = ws;
    /* It is running. A landlord walks into the MDF on the first morning and
     * their own machine is on -- and site_power() can switch it off again
     * like any other box, with the same consequences. */
    if (!site_power(s, ws, true)) return false;
    int lead = cable_run(s, ws, 0, s->uplink, 0, CAB_CAT5E, SITE_PATCH_M, 0, -1);
    if (lead < 0) return false;
    s->link[lead].factory = 1;
    /* AND IT IS ADDRESSED FOR THAT SOCKET, because somebody set it up before
     * the player got here: the /30's other address, the ISP as the way out
     * and the ISP as the resolver. Nothing else in the building has an
     * address until the player writes one.
     *
     * This is also the second half of the day-one decision. The handoff's
     * /30 has exactly two usable addresses and the workstation is sitting on
     * the one a router wants, so a player who builds their tower properly
     * takes it back -- and their own machine then needs an address on the
     * network they built, like everything else they will ever plug in. */
    site_addr(s, ws, 0, s->wan_you, s->wan_mask);
    site_gateway(s, ws, s->wan_isp);
    site_resolver(s, ws, s->wan_isp);

    /* --------------------------------------------------- who is moving in */
    /* One tenancy per Room.tenant, each with an arrival day and a set of
     * requirements drawn from this seed and no other. The building decided
     * who holds which room; this decides what they ask for once they have
     * the keys, and their arithmetic is the entire difficulty curve. */
    Rng r;
    rng_seed(&r, seed ^ 0xde4a5dull);
    /* THE TRADE IS DRAWN FROM ITS OWN STREAM, and that is not fastidiousness.
     * Taking it out of `r` would shift every draw after it -- the servers,
     * the segments and, through the slippage term, the whole letting queue --
     * so adding industries would silently re-schedule every tower in the
     * project and move numbers in four other gates that have nothing to do
     * with industries. A new fact about the world gets a new stream. */
    Rng tr;
    rng_seed(&tr, seed ^ 0x7ade9c0ffeeull);
    for (int i = 0; i < b->nrooms && s->ntenant < SITE_MAX_TENANT; i++) {
        const Room *rm = &b->rooms[i];
        if (!rm->tenant) continue;
        bool seen = false;
        for (int t = 0; t < s->ntenant; t++)
            if (s->tenant[t].tenant == rm->tenant) { seen = true; break; }
        if (seen) continue;
        int rooms = 0;
        double area = 0;
        for (int k = 0; k < b->nrooms; k++)
            if (b->rooms[k].tenant == rm->tenant) {
                rooms++;
                area += bld_room_area(&b->rooms[k]);
            }
        SiteTenant *t = &s->tenant[s->ntenant++];
        t->floor = rm->floor;
        t->tenant = rm->tenant;
        t->room = (uint16_t)i;
        /* Drops come off floor area, because desks come off floor area. A
         * big office wants a lot of ports and a studio flat wants one, and
         * neither number was chosen to be interesting. */
        int per = (rm->kind == RM_RESIDENCE) ? 60 : 12;
        int n = (int)(area / per) + 1;
        if (n > 20) n = 20;
        t->drops = (uint8_t)n;
        t->wants_server = (uint8_t)(rooms >= 3 && rng_range(&r, 0, 99) < 45);
        t->own_segment  = (uint8_t)(n >= 6 || rng_range(&r, 0, 99) < 25);
        /* ------------------------------------------- WHAT BUSINESS THEY ARE IN
         *
         * The draw is off the same seed as everything else about them, so a
         * tower always lets to the same industries in the same order, and it
         * is weighted so that the OFFICE stays the common case: it is the
         * baseline the whole difficulty curve was calibrated against, and a
         * tower where every other floor is exotic would be a different game
         * rather than a richer one.
         *
         * The weights are read off the room, not off nothing. A flat is let
         * to a person, and the person who fills a flat with network is a
         * content creator; a let floor is let to a company, and roughly a
         * third of the companies who need a rack in a building like this are
         * not offices. */
        int roll = (int)rng_range(&tr, 0, 99);
        if (rm->kind == RM_RESIDENCE)
            t->kind = (uint8_t)(roll < 22 ? TEN_STUDIO : TEN_OFFICE);
        else if (roll < 14) t->kind = TEN_VOICE;
        else if (roll < 25) t->kind = TEN_WEBHOST;
        else if (roll < 34) t->kind = TEN_STUDIO;
        else                t->kind = TEN_OFFICE;
        /* AND WHAT THE BUSINESS ASKS THE LANDLORD FOR, which is not the same
         * as how much space it takes. THE DROPS ARE LEFT ALONE ON PURPOSE:
         * ports come off floor area because desks come off floor area, and a
         * hosting company with a big floor has a big floor. Making a trade
         * change the desk count was tried and it was wrong twice over -- it
         * moved every desk-count number in every other gate in the project
         * for no gain, and the differences that matter here are what those
         * desks ASK FOR and what the tenancy PAYS, not how many of them
         * there are. */
        if (t->kind == TEN_WEBHOST)
            t->wants_server = 1;           /* the origin IS their business    */
        else if (t->kind == TEN_VOICE)
            t->own_segment = 1;            /* a voice vlan, for the reason    */
                                           /* every voice engineer gives      */
        /* THE RENT FOLLOWS WHAT THEY NEED. Square metres, at the rate for the
         * kind of space, times what that industry pays for a network that
         * does the thing they are buying. A studio pays triple; a web host
         * pays for uptime and hands it back on a day they were down; a voice
         * business pays for a network that does not drop their calls. */
        long base = (long)(area * (rm->kind == RM_RESIDENCE ? 14 : 26));
        t->rent = (int)(base * site_tenant_rent_pct(t->kind) / 100);
        t->day = 0;                        /* the queue below decides this   */
    }

    /* ------------------------------------------------- WHEN THEY MOVE IN
     *
     * THE LETTING QUEUE, and it replaces a draw that produced a dead zone.
     * Each tenancy used to take an independent day -- `rng(1,40) + floor*12`
     * -- which has two faults a playtest found the hard way. Twelve days a
     * floor means a tower whose lowest let floor is the fourth cannot have a
     * tenant before day forty-nine, and the gate seed is exactly that: a
     * player reached day eighty-five having finished the whole building by
     * day twenty-five and then pressed `day` twenty-four times into an empty
     * tower. Their words: *"the optimal play is to build everything up front
     * and then switch it off, which is the opposite of demand outgrowing the
     * infrastructure."* And independent draws bunch: two tenancies can land
     * on the same day and then nothing for a fortnight, so the rate the
     * player feels is noise rather than pressure.
     *
     * A letting agent does not work like that. Floors come into service from
     * the bottom, viewings are booked back to back, and the next lease is
     * signed as soon as the last one is. So the schedule is a QUEUE: floor
     * order, first lease within the first few days, and the gap to the next
     * one is how long that lease takes to fit out -- a day plus a day per
     * six desks, plus nought to two days of slippage. A one-drop studio is
     * signed a day or two after the last; a twenty-desk office buys the
     * player four to six days.
     *
     * THE POINT OF THE ARITHMETIC IS THAT IT OUTPACES A COMFORTABLE BUILD.
     * Ordering, carrying, cabling and configuring for twenty desks is more
     * than four days of the player's attention if they do it tidily, so the
     * queue is always slightly ahead of them -- which is the loop the owner
     * asked for and not a difficulty constant, because every term in it is
     * this seed's own building.
     *
     * Floor order first, so a tower fills from the bottom the way a building
     * really lets. An insertion sort on (floor, room): stable, and a
     * schedule that depended on qsort's tie-breaking would stop reproducing
     * from a seed. */
    for (int i = 1; i < s->ntenant; i++) {
        SiteTenant key = s->tenant[i];
        int j = i - 1;
        while (j >= 0 && (s->tenant[j].floor > key.floor ||
                          (s->tenant[j].floor == key.floor &&
                           s->tenant[j].room > key.room))) {
            s->tenant[j + 1] = s->tenant[j];
            j--;
        }
        s->tenant[j + 1] = key;
    }
    int when = rng_range(&r, 1, 3);
    for (int i = 0; i < s->ntenant; i++) {
        s->tenant[i].day = when;
        /* How long the next lease takes to be ready: the fit-out is the
         * tenancy's own size, and the slippage is the world. */
        when += 1 + (s->tenant[i].drops + 3) / 6 + rng_range(&r, 0, 2);
    }
    return true;
}

int site_port_factory(const Site *s, int dev, int port)
{
    if (!s || dev < 0 || dev >= s->ndev) return -1;
    for (int i = 0; i < s->nlink; i++) {
        const SiteLink *l = &s->link[i];
        if (l->cable < 0 || !l->factory) continue;
        if ((l->a == dev && l->aport == port) ||
            (l->b == dev && l->bport == port)) return i;
    }
    return -1;
}

int site_workstation(const Site *s)
{
    return (s && s->ws >= 0 && s->ws < s->ndev &&
            s->dev[s->ws].kind == SDEV_WORKSTATION) ? s->ws : -1;
}

void site_free(Site *s)
{
    if (!s) return;
    if (s->net) net_free(s->net);
    s->net = NULL;
    s->ndev = s->nlink = s->ntenant = 0;
}

void site_credit(Site *s, long amount) { s->money += amount; }

/* -------------------------------------------------------------- distance */
int site_metres(const Site *s, int room_a, int room_b)
{
    if (!s->b || room_a < 0 || room_b < 0) return -1;
    if (room_a >= s->b->nrooms || room_b >= s->b->nrooms) return -1;
    double *d = nom_alloc(sizeof(double) * (size_t)s->b->nrooms);
    if (!bld_cable_all(s->b, room_a, d)) { nom_free(d); return -1; }
    double v = d[room_b];
    nom_free(d);
    if (v >= BLD_INF) return -1;
    return SITE_PATCH_M + (int)(v + 0.5);
}

/* AND THE ONE RULE ABOUT A RUN'S METRES THAT IS NOT DISTANCE. The ISP's
 * handoff is on the far side of a wall the landlord has no key to, so it has
 * no room in this building (BLD_NOROOM) and the lead into it is a patch lead.
 * That line used to be written out in site_cable, in site_jack and again in
 * the session's own spool -- three copies of the same sentence, and a quote
 * would have made a fourth. One function, so the price a quote prints and the
 * price the bill charges cannot come from different arithmetic. */
int site_run_metres(const Site *s, int room_a, int room_b)
{
    if (room_a == BLD_NOROOM || room_b == BLD_NOROOM) return SITE_PATCH_M;
    return site_metres(s, room_a, room_b);
}

/* ================================================================= POWER ==
 * See the long note above SiteSocket in site.h. This is the model: how many
 * holes are in a room's wall, which box is in which of them, and what a
 * player has to do about it.
 */
static void power_down(Site *s, int dev);        /* defined with the button */

/* WHAT THE BUILDING WAS WIRED WITH, and every number is a defensible figure
 * for that kind of space rather than a difficulty knob. The shape of the
 * table is the argument: the rooms built to hold equipment have a handful of
 * sockets on a spur and the rooms built to hold PEOPLE have them everywhere,
 * because that is how buildings are wired -- and it is why the decision this
 * makes lands in a comms cupboard and never in an office.
 *
 * The owner asked that "each room should have at least one power outlet",
 * and every room a person can walk into has one. A lift shaft is not a room
 * anybody walks into, and a riser is a shaft with one maintenance socket in
 * it -- both of those are the building generator's own words. */
static int outlets_built_in(const Room *r)
{
    double a = bld_room_area(r);
    switch (r->kind) {
    /* The building's own frame room: it was wired for a frame. */
    case RM_MDF:      return 8;
    /* A TENANT'S SERVER ROOM. The one space in this world built to hold
     * equipment, which is the same sentence the heat model makes about it. */
    case RM_SERVER:   return 6 + (int)(a / 10.0);
    /* AND THE ONE THAT BITES. A floor's comms cupboard is a cupboard: a
     * twin socket and a spare off a spur, and no more, because nobody ran a
     * distribution board up a riser for a cupboard with a switch in it. Four
     * is exactly the owner's own example -- "a cupboard with three switches
     * and a server in it" -- sitting on the limit, which is what makes it a
     * decision rather than an assumption. */
    case RM_COMMS:    return 4;
    case RM_PLANT:    return 4;
    case RM_GOODS:    return 2;
    case RM_RISER:    return 1;
    case RM_LIFT:     return 0;         /* not a room anybody walks into    */
    case RM_TOILET:   return 1;         /* the shaver socket                */
    /* LET SPACE IS WIRED FOR PEOPLE, so it has a socket every few metres and
     * running out of them is not a thing that happens to a floor of desks.
     * That is not generosity, it is the truth about an office, and it is
     * what keeps the mechanic where the equipment is. */
    case RM_OFFICE: case RM_RESIDENCE: case RM_RETAIL:
        return 2 + (int)(a / 8.0);
    /* Corridors, stairs, lift lobbies, the entrance hall: the cleaner's
     * socket, and it is one. A corridor is not somewhere kit lives, and the
     * count is what says so. */
    default:          return 1;
    }
}

int site_room_outlets_built(const Site *s, int room)
{
    if (!s->b || room < 0 || room >= s->b->nrooms) return 0;
    return outlets_built_in(&s->b->rooms[room]);
}

int site_room_outlets(const Site *s, int room)
{
    int n = site_room_outlets_built(s, room);
    if (n <= 0) return 0;                     /* nothing to extend from     */
    for (int i = 0; i < s->nsock; i++) if (s->sock[i].room == room) n++;
    return n;
}

/* AS MANY AGAIN AS IT WAS BUILT WITH, and then the room is finished. A final
 * circuit takes the sockets it takes; the way to power a tenth box in a
 * four-socket cupboard is not a bigger cheque, it is a different room. This
 * is the one limit in the power model that money cannot move, which is why
 * it is here and not in the price. */
int site_room_outlets_max(const Site *s, int room)
{
    return site_room_outlets_built(s, room) * 2;
}

int site_room_outlets_used(const Site *s, int room)
{
    int n = 0;
    for (int i = 0; i < s->ndev; i++)
        if (s->dev[i].room == room && s->dev[i].mains) n++;
    return n;
}

int site_room_outlets_free(const Site *s, int room)
{
    int f = site_room_outlets(s, room) - site_room_outlets_used(s, room);
    return f > 0 ? f : 0;
}

int site_room_outlet_dev(const Site *s, int room, int nth)
{
    int seen = 0;
    for (int i = 0; i < s->ndev; i++)
        if (s->dev[i].room == room && s->dev[i].mains && seen++ == nth) return i;
    return -1;
}

/* Does this kind of box draw from the landlord's wall at all? A tenant's
 * desk does not: it is their machine in their room on their own socket, and
 * it is left out of the heat model for the same reason and in the same
 * words. Everything the player can buy does. */
static bool draws_mains(int kind) { return kind != SDEV_DESK; }

/* Put the plug in, if there is anywhere to put it. Returns whether it went
 * in. This is what `drop`, `deliver` and site_install all end with, because
 * a person who carries a box into a room plugs it in -- nobody sets a switch
 * down and then wonders where the lead went. What the game has to be honest
 * about is the room that has no hole left, and that is the false return. */
static bool mains_attach(Site *s, int dev)
{
    SiteDev *d = &s->dev[dev];
    if (!draws_mains(d->kind)) return true;
    if (d->mains) return true;
    if (d->room == BLD_NOROOM) return false;
    if (site_room_outlets_free(s, d->room) <= 0) return false;
    d->mains = 1;
    /* AN APPLIANCE HAS NO BUTTON, so the plug is its button -- which is what
     * site.h has said about a switch since the pivot and what nothing could
     * act on until there was a plug. */
    if (!site_kind_has_os(d->kind)) {
        d->powered = 1;
        for (int p = 0; p < d->nports; p++)
            net_port_admin(s->net, d->node, p, true);
    }
    return true;
}

/* And take it out. `dirty` is whether a machine that was running is allowed
 * to find out the hard way; site_move passes false because a box being
 * carried had its plug pulled by somebody who was looking at it. */
static void mains_detach(Site *s, int dev, bool dirty)
{
    SiteDev *d = &s->dev[dev];
    if (!d->mains) return;
    if (dirty && site_kind_has_os(d->kind) && d->powered) site_unclean_stop(s, dev);
    d->mains = 0;
    if (d->powered) {
        d->powered = 0;
        if (site_kind_has_os(d->kind)) power_down(s, dev);
    }
    /* A SWITCH WITH NO POWER IN IT HAS NO LINK LIGHTS, and the box at the
     * far end of every one of its ports sees the link go down -- which is
     * what makes an unplugged switch diagnosable from anywhere else in the
     * building rather than only from the cupboard it is in. */
    if (!site_kind_has_os(d->kind))
        for (int p = 0; p < d->nports; p++)
            net_port_admin(s->net, d->node, p, false);
}

bool site_mains(Site *s, int dev, bool on)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    SiteDev *d = &s->dev[dev];
    if (!draws_mains(d->kind)) { s->err = SITE_ENODEV; return false; }
    /* The handoff is the ISP's and so is its socket. It was there before you
     * were and it is not yours to pull out, which is the same sentence
     * site_move already makes about moving it. */
    if (d->kind == SDEV_UPLINK) { s->err = SITE_EFIXED; return false; }
    if (!!d->mains == on) return true;
    if (on) {
        if (site_room_outlets_free(s, d->room) <= 0) { s->err = SITE_ENOMAINS; return false; }
        return mains_attach(s, dev);
    }
    mains_detach(s, dev, true);
    return true;
}

/* WHAT ANOTHER SOCKET COSTS: the fit-out, plus the run back to the shaft the
 * power comes up. Same shape as every other price in this game -- a flat
 * charge for the person and a rate for the metres -- and the metres are
 * bld_cable_all()'s, so a cupboard against the riser is cheap and the far
 * corner of a let floor is not. */
#define OUTLET_FIT      200
#define OUTLET_PER_M      8

static int power_source_room(const Site *s, int room)
{
    if (!s->b || room < 0 || room >= s->b->nrooms) return -1;
    int floor = s->b->rooms[room].floor;
    /* Up the riser on this floor if there is one, then the plant space, then
     * the building's own frame room -- which is the order the copper really
     * comes from and the order bld_find already knows. */
    static const int SRC[] = { RM_RISER, RM_PLANT, RM_MDF };
    for (int i = 0; i < 3; i++) {
        int r = bld_find(s->b, floor, SRC[i]);
        if (r >= 0 && r != room) return r;
    }
    for (int i = 0; i < 3; i++) {
        int r = bld_find(s->b, 0, SRC[i]);
        if (r >= 0 && r != room) return r;
    }
    return -1;
}

long site_outlet_price(const Site *s, int room)
{
    int src = power_source_room(s, room);
    int m = src < 0 ? 0 : site_metres(s, room, src);
    if (m < 0) m = 0;
    return OUTLET_FIT + (long)OUTLET_PER_M * m;
}

int site_outlet(Site *s, int room)
{
    s->err = SITE_OK;
    if (!s->b || room < 0 || room >= s->b->nrooms) { s->err = SITE_ENOROOM; return -1; }
    if (s->nsock >= SITE_MAX_SOCKET) { s->err = SITE_ESPACE; return -1; }
    if (site_room_outlets(s, room) >= site_room_outlets_max(s, room)) {
        s->err = SITE_ECIRCUIT; return -1;
    }
    long price = site_outlet_price(s, room);
    if (s->money < price) { s->err = SITE_EMONEY; return -1; }
    s->money -= price;
    s->spent += price;
    SiteSocket *k = &s->sock[s->nsock];
    memset(k, 0, sizeof *k);
    k->room = (uint16_t)room;
    k->day = s->day;
    k->cost = (int)price;
    return s->nsock++;
}

/* ---------------------------------------------------------- installation */
/* A BOX IN A ROOM, whoever put it there. site_install() is the player buying
 * one and refuses anything the shop does not sell; this is the same act with
 * the till taken out of it, for the two devices that are standing in the
 * building before the player has bought anything -- a tenant's desk and the
 * player's own workstation. Splitting it is what lets site_install() refuse a
 * kind that site_new() must still be able to place. */
static int install_dev(Site *s, int kind, int room, const char *name)
{
    s->err = SITE_OK;
    if (kind <= SDEV_UPLINK || kind >= SDEV_KIND_COUNT) { s->err = SITE_ENODEV; return -1; }
    if (!s->b || room < 0 || room >= s->b->nrooms) { s->err = SITE_ENOROOM; return -1; }
    if (s->ndev >= SITE_MAX_DEV) { s->err = SITE_ESPACE; return -1; }
    if (s->money < site_kind_price(kind)) { s->err = SITE_EMONEY; return -1; }

    SiteDev *d = &s->dev[s->ndev];
    memset(d, 0, sizeof *d);
    d->kind = (uint8_t)kind;
    d->room = (uint16_t)room;
    d->floor = s->b->rooms[room].floor;
    d->tenant = s->b->rooms[room].tenant;
    d->nports = site_kind_ports(kind);
    /* A COMPUTER ARRIVES SWITCHED OFF, in a box, on a pallet. An appliance
     * comes up with the socket -- and since D37 there IS a socket, so
     * mains_attach() below is what decides this for an appliance and this
     * line is only the button. */
    d->powered = 0;
    snprintf(d->name, sizeof d->name, "%s", name && *name ? name : site_kind_name(kind));
    /* Two boxes with one name is a diagnosis nobody can perform. */
    if (site_dev_by_name(s, d->name) >= 0)
        snprintf(d->name, sizeof d->name, "%s%d", site_kind_name(kind), s->ndev);
    /* THE PORTS ON THE BACK ARE THE PORTS IT HAS. A server sold with two
     * sockets used to be given four in the network world, so `site` and
     * `netstat` disagreed about the same box, and three of a router's four
     * ports were holes with no card behind them. One number, from the
     * catalogue, used by both. */
    d->node = site_kind_is_switch(kind)
              ? net_add_switch(s->net, d->name, d->nports)
              : net_add_host_nics(s->net, d->name, d->nports);
    if (d->node < 0) { s->err = SITE_ESPACE; return -1; }
    /* AND WHAT THOSE PORTS WILL CLOCK. A link runs at the slowest of the two
     * ports and the cable, and until D27 only the cable had a say -- so a
     * cat 6 lead to a desk came up at ten gigabit because the run was short,
     * and the desk-cable choice was free. The ports are the box you bought.
     * netstack takes the minimum, so this can only ever slow a link down. */
    for (int p = 0; p < d->nports; p++) {
        net_port_rate(s->net, d->node, p, site_kind_port_mb(kind, p));
        /* AND HOW MUCH IT WILL HOLD WHILE IT WAITS FOR THAT WIRE. */
        net_port_set_buffer(s->net, d->node, p,
                            (uint32_t)site_kind_port_buffer(kind));
    }
    /* AND THE BATTERY, WHEN IT COMES WITH ONE. The dear server arrives with
     * a UPS in the bottom of the rack; on anything else `ups <box>` fits one
     * afterwards for 220. Same flag, same behaviour on a mains failure, and
     * `events` prints the same `yes` -- the difference is that one of them
     * is in the price on the catalogue page and the other is a decision you
     * are still able to get wrong on the morning of day twenty-six. */
    if (site_kind_has_ups(kind)) d->ups = 1;
    s->money -= site_kind_price(kind);
    s->spent += site_kind_price(kind);
    int made = s->ndev++;
    /* AND IT GOES IN THE WALL, if there is a hole in it. Putting a box in a
     * room is the act of putting a box in a room, and nobody sets a switch
     * down and then forgets the lead. What this can fail to do is find a
     * socket -- and that failure is the whole feature: d->mains stays 0, the
     * button does nothing, and every surface in the game says why. */
    mains_attach(s, made);
    return made;
}

int site_install(Site *s, int kind, int room, const char *name)
{
    s->err = SITE_OK;
    /* WHAT THE SHOP DOES NOT SELL, THE BUILDING DOES NOT ACCEPT AN ORDER FOR.
     * A tenant's desk is the exception and it is not an exception to the
     * rule: `serve` installs one when a tenancy moves in, which is the
     * tenant carrying their own computer in, and the price they pay for it
     * is not the landlord's business. */
    if (!site_kind_for_sale(kind) && kind != SDEV_DESK) {
        s->err = SITE_ENODEV; return -1;
    }
    return install_dev(s, kind, room, name);
}

/* ------------------------------------------------------------- goods in */
/* HARDWARE ARRIVES SOMEWHERE, and the somewhere is a room in the building.
 *
 * This is the difference between a game about a building and a game with a
 * building drawn behind it. An order that appears in the room you are
 * standing in makes every room equally far from the loading bay, which makes
 * the floor plan scenery -- and the whole argument for the floor plan is
 * that it is the price list. A switch that has to be carried up eight floors
 * costs the walk, and the walk is a number bld_walk_all() already knows how
 * to produce.
 *
 * Note what is NOT modelled: crates, pallets, a weight in kilogrammes, a
 * trolley. This catalogue is five boxes a person can pick up, and inventing
 * a weight limit for them would be a rule with a made-up number in it. The
 * limits that ARE here come from the object: both hands are on the box, so
 * you carry one at a time, and a box with a cable in it does not move. */
int site_goods_room(const Site *s)
{
    if (!s->b) return -1;
    int r = bld_find(s->b, 0, RM_GOODS);
    if (r < 0) r = bld_find(s->b, 0, RM_LOBBY);
    if (r < 0) r = bld_find(s->b, 0, RM_MDF);
    return r;
}

int site_order(Site *s, int kind, const char *name)
{
    s->err = SITE_OK;
    int goods = site_goods_room(s);
    if (goods < 0) { s->err = SITE_ENOROOM; return -1; }
    int d = site_install(s, kind, goods, name);
    /* IT IS STILL IN ITS BOX. A pallet under a roller door is not a rack,
     * and goods in is not where anything runs -- so an order is delivered
     * unplugged whatever is free on that wall, and it starts drawing power
     * in the room somebody carries it to. This is also the honest answer to
     * the thing that started this: a machine that has never been plugged in
     * anywhere is exactly what the player finds when they go and look. */
    if (d >= 0) mains_detach(s, d, false);
    return d;
}

/* IS THERE A CABLE IN IT? Asked of the SITE'S OWN LINK TABLE and not of the
 * netstack, and the difference matters since there is such a thing as an
 * unpowered switch: a port with no power in it reads DOWN rather than
 * NOCABLE, so asking the wire whether a socket is empty answered "no" for
 * every hole in a box that was still in its packaging. The link table is
 * where a lead being in a socket is recorded, so it is what gets asked. */
static bool port_taken(const Site *s, int dev, int port)
{
    for (int i = 0; i < s->nlink; i++) {
        const SiteLink *l = &s->link[i];
        if (l->cable < 0) continue;
        if ((l->a == dev && l->aport == port) || (l->b == dev && l->bport == port))
            return true;
    }
    return site_port_jack(s, dev, port) >= 0;
}

bool site_dev_cabled(const Site *s, int dev)
{
    if (dev < 0 || dev >= s->ndev) return false;
    for (int i = 0; i < s->nlink; i++) {
        const SiteLink *l = &s->link[i];
        if (l->cable >= 0 && (l->a == dev || l->b == dev)) return true;
    }
    return false;
}

bool site_move(Site *s, int dev, int room)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    /* The handoff is the ISP's, in a room they have a key to and you do not.
     * It is the one thing in the building that was not bought and is not
     * moving. */
    if (s->dev[dev].kind == SDEV_UPLINK) { s->err = SITE_EFIXED; return false; }
    if (!s->b || room < 0 || room >= s->b->nrooms) { s->err = SITE_ENOROOM; return false; }
    if (site_dev_cabled(s, dev)) { s->err = SITE_ECABLED; return false; }
    /* AND A BOX WITH A RUN PUNCHED DOWN INTO IT DOES NOT MOVE EITHER, which
     * is the other half of what permanent means. The pair is terminated on
     * this box's socket and the other end of it is screwed to a wall
     * somewhere else in the building; walking off with the box would take
     * the copper with it, and copper in a ceiling does not do that. This is
     * why the price says the port is gone for good. */
    for (int j = 0; j < s->njack; j++)
        if (s->jack[j].home == dev) { s->err = SITE_EJACK; return false; }
    /* THE PLUG COMES OUT WHEN THE BOX MOVES, and it is not a punishment: it
     * is the first thing anybody does before picking a machine up. It comes
     * out cleanly -- somebody was standing there and looking at it -- and
     * whether it goes back in depends entirely on the wall it arrives at. */
    mains_detach(s, dev, false);
    s->dev[dev].room = (uint16_t)room;
    s->dev[dev].floor = s->b->rooms[room].floor;
    mains_attach(s, dev);
    /* WHOSE BOX IT IS WAS DECIDED WHEN IT WAS INSTALLED, and carrying it
     * somewhere does not change it. This used to reassign ownership from
     * whatever room the thing was put down in -- so a playtester bought a
     * switch24, carried it into a let office to serve the desks in it, put
     * it down, and the game confiscated it:
     *
     *   carry sw3b
     *   refused: sw3b belongs to the tenant on floor 3, not to you, and it
     *     stays where it is.
     *
     * Four hundred pounds and the run, gone, for doing the thing the game
     * recommends. Worse, it made D28's own named mistake -- the floor's
     * switch put in the office with the desks -- an irreversible one, since
     * the documented fix is to move the box somewhere cooler.
     *
     * A tenant's desk is a tenant's desk because move_in() INSTALLED it in
     * their room (site_install, above, still reads the room). Nothing the
     * player can carry is theirs to begin with, so nothing the player
     * carries should become theirs by being set down. */
    return true;
}

int site_port_jack(const Site *s, int dev, int port)
{
    for (int j = 0; j < s->njack; j++)
        if (s->jack[j].home == dev && s->jack[j].hport == port) return j;
    return -1;
}

/* A HELD PORT IS NOT A FREE PORT. The far end of a jack is punched down on a
 * panel: there is no hole to put anything else in, whether or not there is a
 * lead in the faceplate at the other end today. `serve` walks this too, so a
 * tenancy's twenty drops cannot quietly eat the riser you paid to have put
 * in last week. */
bool site_port_used(const Site *s, int dev, int port)
{
    if (!s || dev < 0 || dev >= s->ndev) return false;
    if (port < 0 || port >= s->dev[dev].nports) return false;
    return port_taken(s, dev, port);
}

int site_ports_used(const Site *s, int dev)
{
    if (!s || dev < 0 || dev >= s->ndev) return 0;
    int n = 0;
    for (int p = 0; p < s->dev[dev].nports; p++)
        if (port_taken(s, dev, p)) n++;
    return n;
}

/* HOW MANY HOLES A LEAD CAN STILL GO INTO. Not `nports`, and not `nports
 * minus the leads`: a port punched down to a jack is a pair terminated on a
 * panel and there is no hole, and port_taken() knows that. This is the
 * number `show <box>` prints, and it is the number site_free_port() would
 * find one after another. */
int site_ports_spare(const Site *s, int dev)
{
    if (!s || dev < 0 || dev >= s->ndev) return 0;
    int n = 0;
    for (int p = 0; p < s->dev[dev].nports; p++)
        if (!port_taken(s, dev, p)) n++;
    return n;
}

int site_free_port(const Site *s, int dev)
{
    if (dev < 0 || dev >= s->ndev) return -1;
    for (int p = 0; p < s->dev[dev].nports; p++)
        if (!port_taken(s, dev, p)) return p;
    return -1;
}

/* ----------------------------------------------------------------- cable */
/* THE RUN ITSELF, once, for both ways of buying it. `site_cable` measures the
 * tray and charges by the metre; `site_patch` hands it metres that are
 * already in the wall and the price of a lead. Everything after that -- the
 * netstack cable, the row in `links`, the money -- has to be identical, or
 * the two halves of the decision would not be comparable. */
static int cable_run(Site *s, int a, int aport, int b, int bport, CableKind k,
                     int m, int cost, int jack)
{
    if (s->money < cost) { s->err = SITE_EMONEY; return -1; }
    int cid = net_cable(s->net, s->dev[a].node, aport, s->dev[b].node, bport, m, k);
    if (cid < 0) { s->err = SITE_EBUSY; return -1; }

    SiteLink *l = &s->link[s->nlink];
    memset(l, 0, sizeof *l);
    l->a = (int16_t)a; l->b = (int16_t)b;
    l->aport = (int16_t)aport; l->bport = (int16_t)bport;
    l->room_a = (uint16_t)s->dev[a].room; l->room_b = (uint16_t)s->dev[b].room;
    l->metres = m;
    l->kind = (uint8_t)k;
    l->cost = cost;
    l->cable = cid;
    l->jack = (int16_t)jack;
    s->money -= cost;
    s->spent += cost;
    return s->nlink++;
}

/* THE ONE LEAD THAT GIVES WAY. See SiteLink.factory in site.h: the building
 * came with the player's workstation in the handoff's only port, and putting
 * anything else in that port pulls it. Returns the device that just lost its
 * lead, or -1, so the verb above can say so in words -- nothing here is
 * allowed to happen quietly. */
static int yield_factory(Site *s, int dev, int port)
{
    int i = site_port_factory(s, dev, port);
    if (i < 0) return -1;
    int other = s->link[i].a == dev ? s->link[i].b : s->link[i].a;
    site_uncable(s, i);
    return other;
}

int site_cable(Site *s, int a, int aport, int b, int bport, CableKind k)
{
    s->err = SITE_OK;
    s->yielded = -1;
    if (a < 0 || a >= s->ndev || b < 0 || b >= s->ndev) { s->err = SITE_ENODEV; return -1; }
    /* THE PORT YOU HAVE NOT GOT. The first limit a growing floor meets, and
     * it is not a rule about difficulty: an eight-port switch has eight
     * holes in it. */
    if (aport < 0 || aport >= s->dev[a].nports ||
        bport < 0 || bport >= s->dev[b].nports) { s->err = SITE_ENOPORT; return -1; }
    if (s->nlink >= SITE_MAX_LINK) { s->err = SITE_ESPACE; return -1; }
    /* AND THE PORT SOMEBODY ALREADY PUNCHED DOWN. It is not empty, it is
     * terminated, and the thing it is terminated to is on a wall upstairs. */
    if (site_port_jack(s, a, aport) >= 0 ||
        site_port_jack(s, b, bport) >= 0) { s->err = SITE_EJACK; return -1; }

    int m = site_run_metres(s, s->dev[a].room, s->dev[b].room);
    if (m < 0) { s->err = SITE_ENOROUTE; return -1; }
    /* AND THE LEAD THE BUILDING CAME WITH COMES OUT. Only that one, only
     * while it is still the lead the building came with, and the caller is
     * told which box it was. */
    int y = yield_factory(s, a, aport);
    if (y < 0) y = yield_factory(s, b, bport);
    s->yielded = y;
    /* Note what is NOT here: any check that the run is short enough. The
     * cable is bought, laid and paid for, and whether it carries anything is
     * a question for the copper. */
    return cable_run(s, a, aport, b, bport, k, m, site_cable_price(k, m), -1);
}

void site_uncable(Site *s, int link)
{
    if (link < 0 || link >= s->nlink) return;
    if (s->link[link].cable >= 0) net_uncable(s->net, s->link[link].cable);
    s->link[link].cable = -1;
    /* A LEAD COMES OUT OF A JACK; THE JACK STAYS IN THE WALL. That is the
     * whole difference between the two ways of paying for these metres, and
     * it is one line: the faceplate is free for the next box, the panel port
     * is still held, and nothing is refunded either way. */
    int j = s->link[link].jack;
    if (j >= 0 && j < s->njack && s->jack[j].link == link) s->jack[j].link = -1;
}

/* ------------------------------------------------------------- the jack */
int site_jack(Site *s, int room, int home, int hport, CableKind k)
{
    s->err = SITE_OK;
    if (home < 0 || home >= s->ndev) { s->err = SITE_ENODEV; return -1; }
    if (!s->b || room < 0 || room >= s->b->nrooms) { s->err = SITE_ENOROOM; return -1; }
    if (hport < 0 || hport >= s->dev[home].nports) { s->err = SITE_ENOPORT; return -1; }
    if (s->njack >= SITE_MAX_JACK) { s->err = SITE_ESPACE; return -1; }
    if (k < 0 || k >= CAB_KIND_COUNT) { s->err = SITE_ENODEV; return -1; }
    /* The panel end has to be a socket nobody is using and nobody else has
     * punched down, because the trade is going to terminate it. */
    if (site_port_jack(s, home, hport) >= 0) { s->err = SITE_EJACK; return -1; }
    if (port_taken(s, home, hport)) { s->err = SITE_EBUSY; return -1; }
    /* THE SAME METRES THE SPOOL WOULD HAVE COST. bld_cable_all() through
     * site_metres(), so the two prices in front of the player are prices for
     * the same piece of copper up the same riser. */
    int m = site_run_metres(s, room, s->dev[home].room);
    if (m < 0) { s->err = SITE_ENOROUTE; return -1; }
    int cost = site_jack_price(k, m);
    if (s->money < cost) { s->err = SITE_EMONEY; return -1; }

    SiteJack *j = &s->jack[s->njack];
    memset(j, 0, sizeof *j);
    j->room = (uint16_t)room;
    j->home = (int16_t)home;
    j->hport = (int16_t)hport;
    j->metres = m;
    j->kind = (uint8_t)k;
    j->cost = cost;
    j->ordered = s->day;
    j->ready = s->day + site_jack_days(m);
    j->link = -1;
    s->money -= cost;
    s->spent += cost;
    return s->njack++;
}

int site_patch(Site *s, int jack, int dev, int port)
{
    s->err = SITE_OK;
    if (jack < 0 || jack >= s->njack) { s->err = SITE_ENODEV; return -1; }
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return -1; }
    SiteJack *j = &s->jack[jack];
    if (port < 0 || port >= s->dev[dev].nports) { s->err = SITE_ENOPORT; return -1; }
    if (s->nlink >= SITE_MAX_LINK) { s->err = SITE_ESPACE; return -1; }
    /* NOT UNTIL THE TRADE HAS BEEN. */
    /* Both ends of the run in one box is a loop with no spanning tree in it,
     * exactly as it is off the spool. */
    if (dev == j->home) { s->err = SITE_EBUSY; return -1; }
    if (s->day < j->ready) { s->err = SITE_EEARLY; return -1; }
    /* A JACK IS A FIXED POINT IN A ROOM. The box has to be standing in that
     * room, because a lead is two metres long and the faceplate is on that
     * wall. This is the rule that makes a jack pay off when a floor churns
     * and cost when it does not. */
    if (s->dev[dev].room != j->room) { s->err = SITE_ENOROOM; return -1; }
    if (j->link >= 0) { s->err = SITE_EBUSY; return -1; }
    if (net_port_state(s->net, s->dev[dev].node, port) != PORT_NOCABLE) {
        s->err = SITE_EBUSY; return -1;
    }
    if (site_port_jack(s, dev, port) >= 0) { s->err = SITE_EJACK; return -1; }
    int l = cable_run(s, j->home, j->hport, dev, port, (CableKind)j->kind,
                      j->metres, JACK_LEAD, jack);
    if (l < 0) return -1;
    j->link = l;
    j->leads++;
    j->lead_spend += JACK_LEAD;
    return l;
}

int site_room_jack(const Site *s, int room, int nth, bool free_only)
{
    int seen = 0;
    for (int i = 0; i < s->njack; i++) {
        if (s->jack[i].room != room) continue;
        if (free_only && s->jack[i].link >= 0) continue;
        if (seen++ == nth) return i;
    }
    return -1;
}

PortState site_link_state(const Site *s, int link)
{
    if (link < 0 || link >= s->nlink) return PORT_NOCABLE;
    const SiteLink *l = &s->link[link];
    if (l->cable < 0) return PORT_NOCABLE;
    return net_port_state(s->net, s->dev[l->a].node, l->aport);
}

/* ----------------------------------------------------------------- power */
/* WHAT IS IN A MACHINE'S MEMORY GOES WHEN THE POWER DOES. Addresses, routes,
 * the ARP cache, the filter, every open socket: none of them are on the box,
 * and a box that has been switched off answers nothing at all. What comes
 * back when it is switched on again comes off its disk, which is why the
 * disk is the only place a configuration lives. */
static void power_down(Site *s, int dev)
{
    int node = s->dev[dev].node;
    net_services_stop(s->net, node);
    net_dhcpd_stop(s->net, node);
    net_route_clear(s->net, node);
    net_arp_flush(s->net, node);
    net_fw_clear(s->net, node);
    net_set_resolver(s->net, node, 0);
    for (int i = 0; i < NET_IF_MAX; i++) {
        if (!net_if_exists(s->net, node, i)) continue;
        net_if_addr(s->net, node, i, 0, 0);
        net_if_up(s->net, node, i, false);
    }
    net_forwarding(s->net, node, false);
}

bool site_power(Site *s, int dev, bool on)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    SiteDev *d = &s->dev[dev];
    if (!site_kind_has_os(d->kind)) { s->err = SITE_ENOBTN; return false; }
    /* AND THE BUTTON DOES NOTHING IF THERE IS NOTHING BEHIND IT. Until D37
     * every box in this game drew power from nowhere, so a server standing
     * in a cupboard with no lead in the back of it booted when you pressed
     * the button -- which is the one thing a serial console into a dead
     * machine is supposed to be able to tell you is false. */
    /* NOT THE ROOM'S PROBLEM. This used to set SITE_ENOMAINS, whose sentence
     * is about a room with no socket left in it -- so pressing the button on
     * a box that simply has no lead in the back of it blamed a wall that was
     * half empty and named neither the fault nor `mains`. */
    if (on && !d->mains) { s->err = SITE_EUNPLUGGED; return false; }
    if (!!d->powered == on) return true;
    d->powered = on ? 1 : 0;
    if (!on) { power_down(s, dev); return true; }
    for (int i = 0; i < NET_IF_MAX; i++)
        if (net_if_exists(s->net, d->node, i)) net_if_up(s->net, d->node, i, true);
    return true;
}

/* --------------------------------------------------------- configuration */
static bool host_dev(const Site *s, int dev)
{
    return dev >= 0 && dev < s->ndev && !site_kind_is_switch(s->dev[dev].kind);
}
/* Configuring a box that is not running is configuring nothing: there is no
 * kernel in it to hold what you typed. */
static bool live_dev(Site *s, int dev)
{
    if (!host_dev(s, dev)) { s->err = SITE_EIFACE; return false; }
    if (site_kind_has_os(s->dev[dev].kind) && !s->dev[dev].powered) {
        s->err = SITE_EOFF; return false;
    }
    return true;
}

/* THE TWO ADDRESSES IN A SUBNET THAT ARE NOT A MACHINE'S. A /30 has four
 * addresses and two of them are usable; giving a box the fourth one and
 * letting it answer taught a player that the arithmetic they had done in
 * their head was wrong when it was right. Refused, and the refusal says
 * which two are left. */
static bool usable_host_addr(uint32_t ip, uint32_t mask)
{
    int bits = net_mask_len(mask);
    if (bits >= 31 || bits <= 0) return true;      /* a /31 is all there is */
    if ((ip & ~mask) == 0) return false;                        /* network */
    if ((ip | mask) == 0xffffffffu) return false;             /* broadcast */
    return true;
}

bool site_addr(Site *s, int dev, int ifx, uint32_t ip, uint32_t mask)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    if (ifx < 0 || ifx >= NET_IF_MAX) { s->err = SITE_EIFACE; return false; }
    /* A card it has not got, or a subinterface nobody created. `subif` is
     * what makes one; this is not. */
    if (ifx >= s->dev[dev].nports && !net_if_exists(s->net, s->dev[dev].node, ifx)) {
        s->err = SITE_EIFACE; return false;
    }
    if (ip && !usable_host_addr(ip, mask)) { s->err = SITE_EADDR; return false; }
    net_if_addr(s->net, s->dev[dev].node, ifx, ip, mask);
    return true;
}
bool site_gateway(Site *s, int dev, uint32_t gw)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    net_set_gateway(s->net, s->dev[dev].node, gw);
    return true;
}
bool site_forwarding(Site *s, int dev, bool on)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    net_forwarding(s->net, s->dev[dev].node, on);
    return true;
}
/* ONE SOCKET, A SUBNET PER VLAN. The interface index is not the player's to
 * choose: they name the card and the tag, and the box either finds the
 * subinterface it already has for that pair or makes one. An address of zero
 * takes it away again -- which is how a router leaves a vlan. */
bool site_subif(Site *s, int dev, int nic, int vlan, uint32_t ip, uint32_t mask)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    if (nic < 0 || nic >= s->dev[dev].nports) { s->err = SITE_ENOPORT; return false; }
    if (vlan < 1 || vlan > 4094) { s->err = SITE_EVLAN; return false; }
    if (ip && !usable_host_addr(ip, mask)) { s->err = SITE_EADDR; return false; }
    int node = s->dev[dev].node;
    int ifx = net_if_subif(s->net, node, nic, vlan);
    if (ifx < 0) { s->err = SITE_EIFACE; return false; }
    if (!ip) { net_if_del(s->net, node, ifx); return true; }
    net_if_addr(s->net, node, ifx, ip, mask);
    return true;
}
/* A VLAN IS A PROPERTY OF A SWITCH PORT. It used to be settable on a router
 * and did nothing at all: the frame's tag is what a host looks at, and a
 * host looks at its interface's tag and never at its port's. Answering "set"
 * to a line that changes nothing is worse than refusing it, because the
 * player then trusts it and goes looking somewhere else. */
bool site_port_vlan(Site *s, int dev, int port, int vlan)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    if (!site_kind_is_switch(s->dev[dev].kind)) { s->err = SITE_ENOTSW; return false; }
    if (port < 0 || port >= s->dev[dev].nports) { s->err = SITE_ENOPORT; return false; }
    /* ONE RULE, ONE ANSWER. `trunk s8 0 4095` refused the whole line with "a
     * vlan is a number from 1 to 4094"; `vlan s8 0 99999` answered "set" --
     * about a port netstack had quietly clamped or dropped -- so the game
     * held two opinions about what a vlan is, one line apart, and the
     * permissive one was the one that touched the switch. site_port_trunk()
     * has always checked this; the access half never did. */
    if (vlan < 1 || vlan > VLAN_ID_MAX) { s->err = SITE_EVLAN; return false; }
    net_port_mode(s->net, s->dev[dev].node, port, PORT_ACCESS);
    net_port_vlan(s->net, s->dev[dev].node, port, vlan);
    return true;
}
/* A TRUNK PORT, AND WHAT IT MAY CARRY. `vlan` of 0 makes the port a trunk
 * and adds nothing, which is the state a real trunk starts in.
 *
 * A vlan outside 1..4094 is REFUSED. It used to be accepted and dropped by
 * netstack, which had room for 32 vlans in a word while `subif` has always
 * taken 4094 -- so `trunk core 22 100` answered "set" about a trunk that
 * carried no vlan 100 and never would. */
static bool trunk_port_ok(Site *s, int dev, int port)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    if (!site_kind_is_switch(s->dev[dev].kind)) { s->err = SITE_ENOTSW; return false; }
    if (port < 0 || port >= s->dev[dev].nports) { s->err = SITE_ENOPORT; return false; }
    return true;
}
bool site_port_trunk(Site *s, int dev, int port, int vlan)
{
    if (!trunk_port_ok(s, dev, port)) return false;
    if (vlan < 0 || vlan > VLAN_ID_MAX) { s->err = SITE_EVLAN; return false; }
    net_port_mode(s->net, s->dev[dev].node, port, PORT_TRUNK);
    if (vlan > 0 && !net_trunk_allow(s->net, s->dev[dev].node, port, vlan)) {
        s->err = SITE_EVLAN; return false;
    }
    return true;
}
/* THE WAY BACK OFF. `vlan` of 0 empties the allowed set; the port stays a
 * trunk, because taking every vlan off a trunk is not the same operation as
 * turning it back into an access port and a player who meant the second one
 * has `vlan <sw> <port> <n>` for it. */
bool site_port_trunk_off(Site *s, int dev, int port, int vlan)
{
    if (!trunk_port_ok(s, dev, port)) return false;
    if (vlan < 0 || vlan > VLAN_ID_MAX) { s->err = SITE_EVLAN; return false; }
    if (vlan == 0) { net_trunk_clear(s->net, s->dev[dev].node, port); return true; }
    if (!net_trunk_deny(s->net, s->dev[dev].node, port, vlan)) {
        s->err = SITE_EVLAN; return false;
    }
    return true;
}
/* What it carries now, for anything that wants to print it back. */
int site_port_trunk_list(Site *s, int dev, int port, int *out, int cap)
{
    if (!trunk_port_ok(s, dev, port)) return 0;
    return net_trunk_allowed(s->net, s->dev[dev].node, port, out, cap);
}
/* WHICH LEG OF THE BOX A POOL LANDS ON, and the refusal when there is none.
 *
 * The `dhcpd` line has never had a vlan in it and it must not grow one: a
 * server's segment is a fact about its own interfaces, not a seventh number
 * to get wrong. So the pool is scoped to the interface whose address is in
 * the pool's subnet -- which lets a router with three subinterfaces run
 * three pools by being told about them three times, and refuses outright the
 * thing a playtester did by accident: a pool for one tenancy's subnet
 * started on a router, answering every OTHER tenancy's broadcast with
 * addresses from a subnet their segment has never heard of. */
bool site_dhcpd(Site *s, int dev, uint32_t first, int count, uint32_t mask,
                uint32_t gw, uint32_t dns)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    if (count <= 0) { s->err = SITE_EPOOL; return false; }
    if (net_dhcpd_scope(s->net, s->dev[dev].node, first, mask) < 0) {
        s->err = SITE_ESEG; return false;
    }
    /* TWO THINGS THAT CAN GO WRONG, TWO ERRORS. This was one code carrying
     * one sentence about both, and the sentence led with the half that was
     * false: a pool of 180 addresses, refused for being the ninth on the
     * box, was told "a pool of no addresses serves nobody, and a box holds
     * eight pools at most". The true reason was buried behind a reason the
     * player could check and disprove in the same line they had typed. */
    if (!net_dhcpd(s->net, s->dev[dev].node, first, count, mask, gw, dns)) {
        s->err = net_dhcpd_pools(s->net, s->dev[dev].node) >= NET_POOL_MAX
                 ? SITE_EPOOLS : SITE_EPOOL;
        return false;
    }
    return true;
}
/* AND THERE IS A WAY OUT. `dhcpd <box> off` was not a line anybody could
 * type: a pool started by mistake could only be re-pointed, never stopped,
 * and an appliance has no power button to pull it down with either. */
int site_dhcpd_stop(Site *s, int dev)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return -1;
    return net_dhcpd_stop(s->net, s->dev[dev].node);
}
void site_dump_dhcpd(const Site *s, int dev, Buf *out)
{
    if (dev < 0 || dev >= s->ndev) return;
    int node = s->dev[dev].node;
    int np = net_dhcpd_pools(s->net, node);
    if (!np) {
        buf_printf(out, "%s serves no addresses.\n", s->dev[dev].name);
        return;
    }
    for (int i = 0; i < np; i++) {
        int ifx = 0, count = 0;
        uint32_t first = 0, mask = 0, gw = 0, dns = 0;
        if (!net_dhcpd_pool(s->net, node, i, &ifx, &first, &count, &mask, &gw, &dns))
            break;
        char a[20], b[20], g[20], d[20], nm[24];
        net_fmt_ip(first, a, sizeof a);
        net_fmt_ip(first + (uint32_t)(count - 1), b, sizeof b);
        net_fmt_ip(gw, g, sizeof g);
        net_fmt_ip(dns, d, sizeof d);
        net_if_name(s->net, node, ifx, nm, sizeof nm);
        int vlan = net_if_get_vlan(s->net, node, ifx);
        buf_printf(out, "%s dhcpd: %s-%s /%d on %s", s->dev[dev].name, a, b,
                   net_mask_len(mask), nm);
        if (vlan) buf_printf(out, " (vlan %d)", vlan);
        int held = net_dhcpd_pool_leases(s->net, node, i);
        buf_printf(out, ", gw %s, dns %s, %d lease%s out\n", g, d, held,
                   held == 1 ? "" : "s");
    }
    /* The box's total under the per-pool lines, and only when there is more
     * than one pool for it to be the sum of. */
    if (np > 1)
        buf_printf(out, "  %d lease%s out in all. Each pool answers on its own "
                        "interface and on no other.\n",
                   net_dhcpd_leases(s->net, node),
                   net_dhcpd_leases(s->net, node) == 1 ? "" : "s");
    else
        buf_puts(out, "  It answers on that interface and on no other.\n");
}
bool site_dhcp(Site *s, int dev)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    return net_dhcp_client(s->net, s->dev[dev].node, 0);
}
bool site_resolver(Site *s, int dev, uint32_t ns)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    net_set_resolver(s->net, s->dev[dev].node, ns);
    return true;
}
bool site_dnsd(Site *s, int dev)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    net_dnsd(s->net, s->dev[dev].node);
    return true;
}
bool site_dns(Site *s, int dev, const char *name, uint32_t ip)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    if (!net_dns_record(s->net, s->dev[dev].node, name, ip)) {
        s->err = SITE_EZONE;
        return false;
    }
    return true;
}
/* WHAT A NAME SERVER WILL ACTUALLY ANSWER. `dnsd <box>` used to print the
 * word `serving`, which was true and useless: a server with an empty zone
 * and no forwarder is also `serving`, and it NXDOMAINs the world. Say the
 * two numbers that decide whether it is worth pointing a floor at. */
void site_dump_dnsd(const Site *s, int dev, Buf *out)
{
    if (dev < 0 || dev >= s->ndev) return;
    int node = s->dev[dev].node;
    const char *me = s->dev[dev].name;
    if (!net_dnsd_running(s->net, node)) {
        buf_printf(out, "%s is not a name server. `dnsd %s` starts one.\n", me, me);
        return;
    }
    int nr = net_dns_record_count(s->net, node);
    uint32_t up = net_dns_forwarder(s->net, node);
    uint32_t res = net_get_resolver(s->net, node);
    char f[20];
    net_fmt_ip(up, f, sizeof f);
    buf_printf(out, "%s serves %d name%s and forwards the rest %s%s.\n",
               me, nr, nr == 1 ? "" : "s",
               up ? "to " : "nowhere", up ? f : "");
    for (int i = 0; ; i++) {
        char nm[64], a[20];
        uint32_t ip = 0;
        if (!net_dns_record_at(s->net, node, i, nm, sizeof nm, &ip)) break;
        net_fmt_ip(ip, a, sizeof a);
        buf_printf(out, "  %-32s %s\n", nm, a);
    }
    if (!nr)
        buf_printf(out, "  It holds no names of its own. `dns %s <name> <ip>` "
                        "gives it one.\n", me);
    if (!up) {
        if (!res)
            buf_printf(out, "  AND IT HAS NOWHERE TO ASK: anything not in that "
                            "list is answered\n  `no such host`. `resolver %s "
                            "<ip>` is the address it forwards to.\n", me);
        else
            buf_printf(out, "  Its own resolver is itself, which is not "
                            "somewhere to forward to:\n  anything not in that "
                            "list is answered `no such host`. `resolver %s "
                            "<ip>`\n  with the address of a resolver that is "
                            "not this box.\n", me);
    }
}
bool site_httpd(Site *s, int dev, int port)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    net_httpd(s->net, s->dev[dev].node, (uint16_t)(port ? port : 80));
    return true;
}

/* --------------------------------------------------------------- lookups */
int site_dev_by_name(const Site *s, const char *name)
{
    for (int i = 0; i < s->ndev; i++)
        if (strcmp(s->dev[i].name, name) == 0) return i;
    return -1;
}

/* "#41" is room 41. "f3.comms" is the comms cupboard on floor three, and the
 * same spelling works for mdf, riser, goods, lobby, plant and the lettable
 * kinds -- which is enough to build a whole tower without ever printing a
 * floor plan, though --floorplan is there when a person wants one. */
int site_room_by_name(const Site *s, const char *spec)
{
    if (!s->b || !spec || !*spec) return -1;
    /* THE GAME PRINTS "MDF" AND WOULD NOT ACCEPT IT. The prompt, `look` and
     * `rooms` all spell the room in capitals, and `go MDF` answered "there is
     * no room or box called MDF" -- a playtester put two boxes down in goods
     * in before working out that the spelling the game shows is not the
     * spelling it takes. A room name is a name, not a password. */
    char low[64];
    size_t li = 0;
    for (const char *q = spec; *q && li < sizeof low - 1; q++)
        low[li++] = (*q >= 'A' && *q <= 'Z') ? (char)(*q - 'A' + 'a') : *q;
    low[li] = 0;
    spec = low;
    if (spec[0] == '#') {
        int n = atoi(spec + 1);
        return (n >= 0 && n < s->b->nrooms) ? n : -1;
    }
    if (spec[0] != 'f') return -1;
    const char *dot = strchr(spec, '.');
    if (!dot) return -1;
    int floor = atoi(spec + 1);
    static const struct { const char *n; int k; } K[] = {
        { "comms", RM_COMMS }, { "mdf", RM_MDF }, { "riser", RM_RISER },
        { "goods", RM_GOODS }, { "lobby", RM_LOBBY }, { "plant", RM_PLANT },
        { "server", RM_SERVER }, { "office", RM_OFFICE },
        { "residence", RM_RESIDENCE }, { "retail", RM_RETAIL },
        /* THE WAY UP TO A FLOOR NOBODY HAS OPENED. Its lift button is not
         * lit, so `go f4.stair` is the only spelling that gets a person onto
         * floor four -- and it answered "no such room" because this table
         * knew about offices and not about stairwells. One table, and it is
         * the same list core/session.c walks by. */
        { "stair", RM_STAIR }, { "stairwell", RM_STAIR },
        { "liftlobby", RM_LIFTLOBBY }, { "toilet", RM_TOILET },
        { "corridor", RM_CORRIDOR }, { NULL, 0 }
    };
    for (int i = 0; K[i].n; i++)
        if (strcmp(dot + 1, K[i].n) == 0) return bld_find(s->b, floor, K[i].k);
    return -1;
}

/* HOW MANY ROOMS THAT SHORTHAND REALLY MATCHES.
 *
 * `f2.office` is one word and floor two has twelve offices belonging to
 * three tenants. bld_find() returns the lowest-numbered one and says
 * nothing, so `move pc1 f2.office` put a box in tenant 2's room -- a room
 * somebody else is paying for -- with no sign anywhere that a choice had
 * been made. The shorthand is genuinely useful when it is unambiguous
 * (`f1.comms`, `f0.mdf`) and is not being taken away; what it owes the
 * player is to say when it was not.
 *
 * `#41` and an unambiguous kind both answer 1. A kind with no room on that
 * floor answers 0. Everything else is a choice the game made for you, and
 * the caller prints which one and what the alternatives are. */
int site_room_name_matches(const Site *s, const char *spec, int *first)
{
    if (first) *first = -1;
    int r = site_room_by_name(s, spec);
    if (first) *first = r;
    if (r < 0 || !spec) return 0;
    if (spec[0] == '#') return 1;    /* a number names exactly one room */
    int kind = s->b->rooms[r].kind, floor = s->b->rooms[r].floor, n = 0;
    for (int i = 0; i < s->b->nrooms; i++)
        if (s->b->rooms[i].floor == floor && s->b->rooms[i].kind == kind) n++;
    return n;
}

/* The nth room on that floor of that kind, for listing the candidates. */
static int room_nth_like(const Site *s, int like, int nth)
{
    int kind = s->b->rooms[like].kind, floor = s->b->rooms[like].floor, n = 0;
    for (int i = 0; i < s->b->nrooms; i++)
        if (s->b->rooms[i].floor == floor && s->b->rooms[i].kind == kind) {
            if (n == nth) return i;
            n++;
        }
    return -1;
}

/* WHICH ONE IT PICKED, AND WHY, printed above whatever the verb went on to
 * do. Named here so `move` and anything else that takes the shorthand say
 * the same thing about the same choice. */
void site_room_ambiguity(const Site *s, const char *spec, int picked, Buf *out)
{
    int n = site_room_name_matches(s, spec, NULL);
    if (n < 2 || picked < 0) return;
    buf_printf(out, "  NOTE: `%s` matches %d rooms on floor %d and the game "
                    "picked one: #%d,\n  the lowest-numbered", spec, n,
               s->b->rooms[picked].floor, picked);
    if (s->b->rooms[picked].tenant)
        buf_printf(out, ", which tenant %d is leasing",
                   s->b->rooms[picked].tenant);
    buf_puts(out, ". The others are");
    int shown = 0;
    for (int i = 0; i < n && shown < 11; i++) {
        int r = room_nth_like(s, picked, i);
        if (r < 0 || r == picked) continue;
        buf_printf(out, "%s #%d", shown ? "," : "", r);
        if (s->b->rooms[r].tenant) buf_printf(out, " (tenant %d)",
                                              s->b->rooms[r].tenant);
        shown++;
    }
    if (n - 1 > shown) buf_printf(out, " and %d more", n - 1 - shown);
    buf_printf(out, ".\n  `rooms %d` lists the floor; `#<n>` names one for "
                    "certain.\n", s->b->rooms[picked].floor);
}

/* ----------------------------------------------------------- measurement */
uint64_t site_dev_frames(const Site *s, int dev)
{
    if (dev < 0 || dev >= s->ndev) return 0;
    uint64_t n = 0;
    for (int p = 0; p < s->dev[dev].nports; p++)
        n += net_port_rx(s->net, s->dev[dev].node, p);
    return n;
}

uint64_t site_host_frames(const Site *s)
{
    uint64_t n = 0;
    for (int i = 0; i < s->ndev; i++) {
        if (site_kind_is_switch(s->dev[i].kind)) continue;
        n += site_dev_frames(s, i);
    }
    return n;
}

/* ---------------------------------------------------------------- demand */
int site_demand_upto(const Site *s, int day, int *out, int cap)
{
    int n = 0;
    for (int i = 0; i < s->ntenant && n < cap; i++)
        if (s->tenant[i].day <= day) out[n++] = i;
    return n;
}

void site_dump_demand(const Site *s, Buf *out)
{
    int drops = 0, seg = 0, srv = 0, rent = 0;
    int bykind[TEN_KIND_COUNT];
    memset(bykind, 0, sizeof bykind);
    buf_printf(out, "%d tenancies want service in this tower\n\n", s->ntenant);
    /* THE PRICE HAS TO BE LEGIBLE BEFORE THE LEASE IS SIGNED. A studio pays
     * three times an office for the same floor and will fill an uplink
     * nobody sized for it; that is only a decision if the player can see
     * both halves of it here, on the row, before the day arrives. */
    buf_puts(out, "  day  floor  tenant  trade      drops  wants"
                  "                                          rent/mo\n");
    for (int i = 0; i < s->ntenant; i++) {
        const SiteTenant *t = &s->tenant[i];
        char want[72];
        snprintf(want, sizeof want, "%s%s%s", site_tenant_kind_wants(t->kind),
                 t->own_segment ? " +segment" : "",
                 t->wants_server ? " +server" : "");
        buf_printf(out, "  %3d  %5d  %6d  %-9s  %5d  %-46s %6d\n",
                   t->day, t->floor, t->tenant, site_tenant_kind_name(t->kind),
                   t->drops, want, t->rent);
        drops += t->drops;
        seg += t->own_segment;
        srv += t->wants_server;
        rent += t->rent;
        if (t->kind < TEN_KIND_COUNT) bykind[t->kind]++;
    }
    buf_printf(out, "\n%d drops in all, %d of them wanting a segment of their "
                    "own, %d wanting a server\n", drops, seg, srv);
    buf_puts(out, "by trade: ");
    for (int k = 0; k < TEN_KIND_COUNT; k++)
        buf_printf(out, "%s%d %s", k ? ", " : "", bykind[k],
                   site_tenant_kind_name(k));
    buf_puts(out, "\n");
    /* HOW MANY SWITCHES THAT REALLY IS, which is not drops divided by the
     * number in the product name.
     *
     * This said 23 desks to a switch24 -- every port but one, kept for the
     * riser. But since D27 a switch24 is twenty-two gigabit access ports and
     * an SFP+ PAIR on 22 and 23, and those two are where the riser and the
     * floor's own server go, because they are the only ten gigabit holes in
     * the building. So a switch24 seats twenty-two desks, not twenty-three,
     * and a player who trusted this footer bought two switches for a floor
     * that needed three and found out when `serve` stopped halfway.
     *
     * A playtester put it plainly: "switch24 sounds like 24 desks. It is
     * 20-21 once you have used 23 for the riser and 22 for the server." */
    buf_printf(out, "which is %d twenty-four port switches, or %d eight port "
                    "ones, and %d a month of rent to pay for them\n",
               (drops + 21) / 22, (drops + 6) / 7, rent);
    buf_printf(out, "  a switch24 seats %d desks, not 24: ports 22 and 23 are "
                    "its SFP+ pair,\n  which is where the riser and the floor's "
                    "server want to be. A switch8\n  seats %d, keeping one for "
                    "the run back. `serve` fills from port 0 up,\n  so it will "
                    "spend the pair on desks if you let it.\n",
               site_kind_ports(SDEV_SWITCH24) - 2,
               site_kind_ports(SDEV_SWITCH8) - 1);
    /* AND WHAT EACH TRADE WILL DO TO THE BUILDING, because the drops column
     * is no longer the size of the bill. Every number here is the constant
     * that enforces it, printed rather than spelled, so there is one place
     * for the fact and no second place for it to drift from. */
    buf_printf(out,
        "\n  WHAT EACH TRADE ASKS THE NETWORK FOR, and they are not the same\n"
        "  question. Rent is a percentage of what an office pays for the same\n"
        "  square metres, and it is on the row above before you sign.\n"
        "\n  office    (%3d%%) %d KB of page and %d x %d KB of files per desk, all\n"
        "                   at once. Throughput at nine in the morning, and it\n"
        "                   does not mind waiting. Served on four fifths done.\n"
        "  voice     (%3d%%) %d bytes every %d ms per desk, EACH WAY, out to the\n"
        "                   carrier -- a fiftieth of one office desk. No amount\n"
        "                   of bandwidth will help them. A call breaks up past\n"
        "                   %d%% of its audio concealed (lost, or so late the\n"
        "                   buffer had already played silence) or %d ms of\n"
        "                   one-way delay, and `service` says which it was.\n"
        "  web host  (%3d%%) %d visitors a day arriving FROM THE INTERNET at %d KB\n"
        "                   each, into their origin server. That crosses the\n"
        "                   circuit, the router and the riser INWARDS, which\n"
        "                   nothing else in this tower does. They need %d of\n"
        "                   every %d served -- and a day their origin answered\n"
        "                   nothing costs you a day's rent BACK, not just the\n"
        "                   day's rent. THEIR SITE IS ON THE BOX IN THEIR OWN\n"
        "                   ROOM: a server standing anywhere else is the\n"
        "                   landlord's, and a host on the landlord's box is\n"
        "                   hosted on somebody else's uptime.\n"
        "  studio    (%3d%%) %d KB UP per suite, every day, inside the busy\n"
        "                   period. Not a byte less: a stream that arrives late\n"
        "                   is a dropped stream and there is no partial credit.\n"
        "                   Upload is the one direction a riser sized for desks\n"
        "                   was never sized for, and `isp <mb>` is the other\n"
        "                   half of the answer.\n",
        site_tenant_rent_pct(TEN_OFFICE), SITE_DESK_WEB_KB, SITE_DESK_FILES,
        SITE_DESK_FILE_KB,
        site_tenant_rent_pct(TEN_VOICE), NET_VOICE_PAYLOAD, NET_VOICE_PTIME,
        SITE_VOICE_CONCEAL_PPM / 10000, SITE_VOICE_DELAY_MS,
        site_tenant_rent_pct(TEN_WEBHOST), SITE_WEB_HITS, SITE_WEB_HIT_KB,
        SITE_WEB_UP_NUM, SITE_WEB_UP_DEN,
        site_tenant_rent_pct(TEN_STUDIO), SITE_STREAM_KB);
    /* AND WHAT DISCHARGES `+server`, because the column said the word and
     * nothing anywhere said what satisfies it. A playtester put ONE server
     * in a floor's comms cupboard on three vlan subinterfaces, and all three
     * of that floor's tenancies pulled their files off it and all three paid
     * -- which is right, and which they found out only after spending 1,350
     * on the assumption that it would not be. The web host's rule is the
     * opposite one and `demand` already prints it, so the office case read
     * as an unstated exception to a rule that was actually the exception.
     *
     * This is the same preference order file_server_for() really applies in
     * core/siteday.c, and the same sentence `service`'s files column already
     * explains after the fact. Said before the lease is signed, where the
     * decision about what to buy is actually made. */
    buf_puts(out,
        "\n  WHAT DISCHARGES `+server`, and it is not one box per tenancy.\n"
        "  Their people pull files off the nearest server that is POWERED,\n"
        "  ADDRESSED and SERVING -- `httpd <box>`, which is the one of the\n"
        "  three that leaves every other indicator green: a box that is on\n"
        "  and on the network and running nothing answers nothing, and the\n"
        "  files column says `<box> (no httpd)` when that is what happened.\n"
        "  Nearest is: their own machine if they have one, otherwise one on\n"
        "  their floor, otherwise anything at all in the building, however\n"
        "  many floors of riser that is through. So one server in a floor's\n"
        "  comms cupboard, with a leg on each tenancy's vlan, serves that\n"
        "  whole floor and every tenancy on it counts as having one --\n"
        "  `service`'s files column names the box each of them really used,\n"
        "  and marks with <- the ones being served from another floor.\n"
        "  A WEB HOST IS THE EXCEPTION, for the reason above: their site is\n"
        "  their software, so their origin must stand in their own room and\n"
        "  no server of yours will answer for it.\n");
}

/* ------------------------------------------------------------ inspection */
static const char *pstate(PortState p)
{
    switch (p) {
    case PORT_UP:         return "up";
    case PORT_NOCABLE:    return "no cable";
    case PORT_TOOLONG:    return "TOO LONG";
    case PORT_DOWN_ADMIN: return "admin down";
    }
    return "?";
}

static void where(const Site *s, const SiteDev *d, char *out, size_t cap)
{
    if (d->room == BLD_NOROOM || !s->b || d->room >= s->b->nrooms) {
        snprintf(out, cap, "outside");
        return;
    }
    const Room *r = &s->b->rooms[d->room];
    snprintf(out, cap, "f%d %s #%d", r->floor, bld_kind_name(r->kind), d->room);
}

void site_dump(const Site *s, Buf *out)
{
    char w[48], ip[20];
    buf_printf(out, "site in building %llu: %d devices, %d cables, "
                    "%ld spent, %ld left\n",
               (unsigned long long)s->b->seed, s->ndev, s->nlink,
               s->spent, s->money);
    if (s->ndev == 1)
        buf_puts(out, "nothing is installed. There is an ISP handoff in the "
                      "MDF and nothing is plugged into it.\n");
    buf_puts(out, "\n  name         kind      where                 what\n");
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        /* THREE HUNDRED AND FIFTY DESKS ARE NOT A DEVICE LIST. They belong
         * to the tenants, they arrive by the score, and a player wants one
         * line per tenancy about them. `service` is that line. */
        if (d->kind == SDEV_DESK) continue;
        where(s, d, w, sizeof w);
        buf_printf(out, "  %-12s %-9s %-21s", d->name, site_kind_name(d->kind), w);
        if (site_kind_is_switch(d->kind)) {
            /* THE SAME COUNT `look`, `show <box>` AND `serve` USE, which is
             * leads in holes off the site's own link table. This counted
             * ports whose NETSTACK state was not NOCABLE, and an unpowered
             * switch has every port administratively down rather than
             * unoccupied -- so a switch24 that had just been delivered
             * printed `24/24 ports used` on the summary page while `show
             * core` under it printed twenty-four ports with nothing in
             * them, and the number never moved as the player cabled. The
             * identical bug was found and fixed in `look` (session.c's
             * dev_line, see site_port_used) and this copy was missed,
             * because there were two copies. Now there is one. */
            buf_printf(out, " %d/%d ports used", site_ports_used(s, i),
                       d->nports);
        } else if (site_kind_has_os(d->kind) && !d->powered) {
            buf_puts(out, " switched off");
        } else {
            uint32_t a = net_if_get_addr(s->net, d->node, 0);
            if (a) {
                net_fmt_ip(a, ip, sizeof ip);
                buf_printf(out, " %s/%d", ip,
                           net_mask_len(net_if_get_mask(s->net, d->node, 0)));
            } else buf_puts(out, " no address");
        }
        buf_putc(out, '\n');
    }
    if (s->nlink || s->njack) { buf_putc(out, '\n'); site_dump_links(s, out); }
}

/* WHAT THE TABLE IS FOR: finding a run to pull. A pulled run is not a run,
 * and it was still in the list -- rows an `uncable` could not act on, padding
 * the only list a player scans to find one that it can. They are off the
 * table now.
 *
 * THE INDICES DO NOT MOVE. A pulled row keeps its slot for the life of the
 * site; nothing is compacted and nothing is reused, so `uncable 4` means the
 * same run on day sixty as it did on day one. Renumbering the survivors would
 * have been the worse bug: a stale number that pulls the wrong live cable.
 * The numbers climb, and gaps in them are the visible record of what went.
 *
 * AND WHAT IT COST. Copper is not refunded when it comes out -- a cheap run
 * you have to redo is meant to hurt -- so the live runs are not the spend.
 * Both numbers are here, each saying which it is. */
/* WHAT IS IN THE WALL, as against what is lying in the tray. A jack is the
 * one thing a player buys in this game that survives everything else: the box
 * goes, the lead comes out, the room is re-let, and the copper is still
 * there. So it is listed separately from the runs, with the day the trade
 * comes for the ones that are still a booking. */
void site_dump_jacks(const Site *s, int room, Buf *out)
{
    int n = 0;
    for (int i = 0; i < s->njack; i++) {
        const SiteJack *j = &s->jack[i];
        if (room != BLD_NOROOM && j->room != room) continue;
        if (!n++) buf_puts(out, "  jacks in the wall\n");
        char w[48], h[40];
        if (s->b && j->room < s->b->nrooms) {
            const Room *r = &s->b->rooms[j->room];
            snprintf(w, sizeof w, "f%d %s #%d", r->floor, bld_kind_name(r->kind),
                     j->room);
        } else snprintf(w, sizeof w, "?");
        snprintf(h, sizeof h, "%s:%d", s->dev[j->home].name, j->hport);
        buf_printf(out, "  j%-2d %-21s to %-12s %4d m  %-6s %4d  ", i, w, h,
                   j->metres, site_cable_name((CableKind)j->kind), j->cost);
        if (s->day < j->ready)
            buf_printf(out, "the trade comes on day %d, in %d day%s\n", j->ready,
                       j->ready - s->day, j->ready - s->day == 1 ? "" : "s");
        else if (j->link >= 0)
            buf_printf(out, "%s:%d is in it\n", s->dev[s->link[j->link].b].name,
                       s->link[j->link].bport);
        else
            buf_printf(out, "empty -- `patch <box>:<port> j%d`\n", i);
    }
    if (!n && room == BLD_NOROOM)
        buf_puts(out, "  no jacks. Every metre of copper here is off the spool "
                      "and comes out with the box.\n");
}

/* WHICH ROOMS THE POWER MAP IS ABOUT. A nine-floor tower is four hundred
 * rooms and a page listing every corridor's one cleaner's socket is a page
 * nobody reads -- but the rule that dropped the corridors also dropped the
 * comms cupboards, the plant rooms, the risers and the MDF until something
 * was standing in them, and those are THE ROOMS THE SOCKETS RUN OUT IN.
 *
 * A playtester stood in an empty comms cupboard whose own `look` said "4
 * outlets on the wall, 4 free", typed `outlets`, and got eleven let offices
 * with thirteen free sockets each and no cupboard at all. The one question
 * the page exists to answer -- how many sockets has the empty room I am
 * about to fill -- was the one it refused.
 *
 * So: every room built to hold equipment is always on the map, empty or not,
 * and everywhere else appears once there is something in it or a socket in
 * use. That is the same distinction outlets_built_in() already draws. */
static bool room_holds_kit(int kind)
{
    switch (kind) {
    case RM_MDF: case RM_SERVER: case RM_COMMS:
    case RM_PLANT: case RM_RISER: case RM_GOODS:
        return true;
    default:
        return false;
    }
}

/* THE POWER MAP. The owner asked for "a way to view the mini map for the
 * entire area and request/order additional power", and this is the first
 * half: every room kit can live in plus every room with something in it --
 * what the wiring gave it, what has been bought, what is in it, and what one
 * more would cost. */
void site_dump_outlets(const Site *s, int floor, Buf *out)
{
    if (!s->b) return;
    buf_printf(out, "  power, %s\n", floor < 0 ? "the whole building" : "one floor");
    buf_puts(out, "  room                     built  added  in use  free   "
                  "another\n");
    int shown = 0, spent = 0;
    for (int r = 0; r < s->b->nrooms; r++) {
        if (floor >= 0 && s->b->rooms[r].floor != floor) continue;
        int built = site_room_outlets_built(s, r);
        int have  = site_room_outlets(s, r);
        int used  = site_room_outlets_used(s, r);
        int here  = 0;
        for (int i = 0; i < s->ndev; i++) if (s->dev[i].room == r) here++;
        if (!room_holds_kit(s->b->rooms[r].kind) &&
            !used && !here && have == built) continue;
        char w[48];
        snprintf(w, sizeof w, "f%d %s #%d", s->b->rooms[r].floor,
                 bld_kind_name(s->b->rooms[r].kind), r);
        buf_printf(out, "  %-24s %5d  %5d  %6d  %4d", w, built, have - built,
                   used, have - used);
        if (have >= site_room_outlets_max(s, r))
            buf_puts(out, "   the circuit is full\n");
        else
            buf_printf(out, "   %ld\n", site_outlet_price(s, r));
        shown++;
        /* WHAT IS NOT PLUGGED IN, named, because that is the whole point of
         * the page: a box standing in a room with no socket left for it is
         * the reason nothing happens when its button is pressed. */
        for (int i = 0; i < s->ndev; i++) {
            if (s->dev[i].room != r || s->dev[i].kind == SDEV_DESK) continue;
            if (s->dev[i].mains) continue;
            /* AND WHAT TO DO ABOUT IT DEPENDS ON THE WALL, which this line
             * did not look at: it told a player standing in a room with two
             * empty sockets to buy a third or carry the box somewhere else.
             * A socket free is a lead away. */
            if (site_room_outlets_free(s, r) > 0)
                buf_printf(out, "      %s is NOT plugged in, and there is a "
                                "socket free -- `mains %s on`\n",
                           s->dev[i].name, s->dev[i].name);
            else
                buf_printf(out, "      %s is NOT plugged in -- `outlet` here, or "
                                "carry it somewhere with a socket free\n",
                           s->dev[i].name);
        }
    }
    for (int i = 0; i < s->nsock; i++) spent += s->sock[i].cost;
    if (!shown) buf_puts(out, "  nothing on it draws power yet.\n");
    if (s->nsock)
        buf_printf(out, "  %d outlet%s ordered over the run, %d paid.\n",
                   s->nsock, s->nsock == 1 ? "" : "s", spent);
}

void site_dump_links(const Site *s, Buf *out)
{
    int total = 0, cost = 0, dead = 0, deadm = 0, deadc = 0;
    buf_puts(out, "  cable\n");
    for (int i = 0; i < s->nlink; i++) {
        const SiteLink *l = &s->link[i];
        if (l->cable < 0) { dead++; deadm += l->metres; deadc += l->cost; continue; }
        char a[40], b[40];
        snprintf(a, sizeof a, "%s:%d", s->dev[l->a].name, l->aport);
        snprintf(b, sizeof b, "%s:%d", s->dev[l->b].name, l->bport);
        buf_printf(out, "  %2d  %-16s %-16s %4d m  %-6s %4d  %s", i, a, b,
                   l->metres, site_cable_name((CableKind)l->kind), l->cost,
                   pstate(site_link_state(s, i)));
        /* AND WHERE THE METRES CAME FROM. A lead into a jack is a run like
         * any other on the wire and 12 on the invoice, and a player reading
         * this table has to be able to tell which of their runs they are
         * still paying for and which are already in the wall. */
        if (l->jack >= 0) buf_printf(out, ", a lead in j%d", l->jack);
        buf_putc(out, '\n');
        total += l->metres; cost += l->cost;
    }
    if (!total && !dead) buf_puts(out, "  none\n");
    buf_printf(out, "  %d m of cable in the building, worth %d\n", total, cost);
    if (dead)
        buf_printf(out, "  %d pulled run%s not on it: %d m, %d gone. "
                        "%d spent on cable in all -- pulling it refunds nothing\n",
                   dead, dead == 1 ? "" : "s", deadm, deadc, cost + deadc);
    /* THE COPPER THAT IS NOT GOING ANYWHERE. It is money spent, like the
     * pulled runs, and unlike them it is still an asset -- so it gets its own
     * line and its own sentence, and the sentence is the reason to buy one. */
    if (s->njack) {
        int jm = 0, jc = 0, leads = 0, leadc = 0, live = 0, waiting = 0;
        for (int i = 0; i < s->njack; i++) {
            jm += s->jack[i].metres; jc += s->jack[i].cost;
            leads += s->jack[i].leads; leadc += s->jack[i].lead_spend;
            if (s->jack[i].link >= 0) live++;
            if (s->day < s->jack[i].ready) waiting++;
        }
        buf_printf(out, "  %d jack%s in the wall: %d of those metres, %d paid to "
                        "have them put in, %d in use",
                   s->njack, s->njack == 1 ? "" : "s", jm, jc, live);
        if (waiting) buf_printf(out, ", %d not put in yet", waiting);
        buf_printf(out, "\n  %d lead%s into them since, %d in all -- a lead comes "
                        "out and the run stays\n",
                   leads, leads == 1 ? "" : "s", leadc);
        buf_puts(out, "\n");
        site_dump_jacks(s, BLD_NOROOM, out);
    }
}

/* ------------------------------------------------------------- the quote */
/* WHAT THIS RUN WOULD COST, PRINTED BEFORE ANY OF IT IS BOUGHT.
 *
 * The rule this obeys is the one this project has broken three times in a
 * day: every number below comes out of the function that will charge for it
 * or measure it, and none of them is typed twice. The metres are
 * site_run_metres(), which is what site_cable() and site_jack() price from.
 * The prices are site_cable_price() and site_jack_price() themselves. The
 * days are site_jack_days(). The speeds are laid in a scratch world and read
 * off net_port_speed(), and the port half of them is site_kind_port_mb() by
 * way of the rate the port really has. There is no table in this function.
 *
 * AND IT IS HONEST ABOUT WHAT IT CANNOT KNOW. A quote is for a ROUTE. The
 * kit at each end has the last word on the speed, so an end with no box in
 * it yet gets a sentence saying the cable's number is all there is, rather
 * than a number that assumes a gigabit card somebody has not bought. */
static int quote_port_mb(const Site *s, int dev, int port)
{
    if (dev < 0 || dev >= s->ndev) return 0;
    if (port < 0 || port >= s->dev[dev].nports) port = 0;
    /* WHAT THE PORT REALLY DOES, off the port itself: the catalogue rate
     * site_install() set, or the circuit on the handoff, which is a smaller
     * number than the socket and is the one the frames obey. */
    int mb = net_port_rate_of(s->net, s->dev[dev].node, port);
    return mb > 0 ? mb : site_kind_port_mb(s->dev[dev].kind, port);
}

static void quote_end(const Site *s, int room, int dev, int port,
                      char *out, size_t cap)
{
    char w[64];
    if (room >= 0 && s->b && room < s->b->nrooms) {
        const Room *r = &s->b->rooms[room];
        snprintf(w, sizeof w, "f%d %s #%d", r->floor, bld_kind_name(r->kind), room);
    } else {
        snprintf(w, sizeof w, "outside");
    }
    if (dev >= 0 && dev < s->ndev)
        snprintf(out, cap, "%s:%d in %s", s->dev[dev].name, port < 0 ? 0 : port, w);
    else
        snprintf(out, cap, "%s", w);
}

void site_dump_quote(const Site *s, int room_a, int room_b,
                     int dev_a, int port_a, int dev_b, int port_b, Buf *out)
{
    int m = site_run_metres(s, room_a, room_b);
    char ea[96], eb[96];
    quote_end(s, room_a, dev_a, port_a, ea, sizeof ea);
    quote_end(s, room_b, dev_b, port_b, eb, sizeof eb);
    if (m < 0) {
        buf_printf(out, "no quote: there is no cable tray between %s and %s.\n",
                   ea, eb);
        return;
    }
    buf_printf(out, "a run from %s to %s: %d m through the tray.\n", ea, eb, m);

    int mb[CAB_KIND_COUNT];
    site_cable_speeds(m, mb);
    /* The port half. 0 at an end means nobody has put a box there yet. */
    int pa = quote_port_mb(s, dev_a, port_a), pb = quote_port_mb(s, dev_b, port_b);
    int kit = 0;
    if (pa && pb) kit = pa < pb ? pa : pb;
    else if (pa || pb) kit = pa ? pa : pb;

    /* CHEAPEST FIRST, and the order is the prices themselves rather than a
     * second list of grades that could fall out of step with the first. */
    int ord[CAB_KIND_COUNT];
    for (int i = 0; i < CAB_KIND_COUNT; i++) ord[i] = i;
    for (int i = 1; i < CAB_KIND_COUNT; i++)
        for (int j = i; j > 0 &&
             site_cable_price((CableKind)ord[j], m) <
             site_cable_price((CableKind)ord[j - 1], m); j--) {
            int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t;
        }

    buf_puts(out, "  grade   off the spool   as a jack   it comes up at\n");
    int held = 0;                       /* grades the kit, not the copper, caps */
    for (int i = 0; i < CAB_KIND_COUNT; i++) {
        int k = ord[i];
        int neg = mb[k];
        if (neg && kit && kit < neg) { neg = kit; held++; }
        buf_printf(out, "  %-6s  %11d   %9d   ", site_cable_name((CableKind)k),
                   site_cable_price((CableKind)k, m),
                   site_jack_price((CableKind)k, m));
        if (!mb[k]) buf_puts(out, "nothing: the run is longer than it carries");
        else buf_printf(out, "%d Mb", neg);
        buf_putc(out, '\n');
    }
    /* WHY SOME OF THOSE ARE NOT THE DRUM'S NUMBER. Once, under the table,
     * because it is one fact about one port and not four facts about four
     * grades -- and it is the sentence that says which of them is money
     * burnt. */
    if (held) {
        int hd = (pa && (!pb || pa <= pb)) ? dev_a : dev_b;
        int hp = (pa && (!pb || pa <= pb)) ? port_a : port_b;
        buf_printf(out, "  %s:%d does %d Mb whatever you plug into it, and that "
                        "is what holds the\n  faster %s down. Paying for reach "
                        "you cannot land is money burnt.\n",
                   s->dev[hd].name, hp < 0 ? 0 : hp, kit,
                   held == 1 ? "grade" : "grades");
    }
    /* THE OTHER HALF OF THE JACK, which is not money. */
    buf_printf(out, "  a jack is %d day%s of the trade's time and it is not a "
                    "socket before then;\n  a lead into it afterwards is %d, "
                    "for every box that ever stands there.\n",
               site_jack_days(m), site_jack_days(m) == 1 ? "" : "s",
               site_jack_lead_price());
    /* THE MARGIN. Not a warning about a rule: a statement about this run. */
    if (m >= SITE_COPPER_MARGIN_M)
        buf_printf(out, "  %d m is past the %d m copper has margin for: under a "
                        "floor's load this run\n  takes CRC errors, says so in "
                        "`events`, and retrains itself down. Fibre does not.\n",
                   m, SITE_COPPER_MARGIN_M);
    /* AND WHAT THE QUOTE CANNOT KNOW. */
    if (!pa || !pb) {
        char bare[200];
        if (!pa && !pb) snprintf(bare, sizeof bare, "either end");
        else snprintf(bare, sizeof bare, "%s", !pa ? ea : eb);
        buf_printf(out, "  no box is named at %s:\n  those speeds are the MOST "
                        "this run comes up at, because the port at each\n  end "
                        "has the last word and it arrives with the box.\n", bare);
    }
    buf_puts(out, "  nothing was bought, nothing was booked and nothing was "
                  "charged.\n");
}

void site_dump_rooms(const Site *s, int floor, Buf *out)
{
    buf_printf(out, "floor %d\n", floor);
    for (int i = 0; i < s->b->nrooms; i++) {
        const Room *r = &s->b->rooms[i];
        if (r->floor != floor) continue;
        buf_printf(out, "  #%-4d %-16s %4.0f m2", i, bld_kind_name(r->kind),
                   bld_room_area(r));
        if (r->tenant) buf_printf(out, "  tenant %d", r->tenant);
        buf_putc(out, '\n');
    }
}

/* WHICH BOX IN THIS BUILDING ANSWERS TO THIS ADDRESS, or -1 for an address
 * that is somebody else's. Every interface of every box, because the address
 * a diagnostic was aimed at is as likely to be a subinterface carrying a
 * tenancy's vlan as it is to be eth0. */
static int dev_by_ip(const Site *s, uint32_t ip)
{
    if (!ip) return -1;
    for (int d = 0; d < s->ndev; d++)
        for (int i = 0; i < NET_IF_MAX; i++)
            if (net_if_exists(s->net, s->dev[d].node, i) &&
                net_if_get_addr(s->net, s->dev[d].node, i) == ip)
                return d;
    return -1;
}

/* NOTHING CAME BACK, AND A COUNTER ON THE FAR BOX WENT UP.
 *
 * `ping edge 10.0.0.10` printing `no answer` is true and it is the whole
 * story of a wire that is fine and a filter that is doing its job -- the
 * shipped ruleset is `policy drop` plus 22 and 80, and an echo request it
 * did not ask for has no socket. Two playtesters lost ten minutes each to
 * that silence, and one of them re-cut a trunk to fix a routing fault that
 * did not exist.
 *
 * So: read the far box's drop counter before and after. If it went up, say
 * so -- as a MEASUREMENT, not a diagnosis. It names no fault and repairs
 * nothing; it points at the counter, which the player could have read
 * themselves by walking to the box, and that is the right amount of help.
 * If the counter did not move, this says nothing at all, because a confident
 * sentence that contradicts the machine costs more than silence. */
static uint64_t fw_drops_of(const Site *s, int dev)
{
    if (dev < 0 || dev >= s->ndev) return 0;
    return net_fw_drops(s->net, s->dev[dev].node);
}
static void fw_blame(const Site *s, int dev, uint64_t before, const char *what,
                     Buf *out)
{
    if (dev < 0 || dev >= s->ndev) return;
    uint64_t now = fw_drops_of(s, dev);
    if (now <= before) return;
    unsigned long long d = (unsigned long long)(now - before);
    buf_printf(out, "  %s is a box in this building, and its packet filter "
                    "counted %llu more\n  drop%s while that %s was out. "
                    "`netstat -F` on %s says which rule matched.\n",
               s->dev[dev].name, d, d == 1 ? "" : "s", what, s->dev[dev].name);
}

/* IS ANY OF THIS BOX ACTUALLY ON THE NETWORK? A machine is on the network
 * when it is running, has a lead in a socket that came up, and has an
 * address on the card that lead is in. Anything less and it is a beige box
 * with a light on. This is asked rather than assumed because `show` used to
 * assert it in prose. */
static bool on_network(const Site *s, int dev)
{
    const SiteDev *d = &s->dev[dev];
    if (site_kind_has_os(d->kind) && !d->powered) return false;
    bool link = false;
    for (int p = 0; p < d->nports; p++)
        if (net_port_state(s->net, d->node, p) == PORT_UP) { link = true; break; }
    if (!link) return false;
    for (int i = 0; i < NET_IF_MAX; i++)
        if (net_if_exists(s->net, d->node, i) &&
            net_if_get_addr(s->net, d->node, i)) return true;
    return false;
}

/* The services this box is running, named where a player looks for them. */
static void dump_services(const Site *s, int dev, Buf *out)
{
    const SiteDev *d = &s->dev[dev];
    int node = d->node;
    /* A BOX WITH NO POWER IN IT IS RUNNING NOTHING, and the trailer used to
     * say "It is on the network and serves nothing from it" four lines under
     * a header that had just said nothing of it was on the network. One
     * screen, two answers, and the wrong one was the reassuring one. What
     * the trailer says is now derived from the same facts as the header. */
    bool off = site_kind_has_os(d->kind) && !d->powered;
    if (off) {
        buf_puts(out, "services: none. It is switched off, so nothing of it "
                      "is running and nothing of it is on the network.\n");
        return;
    }
    int pools = net_dhcpd_pools(s->net, node);
    int hp = net_httpd_port(s->net, node);
    bool ns = net_dnsd_running(s->net, node);
    if (!pools && !hp && !ns) {
        if (on_network(s, dev))
            buf_puts(out, "services: none. It is on the network and serves "
                          "nothing from it.\n");
        else
            buf_puts(out, "services: none, and nothing of it is on the "
                          "network to serve anything from.\n");
        return;
    }
    buf_puts(out, "services:\n");
    for (int i = 0; i < pools; i++) {
        int ifx = 0, count = 0;
        uint32_t first = 0, mask = 0, gw = 0, dns = 0;
        if (!net_dhcpd_pool(s->net, node, i, &ifx, &first, &count, &mask, &gw, &dns))
            break;
        char a[20], b[20], nm[24];
        net_fmt_ip(first, a, sizeof a);
        net_fmt_ip(first + (uint32_t)(count - 1), b, sizeof b);
        net_if_name(s->net, node, ifx, nm, sizeof nm);
        int vlan = net_if_get_vlan(s->net, node, ifx);
        buf_printf(out, "  dhcpd  %s-%s on %s", a, b, nm);
        if (vlan) buf_printf(out, " (vlan %d)", vlan);
        /* THIS POOL'S LEASES, not the box's. Seven ranges on one router each
         * printed the box-wide total, so a screen whose whole job is to say
         * which segment has people on it said the same number seven times. */
        int held = net_dhcpd_pool_leases(s->net, node, i);
        buf_printf(out, ", %d lease%s out\n", held, held == 1 ? "" : "s");
    }
    if (ns) {
        /* NOT "ANSWERING NAMES FOR THE TOWER", which a server with an empty
         * zone and no forwarder is not. Two numbers, both of them read off
         * the daemon. */
        int nr = net_dns_record_count(s->net, node);
        uint32_t up = net_dns_forwarder(s->net, node);
        char f[20];
        net_fmt_ip(up, f, sizeof f);
        buf_printf(out, "  dnsd   %d name%s of its own, forwarding the rest %s%s\n",
                   nr, nr == 1 ? "" : "s", up ? "to " : "nowhere", up ? f : "");
    }
    if (hp) buf_printf(out, "  httpd  serving its own files on port %d\n", hp);
}

static void dump_dev(Site *s, int dev, Buf *out, bool empties)
{
    if (dev < 0 || dev >= s->ndev) { buf_puts(out, "no such device\n"); return; }
    SiteDev *d = &s->dev[dev];
    char w[48];
    where(s, d, w, sizeof w);
    /* THE SOCKETS, AND WHICH NUMBERS THEY ARE.
     *
     * This said "1 socket" and stopped. Underneath it netstack prints "1
     * more socket on the back of it, with nothing in it" about the SAME
     * socket -- "more" meaning "the rest of the ones I did not list", not
     * "another one" -- and a playtester read the two lines as one socket
     * plus one more, went looking for the second, and spent a session
     * trying to hang a router off `uplink:1`. The handoff has one port and
     * has never had two.
     *
     * So the header carries the whole fact and closes the arithmetic: how
     * many sockets there are, WHAT THEY ARE NUMBERED -- which is the
     * question the player was really asking -- and how many a lead can
     * still go into, counted by site_ports_spare(), which steps over the
     * ports a jack holds for good exactly as `cable` and `serve` do. */
    buf_printf(out, "%s: %s in %s, %d socket%s, numbered 0 to %d, %d free "
                    "for a lead%s\n",
               d->name, site_kind_name(d->kind), w,
               d->nports, d->nports == 1 ? "" : "s", d->nports - 1,
               site_ports_spare(s, dev),
               site_kind_has_os(d->kind) && !d->powered
               ? " -- SWITCHED OFF, and nothing of it is on the network" : "");
    /* THE PLUG, WHERE A PLAYER LOOKS. `show` is the page somebody reads when
     * a box is not doing anything, so the first fact about a box that is
     * doing nothing has to be here: it is not drawing power, and no amount
     * of pressing the button is going to change that. */
    if (dev != s->uplink && d->kind != SDEV_DESK && !d->mains) {
        int have = site_room_outlets(s, d->room);
        buf_printf(out, "  NOT PLUGGED IN -- there is no lead from it to a wall "
                        "socket, so its\n  power button does nothing. %s has %d "
                        "outlet%s and %d free.\n", w, have, have == 1 ? "" : "s",
                   site_room_outlets_free(s, d->room));
    }
    if (empties) net_dump_ports(s->net, d->node, out);
    else net_dump_ports_used(s->net, d->node, out);
    /* THE SOCKETS THAT ARE NOT SOCKETS ANY MORE. netstack knows this box has
     * a port with no cable in it; it cannot know that somebody punched the
     * pair down onto a panel and screwed a faceplate to a wall two floors up.
     * A player counting free holes on a core switch has to be told, or they
     * will count a port they cannot use -- and `serve` will not use it
     * either. */
    for (int j = 0; j < s->njack; j++) {
        if (s->jack[j].home != dev) continue;
        char w[48];
        if (s->b && s->jack[j].room < s->b->nrooms) {
            const Room *r = &s->b->rooms[s->jack[j].room];
            snprintf(w, sizeof w, "f%d %s #%d", r->floor, bld_kind_name(r->kind),
                     s->jack[j].room);
        } else snprintf(w, sizeof w, "?");
        buf_printf(out, "port %-2d is punched down to jack j%d, %d m of %s to "
                        "%s -- it holds\n        that port for good%s\n",
                   s->jack[j].hport, j, s->jack[j].metres,
                   site_cable_name((CableKind)s->jack[j].kind), w,
                   s->day < s->jack[j].ready ? ", and the trade has not been yet"
                   : s->jack[j].link >= 0 ? "" : ", and nothing is in the faceplate");
    }
    if (site_kind_is_switch(d->kind)) {
        net_dump_fdb(s->net, d->node, out);
    } else {
        net_dump_ifaces(s->net, d->node, out);
        net_dump_routes(s->net, d->node, out);
        net_dump_arp(s->net, d->node, out);
        /* WHAT IT IS SERVING. `show srv3` used to say what it was and where
         * it was and never what it did, so a DHCP server that had stopped
         * serving looked exactly like one that was -- and the only way to
         * find out was to watch a floor fail to get addresses. */
        dump_services(s, dev, out);
    }
}

void site_dump_dev(Site *s, int dev, Buf *out) { dump_dev(s, dev, out, true); }
/* What a person reads when they have just put a lead in a box: what is
 * plugged into it, not a list of the sockets that are empty. */
void site_dump_dev_brief(Site *s, int dev, Buf *out) { dump_dev(s, dev, out, false); }

/* ------------------------------------------------------------- the shell */
/* One line, one operation. Deliberately dull to parse: a blind playtester
 * writing a script should never have to think about quoting. */
/* HOW MANY WORDS FIT ON ONE LINE, and what happens to the ones that do not.
 *
 * This was twelve, silently. `trunk core 22 11 12 ... 23` is sixteen words,
 * so the last four vlans were dropped on the floor and the verb answered
 * "set" -- and since nothing printed a trunk's allowed list either, the
 * floor those four vlans belonged to was quietly dead for eight in-game
 * days. Two things were wrong and only one of them was the number.
 *
 * The number is now sixty-four, which is past anything the game asks for: a
 * vlan per tenancy is the build D27 recommends and the fullest seed's demand
 * table asks for thirty-six tenancies, so `trunk core 22` plus thirty-six
 * vlans is thirty-nine words. And split() now REFUSES a line it cannot hold
 * rather than truncating it, so if this number is ever passed again the
 * player is told, at the line they typed, instead of finding out a week
 * later on a floor that does not work. */
#define MAXTOK 64

/* Returns the token count, or -1 for a line with more words than MAXTOK. */
static int split(char *line, char *tok[MAXTOK])
{
    int n = 0;
    char *p = line;
    while (*p) {
        if (n == MAXTOK) {
            /* Is there another word out there, or just trailing space? Only
             * a real word past the end is a truncation. (A '#' this far in
             * is not a comment: a comment is only a comment at the start of
             * a line, where nobody means a room.) */
            while (*p == ' ' || *p == '\t') p++;
            return *p ? -1 : n;
        }
        while (*p == ' ' || *p == '\t') p++;
        /* '#' STARTS A COMMENT, AND '#41' IS A ROOM.
         *
         * This broke on the first token as well as the rest, so `go #12` and
         * `order switch8 #41` parsed as an empty line -- and #41 is the
         * spelling site_room_by_name() documents. A comment is only a
         * comment at the start of a line, where nobody means a room. */
        if (!*p) break;
        if (*p == '#' && p == line) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

/* "10.0.4.1/24", or "10.0.4.1" for the /24 a person means when they leave
 * the prefix off. */
static bool parse_cidr(const char *s, uint32_t *ip, uint32_t *mask)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    char *slash = strchr(buf, '/');
    int bits = 24;
    if (slash) { *slash = 0; bits = atoi(slash + 1); }
    if (bits < 0 || bits > 32) return false;
    if (!net_parse_ip(buf, ip)) return false;
    *mask = net_mask_bits(bits);
    return true;
}

/* A NUMBER, AND NOTHING BUT. atoi("eth0") is 0, and a parser that accepts it
 * turns a line that reads correctly into a line that does something else in
 * silence. */
static bool small_number(const char *a, int *out)
{
    if (!a || !*a) return false;
    int v = 0;
    for (const char *p = a; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + (*p - '0');
        if (v > 9999) return false;
    }
    *out = v;
    return true;
}

static int dev_arg(const Site *s, const char *a)
{
    int d = site_dev_by_name(s, a);
    if (d >= 0) return d;
    if (a[0] >= '0' && a[0] <= '9') {
        int n = atoi(a);
        if (n >= 0 && n < s->ndev) return n;
    }
    return -1;
}

/* "sw1:3" -- device sw1, port 3. */
static bool port_arg(const Site *s, char *a, int *dev, int *port)
{
    char *colon = strchr(a, ':');
    if (!colon) return false;
    *colon = 0;
    *dev = dev_arg(s, a);
    *port = atoi(colon + 1);
    *colon = ':';
    return *dev >= 0;
}

/* ONE END OF A QUOTE, in either of the two ways a player thinks about a run.
 * A box (`core`, `core:2`) is where a run really terminates, and the port is
 * what decides the speed. A room (`#41`, `f3.comms`) is the question asked
 * before the box is bought -- which is most of the questions worth asking,
 * because the whole point is to find out what a room costs to reach BEFORE
 * putting anything in it. Returns false only when it is neither. */
static bool quote_end_arg(const Site *s, const char *a, int *room, int *dev,
                          int *port)
{
    char b[64];
    snprintf(b, sizeof b, "%s", a);
    char *colon = strchr(b, ':');
    if (colon) { *colon = 0; *port = colon[1] ? atoi(colon + 1) : -1; }
    int d = dev_arg(s, b);
    if (d >= 0) {
        *dev = d;
        *room = s->dev[d].room;
        if (*port < 0 || *port >= s->dev[d].nports) {
            int f = site_free_port(s, d);
            *port = f >= 0 ? f : 0;
        }
        return true;
    }
    if (colon) return false;           /* `x:2` only ever means a box       */
    int r = site_room_by_name(s, b);
    if (r < 0) return false;
    *room = r; *dev = -1; *port = -1;
    return true;
}

static CableKind cable_arg(const char *a)
{
    for (int i = 0; i < CAB_KIND_COUNT; i++)
        if (strcmp(SPOOL[i].name, a) == 0) return (CableKind)i;
    return CAB_CAT6;
}

/* WHAT EVERY VERB WANTS, IN THE VERB'S OWN WORDS.
 *
 * `dhcpd edge` answered "no such command: dhcpd (try help)" -- about a
 * command that exists, is in the help, and had simply been handed the wrong
 * number of arguments. That is the same lie as a help text naming a command
 * the machine has not got, arriving from the other side: the player is told
 * the thing they read about does not exist, and stops trusting the rest of
 * the page. Every verb the dispatcher below answers to is in this table with
 * the smallest number of words it can do anything with, and a line short of
 * that gets the spelling instead of a denial. The table is also the list of
 * verbs that exist, so "no such command" is still true when it is printed.
 */
static const struct { const char *verb; int need; const char *usage; } VERB[] = {
    { "help",     1, "help" },
    { "show",     1, "show [<box>]" },
    { "links",    1, "links" },
    { "rooms",    1, "rooms [<floor>]" },
    { "demand",   1, "demand" },
    { "day",      1, "day [<n>]" },
    { "status",   1, "status" },
    { "events",   1, "events" },
    { "service",  1, "service | service ?   the tenancies, and `?` for what\n                      every column of them means" },
    { "load",     1, "load | load ?         the busiest ports, and `?` for what\n                      every column of them means" },
    { "money",    1, "money" },
    { "frames",   1, "frames" },
    { "isp",      1, "isp [<mb>]" },
    { "credit",   2, "credit <amount>" },
    { "ups",      2, "ups <box>" },
    { "disk",     2, "disk <box>" },
    { "order",    2, "order <kind> [name]   switch4 switch8 switch24 router pc\n"
                     "                      minitower server rackserver\n"
                     "Three grades of switch and three of server, and the difference\n"
                     "is a SPEC: sockets, what each one clocks, what the disk is\n"
                     "rated for, whether a battery is in it. Nothing multiplies\n"
                     "anything by a grade. `catalogue` prints the whole list\n"
                     "with the specs and charges nothing." },
    { "catalogue", 1, "catalogue             every kind, its price and its spec,\n"
                     "                      off the same table the counter charges\n"
                     "                      from. Nothing is bought." },
    { "buy",      2, "buy <kind> [name]     switch4 switch8 switch24 router pc\n"
                     "                      minitower server rackserver" },
    { "move",     3, "move <box> <room>     rooms: #41, f3.comms, f0.mdf" },
    { "cable",    3, "cable <box>:<port> <box>:<port> [cat5e|cat6|fibre|cat5]" },
    { "uncable",  2, "uncable <n>           `links` numbers them" },
    { "quote",    3, "quote <a> <b>         what that run would cost, before it\n"
                     "                      is run: the tray metres, the price in\n"
                     "                      every grade, what each would come up\n"
                     "                      at over that distance, and the same\n"
                     "                      run as a jack. Each end is a box\n"
                     "                      (`core`, `core:2`) or a room (`#41`,\n"
                     "                      `f3.comms`). Nothing is bought" },
    { "jack",     3, "jack <room> <box>:<port> [cat5e|cat6|fibre|cat5]\n"
                     "                      have a permanent socket put in that\n"
                     "                      room, with the run behind it punched\n"
                     "                      down on that port. Priced by the tray\n"
                     "                      metres, and it takes the trade days" },
    { "patch",    3, "patch <box>:<port> j<n>   a lead from a box into a jack in\n"
                     "                      the room it is standing in" },
    { "jacks",    1, "jacks                 every jack in the building" },
    { "serve",    3, "serve <tenant> <box> [cat5|cat5e|cat6|fibre] [vlan]" },
    { "addr",     3, "addr <box>[:<nic>] <ip>/<bits>" },
    { "power",    3, "power <box> on|off" },
    { "mains",    2, "mains <box> on|off    the PLUG, which is not the button:\n"
                     "                      put the box in a wall socket in the\n"
                     "                      room it is standing in, or pull it\n"
                     "                      out. A box that is not in one cannot\n"
                     "                      be switched on at all, and a switch\n"
                     "                      has no button, so this is its" },
    { "outlet",   2, "outlet <room>         have another socket put into that\n"
                     "                      room. Priced on the run back to the\n"
                     "                      riser, charged now, and it does not\n"
                     "                      come out again" },
    { "outlets",  1, "outlets [<floor>]     every room kit can live in -- comms\n"
                     "                      cupboards, plant, risers, the MDF, goods\n"
                     "                      in -- empty or not, plus any other room\n"
                     "                      with something in it: what it was wired\n"
                     "                      with, what is plugged in, what is free\n"
                     "                      and what another socket would cost" },
    { "gw",       3, "gw <box> <ip>" },
    { "router",   3, "router <box> on|off" },
    { "subif",    5, "subif <box> <nic> <vlan> <ip>/<bits>   add one\n"
                     "subif <box> <nic> <vlan> off           take it away again" },
    { "vlan",     4, "vlan <switch> <port> <n>" },
    { "trunk",    3, "trunk <switch> <port> <vlan>...   let these across it\n"
                     "trunk <switch> <port> -<vlan>     take that one back off\n"
                     "trunk <switch> <port> none        take them all off\n"
                     "It answers with the list the port carries afterwards, and\n"
                     "`show <switch>` prints the same list." },
    { "dhcpd",    2, "dhcpd <box> <first> <count> <bits> <gw> <dns>   start a pool\n"
                     "dhcpd <box> off                                stop them all\n"
                     "dhcpd <box>                                    what it serves\n"
                     "<count> is how many addresses, starting at <first>. The pool\n"
                     "goes on whichever interface of that box is already on that\n"
                     "subnet, so a router with a subinterface per vlan serves a pool\n"
                     "per vlan by being told about them one at a time -- up to EIGHT\n"
                     "pools on one box, which is all one box holds." },
    { "dhcp",     2, "dhcp <box>            ask for a lease, for real" },
    { "httpd",    2, "httpd <box> [port]" },
    { "dnsd",     2, "dnsd <box>" },
    { "dns",      4, "dns <box> <name> <ip>" },
    { "resolver", 3, "resolver <box> <ip>" },
    { "ping",     3, "ping <box> <ip>" },
    { "trace",    3, "trace <box> <ip>" },
    { "resolve",  3, "resolve <box> <name>" },
    { "get",      4, "get <box> <ip> <path>" },
    { NULL, 0, NULL }
};

int site_verb_count(void) { return (int)(sizeof VERB / sizeof VERB[0]) - 1; }
const char *site_verb_name(int i)
{
    return (i >= 0 && i < site_verb_count()) ? VERB[i].verb : NULL;
}
int site_verb_arity(int i)
{
    return (i >= 0 && i < site_verb_count()) ? VERB[i].need : 0;
}

bool site_cmd(Site *s, const char *line, Buf *out)
{
    /* THE LINE ITSELF HAS A CAP TOO, and it used to truncate in silence in
     * exactly the same way the token count did -- snprintf() copies what
     * fits and says nothing. Sixty-four vlans of four digits plus a verb is
     * under 400 characters, so 1024 holds anything MAXTOK will take; a line
     * past it is refused rather than half-obeyed. */
    char buf[1024];
    if (line && strlen(line) >= sizeof buf) {
        buf_printf(out, "that line is %zu characters and I can only take %zu. "
                        "Nothing was done --\n  split it into two lines.\n",
                   strlen(line), sizeof buf - 1);
        return true;
    }
    snprintf(buf, sizeof buf, "%s", line);
    char *t[MAXTOK];
    int n = split(buf, t);
    /* A PARSER THAT DROPS INPUT MUST SAY SO. It answered "set" to a `trunk`
     * line whose last four vlans it had thrown away; now the line does
     * nothing at all and says why. Refusing the whole line rather than
     * obeying the front of it is the point: half a trunk that looks like a
     * whole one is what cost the playtester eight days. */
    if (n < 0) {
        buf_printf(out, "that line has more than %d words in it. Nothing was "
                        "done -- no part of it\n  was run.\n", MAXTOK);
        if (strcmp(t[0], "trunk") == 0)
            buf_puts(out, "  Split it: `trunk <sw> <port> ...` may be typed "
                          "again and again, and each\n  line adds to what that "
                          "trunk already carries.\n");
        return true;
    }
    if (!n) return true;

    for (int i = 0; VERB[i].verb; i++) {
        if (strcmp(t[0], VERB[i].verb) != 0 || n >= VERB[i].need) continue;
        buf_printf(out, "%s\n", VERB[i].usage);
        return true;
    }

    if (strcmp(t[0], "help") == 0) {
        buf_puts(out,
            "catalogue                      every kind the shop sells, its price\n"
            "                               and its spec: sockets, what each one\n"
            "                               clocks, what the disk is rated for,\n"
            "                               whether a battery is in it. It buys\n"
            "                               nothing -- it is `quote` for kit\n"
            "order <kind> [name]            kinds: switch4 switch8 switch24 router pc\n"
            "                               minitower server rackserver -- THREE GRADES\n"
            "                               of switch and three of server. The cheap end\n"
            "                               is four sockets at 100 Mb and a disk rated\n"
            "                               for 30 days; the dear end is ten gigabit, 120\n"
            "                               days and a battery in it. Nothing multiplies\n"
            "                               anything by a grade: a slow port queues and\n"
            "                               drops sooner because it is a slow port, and\n"
            "                               `load` counts it. The upgrade is to buy the\n"
            "                               better box, carry it up and move the service\n"
            "                               onto it -- there is no `upgrade` verb\n"
            "                               it is delivered to goods in, on the\n"
            "                               ground floor. Not to where you are.\n"
            "move <dev> <room>              carry it there. Refused while it has a\n"
            "                               cable in it. rooms: #41, f3.comms,\n"
            "                               f0.mdf, f2.office\n"
            "cable <dev>:<port> <dev>:<port> [cat5|cat5e|cat6|fibre]\n"
            "uncable <n>                    pull one out\n"
            "quote <a> <b>                  what that run would cost BEFORE it is\n"
            "                               run: the tray metres, the price in\n"
            "                               every grade, what each comes up at\n"
            "                               over that distance, and the same run\n"
            "                               as a jack. An end is a box or a room\n"
            "                               (`core`, `core:2`, `#41`, `f3.comms`)\n"
            "jack <room> <dev>:<port> [cat5|cat5e|cat6|fibre]\n"
            "                               the other way to buy the same metres:\n"
            "                               a socket on that room's wall, with the\n"
            "                               run punched down on that port for good.\n"
            "                               It costs more than the spool run and it\n"
            "                               takes the trade days -- and after that\n"
            "                               a lead into it is a lead\n"
            "patch <dev>:<port> j<n>        that lead. The box has to be standing\n"
            "                               in the jack's room\n"
            "jacks                          every jack, what is in it, and the day\n"
            "                               the trade comes for the ones that are\n"
            "                               still a booking\n"
            "addr <dev>[:<nic>] <ip>/<bits> an address on a card. `addr rt 1.2.3.4/30`\n"
            "                               is the first socket, `addr rt:1 ...` the\n"
            "                               second -- which is how a router gets a WAN\n"
            "                               side and a LAN side\n"
            "power <dev> on|off             a pc and a server arrive switched off.\n"
            "                               Nothing of an off box is on the network.\n"
            "                               The button does NOTHING on a box with no\n"
            "                               lead to the wall -- see `mains`\n"
            /* THE THREE VERBS THIS PAGE DID NOT HAVE. `power` refuses a box
             * that is not plugged in, and until now the way out of that
             * refusal was in neither the message nor this list: `mains`,
             * `outlet` and `outlets` were verbs the site answered to and no
             * help text anywhere named. */
            "mains <dev> on|off             the plug itself: put the box into a free\n"
            "                               socket in the room it is standing in, or\n"
            "                               pull it out. An appliance has no button,\n"
            "                               so for a switch and a router this IS the\n"
            "                               button -- and pulling the plug on a\n"
            "                               running machine is a blackout with one\n"
            "                               machine in it\n"
            "outlets [<floor>|all]          every room kit can live in and what its\n"
            "                               wall has: built, added, in use, free, and\n"
            "                               what another socket there would cost\n"
            "outlet <room>                  have one more put in, today, for money.\n"
            "                               A room takes as many again as it was\n"
            "                               wired with and then its circuit is full\n"
            "gw <dev> <ip>                  default gateway\n"
            "router <dev> on|off            forward between its interfaces\n"
            "subif <dev> <nic> <vlan> <ip>/<bits>   a tagged subinterface on a card\n"
            "subif <dev> <nic> <vlan> off   take that subinterface away, with any\n"
            "                               pool that was answering on it\n"
            "vlan <dev> <port> <n>          a switch's access port, in a vlan\n"
            "trunk <dev> <port> <vlan>...   a trunk, and what it may carry. It\n"
            "                               ADDS, so a trunk can be built a line\n"
            "                               at a time; `-<vlan>` takes one back\n"
            "                               off and `none` takes them all off.\n"
            "                               It answers with the list the port\n"
            "                               carries afterwards, and `show <dev>`\n"
            "                               prints that list beside the port\n"
            "dhcpd <dev> <first> <count> <bits> <gw> <dns>\n"
            "                               a pool, on the interface of that box\n"
            "                               whose own address is in it -- so a\n"
            "                               router serves the vlan it is on and\n"
            "                               not the one next door. <count> is how\n"
            "                               many addresses; EIGHT POOLS is all one\n"
            "                               box holds, which is the limit on how\n"
            "                               many segments one router can serve\n"
            "dhcpd <dev> off                stop serving addresses. `dhcpd <dev>`\n"
            "                               says what it is serving now\n"
            "dhcp <dev>                     ask for a lease, for real\n"
            "resolver <dev> <ip>            resolv.conf, in one line\n"
            "ping <dev> <ip>                a real ICMP echo over the wire\n"
            "trace <dev> <ip>               traceroute, counted by ttl\n"
            "resolve <dev> <name>           a real DNS query\n"
            "dnsd <dev>                     a name server on that box, and what\n"
            "                               it will answer\n"
            /* AND THE OTHER HALF OF THAT PAIR. The page told a player how to
             * serve names and how to fetch a page, and not how to serve one
             * -- `httpd` has been a verb since the web host trade existed. */
            "httpd <dev> [port]             a web server on that box, serving what\n"
            "                               is on its disk. 80 unless you say\n"
            "                               otherwise -- and `get` is how you check\n"
            "                               it from another\n"
            "dns <dev> <name> <ip>          one name in that server's zone. A name\n"
            "                               it has not got goes to the resolver\n"
            "                               that box is configured with\n"
            "get <dev> <ip> <path>          fetch a page over TCP\n"
            "\n"
            "day [n]                        advance the clock. Tenants move in on\n"
            "                               their day, their people work over what\n"
            "                               you built, and rent arrives for the work\n"
            "                               that finished\n"
            "serve <tenant> <box> [cable] [vlan]\n"
            "                               run copper from a box you own to a\n"
            "                               tenancy's desks, one each, by the metre.\n"
            "                               Name a vlan and every port it patches is\n"
            "                               an access port in it, as it is patched\n"
            "isp [mb]                       what the circuit carries, and what it costs\n"
            "events                         what the world has done to the kit, and\n"
            "                               the condition it is in\n"
            "ups <box>                      a battery under it: it rides a mains\n"
            "                               failure out instead of coming back with\n"
            "                               a filesystem to check\n"
            "disk <box>                     a new disk, copied off the old one\n"
            "status | service | load        the day, who is suffering, which port is full.\n"
            "                               `service ?` and `load ?` explain every\n"
            "                               column of those two, once, instead of\n"
            "                               under every reading of them\n"
            "show [dev] | links | rooms <f> | demand | money | frames\n");
        return true;
    }
    if (strcmp(t[0], "show") == 0) {
        /* THE SOCKETS WITH SOMETHING IN THEM. A 24-port switch with two
         * links in it printed twenty-two lines of `no link` above the two
         * that mattered, on the screen a player goes to when something is
         * wrong. The empties are still counted -- "22 more sockets on the
         * back of it, with nothing in them" is what a person wants to know
         * about a free port -- they are just not twenty-two lines. */
        if (n > 1) site_dump_dev_brief(s, dev_arg(s, t[1]), out);
        else site_dump(s, out);
        return true;
    }
    if (strcmp(t[0], "links") == 0) { site_dump_links(s, out); return true; }
    /* THE PRICE LIST, BEFORE THE MONEY. `quote` exists so that copper can be
     * priced before it is committed to, and there was no equivalent for kit:
     * a blind playtester learned the catalogue by ordering one of everything
     * in a throwaway run, and said so -- "there is no way to see a price
     * before you buy... that undercuts the whole 'the opening is a decision'
     * premise". The `order` line pointed at `links halbert.co.uk/catalogue`,
     * which prints the CABLE LIST, because `links` ignores its argument. So
     * the one route the game named to its own price list did not exist.
     *
     * Off KIT[] through the same accessors the counter charges from, so a
     * grade added to core arrives here on the same commit. */
    if (strcmp(t[0], "catalogue") == 0) {
        buf_puts(out, "  kind         price  sockets  each at             disk      battery\n");
        for (int k = 0; k < SDEV_KIND_COUNT; k++) {
            if (!site_kind_for_sale(k)) continue;
            int np = site_kind_ports(k);
            int top = site_kind_port_mb(k, 0);
            int fast = top;
            for (int p = 1; p < np; p++)
                if (site_kind_port_mb(k, p) > fast) fast = site_kind_port_mb(k, p);
            char speed[40];
            if (fast != top) snprintf(speed, sizeof speed, "%d Mb, top %d", top, fast);
            else             snprintf(speed, sizeof speed, "%d Mb", top);
            int days = site_kind_disk_days(k);
            char disk[24];
            if (days > 0) snprintf(disk, sizeof disk, "%d days", days);
            else          snprintf(disk, sizeof disk, "%s", "-");
            buf_printf(out, "  %-11s %6d  %7d  %-18s  %-8s  %s\n",
                       site_kind_name(k), site_kind_price(k), np, speed, disk,
                       site_kind_has_ups(k) ? "yes" : "no");
        }
        buf_puts(out, "  Nothing here is bought. `order <kind> [name]` buys one and it\n"
                      "  is delivered to goods in; somebody carries it from there.\n");
        return true;
    }
    if (strcmp(t[0], "rooms") == 0) {
        /* `rooms f2` PRINTED FLOOR 0. atoi("f2") is 0, silently, so the one
         * spelling every other verb in this game takes -- `f1.comms`,
         * `f0.mdf`, `go f4.stair` -- answered about the wrong floor and
         * looked like it had worked. `rooms 2` and `rooms f2` are now the
         * same line, because a player who has typed `f` everywhere else
         * will type it here, and silently answering about somewhere else is
         * worse than refusing. Anything that is neither is refused rather
         * than rounded down to the ground floor. */
        int floor = 0;
        if (n > 1) {
            const char *a = t[1];
            if (*a == 'f' || *a == 'F') a++;
            if (!*a) { buf_printf(out, "which floor? `rooms 2` or `rooms f2`. "
                                       "The building has %d.\n", s->b->floors);
                       return true; }
            for (const char *q = a; *q; q++)
                if (*q < '0' || *q > '9') {
                    buf_printf(out, "`%s` is not a floor. `rooms <n>` or "
                                    "`rooms f<n>`, 0 to %d. A room's own name "
                                    "--\n  `f1.comms` -- is what `move`, `go` "
                                    "and `quote` take; `rooms` takes the "
                                    "floor.\n", t[1], s->b->floors - 1);
                    return true;
                }
            floor = atoi(a);
            if (floor < 0 || floor >= s->b->floors) {
                buf_printf(out, "there is no floor %d. This building has %d, "
                                "numbered 0 to %d.\n",
                           floor, s->b->floors, s->b->floors - 1);
                return true;
            }
        }
        site_dump_rooms(s, floor, out);
        return true;
    }
    if (strcmp(t[0], "demand") == 0) { site_dump_demand(s, out); return true; }
    /* ------------------------------------------------------------ the loop */
    if (strcmp(t[0], "day") == 0) {
        int days = n > 1 ? atoi(t[1]) : 1;
        if (days < 1) days = 1;
        if (days > 400) days = 400;
        site_advance(s, days, out);
        return true;
    }
    if (strcmp(t[0], "status") == 0) { site_dump_day(s, out); return true; }
    /* WHAT THE WORLD HAS DONE, and what state the kit is in. See the long
     * note at the top of the weather section in core/siteday.c. */
    if (strcmp(t[0], "events") == 0) { site_dump_events(s, out); return true; }
    if (strcmp(t[0], "ups") == 0 && n >= 2) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_printf(out, "no such box: %s\n", t[1]); return true; }
        if (s->dev[d].ups) {
            buf_printf(out, "%s already has a battery under it.\n", s->dev[d].name);
        } else if (!site_ups(s, d)) {
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
        } else {
            buf_printf(out, "a %ld ups goes under %s. It will ride a mains "
                            "failure out\n  instead of coming back with a "
                            "filesystem to check. %ld left.\n",
                       site_ups_price(), s->dev[d].name, s->money);
        }
        return true;
    }
    if (strcmp(t[0], "disk") == 0 && n >= 2) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_printf(out, "no such box: %s\n", t[1]); return true; }
        int was = s->dev[d].wear;
        if (!site_disk(s, d)) {
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
        } else {
            buf_printf(out, "a new disk in %s, %ld, and what was on the old one "
                            "copied across --\n  including anything already "
                            "wrong with it, because that is what a clone does.\n"
                            "  it was %d%% through its life. %ld left.\n",
                       s->dev[d].name, site_disk_price(),
                       was * 100 / 60, s->money);
        }
        return true;
    }
    /* `service ?` AND `load ?`, WHICH IS WHERE THE LEGEND WENT. Both pages
     * printed their whole legend every time; see site_dump_service_legend.
     * `?` rather than a new verb, because it is one character, it is what
     * the short page tells you to type, and it needs no entry of its own in
     * the arity table to be found. `legend` and `help` answer too, for
     * somebody who guesses a word instead. */
    if (strcmp(t[0], "service") == 0) {
        if (n > 1 && (strcmp(t[1], "?") == 0 || strcmp(t[1], "legend") == 0 ||
                      strcmp(t[1], "help") == 0))
            site_dump_service_legend(s, out);
        else site_dump_service(s, out);
        return true;
    }
    if (strcmp(t[0], "load") == 0) {
        if (n > 1 && (strcmp(t[1], "?") == 0 || strcmp(t[1], "legend") == 0 ||
                      strcmp(t[1], "help") == 0))
            site_dump_load_legend(s, out);
        else site_dump_load(s, out);
        return true;
    }
    if (strcmp(t[0], "isp") == 0) {
        if (n < 2) {
            buf_printf(out, "the circuit is %d Mb, %ld a month, and the next "
                            "month is billed in %d day%s.\n"
                            "  `isp <mb>` buys another size.\n",
                       s->isp_mb, site_isp_price(s->isp_mb),
                       site_isp_days_to_bill(s),
                       site_isp_days_to_bill(s) == 1 ? "" : "s");
            return true;
        }
        int mb = atoi(t[1]);
        if (!site_isp(s, mb))
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
        else
            buf_printf(out, "the circuit is %d Mb now, %ld a month, billed in %d "
                            "day%s. %ld left.\n",
                       s->isp_mb, site_isp_price(s->isp_mb),
                       site_isp_days_to_bill(s),
                       site_isp_days_to_bill(s) == 1 ? "" : "s", s->money);
        return true;
    }
    if (strcmp(t[0], "serve") == 0 && n >= 3) {
        /* `serve <tenant> <box> [cable]` -- run copper from a box you own to
         * a tenancy's desks. One cable each, priced by the metre, stopping
         * when the box runs out of holes. */
        int ti = -1;
        int want = atoi(t[1]);
        for (int i = 0; i < s->ntenant; i++)
            if (s->tenant[i].tenant == want) { ti = i; break; }
        int d = dev_arg(s, t[2]);
        if (ti < 0) { buf_printf(out, "no tenancy %s. `service` lists who is in.\n", t[1]); return true; }
        /* AND THE ONE A PLAYER ACTUALLY HITS: a tenancy that is in the
         * diary and not in the building. `serve 3 sw2b cat5e 30` on day 6
         * for a lease that starts on day 11 used to come back "refused: no
         * such device" -- about a line with no missing device in it -- while
         * `serve 99`, a tenancy that will never exist, got a sentence that
         * named the right verb. The good message existed and the near miss
         * did not use it. Answered here rather than in site_serve_vlan()
         * because the day is what the player needs and this is where the
         * calendar is in scope. */
        if (!s->tenant[ti].moved) {
            buf_printf(out, "tenancy %d has not moved in yet: they take the "
                            "lease on day %d and it is\n  day %d. Their desks "
                            "are not in the room to cable to. `demand` says "
                            "who is\n  coming and when; `day` gets there.\n",
                       s->tenant[ti].tenant, s->tenant[ti].day, s->day);
            return true;
        }
        if (d < 0) { buf_printf(out, "no such box: %s\n", t[2]); return true; }
        /* `serve 2 sw cat6 30` and `serve 2 sw 30` both read naturally, so
         * a number where the cable goes is a vlan and not a cable nobody
         * makes. */
        CableKind k = CAB_CAT5E;
        int vlan = 0, ai = 3;
        if (n > ai && !small_number(t[ai], &vlan)) { k = cable_arg(t[ai]); ai++; vlan = 0; }
        if (n > ai && vlan == 0) small_number(t[ai], &vlan);
        int before = site_tenant_connected(s, ti);
        /* THE WARNING GOES ABOVE THE BILL, NOT UNDER IT. A playtester typed
         * `serve 1 sw1` with no vlan and was told, after the copper had been
         * measured and paid for, that this tenancy had asked for a segment
         * of its own. It is printed first now, so the sentence that matters
         * is the one at the top of the reply rather than the one under the
         * price -- and it names the line that fixes it.
         *
         * WHY THIS IS NOT A REFUSAL, which was the other candidate and is
         * the house style for `carry` on a running server: `serve <t> <sw>`
         * with no vlan is what every scripted build in this tree types,
         * including --loadcheck's naive tower, whose whole job is to be the
         * flat build a player really gets on their first afternoon. A verb
         * that refused it would silently unbuild the calibration the game
         * measures itself against. The real cost the playtester paid was
         * not being patched into the default -- it was that fixing it took
         * twenty-one hand-typed lines, and site_serve_vlan no longer skips
         * a desk that is already on this box, so the fix is this same line
         * with the vlan on the end and it lays no new copper. */
        if (vlan == 0 && s->tenant[ti].own_segment && before < s->tenant[ti].ndesk)
            buf_printf(out, "  NOTE: tenancy %d asked for a broadcast domain of "
                            "its own and this line\n  has no vlan in it, so every "
                            "port it patches lands in the untagged\n  default. "
                            "`serve %d %s %d` puts them in a vlan as it patches "
                            "them --\n  and says it again over desks that are "
                            "already on %s, for nothing, if\n  this is the line "
                            "you meant to type.\n",
                       s->tenant[ti].tenant, s->tenant[ti].tenant, s->dev[d].name,
                       30 + s->tenant[ti].tenant, s->dev[d].name);
        int got = site_serve_vlan(s, ti, d, k, vlan);
        if (got < 0) { buf_printf(out, "refused: %s\n", site_err_text(s->err)); return true; }
        buf_printf(out, "tenancy %d: %d of %d desks have a port (%d new). %ld left.\n",
                   s->tenant[ti].tenant, got, s->tenant[ti].ndesk, got - before,
                   s->money);
        if (got < s->tenant[ti].ndesk)
            buf_printf(out, "  %d of them have nowhere to go: %s\n",
                       s->tenant[ti].ndesk - got, site_err_text(s->err));
        /* WHERE THE PORTS ENDED UP, said rather than left to be discovered.
         * `serve` used to patch twenty desks into the untagged default and
         * say nothing about it, and a tenancy the generator marked as wanting
         * a broadcast domain of its own got exactly the opposite -- twenty
         * `vlan` lines later the player found out. */
        if (vlan > 0)
            buf_printf(out, "  and every one of their ports on %s is an access "
                            "port in vlan %d --\n  the ones it has just patched "
                            "and the ones that were already in it. The\n  trunk "
                            "back and the router's subinterface are still "
                            "yours.\n", s->dev[d].name, vlan);
        else if (s->tenant[ti].own_segment)
            buf_printf(out, "  they are in the untagged default vlan, and this "
                            "tenancy asked for a\n  broadcast domain of its own. "
                            "`serve %d %s %d` puts them in a vlan\n  without "
                            "laying another metre; the trunk and the router's "
                            "subinterface\n  are still yours.\n",
                       s->tenant[ti].tenant, s->dev[d].name,
                       30 + s->tenant[ti].tenant);
        else
            buf_printf(out, "  they are in the untagged default vlan. `serve %d "
                            "%s <vlan>` puts them\n  somewhere else as it patches "
                            "them.\n", s->tenant[ti].tenant, s->dev[d].name);
        return true;
    }
    if (strcmp(t[0], "money") == 0) {
        buf_printf(out, "%ld left, %ld spent\n", s->money, s->spent);
        return true;
    }
    if (strcmp(t[0], "credit") == 0 && n > 1) {
        site_credit(s, atol(t[1]));
        buf_printf(out, "%ld left\n", s->money);
        return true;
    }
    if (strcmp(t[0], "frames") == 0) {
        buf_printf(out, "%llu frames have arrived at a machine's card in this "
                        "site\n", (unsigned long long)site_host_frames(s));
        return true;
    }
    /* ORDERED, NOT PLACED. There used to be `install <kind> <room>`, which
     * put a box wherever you named -- eight floors up, through a locked
     * door, without anybody carrying it. That made the building scenery.
     * Kit is delivered to goods in and carried from there, and this is the
     * only way to bring any into the tower. */
    if ((strcmp(t[0], "order") == 0 || strcmp(t[0], "buy") == 0) && n >= 2) {
        int kind = site_kind_by_name(t[1]);
        if (kind < 0) { buf_printf(out, "no such kit: %s\n", t[1]); return true; }
        int d = site_order(s, kind, n > 2 ? t[2] : NULL);
        if (d < 0) { buf_printf(out, "refused: %s\n", site_err_text(s->err)); return true; }
        int goods = site_goods_room(s);
        /* WHAT GRADE IT IS, SAID AT THE MOMENT THE MONEY LEAVES, which is
         * where D27 put the negotiated port speed and for the same reason:
         * the spec is the whole of the decision and a player who has to go
         * and look it up afterwards has already spent it. What each socket
         * clocks, what the disk in it is rated for, and whether a battery
         * came with it -- the three things that differ between the grades,
         * every one of them read off the same KIT[] row the counter charged
         * from. */
        buf_printf(out, "%s: a %s, %d port%s at %d Mb", s->dev[d].name,
                   site_kind_name(kind), s->dev[d].nports,
                   s->dev[d].nports == 1 ? "" : "s",
                   site_kind_port_mb(kind, 0));
        {
            int slow = site_kind_port_mb(kind, 0), fast = 0;
            for (int p = 0; p < s->dev[d].nports; p++)
                if (site_kind_port_mb(kind, p) != slow) fast++;
            if (fast)
                buf_printf(out, " and its top %d at %d Mb", fast,
                           site_kind_port_mb(kind, s->dev[d].nports - 1));
        }
        if (site_kind_disk_days(kind))
            buf_printf(out, ", a disk rated for %d days",
                       site_kind_disk_days(kind));
        if (site_kind_has_ups(kind))
            buf_puts(out, " and a battery in it -- it rides a mains failure "
                          "out\n  without a `ups`");
        buf_printf(out, ". %d paid, %ld left.\n",
                   site_kind_price(kind), s->money);
        buf_printf(out, "it is in %s #%d, on the ground floor. Somebody has to "
                        "carry it: `move %s <room>`\n",
                   bld_kind_name(s->b->rooms[goods].kind), goods, s->dev[d].name);
        return true;
    }
    if (strcmp(t[0], "move") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        int room = site_room_by_name(s, t[2]);
        if (d < 0) { buf_printf(out, "no such box: %s\n", t[1]); return true; }
        if (room < 0) { buf_printf(out, "no such room: %s\n", t[2]); return true; }
        /* AND SAY SO WHEN THE NAME WAS A CHOICE. `help` offers `f2.office`
         * as a room name and floor two has twelve of them belonging to
         * three tenants; `move pc1 f2.office` put the box in tenant 2's
         * room and printed nothing about it. The shorthand stays -- it is
         * how the whole tower gets built without a floor plan -- but it
         * owes the player the fact that there was a choice, ABOVE the
         * sentence about the walk, so the correction is the next line
         * rather than a discovery six days later. */
        site_room_ambiguity(s, t[2], room, out);
        int was = s->dev[d].room;
        if (!site_move(s, d, room)) {
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
            if (s->err == SITE_ECABLED)
                buf_puts(out, "  `links` says which cable, `uncable <n>` pulls "
                              "it out. The copper is\n  bought and paid for and "
                              "you will be buying it again.\n");
            if (s->err == SITE_EJACK)
                buf_puts(out, "  a jack is punched down into it and the far end "
                              "of that run is on a wall\n  in another room. "
                              "`jacks` says which. A jack does not move.\n");
            return true;
        }
        /* The metres a PERSON walks carrying it, which is not the metres a
         * cable would take between the same two rooms -- see building.h. */
        int m = -1;
        if (was >= 0 && was < s->b->nrooms) {
            double *dm = nom_alloc(sizeof(double) * (size_t)s->b->nrooms);
            if (bld_walk_all(s->b, was, dm) && dm[room] < BLD_INF)
                m = (int)(dm[room] + 0.5);
            nom_free(dm);
        }
        buf_printf(out, "%s is in %s #%d now", s->dev[d].name,
                   bld_kind_name(s->b->rooms[room].kind), room);
        /* Zero metres is somebody putting a box down in the room they are
         * already standing in, which is what a carry looks like when the
         * view calls this on every threshold. Do not print a distance
         * nobody walked. */
        if (m > 0) buf_printf(out, ", carried %d m", m);
        buf_puts(out, "\n");
        return true;
    }
    if (strcmp(t[0], "cable") == 0 && n >= 3) {
        int a, ap, b, bp;
        if (!port_arg(s, t[1], &a, &ap) || !port_arg(s, t[2], &b, &bp)) {
            buf_puts(out, "cable <dev>:<port> <dev>:<port>\n");
            return true;
        }
        CableKind k = n > 3 ? cable_arg(t[3]) : CAB_CAT6;
        int l = site_cable(s, a, ap, b, bp, k);
        if (l < 0) buf_printf(out, "refused: %s\n", site_err_text(s->err));
        else buf_printf(out, "link %d: %d m of %s, %d, %s\n", l, s->link[l].metres,
                        site_cable_name(k), s->link[l].cost,
                        pstate(site_link_state(s, l)));
        return true;
    }
    /* A QUOTE COSTS NOTHING, and the reason is in docs/decisions-d32.md: a
     * fee on asking is a tax on carefulness, and every number it prints is
     * one the game already prints for free at the moment the money leaves.
     * What it does not do is walk you anywhere -- it is a plan on a
     * clipboard, and the legs are the same legs whichever end you start at. */
    if (strcmp(t[0], "quote") == 0 && n >= 3) {
        int ra, rb, da = -1, db = -1, pa = -1, pb = -1;
        if (!quote_end_arg(s, t[1], &ra, &da, &pa)) {
            buf_printf(out, "no box or room called %s. An end of a quote is a box "
                            "(`core`, `core:2`)\n  or a room (`#41`, `f3.comms`).\n",
                       t[1]);
            return true;
        }
        if (!quote_end_arg(s, t[2], &rb, &db, &pb)) {
            buf_printf(out, "no box or room called %s. An end of a quote is a box "
                            "(`core`, `core:2`)\n  or a room (`#41`, `f3.comms`).\n",
                       t[2]);
            return true;
        }
        site_dump_quote(s, ra, rb, da, pa, db, pb, out);
        return true;
    }
    if (strcmp(t[0], "uncable") == 0 && n >= 2) {
        int l = atoi(t[1]);
        int j = (l >= 0 && l < s->nlink) ? s->link[l].jack : -1;
        site_uncable(s, l);
        if (j >= 0)
            buf_printf(out, "the lead comes out of j%d. The jack is still in the "
                            "wall and the panel port\n  is still its own: `patch "
                            "<box>:<port> j%d` puts the next box in it for %d.\n",
                       j, j, JACK_LEAD);
        else buf_puts(out, "pulled out\n");
        return true;
    }
    /* ---------------------------------------------------------- the jack */
    if (strcmp(t[0], "jack") == 0 && n >= 3) {
        int room = site_room_by_name(s, t[1]);
        if (room < 0) { buf_printf(out, "no such room: %s\n", t[1]); return true; }
        int home, hport;
        if (!port_arg(s, t[2], &home, &hport)) {
            buf_puts(out, "jack <room> <box>:<port> [cat5e|cat6|fibre|cat5]\n");
            return true;
        }
        CableKind k = n > 3 ? cable_arg(t[3]) : CAB_CAT5E;
        int j = site_jack(s, room, home, hport, k);
        if (j < 0) { buf_printf(out, "refused: %s\n", site_err_text(s->err)); return true; }
        const SiteJack *jk = &s->jack[j];
        buf_printf(out, "j%d: a jack on the wall of %s #%d, %d m of %s back to "
                        "%s:%d, %d paid, %ld left.\n",
                   j, bld_kind_name(s->b->rooms[room].kind), room, jk->metres,
                   site_cable_name((CableKind)jk->kind), s->dev[home].name, hport,
                   jk->cost, s->money);
        /* THE OTHER PRICE, SAID AT THE MOMENT THE MONEY LEAVES. The same
         * metres off the spool are on the screen next to what you just paid,
         * because a trade-off nobody can see the other half of is not a
         * decision -- it is a tax. */
        buf_printf(out, "  the same run off the spool is %d. You have paid %d "
                        "more for copper that\n  stays when the box goes, and a "
                        "lead into it after that is %d.\n",
                   site_cable_price(k, jk->metres),
                   jk->cost - site_cable_price(k, jk->metres), JACK_LEAD);
        buf_printf(out, "  the trade comes on day %d, in %d day%s. Nothing plugs "
                        "into it before then.\n", jk->ready, jk->ready - s->day,
                   jk->ready - s->day == 1 ? "" : "s");
        buf_printf(out, "  %s:%d is not a free port any more, and %s does not "
                        "move again while a run\n  is punched down into it.\n",
                   s->dev[home].name, hport, s->dev[home].name);
        return true;
    }
    if (strcmp(t[0], "patch") == 0 && n >= 3) {
        int dev, port;
        if (!port_arg(s, t[1], &dev, &port)) {
            buf_puts(out, "patch <box>:<port> j<n>\n");
            return true;
        }
        const char *ja = t[2];
        if (*ja == 'j') ja++;
        int j = atoi(ja);
        if (j < 0 || j >= s->njack) {
            buf_printf(out, "no such jack: %s. `jacks` lists them.\n", t[2]);
            return true;
        }
        int l = site_patch(s, j, dev, port);
        if (l < 0) {
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
            if (s->err == SITE_EEARLY)
                buf_printf(out, "  j%d is a socket on day %d. It is day %d. Copper "
                                "off the spool is in your\n  hands now, and that is "
                                "what it is for.\n", j, s->jack[j].ready, s->day);
            if (s->err == SITE_ENOROOM)
                buf_printf(out, "  j%d is on the wall of #%d and %s is in #%d. A "
                                "jack is a fixed point in\n  one room: carry the "
                                "box to it, or run copper off the spool.\n",
                           j, s->jack[j].room, s->dev[dev].name, s->dev[dev].room);
            return true;
        }
        const SiteLink *lk = &s->link[l];
        buf_printf(out, "link %d: %s:%d to %s:%d through j%d, %d m of %s already "
                        "in the wall,\n  %d paid for the lead, %s. %ld left.\n",
                   l, s->dev[lk->a].name, lk->aport, s->dev[lk->b].name, lk->bport,
                   j, lk->metres, site_cable_name((CableKind)lk->kind), lk->cost,
                   pstate(site_link_state(s, l)), s->money);
        return true;
    }
    if (strcmp(t[0], "jacks") == 0) { site_dump_jacks(s, BLD_NOROOM, out); return true; }
    if (strcmp(t[0], "addr") == 0 && n >= 3) {
        /* `addr edge 198.51.100.2/30` is the first card, and `addr edge:1
         * 10.0.1.1/24` is the second one. The colon is the spelling `cable`
         * and `plug` already use for a socket on the back of a box, so a
         * router's WAN side and LAN side are addressed the same way they are
         * cabled. */
        int d = -1, ifx = 0;
        if (!port_arg(s, t[1], &d, &ifx)) { d = dev_arg(s, t[1]); ifx = 0; }
        uint32_t ip, mask;
        if (d < 0 || !parse_cidr(t[2], &ip, &mask)) { buf_puts(out, "?\n"); return true; }
        if (site_addr(s, d, ifx, ip, mask)) { buf_puts(out, "set\n"); return true; }
        buf_printf(out, "%s\n", site_err_text(s->err));
        if (s->err == SITE_EIFACE && ifx >= s->dev[d].nports)
            buf_printf(out, "  %s has %d socket%s, numbered 0 to %d. A tagged\n"
                            "  subinterface is `subif %s <nic> <vlan> <ip>/<bits>`.\n",
                       s->dev[d].name, s->dev[d].nports,
                       s->dev[d].nports == 1 ? "" : "s", s->dev[d].nports - 1,
                       s->dev[d].name);
        if (s->err == SITE_EADDR) {
            char a[20], b[20];
            net_fmt_ip((ip & mask) + 1, a, sizeof a);
            net_fmt_ip((ip | ~mask) - 1, b, sizeof b);
            buf_printf(out, "  a /%d runs from %s to %s: %d machine%s.\n",
                       net_mask_len(mask), a, b, site_hosts_in_mask(mask),
                       site_hosts_in_mask(mask) == 1 ? "" : "s");
        }
        return true;
    }
    if (strcmp(t[0], "power") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        bool on = strcmp(t[2], "on") == 0;
        if (!site_power(s, d, on)) {
            buf_printf(out, "%s\n", site_err_text(s->err));
            /* AND THE MOVE, from where the box is really standing. `power`
             * and `mains` are two verbs and the refusal from one used to
             * name neither: a pc in goods in, which has two free sockets,
             * was told there was no free outlet on that room's wall. */
            if (s->err == SITE_EUNPLUGGED) {
                int free = site_room_outlets_free(s, s->dev[d].room);
                if (free > 0)
                    buf_printf(out, "  #%d has %d socket%s free: `mains %s on`, "
                                    "then `power %s on`.\n",
                               s->dev[d].room, free, free == 1 ? "" : "s",
                               s->dev[d].name, s->dev[d].name);
                else
                    buf_printf(out, "  #%d has no socket free: `outlet %s` puts "
                                    "one in, or carry it\n  somewhere that has "
                                    "one. `outlets` says which rooms do.\n",
                               s->dev[d].room, t[1]);
            }
            return true;
        }
        buf_printf(out, "%s is %s\n", s->dev[d].name, on ? "on" : "off");
        return true;
    }
    /* ------------------------------------------------------------- power */
    if (strcmp(t[0], "outlets") == 0) {
        int f = n >= 2 ? atoi(t[1]) : -1;
        site_dump_outlets(s, f, out);
        return true;
    }
    if (strcmp(t[0], "outlet") == 0 && n >= 2) {
        int r = site_room_by_name(s, t[1]);
        if (r < 0) { buf_printf(out, "no such room: %s\n", t[1]); return true; }
        long price = site_outlet_price(s, r);
        int have = site_room_outlets(s, r);
        if (site_outlet(s, r) < 0) {
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
            if (s->err == SITE_ECIRCUIT)
                buf_printf(out, "  #%d has %d, which is every socket its final "
                                "circuit will carry.\n  The way to power another "
                                "box is another room.\n", r, have);
            else if (s->err == SITE_EMONEY)
                buf_printf(out, "  a socket in #%d is %ld and you have %ld.\n",
                           r, price, s->money);
            return true;
        }
        buf_printf(out, "a sparky runs a spur off the board: #%d has %d outlet%s "
                        "now, %d free.\n  %ld paid, %ld left. It does not come "
                        "out again and nothing is refunded.\n",
                   r, site_room_outlets(s, r),
                   site_room_outlets(s, r) == 1 ? "" : "s",
                   site_room_outlets_free(s, r), price, s->money);
        return true;
    }
    if (strcmp(t[0], "mains") == 0 && n >= 2) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_printf(out, "no such box: %s\n", t[1]); return true; }
        SiteDev *dv = &s->dev[d];
        /* A TENANT'S DESK IS ON THEIR OWN SOCKET IN THEIR OWN ROOM, which is
         * the same sentence the heat model has always made about a desk, and
         * "no such device" would have been a lie about a box the player can
         * see. */
        if (dv->kind == SDEV_DESK) {
            buf_printf(out, "refused: %s is a tenant's own computer on a socket "
                            "in their own office.\n  The outlets you pay for are "
                            "the landlord's, and their desk is not on one.\n",
                       dv->name);
            return true;
        }
        if (n < 3) {
            buf_printf(out, "%s is %s. `mains %s %s`?\n", dv->name,
                       dv->mains ? "plugged into the wall" : "not plugged in",
                       dv->name, dv->mains ? "off" : "on");
            return true;
        }
        bool on = strcmp(t[2], "on") == 0;
        bool was_running = site_kind_has_os(dv->kind) && dv->powered;
        if (!site_mains(s, d, on)) {
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
            if (s->err == SITE_ENOMAINS)
                buf_printf(out, "  %s is standing in a room with %d outlet%s and "
                                "all of them are in use.\n  `outlets` says which "
                                "rooms have one free; `outlet <room>` buys one.\n",
                           dv->name, site_room_outlets(s, dv->room),
                           site_room_outlets(s, dv->room) == 1 ? "" : "s");
            return true;
        }
        if (on) {
            buf_printf(out, "%s is in a wall socket. %d of #%d's %d left.\n",
                       dv->name, site_room_outlets_free(s, dv->room), dv->room,
                       site_room_outlets(s, dv->room));
            if (site_kind_has_os(dv->kind))
                buf_printf(out, "  it is still switched off. `power %s on`.\n",
                           dv->name);
            return true;
        }
        buf_printf(out, "the plug comes out of %s.\n", dv->name);
        if (was_running)
            buf_printf(out, "  it was running. %s\n",
                       dv->ups ? "The battery held the load and shut it down "
                                 "cleanly -- `events`."
                               : "That is a blackout with one machine in it: "
                                 "it went down\n  unclean and there is a "
                                 "filesystem to check. `events`.");
        return true;
    }
    if (strcmp(t[0], "gw") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        uint32_t ip;
        if (d < 0 || !net_parse_ip(t[2], &ip)) { buf_puts(out, "?\n"); return true; }
        buf_printf(out, "%s\n", site_gateway(s, d, ip) ? "set" : site_err_text(s->err));
        return true;
    }
    if (strcmp(t[0], "router") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        buf_printf(out, "%s\n", site_forwarding(s, d, strcmp(t[2], "on") == 0)
                   ? "set" : site_err_text(s->err));
        return true;
    }
    if (strcmp(t[0], "subif") == 0 && n >= 5) {
        /* `subif <box> <nic> <vlan> <ip>/<bits>`. There used to be an
         * interface index in front of the nic, and both of them went through
         * atoi, so `subif edge wan eth0 100 198.51.100.2/30` -- which reads
         * perfectly -- meant interface 0 on card 0 and silently replaced the
         * box's only address. Numbers only, and they are checked. */
        int d = dev_arg(s, t[1]);
        uint32_t ip, mask;
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        int nic, vlan;
        if (!small_number(t[2], &nic) || !small_number(t[3], &vlan)) {
            buf_printf(out, "subif <box> <nic> <vlan> <ip>/<bits>\n"
                            "  the nic is a socket number and the vlan is a tag: "
                            "`subif %s 0 30 10.0.30.1/24`\n", s->dev[d].name);
            return true;
        }
        /* AND A WAY TO TAKE ONE AWAY. There was one -- an address of zero
         * deletes the subinterface -- and no word in the game spelled it, so
         * a playtester with stale vlans on a floor server could only park
         * them on an unused subnet to stop `dhcpd` binding to them. `subif
         * <box> <nic> <vlan> off` is that line, and it says what went. */
        if (strcmp(t[4], "off") == 0 || strcmp(t[4], "none") == 0 ||
            strcmp(t[4], "delete") == 0) {
            if (!site_subif(s, d, nic, vlan, 0, 0)) {
                buf_printf(out, "%s\n", site_err_text(s->err));
                return true;
            }
            buf_printf(out, "eth%d.%d is gone from %s -- the subinterface, its "
                            "address and any pool\n  that was answering on it. "
                            "The card underneath it is untouched.\n",
                       nic, vlan, s->dev[d].name);
            return true;
        }
        if (!parse_cidr(t[4], &ip, &mask)) { buf_puts(out, "?\n"); return true; }
        buf_printf(out, "%s\n", site_subif(s, d, nic, vlan, ip, mask)
                   ? "set" : site_err_text(s->err));
        return true;
    }
    if (strcmp(t[0], "vlan") == 0 && n >= 4) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        buf_printf(out, "%s\n", site_port_vlan(s, d, atoi(t[2]), atoi(t[3]))
                   ? "set" : site_err_text(s->err));
        return true;
    }
    /* THE TRUNK LINE, AND EVERY WAY IT USED TO LIE.
     *
     *   trunk <sw> <port> 11 12 13      add these to what it carries
     *   trunk <sw> <port> -13           take that one back off
     *   trunk <sw> <port> none          take them all off
     *
     * `-13` is the spelling because it is the same word with a minus in
     * front of it: a player correcting a typo edits the line they already
     * typed instead of learning a second verb, and add and remove sit side
     * by side in one line so the whole edit is one operation. A new verb
     * (`untrunk`) would need its own arity, its own usage line and its own
     * help entry, and would still not compose with the list.
     *
     * NOTHING IS APPLIED UNTIL EVERY WORD PARSES. A line with a typo in the
     * middle used to set the vlans before it and refuse afterwards, so the
     * switch was left in a state the player had not asked for and the error
     * did not describe. And the answer is no longer the word "set": it is
     * the allowed list read back off the port, which is the only way to see
     * that the line did what it said. */
    if (strcmp(t[0], "trunk") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        int port;
        if (!small_number(t[2], &port)) {
            buf_printf(out, "%s is not a port number\n", t[2]);
            return true;
        }
        /* Pass one: parse. `+v` and a bare number add, `-v` removes, `none`
         * empties the set. */
        int op[MAXTOK], vl[MAXTOK], nops = 0;
        for (int i = 3; i < n; i++) {
            const char *a = t[i];
            int sign = 1, v = 0;
            if (strcmp(a, "none") == 0 || strcmp(a, "clear") == 0) {
                op[nops] = -1; vl[nops++] = 0; continue;
            }
            if (*a == '-') { sign = -1; a++; }
            else if (*a == '+') a++;
            if (!small_number(a, &v) || v < 1 || v > VLAN_ID_MAX) {
                buf_printf(out, "%s: %s\n  Nothing was done -- the whole line "
                                "is refused, so the port is as it was.\n",
                           t[i], site_err_text(SITE_EVLAN));
                return true;
            }
            op[nops] = sign; vl[nops++] = v;
        }
        /* Pass two: do it. Only the port itself can fail now. */
        if (!site_port_trunk(s, d, port, 0)) {
            buf_printf(out, "%s\n", site_err_text(s->err));
            return true;
        }
        for (int i = 0; i < nops; i++) {
            if (op[i] > 0) site_port_trunk(s, d, port, vl[i]);
            else           site_port_trunk_off(s, d, port, vl[i]);
        }
        /* READ IT BACK, off the port, every time. */
        buf_printf(out, "%s:%d trunk ", s->dev[d].name, port);
        net_dump_trunk(s->net, s->dev[d].node, port, out);
        buf_putc(out, '\n');
        return true;
    }
    if (strcmp(t[0], "dhcpd") == 0) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_printf(out, "no such box: %s\n", t[1]); return true; }
        /* `dhcpd <box>` says what it is serving. A service you cannot ask
         * about is a service you cannot find, and this is the line the
         * player types when the addresses on a floor are wrong. */
        if (n == 2) {
            site_dump_dhcpd(s, d, out);
            buf_printf(out, "  dhcpd <box> <first> <count> <bits> <gw> <dns> "
                            "starts one, `dhcpd <box> off` stops\n  every pool "
                            "on it. %d of the 8 pools this box holds are in "
                            "use.\n",
                       net_dhcpd_pools(s->net, s->dev[d].node));
            return true;
        }
        if (strcmp(t[2], "off") == 0 || strcmp(t[2], "stop") == 0) {
            int k = site_dhcpd_stop(s, d);
            if (k < 0) { buf_printf(out, "%s\n", site_err_text(s->err)); return true; }
            if (!k) buf_printf(out, "%s was not serving any addresses.\n",
                               s->dev[d].name);
            else buf_printf(out, "%s stops serving addresses: %d pool%s gone, "
                                 "and the leases with\n  them. Anything holding "
                                 "one keeps it until it asks again.\n",
                            s->dev[d].name, k, k == 1 ? "" : "s");
            return true;
        }
        if (n < 7) {
            buf_puts(out, "dhcpd <box> <first> <count> <bits> <gw> <dns>   "
                          "start a pool\n"
                          "dhcpd <box> off                                "
                          "stop them all\n"
                          "dhcpd <box>                                    "
                          "what it serves\n");
            return true;
        }
        uint32_t first, gw, dns;
        if (!net_parse_ip(t[2], &first) || !net_parse_ip(t[5], &gw) ||
            !net_parse_ip(t[6], &dns)) { buf_puts(out, "?\n"); return true; }
        if (!site_dhcpd(s, d, first, atoi(t[3]), net_mask_bits(atoi(t[4])),
                        gw, dns)) {
            buf_printf(out, "%s\n", site_err_text(s->err));
            if (s->err == SITE_ESEG) {
                Buf ifs = {0};
                net_dump_ifaces(s->net, s->dev[d].node, &ifs);
                buf_printf(out, "  %s has:\n", s->dev[d].name);
                if (ifs.len) buf_put(out, ifs.p, ifs.len);
                buf_puts(out, "  Give it an address on that subnet first -- "
                              "`addr` for a socket, `subif` for\n  a vlan -- and "
                              "the pool goes on that interface and serves only "
                              "it.\n");
                buf_free(&ifs);
            }
            return true;
        }
        site_dump_dhcpd(s, d, out);
        /* WHEN THE CHOICE WAS AMBIGUOUS, SAY SO. The pool lands on the
         * interface whose address is inside it, and when two of this box's
         * interfaces are in the same subnet the first one wins silently. It
         * prints which one it chose, which is what saved a playtester -- but
         * a player who is not told the choice was ambiguous has no reason to
         * read that line at all. */
        {
            int node = s->dev[d].node, hit = 0;
            char names[96] = "";
            for (int i = 0; i < NET_IF_MAX; i++) {
                uint32_t a = net_if_get_addr(s->net, node, i);
                if (!a) continue;
                uint32_t mk = net_mask_bits(atoi(t[4]));
                if ((a & mk) != (first & mk)) continue;
                char nm[24];
                net_if_name(s->net, node, i, nm, sizeof nm);
                if (hit++) strncat(names, " and ", sizeof names - strlen(names) - 1);
                strncat(names, nm, sizeof names - strlen(names) - 1);
            }
            if (hit > 1)
                buf_printf(out, "  AMBIGUOUS: %d of %s's interfaces are in that "
                                "subnet -- %s.\n  It took the first. Two legs on one "
                                "segment is usually a mistake somewhere\n  else, and "
                                "`subif %s <nic> <vlan> off` takes one away.\n",
                           hit, s->dev[d].name, names, s->dev[d].name);
        }
        return true;
    }
    if (strcmp(t[0], "dhcp") == 0 && n >= 2) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        /* A DISCOVER IS A BROADCAST, so there is no one box to watch. Watch
         * every box that is running a pool: a server whose filter is eating
         * udp/67 is silent in exactly the way an empty pool is. */
        int srv[16], nsrv = 0;
        uint64_t was[16];
        for (int i = 0; i < s->ndev && nsrv < 16; i++)
            if (i != d && net_dhcpd_pools(s->net, s->dev[i].node) > 0) {
                srv[nsrv] = i;
                was[nsrv] = fw_drops_of(s, i);
                nsrv++;
            }
        bool got = site_dhcp(s, d);
        char ip[20];
        net_fmt_ip(net_if_get_addr(s->net, s->dev[d].node, 0), ip, sizeof ip);
        buf_printf(out, "%s\n", got ? ip
                   : "no lease: nothing answered, or the pool is empty");
        if (!got)
            for (int i = 0; i < nsrv; i++) fw_blame(s, srv[i], was[i], "discover", out);
        return true;
    }
    /* THE SERVICES A BOX RUNS. There was no verb for either of these, so a
     * server the player bought could hold files for nobody and a tower could
     * not resolve a name without asking the ISP. */
    if (strcmp(t[0], "httpd") == 0 && n >= 2) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        buf_printf(out, "%s\n", site_httpd(s, d, n > 2 ? atoi(t[2]) : 80)
                   ? "serving" : site_err_text(s->err));
        return true;
    }
    if (strcmp(t[0], "dnsd") == 0 && n >= 2) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        if (!site_dnsd(s, d)) { buf_printf(out, "%s\n", site_err_text(s->err)); return true; }
        site_dump_dnsd(s, d, out);
        return true;
    }
    /* A NAME OF YOUR OWN, ON A NAME SERVER OF YOUR OWN. */
    if (strcmp(t[0], "dns") == 0 && n >= 4) {
        int d = dev_arg(s, t[1]);
        uint32_t ip;
        if (d < 0 || !net_parse_ip(t[3], &ip)) { buf_puts(out, "?\n"); return true; }
        if (!site_dns(s, d, t[2], ip)) {
            buf_printf(out, "%s\n", site_err_text(s->err));
            return true;
        }
        char a[20];
        net_fmt_ip(ip, a, sizeof a);
        buf_printf(out, "%s -> %s\n", t[2], a);
        if (!net_dnsd_running(s->net, s->dev[d].node))
            buf_printf(out, "  %s is holding that name and is NOT answering "
                            "queries: `dnsd %s` starts\n  the server. Until it "
                            "does, nothing can look it up.\n",
                       s->dev[d].name, s->dev[d].name);
        else
            site_dump_dnsd(s, d, out);
        return true;
    }
    if (strcmp(t[0], "resolver") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        uint32_t ip;
        if (d < 0 || !net_parse_ip(t[2], &ip)) { buf_puts(out, "?\n"); return true; }
        buf_printf(out, "%s\n", site_resolver(s, d, ip) ? "set" : site_err_text(s->err));
        return true;
    }
    if (strcmp(t[0], "ping") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        uint32_t ip;
        if (d < 0 || !net_parse_ip(t[2], &ip)) { buf_puts(out, "?\n"); return true; }
        int rtt = 0;
        int far = dev_by_ip(s, ip);
        uint64_t was = fw_drops_of(s, far);
        PingResult r = net_ping(s->net, s->dev[d].node, ip, &rtt);
        buf_printf(out, "%s", net_ping_text(r));
        if (r == PING_OK) buf_printf(out, " in %d ms", rtt);
        buf_putc(out, '\n');
        if (r != PING_OK && far != d) fw_blame(s, far, was, "ping", out);
        return true;
    }
    if (strcmp(t[0], "trace") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        uint32_t ip, hops[8];
        if (d < 0 || !net_parse_ip(t[2], &ip)) { buf_puts(out, "?\n"); return true; }
        int nh = net_traceroute(s->net, s->dev[d].node, ip, hops, 8);
        for (int i = 0; i < nh; i++) {
            char h[20];
            net_fmt_ip(hops[i], h, sizeof h);
            buf_printf(out, "  %d  %s\n", i + 1, hops[i] ? h : "*");
        }
        if (!nh) buf_puts(out, "  nothing came back at all\n");
        return true;
    }
    if (strcmp(t[0], "resolve") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        uint32_t a = 0;
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        /* `no answer` FOR BOTH OF THESE WAS A LIE OF OMISSION. A server that
         * is not there and a server that is there and has never heard of the
         * name look identical from here and have nothing in common: one is a
         * network fault, the other is a missing record. netstack has known
         * the difference all along -- rcode 3 -- and the tower threw it
         * away. */
        uint32_t ns = net_get_resolver(s->net, s->dev[d].node);
        int nsd = dev_by_ip(s, ns);
        uint64_t was = fw_drops_of(s, nsd);
        char h[20];
        net_fmt_ip(ns, h, sizeof h);
        switch (net_resolve_ex(s->net, s->dev[d].node, t[2], &a)) {
        case RESOLVE_OK: {
            char b[20];
            net_fmt_ip(a, b, sizeof b);
            buf_printf(out, "%s\n", b);
            break;
        }
        case RESOLVE_NXDOMAIN:
            buf_printf(out, "no such name: %s answered, and there is no record "
                            "for %s.\n  The server is up and it is reachable. "
                            "This is not a network fault -- the name\n  does "
                            "not exist as far as that server can tell.\n",
                       h, t[2]);
            break;
        case RESOLVE_NODATA:
            buf_printf(out, "no address: %s answered about %s and gave no A "
                            "record.\n", h, t[2]);
            break;
        case RESOLVE_NO_RESOLVER:
            buf_printf(out, "no resolver on %s: it has nothing to ask. "
                            "`resolver %s <ip>` sets one.\n",
                       s->dev[d].name, s->dev[d].name);
            break;
        case RESOLVE_TIMEOUT:
            buf_printf(out, "no answer from %s: nothing came back before the "
                            "query timed out.\n  A name server that is not "
                            "there sounds like this, and so does one that is\n"
                            "  there and cannot reach its own forwarder. "
                            "`ping %s %s` separates them.\n",
                       h, s->dev[d].name, h);
            fw_blame(s, nsd, was, "query", out);
            break;
        }
        return true;
    }
    if (strcmp(t[0], "get") == 0 && n >= 4) {
        int d = dev_arg(s, t[1]);
        uint32_t ip;
        if (d < 0 || !net_parse_ip(t[2], &ip)) { buf_puts(out, "?\n"); return true; }
        Buf page = {0};
        int far = dev_by_ip(s, ip);
        uint64_t was = fw_drops_of(s, far);
        int st = net_http_get(s->net, s->dev[d].node, ip, 80, t[3], &page);
        if (st >= 100) buf_printf(out, "HTTP %d, %u bytes\n", st, (unsigned)page.len);
        else {
            /* `HTTP -1, 0 bytes` IS NOT A STATUS. There was no reply, and the
             * interesting question is how far it got -- so ask the wire the
             * same way a person would, with a ping, and SAY WHAT CAME BACK.
             *
             * What it must not do is name a fault it has not established. It
             * used to answer a failed ping with "this is a routing or
             * addressing fault, not a web one" -- and a playtester read that
             * over a network whose routing and addressing were provably
             * correct, because the far box was simply filtering icmp. A
             * confident sentence that contradicts the machine costs more
             * than no sentence at all: it sends somebody to re-cable a riser
             * that was never wrong. Two observations and the next question,
             * and the player draws the conclusion. */
            char a[20];
            net_fmt_ip(ip, a, sizeof a);
            int rtt = 0;
            PingResult pr = net_ping(s->net, s->dev[d].node, ip, &rtt);
            buf_printf(out, "no reply from %s port 80\n", a);
            if (pr != PING_OK)
                buf_printf(out, "  and a ping to %s came back: %s.\n"
                                "  So nothing has been shown to reach it -- but a "
                                "box that filters icmp\n  looks exactly like this "
                                "too. `trace %s %s` says how far the packets\n"
                                "  get, and `show` the far box says whether it has "
                                "an address at all.\n",
                           a, net_ping_text(pr), t[1], a);
            else
                buf_printf(out, "  %s answers a ping in %d ms, so the copper and "
                                "the routing are fine.\n  Nothing accepted the "
                                "connection: either no service is listening on\n"
                                "  port 80, or a filter is dropping it. `show` the "
                                "far box.\n", a, rtt);
            if (far != d) fw_blame(s, far, was, "fetch", out);
        }
        buf_free(&page);
        return true;
    }
    buf_printf(out, "no such command: %s (try help)\n", t[0]);
    return false;
}
