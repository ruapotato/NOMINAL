/* siteday.c — the clock, the people on the other end of it, and the bill.
 *
 * WHY THIS FILE EXISTS. The first blind playtester of the tower measured
 * fifteen decisions an hour -- fifteen times the break-fix game -- and then
 * said the thing that mattered: *"They felt like MY decisions; they did not
 * yet feel like decisions that would come back for me."* Nothing came back,
 * because nothing advanced. Cable refunded in full, floors opened free from
 * anywhere, and no day ever ended, so nothing anybody built was ever tested
 * by anything.
 *
 * So: a day passes. Tenancies whose day has come move in and their desks
 * arrive -- real cards, in the room they rent, with nothing plugged into
 * them until the player runs the copper. Their people then do a day's work
 * over the network the player built, and the work is REAL: a DNS query to
 * the resolver they were given, a TCP connection to a file server, two
 * megabytes pulled through whatever wires are between them. What finishes
 * is what the tenant pays for.
 *
 * THE ONE RULE THIS FILE MUST NOT BREAK. There is no load model here. Not a
 * number added to a counter, not a bandwidth budget, not a difficulty
 * constant. Every byte in this file goes through core/netstack.c as frames
 * on ports, and every frame that is lost is lost BY the stack, on a port,
 * for a reason the port counter prints in words -- `show <box>` from the
 * tower, or `netstat -P` on a box that has a shell. The reason a flat tower falls
 * over and a segmented one does not is that the frames really go somewhere
 * different, and nobody wrote down that they should.
 *
 * DETERMINISM. Session start offsets come from a Rng seeded from the world
 * seed and the day number, so the same seed plays the same day, always.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "site.h"
/* The world's half of the breaker. Everything this file does to a disk goes
 * through core/breaker.c, so there is exactly one kind of broken. */
#include "machine.h"

/* ------------------------------------------------------------ the desks */
/* WHAT A TENANT BRINGS. One computer per drop they asked for, in the first
 * room they hold. The landlord does not buy these and does not own them:
 * what the landlord sells is the port each one is plugged into, and until
 * somebody runs the copper they are a room full of machines with no network.
 *
 * They are named for the tenancy so that a player reading `netstat` on a
 * switch, or an fdb, or a trace, can tell whose traffic they are looking at.
 * "t7d3" is the fourth desk of tenancy seven and it is on floor seven. */
static void move_in(Site *s, int ti)
{
    SiteTenant *t = &s->tenant[ti];
    if (t->moved) return;
    t->moved = 1;
    t->desk0 = s->ndev;
    t->ndesk = 0;
    for (int i = 0; i < t->drops; i++) {
        char nm[NET_NAME_MAX];
        snprintf(nm, sizeof nm, "t%dd%d", t->tenant, i);
        int d = site_install(s, SDEV_DESK, t->room, nm);
        if (d < 0) break;              /* the world is full; say so upstairs */
        t->ndesk++;
    }
}

int site_tenant_connected(const Site *s, int tenant)
{
    if (tenant < 0 || tenant >= s->ntenant) return 0;
    const SiteTenant *t = &s->tenant[tenant];
    int k = 0;
    for (int i = 0; i < t->ndesk; i++) {
        int d = t->desk0 + i;
        if (net_port_state(s->net, s->dev[d].node, 0) == PORT_UP) k++;
    }
    return k;
}

int site_tenant_addressed(const Site *s, int tenant)
{
    if (tenant < 0 || tenant >= s->ntenant) return 0;
    const SiteTenant *t = &s->tenant[tenant];
    int k = 0;
    for (int i = 0; i < t->ndesk; i++) {
        int d = t->desk0 + i;
        if (net_port_state(s->net, s->dev[d].node, 0) == PORT_UP &&
            net_if_get_addr(s->net, s->dev[d].node, 0)) k++;
    }
    return k;
}

/* THE PLAYER'S HALF OF IT. Run copper from a box you own to a tenancy's
 * desks: one cable each, priced by the metre of tray between the room the
 * box is in and the room the desks are in, and it stops when the box runs
 * out of holes. The twenty-fifth desk on a twenty-four port switch has
 * nowhere to go and this says so by connecting twenty-three of them.
 *
 * A view does this one desk at a time with a person walking a drum around;
 * this is the same call, in bulk, for the socket. Nothing here is cheaper
 * than doing it by hand -- every metre is charged. */
/* AND THE VLAN THE PORTS LAND IN, because leaving that out made the verb
 * half a job. `serve` cabled twenty desks and left every one of them in the
 * default vlan, so a playtester typed twenty `vlan sw 0 30`..`vlan sw 19 30`
 * lines afterwards -- for a tenancy the site already knows wants a broadcast
 * domain of its own. A person standing at that switch with a drum patches
 * the port and sets the port, in that order, at the same moment.
 *
 * Zero means "leave them where they are", which is what the untagged default
 * is and what every existing caller wants. */
int site_serve_vlan(Site *s, int tenant, int dev, CableKind k, int vlan)
{
    s->err = SITE_OK;
    if (tenant < 0 || tenant >= s->ntenant) { s->err = SITE_ENODEV; return -1; }
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return -1; }
    SiteTenant *t = &s->tenant[tenant];
    if (!t->moved) { s->err = SITE_ENODEV; return -1; }
    int done = 0;
    for (int i = 0; i < t->ndesk; i++) {
        int d = t->desk0 + i;
        if (net_port_state(s->net, s->dev[d].node, 0) != PORT_NOCABLE) { done++; continue; }
        int p = site_free_port(s, dev);
        if (p < 0) { s->err = SITE_ENOPORT; break; }
        if (site_cable(s, dev, p, d, 0, k) < 0) break;   /* money, or space */
        /* The port is set as it is patched, not in a second pass, so a run
         * that stops halfway leaves no port cabled into the wrong segment. */
        if (vlan > 0) site_port_vlan(s, dev, p, vlan);
        done++;
    }
    return done;
}

int site_serve(Site *s, int tenant, int dev, CableKind k)
{
    return site_serve_vlan(s, tenant, dev, k, 0);
}

/* ----------------------------------------------------------- the circuit */
/* WHAT THE ISP SELLS, and it is not the speed of the fibre in the street.
 * The handoff is rate-limited to what has been bought, which is a media
 * converter on a wall doing exactly this, and it is why an uplink can be
 * saturated by a building that has gigabit copper in every riser. */
long site_isp_price(int mb)
{
    if (mb <= 0) return 0;
    return 40 + (long)mb * 3;           /* pounds a month                    */
}

/* AND THE CIRCUIT IS BILLED, which for forty-two days of playtesting it was
 * not. `isp` said "500 Mb, 1540 a month" and a month and a half went by
 * without a penny of it ever leaving the account: `spent` was hardware plus
 * copper and nothing else, so the biggest recurring decision in the game --
 * how much circuit to buy, against how much of the tower's traffic you keep
 * off it by putting a server on the floor -- cost the player nothing either
 * way. A bill that never arrives is not a price.
 *
 * The month is the same thirty days the rent is a thirtieth of, so the two
 * halves of the account use one calendar. */
#define SITE_MONTH_DAYS  30

/* HOW LONG A NEW TENANT GIVES YOU BEFORE THEY START RINGING. They moved in
 * this morning; they are unpacking, and nobody expects the network on day
 * one. Three days later they expect it, and a day after that with not one
 * desk able to work is a strike -- so a tenancy left with no ports at all
 * files a complaint on the sixth day after their lease started. It is a
 * chosen number, in the same sense as the fit-out rate and the two demand
 * numbers in site.h, and it is chosen to be shorter than the schedule's gap
 * between leases so that falling behind compounds. */
#define SITE_FITOUT_DAYS  3

int site_isp_days_to_bill(const Site *s)
{
    return SITE_MONTH_DAYS - (s->day % SITE_MONTH_DAYS);
}

bool site_isp(Site *s, int mb)
{
    s->err = SITE_OK;
    if (mb < 10) { s->err = SITE_EADDR; return false; }
    long up = site_isp_price(mb) - site_isp_price(s->isp_mb);
    if (up > 0 && s->money < up) { s->err = SITE_EMONEY; return false; }
    if (up > 0) { s->money -= up; s->spent += up; }
    s->isp_mb = mb;
    net_port_rate(s->net, s->dev[s->uplink].node, 0, mb);
    return true;
}

/* ================================================== a day's work, for real
 *
 * Two transfers per desk, both running at once, because that is what a
 * machine on a desk does: the page and the file are pulled at the same
 * moment, not one after the other. Both are ordinary HTTP over the ordinary
 * TCP in netstack.c, driven a millisecond at a time so that every desk in
 * the building is pulling at once -- which is the whole point. A transfer
 * that has not finished when the busy period ends is a person who did not
 * get their work done, and that is the only definition of "bad service"
 * anywhere in this program.
 *
 * WHY THEY USED TO BE ONE AFTER THE OTHER, AND WHY THAT CAPPED THE GAME.
 * A desk fetched its page, and only when that finished did it open its file.
 * One socket at a time per desk, and a socket in this world cannot beat one
 * receive window per round trip -- twelve kilobytes over the four
 * milliseconds a two-hop LAN costs is about twenty-four megabits, and a
 * routed path is half that. So one desk could not offer more than about
 * three megabytes across the whole busy period NO MATTER WHAT THE PLAYER
 * BUILT, and a hundred and seventy-six of them together could not fill more
 * than roughly one gigabit link. Every riser in the tower is a gigabit link.
 * That, and not the size of anybody's day, is why cable grade, circuit size
 * and switch count never bit: the demand was capped by the client, and the
 * only thing left that could ever be full was the landlord's circuit -- so
 * the whole difficulty curve reduced to the single question of whether a
 * file server existed at all.
 *
 * It also meant the file server was never tested on the day it mattered. A
 * desk whose page did not arrive never opened its file, so on a congested
 * day the traffic to the thing the architecture is about simply vanished.
 *
 * Nothing here is a bigger number. A person's machine does several things at
 * once; it always did; this file was modelling it as though it did not. */
typedef enum {
    X_WAIT = 0,     /* has not started yet                                  */
    X_CONNECT,      /* the handshake is out                                 */
    X_RECV,         /* the request is sent; pulling the answer              */
    X_DONE,
    X_FAILED
} XState;

typedef struct {
    int      dev;          /* the desk                                      */
    int      tenant;
    uint32_t dst;
    int      leg;          /* 0 = the web fetch, 1 = the file               */
    int      kb;
    int      start;        /* the tick it begins                            */
    int      sock;
    uint8_t  state;
    long     got, want;
    int      began, ended; /* ticks, for the latency the player feels       */
} Xfer;

/* WHO SERVES THE FILES, and it is the nearest one, which is why WHERE the
 * player puts a server is a decision rather than a purchase.
 *
 * Their own tenancy's server first; then any server on their own floor,
 * because that is the one their people would be told to use and because it
 * is the one whose traffic never has to leave the floor; then any server in
 * the building at all; and if there is none, the internet -- which is the
 * naive answer and puts every file anybody opens onto the landlord's
 * circuit. Nothing here decides how much that costs. It only decides where
 * the frames are addressed, and the wires decide the rest. */
/* Returns the DEVICE, not the address, so that the day report can name it.
 * The preference order is the same as it always was: their own machine, then
 * one on their floor, then anything powered and addressed in the building. */
/* IS THERE AN ADDRESS ON THIS BOX AT ALL -- on any card, socket or tagged
 * subinterface.
 *
 * This used to ask for an address on INTERFACE 0 specifically, and that
 * silently disqualified the exact machine D27 recommends building: a floor's
 * own server, in the floor's own cupboard, doing that floor's DHCP -- which
 * has to live on subinterfaces, because a box serving several vlans needs an
 * address in each of them and eth0 is only one of them. A playtester watched
 * floor 2's tenancies pull their files off srv1 a floor down across the
 * riser for two days, with srv2 powered, addressed, httpd running, ten
 * metres away. `service` printed the `<-` and explained it as a riser
 * crossing, which was true and was not the reason.
 *
 * There is no justification anybody could have given a player for the old
 * rule, so it is gone rather than documented. Which card a server's address
 * is on is a fact about its cabling, not about whether it can hold files. */
static uint32_t any_addr(const Site *s, int node)
{
    for (int i = 0; i < NET_IF_MAX; i++) {
        uint32_t a = net_if_get_addr(s->net, node, i);
        if (a) return a;
    }
    return 0;
}

/* WHICH OF ITS ADDRESSES A DESK WOULD USE: the one on the desk's own
 * segment, if it has one, and otherwise the first it has. A floor server
 * with a leg in each of three vlans is answered on the leg the asker is
 * standing in, which is what a routing table does, and it means the traffic
 * of a floor whose server is on its own vlan never leaves the floor. */
static uint32_t server_addr_for(const Site *s, int dev, int desk)
{
    uint32_t da = net_if_get_addr(s->net, s->dev[desk].node, 0);
    uint32_t dm = net_if_get_mask(s->net, s->dev[desk].node, 0);
    int node = s->dev[dev].node;
    uint32_t first = 0;
    for (int i = 0; i < NET_IF_MAX; i++) {
        uint32_t a = net_if_get_addr(s->net, node, i);
        if (!a) continue;
        if (!first) first = a;
        uint32_t m = net_if_get_mask(s->net, node, i);
        if (da && dm && (a & m) == (da & m) && (a & dm) == (da & dm)) return a;
    }
    return first;
}

static int file_server_for(const Site *s, int tenant)
{
    int any = -1, floor = -1;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (d->kind != SDEV_SERVER || !d->powered) continue;
        if (!any_addr(s, d->node)) continue;
        if (d->tenant && d->tenant == s->tenant[tenant].tenant) return i;
        if (floor < 0 && d->floor == s->tenant[tenant].floor) floor = i;
        if (any < 0) any = i;
    }
    return floor >= 0 ? floor : any;
}

static void xfer_begin(Site *s, Xfer *x, int tick)
{
    int node = s->dev[x->dev].node;
    char path[32];
    snprintf(path, sizeof path, "/n/%d", x->kb);
    x->sock = net_tcp_connect(s->net, node, x->dst, 80);
    if (x->sock < 0) { x->state = X_FAILED; return; }
    x->began = tick;
    x->state = X_CONNECT;
    (void)path;
}

static void xfer_poll(Site *s, Xfer *x, int tick)
{
    Net *n = s->net;
    if (x->state == X_CONNECT) {
        TcpState st = net_tcp_state(n, x->sock);
        if (st == TCP_ESTABLISHED) {
            char req[96];
            int rl = snprintf(req, sizeof req,
                              "GET /n/%d HTTP/1.0\r\nHost: files\r\n\r\n", x->kb);
            net_tcp_send(n, x->sock, req, rl);
            x->state = X_RECV;
        } else if (st == TCP_CLOSED) {
            /* The handshake never completed. On a saturated network this is
             * a SYN that was dropped on a port, and it is the same failure a
             * person sees as a browser that will not connect. */
            x->state = X_FAILED;
            net_sock_free(n, x->sock);
            x->sock = -1;
        }
        return;
    }
    if (x->state != X_RECV) return;
    uint8_t b[1024];
    int k;
    while ((k = net_tcp_recv(n, x->sock, b, sizeof b)) > 0) x->got += k;
    TcpState st = net_tcp_state(n, x->sock);
    if (st == TCP_CLOSE_WAIT || st == TCP_CLOSED) {
        while ((k = net_tcp_recv(n, x->sock, b, sizeof b)) > 0) x->got += k;
        net_tcp_close(n, x->sock);
        x->ended = tick;
        /* Content-Length said how many bytes; anything short is a transfer
         * that was cut off, and a cut-off transfer is not a finished one. */
        x->state = (x->got >= x->want) ? X_DONE : X_FAILED;
        x->sock = -1;
    }
}

/* ------------------------------------------------------------ the report */
/* The busiest port in the building, and how busy. This is the summary line;
 * the evidence is `show <box>` on the box it names, which prints the drops
 * and the queue and the reason -- and it is `show` rather than `netstat -P`
 * because the busiest port in a tower is almost always on a switch, a router
 * or the handoff, and none of those have a shell to type netstat into. */
static void hottest_port(const Site *s, uint64_t window_us, SiteDay *rep)
{
    int best_util = -1;
    rep->hot[0] = 0;
    rep->hot_util = 0;
    if (!window_us) return;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (d->kind == SDEV_DESK) continue;
        for (int p = 0; p < d->nports; p++) {
            if (net_port_state(s->net, d->node, p) != PORT_UP) continue;
            uint64_t busy = net_port_busy_us(s->net, d->node, p);
            int util = (int)((busy * 100) / window_us);
            if (util > best_util) {
                best_util = util;
                snprintf(rep->hot, sizeof rep->hot, "%s:%d", d->name, p);
            }
        }
    }
    rep->hot_util = best_util < 0 ? 0 : best_util;
}

/* ===================================================== THE WORLD BREAKS THINGS
 *
 * WHY THIS IS HERE AND NOT IN A FAULT GENERATOR. From D23: *the world
 * supplies the cause.* Sixty-two fault types existed and every one was proven
 * findable and repairable, and there was no way for the TOWER to cause a
 * single one of them -- faults arrived because a ticket was generated, and a
 * machine you installed, cabled and ran for forty days never broke.
 *
 * Five rules, and each of them is a thing this code can be checked against:
 *
 *   1. THE DAMAGE IS REAL. Every byte of it is written by core/breaker.c into
 *      the machine's own Vfs. `pkg verify` sees it because the file genuinely
 *      differs from what shipped. There is no event flag anywhere in the boot
 *      chain and there must never be one.
 *   2. THE CAUSE IS FINDABLE. A blackout is in the site's own log and in the
 *      syslog of every box that had a battery under it; a dying disk
 *      complains in /var/log/messages for days before it loses anything.
 *   3. IT IS FIXABLE WITH WHAT EXISTS. `fsck`, `pkg verify`, `pkg diff`, `pkg
 *      reinstall`, the rescue medium. Nothing new was added to repair any of
 *      this.
 *   4. IT IS AVOIDABLE OR SURVIVABLE BY GOOD PLAY. A UPS rides the mains
 *      failure out; a disk warns for days before it fails and can be swapped;
 *      a cupboard that is cooking says so before it trips and the kit in it
 *      can be moved. A disaster nobody could have prevented is a tax.
 *   5. IT IS DETERMINISTIC FROM THE SEED. The blackout schedule is a pure
 *      function of seed and day. Wear is measured off the ports, and the
 *      ports carry whatever the busy period really put through them.
 *
 * NOTHING HERE IS A TIMER WITH A DIE IN IT. A disk wears at a rate taken from
 * how hard its own port worked that day, and a cupboard cooks because of the
 * watts the player put in it divided by the square metres the building
 * generator gave it.
 */

/* --------------------------------------------------------------- the log */
static void ev_add(Site *s, int kind, int dev, const char *fmt, ...)
{
    if (s->nev >= SITE_MAX_EVENT) {
        /* Keep the newest. A tower that has run three hundred days has had
         * more weather than anybody wants to read. */
        memmove(&s->ev[0], &s->ev[1], sizeof s->ev[0] * (SITE_MAX_EVENT - 1));
        s->nev = SITE_MAX_EVENT - 1;
    }
    SiteEvent *e = &s->ev[s->nev++];
    memset(e, 0, sizeof *e);
    e->day = s->day;
    e->kind = (uint8_t)kind;
    e->dev = (int16_t)dev;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->what, sizeof e->what, fmt, ap);
    va_end(ap);
    s->ev_total++;
}

void site_boxes(Site *s, SiteBoxFn fn, void *ctx) { s->box = fn; s->boxctx = ctx; }

static Machine *box_of_dev(Site *s, int dev)
{
    if (!s->box) return NULL;
    return (Machine *)s->box(s->boxctx, dev);
}

/* ------------------------------------------------------------ the mains */
/* WHEN THE LIGHTS GO OUT, and it is a pure function of the seed and the day.
 * No state, nothing rolled during play, so the same seed always has its
 * blackout on the same morning and a gate can say which one.
 *
 * THE FIRST ONE IS DELIBERATELY LATE. Not to be kind: a mains failure in the
 * first fortnight lands on a building with one switch in it and nothing to
 * lose, which teaches the player nothing except that the game has weather.
 * By the third or fourth week there are tenants, a server holding their
 * files, and a decision about a battery that has already been available to
 * make. After that they come round every two or three weeks, which is often
 * enough that a UPS pays for itself on a long run and rare enough that
 * nobody plans their week around one. */
bool site_mains_fails_on(uint64_t seed, int day)
{
    if (day < 1) return false;
    Rng r;
    rng_seed(&r, seed ^ 0x9d0c17c0ffeeull);
    int d = 20 + (int)rng_range(&r, 0, 10);
    while (d <= day) {
        if (d == day) return true;
        d += 17 + (int)rng_range(&r, 0, 12);
    }
    return false;
}

long site_ups_price(void)  { return 220; }
long site_disk_price(void) { return 140; }

bool site_ups(Site *s, int dev)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    if (!site_kind_has_os(s->dev[dev].kind)) { s->err = SITE_ENOBTN; return false; }
    if (s->dev[dev].ups) return true;
    if (s->money < site_ups_price()) { s->err = SITE_EMONEY; return false; }
    s->money -= site_ups_price();
    s->spent += site_ups_price();
    s->dev[dev].ups = 1;
    return true;
}

bool site_disk(Site *s, int dev)
{
    s->err = SITE_OK;
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return false; }
    if (!site_kind_has_os(s->dev[dev].kind)) { s->err = SITE_ENOBTN; return false; }
    if (s->money < site_disk_price()) { s->err = SITE_EMONEY; return false; }
    s->money -= site_disk_price();
    s->spent += site_disk_price();
    s->dev[dev].wear = 0;
    s->dev[dev].warned = 0;
    /* A NEW DISK HAS LOST NOTHING, so the escalation starts again from the
     * courtesy the first loss gets. What it does NOT reset is the damage
     * already on the old one: `disk` clones what is there, so a file the last
     * sector took is still wrong on the new platter and `pkg verify` still
     * says so. Swapping the disk stops the bleeding; it does not undo it. */
    s->dev[dev].lost = 0;
    Machine *m = box_of_dev(s, dev);
    if (m) breaker_syslog(m, "kernel: sd 0:0:0:0: [sda] new medium, "
                             "0 reallocated sectors, 0 power-on days");
    return true;
}

/* ----------------------------------------------------------------- heat */
/* WHAT A BOX DISSIPATES. Ordinary nameplate figures for the class of kit --
 * an eight-port switch is a wall wart, a server with disks in it is a fan
 * heater -- and the desks are not counted because they are the tenant's own
 * machines in the tenant's own room, which is a room with a window in it. */
static int watts_of(int kind)
{
    switch (kind) {
    case SDEV_UPLINK:   return 15;
    case SDEV_SWITCH8:  return 25;
    case SDEV_SWITCH24: return 60;
    case SDEV_ROUTER:   return 45;
    case SDEV_PC:       return 130;
    case SDEV_SERVER:   return 320;
    default:            return 0;
    }
}

int site_room_watts(const Site *s, int room)
{
    int w = 0;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (d->room != room || d->kind == SDEV_DESK) continue;
        if (site_kind_has_os(d->kind) && !d->powered) continue;
        w += watts_of(d->kind);
    }
    return w;
}

/* WHAT A ROOM CAN GET RID OF, in watts, and the square metres in it come out
 * of the building generator rather than out of anything anybody typed here.
 *
 * Per square metre a sealed cupboard sheds about twenty watts through its
 * walls and its door and no more; a plant space with some air movement in it
 * does half as well again; occupied space is conditioned for people and can
 * take three times that; and a tenant's own server room is the one space in
 * this world that was built to hold equipment, so it has cooling in it. These
 * are the only four numbers in the heat model and every one of them is a
 * defensible figure for that kind of space.
 *
 * The point is not the arithmetic. It is that a comms cupboard is a cupboard:
 * putting a third server in one is a decision with a consequence, and the
 * consequence is legible before it bites. */
static int sheds_per_m2(int kind)
{
    switch (kind) {
    case RM_COMMS: case RM_RISER:            return 20;
    case RM_MDF:   case RM_PLANT:            return 30;
    case RM_SERVER:                          return 120;
    default:                                 return 60;
    }
}

int site_room_capacity(const Site *s, int room)
{
    if (room < 0 || room >= s->b->nrooms) return 0;
    const Room *r = &s->b->rooms[room];
    double area = bld_room_area(r);
    if (area < 1.0) area = 1.0;
    return (int)(area * sheds_per_m2(r->kind));
}

/* How full of heat a room is, as a percentage of what it can shed. A hundred
 * means the kit in there is making exactly as much heat as the room can lose,
 * which is the point at which it stops being room temperature in there. */
int site_room_heat(const Site *s, int room)
{
    int cap = site_room_capacity(s, room);
    if (cap <= 0) return 0;
    return site_room_watts(s, room) * 100 / cap;
}

#define HEAT_WARN   100    /* per cent of what the room can shed            */
#define HEAT_TRIP   140    /* and hot enough to shut something down         */
#define HEAT_DAYS     3    /* consecutive days over, before anything trips  */
#define WEAR_WARN    45    /* days of average use before a disk complains   */
#define WEAR_FAIL    60    /* and before it loses a sector                  */

/* ------------------------------------------------------- what today did
 * WEAR IS MEASURED, NOT COUNTED. The busy period has just finished and the
 * port counters have not been reset yet, so this is exactly how hard the wire
 * into this box worked today. A server holding a floor's files ages three or
 * four times as fast as a box nobody has touched since it was installed,
 * which is the whole of "a property of the kit and how it has been used". */
static int used_pct(const Site *s, int dev)
{
    uint64_t window = SITE_BUSY_MS * 1000ull;
    uint64_t busy = 0;
    for (int p = 0; p < s->dev[dev].nports; p++) {
        if (net_port_state(s->net, s->dev[dev].node, p) != PORT_UP) continue;
        uint64_t b = net_port_busy_us(s->net, s->dev[dev].node, p);
        if (b > busy) busy = b;
    }
    return (int)((busy * 100) / window);
}

/* AND HEAT IS THE THIRD TERM, added in D28.
 *
 * A disk in a cupboard that is over what it can shed is a disk running above
 * its rated ambient, and the one thing every field study of the things agrees
 * on is that heat is what kills them. So a box in a hot room ages faster:
 * one extra point at the warning line, and another for every forty per cent
 * over it.
 *
 * This is here for a reason beyond realism. The same playtester who wanted
 * the blackout to be several diagnoses also said days 0 to 25 were "admin" --
 * and the world's only early lever was the heat warning, which said a thing
 * and then did nothing for three days and often nothing ever, because a room
 * at 110% warns and never trips. Now it costs something from the first day it
 * is over: not a machine down, but a disk that will reach its warning weeks
 * early and start naming itself in `events`. A small consequence, soon, out
 * of a decision the player made when they chose which room to put it in. */
static void age_the_kit(Site *s)
{
    for (int i = 0; i < s->ndev; i++) {
        SiteDev *d = &s->dev[i];
        if (!site_kind_has_os(d->kind) || !d->powered) continue;
        d->run_days++;
        d->wear += 1 + used_pct(s, i) / 25;      /* one to five             */
        int heat = site_room_heat(s, d->room);
        if (heat >= HEAT_WARN) d->wear += 1 + (heat - HEAT_WARN) / 40;
    }
}

/* HOW MANY COMPLAINTS THE LANDLORD WILL WEAR, in one place because two
 * places is how the number a player reads stops being the number that ends
 * their run. */
int site_complaints_allowed(const Site *s)
{
    int in = 0;
    for (int i = 0; i < s->ntenant; i++) if (s->tenant[i].moved) in++;
    int bear = (in + 2) / 3;
    return bear < 3 ? 3 : bear;
}

/* ------------------------------------------------------------ a blackout */
/* HOW THE CASUALTIES ARE DEALT, and this is the whole of D28.
 *
 * A playtester met a mains failure on day 30 that took three servers down and
 * called it *"the single event worth the preceding twenty-five days"* -- and
 * then said exactly what was wrong with it:
 *
 *   "Three servers down from one cause was three instances of the same
 *   puzzle. Three servers down where one is a heat trip, one is a worn disk
 *   and one is a truncated fstab would have been the game this engine is
 *   obviously capable of."
 *
 * They were right. Every box the mains took down was handed the same
 * fault_unclean_shutdown, so the second and third repair were the first one
 * typed again with a different hostname.
 *
 * WHY ROUND-ROBIN AND NOT A DIE PER BOX. A die is what produced the complaint
 * in the first place: three independent draws out of three casualties deal the
 * same one to all three boxes about one morning in nine, and two the same
 * about half the time. What is physically true is not that each machine rolls
 * -- it is that different machines were doing different things at 04:12 -- so
 * the deal guarantees the difference and the SEED decides where the deal
 * starts and which file inside each casualty goes. A blackout across three
 * working servers is three different mornings, always; which server gets which
 * morning is the seed's business and is written down nowhere the player can
 * read it.
 *
 * A BOX THAT DID NO WORK YESTERDAY IS NOT DEALT ONE. It had nothing in flight,
 * so it comes back dirty and complete: fsck, clean, up. That is not a gap in
 * the model, it is the control -- the machine that proves the player's move is
 * to READ the tools rather than reinstall everything on every box. */
static int pf_deal(Site *s, Rng *rng, int *seq)
{
    int nk = breaker_powerfail_kinds();      /* PF_CLEAN and the casualties  */
    if (nk < 2) return PF_CLEAN;
    if (*seq < 0) *seq = (int)rng_range(rng, 0, (unsigned)(nk - 2));
    int k = PF_CLEAN + 1 + (*seq % (nk - 1));
    (*seq)++;
    return k;
}

static void the_mains_fails(Site *s, Rng *rng)
{
    ev_add(s, SEV_POWERCUT, -1,
           "the building lost mains power at 04:12 and had it back by 04:31.");
    int seq = -1;
    for (int i = 0; i < s->ndev; i++) {
        SiteDev *d = &s->dev[i];
        if (!site_kind_has_os(d->kind) || !d->powered) continue;
        Machine *m = box_of_dev(s, i);
        if (d->ups) {
            /* IT RODE IT OUT, and it says so in its own log. This is the
             * receipt for the two hundred and twenty pounds. */
            if (m) {
                breaker_syslog(m, "nomups: utility power lost -- load transferred to battery");
                breaker_syslog(m, "nomups: on battery, 19 min runtime remaining");
                breaker_syslog(m, "nomups: utility power restored -- back on mains");
            }
            ev_add(s, SEV_UPS_HELD, i,
                   "%s was on a battery and stayed up.", d->name);
            continue;
        }
        /* IT WENT DOWN THE WAY A MACHINE GOES DOWN WHEN THE PLUG IS PULLED.
         * Whether it lost anything depends on whether it was writing, and
         * "was it writing" is not a die roll: it is whether this box moved
         * frames in the busy period that has just finished. */
        bool writing = used_pct(s, i) > 0;
        int kind = writing ? pf_deal(s, rng, &seq) : PF_CLEAN;
        char note[200] = "";
        if (m) breaker_powerfail_as(m, rng, kind, note, sizeof note);
        site_power(s, i, false);
        ev_add(s, SEV_DOWN_DIRTY, i,
               "%s went down with the power and has not been switched back on.",
               d->name);
        (void)note;
    }
}

/* --------------------------------------------------------- a dying disk */
static void the_disks(Site *s, Rng *rng)
{
    for (int i = 0; i < s->ndev; i++) {
        SiteDev *d = &s->dev[i];
        if (!site_kind_has_os(d->kind) || !d->powered) continue;
        Machine *m = box_of_dev(s, i);
        if (d->wear >= WEAR_FAIL) {
            char note[200] = "";
            /* THE SECOND SECTOR IS ALLOWED WHAT THE FIRST WAS NOT.
             *
             * The first loss is kept off the files the boot chain reads, so
             * the box comes up and the player can get a shell on it and work
             * out what happened. By the time a disk loses a SECOND one it has
             * logged SMART warnings for a fortnight, taken a file, been named
             * in `events`, and `disk <box>` has cost a hundred and forty
             * pounds the whole while. So it takes /etc/fstab this time, the
             * boot stops at the stage that is really wrong, and the repair is
             * the one the boot log names. That is D23's *a disk nobody
             * replaced -> the truncated file the boot log names*, and the
             * warning it escalates from is fifteen days long. */
            bool boot_too = d->lost > 0;
            if (m && breaker_bad_sector_any(m, rng, boot_too, note, sizeof note)) {
                d->lost = d->lost < 255 ? d->lost + 1 : 255;
                breaker_syslog(m, "kernel: sd 0:0:0:0: [sda] "
                                  "UNRECOVERED READ ERROR - auto reallocate failed");
                breaker_syslog(m, "kernel: end_request: critical medium error, "
                                  "dev sda, sector 1841776");
                breaker_syslog(m, "kernel: sd 0:0:0:0: [sda] "
                                  "SMART attribute 5 (reallocated) at threshold; "
                                  "no spare sectors left");
                if (boot_too)
                    ev_add(s, SEV_DISK_BOOT, i,
                           "the disk in %s has lost another sector, its %d"
                           "%s, and it was under something the boot reads.",
                           d->name, d->lost,
                           d->lost == 2 ? "nd" : d->lost == 3 ? "rd" : "th");
                else
                ev_add(s, SEV_DISK_FAIL, i,
                       "the disk in %s lost a sector after %d days. It had been "
                       "warning.", d->name, d->run_days);
                /* It has lost what it was going to lose. The disk keeps
                 * running -- and keeps being a disk that has run out of
                 * spares, which is why the wear does not reset until
                 * somebody puts a new one in. */
                d->wear = WEAR_WARN;
            }
            continue;
        }
        if (d->wear >= WEAR_WARN) {
            int bad = 3 + (d->wear - WEAR_WARN) * 4;
            char line[160];
            snprintf(line, sizeof line,
                     "kernel: sd 0:0:0:0: [sda] SMART attribute 5 (reallocated "
                     "sector count) is %d and rising; %d power-on days",
                     bad, d->run_days);
            if (m) breaker_syslog(m, line);
            if (!d->warned) {
                d->warned = 1;
                ev_add(s, SEV_DISK_WARN, i,
                       "%s is logging reallocated sectors. Its disk is going.",
                       d->name);
            }
        }
    }
}

/* ------------------------------------------------------------- a cupboard */
static void the_heat(Site *s, Rng *rng)
{
    int seq = -1;
    for (int i = 0; i < s->ndev; i++) {
        SiteDev *d = &s->dev[i];
        if (!site_kind_has_os(d->kind) || !d->powered) continue;
        int heat = site_room_heat(s, d->room);
        if (heat < HEAT_WARN) { d->hot_warned = 0; continue; }
        Machine *m = box_of_dev(s, i);
        if (d->hot_warned < 255) d->hot_warned++;
        char line[160];
        snprintf(line, sizeof line,
                 "kernel: thermal: sensor 0 (intake) at %d C, above the 40 C "
                 "trip point -- the air in this room is not being changed",
                 22 + heat / 5);
        if (m) breaker_syslog(m, line);
        if (d->hot_warned == 1)
            ev_add(s, SEV_HEAT_WARN, i,
                   "%s is running hot: %d W of kit in a room that can shed %d W.",
                   d->name, site_room_watts(s, d->room),
                   site_room_capacity(s, d->room));
        if (heat >= HEAT_TRIP && d->hot_warned >= HEAT_DAYS) {
            /* IT SHUT ITSELF DOWN, which is what thermal protection is for --
             * and a machine that stops dead mid-write is the same unclean
             * shutdown a blackout leaves, because it is the same event. */
            char note[200] = "";
            /* AND IT IS DEALT A CASUALTY LIKE ANY OTHER MACHINE THAT STOPS
             * DEAD. Two boxes in the same cupboard tripping on the same
             * afternoon used to come back with the same fault for the same
             * reason the blackout did; they do not now. */
            int kind = used_pct(s, i) > 0 ? pf_deal(s, rng, &seq) : PF_CLEAN;
            if (m) {
                breaker_syslog(m, "kernel: thermal: CRITICAL trip point reached, "
                                  "shutting down");
                breaker_powerfail_as(m, rng, kind, note, sizeof note);
            }
            site_power(s, i, false);
            d->hot_warned = 0;
            ev_add(s, SEV_HEAT_TRIP, i,
                   "%s shut itself down on temperature. There is too much kit "
                   "in that room.", d->name);
            (void)note;
        }
    }
}

/* ------------------------------------------------------- a marginal run */
/* D23, in its own words: *a cable run past interference -> errors that only
 * appear under traffic.* This is that, and it is the one cause in this file
 * whose evidence is not on a disk.
 *
 * WHERE THE NUMBER COMES FROM. Copper of every grade in this game carries a
 * hundred metres, and the netstack stops carrying anything at all past it. The
 * last ten of those metres are what the standard spends on margin -- a channel
 * that long meets its loss and crosstalk budget with nothing left over, and
 * what it does when it runs out of budget is not fail: it takes CRC errors,
 * retrains, and settles at the next speed down. Every network cupboard in the
 * world has one of these in it.
 *
 * WHY IT ONLY BITES UNDER TRAFFIC, and this is the part that makes it a
 * decision rather than a tax. `errs` is the share of the busy period the port
 * really spent clocking bits -- the same counter `load` prints -- multiplied
 * by how far past the margin the run is. Both terms matter and the numbers
 * say why:
 *
 *   a 95 m drop to ONE desk       ~2% busy   x6   =   12 a day: 50 days to a
 *                                                     word, 150 to a retrain.
 *                                                     A desk pulls ten
 *                                                     megabits and a long run
 *                                                     to one is a bad idea
 *                                                     that takes half a year
 *                                                     to become a problem,
 *                                                     which is honest.
 *   a 95 m uplink under a FLOOR   ~30% busy  x6   =  180 a day: a word on the
 *                                                     fourth day, a hundred
 *                                                     megabits on the tenth.
 *
 * The second one is the shape of a real mistake -- the switch put in the
 * office with the desks rather than in the comms cupboard, home-run to the
 * basement in copper -- and in this tower it measures 95 m, which is a number
 * out of the building generator and not one anybody chose. The tenancies
 * behind it are then on a hundred megabits, which --loadcheck has asserted
 * since D25 is not enough for two floors of desks.
 *
 * FOUND WITH: `events` names the day and both ends. `load` prints the port at
 * 100Mb where it used to print 1000Mb, `show <box>` prints the same beside
 * the drops, and on a box with an operating system in it `netstat -P` reads
 * the CRC counter off its own kernel. The machines at each end log the errors
 * in their own /var/log/messages for days before anything changes speed.
 *
 * AVOIDED BY: not running ninety metres of copper under a floor. The game
 * charges by the metre and prints the length at the moment the money leaves,
 * so the player was told. Fibre runs two kilometres and has no such budget.
 *
 * FIXED BY: `uncable` and pull it again -- shorter, or in fibre. The rates are
 * reapplied from the catalogue every day for every live run, so a fresh run
 * comes up at the speed the kit at each end can do, and a run that is still
 * the marginal one is still slow. Nothing is remembered about a port; the
 * memory is in the run. */
#define COPPER_MARGIN_M   90   /* metres, of a hundred the copper will carry  */
#define LINK_ERR_WARN    600   /* busy-per-cent x metres over, before a word  */
#define LINK_ERR_SLOW   1800   /* and before the phy gives up and retrains    */
#define LINK_SLOW_MB     100

static int link_port_rate(const Site *s, int dev, int port, bool slow)
{
    /* The handoff's port 0 is the circuit and its rate is what the landlord
     * bought, not what the socket can do. Everything else is the catalogue. */
    if (dev == s->uplink && port == 0) return s->isp_mb;
    int mb = site_kind_port_mb(s->dev[dev].kind, port);
    return (slow && mb > LINK_SLOW_MB) ? LINK_SLOW_MB : mb;
}

static int link_used_pct(const Site *s, const SiteLink *l)
{
    uint64_t window = SITE_BUSY_MS * 1000ull;
    uint64_t a = net_port_busy_us(s->net, s->dev[l->a].node, l->aport);
    uint64_t b = net_port_busy_us(s->net, s->dev[l->b].node, l->bport);
    uint64_t busy = a > b ? a : b;
    return (int)((busy * 100) / window);
}

static void the_copper(Site *s)
{
    for (int i = 0; i < s->nlink; i++) {
        SiteLink *l = &s->link[i];
        if (l->cable < 0) continue;
        net_port_rate(s->net, s->dev[l->a].node, l->aport,
                      link_port_rate(s, l->a, l->aport, l->slow));
        net_port_rate(s->net, s->dev[l->b].node, l->bport,
                      link_port_rate(s, l->b, l->bport, l->slow));
        if (l->kind == CAB_FIBRE || l->metres < COPPER_MARGIN_M) continue;
        if (l->slow) continue;
        int used = link_used_pct(s, l);
        if (used <= 0) continue;
        /* How far past the margin, in metres, one for one -- and capped, so
         * that a run at the very edge of what copper carries is ten times as
         * bad as one a metre over and not a hundred times. */
        int over = l->metres - (COPPER_MARGIN_M - 1);
        if (over < 1) over = 1;
        if (over > 10) over = 10;
        int was = l->errs;
        l->errs += used * over;
        char line[180];
        snprintf(line, sizeof line,
                 "kernel: eth: %d CRC errors and %d symbol errors in 24h -- "
                 "the link partner retrained twice", l->errs * 7, l->errs);
        for (int e = 0; e < 2; e++) {
            int dv = e ? l->b : l->a;
            if (!site_kind_has_os(s->dev[dv].kind) || !s->dev[dv].powered) continue;
            Machine *m = box_of_dev(s, dv);
            if (m) breaker_syslog(m, line);
        }
        if (was < LINK_ERR_WARN && l->errs >= LINK_ERR_WARN)
            ev_add(s, SEV_LINK_WARN, l->a,
                   "the %d m %s run from %s:%d to %s:%d is taking errors "
                   "under load.", l->metres, site_cable_name((CableKind)l->kind),
                   s->dev[l->a].name, l->aport, s->dev[l->b].name, l->bport);
        if (l->errs >= LINK_ERR_SLOW) {
            l->slow = 1;
            net_port_rate(s->net, s->dev[l->a].node, l->aport,
                          link_port_rate(s, l->a, l->aport, true));
            net_port_rate(s->net, s->dev[l->b].node, l->bport,
                          link_port_rate(s, l->b, l->bport, true));
            ev_add(s, SEV_LINK_SLOW, l->a,
                   "run %d, %s:%d to %s:%d, has retrained to %d Mb: %d m is "
                   "too far for %s.", i, s->dev[l->a].name, l->aport,
                   s->dev[l->b].name, l->bport, LINK_SLOW_MB, l->metres,
                   site_cable_name((CableKind)l->kind));
        }
    }
}

/* Everything the world did today, in the order it would have happened: the
 * kit ages on the day's own traffic, the copper takes its errors while the
 * traffic is on it, the heat builds through the working day, the disks fail
 * when they fail, and the mains goes in the small hours. */
static void the_weather(Site *s)
{
    Rng rng;
    rng_seed(&rng, s->seed ^ (0x77e47ull * (uint64_t)s->day) ^ 0xbeef01ull);
    age_the_kit(s);
    the_copper(s);
    the_heat(s, &rng);
    the_disks(s, &rng);
    if (site_mains_fails_on(s->seed, s->day)) the_mains_fails(s, &rng);
}

/* ------------------------------------------------------------ the report */
void site_dump_events(const Site *s, Buf *out)
{
    if (!s->nev) {
        buf_puts(out, "nothing has happened to the building yet.\n");
    } else {
        buf_printf(out, "  what the world has done (%d in all, newest last)\n",
                   s->ev_total);
        for (int i = 0; i < s->nev; i++)
            buf_printf(out, "  day %-4d %s\n", s->ev[i].day, s->ev[i].what);
    }
    buf_puts(out, "\n  box            days  disk   ups   room heat\n");
    int shown = 0;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (!site_kind_has_os(d->kind)) continue;
        int pct = d->wear * 100 / WEAR_FAIL;
        buf_printf(out, "  %-14s %4d  %3d%%  %-4s  %7d%%\n", d->name, d->run_days,
                   pct > 100 ? 100 : pct, d->ups ? "yes" : "no",
                   site_room_heat(s, d->room));
        shown++;
    }
    if (!shown) buf_puts(out, "  no box in this building has an operating "
                              "system in it yet.\n");
    buf_puts(out,
        /* WHAT THIS SENTENCE USED TO CLAIM. "worked out from how hard it has
         * actually been used" -- which is half the model. There is a flat
         * charge for every day the box is switched on, and a playtester who
         * read this left seven servers running through a forty-eight day
         * stretch with no tenants in the building, watched them go from 0% to
         * 33% on no traffic at all, and had to replace five disks. Their
         * words: "it isn't". Both terms are now named, with the ratio between
         * them, because the decision the sentence is for -- leave it running
         * or switch it off -- turns entirely on the flat one. */
        "\n  disk is how far through its life the disk in that box is. A disk\n"
        "  ages every day it is SWITCHED ON, and faster on the days it works\n"
        "  hard: a box at full load wears about five times as fast as one\n"
        "  that is merely powered. Kit you have built ahead of the tenants is\n"
        "  ageing in the cupboard. It also ages faster in a room that is over\n"
        "  what it can shed, which is the heat column below. Past about three\n"
        "  quarters it starts saying so in its own /var/log/messages, and a\n"
        "  new one is `disk <box>`. `ups <box>` fits a battery: a box on one\n"
        "  rides a mains failure out instead of coming back with a filesystem\n"
        "  to check.\n"
        "  A DISK THAT LOSES A SECTOR AND IS NOT REPLACED LOSES ANOTHER. The\n"
        "  first one is kept off the files the boot chain reads, so the box\n"
        "  still comes up and can be worked on. The second one is not.\n"
        "\n"
        "  heat is the watts of kit in that box's room against what the room\n"
        "  can shed -- a cupboard sheds what its walls and its door will take\n"
        "  and no more, and a server room has cooling in it. Past a hundred\n"
        "  per cent the kit starts saying so, and its disk starts ageing\n"
        "  faster; the fix is to carry some of it somewhere with more air in\n"
        "  it.\n"
        "\n"
        );
    /* THE NUMBER COMES FROM THE CONSTANT THAT ENFORCES IT. It was spelled
     * "ninety metres" in the prose beside a `#define COPPER_MARGIN_M 90`,
     * which is two places for one fact and therefore one place for it to
     * drift -- the same shape as the `demand` footer that told a player a
     * switch24 seats 23 desks after the catalogue had made it 22. */
    buf_printf(out,
        "\n  AND THE COPPER. A run over %d metres is inside what the cable\n"
        "  carries and outside what it carries with any margin: under real\n"
        "  traffic it takes errors, says so here for days, and then retrains\n"
        "  itself down to a hundred megabits. `load` prints the speed the port\n"
        "  really clocks. The fix is `uncable` and a shorter run, or fibre.\n",
        COPPER_MARGIN_M);
}

/* ================================================================== a day */
bool site_day(Site *s, SiteDay *rep)
{
    SiteDay r;
    memset(&r, 0, sizeof r);
    if (s->over) { if (rep) *rep = s->last; return false; }
    s->day++;
    r.day = s->day;

    /* ---------------------------------------------------- who moves in */
    for (int i = 0; i < s->ntenant; i++)
        if (!s->tenant[i].moved && s->tenant[i].day <= s->day) move_in(s, i);

    /* ------------------------------------- the computers ask for addresses
     * A desk that has just been plugged in does what a computer does: it
     * asks. If nobody is running a DHCP server on the segment it landed in,
     * it gets nothing, and a machine with no address is a person with no
     * network -- which is a real fault with a real diagnosis and not a flag
     * anybody set. */
    for (int i = 0; i < s->ntenant; i++) {
        SiteTenant *t = &s->tenant[i];
        if (!t->moved) continue;
        for (int j = 0; j < t->ndesk; j++) {
            int d = t->desk0 + j;
            if (net_port_state(s->net, s->dev[d].node, 0) != PORT_UP) continue;
            if (net_if_get_addr(s->net, s->dev[d].node, 0)) continue;
            net_dhcp_client(s->net, s->dev[d].node, 0);
        }
    }

    /* ------------------------------------------------- build the day's work */
    int cap = 0;
    for (int i = 0; i < s->ntenant; i++) if (s->tenant[i].moved) cap += s->tenant[i].ndesk;
    /* The page and the files, all at the same time: see SITE_DESK_FILES. */
    cap *= 1 + SITE_DESK_FILES;
    Xfer *xs = cap ? (Xfer *)nom_alloc(sizeof(Xfer) * (size_t)cap) : NULL;
    int nx = 0;

    Rng rng;
    rng_seed(&rng, s->seed ^ (0x0d0a17ull * (uint64_t)s->day));

    uint32_t web = net_if_get_addr(s->net, s->dev[s->uplink].node, 0);
    /* The internet's own web server answers on the handoff's address in this
     * world; net_sites.c is the content and site_new put it there. */
    for (int i = 0; i < s->ntenant; i++) {
        SiteTenant *t = &s->tenant[i];
        t->tried = t->finished = t->worst_ms = 0;
        t->bytes = 0;
        t->files_dev = -1;
        if (!t->moved) continue;
        r.tenants_in++;
        r.desks += t->ndesk;
        int fsd = file_server_for(s, i);
        t->files_dev = fsd;
        for (int j = 0; j < t->ndesk && nx + SITE_DESK_FILES < cap; j++) {
            int d = t->desk0 + j;
            if (net_port_state(s->net, s->dev[d].node, 0) != PORT_UP) continue;
            if (!net_if_get_addr(s->net, s->dev[d].node, 0)) continue;
            /* Per desk, because which of the server's legs answers depends on
             * which segment the asker is standing in. */
            uint32_t files = fsd >= 0 ? server_addr_for(s, fsd, d) : 0;
            r.connected++;
            /* WHEN THEY START. Spread across the first tenth of the busy
             * period, from the seed, because a building does not begin work
             * on the same millisecond and a thundering herd would be a
             * difficulty knob rather than a day. Both of this desk's
             * transfers start together, because they are one person sitting
             * down and one machine waking up. */
            int begins = (int)rng_range(&rng, 0, SITE_BUSY_MS / 10);
            Xfer *x = &xs[nx++];            /* the page, off the internet    */
            memset(x, 0, sizeof *x);
            x->dev = d; x->tenant = i; x->sock = -1; x->leg = 0;
            x->kb = SITE_DESK_WEB_KB;
            x->want = (long)SITE_DESK_WEB_KB * 1024;
            x->dst = web;
            x->start = begins;
            /* The files, off the nearest server, all open together -- one
             * person with more than one thing on the go, which is what a
             * desk is. They start on the same millisecond as the page for
             * the same reason the page does: one person sitting down. */
            for (int k = 0; k < SITE_DESK_FILES; k++) {
                Xfer *y = &xs[nx++];
                memset(y, 0, sizeof *y);
                y->dev = d; y->tenant = i; y->sock = -1; y->leg = 1;
                y->kb = SITE_DESK_FILE_KB;
                y->want = (long)SITE_DESK_FILE_KB * 1024;
                y->dst = files ? files : web;
                y->start = begins;
            }
        }
    }

    /* ------------------------------------------------------- the resolver
     * One real DNS query per tenancy, from their first desk, to whatever
     * resolver their lease gave them. A tenancy whose resolver does not
     * answer cannot find anything, and that is diagnosed with `resolve`. */
    for (int i = 0; i < s->ntenant; i++) {
        SiteTenant *t = &s->tenant[i];
        if (!t->moved || !t->ndesk) continue;
        int d = t->desk0;
        if (net_port_state(s->net, s->dev[d].node, 0) != PORT_UP) continue;
        if (!net_if_get_addr(s->net, s->dev[d].node, 0)) continue;
        uint32_t a = 0;
        net_resolve(s->net, s->dev[d].node, "news.nom", &a);
    }

    /* ------------------------------------------------- reset the meters
     * Utilisation is measured over THIS busy period, so the counters that
     * measure it start here. The lifetime tx/rx/drop counters on a port are
     * not touched: those are what a player reads with `show <box>` -- or with
     * `netstat -P` where the box has a shell to type it into -- and they
     * are cumulative, exactly as they are on a real switch. */
    uint64_t t0 = net_now(s->net);
    for (int i = 0; i < s->ndev; i++)
        for (int p = 0; p < s->dev[i].nports; p++)
            net_port_busy_reset(s->net, s->dev[i].node, p);
    uint64_t frames0 = 0, drops0 = 0;
    for (int i = 0; i < s->ndev; i++)
        for (int p = 0; p < s->dev[i].nports; p++) {
            frames0 += net_port_rx(s->net, s->dev[i].node, p);
            drops0  += net_port_drops(s->net, s->dev[i].node, p);
        }

    /* --------------------------------------------------- the busy period */
    for (int tick = 0; tick < SITE_BUSY_MS; tick++) {
        for (int i = 0; i < nx; i++) {
            Xfer *x = &xs[i];
            if (x->state == X_WAIT) {
                if (tick >= x->start) xfer_begin(s, x, tick);
            } else if (x->state == X_CONNECT || x->state == X_RECV) {
                xfer_poll(s, x, tick);
            }
        }
        net_step(s->net, 1);
    }

    /* ------------------------------------------------------- what happened */
    for (int i = 0; i < nx; i++) {
        Xfer *x = &xs[i];
        SiteTenant *t = &s->tenant[x->tenant];
        if (x->state == X_DONE) {
            t->finished++;
            t->bytes += x->got;
            r.finished++;
            r.bytes += x->got;
            int ms = x->ended - x->began;
            if (ms > t->worst_ms) t->worst_ms = ms;
        } else if (x->sock >= 0) {
            net_tcp_close(s->net, x->sock);
            net_sock_free(s->net, x->sock);
        }
        /* Two things this person's machine was asked to do today, counted
         * whichever way they went. A page that never arrived used to take
         * the file with it and be counted once; both are counted now. */
        t->tried++; r.sessions++;
        if (t->worst_ms > r.worst_ms) r.worst_ms = t->worst_ms;
    }
    /* Anything still half open at the end of the day is closed, because the
     * next busy period is a different day and the sockets are a pool. */
    for (int i = 0; i < s->ndev; i++)
        if (s->dev[i].kind == SDEV_DESK) net_close_all(s->net, s->dev[i].node);
    net_step(s->net, 5);
    /* AND EVERYBODY WENT HOME. Whatever was still half open at either end is
     * gone: the servers' side of a transfer nobody finished, the handshakes
     * that never completed, the teardowns that are still waiting for an ACK
     * that is not coming. Without this a bad day leaves its wreckage in the
     * socket pool and the NEXT day cannot open a connection at all -- which
     * looks like a network that has died and is only a leak. */
    net_tcp_reap(s->net, 1);
    nom_free(xs);

    for (int i = 0; i < s->ndev; i++)
        for (int p = 0; p < s->dev[i].nports; p++) {
            r.frames += net_port_rx(s->net, s->dev[i].node, p);
            r.drops  += net_port_drops(s->net, s->dev[i].node, p);
        }
    r.frames -= frames0;
    r.drops  -= drops0;
    hottest_port(s, (net_now(s->net) - t0) * 1000ull, &r);

    /* ------------------------------------------------- the rent, and the bill
     * A tenancy is SERVED on a day when four fifths of their people got
     * their work done. That is the whole rule, and the fraction is the only
     * judgement in it: below that a floor is visibly not working and above
     * it a slow day is a slow day. Rent is a thirtieth of a month, for a day
     * that worked, because nobody pays for a day the network did not give
     * them.
     *
     * A TENANCY WITH NOBODY PLUGGED IN IS NOT WAITING PATIENTLY, and this
     * file used to say it was. The rule read: strikes only start once
     * somebody has been connected, so *"a complaint is always about service
     * that got worse"*. It sounds careful and it made the whole service half
     * of the game optional -- an agent ran two hundred days with SEVEN
     * tenancies moved in and unserved and never drew a single complaint, and
     * had to go overdrawn on purpose to see the run end at all. Every ounce
     * of pressure in the game was money.
     *
     * A tenancy that has moved in has signed a lease. The desks are in the
     * room. Nobody promised them a slow network and nobody promised them no
     * network either, and of the two the second is worse. So they get a
     * FIT-OUT WINDOW -- SITE_FITOUT_DAYS, the few days in which a new tenant
     * expects to be finding the kettle rather than the file server -- and
     * after that a day with not one desk able to work is a strike like any
     * other. Three of those is a complaint, so ignoring a tenancy costs the
     * player a complaint on the sixth day after they moved in, and three
     * ignored tenancies end the run exactly as three badly served ones do.
     *
     * Note what is still true: a tenancy the schedule has not brought in yet
     * cannot strike, and a day the player fixes it resets the count. */
    for (int i = 0; i < s->ntenant; i++) {
        SiteTenant *t = &s->tenant[i];
        if (!t->moved) continue;
        bool served = t->tried > 0 && t->finished * 5 >= t->tried * 4;
        bool ignored = t->tried == 0 && t->strikes == 0 &&
                       s->day - t->day > SITE_FITOUT_DAYS;
        if (served) {
            r.tenants_served++;
            long day_rent = t->rent / 30;
            s->money += day_rent;
            s->rent_taken += day_rent;
            r.rent += day_rent;
            t->strikes = 0;
        } else if (t->tried > 0 || t->strikes > 0 || ignored) {
            /* They have people plugged in and the work is not finishing --
             * or they have people and no ports at all, which is the same
             * complaint with a shorter phone call. */
            if (t->strikes < 255) t->strikes++;
            if (t->strikes >= 3 && !t->complained) {
                t->complained = 1;
                s->complaints++;
                r.complaints_today++;
            }
        }
    }

    /* ------------------------------------------------------- and the bill
     * The circuit is a standing charge and it lands on the thirtieth day,
     * whatever the network did with it. It is taken AFTER the rent, so a
     * month that just about paid for itself reads in that order, and it is
     * `spent` like everything else the player bought. */
    if (s->day % SITE_MONTH_DAYS == 0 && s->isp_mb > 0) {
        r.bill = site_isp_price(s->isp_mb);
        s->money -= r.bill;
        s->spent += r.bill;
    }

    /* ----------------------------------------------------------- and then
     * THE WORLD HAPPENS, and it happens AFTER the day's work rather than
     * before it. A blackout is a thing that occurs in the small hours: the
     * building has already done its day, the player meets the mess in the
     * morning, and has the next day to put it right before anybody's work
     * suffers for it. Running it before the busy period would mean a mains
     * failure took every tenancy's day with it whatever the player did, and
     * a disaster nobody can react to is a tax rather than a game. */
    int ev0 = s->ev_total;
    the_weather(s);
    r.events = s->ev_total - ev0;

    /* -------------------------------------------------------- and the end */
    /* HOW MANY COMPLAINTS THE LANDLORD WILL WEAR, and it is not a constant.
     *
     * It was three, whatever the size of the building. That was right when a
     * tower held three tenancies and it stopped being right the moment D27's
     * letting queue started signing thirteen leases by day sixty: three of
     * three is a building nobody can work in, and three of thirteen is
     * seventy-seven per cent of your tenants perfectly happy. A landlord does
     * not lose the freehold over that, and a game should not get more brittle
     * the better you are doing at the thing it asked you to do.
     *
     * It also mattered more than it looks, because tenancies fail together.
     * One overheated server takes a floor's worth of them out on the same
     * morning, so a flat three meant a single event that a big tower ought to
     * absorb ended the run in the three days it takes strikes to mature --
     * measured on seed 42, where a competent build died on day 20 from one
     * heat trip on day 17.
     *
     * A third of the building, rounded up, never fewer than three. Nine
     * tenancies still ends at three, so nothing before that point moves and
     * the difficulty curve --loadcheck asserts is untouched. */
    int in = 0;
    for (int i = 0; i < s->ntenant; i++) if (s->tenant[i].moved) in++;
    int bear = site_complaints_allowed(s);
    if (s->complaints >= bear) {
        s->over = 1;
        snprintf(s->over_why, sizeof s->over_why,
                 "%d of your %d tenancies have filed a complaint. The lease is "
                 "not renewed.", s->complaints, in);
    } else if (s->money < 0) {
        s->over = 1;
        snprintf(s->over_why, sizeof s->over_why,
                 "the account is %ld overdrawn and no rent is coming in.", -s->money);
    }
    s->last = r;
    if (rep) *rep = r;
    return !s->over;
}

bool site_advance(Site *s, int days, Buf *out)
{
    for (int i = 0; i < days; i++) {
        SiteDay r;
        if (s->over) {
            if (out) buf_printf(out, "the run ended on day %d: %s\n",
                                s->day, s->over_why);
            return false;
        }
        bool alive = site_day(s, &r);
        if (out) {
            /* "DESKS UP" AND `service`'s "up" WERE DIFFERENT NUMBERS.
             * Consecutive commands answered "0/20 desks up" and "up 20", and
             * both were right: this line counts the desks that did any work,
             * which means a port with link on it AND an address on the card,
             * while `service` prints link and address in separate columns.
             * One of them had to say which it meant. */
            buf_printf(out, "day %d: %d in, %d served, %d/%d desks addressed, "
                            "%d/%d transfers finished, %ld taken, %ld in hand\n",
                       r.day, r.tenants_in, r.tenants_served, r.connected,
                       r.desks, r.finished, r.sessions, r.rent, s->money);
            if (r.hot[0])
                buf_printf(out, "        busiest port %s at %d%%%s\n", r.hot,
                           r.hot_util,
                           r.drops ? "; something is dropping -- `load`, then "
                                     "`show <box>`" : "");
            if (r.bill)
                buf_printf(out, "        the ISP bills the month: %ld for the "
                                "%d Mb circuit. %ld in hand\n",
                           r.bill, s->isp_mb, s->money);
            if (r.complaints_today)
                buf_printf(out, "        %d COMPLAINT%s filed today (%d in all)\n",
                           r.complaints_today, r.complaints_today == 1 ? "" : "S",
                           s->complaints);
            /* AND WHAT THE WORLD DID, said on the day it happened. A player
             * who advances ten days and is never told the lights went out on
             * the sixth has been handed a mystery rather than a fault. */
            for (int k = s->nev - r.events; k >= 0 && k < s->nev; k++)
                buf_printf(out, "        ** %s\n", s->ev[k].what);
        }
        if (!alive) {
            if (out) buf_printf(out, "\nTHE RUN IS OVER on day %d: %s\n",
                                s->day, s->over_why);
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------ inspection */
void site_dump_day(const Site *s, Buf *out)
{
    const SiteDay *r = &s->last;
    buf_printf(out, "day %d. %ld in hand, %ld spent, %ld taken in rent.\n",
               s->day, s->money, s->spent, s->rent_taken);
    buf_printf(out, "the circuit is %d Mb (%ld a month, next billed in %d "
                    "day%s).\n", s->isp_mb, site_isp_price(s->isp_mb),
               site_isp_days_to_bill(s),
               site_isp_days_to_bill(s) == 1 ? "" : "s");
    if (!r->day) {
        buf_puts(out, "no day has been run yet: `day` advances the clock.\n");
        return;
    }
    buf_printf(out, "%d tenancies in, %d of them served yesterday. "
                    "%d of %d desks have a live port AND an address.\n",
               r->tenants_in, r->tenants_served, r->connected, r->desks);
    buf_printf(out, "%d of %d transfers finished inside the busy period; "
                    "%ld MB moved.\n", r->finished, r->sessions,
               r->bytes / (1024 * 1024));
    /* SAY WHICH DAY THESE ARE. `status` reports the day just gone and `load`
     * reports the life of the port, and a playtester found them disagreeing
     * -- 3590 against 12429 at the same moment -- with nothing anywhere
     * saying one was a delta and the other a total. Two true numbers that
     * look like they should match are worse than one. */
    buf_printf(out, "%llu frames handled yesterday, %llu lost yesterday.\n",
               (unsigned long long)r->frames, (unsigned long long)r->drops);
    if (r->hot[0])
        buf_printf(out, "busiest port: %s, clocking %d%% of the busy period.\n",
                   r->hot, r->hot_util);
    buf_printf(out, "%d complaint%s filed in all. Three ends the run.\n",
               s->complaints, s->complaints == 1 ? "" : "s");
    if (s->over) buf_printf(out, "\nTHE RUN IS OVER: %s\n", s->over_why);
}

void site_dump_service(const Site *s, Buf *out)
{
    buf_puts(out, "  floor tenant  desks   up  addr   done  worst   strikes  rent/day  files\n");
    bool offfloor = false;
    for (int i = 0; i < s->ntenant; i++) {
        const SiteTenant *t = &s->tenant[i];
        if (!t->moved) continue;
        int up = site_tenant_connected(s, i), ad = site_tenant_addressed(s, i);
        char done[16];
        if (t->tried) snprintf(done, sizeof done, "%d/%d", t->finished, t->tried);
        else snprintf(done, sizeof done, "-");
        /* Whose server their people actually pulled off. A tenancy being
         * served from another floor is not an error and is not refused --
         * it is the naive build working, right up until the riser fills --
         * so it is reported rather than prevented. */
        char files[NET_NAME_MAX + 8];
        if (t->files_dev < 0) snprintf(files, sizeof files, "%s", "none");
        else {
            const SiteDev *fd = &s->dev[t->files_dev];
            bool away = fd->floor != t->floor;
            snprintf(files, sizeof files, "%s%s", fd->name, away ? " <-" : "");
            if (away) offfloor = true;
        }
        buf_printf(out, "  %5d %6d  %5d %4d %5d  %5s %5dms  %7d%s  %8d  %s\n",
                   t->floor, t->tenant, t->ndesk, up, ad, done, t->worst_ms,
                   t->strikes, t->complained ? "*" : " ", t->rent / 30, files);
    }
    buf_puts(out, "\n  up is desks whose port has LINK on it: copper in a socket at both\n"
                  "  ends, short enough to carry. addr is how many of those also got an\n"
                  "  ADDRESS, and only an addressed desk does any work -- which is the\n"
                  "  number `day` counts. up 20 addr 0 is twenty cables and no dhcp.\n"
                  "\n  a tenancy is served on a day when four fifths of its people got\n"
                  "  their work done. Three days in a row without that is a complaint,\n"
                  "  and a * is one that has been filed. `load` says which port is full.\n");
    /* AND HOW MANY OF THOSE ENDS IT, which was a constant three nobody could
     * read anywhere. It scales with the building now, so it has to be said
     * out loud and it has to be said as a number the player can count
     * against the stars in the column above. */
    {
        int in = 0;
        for (int i = 0; i < s->ntenant; i++) if (s->tenant[i].moved) in++;
        int bear = site_complaints_allowed(s);
        buf_printf(out, "\n  %d filed complaints ends the run. That is a third of the %d\n"
                        "  tenancies in the building, rounded up, and never fewer than\n"
                        "  three -- so it grows as you let the floors.\n", bear, in);
    }
    buf_puts(out,
                  "\n  files is the server their people actually pulled off yesterday.\n"
                  "  Their own machine if they have one and it is on; otherwise one on\n"
                  "  their floor; otherwise anything powered in the building. A server\n"
                  "  qualifies on ANY address it holds -- a socket or a tagged vlan\n"
                  "  subinterface, it makes no difference -- and the leg that answers is\n"
                  "  the one on the asking desk's own segment when it has one.\n");
    if (offfloor)
        buf_puts(out, "  <- is a tenancy being served from another floor. Nothing refused\n"
                      "  it -- their traffic is just crossing a riser to get there, and\n"
                      "  `load` will show you which port is carrying it.\n");
}

void site_dump_load(const Site *s, Buf *out)
{
    uint64_t window = SITE_BUSY_MS * 1000ull;
    /* "drops" alone read as today's, next to a `status` line that really was
     * today's. It is the port's whole life. */
    buf_puts(out, "  port                 speed   busy    queue   drops\n"
                  "                                       peak    (since it was cabled)\n");
    /* Selection sort by utilisation, printing the worst eight. A tower has a
     * few hundred ports and the player wants the ones that are full. */
    int shown = 0;
    int seen[16];
    for (int k = 0; k < 8; k++) {
        int bd = -1, bp = -1;
        uint64_t bb = 0;
        for (int i = 0; i < s->ndev; i++) {
            if (s->dev[i].kind == SDEV_DESK) continue;
            for (int p = 0; p < s->dev[i].nports; p++) {
                if (net_port_state(s->net, s->dev[i].node, p) != PORT_UP) continue;
                bool had = false;
                for (int j = 0; j < shown; j++)
                    if (seen[j] == i * 64 + p) { had = true; break; }
                if (had) continue;
                uint64_t busy = net_port_busy_us(s->net, s->dev[i].node, p);
                if (bd < 0 || busy > bb) { bb = busy; bd = i; bp = p; }
            }
        }
        if (bd < 0) break;
        if (shown < 16) seen[shown] = bd * 64 + bp;
        shown++;
        char nm[NET_NAME_MAX + 8];
        snprintf(nm, sizeof nm, "%s:%d", s->dev[bd].name, bp);
        /* IN MICROSECONDS, BECAUSE THAT IS THE SCALE IT HAPPENS AT.
         *
         * This printed the peak queue in whole milliseconds, so a port that
         * was genuinely dropping on 405 us bursts reported `0ms` next to its
         * drop count -- and a playtester quite reasonably concluded the tool
         * was pointing at nothing. A 48 KB buffer is 394 us of wire at a
         * gigabit; the interesting queues here are all under a millisecond. */
        unsigned long long qus = net_port_queue_us(s->net, s->dev[bd].node, bp);
        char q[24];
        if (qus >= 1000) snprintf(q, sizeof q, "%llums", qus / 1000);
        else             snprintf(q, sizeof q, "%lluus", qus);
        buf_printf(out, "  %-20s %5dMb %5d%%  %7s %7llu\n", nm,
                   net_port_speed(s->net, s->dev[bd].node, bp),
                   (int)((bb * 100) / window),
                   q,
                   (unsigned long long)net_port_drops(s->net, s->dev[bd].node, bp));
    }
    if (!shown) buf_puts(out, "  nothing is cabled up.\n");
    /* THE LEGEND WAS FALSE AT THESE SPEEDS, AND IT WAS THE THING THAT MADE
     * THE TOOL USELESS.
     *
     * It promised that a port starts hurting past eighty per cent and drops at
     * a hundred. A playtester's tower died with nothing above 31%, every queue
     * reading 0ms, and three complaints filed -- so the instrument said calm
     * while the building fell over, and there was no move to make.
     *
     * The arithmetic: a 48 KB egress buffer is 394 us of wire at a gigabit. A
     * floor of desks all fetching at once empties into that in well under a
     * millisecond, so a port drops on bursts while its average over a
     * four-second busy period is single digits. Busy is an average and the
     * drops are not; saying so is the whole difference between a tool that
     * points at the problem and one that alibis it. */
    else buf_puts(out, "\n  busy is the SHARE OF THE BUSY PERIOD this port spent clocking bits,\n"
                       "  averaged over four seconds. Drops do not wait for it to be high: a\n"
                       "  48 KB buffer is 394us of wire at a gigabit, so a floor of desks\n"
                       "  fetching at once can overrun it in bursts while the average sits in\n"
                       "  single figures. READ THE DROPS AND THE PEAK QUEUE, not the average.\n"
                       "  `show <box>` says how many were lost and which of the four reasons\n"
                       "  it was.\n");
}
