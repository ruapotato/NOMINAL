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
/* WHAT A TENANT BRINGS. One computer per drop they asked for, in the rooms
 * they actually lease. The landlord does not buy these and does not own
 * them: what the landlord sells is the port each one is plugged into, and
 * until somebody runs the copper they are a floor full of machines with no
 * network.
 *
 * They are named for the tenancy so that a player reading `netstat` on a
 * switch, or an fdb, or a trace, can tell whose traffic they are looking at.
 * "t7d3" is the fourth desk of tenancy seven and it is on floor seven.
 *
 * WHY THEY ARE NOT ALL IN ONE ROOM ANY MORE (D35). Every desk used to go
 * into `t->room`, the first room the tenancy holds. It was invisible for
 * months and stopped being invisible the moment a person was seated at every
 * desk: seed 7008's tenancy 1 holds eleven offices and a server room, and
 * all twenty of its people were in `#36` while the other ten offices stood
 * empty. A playtester's words: *"the building the letting agent describes
 * and the building you walk through aren't the same building."*
 *
 * It is not decoration. Copper is the metered resource -- about a third of
 * what a tower costs -- and a desk's price is the tray metres between it and
 * whatever box is serving it. Desks in one room means every run in a tenancy
 * is the same length and there is no within-floor geometry at all: the only
 * question a floor can ask is which cupboard the switch is in. Desks in the
 * rooms they are leased in makes the far office really far -- on seed 7008's
 * floor 1 that is 42 m at the near end and 95 m at the far end of the same
 * tenancy -- which is what `quote` is for.
 *
 * WHICH ROOMS TAKE PEOPLE. Offices, flats and shops do. A tenant's server
 * room does not: it is a room built to hold equipment, it is the one room
 * kind with cooling in the heat model, and nobody sits in it. So a tenancy
 * holding a server room puts its desks in the other rooms, and the server
 * room stays what the player carried a server into. If a tenancy somehow
 * holds nothing but a server room, the desks go in `t->room` as before,
 * because a desk that does not exist is worse than a desk in the wrong room.
 */
static bool room_takes_desks(int kind)
{
    return kind == RM_OFFICE || kind == RM_RESIDENCE || kind == RM_RETAIL;
}

/* HOW MANY DESKS IN EACH, and it is arithmetic rather than a draw.
 *
 * Area, apportioned: a hundred-and-twelve-metre office takes four times what
 * a twenty-eight-metre one does, because that is what the rooms are. The
 * split is the Hare quota with largest remainders -- floor(drops * area /
 * total) each, then the leftover desks to the largest remainders, ties to
 * the bigger room and then to the lower room index.
 *
 * THERE IS NO RNG HERE ON PURPOSE. This project was bitten on D30 by a new
 * draw shifting an existing stream: the trade roll came out of the same Rng
 * as `wants_server`, and every tenancy in every tower moved its move-in day,
 * which announced itself as three unrelated blackout checks failing. The
 * safest new stream is no new stream. Every term below is this building's
 * own square metres in integers, so the same seed puts the same desk in the
 * same room on every machine, and `demand` is untouched to the byte.
 *
 * Areas are integers because the building is on a metre grid, and the whole
 * apportionment is integer arithmetic for the same reason: a remainder
 * compared as a double is a remainder that can compare differently on
 * another compiler. */
static void apportion(const Site *s, const SiteTenant *t,
                      const int *room, int nroom, int *out)
{
    long area[BLD_MAX_ROOMS], rem[BLD_MAX_ROOMS], total = 0;
    for (int i = 0; i < nroom; i++) {
        area[i] = (long)(bld_room_area(&s->b->rooms[room[i]]) + 0.5);
        total += area[i];
        out[i] = 0;
    }
    if (total <= 0) { out[0] = t->drops; return; }
    int left = t->drops;
    for (int i = 0; i < nroom; i++) {
        out[i] = (int)((long)t->drops * area[i] / total);
        rem[i] = ((long)t->drops * area[i]) % total;
        left  -= out[i];
    }
    /* The leftover is strictly fewer than the number of rooms, so no room
     * can win twice and a spent remainder is simply struck out. */
    while (left > 0) {
        int best = -1;
        for (int i = 0; i < nroom; i++)
            if (rem[i] >= 0 &&
                (best < 0 || rem[i] > rem[best] ||
                 (rem[i] == rem[best] && area[i] > area[best])))
                best = i;
        if (best < 0) break;
        out[best]++;
        rem[best] = -1;
        left--;
    }
}

static void move_in(Site *s, int ti)
{
    SiteTenant *t = &s->tenant[ti];
    if (t->moved) return;
    t->moved = 1;
    t->desk0 = s->ndev;
    t->ndesk = 0;

    /* The rooms this tenancy holds that people sit in, in room order, which
     * is the order the building generator laid them out and therefore the
     * order `rooms` prints them. Desk numbering follows it, so t1d0 is in
     * the tenancy's first room and the numbers walk the floor. */
    int room[BLD_MAX_ROOMS], nroom = 0;
    for (int i = 0; i < s->b->nrooms && nroom < BLD_MAX_ROOMS; i++) {
        const Room *r = &s->b->rooms[i];
        if (r->tenant != t->tenant) continue;
        if (!room_takes_desks(r->kind)) continue;
        room[nroom++] = i;
    }
    if (nroom == 0) { room[0] = t->room; nroom = 1; }

    int want[BLD_MAX_ROOMS];
    apportion(s, t, room, nroom, want);

    int n = 0;
    for (int i = 0; i < nroom; i++) {
        for (int k = 0; k < want[i]; k++) {
            char nm[NET_NAME_MAX];
            snprintf(nm, sizeof nm, "t%dd%d", t->tenant, n);
            int d = site_install(s, SDEV_DESK, room[i], nm);
            if (d < 0) return;         /* the world is full; say so upstairs */
            t->ndesk++;
            n++;
        }
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
/* Which port of `dev` this desk is already on the end of -- the run off the
 * spool or the lead into a jack, it makes no difference, because both are one
 * link between the two boxes. -1 if the two are not joined. */
static int port_between(const Site *s, int dev, int other)
{
    for (int i = 0; i < s->nlink; i++) {
        const SiteLink *l = &s->link[i];
        if (l->cable < 0) continue;
        if (l->a == dev && l->b == other) return l->aport;
        if (l->b == dev && l->a == other) return l->bport;
    }
    return -1;
}

int site_serve_vlan(Site *s, int tenant, int dev, CableKind k, int vlan)
{
    s->err = SITE_OK;
    if (tenant < 0 || tenant >= s->ntenant) { s->err = SITE_ENODEV; return -1; }
    if (dev < 0 || dev >= s->ndev) { s->err = SITE_ENODEV; return -1; }
    SiteTenant *t = &s->tenant[tenant];
    /* A TENANCY WITH A DATE ON IT IS NOT A MISSING DEVICE. This said
     * SITE_ENODEV -- "no such device" -- about a line whose device was
     * standing in the cupboard with a lead in it. See SITE_ENOTIN. */
    if (!t->moved) { s->err = SITE_ENOTIN; return -1; }
    int done = 0;
    for (int i = 0; i < t->ndesk; i++) {
        int d = t->desk0 + i;
        if (net_port_state(s->net, s->dev[d].node, 0) != PORT_NOCABLE) {
            /* AND A DESK ALREADY PATCHED INTO THIS BOX IS RE-VLANNED, not
             * skipped. `serve 1 sw1` with no vlan patches twenty desks into
             * the untagged default, and a playtester who did that to a
             * tenancy wanting a segment of its own paid for the mistake
             * twice: once in copper, and then in twenty-one hand-typed
             * `vlan sw1 <port> 11` lines, because saying the line again
             * with the vlan on the end did nothing at all. Setting a port
             * that is already patched is a config change on a switch and
             * costs nothing -- no copper is laid here and no metre is
             * charged -- so the remedy for the whole mistake is the same
             * line again with the vlan on it. Only ports on THIS box: the
             * desks on somebody else's switch are somebody else's segment.
             */
            if (vlan > 0) {
                int p = port_between(s, dev, d);
                if (p >= 0) site_port_vlan(s, dev, p, vlan);
            }
            done++;
            continue;
        }
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

/* One in twenty, for a box that was doing work when the lead came out. See
 * site_unclean_stop(). Named here rather than written into the roll so the
 * number a player is told is the number that is rolled. */
#define SITE_UNPLUG_RISK_PCT 5

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
    /* MEGABITS, AND THE REFUSAL SAYS SO. `isp 0` and `isp -5` both answered
     * "that is the network or broadcast address of its own subnet, not a
     * machine's" -- an error about addressing, from the one verb in this
     * shell that takes no address at all. SITE_EADDR was borrowed because
     * nothing else fitted; now something does. */
    if (mb < 10) { s->err = SITE_EMBIT; return false; }
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
    int      dev;          /* the desk -- or the handoff, for a visitor     */
    int      tenant;
    uint32_t dst;
    int      leg;          /* 0 = the web fetch, 1 = the file, 2 = a visitor*/
    int      kb;
    int      start;        /* the tick it begins                            */
    int      sock;
    uint8_t  state;
    uint8_t  judged;       /* is this one of the units they are judged on?  */
    long     got, want;
    int      began, ended; /* ticks, for the latency the player feels       */
} Xfer;

/* ================================================ THE OTHER THREE INDUSTRIES
 *
 * Everything below is the same rule as the transfers above: it goes through
 * core/netstack.c or it does not happen. A call is `net_voice_call`, which is
 * real UDP at a real rate through the same ports and queues; a stream is an
 * ordinary TCP connection carrying bytes UP to an ingest on the far side of
 * the landlord's circuit; a visitor is an ordinary TCP connection opened
 * FROM the handoff, which is the internet in this world, INTO a tenancy's
 * origin server. Not one byte of any of it is a number added to a counter.
 */

/* A CALL, WHICH IS TWO STREAMS. One each way, because a call is two streams
 * and a floor's congestion is almost always in only one of them: a naive
 * tower's riser is full DOWNWARDS, with the files coming off the basement
 * server, and a call measured only upwards would sail through it and report
 * that everything was fine. The tenancy is judged on the call, and the call
 * is as good as its worse half. */
typedef struct {
    int dev, tenant;
    int up, down;          /* net_voice stream ids, or -1                   */
} Call;

/* A STREAM OUT OF A STUDIO. The desk opens one connection to the ingest and
 * pushes for the whole busy period; the ingest reads it as fast as it
 * arrives, which is what an ingest does, so nothing is throttled anywhere
 * but on the wire. The first four bytes are the stream key, which is how the
 * ingest knows whose stream it is -- the same reason a real one has one. */
typedef struct {
    int  dev, tenant;
    int  sock;
    long want;             /* bytes that have to arrive, or it dropped      */
    long pushed, got;
    uint8_t state;         /* X_*                                           */
} Strm;

/* And the ingest's side of one, before the key has arrived. */
typedef struct {
    int     sock;
    int     strm;          /* -1 until the key is read                      */
    int     keylen;
    uint8_t key[4];
} Ingest;

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

/* AND IT HAS TO BE SERVING, WHICH IS A THIRD THING.
 *
 * Powered and addressed were the two conditions, and `service ?` says so in
 * capitals. They are not enough, and two blind playtesters in a row lost runs
 * to the gap. A server that is on and on the network and running no httpd
 * answers nothing, and the day's transfers simply did not happen -- while
 * every indicator a player looks at read healthy: 20 of 20 desks up, 20 of 20
 * addressed, worst 304 ms, zero drops, the port 3% busy, and 60 of 80
 * transfers quietly missing.
 *
 * Worse, `service`'s files column NAMED that box, so the game pointed at a
 * machine and said "their people pulled off this" about a machine that
 * `show` described, in the same session, as "services: none. It is on the
 * network and serves nothing from it." One fact with two answers, in the two
 * places a player looks hardest.
 *
 * net_httpd_port() is the fact. It is 0 when a node serves nothing, and it is
 * what the netstack really answers requests from, so this cannot drift. */
static bool serving_files(const Site *s, int dev)
{
    return net_httpd_port(s->net, s->dev[dev].node) != 0;
}

/* The best server that would ANSWER, and separately the best that is merely
 * standing there powered and addressed. The second one exists so the report
 * can say "you own a box that would do this" rather than "none", which is
 * the difference between a player buying another server they do not need and
 * typing one word. */
static int file_server_for(const Site *s, int tenant)
{
    int any = -1, floor = -1;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (!site_kind_is_server(d->kind) || !d->powered) continue;
        if (!any_addr(s, d->node)) continue;
        if (!serving_files(s, i)) continue;
        if (d->tenant && d->tenant == s->tenant[tenant].tenant) return i;
        if (floor < 0 && d->floor == s->tenant[tenant].floor) floor = i;
        if (any < 0) any = i;
    }
    return floor >= 0 ? floor : any;
}

/* Powered, addressed, and NOT serving: a box one `httpd` away from being the
 * answer. -1 when there is no such box. */
static int idle_server_for(const Site *s, int tenant)
{
    int any = -1, floor = -1;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (!site_kind_is_server(d->kind) || !d->powered) continue;
        if (!any_addr(s, d->node) || serving_files(s, i)) continue;
        if (d->tenant && d->tenant == s->tenant[tenant].tenant) return i;
        if (floor < 0 && d->floor == s->tenant[tenant].floor) floor = i;
        if (any < 0) any = i;
    }
    return floor >= 0 ? floor : any;
}

/* WHERE A WEB HOST'S SITE ACTUALLY LIVES, and it is not the same question as
 * where a tenancy's files come from.
 *
 * A file server is fungible: an office whose own machine is off is served,
 * honestly and a little slower, off whatever else is powered, because a file
 * is a file. A web host's site is not fungible. It is their software on
 * their machine, and no amount of the landlord's kit will answer for it. So:
 *
 *   - if a server of THEIRS exists, that is the origin, and it being off is
 *     them being DOWN. That is the whole of what they are buying and it is
 *     what a battery under it is for.
 *   - if they never had one -- the landlord never bought them a rack -- they
 *     are hosted on whatever is powered, like everybody else. A shared box
 *     is a real arrangement and its weakness is that it is somebody else's
 *     uptime, which is exactly what this models.
 *
 * Returns the device, or -1 for "nothing of theirs is answering". */
/* WHICH BOX IS "THEIRS" IS THE ROOM IT STANDS IN, not who paid for it.
 * Everything a player can carry belongs to the player -- site_move says so at
 * length, because a version that reassigned ownership by where a box was set
 * down confiscated a switch a playtester carried into a let office. So the
 * question this asks is the physical one: is there a server standing in a
 * room this tenancy rents? That is a decision the player makes with their
 * legs -- carry it into their office rather than into your comms cupboard --
 * and it is the one that decides whose uptime the site is on. */
static int web_origin_for(const Site *s, int tenant)
{
    uint8_t who = s->tenant[tenant].tenant;
    bool theirs = false;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (!site_kind_is_server(d->kind)) continue;
        if (d->room >= s->b->nrooms || s->b->rooms[d->room].tenant != who) continue;
        theirs = true;
        if (d->powered && any_addr(s, d->node)) return i;
    }
    return theirs ? -1 : file_server_for(s, tenant);
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
/* WHAT A BOX DRAWS, AND THERE IS ONE ANSWER TO IT.
 *
 * This function used to be a table. It listed nine kinds with their own
 * wattages, and core/site.c's KIT[] listed the same nine with DIFFERENT ones:
 * a switch24 was 60 W here and 90 W there, a rackserver 520 against 700, a
 * router 45 against 120. Two nameplates on one box, and which one you got
 * depended on which question you asked -- the conduit model charged KIT[], the
 * heat model charged this, and `conduits` and the room temperature were
 * describing different stations.
 *
 * The two that were not merely different were wrong outright. The player's own
 * workstation was not in this switch at all, so it fell to `default: 0` and
 * heated its room by nothing while drawing 180 W off the conduit standing
 * beside it. The ISP handoff was the mirror image: 15 W of heat out of a box
 * KIT[] correctly rates at zero, because it is on the ISP's meter and not on
 * yours.
 *
 * So it is deleted, and site_kind_watts() is the nameplate. Adding a kind to
 * the catalogue now heats a room on the same commit that gives it a price. */
static int watts_of(int kind)
{
    return site_kind_watts(kind);
}

int site_room_watts(const Site *s, int room)
{
    int w = 0;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (d->room != room || d->kind == SDEV_DESK) continue;
        /* AND A BOX THAT IS NOT PLUGGED IN MAKES NO HEAT. Obvious once there
         * is a plug, and it was not expressible before D37: a switch in a
         * cupboard dissipated sixty watts whether or not anything was
         * feeding it, because nothing was. */
        if (!d->mains) continue;
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
/* THE DISK IN IT IS A PROPERTY OF THE BOX, NOT OF THE GAME. These were two
 * constants and every disk in the building was rated the same, so "buy the
 * cheap server" cost nothing that any instrument in the game could show.
 * They are read off site_kind_disk_days() now -- 30 days for a minitower, 60
 * for a server, 120 for a rack server -- and the warning still comes at
 * three quarters of the life, which is where it was and is what the fifteen
 * days of SMART logging before the first loss are measured from.
 *
 * It is a RATING, not a countdown: wear is added from how hard the box's own
 * port worked that day, so a busy server ages five times as fast as an idle
 * one and the same disk lasts a different number of days in two buildings.  */
static int wear_fail(const Site *s, int dev)
{
    int d = site_kind_disk_days(s->dev[dev].kind);
    return d > 0 ? d : 60;
}
static int wear_warn(const Site *s, int dev) { return wear_fail(s, dev) * 3 / 4; }

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

/* ------------------------------------------- and one plug, pulled by hand
 * THE SAME EVENT, WITH THE PLAYER AS THE WEATHER. D37 gave a box a plug and
 * a wall to put it in, and the moment there is a plug there is somebody
 * pulling it out of a running server. That has to be the blackout the
 * building has at 04:12 and not a softer relative of it, because the machine
 * on the end of it cannot tell the difference and this project's whole claim
 * is that it never has to: the damage is dealt by the same pf_deal, written
 * by the same breaker_powerfail_as, and read afterwards by the same fsck.
 *
 * A BATTERY IS THE DIFFERENCE, and this is where the second half of what a
 * ups is for finally shows up. In a blackout nomups sees the utility come
 * back in nineteen minutes and the machine never notices. Here it does not
 * come back, so the battery does the other thing it is bought for: it holds
 * the load long enough to shut the machine down in an orderly way. Clean
 * stop, nothing to check in the morning, and the player CHOSE the moment --
 * which is the first time in this game the two hundred and twenty pounds
 * pays for something the world did not do to them.
 *
 * The rng is seeded off the site, the day and the device rather than off the
 * day's own stream, because pulling a plug is not part of the day: two
 * players who pull the same plug on the same morning of the same seed get
 * the same morning after, and a player who pulls one does not shift the
 * weather of every box behind them. */
bool site_unclean_stop(Site *s, int dev)
{
    if (dev < 0 || dev >= s->ndev) return false;
    SiteDev *d = &s->dev[dev];
    if (!site_kind_has_os(d->kind) || !d->powered) return false;
    Machine *m = box_of_dev(s, dev);

    /* WHY THE POWER WENT, ASKED OF THE MODEL RATHER THAN ASSUMED.
     *
     * This function is reached two ways: somebody pulls a box's lead, and a
     * run somewhere upstream trips under load and takes everything behind it
     * down. It used to say "was unplugged while it was running" for both, and
     * that is a lie in the second case -- nobody unplugged anything; a
     * breaker did its job. The one claim this project makes about itself is
     * that it never says anything untrue, and a player looking for the hand
     * that pulled the lead would never find one.
     *
     * The distinction is derived rather than passed down the call chain,
     * because the model already knows it: site_dev_fed() returns the run that
     * tripped. At this moment the conduit tree is already up to date -- the
     * load that tipped it has been added -- so a box going down because of a
     * trip can say WHICH run, and a box going down because its run was pulled
     * has no run to name. */
    int tripped = -1;
    (void)site_dev_fed(s, dev, &tripped);

    if (d->ups) {
        if (m) {
            breaker_syslog(m, "nomups: utility power lost -- load transferred to battery");
            breaker_syslog(m, "nomups: on battery, 19 min runtime remaining");
            breaker_syslog(m, "nomups: no utility power -- commanding an orderly shutdown");
        }
        if (tripped >= 0)
            ev_add(s, SEV_UPS_HELD, dev,
                   "%s was behind run %d when it tripped, and the battery shut "
                   "it down cleanly.", d->name, tripped);
        else
            ev_add(s, SEV_UPS_HELD, dev,
                   "%s had its plug pulled and the battery shut it down cleanly.",
                   d->name);
        site_power(s, dev, false);
        return false;
    }
    Rng rng;
    rng_seed(&rng, s->seed ^ 0x9e3779b97f4a7c15ull ^
                   ((uint64_t)s->day << 12) ^ (uint64_t)dev);
    /* HOW OFTEN PULLING A PLUG BREAKS SOMETHING.
     *
     * Every unplug of a working box used to deal a casualty, and three of
     * pf_deal's four are damage -- so yanking a lead was a three-in-four
     * chance of a repair. That is not what pulling a plug feels like, and
     * the owner said so: *"pulling a plug on a live server should have a
     * chance (somewhat low) to damage the FS and make you have to repair
     * it."*
     *
     * It is also more true this way. A journalled filesystem survives most
     * unclean stops; what kills it is being mid-write at the instant the
     * power goes, and the busier the disk, the likelier that is. So the roll
     * The owner set the number: *"it should be more like a 5% chance of
     * damage to the FS."* One in twenty, flat, for any box that was doing
     * work. I first scaled it with how busy the disk was -- 8% at a whisper
     * up to 33% flat out -- and dropped that: a rate the player cannot count
     * is a rate they cannot learn, and "pulling a live plug is about a one
     * in twenty" is a thing somebody can hold in their head and take a
     * decision against. The gradient was truer to physics and worse to
     * play.
     *
     * A box that did nothing at all is clean, unchanged: there was nothing
     * in flight to lose.
     *
     * The MAINS failure is deliberately NOT this. That is the whole building
     * going down at 04:12 with everything mid-write at once, it is the event
     * D28 built the four casualties for, and a player who has been warned by
     * `events` for a fortnight and bought no battery should lose something.
     * This is one lead, pulled on purpose, by somebody standing there. */
    /* "A LIVE SERVER" IS ONE THAT IS RUNNING, not one whose port happens to
     * read a whole per cent. The first version of this rolled only when
     * used_pct() was above zero, and measured 0 damage in 397 unplugs of a
     * server that was up, addressed and serving -- because that number is
     * the port's share of the busy period as an integer, and a box nobody is
     * hammering rounds to nothing. The machine either has a filesystem it
     * could be part-way through writing or it does not. */
    bool live = m && m->boot.running;
    /* AND A TRIP IS NOT A PULLED LEAD, which is why the roll differs.
     *
     * Every word of the note above argues for one in twenty on the grounds
     * that this is "one lead, pulled on purpose, by somebody standing there"
     * -- a person who, on the whole, picks a quiet moment. Not one of those
     * grounds survives a breaker going: nobody chose the instant, nobody was
     * standing there, and everything behind that run goes down together while
     * it is doing whatever it was doing. That is the blackout's character in
     * one cupboard, so it is dealt the blackout's outcome.
     *
     * It is also the fairest hard thing in the game, and that is the argument
     * for making it hurt rather than roll. `conduits` prints the percentage
     * of every run on demand, the number climbs as the player adds load and
     * nothing else moves it, `feed` names a source with a hole left in it,
     * and a strip is 45 pounds. A run at 93% has been telling them so since
     * the day they built it. Nobody is ambushed by this one -- which is
     * exactly the shape D23 asked for, a fault that is a consequence of
     * something you did rather than something a designer hid. */
    bool unlucky = live && (tripped >= 0 ||
                            rng_range(&rng, 0, 99) < SITE_UNPLUG_RISK_PCT);
    int seq = -1;
    int kind = unlucky ? pf_deal(s, &rng, &seq) : PF_CLEAN;
    char note[200] = "";
    if (m) breaker_powerfail_as(m, &rng, kind, note, sizeof note);
    site_power(s, dev, false);
    if (tripped >= 0) {
        /* NAME THE RUN AND THE ARITHMETIC. The player's next move is to take
         * something off that run or pull another from the core, and both of
         * those need to know which run and by how much. */
        int load = site_conduit_load(s, tripped);
        int cap = s->cond[tripped].watts > 0 ? s->cond[tripped].watts
                                             : SITE_CONDUIT_W;
        if (kind == PF_CLEAN)
            ev_add(s, SEV_TRIPPED, dev,
                   "run %d tripped with %d W on it against the %d W it "
                   "carries, and %s went down with it. Its filesystem came "
                   "through it.", tripped, load, cap, d->name);
        else
            ev_add(s, SEV_TRIPPED, dev,
                   "run %d tripped with %d W on it against the %d W it "
                   "carries, and %s went down unclean with it.",
                   tripped, load, cap, d->name);
    } else if (kind == PF_CLEAN) {
        ev_add(s, SEV_DOWN_DIRTY, dev,
               "%s was unplugged while it was running. Its filesystem came "
               "through it.", d->name);
    } else {
        ev_add(s, SEV_DOWN_DIRTY, dev,
               "%s was unplugged while it was running and went down unclean.",
               d->name);
    }
    (void)note;
    return true;
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
        if (d->wear >= wear_fail(s, i)) {
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
                /* AND SAY WHY THE PERCENTAGE IS ABOUT TO GO DOWN. A
                 * playtester watched srv2 read 88% on day 27 and 75% on day
                 * 30 -- the morning it lost a sector -- and asked why a disk
                 * appears to get better by failing. It is honest: the drive
                 * reallocated into its spares and is measurably further from
                 * the next loss than it was yesterday. But "the number went
                 * down" is the opposite of the signal you want after a loss,
                 * and a number nobody can interpret is a number nobody
                 * watches. */
                ev_add(s, SEV_DISK_FAIL, i,
                       "the disk in %s lost a sector after %d days. It had been "
                       "warning. Its reading drops back now -- it reallocated "
                       "into its spares, so it is further from the NEXT loss "
                       "and no further from the end. Only `disk %s` resets it "
                       "for good.", d->name, d->run_days, d->name);
                /* It has lost what it was going to lose. The disk keeps
                 * running -- and keeps being a disk that has run out of
                 * spares, which is why the wear does not reset until
                 * somebody puts a new one in. */
                d->wear = wear_warn(s, i);
            }
            continue;
        }
        if (d->wear >= wear_warn(s, i)) {
            int bad = 3 + (d->wear - wear_warn(s, i)) * 4;
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

/* ------------------------------------------------------------ the watch */
/*
 * WHAT A WORKING CREW STATION IS FOR.
 *
 * A blind playtester spent 5,482 of a 5,495 opening budget getting all six
 * bridge stations working, read "6 of 6 bridge stations working", and nothing
 * happened. Their verdict was the right one: *"either the crew starts doing
 * something, or it should not be advertised."* A checklist that turns green
 * over a stub is exactly the scenery this whole pivot exists to get away
 * from.
 *
 * So a station that works is somebody ON WATCH, and what they do is the
 * thing a person would otherwise have to walk across the station to do at
 * four in the morning: when the mains drops and a box goes down with it, the
 * crew put it back on. The same playtester lost a day to precisely that --
 * `events` said "files went down with the power and has not been switched
 * back on", and the office read every document across the handoff for the
 * rest of the day because nobody was there to press a button.
 *
 * ONE PAIR OF HANDS PER WORKING STATION, and no more. Six stations up is a
 * full watch and the night costs you nothing; three is a skeleton crew that
 * gets to some of it; none and you find out when a tenancy's day fails,
 * which is what happens today and will keep happening to anybody who does
 * not build a bridge.
 *
 * IT IS DERIVED, LIKE EVERYTHING ELSE ABOUT THE CREW. site_crew_working()
 * asks the model -- a machine at the station, power in it, a cable out of it
 * -- so a bridge that stops working stops keeping watch on the same morning,
 * with nothing to keep in step.
 *
 * WHAT IT DELIBERATELY DOES NOT DO: repair a disk, clear a fault, or touch
 * anything the break-fix half of the game is about. The crew press a button
 * a person could press. Every diagnosis in this game is still the player's,
 * and a station that came up dirty still comes up dirty -- the crew got the
 * power back on, not the filesystem back.
 */
static void the_watch(Site *s, int hands)
{
    if (hands <= 0) {
        /* AND SAYING NOTHING WOULD BE THE WORSE FAILURE. A player whose
         * bridge is dark should be told what it cost them, on the morning it
         * cost them, or the bridge is a thing they never find a reason to
         * build.
         *
         * ON THE MORNING IT COST THEM, AND NOT EVERY MORNING AFTER. The first
         * version counted every box that was off and fed, which after one
         * blackout is every day for the rest of the run -- a line a player
         * would learn to skip, and fifty days of it overran the four-kilobyte
         * buffer --eventcheck compares two runs in, so the determinism gate
         * failed on a truncation. It speaks about TONIGHT: boxes this night
         * took down, which is the only night it has anything to say. */
        int dark = 0;
        for (int e = s->nev - 1; e >= 0 && s->ev[e].day == s->day; e--)
            if (s->ev[e].kind == SEV_DOWN_DIRTY) dark++;
        if (dark > 0 && s->ncrew > 0)
            ev_add(s, SEV_CREW_DARK, -1,
                   "%d box%s went down in the night and %s still off. Nobody "
                   "is on the bridge: %d of %d crew stations work.",
                   dark, dark == 1 ? "" : "es", dark == 1 ? "it is" : "they are",
                   site_crew_working(s), s->ncrew);
        return;
    }
    int done = 0;
    for (int i = 0; i < s->ndev && done < hands; i++) {
        SiteDev *d = &s->dev[i];
        /* Something with an operating system, with power available to it,
         * that is switched off. That is a box the night took down and a
         * person could switch back on -- and nothing else. A box the player
         * deliberately switched off has no mains-fail behind it, so this
         * would put it back on: it does not, because only a box that is FED
         * and OFF is a candidate, and switching one off by hand is what
         * `power off` does to a box that is still fed. See the note. */
        if (!site_kind_has_os(d->kind) || !d->mains || d->powered) continue;
        /* ONLY WHAT TONIGHT TOOK DOWN. `power <box> off` is a thing a player
         * does on purpose, and a crew that undid it every morning would be
         * taking a decision away rather than doing a job. So the candidate
         * has to be named in today's events as having gone down with the
         * power. */
        bool tonight = false;
        for (int e = s->nev - 1; e >= 0 && s->ev[e].day == s->day; e--)
            if (s->ev[e].kind == SEV_DOWN_DIRTY && s->ev[e].dev == (int16_t)i)
                tonight = true;
        if (!tonight) continue;
        if (!site_power(s, i, true)) continue;
        done++;
        ev_add(s, SEV_CREW_WATCH, i,
               "%s was switched back on by the watch before the working day.",
               d->name);
    }
    if (done > 0)
        ev_add(s, SEV_CREW_WATCH, -1,
               "%d of %d crew stations were manned overnight, and put %d box%s "
               "back on.", hands, s->ncrew, done, done == 1 ? "" : "es");
}

/* Everything the world did today, in the order it would have happened: the
 * kit ages on the day's own traffic, the copper takes its errors while the
 * traffic is on it, the heat builds through the working day, the disks fail
 * when they fail, the mains goes in the small hours -- and then the bridge
 * crew, if there is one, put right what they can before anybody starts work. */
static void the_weather(Site *s)
{
    Rng rng;
    rng_seed(&rng, s->seed ^ (0x77e47ull * (uint64_t)s->day) ^ 0xbeef01ull);
    age_the_kit(s);
    the_copper(s);
    the_heat(s, &rng);
    the_disks(s, &rng);
    /* WHO WAS ON DUTY WHEN THE NIGHT BEGAN, counted BEFORE the mains goes.
     *
     * The first version asked site_crew_working() inside the watch, after the
     * blackout -- and a blackout switches the crew's own consoles off with
     * everything else, so the answer was always nought and the watch could
     * never act on the one night it exists for. Measured: six of six stations
     * working, the box down, and the crew did nothing.
     *
     * The crew are PEOPLE. Their screens going dark at 04:12 does not mean
     * they went home; it means they are standing in the dark on the deck they
     * were already on. So the watch is a property of the night as it STARTED,
     * and their own consoles are among the things they put back on. */
    int hands = site_crew_working(s);
    if (site_mains_fails_on(s->seed, s->day)) the_mains_fails(s, &rng);
    the_watch(s, hands);
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
    /* AND WHAT THE DISK IN THAT PARTICULAR BOX IS RATED FOR. The percentage
     * used to be against one constant for the whole building, so a minitower
     * and a rack server at 50% were fifteen days apart and the page said the
     * same thing about both. `rated` is the box's own number. */
    buf_puts(out, "\n  box            days  disk  rated   ups   room heat\n");
    int shown = 0;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (!site_kind_has_os(d->kind)) continue;
        int rated = wear_fail(s, i);
        int pct = d->wear * 100 / rated;
        buf_printf(out, "  %-14s %4d  %3d%%  %4dd  %-4s  %7d%%\n", d->name,
                   d->run_days, pct > 100 ? 100 : pct, rated,
                   d->ups ? "yes" : "no", site_room_heat(s, d->room));
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
        "  new one is `disk <box>`. rated is what THAT box's disk is rated for,\n"
        "  in days of average use, and it is a fact about the box you bought:\n"
        "  30 for a minitower, 60 for a server, 120 for a rack server.\n"
        "  `ups <box>` fits a battery: a box on one\n"
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

/* ============================================= WAS THAT A DAY THEY PAY FOR
 *
 * Four fifths of what a tenancy was promised, which is the rule this game
 * has always had -- except that what they were promised is not the same
 * thing for every industry, and one of them buys something stricter.
 *
 * A WEB HOST IS BUYING UPTIME. Four fifths of your visitors served is not a
 * slightly slow day for a hosting company, it is one visitor in five looking
 * at a browser error, and no host would wear it. Nineteen of twenty, which
 * is the ordinary shape of the thing they would have signed. It is the only
 * per-kind number in the rule and it is here rather than in three places. */
bool site_tenant_served(const Site *s, int ti)
{
    if (ti < 0 || ti >= s->ntenant) return false;
    const SiteTenant *t = &s->tenant[ti];
    if (t->tried <= 0) return false;
    if (t->kind == TEN_WEBHOST)
        return t->finished * SITE_WEB_UP_DEN >= t->tried * SITE_WEB_UP_NUM;
    return t->finished * 5 >= t->tried * 4;
}

/* AND WHY NOT, IN THEIR OWN TERMS. A voice tenancy is not unhappy because
 * transfers did not finish -- they are unhappy because the calls broke up,
 * and this is the sentence that says which. Every number in it was measured
 * during the busy period that has just ended. */
void site_tenant_why(const Site *s, int ti, char *out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (ti < 0 || ti >= s->ntenant) return;
    const SiteTenant *t = &s->tenant[ti];
    if (!t->moved) return;
    if (t->tried == 0) {
        if (site_tenant_connected(s, ti) == 0)
            snprintf(out, (size_t)cap, "not one of their %d desks has a cable "
                                       "in it.", t->ndesk);
        /* NOT ASKED YET IS NOT UNANSWERED. This said "nothing is serving
         * dhcp on their segment" off `addressed == 0` alone, and printed it
         * at a player who had just typed a correct `dhcpd` line: the pool
         * was up, on the right subinterface, on the right vlan, and every
         * one of those twenty desks held a lease from it one `day` later.
         * The desks ask when the busy period runs and not a moment before,
         * so which sentence this is depends on whether they have asked --
         * which siteday counts at the call itself. */
        else if (site_tenant_addressed(s, ti) == 0 && t->leases_asked == 0)
            snprintf(out, (size_t)cap, "%d desks with link and no address "
                                       "YET: their machines ask for a lease "
                                       "when the day runs. `day`.",
                     site_tenant_connected(s, ti));
        else if (site_tenant_addressed(s, ti) == 0)
            snprintf(out, (size_t)cap, "%d desks with link asked for a lease "
                                       "and got nothing: no dhcp pool answered "
                                       "on their segment. `dhcpd <box>` says "
                                       "what a box serves.",
                     site_tenant_connected(s, ti));
        return;
    }
    if (site_tenant_served(s, ti)) return;
    switch (t->kind) {
    case TEN_VOICE:
        /* AND WHERE TO GO NEXT. This row is the landlord's view of a fault
         * whose evidence lives on the tenant's own machine, and a playtester
         * who read exactly this line then sat at one of those desks found
         * nothing, because every tool they knew asks about NOW and the calls
         * were over. `voice` is the one that remembers, and it names the port
         * that threw the audio away -- which on a naive tower is three hops
         * from the desk and not on that floor at all.
         *
         * AND WHERE THAT VERB LIVES, which this line did not say. There is
         * no `sit` and no `voice` in the tower shell: they are verbs of the
         * SESSION -- the chair the player gets over `--serve`/`--desk`, and
         * `tower` is the word that stands them up out of it. A blind
         * playtester read this sentence at a tower prompt, typed `sit`,
         * was told there is no such command, and the game had advertised
         * two commands that do not exist at the prompt it printed the
         * advice at. So the line now leads with the verb this shell really
         * has -- `load`, which names the busiest ports and their drops --
         * and names the other two as somebody else's, rather than pretending
         * either that they do not exist or that they are here. */
        snprintf(out, (size_t)cap,
                 "%d of %d calls broke up: %d.%d%% of the audio concealed, "
                 "%d ms one way, %u us of jitter. `load` names the port that "
                 "threw it away (`sit`+`voice` at a desk, in the session, say "
                 "it from the other end).",
                 t->tried - t->finished, t->tried,
                 t->conceal_ppm / 10000, (t->conceal_ppm / 1000) % 10,
                 t->delay_ms, (unsigned)t->jitter_us);
        break;
    case TEN_WEBHOST:
        if (t->finished == 0)
            snprintf(out, (size_t)cap,
                     "their site answered NOTHING from the internet all day. "
                     "%d visitors, %d served.", t->tried, t->finished);
        else
            snprintf(out, (size_t)cap,
                     "%d of %d visitors got an error. They are paying for %d "
                     "in %d.", t->tried - t->finished, t->tried,
                     SITE_WEB_UP_NUM, SITE_WEB_UP_DEN);
        break;
    case TEN_STUDIO:
        snprintf(out, (size_t)cap,
                 "%d of %d streams dropped: %ld KB went up of the %ld KB they "
                 "had to have. Upload.",
                 t->tried - t->finished, t->tried, t->up_kb, t->up_want_kb);
        break;
    default:
        snprintf(out, (size_t)cap,
                 "%d of %d transfers did not finish inside the busy period; "
                 "the slowest took %d ms.",
                 t->tried - t->finished, t->tried, t->worst_ms);
        break;
    }
}

/* ================================================================== a day */
/* ONE DAY, WHICH IS THE THREE BELOW IN A ROW.
 *
 * This is what every gate in this project calls and what `day 1` means, and
 * it must stay exactly that: --loadcheck, --eventcheck and --sitecheck drive
 * thousands of days between them and measure what each one did. Splitting the
 * day for live time (D44) is only allowed if this keeps producing the same
 * numbers, so the split is BELOW this line and this line is the proof that it
 * composes back.
 *
 * The tick loop is asked for the whole busy period in one call, so a day run
 * this way does exactly what it did before the split: same order, same rng,
 * same ticks. */
bool site_day(Site *s, SiteDay *rep)
{
    if (!site_day_begin(s)) {
        if (rep) *rep = s->last;
        return !s->over;
    }
    while (site_day_tick(s, SITE_BUSY_MS) > 0) { }
    return site_day_end(s, rep);
}


/* ------------------------------------------------------------ the clock */
/*
 * A DAY IN PROGRESS. See D44.
 *
 * Everything in here used to be a local of site_day(), which is why site_day()
 * could not be interrupted: the day's work lived on the stack and vanished if
 * you stopped in the middle of it. It is the same state, in the same order,
 * moved somewhere it can survive a frame boundary.
 *
 * WHY AN OPAQUE STRUCT AND NOT FIELDS ON Site: nothing outside this file has
 * any business reading a busy period that is half over. `Site` carries one
 * pointer and this is the only translation unit that knows what is behind it.
 */
struct SiteDayRun {
    SiteDay  r;
    Xfer    *xs;   int nx;
    Call    *cs;   int ncall;
    Strm    *ss;   int nstrm;
    Ingest  *ing;  int ning;   int strmcap;
    int      ingest;
    Rng      rng;
    int      upnode;
    uint32_t web;
    uint64_t frames0, drops0, t0;
    int      tick;             /* how far through SITE_BUSY_MS we are */
};

int site_day_progress(const Site *s)
{
    return s->run ? s->run->tick : -1;
}

bool site_day_begin(Site *s)
{
    if (s->over || s->run) return false;
    struct SiteDayRun *R = (struct SiteDayRun *)nom_alloc(sizeof *R);
    memset(R, 0, sizeof *R);
    R->ingest = -1;
    s->run = R;
    SiteDay r;
    memset(&r, 0, sizeof r);
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
            /* COUNTED WHERE THE REQUEST IS REALLY MADE. A desk that has
             * asked and got nothing is a fault; a desk that has not asked
             * yet is a day that has not run. `site_tenant_why` is the only
             * reader, and it is the difference between accusing the player
             * and telling them the clock has not turned. */
            t->leases_asked++;
            net_dhcp_client(s->net, s->dev[d].node, 0);
        }
    }

    /* ------------------------------------------------- build the day's work */
    /* AND IT IS NOT THE SAME WORK ON EVERY FLOOR ANY MORE. Four industries,
     * four shapes: an office pulls, a studio pushes, a web host is pulled
     * FROM outside, and a voice business hardly moves any bytes at all and
     * is ruined by the ones everybody else moved. What each desk does is
     * decided here; where the frames then go is the player's architecture,
     * and nothing in this file has an opinion about how much that costs. */
    int desks_in = 0;
    for (int i = 0; i < s->ntenant; i++) if (s->tenant[i].moved) desks_in += s->tenant[i].ndesk;
    /* The page and the files, all at the same time: see SITE_DESK_FILES --
     * plus, per web host, a busy period's worth of visitors off the street. */
    int cap = desks_in * (1 + SITE_DESK_FILES);
    for (int i = 0; i < s->ntenant; i++)
        if (s->tenant[i].moved && s->tenant[i].kind == TEN_WEBHOST)
            cap += SITE_WEB_HITS;
    Xfer *xs = cap ? (Xfer *)nom_alloc(sizeof(Xfer) * (size_t)cap) : NULL;
    int nx = 0;
    Call *cs = desks_in ? (Call *)nom_alloc(sizeof(Call) * (size_t)desks_in) : NULL;
    int ncall = 0;
    int strmcap = desks_in * SITE_STREAM_LEGS;
    Strm *ss = strmcap ? (Strm *)nom_alloc(sizeof(Strm) * (size_t)strmcap) : NULL;
    Ingest *ing = strmcap ? (Ingest *)nom_alloc(sizeof(Ingest) * (size_t)strmcap) : NULL;
    int nstrm = 0, ning = 0, ingest = -1;

    Rng rng;
    rng_seed(&rng, s->seed ^ (0x0d0a17ull * (uint64_t)s->day));

    int upnode = s->dev[s->uplink].node;
    /* WHERE A TENANT'S DAY GETS ITS BYTES FROM, and it is still the handoff.
     *
     * D42 put the web on real machines out past the circuit, and the obvious
     * next move was to aim this at one of them -- an office pulling a file
     * "off the internet" really pulling it off a box on the internet. It was
     * tried and it is NOT done, because it is not free and the price is the
     * difficulty curve. Two extra hops each way doubles the round trip, and
     * a round trip is what a windowed TCP transfer is divided by. Measured,
     * on the 500 Mb circuit scenario in --sitecheck:
     *
     *     office 100% -> 63%,  studio 100% -> 2%
     *
     * That is a real consequence of a real change and it is not a bug. But
     * re-calibrating a tenancy's day is a different piece of work from
     * building the internet, and doing both at once means neither can be
     * measured. The circuit is still crossed either way -- the handoff's
     * port 0 is the rate-limited one and every byte here goes through it --
     * so what is lost is the two hops, not the constraint. See D42. */
    uint32_t web = net_if_get_addr(s->net, upnode, 0);
    for (int i = 0; i < s->ntenant; i++) {
        SiteTenant *t = &s->tenant[i];
        t->tried = t->finished = t->worst_ms = 0;
        t->bytes = 0;
        t->files_dev = -1;
        t->conceal_ppm = t->jitter_us = t->delay_ms = 0;
        t->up_kb = t->up_want_kb = 0;
        t->down = 0;
        t->sla = 0;
        if (!t->moved) continue;
        r.tenants_in++;
        r.desks += t->ndesk;
        int fsd = file_server_for(s, i);
        t->files_dev = fsd;
        /* HOW MANY FILES ONE OF THEIR PEOPLE OPENS, which is what makes an
         * office an office. A call centre agent and a hosting company's three
         * staff have a machine each and a normal amount of work on it; a
         * studio suite is not opening documents at all, it is uploading. */
        int nfile = SITE_DESK_FILES;
        if (t->kind == TEN_VOICE)        nfile = SITE_VOICE_FILES;
        else if (t->kind == TEN_WEBHOST) nfile = SITE_WEB_FILES;
        else if (t->kind == TEN_STUDIO)  nfile = SITE_STUDIO_FILES;
        for (int j = 0; j < t->ndesk && nx + nfile < cap; j++) {
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
            /* A STUDIO SUITE HAS THE PROJECT OPEN AND THE STREAM RUNNING.
             * The uploads are ordinary TCP connections to an ingest on the
             * far side of the landlord's circuit, pushed as hard as the
             * network will take them, and every kilobyte has to be there by
             * the end or the stream dropped. */
            if (t->kind == TEN_STUDIO) {
                if (ingest < 0)  /* one ingest for the tower, as there is */
                    ingest = net_tcp_listen(s->net, upnode, SITE_STREAM_PORT);
                for (int k = 0; k < SITE_STREAM_LEGS; k++) {
                    Strm *m = &ss[nstrm++];
                    memset(m, 0, sizeof *m);
                    m->dev = d; m->tenant = i; m->sock = -1;
                    m->want = (long)SITE_STREAM_KB * 1024;
                    t->up_want_kb += SITE_STREAM_KB;
                }
            }
            /* AND A PHONE IS NOT A TRANSFER. Two streams, one each way, to
             * the carrier on the far side of the handoff -- which is where a
             * SIP trunk really is -- so a call crosses the desk's own port,
             * the floor switch, the riser, the core, the router and the
             * circuit, in both directions, exactly as everybody else's
             * traffic does and at a fiftieth of the bytes. */
            if (t->kind == TEN_VOICE) {
                Call *c = &cs[ncall];
                memset(c, 0, sizeof *c);
                c->dev = d; c->tenant = i;
                /* A PORT PAIR PER CALL, which is what RTP really does and
                 * is not a detail: one carrier port for the whole building
                 * would mean the second call could not open a socket, and
                 * twenty agents would share one phone. */
                uint16_t rtp = (uint16_t)(16384 + 2 * ncall);
                c->up   = net_voice_start(s->net, s->dev[d].node, upnode, web,
                                          rtp, NET_VOICE_PAYLOAD, NET_VOICE_PTIME);
                c->down = net_voice_start(s->net, upnode, s->dev[d].node,
                                          net_if_get_addr(s->net, s->dev[d].node, 0),
                                          (uint16_t)(rtp + 1),
                                          NET_VOICE_PAYLOAD, NET_VOICE_PTIME);
                /* The world holds NET_VOICE_MAX calls. One it could not seat
                 * is not a call that went badly, so it is not counted at all
                 * -- the same treatment a socket the pool could not give out
                 * gets, and for the same reason. */
                if (c->up < 0 || c->down < 0) {
                    if (c->up >= 0)   net_voice_stop(s->net, c->up);
                    if (c->down >= 0) net_voice_stop(s->net, c->down);
                } else ncall++;
            }
            Xfer *x = &xs[nx++];            /* the page, off the internet    */
            memset(x, 0, sizeof *x);
            x->dev = d; x->tenant = i; x->sock = -1; x->leg = 0;
            x->kb = SITE_DESK_WEB_KB;
            x->want = (long)SITE_DESK_WEB_KB * 1024;
            x->dst = web;
            x->start = begins;
            x->judged = (uint8_t)(t->kind == TEN_OFFICE);
            /* The files, off the nearest server, all open together -- one
             * person with more than one thing on the go, which is what a
             * desk is. They start on the same millisecond as the page for
             * the same reason the page does: one person sitting down. */
            for (int k = 0; k < nfile; k++) {
                Xfer *y = &xs[nx++];
                memset(y, 0, sizeof *y);
                y->dev = d; y->tenant = i; y->sock = -1; y->leg = 1;
                y->kb = SITE_DESK_FILE_KB;
                y->want = (long)SITE_DESK_FILE_KB * 1024;
                y->dst = files ? files : web;
                y->start = begins;
                y->judged = (uint8_t)(t->kind == TEN_OFFICE);
            }
        }
        /* ------------------------------------------- and the visitors
         * A WEB HOST'S TRAFFIC ARRIVES FROM OUTSIDE, which is the direction
         * nothing in this tower has ever been asked to carry. These are real
         * TCP connections opened BY the handoff -- the internet, in this
         * world -- to the tenancy's origin server, and they only arrive if
         * the player's router really routes inbound: the handoff has had a
         * route for 10/8 and 192.168/16 pointing at the far end of its own
         * /30 since day one, and something has to be on the far end of it
         * that knows the way to the tenancy's segment.
         *
         * The origin is their own server if they have one, and otherwise
         * whatever else is powered -- the same fallback everybody's files
         * get, because a host with no rack of their own is hosted on the
         * landlord's box, and that is exactly the arrangement whose weakness
         * is that it is somebody else's uptime. */
        if (t->kind == TEN_WEBHOST && site_tenant_addressed(s, i) > 0) {
            int od = web_origin_for(s, i);
            t->files_dev = od;         /* `service` names what answered      */
            uint32_t origin = od >= 0 ? any_addr(s, s->dev[od].node) : 0;
            for (int k = 0; k < SITE_WEB_HITS && nx < cap; k++) {
                Xfer *v = &xs[nx++];
                memset(v, 0, sizeof *v);
                v->dev = s->uplink; v->tenant = i; v->sock = -1; v->leg = 2;
                v->kb = SITE_WEB_HIT_KB;
                v->want = (long)SITE_WEB_HIT_KB * 1024;
                v->dst = origin;
                v->start = (int)rng_range(&rng, 0, SITE_BUSY_MS / 10);
                v->judged = 1;
                /* No origin at all is not a slow site, it is a site that is
                 * not there. It fails at connect, on the wire, because there
                 * is nothing at address zero to answer it. */
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

    /* THE SETUP IS DONE. Everything the busy period needs goes into the run,
     * in the order it was declared, and the ticks come next. */
    R->r = r;
    R->xs = xs;   R->nx = nx;
    R->cs = cs;   R->ncall = ncall;
    R->ss = ss;   R->nstrm = nstrm;
    R->ing = ing; R->ning = ning; R->strmcap = strmcap;
    R->ingest = ingest;
    R->rng = rng;
    R->upnode = upnode;
    R->web = web;
    R->frames0 = frames0;
    R->drops0 = drops0;
    R->t0 = t0;
    R->tick = 0;
    return true;
}


/* SOME OF THE MILLISECONDS. The body is the loop that was here before, one
 * tick at a time, with the day's work read out of the run instead of off the
 * stack. Returns how many milliseconds are still to come. */
int site_day_tick(Site *s, int ms)
{
    struct SiteDayRun *R = s->run;
    if (!R) return 0;
    SiteDay r = R->r;
    Xfer *xs = R->xs;   int nx = R->nx;
    Call *cs = R->cs;   int ncall = R->ncall;
    Strm *ss = R->ss;   int nstrm = R->nstrm;
    Ingest *ing = R->ing; int ning = R->ning;
    int strmcap = R->strmcap;
    int ingest = R->ingest;
    Rng rng = R->rng;
    int upnode = R->upnode;
    uint32_t web = R->web;
    (void)cs; (void)ncall; (void)ingest; (void)upnode;
    int last = R->tick + ms;
    if (last > SITE_BUSY_MS) last = SITE_BUSY_MS;
    for (int tick = R->tick; tick < last; tick++) {
        for (int i = 0; i < nx; i++) {
            Xfer *x = &xs[i];
            if (x->state == X_WAIT) {
                if (tick >= x->start) xfer_begin(s, x, tick);
            } else if (x->state == X_CONNECT || x->state == X_RECV) {
                xfer_poll(s, x, tick);
            }
        }
        /* THE STUDIOS PUSH. One connection per suite, opened on the first
         * tick and then fed every millisecond with as much as the send
         * buffer will take -- which is as much as the ACKs coming back allow,
         * which is as much as the wire allows. Nothing here decides the rate;
         * TCP and the ports do. */
        for (int i = 0; i < nstrm; i++) {
            Strm *m = &ss[i];
            if (m->state == X_WAIT) {
                m->sock = net_tcp_connect(s->net, s->dev[m->dev].node, web,
                                          SITE_STREAM_PORT);
                m->state = m->sock < 0 ? X_FAILED : X_CONNECT;
                continue;
            }
            if (m->state != X_CONNECT && m->state != X_RECV) continue;
            TcpState st = net_tcp_state(s->net, m->sock);
            if (st == TCP_CLOSED) { m->state = X_FAILED; m->sock = -1; continue; }
            if (st != TCP_ESTABLISHED) continue;
            if (m->state == X_CONNECT) {
                /* THE STREAM KEY, four bytes, first: it is how the ingest
                 * knows whose stream this is, which is the same reason a real
                 * one has one. */
                uint8_t key[4];
                key[0] = (uint8_t)(i >> 24); key[1] = (uint8_t)(i >> 16);
                key[2] = (uint8_t)(i >> 8);  key[3] = (uint8_t)i;
                if (net_tcp_send(s->net, m->sock, key, 4) != 4) continue;
                m->state = X_RECV;
            }
            static const uint8_t frame[1400] = { 0 };
            while (m->pushed < m->want) {
                long left = m->want - m->pushed;
                int wantb = left < (long)sizeof frame ? (int)left : (int)sizeof frame;
                int k = net_tcp_send(s->net, m->sock, frame, wantb);
                if (k <= 0) break;
                m->pushed += k;
            }
        }
        /* AND THE INGEST READS. A real one drains its socket as fast as the
         * bytes arrive, so this does too -- otherwise the receive window
         * would shut and the stream would be throttled by this file rather
         * than by the network, which is the one thing this file may not do. */
        if (ingest >= 0) {
            int a;
            while (ning < strmcap && (a = net_tcp_accept(s->net, ingest)) >= 0) {
                Ingest *g = &ing[ning++];
                memset(g, 0, sizeof *g);
                g->sock = a; g->strm = -1;
            }
            uint8_t b[2048];
            for (int i = 0; i < ning; i++) {
                Ingest *g = &ing[i];
                if (g->sock < 0) continue;
                int k;
                while ((k = net_tcp_recv(s->net, g->sock, b, sizeof b)) > 0) {
                    int off = 0;
                    while (g->keylen < 4 && off < k) g->key[g->keylen++] = b[off++];
                    if (g->strm < 0 && g->keylen == 4)
                        g->strm = (g->key[0] << 24) | (g->key[1] << 16) |
                                  (g->key[2] << 8) | g->key[3];
                    if (g->strm >= 0 && g->strm < nstrm)
                        ss[g->strm].got += k - off;
                }
            }
        }
        net_step(s->net, 1);
    }
    /* What the ticks changed goes back into the run. The arrays are pointers
     * and were mutated in place; the counters and the rng are values. */
    R->tick = last;
    R->r = r;
    R->rng = rng;
    R->nstrm = nstrm;
    R->ning = ning;
    R->web = web;
    return SITE_BUSY_MS - last;
}


/* AND WHAT IT ALL CAME TO. Scoring, the rent, the weather and the end of the
 * run -- everything that used to follow the loop, unchanged, reading the
 * day's work out of the run one last time before it is freed. */
bool site_day_end(Site *s, SiteDay *rep)
{
    struct SiteDayRun *R = s->run;
    if (!R) { if (rep) *rep = s->last; return !s->over; }
    SiteDay r = R->r;
    Xfer *xs = R->xs;   int nx = R->nx;
    Call *cs = R->cs;   int ncall = R->ncall;
    Strm *ss = R->ss;   int nstrm = R->nstrm;
    Ingest *ing = R->ing; int ning = R->ning;
    int ingest = R->ingest;
    uint64_t frames0 = R->frames0, drops0 = R->drops0, t0 = R->t0;

    /* ------------------------------------------------------- what happened */
    for (int i = 0; i < nx; i++) {
        Xfer *x = &xs[i];
        SiteTenant *t = &s->tenant[x->tenant];
        if (x->state == X_DONE) {
            t->bytes += x->got;
            r.finished++;
            r.bytes += x->got;
            int ms = x->ended - x->began;
            if (ms > t->worst_ms) t->worst_ms = ms;
            if (x->judged) t->finished++;
        } else if (x->sock >= 0) {
            net_tcp_close(s->net, x->sock);
            net_sock_free(s->net, x->sock);
        }
        /* Two things this person's machine was asked to do today, counted
         * whichever way they went. A page that never arrived used to take
         * the file with it and be counted once; both are counted now.
         *
         * `tried` is now what the TENANCY is judged on and r.sessions is what
         * the TOWER was asked for, and they are different totals on purpose:
         * a call centre agent's CRM is real traffic the building has to
         * carry, and it is not what makes their day good or bad. */
        if (x->judged) t->tried++;
        r.sessions++;
        if (t->worst_ms > r.worst_ms) r.worst_ms = t->worst_ms;
    }
    /* THE CALLS, read off the stack's own measurement of them and not off
     * anything this file counted. A call is as good as its worse direction:
     * concealment -- audio frames that had nothing to play, because the
     * packet was lost or arrived after the buffer had played the silence --
     * under two per cent, and one-way delay inside G.114's hundred and fifty
     * milliseconds. */
    for (int i = 0; i < ncall; i++) {
        Call *c = &cs[i];
        SiteTenant *t = &s->tenant[c->tenant];
        VoiceStats a, b;
        bool ok = net_voice_stats(s->net, c->up, &a) &&
                  net_voice_stats(s->net, c->down, &b);
        int conceal = 0, delay = 0;
        uint32_t jit = 0;
        if (ok) {
            conceal = a.conceal_ppm > b.conceal_ppm ? a.conceal_ppm : b.conceal_ppm;
            delay = (int)((a.delay_avg_us > b.delay_avg_us
                           ? a.delay_avg_us : b.delay_avg_us) / 1000);
            jit = a.jitter_us > b.jitter_us ? a.jitter_us : b.jitter_us;
            t->bytes += (long)(a.received + b.received) * NET_VOICE_PAYLOAD;
            r.bytes  += (long)(a.received + b.received) * NET_VOICE_PAYLOAD;
        } else {
            conceal = 1000000;          /* nothing came through at all       */
        }
        bool good = ok && conceal <= SITE_VOICE_CONCEAL_PPM &&
                    delay <= SITE_VOICE_DELAY_MS;
        if (conceal > t->conceal_ppm) t->conceal_ppm = conceal;
        if ((int)jit > t->jitter_us)  t->jitter_us = (int)jit;
        if (delay > t->delay_ms)      t->delay_ms = delay;
        t->tried++; r.sessions++;
        if (good) { t->finished++; r.finished++; }
        net_voice_stop(s->net, c->up);
        net_voice_stop(s->net, c->down);
    }
    /* THE STREAMS, and there is no partial credit. Every kilobyte, inside the
     * window, or the stream dropped and the viewers have gone. */
    for (int i = 0; i < nstrm; i++) {
        Strm *m = &ss[i];
        SiteTenant *t = &s->tenant[m->tenant];
        t->up_kb += m->got / 1024;
        t->bytes += m->got;
        r.bytes  += m->got;
        t->tried++; r.sessions++;
        if (m->got >= m->want) { t->finished++; r.finished++; }
        if (m->sock >= 0) { net_tcp_close(s->net, m->sock); net_sock_free(s->net, m->sock); }
    }
    for (int i = 0; i < ning; i++)
        if (ing[i].sock >= 0) {
            net_tcp_close(s->net, ing[i].sock);
            net_sock_free(s->net, ing[i].sock);
        }
    if (ingest >= 0) net_sock_free(s->net, ingest);
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
    nom_free(cs);
    nom_free(ss);
    nom_free(ing);
    nom_free(s->run);
    s->run = NULL;

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
        bool served = site_tenant_served(s, i);
        bool ignored = t->tried == 0 && t->strikes == 0 &&
                       s->day - t->day > SITE_FITOUT_DAYS;
        /* THEM BEING DOWN COSTS YOU DIFFERENTLY FROM A SLOW MORNING, and this
         * is the difference. Everybody else's bad day costs the landlord the
         * rent they did not earn. A web host's contract is uptime, and a day
         * their origin answered nothing at all is a day the landlord hands a
         * day's rent BACK -- so the same outage is twice the money it is for
         * an office, and it is the one bill in this game that can be paid by
         * a two hundred and twenty pound battery. It is only levied once the
         * fit-out window has closed, because nobody credits a service they
         * have not started yet. */
        if (t->kind == TEN_WEBHOST && t->tried > 0 && t->finished == 0)
            t->down = 1;
        if (t->kind == TEN_WEBHOST && t->moved && t->finished == 0 &&
            t->tried > 0 && s->day - t->day > SITE_FITOUT_DAYS) {
            t->sla = t->rent / 30;
            s->money -= t->sla;
            s->spent += t->sla;
            r.sla += t->sla;
        }
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
            /* AND THE WORK, SUMMED FROM THE ROWS `service` PRINTS. This
             * printed r.finished/r.sessions -- what the TOWER carried --
             * under the word "transfers", and a playtester with an office
             * and a call centre in the building read 134 here and 80 + 18
             * on the `service` page in the same second. See site_day_work. */
            int wdone = 0, wtried = 0;
            const char *wunit = "jobs";
            site_day_work(s, &wdone, &wtried, &wunit);
            buf_printf(out, "day %d: %d in, %d served, %d/%d desks addressed, "
                            "%d/%d %s done, %ld taken, %ld in hand\n",
                       r.day, r.tenants_in, r.tenants_served, r.connected,
                       r.desks, wdone, wtried, wunit, r.rent, s->money);
            if (r.hot[0]) {
                /* A DROP IS NOT NEWS; A DROP RATE IS.
                 *
                 * This warned on r.drops != 0, so it fired from day three of
                 * a tower whose every tenancy finished all its work, and a
                 * playtester said "by day 40 I had stopped reading it, which
                 * is a bad habit for the game to teach given how much else it
                 * wants me to read." They were right, and a warning nobody
                 * reads is worse than none: it is the line that is supposed
                 * to send them to `load` on the day it matters.
                 *
                 * A real network drops a frame now and then and nothing is
                 * wrong. One in a thousand is where a burst stops being a
                 * burst, and the rate is printed rather than the adjective,
                 * so the player can watch it climb instead of being told
                 * twice that something is happening. */
                bool loud = r.frames && r.drops * 1000 > r.frames;
                if (loud)
                    buf_printf(out, "        busiest port %s at %d%%; %llu of "
                                    "%llu frames dropped -- `load`, then "
                                    "`show <box>`\n", r.hot, r.hot_util,
                               (unsigned long long)r.drops,
                               (unsigned long long)r.frames);
                else
                    buf_printf(out, "        busiest port %s at %d%%\n",
                               r.hot, r.hot_util);
            }
            if (r.bill)
                buf_printf(out, "        the ISP bills the month: %ld for the "
                                "%d Mb circuit. %ld in hand\n",
                           r.bill, s->isp_mb, s->money);
            if (r.sla)
                buf_printf(out, "        %ld handed BACK to web hosts whose sites "
                                "were down: their lease is uptime\n", r.sla);
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
    /* THE SAME ARITHMETIC AS THE `service` ROWS, and nothing else. The MB is
     * everything the tower moved, including the traffic no tenancy is judged
     * on, and it says so rather than being a second total of the same word. */
    {
        int wdone = 0, wtried = 0;
        const char *wunit = "jobs";
        site_day_work(s, &wdone, &wtried, &wunit);
        buf_printf(out, "%d of %d %s finished inside the busy period, summed "
                        "over the `service` rows; %ld MB moved in all.\n",
                   wdone, wtried, wunit, r->bytes / (1024 * 1024));
    }
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
    /* THE NUMBER THAT ENDS THE RUN, ASKED FOR RATHER THAN REMEMBERED. This
     * said "Three" while `service` four hundred lines below computed it from
     * site_complaints_allowed() -- so a player with fourteen tenancies read
     * "Three ends the run" here and "5 filed complaints ends the run" there,
     * about the same rule, in the same session. Of every number in the game
     * this is the worst one to be wrong twice: it is the one the player is
     * counting against. */
    buf_printf(out, "%d complaint%s filed in all. %d ends the run.\n",
               s->complaints, s->complaints == 1 ? "" : "s",
               site_complaints_allowed(s));
    if (s->over) buf_printf(out, "\nTHE RUN IS OVER: %s\n", s->over_why);
}

void site_dump_service(const Site *s, Buf *out)
{
    buf_puts(out, "  deck tenant  trade      desks   up  addr   done  worst"
                  "   strikes  rent/day  files\n");
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
        char files[NET_NAME_MAX + 24];
        if (t->files_dev < 0) {
            /* NAME THE BOX THAT WOULD DO IT. "none" is true and useless when
             * the player owns a server standing in the right room with no
             * httpd on it: it reads as "buy a server" and the answer is one
             * word. */
            int idle = idle_server_for(s, i);
            if (idle >= 0)
                snprintf(files, sizeof files, "%s (no httpd)", s->dev[idle].name);
            else
                snprintf(files, sizeof files, "%s", "none");
        } else {
            const SiteDev *fd = &s->dev[t->files_dev];
            bool away = fd->floor != t->floor;
            snprintf(files, sizeof files, "%s%s", fd->name, away ? " <-" : "");
            if (away) offfloor = true;
        }
        buf_printf(out, "  %5d %6d  %-9s %5d %4d %5d  %5s %5dms  %7d%s  %8d  %s\n",
                   t->floor, t->tenant, site_tenant_kind_name(t->kind),
                   t->ndesk, up, ad, done, t->worst_ms,
                   t->strikes, t->complained ? "*" : " ", t->rent / 30, files);
        /* AND WHY, WHEN THERE IS A WHY, IN THEIR OWN TERMS. `done` reads
         * 12/20 for everybody and means a different thing on every row: a
         * transfer for an office, a call for a voice tenancy, a visitor for
         * a web host, a stream for a studio. A number that means four things
         * has to say which one it is on the day it matters. */
        char why[288];
        site_tenant_why(s, i, why, (int)sizeof why);
        if (why[0]) buf_printf(out, "          %s\n", why);
        if (t->sla)
            buf_printf(out, "          and %ld of rent handed BACK: they were "
                            "down, and their lease says what that costs.\n",
                       t->sla);
    }
    /* AND HOW MANY OF THOSE ENDS IT, which was a constant three nobody could
     * read anywhere. It scales with the building now, so it has to be said
     * out loud and it has to be said as a number the player can count
     * against the stars in the column above. It stays on the SHORT page:
     * of every number in this report it is the one being counted against,
     * and it moves as the building fills. */
    {
        int in = 0;
        for (int i = 0; i < s->ntenant; i++) if (s->tenant[i].moved) in++;
        int bear = site_complaints_allowed(s);
        buf_printf(out, "\n  %d filed complaints ends the run (a third of the %d "
                        "tenancies in, never\n  fewer than three). `service ?` "
                        "explains every column.\n", bear, in);
    }
    if (offfloor)
        buf_puts(out, "  <- is a tenancy served from another deck: their traffic "
                      "crosses a riser\n  to get there. `load` says which port "
                      "carries it.\n");
}

/* THE LEGEND, WHICH IS NOW SOMEWHERE RATHER THAN EVERYWHERE.
 *
 * `service` printed thirty-five lines of this every single time. By day 60 a
 * blind playtester's `service` was nine tenancy rows inside ninety per cent
 * legend, and the day-31 disaster -- four `**` lines, the only place in the
 * game the world tells you what it did to your kit -- was very nearly lost
 * in it. They asked for exactly this: the legend behind `service ?`.
 *
 * The project's rule is that a message must be honest AND complete, and
 * moving text is how completeness usually gets lost. So the split is: the
 * short page keeps every number that is a MEASUREMENT of this building on
 * this day, including the complaint threshold, and the legend keeps every
 * sentence that explains what a column MEANS -- which does not change from
 * day to day and is therefore the part that is worth reading once. Nothing
 * was deleted; `service ?` is one word and prints all of it, and the gate
 * asserts that every sentence that used to be on the page is still reachable
 * from it. */
void site_dump_service_legend(const Site *s, Buf *out)
{
    buf_puts(out, "`service` -- what each column is\n");
    buf_puts(out, "\n  up is desks whose port has LINK on it: copper in a socket at both\n"
                  "  ends, short enough to carry. addr is how many of those also got an\n"
                  "  ADDRESS, and only an addressed desk does any work -- which is the\n"
                  "  number `day` counts. up 20 addr 0 is twenty cables and no dhcp.\n"
                  "\n  a tenancy is served on a day when four fifths of what it was\n"
                  "  promised happened -- and WHAT it was promised depends on the\n"
                  "  trade. done counts transfers for an office, CALLS for a voice\n"
                  "  business, VISITORS off the internet for a web host and STREAMS\n"
                  "  for a studio, and `demand` says what each of those is. A web\n"
                  "  host is the one exception to the fraction: they are buying\n"
                  "  uptime, so it is nineteen in twenty, and a day their origin\n"
                  "  answered nothing costs you a day's rent back.\n"
                  "  Three days in a row without that is a complaint, and a * is one\n"
                  "  that has been filed. `load` says which port is full.\n"
                  /* AND WHAT `worst` IS, which this footer never said. It is
                   * `x->ended - x->began` off a finished TRANSFER and the
                   * call loop never touches it -- so a call centre's `worst`
                   * is measured on its agents' file and page traffic and has
                   * nothing to do with a call. A playtester on day 18 read
                   * `worst 780ms` beside `demand`'s "a call dies past 150 ms
                   * one way", could not reconcile them, and had to sit at a
                   * desk and run `voice` to find the real figure was 3.0 ms.
                   * Two true numbers that look like they should compare. */
                  "\n  worst is WALL TIME and not delay: the longest one transfer took\n"
                  "  from start to finish, for any desk in that tenancy, all day. It\n"
                  "  never comes off a call -- a voice tenancy's worst is measured on\n"
                  "  its agents' files and pages -- so it does not compare with the\n"
                  "  150 ms `demand` gives a call. What the calls sounded like is the\n"
                  "  line under the row.\n"
                  /* AND WHERE THOSE TWO VERBS LIVE. This said "`sit` at one
                   * of their desks and run `voice`" at a prompt that has
                   * neither: they are SESSION verbs (core/session.c), and
                   * the tower shell a blind playtester was reading this in
                   * answers "no such command" to both. A page that names a
                   * command the shell has not got is the one failure this
                   * project treats as fatal, so the sentence names what
                   * this shell really has first. */
                  "  From HERE the port is `load`, and `show <box>` prints how many\n"
                  "  frames it threw away and which of the four reasons it was. `sit`\n"
                  "  and `voice` are verbs of the SESSION and not of this shell: they\n"
                  "  are how you read the same fault off the tenant's own machine,\n"
                  /* NOT `tower`. A blind playtester followed this sentence
                   * out of a tenant's shell and got "tower: command not
                   * found", bare, with none of the guidance every other
                   * refusal in this game gives. `stand` is the word: `tower`
                   * comes back from the DESK, which is a different chair. */
                  "  and `stand` is the word that gets you out of their "
                  "chair again.\n");
    buf_puts(out, "\n  The line under a row is that tenancy's own account of the day,\n"
                  "  in the units their business counts.\n");
    {
        int in = 0;
        for (int i = 0; i < s->ntenant; i++) if (s->tenant[i].moved) in++;
        int bear = site_complaints_allowed(s);
        buf_printf(out, "\n  %d filed complaints ends the run. That is a third of the %d\n"
                        "  tenancies in the building, rounded up, and never fewer than\n"
                        "  three -- so it grows as you let the decks.\n", bear, in);
    }
    buf_puts(out,
                  "\n  files is the server their people actually pulled off yesterday.\n"
                  "  Their own machine if they have one and it is on; otherwise one on\n"
                  "  their deck; otherwise anything powered in the building. A server\n"
                  "  qualifies on ANY address it holds -- a socket or a tagged vlan\n"
                  "  subinterface, it makes no difference -- and the leg that answers is\n"
                  "  the one on the asking desk's own segment when it has one.\n");
    buf_puts(out, "  <- is a tenancy being served from another deck. Nothing refused\n"
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
        /* THE PEAK, WHICH IS WHAT THE COLUMN IS HEADED AND WHAT THE FOOTER
         * TELLS YOU TO READ.
         *
         * This called net_port_queue_us() -- the queue RIGHT NOW -- under a
         * heading that says "queue peak", so every row read 0us: by the time
         * a player types `load` the busy period is over and every queue has
         * drained. `show <box>` on the same port in the same second said
         * "the queue reached 406us", because it reads pt->qpeak_us. Two
         * functions, one fact, and the tool the README stakes the whole
         * difficulty model on was printing the wrong one -- while its own
         * footer said READ THE DROPS AND THE PEAK QUEUE.
         *
         * A playtester found it the only way anybody could: by disbelieving
         * one of the game's reports and checking it against another. */
        unsigned long long qus = net_port_qpeak_us(s->net, s->dev[bd].node, bp);
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
    else buf_puts(out, "\n  READ THE DROPS AND THE PEAK QUEUE, not busy: busy is a four-second\n"
                       "  average and the drops are not. `show <box>` says which of the four\n"
                       "  reasons each one was. `load ?` explains the columns.\n");
}

/* THE SAME SPLIT `service` GOT, AND FOR THE SAME MEASUREMENT: by day 60 the
 * legend was most of the page and the eight rows it is about were not. What
 * stays above is the one instruction that changes what the player does --
 * read the drops, not the average -- because that is the sentence the whole
 * difficulty model rests on and a player who skips it will misread every row.
 * The arithmetic that PROVES it moves here, where it can be read once. */
void site_dump_load_legend(const Site *s, Buf *out)
{
    (void)s;
    buf_puts(out,
        "`load` -- the busiest eight ports in the tower\n"
        "\n  port is <box>:<port>. Desks are not on it: a desk has one gigabit\n"
        "  card and the frames all meet somewhere else, which is the port you\n"
        "  are looking for.\n"
        "\n  speed is what the LINK negotiated -- the slowest of the port at each\n"
        "  end and the copper between them -- so a cat5e run to a gigabit switch\n"
        "  reads 1000Mb and the same run past 100 m does not come up at all.\n"
        /* THE LEGEND WAS FALSE AT THESE SPEEDS, AND IT WAS THE THING THAT
         * MADE THE TOOL USELESS.
         *
         * It promised that a port starts hurting past eighty per cent and
         * drops at a hundred. A playtester's tower died with nothing above
         * 31%, every queue reading 0ms, and three complaints filed -- so the
         * instrument said calm while the building fell over, and there was no
         * move to make.
         *
         * The arithmetic: a 48 KB egress buffer is 394 us of wire at a
         * gigabit. A floor of desks all fetching at once empties into that in
         * well under a millisecond, so a port drops on bursts while its
         * average over a four-second busy period is single digits. Busy is an
         * average and the drops are not; saying so is the whole difference
         * between a tool that points at the problem and one that alibis it. */
        "\n  busy is the SHARE OF THE BUSY PERIOD this port spent clocking bits,\n"
        "  averaged over four seconds. Drops do not wait for it to be high: a\n"
        "  48 KB buffer is 394us of wire at a gigabit, so a deck of desks\n"
        "  fetching at once can overrun it in bursts while the average sits in\n"
        "  single figures. READ THE DROPS AND THE PEAK QUEUE, not the average.\n"
        "  `show <box>` says how many were lost and which of the four reasons\n"
        "  it was.\n"
        "\n  queue peak is the deepest that port's egress queue ever got, in\n"
        "  microseconds of wire -- the scale it happens at. drops is the whole\n"
        "  life of the port since it was cabled, NOT yesterday: `status` is the\n"
        "  one that reports the day just gone.\n");
}
