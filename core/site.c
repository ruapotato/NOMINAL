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
static const struct { const char *name; int ports; int price; } KIT[SDEV_KIND_COUNT] = {
    { "uplink",   1,    0 },   /* the ISP's socket. Not for sale.          */
    { "switch8",  8,  120 },
    { "switch24", 24, 400 },
    { "router",   4,  650 },   /* four sockets; as many vlans as you like  */
    { "pc",       1,  480 },
    { "server",   2, 1350 },
};

const char *site_kind_name(int kind)
{
    return (kind >= 0 && kind < SDEV_KIND_COUNT) ? KIT[kind].name : "?";
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
    return kind == SDEV_SWITCH8 || kind == SDEV_SWITCH24;
}
bool site_kind_has_os(int kind)
{
    return kind == SDEV_PC || kind == SDEV_SERVER;
}
int site_kind_by_name(const char *name)
{
    for (int i = 0; i < SDEV_KIND_COUNT; i++)
        if (strcmp(KIT[i].name, name) == 0) return i;
    return -1;
}

/* Cable is priced by the metre, which is the whole reason the two distances
 * in building.h are different numbers: the player is choosing between
 * carrying the box further and paying for more copper. */
static const struct { const char *name; int per_100m; int ends; } SPOOL[CAB_KIND_COUNT] = {
    { "cat5e",  38, 12 },
    { "cat6",   66, 16 },
    { "fibre", 210, 90 },
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

bool site_new(Site *s, const Building *b, uint64_t seed, long budget)
{
    memset(s, 0, sizeof *s);
    s->b = b;
    s->seed = seed;
    s->uplink = -1;
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

    /* --------------------------------------------------- who is moving in */
    /* One tenancy per Room.tenant, each with an arrival day and a set of
     * requirements drawn from this seed and no other. The building decided
     * who holds which room; this decides what they ask for once they have
     * the keys, and their arithmetic is the entire difficulty curve. */
    Rng r;
    rng_seed(&r, seed ^ 0xde4a5dull);
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
        t->rent = (int)(area * (rm->kind == RM_RESIDENCE ? 14 : 26));
        t->day = rng_range(&r, 1, 40) + rm->floor * 12;
    }
    /* In the order they arrive. An insertion sort, because it is stable and
     * because a schedule that depended on qsort's tie-breaking would stop
     * reproducing from a seed. */
    for (int i = 1; i < s->ntenant; i++) {
        SiteTenant key = s->tenant[i];
        int j = i - 1;
        while (j >= 0 && s->tenant[j].day > key.day) {
            s->tenant[j + 1] = s->tenant[j];
            j--;
        }
        s->tenant[j + 1] = key;
    }
    return true;
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

/* ---------------------------------------------------------- installation */
int site_install(Site *s, int kind, int room, const char *name)
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
     * comes up with the socket. */
    d->powered = site_kind_has_os(kind) ? 0 : 1;
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
    s->money -= site_kind_price(kind);
    s->spent += site_kind_price(kind);
    return s->ndev++;
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
    return site_install(s, kind, goods, name);
}

bool site_dev_cabled(const Site *s, int dev)
{
    if (dev < 0 || dev >= s->ndev) return false;
    for (int p = 0; p < s->dev[dev].nports; p++)
        if (net_port_state(s->net, s->dev[dev].node, p) != PORT_NOCABLE) return true;
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
    s->dev[dev].room = (uint16_t)room;
    s->dev[dev].floor = s->b->rooms[room].floor;
    s->dev[dev].tenant = s->b->rooms[room].tenant;
    return true;
}

int site_free_port(const Site *s, int dev)
{
    if (dev < 0 || dev >= s->ndev) return -1;
    for (int p = 0; p < s->dev[dev].nports; p++)
        if (net_port_state(s->net, s->dev[dev].node, p) == PORT_NOCABLE) return p;
    return -1;
}

/* ----------------------------------------------------------------- cable */
int site_cable(Site *s, int a, int aport, int b, int bport, CableKind k)
{
    s->err = SITE_OK;
    if (a < 0 || a >= s->ndev || b < 0 || b >= s->ndev) { s->err = SITE_ENODEV; return -1; }
    /* THE PORT YOU HAVE NOT GOT. The first limit a growing floor meets, and
     * it is not a rule about difficulty: an eight-port switch has eight
     * holes in it. */
    if (aport < 0 || aport >= s->dev[a].nports ||
        bport < 0 || bport >= s->dev[b].nports) { s->err = SITE_ENOPORT; return -1; }
    if (s->nlink >= SITE_MAX_LINK) { s->err = SITE_ESPACE; return -1; }

    int ra = s->dev[a].room, rb = s->dev[b].room;
    int m = (ra == BLD_NOROOM || rb == BLD_NOROOM) ? SITE_PATCH_M
                                                   : site_metres(s, ra, rb);
    if (m < 0) { s->err = SITE_ENOROUTE; return -1; }
    int cost = site_cable_price(k, m);
    if (s->money < cost) { s->err = SITE_EMONEY; return -1; }

    int cid = net_cable(s->net, s->dev[a].node, aport, s->dev[b].node, bport, m, k);
    if (cid < 0) { s->err = SITE_EBUSY; return -1; }

    SiteLink *l = &s->link[s->nlink];
    memset(l, 0, sizeof *l);
    l->a = (int16_t)a; l->b = (int16_t)b;
    l->aport = (int16_t)aport; l->bport = (int16_t)bport;
    l->room_a = (uint16_t)ra; l->room_b = (uint16_t)rb;
    l->metres = m;
    l->kind = (uint8_t)k;
    l->cost = cost;
    l->cable = cid;
    s->money -= cost;
    s->spent += cost;
    /* Note what is NOT here: any check that the run is short enough. The
     * cable is bought, laid and paid for, and whether it carries anything is
     * a question for the copper. */
    return s->nlink++;
}

void site_uncable(Site *s, int link)
{
    if (link < 0 || link >= s->nlink) return;
    if (s->link[link].cable >= 0) net_uncable(s->net, s->link[link].cable);
    s->link[link].cable = -1;
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
    net_close_all(s->net, node);
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
    net_port_mode(s->net, s->dev[dev].node, port, PORT_ACCESS);
    net_port_vlan(s->net, s->dev[dev].node, port, vlan);
    return true;
}
bool site_port_trunk(Site *s, int dev, int port, int vlan)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    if (!site_kind_is_switch(s->dev[dev].kind)) { s->err = SITE_ENOTSW; return false; }
    if (port < 0 || port >= s->dev[dev].nports) { s->err = SITE_ENOPORT; return false; }
    net_port_mode(s->net, s->dev[dev].node, port, PORT_TRUNK);
    if (vlan > 0) net_trunk_allow(s->net, s->dev[dev].node, port, vlan);
    return true;
}
bool site_dhcpd(Site *s, int dev, uint32_t first, int count, uint32_t mask,
                uint32_t gw, uint32_t dns)
{
    s->err = SITE_OK;
    if (!live_dev(s, dev)) return false;
    net_dhcpd(s->net, s->dev[dev].node, first, count, mask, gw, dns);
    return true;
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
        { "residence", RM_RESIDENCE }, { "retail", RM_RETAIL }, { NULL, 0 }
    };
    for (int i = 0; K[i].n; i++)
        if (strcmp(dot + 1, K[i].n) == 0) return bld_find(s->b, floor, K[i].k);
    return -1;
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
    buf_printf(out, "%d tenancies want service in this tower\n\n", s->ntenant);
    buf_puts(out, "  day  floor  tenant  drops  wants                             rent/mo\n");
    for (int i = 0; i < s->ntenant; i++) {
        const SiteTenant *t = &s->tenant[i];
        char want[48];
        snprintf(want, sizeof want, "%s%s", t->own_segment
                 ? "a segment of its own" : "a port on anything",
                 t->wants_server ? " and a server" : "");
        buf_printf(out, "  %3d  %5d  %6d  %5d  %-33s %6d\n",
                   t->day, t->floor, t->tenant, t->drops, want, t->rent);
        drops += t->drops;
        seg += t->own_segment;
        srv += t->wants_server;
        rent += t->rent;
    }
    buf_printf(out, "\n%d drops in all, %d of them wanting a segment of their "
                    "own, %d wanting a server\n", drops, seg, srv);
    buf_printf(out, "which is %d twenty-four port switches, or %d eight port "
                    "ones, and %d a month of rent to pay for them\n",
               (drops + 22) / 23, (drops + 6) / 7, rent);
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
        where(s, d, w, sizeof w);
        buf_printf(out, "  %-12s %-9s %-21s", d->name, site_kind_name(d->kind), w);
        if (site_kind_is_switch(d->kind)) {
            int used = 0;
            for (int p = 0; p < d->nports; p++)
                if (net_port_state(s->net, d->node, p) != PORT_NOCABLE) used++;
            buf_printf(out, " %d/%d ports used", used, d->nports);
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
    if (s->nlink) { buf_putc(out, '\n'); site_dump_links(s, out); }
}

void site_dump_links(const Site *s, Buf *out)
{
    int total = 0, cost = 0;
    buf_puts(out, "  cable\n");
    for (int i = 0; i < s->nlink; i++) {
        const SiteLink *l = &s->link[i];
        char a[40], b[40];
        snprintf(a, sizeof a, "%s:%d", s->dev[l->a].name, l->aport);
        snprintf(b, sizeof b, "%s:%d", s->dev[l->b].name, l->bport);
        buf_printf(out, "  %2d  %-16s %-16s %4d m  %-6s %4d  %s\n", i, a, b,
                   l->metres, site_cable_name((CableKind)l->kind), l->cost,
                   pstate(site_link_state(s, i)));
        if (l->cable >= 0) { total += l->metres; cost += l->cost; }
    }
    buf_printf(out, "  %d m of cable in the building, %d of it spent\n", total, cost);
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

static void dump_dev(Site *s, int dev, Buf *out, bool empties)
{
    if (dev < 0 || dev >= s->ndev) { buf_puts(out, "no such device\n"); return; }
    SiteDev *d = &s->dev[dev];
    char w[48];
    where(s, d, w, sizeof w);
    buf_printf(out, "%s: %s in %s, %d socket%s%s\n", d->name, site_kind_name(d->kind),
               w, d->nports, d->nports == 1 ? "" : "s",
               site_kind_has_os(d->kind) && !d->powered
               ? " -- SWITCHED OFF, and nothing of it is on the network" : "");
    if (empties) net_dump_ports(s->net, d->node, out);
    else net_dump_ports_used(s->net, d->node, out);
    if (site_kind_is_switch(d->kind)) {
        net_dump_fdb(s->net, d->node, out);
    } else {
        net_dump_ifaces(s->net, d->node, out);
        net_dump_routes(s->net, d->node, out);
        net_dump_arp(s->net, d->node, out);
    }
}

void site_dump_dev(Site *s, int dev, Buf *out) { dump_dev(s, dev, out, true); }
/* What a person reads when they have just put a lead in a box: what is
 * plugged into it, not a list of the sockets that are empty. */
void site_dump_dev_brief(Site *s, int dev, Buf *out) { dump_dev(s, dev, out, false); }

/* ------------------------------------------------------------- the shell */
/* One line, one operation. Deliberately dull to parse: a blind playtester
 * writing a script should never have to think about quoting. */
#define MAXTOK 12

static int split(char *line, char *tok[MAXTOK])
{
    int n = 0;
    char *p = line;
    while (*p && n < MAXTOK) {
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

static CableKind cable_arg(const char *a)
{
    for (int i = 0; i < CAB_KIND_COUNT; i++)
        if (strcmp(SPOOL[i].name, a) == 0) return (CableKind)i;
    return CAB_CAT6;
}

bool site_cmd(Site *s, const char *line, Buf *out)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", line);
    char *t[MAXTOK];
    int n = split(buf, t);
    if (!n) return true;

    if (strcmp(t[0], "help") == 0) {
        buf_puts(out,
            "order <kind> [name]            kinds: switch8 switch24 router pc server\n"
            "                               it is delivered to goods in, on the\n"
            "                               ground floor. Not to where you are.\n"
            "move <dev> <room>              carry it there. Refused while it has a\n"
            "                               cable in it. rooms: #41, f3.comms,\n"
            "                               f0.mdf, f2.office\n"
            "cable <dev>:<port> <dev>:<port> [cat5e|cat6|fibre]\n"
            "uncable <n>                    pull one out\n"
            "addr <dev>[:<nic>] <ip>/<bits> an address on a card. `addr rt 1.2.3.4/30`\n"
            "                               is the first socket, `addr rt:1 ...` the\n"
            "                               second -- which is how a router gets a WAN\n"
            "                               side and a LAN side\n"
            "power <dev> on|off             a pc and a server arrive switched off.\n"
            "                               Nothing of an off box is on the network\n"
            "gw <dev> <ip>                  default gateway\n"
            "router <dev> on|off            forward between its interfaces\n"
            "subif <dev> <nic> <vlan> <ip>/<bits>   a tagged subinterface on a card\n"
            "vlan <dev> <port> <n>          a switch's access port, in a vlan\n"
            "trunk <dev> <port> <vlan>...   a trunk, and what it may carry\n"
            "dhcpd <dev> <first> <count> <bits> <gw> <dns>\n"
            "dhcp <dev>                     ask for a lease, for real\n"
            "resolver <dev> <ip>            resolv.conf, in one line\n"
            "ping <dev> <ip>                a real ICMP echo over the wire\n"
            "trace <dev> <ip>               traceroute, counted by ttl\n"
            "resolve <dev> <name>           a real DNS query\n"
            "get <dev> <ip> <path>          fetch a page over TCP\n"
            "show [dev] | links | rooms <f> | demand | money | frames\n");
        return true;
    }
    if (strcmp(t[0], "show") == 0) {
        if (n > 1) site_dump_dev(s, dev_arg(s, t[1]), out);
        else site_dump(s, out);
        return true;
    }
    if (strcmp(t[0], "links") == 0) { site_dump_links(s, out); return true; }
    if (strcmp(t[0], "rooms") == 0) {
        site_dump_rooms(s, n > 1 ? atoi(t[1]) : 0, out);
        return true;
    }
    if (strcmp(t[0], "demand") == 0) { site_dump_demand(s, out); return true; }
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
        buf_printf(out, "%s: a %s, %d port%s, %d paid, %ld left.\n",
                   s->dev[d].name, site_kind_name(kind), s->dev[d].nports,
                   s->dev[d].nports == 1 ? "" : "s", site_kind_price(kind), s->money);
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
        int was = s->dev[d].room;
        if (!site_move(s, d, room)) {
            buf_printf(out, "refused: %s\n", site_err_text(s->err));
            if (s->err == SITE_ECABLED)
                buf_puts(out, "  `links` says which cable, `uncable <n>` pulls "
                              "it out. The copper is\n  bought and paid for and "
                              "you will be buying it again.\n");
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
    if (strcmp(t[0], "uncable") == 0 && n >= 2) {
        site_uncable(s, atoi(t[1]));
        buf_puts(out, "pulled out\n");
        return true;
    }
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
            return true;
        }
        buf_printf(out, "%s is %s\n", s->dev[d].name, on ? "on" : "off");
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
    if (strcmp(t[0], "trunk") == 0 && n >= 3) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        bool ok = site_port_trunk(s, d, atoi(t[2]), 0);
        for (int i = 3; i < n; i++)
            ok = site_port_trunk(s, d, atoi(t[2]), atoi(t[i])) && ok;
        buf_printf(out, "%s\n", ok ? "set" : site_err_text(s->err));
        return true;
    }
    if (strcmp(t[0], "dhcpd") == 0 && n >= 7) {
        int d = dev_arg(s, t[1]);
        uint32_t first, gw, dns;
        if (d < 0 || !net_parse_ip(t[2], &first) || !net_parse_ip(t[5], &gw) ||
            !net_parse_ip(t[6], &dns)) { buf_puts(out, "?\n"); return true; }
        buf_printf(out, "%s\n", site_dhcpd(s, d, first, atoi(t[3]),
                                           net_mask_bits(atoi(t[4])), gw, dns)
                   ? "serving" : site_err_text(s->err));
        return true;
    }
    if (strcmp(t[0], "dhcp") == 0 && n >= 2) {
        int d = dev_arg(s, t[1]);
        if (d < 0) { buf_puts(out, "?\n"); return true; }
        bool got = site_dhcp(s, d);
        char ip[20];
        net_fmt_ip(net_if_get_addr(s->net, s->dev[d].node, 0), ip, sizeof ip);
        buf_printf(out, "%s\n", got ? ip
                   : "no lease: nothing answered, or the pool is empty");
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
        PingResult r = net_ping(s->net, s->dev[d].node, ip, &rtt);
        buf_printf(out, "%s", net_ping_text(r));
        if (r == PING_OK) buf_printf(out, " in %d ms", rtt);
        buf_putc(out, '\n');
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
        if (net_resolve(s->net, s->dev[d].node, t[2], &a)) {
            char h[20];
            net_fmt_ip(a, h, sizeof h);
            buf_printf(out, "%s\n", h);
        } else buf_puts(out, "no answer\n");
        return true;
    }
    if (strcmp(t[0], "get") == 0 && n >= 4) {
        int d = dev_arg(s, t[1]);
        uint32_t ip;
        if (d < 0 || !net_parse_ip(t[2], &ip)) { buf_puts(out, "?\n"); return true; }
        Buf page = {0};
        int st = net_http_get(s->net, s->dev[d].node, ip, 80, t[3], &page);
        if (st >= 100) buf_printf(out, "HTTP %d, %u bytes\n", st, (unsigned)page.len);
        else {
            /* `HTTP -1, 0 bytes` IS NOT A STATUS. There was no reply, and the
             * interesting question is how far it got -- so ask the wire the
             * same way a person would, with a ping, and say which of the two
             * different faults this is. */
            char a[20];
            net_fmt_ip(ip, a, sizeof a);
            int rtt = 0;
            PingResult pr = net_ping(s->net, s->dev[d].node, ip, &rtt);
            buf_printf(out, "no reply from %s port 80\n", a);
            if (pr != PING_OK)
                buf_printf(out, "  and %s does not answer a ping either: %s.\n"
                                "  This is a routing or addressing fault, not a "
                                "web one.\n", a, net_ping_text(pr));
            else
                buf_printf(out, "  %s answers a ping in %d ms, so the copper and "
                                "the routing are fine.\n  Nothing accepted the "
                                "connection: either no service is listening on\n"
                                "  port 80, or a filter is dropping it. `show` the "
                                "far box.\n", a, rtt);
        }
        buf_free(&page);
        return true;
    }
    buf_printf(out, "no such command: %s (try help)\n", t[0]);
    return false;
}
