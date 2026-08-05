/* session.c — the tower and the shell, in one session. See session.h.
 *
 * Everything below is either a thing a person does with their body (walk,
 * ride the lift, push the cart up to a rack) or a thing they do with their
 * hands once they are standing in front of the box. Nothing here decides
 * whether anything works: it calls site_* for the world and kernel_run() for
 * the operating system, and both of those are the same functions the 3D
 * shell and the break-fix bench call. There is no second implementation of
 * anything in this file, which is the only reason a blind playtest of it
 * means anything.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "session.h"
#include "kernel.h"

/* A machine standing in the player's tower is plugged into the player's
 * network, not into the one core/netsite.c keeps for the break-fix game.
 * See the note on Machine.net_home. */
void netsite_pin(Machine *m, struct Net *n, int node);

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
         * `install switch8 #41` parsed as an empty line -- and #41 is the
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

/* ------------------------------------------------------------- where you are */
static const Room *room_of(const Session *ses, int r)
{
    if (r < 0 || r >= ses->b.nrooms) return NULL;
    return &ses->b.rooms[r];
}

static void room_label(const Session *ses, int r, char *out, size_t cap)
{
    const Room *rm = room_of(ses, r);
    if (!rm) { snprintf(out, cap, "nowhere"); return; }
    snprintf(out, cap, "f%d %s #%d", rm->floor, bld_kind_name(rm->kind), r);
}

static int here_floor(const Session *ses)
{
    const Room *rm = room_of(ses, ses->room);
    return rm ? rm->floor : 0;
}

/* A room, however the player felt like spelling it: "#41", "f3.comms",
 * "comms" for the one on this floor, or the name of a box, which means the
 * room that box is in. The last spelling is the one that matters -- reaching
 * a machine means being in the room with it, so "walk to that switch" has to
 * be one command and not a floor plan lookup. */
static int room_arg(const Session *ses, const char *spec)
{
    if (!spec || !*spec) return -1;
    int r = site_room_by_name(&ses->s, spec);
    if (r >= 0) return r;
    char buf[64];
    snprintf(buf, sizeof buf, "f%d.%s", here_floor(ses), spec);
    r = site_room_by_name(&ses->s, buf);
    if (r >= 0) return r;
    int d = site_dev_by_name(&ses->s, spec);
    if (d >= 0 && ses->s.dev[d].room != BLD_NOROOM) return ses->s.dev[d].room;

    /* THE NEAREST ONE, WHEREVER IT IS. `go mdf` worked on the ground floor
     * and failed on the eighth, because there is only one MDF and it is not
     * on your floor -- and the same is true of goods in and the lobby. A
     * player standing on floor 8 who types `go mdf` means the MDF. So: the
     * room of that kind with the shortest walk from here, which is also
     * what `go comms` should mean in a tower with nine of them. The
     * spellings are site.c's, because two spellings of one room is a bug. */
    static const struct { const char *n; int k; } K[] = {
        { "comms", RM_COMMS }, { "mdf", RM_MDF }, { "riser", RM_RISER },
        { "goods", RM_GOODS }, { "lobby", RM_LOBBY }, { "plant", RM_PLANT },
        { "server", RM_SERVER }, { "office", RM_OFFICE },
        { "residence", RM_RESIDENCE }, { "retail", RM_RETAIL },
        { "stair", RM_STAIR }, { "liftlobby", RM_LIFTLOBBY },
        { "toilet", RM_TOILET }, { "corridor", RM_CORRIDOR }, { NULL, 0 }
    };
    int kind = -1;
    for (int i = 0; K[i].n; i++) if (strcmp(K[i].n, spec) == 0) kind = K[i].k;
    if (kind < 0) return -1;
    double *dist = nom_alloc(sizeof(double) * (size_t)ses->b.nrooms);
    if (!bld_walk_all(&ses->b, ses->room, dist)) { nom_free(dist); return -1; }
    int best = -1;
    double bd = BLD_INF;
    for (int i = 0; i < ses->b.nrooms; i++)
        if (ses->b.rooms[i].kind == kind && dist[i] < bd) { bd = dist[i]; best = i; }
    nom_free(dist);
    return best;
}

static bool dev_here(const Session *ses, int dev)
{
    return dev >= 0 && dev < ses->s.ndev && ses->s.dev[dev].room == ses->room;
}

/* A device by name or index, and where it is if it is not here -- because
 * "no such device" and "it is four floors up" are different problems and a
 * player who is told the first when the second is true goes looking in the
 * wrong place. */
static int dev_arg(const Session *ses, const char *a)
{
    int d = site_dev_by_name(&ses->s, a);
    if (d >= 0) return d;
    if (a[0] >= '0' && a[0] <= '9') {
        int n = atoi(a);
        if (n >= 0 && n < ses->s.ndev) return n;
    }
    return -1;
}

static bool need_here(Session *ses, const char *a, int *dev, Buf *out)
{
    int d = dev_arg(ses, a);
    if (d < 0) { buf_printf(out, "there is no box called %s in this building.\n", a); return false; }
    if (!dev_here(ses, d)) {
        char w[48];
        room_label(ses, ses->s.dev[d].room, w, sizeof w);
        buf_printf(out, "%s is in %s and you are not. `go %s` first -- you cannot\n"
                        "  configure a box you are not standing in front of.\n",
                   ses->s.dev[d].name, w, ses->s.dev[d].name);
        return false;
    }
    *dev = d;
    return true;
}

/* ------------------------------------------------------- the real machines */
/* WHAT THE BOX ON THE DISK SAYS ABOUT ITSELF.
 *
 * A pc or a server in the tower has an operating system in it, and that
 * operating system configures its own card from /etc/net/interfaces the way
 * every other machine in this game does. So the address the player gave the
 * DEVICE has to be written onto the DISK, or netd will come up, read a file
 * that says something else, and the machine will disagree with the network
 * it is cabled into -- which would be a fault nobody built and nobody could
 * diagnose, because the two facts live in different places. One source: the
 * disk. This copies the player's decision onto it. */
static void sync_disk(Session *ses, int dev)
{
    Machine *m = ses->mach[dev];
    if (!m) return;
    Net *n = ses->s.net;
    int node = ses->s.dev[dev].node;
    uint32_t a = net_if_get_addr(n, node, 0);
    uint32_t mk = net_if_get_mask(n, node, 0);
    uint32_t gw = net_get_gateway(n, node);
    uint32_t ns = net_get_resolver(n, node);
    char cfg[320], ip[20], g[20], s[20];
    if (a) {
        net_fmt_ip(a, ip, sizeof ip);
        int len = snprintf(cfg, sizeof cfg,
                           "iface eth0\n  address %s\n  netmask %d\n", ip,
                           net_mask_len(mk));
        if (gw) {
            net_fmt_ip(gw, g, sizeof g);
            len += snprintf(cfg + len, sizeof cfg - (size_t)len, "  gateway %s\n", g);
        }
    } else {
        snprintf(cfg, sizeof cfg, "iface eth0\n  address dhcp\n");
    }
    vfs_write(&m->disk, "/etc/net/interfaces", cfg, strlen(cfg));
    if (ns) {
        char rc[64];
        net_fmt_ip(ns, s, sizeof s);
        snprintf(rc, sizeof rc, "nameserver %s\nsearch nomnix.org\n", s);
        vfs_write(&m->disk, "/etc/resolv.conf", rc, strlen(rc));
    }
    char host[NET_NAME_MAX + 2];
    snprintf(host, sizeof host, "%s\n", ses->s.dev[dev].name);
    vfs_write(&m->disk, "/etc/hostname", host, strlen(host));
    /* Whatever it had applied is now stale. */
    m->net_cfg = 0;
}

/* Install and boot the box, once, the first time anybody opens a lead on
 * it. A booted machine is 13.5 MB; a rack of boxes nobody has looked at
 * costs nothing. */
static Machine *box_of(Session *ses, int dev, Buf *out)
{
    if (ses->mach[dev]) return ses->mach[dev];
    Machine *m = nom_alloc(sizeof *m);
    memset(m, 0, sizeof *m);
    machine_install(m, ses->seed + 7000 + (uint64_t)dev);
    ses->mach[dev] = m;
    sync_disk(ses, dev);
    netsite_pin(m, ses->s.net, ses->s.dev[dev].node);
    machine_boot(m);
    if (out) {
        buf_put(out, m->boot.console.p, m->boot.console.len);
        buf_printf(out, "\n[%s at %s]\n", m->boot.running ? "UP" : "DOWN",
                   boot_stage_name(m->boot.failed_at));
    }
    return m;
}

/* --------------------------------------------------------------- walking */
/* Real metres through real doors. A room with no route is refused and says
 * so: the riser is a shaft with a ladder in it and building.h has always
 * said you cannot walk into one, so `go f3.riser` is a refusal that came
 * out of the geometry rather than out of a rule somebody wrote. */
static bool walk_to(Session *ses, int dst, Buf *out, bool quiet)
{
    if (dst < 0 || dst >= ses->b.nrooms) { buf_puts(out, "no such room.\n"); return false; }
    if (dst == ses->room) { if (!quiet) buf_puts(out, "you are already there.\n"); return true; }
    double *d = nom_alloc(sizeof(double) * (size_t)ses->b.nrooms);
    bool ok = bld_walk_all(&ses->b, ses->room, d);
    double m = ok ? d[dst] : BLD_INF;
    nom_free(d);
    char w[48];
    room_label(ses, dst, w, sizeof w);
    if (m >= BLD_INF) {
        buf_printf(out, "there is no way to walk from here into %s.\n", w);
        if (ses->b.rooms[dst].kind == RM_RISER)
            buf_puts(out, "  a riser is a shaft. Cable goes up it; people do not.\n");
        return false;
    }
    int metres = (int)(m + 0.5);
    ses->walked += metres;
    int was = here_floor(ses);
    ses->room = dst;
    if (!quiet) {
        buf_printf(out, "you walk %d m to %s", metres, w);
        if (ses->b.rooms[dst].floor != was) buf_puts(out, ", by the stairs");
        buf_puts(out, ".\n");
    }
    return true;
}

/* --------------------------------------------------------------- looking */
static void dev_line(const Session *ses, int i, Buf *out)
{
    const SiteDev *d = &ses->s.dev[i];
    char ip[20];
    buf_printf(out, "    %-12s %-9s", d->name, site_kind_name(d->kind));
    if (site_kind_is_switch(d->kind)) {
        int used = 0;
        for (int p = 0; p < d->nports; p++)
            if (net_port_state(ses->s.net, d->node, p) != PORT_NOCABLE) used++;
        buf_printf(out, " %d/%d ports used", used, d->nports);
    } else {
        uint32_t a = net_if_get_addr(ses->s.net, d->node, 0);
        if (a) {
            net_fmt_ip(a, ip, sizeof ip);
            buf_printf(out, " %s/%d", ip,
                       net_mask_len(net_if_get_mask(ses->s.net, d->node, 0)));
        } else buf_printf(out, " no address, %d port%s", d->nports,
                          d->nports == 1 ? "" : "s");
        if (ses->mach[i]) buf_puts(out, "  [an OS is running on it]");
    }
    /* A PORT AN AGENT CAN NAME WITHOUT SEEING IT. `plug core:2` is only
     * usable if something printed which sockets are empty. */
    int free = site_free_port(&ses->s, i);
    if (free >= 0) buf_printf(out, "   next free port %s:%d", d->name, free);
    else buf_printf(out, "   all %d ports used", d->nports);
    buf_putc(out, '\n');
}

static void do_look(Session *ses, Buf *out)
{
    const Room *rm = room_of(ses, ses->room);
    char w[48];
    room_label(ses, ses->room, w, sizeof w);
    buf_printf(out, "%s, %.0f m2", w, bld_room_area(rm));
    if (rm->tenant) buf_printf(out, ", let to tenant %d", rm->tenant);
    buf_putc(out, '\n');

    int n = 0;
    for (int i = 0; i < ses->s.ndev; i++) if (ses->s.dev[i].room == ses->room) {
        if (!n++) buf_puts(out, "  kit in this room:\n");
        dev_line(ses, i, out);
    }
    if (!n) buf_puts(out, "  there is no kit in this room.\n");

    /* The ways out, because a player who cannot see the walls has to be told
     * where the doors are before `go` means anything. */
    buf_puts(out, "  ways out:");
    int doors = 0;
    for (int i = 0; i < ses->b.ndoors; i++) {
        const Door *dr = &ses->b.doors[i];
        int other = -1;
        if (dr->a == ses->room) other = dr->b;
        else if (dr->b == ses->room) other = dr->a;
        if (other < 0) continue;
        bool seen = false;
        for (int k = 0; k < i; k++)
            if ((ses->b.doors[k].a == ses->room && ses->b.doors[k].b == other) ||
                (ses->b.doors[k].b == ses->room && ses->b.doors[k].a == other))
                seen = true;
        if (seen) continue;
        buf_printf(out, "%s #%d %s", doors++ ? "," : "", other,
                   bld_kind_name(ses->b.rooms[other].kind));
    }
    if (!doors) buf_puts(out, " none");
    buf_puts(out, "\n  (`go #<n>` by number, `go <kind>` for one on this floor,\n"
                  "   `go <box>` for the room a box is in)\n");
}

static void do_where(Session *ses, Buf *out)
{
    char w[48];
    room_label(ses, ses->room, w, sizeof w);
    buf_printf(out, "you are in %s, %ld m walked so far, %ld left to spend\n",
               w, ses->walked, ses->s.money);
    int n = 0;
    for (int i = 0; i < ses->s.ndev; i++) if (ses->s.dev[i].room == ses->room) n++;
    buf_printf(out, "%d box%s in reach, %d of %d floors in service\n",
               n, n == 1 ? "" : "es", ses->floors, ses->b.floors);
    if (ses->plugged >= 0)
        buf_printf(out, "the cart's %s lead is in %s\n", ses->hdmi ? "hdmi" : "serial",
                   ses->s.dev[ses->plugged].name);
    if (ses->spool_kind >= 0) {
        buf_printf(out, "%d m of %s on the spool in your hands", ses->spool_left,
                   site_cable_name((CableKind)ses->spool_kind));
        if (ses->cab_dev >= 0)
            buf_printf(out, ", one end in %s port %d",
                       ses->s.dev[ses->cab_dev].name, ses->cab_port);
        buf_puts(out, "\n");
    }
}

/* ---------------------------------------------------------------- the help */
static void do_help(const Session *ses, Buf *out)
{
    if (ses->where == SES_SHELL) {
        buf_printf(out,
            "this is a REAL SHELL on %s -- the same operating system every\n"
            "other machine in this game runs, on an emulated processor.\n"
            "  ip   route   netstat   ping <addr>   svc   ps   dmesg\n"
            "  cat /etc/net/interfaces      what its card is configured from\n"
            "  man                          the manuals it ships with\n"
            "its address came off its own disk. Edit that file and netd will\n"
            "apply what you wrote, right or wrong.\n"
            "  unplug            take the lead out and stand up again\n",
            ses->s.dev[ses->plugged].name);
        return;
    }
    if (ses->where == SES_MGMT) {
        buf_printf(out,
            "the management line on %s. One operation at a time. The box is\n"
            "assumed, so `addr 10.0.1.1/24` means this one.\n\n",
            ses->s.dev[ses->plugged].name);
        site_cmd((Site *)&ses->s, "help", out);
        buf_puts(out, "unplug                         put the lead back on the cart\n");
        return;
    }
    buf_puts(out,
        "YOU ARE THE IT DEPARTMENT OF A BUILDING. On day one it holds exactly\n"
        "one thing: the ISP's socket on the wall of the MDF, with nothing\n"
        "plugged into it. Every switch, every metre of copper and every\n"
        "address after that is yours. Tenants move in on a schedule, they pay\n"
        "rent, and what they ask for is what walks you into the limits.\n"
        "\n"
        "WHERE YOU ARE -- and it matters. You can only touch what is in the\n"
        "room with you, and walking is metres of real building.\n"
        "  where              floor, room, what is in reach, how far you walked\n"
        "  look               what is in this room, and the ways out of it\n"
        "  map                an ASCII plan of this floor\n"
        "  go <room>          walk. `go comms` `go f3.office` `go #41` `go core`\n"
        "                     -- a box's name walks you to the room it is in\n"
        "  lift <floor>       take the lift. Only floors in service have a button\n"
        "  open               put the next floor in service\n"
        "  desk               walk back and sit down at your own workstation,\n"
        "                     where the support tickets are\n"
        "\n"
        "BUYING. Kit is installed where you are standing.\n"
        "  buy <kind> [name]  switch8 120   switch24 400   router 650\n"
        "                     pc 480        server 1350\n"
        "\n"
        "CABLING, which is four things a person does and four things you type:\n"
        "  spool cat6         take a drum off the shelf. cat5e, cat6, fibre\n"
        "  plug <box>:<port>  one end into a socket in THIS room\n"
        "  go <room>          walk to the other end\n"
        "  plug <box>:<port>  the other end in. The run is measured through the\n"
        "                     tray -- a different route from the one you walked\n"
        "                     -- and charged by the metre. A run past what the\n"
        "                     copper carries is laid, paid for, and does not\n"
        "                     come up\n"
        "  plug <box>:        a bare colon takes the next free port\n"
        "  spool              what is on the drum   `spool back` puts it away\n"
        "  cable <a> <b> [kind]  the four steps above, done and printed. It is\n"
        "                     a shorthand, not a shortcut: it still walks\n"
        "  uncable <n>        pull one out\n"
        "\n"
        "CONFIGURING. You must be in the room with the box.\n"
        "  addr <box> <ip>/<bits>    gw <box> <ip>      resolver <box> <ip>\n"
        "  router <box> on|off       vlan <box> <port> <n>\n"
        "  subif <box> <if> <nic> <vlan> <ip>/<bits>    trunk <box> <port> <v>..\n"
        "  dhcpd <box> <first> <count> <bits> <gw> <dns>     dhcp <box>\n"
        "  ping <box> <ip>   trace <box> <ip>   resolve <box> <name>\n"
        "                     a real echo request, from that box, over the\n"
        "                     copper you laid. Nothing is reachable by default\n"
        "\n"
        "THE CRASH CART. You push it up to a box and plug a lead in.\n"
        "  plug <box>         serial. A switch, a router or the handoff gives\n"
        "                     you its MANAGEMENT LINE; a pc or a server gives\n"
        "                     you a SHELL on the real operating system in it\n"
        "  plug hdmi <box>    the display lead\n"
        "  unplug             lead back on the cart, and you can walk again\n"
        "\n"
        "READING THE STATE\n"
        "  show [box]  links  money  demand  rooms [floor]  frames\n"
        "  demand             who is moving in, when, and what they want. The\n"
        "                     arithmetic at the bottom is the shape of the job\n");
}

/* --------------------------------------------------------------- the lift */
static int lift_lobby(const Session *ses, int floor)
{
    int r = bld_find(&ses->b, floor, RM_LIFTLOBBY);
    if (r < 0) r = bld_find(&ses->b, floor, RM_LOBBY);
    return r;
}

static void do_lift(Session *ses, int f, Buf *out)
{
    if (f < 0 || f >= ses->b.floors) {
        buf_printf(out, "this lift does not pass floor %d.\n", f);
        return;
    }
    /* The button for a floor nobody has opened is not lit. Same rule as
     * lift.gd, same words, because it is the same lift. */
    if (f >= ses->floors) {
        buf_printf(out, "floor %d is not in service. The button is not lit.\n"
                        "  `open` puts the next floor into service.\n", f);
        return;
    }
    int from = lift_lobby(ses, here_floor(ses));
    int to = lift_lobby(ses, f);
    if (from < 0 || to < 0) { buf_puts(out, "there is no lift in this building.\n"); return; }
    if (from != ses->room && !walk_to(ses, from, out, false)) return;
    if (f == here_floor(ses)) { buf_puts(out, "you are on that floor already.\n"); return; }
    ses->room = to;
    buf_printf(out, "floor %d.\n", f);
    do_look(ses, out);
}

static void do_open(Session *ses, Buf *out)
{
    if (ses->floors >= ses->b.floors) {
        buf_puts(out, "every floor in this tower is already in service.\n");
        return;
    }
    int f = ses->floors++;
    int lets = 0, drops = 0;
    for (int i = 0; i < ses->b.nrooms; i++)
        if (ses->b.rooms[i].floor == f && ses->b.rooms[i].tenant) lets++;
    for (int i = 0; i < ses->s.ntenant; i++)
        if (ses->s.tenant[i].floor == f) drops += ses->s.tenant[i].drops;
    buf_printf(out, "floor %d is in service. %d let space%s on it, wanting %d "
                    "drop%s between them.\n", f, lets, lets == 1 ? "" : "s",
               drops, drops == 1 ? "" : "s");
}

/* ------------------------------------------------------------- the cart */
static void do_plug(Session *ses, const char *what, bool hdmi, Buf *out)
{
    int d;
    if (!need_here(ses, what, &d, out)) return;
    const SiteDev *dev = &ses->s.dev[d];
    if (hdmi) {
        if (dev->kind == SDEV_PC || dev->kind == SDEV_SERVER)
            buf_printf(out, "%s: there is a display output on the back of it, and\n"
                            "  over this line all you would get is [graphical display].\n"
                            "  A picture is for somebody standing there. Use the serial\n"
                            "  lead: `plug %s`.\n", dev->name, dev->name);
        else
            buf_printf(out, "%s: no display output on the back of it. Use the "
                            "serial lead.\n", dev->name);
        return;
    }
    ses->plugged = d;
    ses->hdmi = false;
    if (dev->kind == SDEV_PC || dev->kind == SDEV_SERVER) {
        ses->where = SES_SHELL;
        bool first = ses->mach[d] == NULL;
        buf_printf(out, "serial console on %s.\n", dev->name);
        Machine *m = box_of(ses, d, first ? out : NULL);
        if (!first)
            buf_printf(out, "[%s at %s]\n", m->boot.running ? "UP" : "DOWN",
                       boot_stage_name(m->boot.failed_at));
        buf_puts(out, "you are root on it. `help` for what this line is, "
                      "`unplug` to leave.\n");
        return;
    }
    ses->where = SES_MGMT;
    buf_printf(out, "management line on %s\n\n", dev->name);
    site_dump_dev(&ses->s, d, out);
    buf_puts(out, "\nthis line takes one operation at a time and the box is "
                  "assumed:\n  `addr 10.0.1.1/24`, `ping 10.0.1.10`, `help`, "
                  "`unplug`.\n");
}

/* ------------------------------------------------------------- the spool */
/* FOUR THINGS A PERSON DOES, and the socket does all four of them.
 *
 *   spool cat6            pick a drum up. 305 m on it, which is a drum.
 *   plug core:2           one end into a socket in this room
 *   go comms              walk. The building charges you the metres.
 *   plug panel:6          the other end, in the room you walked to
 *
 * There is no verb that lays a cable without going to both ends, because
 * there is no such thing in the 3D shell and a socket player who could do
 * it would not be testing the game being shipped. `cable a b` exists and is
 * a MACRO: it performs exactly these steps and prints each one, so the
 * transcript shows what actually happened.
 *
 * TWO LENGTHS, deliberately different. The metres you walk are through
 * doors and down stairs; the metres of copper are the tray route, up into
 * the ceiling and down the riser. bld_walk_all() and bld_cable_all() are
 * separate functions for exactly this reason and the player is choosing
 * between them every time they decide where to put a box. */
#define SPOOL_DRUM_M  305     /* what is on a drum of it                    */

static CableKind kind_arg(const char *a, bool *ok)
{
    *ok = true;
    if (strcmp(a, "cat5e") == 0) return CAB_CAT5E;
    if (strcmp(a, "cat6") == 0)  return CAB_CAT6;
    if (strcmp(a, "fibre") == 0) return CAB_FIBRE;
    *ok = false;
    return CAB_CAT6;
}

static void do_spool(Session *ses, int n, char *t[MAXTOK], Buf *out)
{
    /* `spool` on its own: what is in your hands. */
    if (n < 2) {
        if (ses->spool_kind < 0) {
            buf_puts(out, "your hands are empty. `spool cat6` takes a drum off "
                          "the shelf --\n  cat5e, cat6 or fibre, and the run is "
                          "charged by the metre when you\n  finish it.\n");
            return;
        }
        buf_printf(out, "%d m of %s on the spool.", ses->spool_left,
                   site_cable_name((CableKind)ses->spool_kind));
        if (ses->cab_dev >= 0)
            buf_printf(out, " One end is in %s port %d.",
                       ses->s.dev[ses->cab_dev].name, ses->cab_port);
        buf_putc(out, '\n');
        return;
    }
    if (strcmp(t[1], "back") == 0 || strcmp(t[1], "down") == 0) {
        if (ses->cab_dev >= 0)
            buf_printf(out, "you pull the end back out of %s port %d.\n",
                       ses->s.dev[ses->cab_dev].name, ses->cab_port);
        ses->spool_kind = -1; ses->cab_dev = -1;
        buf_puts(out, "spool back on the shelf.\n");
        return;
    }
    bool ok;
    CableKind k = kind_arg(t[1], &ok);
    if (!ok) { buf_printf(out, "no such cable: %s. cat5e, cat6 or fibre.\n", t[1]); return; }
    if (ses->cab_dev >= 0) {
        buf_printf(out, "you are in the middle of a run -- one end is in %s port "
                        "%d.\n  Finish it, or `spool back` to pull it out.\n",
                   ses->s.dev[ses->cab_dev].name, ses->cab_port);
        return;
    }
    ses->spool_kind = (int)k;
    ses->spool_left = SPOOL_DRUM_M;
    ses->cab_dev = -1;
    buf_printf(out, "you have %d m of %s on the spool. `plug <box>:<port>` puts "
                    "one end in.\n", ses->spool_left, site_cable_name(k));
}

/* One end of the run, into a socket in this room. */
static void spool_plug(Session *ses, const char *arg, Buf *out)
{
    char a[64];
    snprintf(a, sizeof a, "%s", arg);
    int port = -1;
    char *colon = strchr(a, ':');
    /* "core:" with nothing after it means "any free one".  */
    if (colon) { *colon = 0; port = colon[1] ? atoi(colon + 1) : -1; }
    int d = dev_arg(ses, a);
    if (d < 0) { buf_printf(out, "there is no box called %s in this building.\n", a); return; }
    if (!dev_here(ses, d)) {
        char w[48];
        room_label(ses, ses->s.dev[d].room, w, sizeof w);
        buf_printf(out, "%s is in %s. You cannot reach into another room:\n"
                        "  go %s\n  then plug this end in.\n",
                   ses->s.dev[d].name, w, ses->s.dev[d].name);
        return;
    }
    if (ses->spool_kind < 0) {
        buf_puts(out, "you have no spool in your hands.\n"
                      "  `spool cat6`      take a drum, then plug an end in\n"
                      "  `plug <box>`      (no port) puts the crash cart's serial "
                      "lead in instead\n");
        return;
    }
    if (port < 0) {
        port = site_free_port(&ses->s, d);
        if (port < 0) {
            buf_printf(out, "%s has no free port left -- all %d are used. That is "
                            "a purchase,\n  not a mistake.\n",
                       ses->s.dev[d].name, ses->s.dev[d].nports);
            return;
        }
    }
    if (port >= ses->s.dev[d].nports) {
        buf_printf(out, "%s has %d port%s, numbered 0 to %d. There is no port %d "
                        "on the back of it.\n", ses->s.dev[d].name,
                   ses->s.dev[d].nports, ses->s.dev[d].nports == 1 ? "" : "s",
                   ses->s.dev[d].nports - 1, port);
        return;
    }
    if (net_port_state(ses->s.net, ses->s.dev[d].node, port) != PORT_NOCABLE) {
        buf_printf(out, "%s port %d already has a cable in it. `links` says which, "
                        "`uncable <n>` pulls it out.\n", ses->s.dev[d].name, port);
        return;
    }

    if (ses->cab_dev < 0) {
        ses->cab_dev = d; ses->cab_port = port;
        buf_printf(out, "one end into %s port %d. Walk to the other end and plug "
                        "it in.\n", ses->s.dev[d].name, port);
        return;
    }
    if (ses->cab_dev == d && ses->cab_port == port) {
        buf_puts(out, "that is the end you already put in.\n");
        return;
    }
    /* Both ends in hand. The copper goes through the tray, which is a
     * different route from the one you walked, and the drum has to have
     * enough on it -- a refusal that is arithmetic rather than a rule. */
    int ra = ses->s.dev[ses->cab_dev].room, rb = ses->s.dev[d].room;
    int m = (ra == BLD_NOROOM || rb == BLD_NOROOM) ? SITE_PATCH_M
                                                   : site_metres(&ses->s, ra, rb);
    if (m >= 0 && m > ses->spool_left) {
        buf_printf(out, "the run is %d m through the tray and there are %d m left "
                        "on the spool.\n  `spool back`, take a fresh drum, and "
                        "start again -- or put the box somewhere nearer.\n",
                   m, ses->spool_left);
        return;
    }
    int l = site_cable(&ses->s, ses->cab_dev, ses->cab_port, d, port,
                       (CableKind)ses->spool_kind);
    if (l < 0) {
        buf_printf(out, "refused: %s\n", site_err_text(ses->s.err));
        if (ses->s.err == SITE_EMONEY)
            buf_printf(out, "  %d m of %s costs %d and you have %ld.\n", m,
                       site_cable_name((CableKind)ses->spool_kind),
                       site_cable_price((CableKind)ses->spool_kind, m), ses->s.money);
        return;
    }
    const SiteLink *lk = &ses->s.link[l];
    PortState st = site_link_state(&ses->s, l);
    ses->spool_left -= lk->metres;
    buf_printf(out, "link %d: %s:%d to %s:%d, %d m of %s through the tray, %d "
                    "paid, %s.\n", l, ses->s.dev[ses->cab_dev].name, ses->cab_port,
               ses->s.dev[d].name, port, lk->metres,
               site_cable_name((CableKind)lk->kind), lk->cost,
               st == PORT_UP ? "the port comes up" :
               st == PORT_TOOLONG ? "TOO LONG -- it does not come up" :
               "the port does not come up");
    if (st == PORT_TOOLONG)
        buf_puts(out, "  copper carries a hundred metres. Nothing refused the run; "
                      "you have\n  paid for a cable that does not work.\n");
    buf_printf(out, "%d m left on the spool.\n", ses->spool_left);
    ses->cab_dev = -1;
}

/* THE MACRO. `cable core files` is the four steps above, performed and
 * printed. It saves an agent typing, and it cannot do anything the hands
 * cannot: if the walk is refused, the run stops there, half done, exactly
 * as it would if a person had tried it. */
static void do_cable(Session *ses, int n, char *t[MAXTOK], Buf *out)
{
    char aend[64], bend[64];
    snprintf(aend, sizeof aend, "%s", t[1]);
    snprintf(bend, sizeof bend, "%s", t[2]);
    char abox[64], bbox[64];
    snprintf(abox, sizeof abox, "%s", aend);
    snprintf(bbox, sizeof bbox, "%s", bend);
    char *c;
    if ((c = strchr(abox, ':')) != NULL) *c = 0;
    if ((c = strchr(bbox, ':')) != NULL) *c = 0;
    int a = dev_arg(ses, abox), b = dev_arg(ses, bbox);
    if (a < 0 || b < 0) {
        buf_printf(out, "there is no box called %s in this building.\n",
                   a < 0 ? abox : bbox);
        return;
    }
    if (a == b) { buf_puts(out, "both ends of that cable are the same box.\n"); return; }

    if (ses->spool_kind < 0 || (n > 3)) {
        char *sp[MAXTOK]; int sn = 2;
        char kbuf[16];
        snprintf(kbuf, sizeof kbuf, "%s", n > 3 ? t[3] : "cat6");
        sp[0] = (char *)"spool"; sp[1] = kbuf;
        if (ses->cab_dev < 0) do_spool(ses, sn, sp, out);
    }
    /* Start from whichever end you are standing with. */
    if (!dev_here(ses, a) && dev_here(ses, b)) {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%s", aend);
        snprintf(aend, sizeof aend, "%s", bend);
        snprintf(bend, sizeof bend, "%s", tmp);
        int t2 = a; a = b; b = t2;
    }
    if (!dev_here(ses, a)) {
        char wa[48], wb[48];
        room_label(ses, ses->s.dev[a].room, wa, sizeof wa);
        room_label(ses, ses->s.dev[b].room, wb, sizeof wb);
        buf_printf(out, "you are in neither room. %s is in %s and %s is in %s --\n"
                        "  walk to one of them first.\n",
                   ses->s.dev[a].name, wa, ses->s.dev[b].name, wb);
        return;
    }
    spool_plug(ses, aend, out);
    if (ses->cab_dev < 0) return;                 /* the first end refused */
    if (!dev_here(ses, b) && !walk_to(ses, ses->s.dev[b].room, out, false)) return;
    spool_plug(ses, bend, out);
}

/* ------------------------------------------- handing a line to site_cmd */
/* The verbs that name a box. On the management line the box is assumed, so
 * `addr 10.0.1.1/24` is rewritten into the spelling site_cmd wants -- and
 * the full spelling still works, because tower.gd's cart passes lines
 * through untouched and the two must not disagree. */
static const char *DEVVERB[] = {
    "addr", "gw", "router", "subif", "vlan", "trunk", "dhcpd", "dhcp",
    "resolver", "ping", "trace", "resolve", "get", "show", NULL
};

static bool is_devverb(const char *v)
{
    for (int i = 0; DEVVERB[i]; i++) if (strcmp(DEVVERB[i], v) == 0) return true;
    return false;
}

/* A CONFIGURATION verb changed this box -- not a diagnostic one. If it has
 * an operating system in it, the disk has to be told, or netd will overwrite
 * what was just set.
 *
 * The list matters. This used to fire on every verb that names a box, so
 * `ping files 10.0.1.1` rewrote the disk, which invalidated the applied
 * config, which flushed the ARP cache -- and `netstat -A` on the machine
 * then said nothing had ever answered on that wire, immediately after
 * something had. A diagnostic that erases the evidence it is being used to
 * find is worse than no diagnostic. */
static bool is_config(const char *v)
{
    return strcmp(v, "addr") == 0 || strcmp(v, "gw") == 0 ||
           strcmp(v, "resolver") == 0 || strcmp(v, "subif") == 0 ||
           strcmp(v, "dhcp") == 0;
}

static void after_config(Session *ses, const char *verb, int dev, Buf *out)
{
    if (!is_config(verb) && strcmp(verb, "router") != 0) return;
    if (dev < 0 || dev >= ses->s.ndev) return;
    if (ses->mach[dev]) sync_disk(ses, dev);
    /* `set` IS NOT A CONFIRMATION. site_cmd answers a configuration line
     * with one word, which tells a player who cannot see the box nothing at
     * all -- not what was set, not on which card, not what it now is. Say
     * what the box says about itself now, which is the same line `look`
     * prints, so the two cannot disagree. */
    if (out->len && out->p && strncmp(out->p, "set", 3) == 0 && out->len < 6) {
        buf_clear(out);
        /* A SUBINTERFACE IS NOT THE FIRST CARD. dev_line prints eth0, so
         * `subif edge 1 1 0 10.0.1.1/24` answered with edge's WAN address
         * and looked as though it had done nothing, or worse, the wrong
         * thing. Print every interface it now has. */
        if (strcmp(verb, "subif") == 0) {
            buf_printf(out, "%s:\n", ses->s.dev[dev].name);
            net_dump_ifaces(ses->s.net, ses->s.dev[dev].node, out);
            return;
        }
        if (strcmp(verb, "router") == 0) {
            Buf p = {0};
            net_dump_routes(ses->s.net, ses->s.dev[dev].node, &p);
            buf_printf(out, "%s now %s between its interfaces:\n", ses->s.dev[dev].name,
                       p.p && strstr(p.p, "ip_forward 1") ? "forwards" : "does NOT forward");
            if (p.len) buf_put(out, p.p, p.len);
            buf_free(&p);
            return;
        }
        dev_line(ses, dev, out);
        if (ses->mach[dev])
            buf_printf(out, "    (written onto its disk: it has an OS and netd "
                            "reads that file)\n");
    }
}

/* --------------------------------------------------------------- day one */
bool session_start(Session *ses, uint64_t seed, long budget)
{
    memset(ses, 0, sizeof *ses);
    ses->seed = seed;
    ses->plugged = -1;
    ses->spool_kind = -1;
    ses->cab_dev = -1;
    if (!bld_generate(&ses->b, seed)) return false;
    if (!site_new(&ses->s, &ses->b, seed, budget)) { bld_free(&ses->b); return false; }
    ses->room = bld_find(&ses->b, 0, RM_MDF);
    if (ses->room < 0) { site_free(&ses->s); bld_free(&ses->b); return false; }
    /* The ground floor and the one above it, as the 3D shell starts. */
    ses->floors = ses->b.floors < 2 ? ses->b.floors : 2;
    ses->where = SES_BODY;
    ses->up = true;
    return true;
}

void session_end(Session *ses)
{
    if (!ses->up) return;
    for (int i = 0; i < SITE_MAX_DEV; i++) {
        if (!ses->mach[i]) continue;
        machine_free(ses->mach[i]);
        nom_free(ses->mach[i]);
        ses->mach[i] = NULL;
    }
    site_free(&ses->s);
    bld_free(&ses->b);
    ses->up = false;
    ses->where = SES_DESK;
}

void session_prompt(const Session *ses, char *out, size_t cap)
{
    switch (ses->where) {
    case SES_SHELL: snprintf(out, cap, "root@%s# ", ses->s.dev[ses->plugged].name); break;
    case SES_MGMT:  snprintf(out, cap, "mgmt@%s# ", ses->s.dev[ses->plugged].name); break;
    case SES_BODY: {
        const Room *rm = room_of(ses, ses->room);
        snprintf(out, cap, "f%d %s> ", rm ? rm->floor : 0,
                 rm ? bld_kind_name(rm->kind) : "?");
        break; }
    default: snprintf(out, cap, "you@desk# "); break;
    }
}

/* The first thing a blind tester reads. It has to say what the game is, what
 * exists, and what the tenants are going to want -- the arithmetic at the
 * bottom of `demand` is the whole shape of the problem in two lines. */
static void intro(Session *ses, Buf *out)
{
    buf_printf(out, "\n--- %llu, a tower of %d floors and %d rooms ---\n",
               (unsigned long long)ses->seed, ses->b.floors, ses->b.nrooms);
    buf_printf(out, "you stand up from your desk and walk into the MDF. There is "
                    "an ISP handoff\non the wall (%d/%d) and nothing is plugged "
                    "into it. You have %ld to spend.\n",
               ses->s.uplink >= 0 ? 1 : 0, 1, ses->s.money);
    Buf d = {0};
    site_dump_demand(&ses->s, &d);
    const char *p = d.p ? strstr(d.p, "\ndrops in all") : NULL;
    (void)p;
    /* Only the arithmetic, not the whole schedule: `demand` prints that. */
    if (d.p) {
        const char *tail = strstr(d.p, " drops in all");
        if (tail) {
            while (tail > d.p && tail[-1] != '\n') tail--;
            buf_printf(out, "%s", tail);
        }
    }
    buf_free(&d);
    buf_puts(out, "\n`help` says what you can do, `look` says what is here, "
                  "`desk` goes back.\n");
}

/* ------------------------------------------------------------- the line */
bool session_line(Session *ses, const char *line, Buf *out)
{
    char buf[NOM_ARG_MAX];
    snprintf(buf, sizeof buf, "%s", line);
    char *t[MAXTOK];
    char raw[NOM_ARG_MAX];
    snprintf(raw, sizeof raw, "%s", line);
    int n = split(buf, t);

    /* AT THE DESK THE BREAK-FIX GAME OWNS EVERY WORD. One verb gets you out
     * of the chair; anything else is somebody else's line, and taking it
     * here would change a game four playtests have already been run on. */
    if (ses->where == SES_DESK) {
        if (!n) return false;
        if (strcmp(t[0], "tower") == 0 || strcmp(t[0], "building") == 0 ||
            strcmp(t[0], "site") == 0) {
            if (!ses->up) {
                uint64_t seed = ses->seed;
                if (!session_start(ses, seed, 60000)) {
                    buf_puts(out, "that seed makes no building.\n");
                    return true;
                }
            }
            ses->where = SES_BODY;
            intro(ses, out);
            return true;
        }
        return false;
    }

    /* A REAL SHELL ON A REAL MACHINE. Everything goes to it, because that is
     * what a console is -- except the one word that takes the lead out, the
     * way `exit` leaves a chroot and does not hang up the phone. */
    if (ses->where == SES_SHELL) {
        if (n && strcmp(t[0], "unplug") == 0) {
            ses->where = SES_BODY; ses->plugged = -1;
            buf_puts(out, "lead back on the cart.\n");
            return true;
        }
        if (n && strcmp(t[0], "help") == 0) { do_help(ses, out); return true; }
        if (!n) return true;
        Machine *m = ses->mach[ses->plugged];
        if (m) kernel_run(m, raw, out);
        return true;
    }

    if (!n) return true;

    if (ses->where == SES_MGMT) {
        if (strcmp(t[0], "unplug") == 0) {
            ses->where = SES_BODY; ses->plugged = -1;
            buf_puts(out, "lead back on the cart.\n");
            return true;
        }
        if (strcmp(t[0], "help") == 0) { do_help(ses, out); return true; }
        if (strcmp(t[0], "where") == 0) { do_where(ses, out); return true; }
        /* The box is assumed. If the second token already names a box, the
         * player spelled it in full and that is fine too. */
        char cmd[NOM_ARG_MAX];
        if (is_devverb(t[0]) && (n < 2 || dev_arg(ses, t[1]) < 0)) {
            snprintf(cmd, sizeof cmd, "%s %s", t[0], ses->s.dev[ses->plugged].name);
            for (int i = 1; i < n; i++) {
                size_t l = strlen(cmd);
                snprintf(cmd + l, sizeof cmd - l, " %s", t[i]);
            }
        } else snprintf(cmd, sizeof cmd, "%s", raw);
        if (!site_cmd(&ses->s, cmd, out))
            buf_puts(out, "  `help` lists what this line takes.\n");
        after_config(ses, t[0], ses->plugged, out);
        return true;
    }

    /* ------------------------------------------------------- in the room */
    if (strcmp(t[0], "help") == 0)  { do_help(ses, out); return true; }
    if (strcmp(t[0], "where") == 0) { do_where(ses, out); return true; }
    if (strcmp(t[0], "look") == 0)  { do_look(ses, out); return true; }
    if (strcmp(t[0], "map") == 0) {
        bld_floorplan(&ses->b, n > 1 ? atoi(t[1]) : here_floor(ses), out);
        return true;
    }
    if (strcmp(t[0], "desk") == 0) {
        int mdf = bld_find(&ses->b, 0, RM_MDF);
        if (mdf >= 0 && mdf != ses->room) walk_to(ses, mdf, out, false);
        ses->where = SES_DESK;
        ses->plugged = -1;
        buf_puts(out, "you sit back down at your own workstation. `tower` to go "
                      "back into the building.\n");
        return true;
    }
    if (strcmp(t[0], "go") == 0 || strcmp(t[0], "walk") == 0) {
        if (n < 2) { buf_puts(out, "go where? `look` lists the ways out.\n"); return true; }
        int r = room_arg(ses, t[1]);
        if (r < 0) {
            buf_printf(out, "there is no room or box called %s. `look` lists the "
                            "ways out of this one, `map` draws the floor.\n", t[1]);
            return true;
        }
        if (walk_to(ses, r, out, false)) do_look(ses, out);
        return true;
    }
    if (strcmp(t[0], "lift") == 0) {
        if (n < 2) {
            buf_printf(out, "lift to which floor? %d of %d are in service.\n",
                       ses->floors, ses->b.floors);
            return true;
        }
        do_lift(ses, atoi(t[1]), out);
        return true;
    }
    if (strcmp(t[0], "open") == 0) { do_open(ses, out); return true; }
    if (strcmp(t[0], "unplug") == 0) {
        buf_puts(out, "there is no lead in anything.\n");
        return true;
    }
    if (strcmp(t[0], "spool") == 0) { do_spool(ses, n, t, out); return true; }
    if (strcmp(t[0], "take") == 0 && n > 1 && strcmp(t[1], "spool") == 0) {
        char *sp[MAXTOK]; sp[0] = t[0]; sp[1] = n > 2 ? t[2] : (char *)"cat6";
        do_spool(ses, 2, sp, out);
        return true;
    }
    /* `plug core:2` IS THE CABLE, `plug core` IS THE CART. A port number
     * means copper -- that is the only thing a port number can mean -- and a
     * bare box means the lead on the crash cart. Both refusals name the
     * other one, so getting it the wrong way round costs a line. */
    if (strcmp(t[0], "plug") == 0) {
        if (n < 2) {
            buf_puts(out, "plug what into what?\n"
                          "  plug <box>:<port>   an end of the spool, into copper\n"
                          "  plug <box>          the crash cart's serial lead\n"
                          "  plug hdmi <box>     the crash cart's display lead\n");
            return true;
        }
        bool hdmi = strcmp(t[1], "hdmi") == 0;
        bool serial = strcmp(t[1], "serial") == 0;
        const char *what = (hdmi || serial) ? (n > 2 ? t[2] : NULL) : t[1];
        if (!what) { buf_puts(out, "plug into what?\n"); return true; }
        if (!hdmi && strchr(what, ':')) { spool_plug(ses, what, out); return true; }
        do_plug(ses, what, hdmi, out);
        return true;
    }
    if (strcmp(t[0], "buy") == 0 || strcmp(t[0], "install") == 0 ||
        strcmp(t[0], "order") == 0) {
        if (n < 2) {
            buf_puts(out, "buy what? switch8 120  switch24 400  router 650  "
                          "pc 480  server 1350\n");
            return true;
        }
        int kind = site_kind_by_name(t[1]);
        if (kind < 0) {
            buf_printf(out, "no such kit: %s. switch8 switch24 router pc server\n", t[1]);
            return true;
        }
        /* `install <kind> <room>` is site_cmd's spelling and it would put a
         * box in a room the player is not in. You install kit where you are
         * standing; that is the whole point of having a building. */
        if (n > 2 && site_room_by_name(&ses->s, t[2]) >= 0) {
            buf_printf(out, "kit goes in the room you are standing in. Walk there "
                            "first:\n  go %s\n  buy %s\n", t[2], t[1]);
            return true;
        }
        int d = site_install(&ses->s, kind, ses->room, n > 2 ? t[2] : NULL);
        if (d < 0) { buf_printf(out, "refused: %s\n", site_err_text(ses->s.err)); return true; }
        char w[48];
        room_label(ses, ses->room, w, sizeof w);
        buf_printf(out, "%s: a %s in %s. %d port%s, %d paid, %ld left.\n",
                   ses->s.dev[d].name, site_kind_name(kind), w, ses->s.dev[d].nports,
                   ses->s.dev[d].nports == 1 ? "" : "s", site_kind_price(kind),
                   ses->s.money);
        return true;
    }
    if (strcmp(t[0], "uncable") == 0 && n < 2) {
        buf_puts(out, "uncable which one? `links` numbers them.\n");
        return true;
    }
    if (strcmp(t[0], "cable") == 0) {
        if (n < 3) { buf_puts(out, "cable <box> <box> [cat5e|cat6|fibre]\n"); return true; }
        do_cable(ses, n, t, out);
        return true;
    }

    /* Reading the state costs nothing and needs no legs: it is a clipboard. */
    if (strcmp(t[0], "links") == 0 || strcmp(t[0], "money") == 0 ||
        strcmp(t[0], "demand") == 0 || strcmp(t[0], "frames") == 0 ||
        strcmp(t[0], "rooms") == 0 || strcmp(t[0], "uncable") == 0 ||
        strcmp(t[0], "credit") == 0 ||
        (strcmp(t[0], "show") == 0)) {
        site_cmd(&ses->s, raw, out);
        return true;
    }

    /* Everything else that names a box: you have to be in the room with it. */
    if (is_devverb(t[0])) {
        if (n < 2) { buf_printf(out, "%s which box?\n", t[0]); return true; }
        int d;
        if (!need_here(ses, t[1], &d, out)) return true;
        site_cmd(&ses->s, raw, out);
        after_config(ses, t[0], d, out);
        return true;
    }

    buf_printf(out, "no such command: %s. `help` lists what you can do here.\n", t[0]);
    return true;
}
