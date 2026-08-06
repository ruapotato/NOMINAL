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
 * for a reason `netstat -P` prints in words. The reason a flat tower falls
 * over and a segmented one does not is that the frames really go somewhere
 * different, and nobody wrote down that they should.
 *
 * DETERMINISM. Session start offsets come from a Rng seeded from the world
 * seed and the day number, so the same seed plays the same day, always.
 */
#include <string.h>
#include <stdio.h>
#include "site.h"

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
int site_serve(Site *s, int tenant, int dev, CableKind k)
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
        done++;
    }
    return done;
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
 * One transfer at a time per desk, because a person does one thing and then
 * the next: the web fetch first and then the file. Both are ordinary HTTP
 * over the ordinary TCP in netstack.c, driven a millisecond at a time so
 * that every desk in the building is pulling at once -- which is the whole
 * point. A transfer that has not finished when the busy period ends is a
 * person who did not get their work done, and that is the only definition of
 * "bad service" anywhere in this program.
 */
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
static uint32_t file_server_for(const Site *s, int tenant)
{
    uint32_t any = 0, floor = 0;
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        if (d->kind != SDEV_SERVER || !d->powered) continue;
        uint32_t ip = net_if_get_addr(s->net, d->node, 0);
        if (!ip) continue;
        if (d->tenant && d->tenant == s->tenant[tenant].tenant) return ip;
        if (!floor && d->floor == s->tenant[tenant].floor) floor = ip;
        if (!any) any = ip;
    }
    return floor ? floor : any;
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
        if (!t->moved) continue;
        r.tenants_in++;
        r.desks += t->ndesk;
        uint32_t files = file_server_for(s, i);
        for (int j = 0; j < t->ndesk && nx < cap; j++) {
            int d = t->desk0 + j;
            if (net_port_state(s->net, s->dev[d].node, 0) != PORT_UP) continue;
            if (!net_if_get_addr(s->net, s->dev[d].node, 0)) continue;
            r.connected++;
            Xfer *x = &xs[nx++];
            memset(x, 0, sizeof *x);
            x->dev = d;
            x->tenant = i;
            x->sock = -1;
            x->leg = 0;
            x->kb = SITE_DESK_WEB_KB;
            x->want = (long)SITE_DESK_WEB_KB * 1024;
            x->dst = web;
            (void)files;
            /* WHEN THEY START. Spread across the first tenth of the busy
             * period, from the seed, because a building does not begin work
             * on the same millisecond and a thundering herd would be a
             * difficulty knob rather than a day. */
            x->start = (int)rng_range(&rng, 0, SITE_BUSY_MS / 10);
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
            /* THE SECOND LEG. The web fetch done, the same desk goes to the
             * file server -- a different destination, over a path the player
             * chose, which is where architecture stops being decoration. */
            if (x->state == X_DONE && x->leg == 0) {
                SiteTenant *t = &s->tenant[x->tenant];
                t->finished++;
                t->tried++;
                t->bytes += x->got;
                int ms = x->ended - x->began;
                if (ms > t->worst_ms) t->worst_ms = ms;
                r.sessions++; r.finished++; r.bytes += x->got;
                uint32_t files = file_server_for(s, x->tenant);
                x->leg = 1;
                x->kb = SITE_DESK_FILE_KB;
                x->want = (long)SITE_DESK_FILE_KB * 1024;
                x->got = 0;
                x->dst = files ? files : web;
                x->state = X_WAIT;
                x->start = tick;
                x->sock = -1;
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
        if (x->state != X_DONE || x->leg == 1) { t->tried++; r.sessions++; }
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
     * A tenancy that has never had a working day is WAITING, not suffering:
     * they have no service yet, they pay nothing, and they do not complain
     * about a network nobody has promised them. Strikes only start once
     * somebody has been connected -- so a complaint is always about service
     * that got worse, which is what the player can actually be held to. */
    for (int i = 0; i < s->ntenant; i++) {
        SiteTenant *t = &s->tenant[i];
        if (!t->moved) continue;
        bool served = t->tried > 0 && t->finished * 5 >= t->tried * 4;
        if (served) {
            r.tenants_served++;
            long day_rent = t->rent / 30;
            s->money += day_rent;
            s->rent_taken += day_rent;
            r.rent += day_rent;
            t->strikes = 0;
        } else if (t->tried > 0 || t->strikes > 0) {
            /* They have people plugged in and the work is not finishing. */
            if (t->strikes < 255) t->strikes++;
            if (t->strikes >= 3 && !t->complained) {
                t->complained = 1;
                s->complaints++;
                r.complaints_today++;
            }
        }
    }

    /* -------------------------------------------------------- and the end */
    if (s->complaints >= 3) {
        s->over = 1;
        snprintf(s->over_why, sizeof s->over_why,
                 "three tenancies have filed a complaint. The lease is not renewed.");
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
            buf_printf(out, "day %d: %d in, %d served, %d/%d desks up, "
                            "%d/%d transfers finished, %ld taken, %ld in hand\n",
                       r.day, r.tenants_in, r.tenants_served, r.connected,
                       r.desks, r.finished, r.sessions, r.rent, s->money);
            if (r.hot[0])
                buf_printf(out, "        busiest port %s at %d%%%s\n", r.hot,
                           r.hot_util,
                           r.drops ? "; something is dropping -- `load`, then "
                                     "`show <box>`" : "");
            if (r.complaints_today)
                buf_printf(out, "        %d COMPLAINT%s filed today (%d in all)\n",
                           r.complaints_today, r.complaints_today == 1 ? "" : "S",
                           s->complaints);
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
    buf_printf(out, "the circuit is %d Mb (%ld a month).\n",
               s->isp_mb, site_isp_price(s->isp_mb));
    if (!r->day) {
        buf_puts(out, "no day has been run yet: `day` advances the clock.\n");
        return;
    }
    buf_printf(out, "%d tenancies in, %d of them served yesterday. "
                    "%d of %d desks have a live port.\n",
               r->tenants_in, r->tenants_served, r->connected, r->desks);
    buf_printf(out, "%d of %d transfers finished inside the busy period; "
                    "%ld MB moved.\n", r->finished, r->sessions,
               r->bytes / (1024 * 1024));
    buf_printf(out, "%llu frames handled, %llu lost.\n",
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
    buf_puts(out, "  floor tenant  desks   up  addr   done  worst   strikes  rent/day\n");
    for (int i = 0; i < s->ntenant; i++) {
        const SiteTenant *t = &s->tenant[i];
        if (!t->moved) continue;
        int up = site_tenant_connected(s, i), ad = site_tenant_addressed(s, i);
        char done[16];
        if (t->tried) snprintf(done, sizeof done, "%d/%d", t->finished, t->tried);
        else snprintf(done, sizeof done, "-");
        buf_printf(out, "  %5d %6d  %5d %4d %5d  %5s %5dms  %7d%s  %8d\n",
                   t->floor, t->tenant, t->ndesk, up, ad, done, t->worst_ms,
                   t->strikes, t->complained ? "*" : " ", t->rent / 30);
    }
    buf_puts(out, "\n  a tenancy is served on a day when four fifths of its people got\n"
                  "  their work done. Three days in a row without that is a complaint,\n"
                  "  and a * is one that has been filed. `load` says which port is full.\n");
}

void site_dump_load(const Site *s, Buf *out)
{
    uint64_t window = SITE_BUSY_MS * 1000ull;
    buf_puts(out, "  port                 speed   busy   queue   drops\n");
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
        buf_printf(out, "  %-20s %5dMb %5d%%  %5llums %7llu\n", nm,
                   net_port_speed(s->net, s->dev[bd].node, bp),
                   (int)((bb * 100) / window),
                   (unsigned long long)(net_port_queue_us(s->net, s->dev[bd].node, bp) / 1000),
                   (unsigned long long)net_port_drops(s->net, s->dev[bd].node, bp));
    }
    if (!shown) buf_puts(out, "  nothing is cabled up.\n");
    else buf_puts(out, "\n  busy is the share of the last busy period this port spent clocking\n"
                       "  bits. Past about eighty per cent the queue behind it starts to be\n"
                       "  latency somebody can feel; at a hundred it is dropping. The evidence\n"
                       "  is `show <box>` -- the port counters say how many and why.\n");
}
