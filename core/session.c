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
/* And make its node agree with what the operating system on it has done --
 * which is nothing at all when the operating system is not running. */
void netsite_apply(Machine *m);
void netsite_stale(Machine *m);

/* TWELVE WAS A SILENT CEILING ON WHAT A PLAYER MAY TYPE.
 *
 * The same number in core/site.c cost a playtester two of the complaints that
 * end a run: `trunk core 22 11 12 ... 23` is sixteen words, the parser kept
 * twelve, the verb answered `set`, and a floor was quietly broken for eight
 * days. That was fixed there and this is the same cap in the same shape --
 * from the room the raw line goes to site_cmd and is safe, but on a
 * management line (`plug core`) a devverb is rebuilt from THESE tokens, so
 * the twelfth word was still the last one that counted.
 *
 * Sixty-four, because a vlan per tenancy on the fullest seed is thirty-nine
 * words, and -1 for a line that would not fit rather than its first twelve.
 * A parser that drops input has to say so. */
#define MAXTOK 64

static int split(char *line, char *tok[MAXTOK])
{
    int n = 0;
    char *p = line;
    while (*p) {
        if (n >= MAXTOK) return -1;
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
    /* HOWEVER THEY SPELLED IT. The prompt says `f0 MDF>`, and `look` and
     * `rooms` print MDF, so `go MDF` is what a person types -- and it used to
     * be the one spelling that failed. The box lookup below keeps the letters
     * it was given, because a box is named by the player and `sw1` and `SW1`
     * could be two different boxes; a room kind is a word this file owns. */
    char low[64];
    size_t li = 0;
    for (const char *q = spec; *q && li < sizeof low - 1; q++)
        low[li++] = (*q >= 'A' && *q <= 'Z') ? (char)(*q - 'A' + 'a') : *q;
    low[li] = 0;
    int r = site_room_by_name(&ses->s, low);
    if (r >= 0) return r;
    char buf[64];
    snprintf(buf, sizeof buf, "f%d.%s", here_floor(ses), low);
    r = site_room_by_name(&ses->s, buf);
    if (r >= 0) return r;
    int d = site_dev_by_name(&ses->s, spec);
    if (d >= 0 && ses->s.dev[d].room != BLD_NOROOM) return ses->s.dev[d].room;
    spec = low;

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
    /* `addr edge:1 10.0.1.1/24` names a SOCKET on a box, the same spelling
     * `plug` and `cable` use. The box is what you have to be standing in
     * front of, so the lookup stops at the colon -- without this, the one
     * verb that gives a router its second address answered "there is no box
     * called edge:1 in this building". */
    char buf[64];
    const char *colon = strchr(a, ':');
    if (colon) {
        size_t k = (size_t)(colon - a);
        if (k >= sizeof buf) k = sizeof buf - 1;
        memcpy(buf, a, k);
        buf[k] = 0;
        a = buf;
    }
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
                        "  reach a box you are not standing in front of.\n",
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
    uint32_t gw = net_get_gateway(n, node);
    uint32_t ns = net_get_resolver(n, node);
    char ip[20], g[20], s[20];
    /* EVERY CARD IT HAS, NOT THE FIRST ONE.
     *
     * This wrote `iface eth0` and nothing else, so a floor server carrying a
     * vlan per tenancy on tagged subinterfaces -- which is the build D27
     * recommends and --loadcheck measures -- had its whole configuration in
     * memory and nowhere else. A mains failure took the subinterfaces, their
     * addresses and every DHCP pool riding on them, and the file on its own
     * disk went on naming an address for a card that had nothing, which is
     * the founding rule broken in the place it costs most. netsite.c reads
     * `iface eth1.13` back and makes the subinterface again.
     *
     * A card with no address still gets a stanza: the player made that
     * subinterface, and a subinterface waiting for an address is a decision
     * in exactly the way an address is. */
    Buf cfg = {0};
    int nports = net_node_ports(n, node);
    bool any = false;
    for (int i = 0; i < NET_IF_MAX; i++) {
        if (!net_if_exists(n, node, i)) continue;
        uint32_t ia = net_if_get_addr(n, node, i);
        /* A bare socket with no address is not worth a stanza: it is a hole
         * in the back of the box whether or not anybody writes it down. A
         * SUBINTERFACE with no address IS, because nothing else in the world
         * remembers that the player made it.
         *
         * SO A SERVER ADDRESSED ONLY ON VLANS -- the build D27 recommends --
         * has `iface eth0.12` as the first line of its file and no stanza for
         * the card at all, and that is correct: a tagged subinterface names
         * the card it rides on. netd used to read that first name as a CARD,
         * compare it against the name udev gives the device, and refuse to
         * start, so every such box was a landmine armed for its next reboot
         * and the only fix -- inserting a bare `iface eth0` by hand -- was
         * erased by the next verb through here. That was netd's misreading,
         * and guest/netd.c now takes the part before the dot. Writing a
         * stanza for an empty card to work around it would have put a card
         * nobody configured into the one file that is supposed to say only
         * what somebody decided. */
        if (!ia && i < nports) continue;
        char nm[24];
        net_if_name(n, node, i, nm, sizeof nm);
        buf_printf(&cfg, "iface %s\n", nm);
        if (ia) {
            net_fmt_ip(ia, ip, sizeof ip);
            buf_printf(&cfg, "  address %s\n  netmask %d\n", ip,
                       net_mask_len(net_if_get_mask(n, node, i)));
        }
        /* THE GATEWAY BELONGS TO THE BOX, NOT TO A CARD, so it goes under the
         * first stanza whichever card that is -- a floor server addressed
         * only on subinterfaces has no eth0 stanza to hang it off, and netd
         * reads the first `gateway` in the file either way. */
        if (!any && gw) {
            net_fmt_ip(gw, g, sizeof g);
            buf_printf(&cfg, "  gateway %s\n", g);
        }
        any = true;
    }
    /* Nothing configured at all means dhcp, which is what a machine with no
     * configuration does and what the image ships with. */
    if (!any) buf_puts(&cfg, "iface eth0\n  address dhcp\n");
    vfs_write(&m->disk, "/etc/net/interfaces", cfg.p, cfg.len);
    buf_free(&cfg);
    if (ns) {
        char rc[64];
        net_fmt_ip(ns, s, sizeof s);
        snprintf(rc, sizeof rc, "nameserver %s\nsearch nomnix.org\n", s);
        vfs_write(&m->disk, "/etc/resolv.conf", rc, strlen(rc));
    }
    char host[NET_NAME_MAX + 2];
    snprintf(host, sizeof host, "%s\n", ses->s.dev[dev].name);
    vfs_write(&m->disk, "/etc/hostname", host, strlen(host));
    /* AND WHAT IT SERVES, for the same reason and in the same place.
     *
     * A DHCP pool and a nameserver the tower started lived in the stack and
     * nowhere else, so three servers that were fsck'd and rebooted after a
     * mains failure came back addressed, booted and silent -- serving
     * nothing, with `svc` on the box still saying the box was fine, because
     * from the box's point of view it was. The address survives a power cut
     * because it is on the disk; a service the player configured is a
     * decision of exactly the same kind, so it goes on the disk beside it
     * and netd starts it again. `dhcpd <box> off` takes the line out, which
     * is why an empty file is written rather than none. */
    /* A Buf, not a fixed 256 bytes: a name server with a zone in it writes a
     * line per name, and sixty-four names do not fit in a stack buffer that
     * was sized for a pool and a word. Worse, the old code accumulated
     * snprintf's return into `sl` without clamping it, so the first file
     * that did not fit would have passed a negative size to the next
     * snprintf -- a silent overflow waiting for the feature below. */
    Buf svc = {0};
    buf_puts(&svc, "# what this box serves. netd starts it; the tower "
                   "wrote it.\n");
    for (int i = 0; i < NET_POOL_MAX; i++) {
        int ifx = 0, count = 0;
        uint32_t first = 0, mk = 0, pg = 0, pd = 0;
        if (!net_dhcpd_pool(n, node, i, &ifx, &first, &count, &mk, &pg, &pd))
            break;
        char a[20], gg[20], dd[20];
        net_fmt_ip(first, a, sizeof a);
        net_fmt_ip(pg, gg, sizeof gg);
        net_fmt_ip(pd, dd, sizeof dd);
        buf_printf(&svc, "dhcpd %s %d %d %s %s\n", a, count, net_mask_len(mk),
                   gg, dd);
    }
    if (net_dnsd_running(n, node)) {
        buf_puts(&svc, "dnsd\n");
        /* AND ITS ZONE. A name the player gave a name server is a decision of
         * exactly the same kind as an address, and it lived in the stack and
         * nowhere else -- so a server that had been the tower's resolver for
         * a fortnight came back from a power cut answering `no such host` to
         * every name in the building, with `svc` on the box saying it was
         * fine. The forwarder needs no line here: it is this box's own
         * resolver, and that is already in /etc/resolv.conf above. */
        for (int i = 0; ; i++) {
            char nm[64], a[20];
            uint32_t rip = 0;
            if (!net_dns_record_at(n, node, i, nm, sizeof nm, &rip)) break;
            net_fmt_ip(rip, a, sizeof a);
            buf_printf(&svc, "record %s %s\n", nm, a);
        }
    }
    vfs_write(&m->disk, "/etc/net/services", svc.p, svc.len);
    buf_free(&svc);
    /* Whatever it had applied is now stale. */
    m->net_cfg = 0;
}

/* POWERING ON IS WHAT PUTS A MACHINE ON THE NETWORK, and it is the only
 * thing that does.
 *
 * This used to happen the first time somebody plugged a serial lead in, which
 * made the crash cart the registrar of the network: a box nobody had ever
 * switched on answered pings, and then booting it made it LESS reachable,
 * because its own firewall finally started. The operating system and the
 * network were two machines married by a lead.
 *
 * Now: the disk is written first, the box is joined to its node, it boots,
 * and then netsite_apply makes the node agree with what the operating system
 * did -- which for a machine whose boot failed is nothing at all, because a
 * kernel that is not running has not configured a card. A booted machine is
 * 13.5 MB; a rack of boxes nobody has switched on costs nothing. */
static Machine *box_of(Session *ses, int dev, Buf *out)
{
    Machine *m = ses->mach[dev];
    bool first = m == NULL;
    if (first) {
        m = nom_alloc(sizeof *m);
        memset(m, 0, sizeof *m);
        machine_install(m, ses->seed + 7000 + (uint64_t)dev);
        ses->mach[dev] = m;
        sync_disk(ses, dev);
        netsite_pin(m, ses->s.net, ses->s.dev[dev].node);
    }
    machine_boot(m);
    netsite_apply(m);
    if (out) {
        buf_put(out, m->boot.console.p, m->boot.console.len);
        buf_printf(out, "\n[%s at %s]\n", m->boot.running ? "UP" : "DOWN",
                   boot_stage_name(m->boot.failed_at));
    }
    return m;
}

/* The power button on the front of a box that has an operating system in it.
 * The site takes the addresses away and the machine's own kernel is what
 * puts them back. */
static void do_power(Session *ses, int dev, bool on, Buf *out)
{
    const SiteDev *d = &ses->s.dev[dev];
    if (!site_power(&ses->s, dev, on)) {
        buf_printf(out, "%s: %s\n", d->name, site_err_text(ses->s.err));
        return;
    }
    if (!on) {
        if (ses->plugged == dev && ses->where == SES_SHELL) {
            ses->where = SES_BODY;
            ses->plugged = -1;
            buf_puts(out, "the console goes dead and the lead comes out.\n");
        }
        buf_printf(out, "%s is off. Its addresses, its routes and everything it "
                        "had open went\n  with the power -- they were in its "
                        "memory. What comes back is what is\n  on its disk.\n",
                   d->name);
        return;
    }
    buf_printf(out, "you press the button on %s.\n", d->name);
    /* THE SITE EMPTIED ITS NODE WHEN THE POWER WENT (site_power ->
     * power_down), and nothing on the disk changed while it was off -- so
     * netsite's "the config has not changed, there is nothing to apply"
     * short-cut was true of the FILES and false of the WIRE. Say the wire is
     * stale, and the boot below reads the disk again. Without this a server
     * switched back on after a mains failure came up with no address at all,
     * while `cat /etc/net/interfaces` on its own console named one. */
    netsite_stale(ses->mach[dev]);
    Machine *m = box_of(ses, dev, out);
    if (!m->boot.running)
        buf_puts(out, "it did not finish booting, so nothing of it is on the "
                      "network: no card was\n  configured, because no kernel got "
                      "far enough to configure one.\n");
}

/* ====================================================== somebody else's desk
 *
 * D31. The owner: *"let's also add in the virtual people to actually be in
 * their office at a computer desk... where if you felt like it you could go
 * over to their desk and see what issues they're complaining about --
 * literally using their computer."*
 *
 * A tenancy's desk has been a real card in a real broadcast domain since the
 * pivot: it asks for an address, it pulls its files, and core/siteday.c
 * counts whether it finished. What it has never had is an operating system,
 * and it cannot have one lying about: 13.5 MB a machine against 176 desks in
 * a full tower is 2.4 GB, and the world is meant to be 73 MB plus what is
 * running.
 *
 * So the machine is LAZY AND BOUNDED BY THE BODY. There is one chair and one
 * person to sit in it: the Machine is installed and booted when you pull the
 * chair out, and freed the moment you stand up. The cap is one, it is not a
 * number anybody tuned, and a session that never sits at a desk pays nothing.
 *
 * WHAT IT COSTS IN HONESTY, and this is the real decision. The machine's
 * disk is written from what its card ALREADY HAS on the wire -- the lease it
 * is holding, the gateway and the resolver that came with it -- rather than
 * from `address dhcp`. That is what makes waking it a no-op for the network:
 * netsite.c tears the node down and puts back exactly what was there, so a
 * player who sits at a desk has not renewed anybody's lease, has not
 * re-pointed anybody's resolver, and cannot make a striking tenancy worse by
 * looking at it. The price is that /etc/net/interfaces reads as a static
 * address on a machine that was handed one, so the file says which it is in
 * a comment above it. A desk with NO address writes `address dhcp` and means
 * it: netd really asks, really gets nothing, and that is the commonest
 * complaint in the game diagnosed from the complainant's own console.
 */
/* WHOSE DESK IT IS. A name is not a technical claim and nothing hangs off
 * it, but a floor of numbered cards is not a floor of people, and the 3D
 * shell needs somewhere to read the nameplate from. Deterministic in the
 * seed and the device, so the same tower always seats the same people. */
static const char *PERSON[] = {
    "Ada", "Bo", "Cai", "Dot", "Efe", "Fen", "Gus", "Hana", "Ida", "Jo",
    "Kit", "Lev", "Mira", "Nils", "Ola", "Pia", "Quin", "Ravi", "Sam", "Tess",
    "Uli", "Vik", "Wren", "Xan", "Yara", "Zoe"
};
/* AND A SURNAME, because twenty desks drawn from twenty-six given names put
 * four people called Vik in one office and a room of four Viks is not a room
 * of people. Two draws off one hash is five hundred and twenty of them. */
static const char *FAMILY[] = {
    "Adeyemi", "Baqri", "Costa", "Dvorak", "Eriksen", "Farah", "Guo", "Haddad",
    "Iversen", "Jelinek", "Kowalski", "Lindqvist", "Moreau", "Nakamura",
    "Okonkwo", "Petrov", "Quiroga", "Reyes", "Sandoval", "Tanaka"
};
#define NPERSON ((int)(sizeof PERSON / sizeof PERSON[0]))
#define NFAMILY ((int)(sizeof FAMILY / sizeof FAMILY[0]))

static uint64_t desk_hash(const Session *ses, int dev)
{
    uint64_t h = ses->seed ^ (0x9e3779b97f4a7c15ull * (uint64_t)(dev + 1));
    h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27; h *= 0x94d049bb133111ebull;
    h ^= h >> 31;
    return h;
}

/* Their full name, into a caller's buffer, because there are two tables and
 * a static would be a bug the day two desks are named in one line. */
static const char *desk_person_full(const Session *ses, int dev, char *out,
                                    size_t cap)
{
    uint64_t h = desk_hash(ses, dev);
    snprintf(out, cap, "%s %s", PERSON[(int)(h % (uint64_t)NPERSON)],
             FAMILY[(int)((h >> 20) % (uint64_t)NFAMILY)]);
    return out;
}

/* Just the given name, for the sentences that use one. */
static const char *desk_person(const Session *ses, int dev)
{
    return PERSON[(int)(desk_hash(ses, dev) % (uint64_t)NPERSON)];
}

static bool is_desk(const Session *ses, int d)
{
    return d >= 0 && d < ses->s.ndev && ses->s.dev[d].kind == SDEV_DESK;
}

/* WHAT THIS MACHINE IS, WRITTEN FROM WHAT ITS CARD ALREADY HAS. See the note
 * above. This is deliberately NOT sync_disk(): that function writes the
 * player's decisions onto a box the player owns, and nobody decided anything
 * about this one -- a desk's address came off the wire from a server the
 * player runs, and the file has to say so. */
static void desk_disk(Session *ses, int dev)
{
    Machine *m = ses->mach[dev];
    if (!m) return;
    Net *n = ses->s.net;
    int node = ses->s.dev[dev].node;
    uint32_t a  = net_if_get_addr(n, node, 0);
    uint32_t gw = net_get_gateway(n, node);
    uint32_t ns = net_get_resolver(n, node);
    Buf cfg = {0};
    if (a) {
        char ip[20], g[20];
        net_fmt_ip(a, ip, sizeof ip);
        buf_puts(&cfg, "# the lease this machine is holding. It asked, something\n"
                       "# answered, and this is what came back.\n");
        buf_printf(&cfg, "iface eth0\n  address %s\n  netmask %d\n", ip,
                   net_mask_len(net_if_get_mask(n, node, 0)));
        if (gw) {
            net_fmt_ip(gw, g, sizeof g);
            buf_printf(&cfg, "  gateway %s\n", g);
        }
    } else {
        buf_puts(&cfg, "# nobody has ever configured this machine by hand. It asks.\n"
                       "iface eth0\n  address dhcp\n");
    }
    vfs_write(&m->disk, "/etc/net/interfaces", cfg.p, cfg.len);
    buf_free(&cfg);
    if (ns) {
        char rc[64], s[20];
        net_fmt_ip(ns, s, sizeof s);
        snprintf(rc, sizeof rc, "nameserver %s\n", s);
        vfs_write(&m->disk, "/etc/resolv.conf", rc, strlen(rc));
    } else {
        /* THE IMAGE SHIPS A RESOLVER AND IT IS NOT IN THIS BUILDING. Leaving
         * the shipped 10.0.2.3 on a tenant's desk would have put an address
         * out of the break-fix world onto a machine in the player's tower,
         * and the first thing a player does at a desk with no network is
         * read this file. A resolver arrives with a lease; there is no
         * lease. */
        static const char NORES[] =
            "# a nameserver arrives with the lease. No lease has arrived.\n";
        vfs_write(&m->disk, "/etc/resolv.conf", NORES, sizeof NORES - 1);
    }
    char host[NET_NAME_MAX + 2];
    snprintf(host, sizeof host, "%s\n", ses->s.dev[dev].name);
    vfs_write(&m->disk, "/etc/hostname", host, strlen(host));
    /* A desk serves nothing. An empty file rather than none, so netd reads a
     * decision rather than an absence. */
    vfs_write(&m->disk, "/etc/net/services", "", 0);
    m->net_cfg = 0;
}

/* Pull the chair out. Installs, writes, pins and boots -- and the boot
 * console is NOT printed: this machine has been on since the morning, the
 * person has been working at it all day, and a kernel banner would be the
 * game claiming something it does not mean. What is printed is the state,
 * which is read off the machine and off the wire. */
static Machine *desk_wake(Session *ses, int dev)
{
    Machine *m = ses->mach[dev];
    if (m) return m;
    m = nom_alloc(sizeof *m);
    memset(m, 0, sizeof *m);
    machine_install(m, ses->seed + 9000 + (uint64_t)dev);
    ses->mach[dev] = m;
    desk_disk(ses, dev);
    netsite_pin(m, ses->s.net, ses->s.dev[dev].node);
    machine_boot(m);
    netsite_apply(m);
    return m;
}

/* And put it back. THE MACHINE GOES WITH THE CHAIR: whatever was typed at it
 * is gone, because it is not the player's machine and they do not get to keep
 * anything of it. That is the memory answer and the ownership answer in one
 * decision -- see docs/decisions-d31.md. */
static void desk_sleep(Session *ses)
{
    int d = ses->seat;
    ses->seat = -1;
    if (d < 0 || !ses->mach[d]) return;
    machine_free(ses->mach[d]);
    nom_free(ses->mach[d]);
    ses->mach[d] = NULL;
}

/* WHAT IS WRONG WITH THIS DESK, in the words of the thing that is wrong --
 * link, then address, and no further. This is a reading of the wire and it
 * deliberately stops at the two facts core/siteday.c uses to decide whether
 * a person got their work done: anything past that is a diagnosis and the
 * diagnosis is the player's job, at the machine. */
static const char *desk_state(const Session *ses, int d)
{
    if (net_port_state(ses->s.net, ses->s.dev[d].node, 0) != PORT_UP)
        return "no link";
    if (!net_if_get_addr(ses->s.net, ses->s.dev[d].node, 0))
        return "link, no address";
    return "link and address";
}

static void do_sit(Session *ses, const char *what, Buf *out)
{
    if (ses->carrying >= 0) {
        buf_printf(out, "refused: you are carrying %s in both hands. `drop` "
                        "first.\n", ses->s.dev[ses->carrying].name);
        return;
    }
    int d = dev_arg(ses, what);
    if (d < 0) {
        buf_printf(out, "there is no desk called %s in this building. `desks` "
                        "lists them by\n  tenancy, and `look` says which are in "
                        "this room.\n", what);
        return;
    }
    if (!is_desk(ses, d)) {
        buf_printf(out, "refused: %s is not somebody's desk -- it is a %s, and "
                        "it is yours.\n  `plug %s` puts the crash cart's serial "
                        "lead in it.\n",
                   ses->s.dev[d].name, site_kind_name(ses->s.dev[d].kind),
                   ses->s.dev[d].name);
        return;
    }
    if (!dev_here(ses, d)) {
        char w[48];
        room_label(ses, ses->s.dev[d].room, w, sizeof w);
        buf_printf(out, "%s is in %s and you are not. `go %s` first -- you have "
                        "to be in\n  their office to sit down at it.\n",
                   ses->s.dev[d].name, w, ses->s.dev[d].name);
        return;
    }
    Machine *m = desk_wake(ses, d);
    ses->seat = d;
    ses->where = SES_SEAT;
    char who[48];
    buf_printf(out, "%s pushes their chair back and lets you sit down at %s.\n",
               desk_person_full(ses, d, who, sizeof who), ses->s.dev[d].name);
    buf_printf(out, "[%s at %s] -- %s\n", m->boot.running ? "UP" : "DOWN",
               boot_stage_name(m->boot.failed_at), desk_state(ses, d));
    /* WHAT THEY ARE COMPLAINING ABOUT, in their words, before any tool has
     * been run. It is a reading of the tenancy's own strike count -- the
     * number `service` prints -- so it cannot say anything the player could
     * not already have read, and it never names a cause. */
    int tn = ses->s.dev[d].tenant;
    for (int i = 0; i < ses->s.ntenant; i++) {
        const SiteTenant *t = &ses->s.tenant[i];
        if (t->tenant != tn || !t->moved) continue;
        /* THREE THINGS WERE WRONG WITH THIS AND A PLAYTESTER HIT ALL THREE.
         *
         * It said "on this floor" while reading `tried`, which is per
         * TENANCY -- and floors hold two or three of them, so a studio with
         * nothing working said the floor was dead while the web host beside
         * it served 24 of 24 visitors.
         *
         * `!tried` was tested before `complained`, so a tenancy that had
         * filed and had never had one working desk -- the worst case there
         * is -- got the neutral line and never said it had filed. Their
         * studio had five strikes and a star in `service` and still did not
         * mention it. Filing is now said first, because it is the strongest
         * thing true of them, and the rest is added to it.
         *
         * And it counted in an office's units. A call centre on a day the
         * row above said "18 of 18 calls broke up" told the player "0 of 18
         * THINGS WE TRIED finished". The unit comes from the trade now, out
         * of the same function `service` uses. */
        const char *unit = site_tenant_kind_unit(t->kind, true);
        if (t->complained && !t->tried)
            buf_printf(out, "\"we filed with the landlord. Not one machine in "
                            "this office has got onto anything at all, %d day%s "
                            "running.\"\n",
                       t->strikes, t->strikes == 1 ? "" : "s");
        else if (t->complained)
            buf_printf(out, "\"we filed with the landlord. %d of our %d %s got "
                            "through yesterday.\"\n", t->finished, t->tried, unit);
        else if (!t->tried)
            buf_printf(out, "\"not one machine in this office has got onto "
                            "anything at all. %d day%s of it.\"\n",
                       t->strikes, t->strikes == 1 ? "" : "s");
        else if (t->strikes)
            buf_printf(out, "\"it has been like this %d day%s now. %d of %d "
                            "%s got through.\"\n", t->strikes,
                       t->strikes == 1 ? "" : "s", t->finished, t->tried, unit);
        else
            buf_printf(out, "\"it has been fine, actually -- %d of %d %s "
                            "yesterday.\"\n", t->finished, t->tried, unit);
        break;
    }
    buf_puts(out, "this is a REAL SHELL on their machine, not yours. `help` for "
                  "what is on it,\n  `stand` to get up again -- and the machine "
                  "goes back to being theirs when\n  you do, so nothing you "
                  "leave on it stays.\n");
}

static void do_stand(Session *ses, Buf *out)
{
    int d = ses->seat;
    desk_sleep(ses);
    ses->where = SES_BODY;
    buf_printf(out, "you stand up out of %s's chair.\n",
               d >= 0 ? desk_person(ses, d) : "somebody");
    if (d >= 0)
        buf_printf(out, "%s is theirs again, and it is a card on a wire once "
                        "more: nothing of the\n  operating system in it is left "
                        "in this game's memory.\n", ses->s.dev[d].name);
}

/* WHERE THE PEOPLE ARE. The model is what says who sits where and what is
 * wrong with their machine -- D23's rule, and the reason a fourth agent can
 * put desks and people into the 3D world against something rather than
 * inventing them. Every field here is read off the site and the wire. */
static void do_desks(Session *ses, int n, char *t[MAXTOK], Buf *out)
{
    int want = n > 1 ? atoi(t[1]) : -1;
    int shown = 0;
    for (int i = 0; i < ses->s.ntenant; i++) {
        const SiteTenant *tn = &ses->s.tenant[i];
        if (!tn->moved) continue;
        if (want >= 0 && tn->tenant != want) continue;
        shown++;
        char w[48];
        room_label(ses, tn->room, w, sizeof w);
        buf_printf(out, "tenancy %d, %s: %d desk%s, %d of %d finished yesterday, "
                        "%d strike%s%s\n", tn->tenant, w, tn->ndesk,
                   tn->ndesk == 1 ? "" : "s", tn->finished, tn->tried,
                   tn->strikes, tn->strikes == 1 ? "" : "s",
                   tn->complained ? ", COMPLAINT FILED" : "");
        if (want < 0) {
            /* One line a tenancy, and the desk to walk to if you want it. */
            if (tn->ndesk)
                buf_printf(out, "    `desks %d` for who sits at them; `go %s` "
                                "then `sit %s`\n", tn->tenant,
                           ses->s.dev[tn->desk0].name,
                           ses->s.dev[tn->desk0].name);
            continue;
        }
        for (int j = 0; j < tn->ndesk; j++) {
            int d = tn->desk0 + j;
            char rw[48];
            room_label(ses, ses->s.dev[d].room, rw, sizeof rw);
            char ip[20] = "-", who[48];
            uint32_t a = net_if_get_addr(ses->s.net, ses->s.dev[d].node, 0);
            if (a) net_fmt_ip(a, ip, sizeof ip);
            buf_printf(out, "    %-8s %-18s %-18s %-16s %s\n",
                       ses->s.dev[d].name,
                       desk_person_full(ses, d, who, sizeof who), rw, ip,
                       desk_state(ses, d));
        }
    }
    if (!shown) {
        if (want >= 0)
            buf_printf(out, "tenancy %d has not moved in, so there are no desks "
                            "on that floor yet.\n  `demand` says when they "
                            "come.\n", want);
        else
            buf_puts(out, "nobody has moved in yet, so there is not a desk in "
                          "the building.\n  `demand` says who is coming and "
                          "when.\n");
        return;
    }
    if (want < 0)
        buf_puts(out, "\n`sit <desk>` sits down at one, in the room it is in. It "
                      "is their computer:\n  the operating system in it exists "
                      "while you are in the chair and no longer.\n");
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
    /* WHAT YOU ARE CARRYING COMES WITH YOU, and it is in this room now --
     * not in a pocket, not in an inventory. `look` in here shows it because
     * it is here, which is the same reason `look` in goods in showed it
     * before you picked it up. */
    if (ses->carrying >= 0) site_move(&ses->s, ses->carrying, dst);
    if (!quiet) {
        buf_printf(out, "you walk %d m to %s", metres, w);
        if (ses->b.rooms[dst].floor != was) buf_puts(out, ", by the stairs");
        if (ses->carrying >= 0)
            buf_printf(out, ", carrying %s", ses->s.dev[ses->carrying].name);
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
        /* THE FIRST ADDRESS IT HAS, ON WHICHEVER CARD.
         *
         * This read interface 0 and nothing else, so a floor server carrying
         * its floor's vlan on a tagged subinterface -- the build D27
         * recommends -- was listed by `look` as "no address" while `show`
         * two lines later printed the address it plainly had. The name of
         * the interface comes with it when it is not eth0, because that is
         * the only way somebody who cannot see the box can tell a socket
         * from a vlan riding on one. */
        int ifx = 0;
        uint32_t a = 0;
        for (int k = 0; k < NET_IF_MAX && !a; k++) {
            a = net_if_get_addr(ses->s.net, d->node, k);
            if (a) ifx = k;
        }
        if (a) {
            net_fmt_ip(a, ip, sizeof ip);
            buf_printf(out, " %s/%d", ip,
                       net_mask_len(net_if_get_mask(ses->s.net, d->node, ifx)));
            if (ifx) {
                char nm[24];
                net_if_name(ses->s.net, d->node, ifx, nm, sizeof nm);
                buf_printf(out, " on %s", nm);
            }
        } else buf_printf(out, " no address, %d port%s", d->nports,
                          d->nports == 1 ? "" : "s");
        /* AN OS IS RUNNING ON IT ONLY IF IT IS SWITCHED ON.
         *
         * ses->mach[i] is the allocated Machine. It is non-NULL from the first
         * power-on until session_end() and power-off never touches it -- so
         * `look` went on saying "[an OS is running on it]" about a box that had
         * been off for a week. `show <box>` in core/site.c says the opposite
         * in the same room in the same second ("It is switched off, so nothing
         * of it is running"), and a playtester who read the first one would
         * have walked away believing the box was up. The machine's own answer
         * is the power state, so ask it. */
        /* AND ONLY IF THAT OS GOT ANYWHERE. Powered on is not booted. A
         * playtester switched five servers back on the morning after a mains
         * failure, read "[an OS is running on it]" on every one, and only
         * found out from `plug` that all five had stopped at the initrd
         * unable to mount a dirty root. That is the exact morning you are
         * triaging, so it is the worst possible moment for this line to be
         * generous. m->boot.running is the machine's own answer. */
        if (ses->mach[i] && d->powered) {
            if (ses->mach[i]->boot.running)
                buf_puts(out, "  [an OS is running on it]");
            else
                buf_printf(out, "  [switched on, but its boot stopped at %s]",
                           boot_stage_name(ses->mach[i]->boot.failed_at));
        }
        else if (ses->mach[i])
            buf_puts(out, "  [SWITCHED OFF -- nothing of it is running]");
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
        if (i == ses->carrying)
            buf_puts(out, "      -- and it is in your hands, not on the floor\n");
    }
    if (!n) buf_puts(out, "  there is no kit in this room.\n");
    /* WHAT IS ON THE WALL, which is not kit and does not move. A jack is the
     * one thing in a room a player can own without there being a box in it,
     * so a room that looks empty may already have copper in it -- and if
     * `look` did not say so, the only way to find out would be to pay to
     * pull the run again. */
    if (site_room_jack(&ses->s, ses->room, 0, false) >= 0)
        site_dump_jacks(&ses->s, ses->room, out);
    /* GOODS IN IS WHERE ORDERS LAND, and a player who has not read the help
     * finds that out by standing in it. */
    if (rm->kind == RM_GOODS)
        buf_puts(out, "  the roller door. Anything you order is left here, and "
                      "carried out by you.\n");

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
    if (ses->carrying >= 0)
        buf_printf(out, "you are carrying %s, in both hands\n",
                   ses->s.dev[ses->carrying].name);
    if (ses->plugged >= 0)
        buf_printf(out, "the cart's %s lead is in %s\n", ses->hdmi ? "hdmi" : "serial",
                   ses->s.dev[ses->plugged].name);
    /* THE COPPER THAT IS ALREADY HERE. Counted off the jack table, in the
     * room you are standing in, because a jack is a fact about this wall. */
    {
        int jn = 0, jfree = 0, jsoon = 0;
        for (int i = 0; i < ses->s.njack; i++) {
            if (ses->s.jack[i].room != ses->room) continue;
            jn++;
            if (ses->s.day < ses->s.jack[i].ready) jsoon++;
            else if (ses->s.jack[i].link < 0) jfree++;
        }
        if (jn) {
            buf_printf(out, "%d jack%s on this wall, %d of them empty and ready to "
                            "`patch`", jn, jn == 1 ? "" : "s", jfree);
            if (jsoon) buf_printf(out, ", %d not put in yet", jsoon);
            buf_puts(out, "\n");
        }
    }
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
/* WHAT IS ACTUALLY IN THE BUILDING, COUNTED RATHER THAN PROMISED.
 *
 * The help used to open with a sentence: *"On day one it holds exactly one
 * thing: the ISP's socket on the wall of the MDF."* That is true of a session
 * started over the socket and it is FALSE of the one the 3D window starts,
 * which pre-orders a router, a switch24 and a server into goods in and has
 * already spent 2400 of the budget doing it. A blind playtester believed the
 * sentence, bought a router and a switch24 they already owned, and lost
 * another 1050 -- and there is no `sell`, so it stayed lost.
 *
 * Two starting states and one constant sentence is a claim that cannot be
 * true of both. So the help does not make the claim: it walks the device
 * table and prints what is there, which is right in every session because it
 * is a reading of the session rather than a memory of one. Same rule as
 * everything else in this file -- the machine is the source, not the prose. */
static void inventory(const Session *ses, Buf *out)
{
    buf_puts(out, "WHAT IS IN THE BUILDING RIGHT NOW -- counted off the device\n"
                  "table as you read this, not a promise about day one:\n");
    int n = 0;
    for (int i = 0; i < ses->s.ndev; i++) {
        const SiteDev *d = &ses->s.dev[i];
        if (d->tenant != 0) continue;       /* the tenants' own, not yours */
        char w[48];
        room_label(ses, d->room, w, sizeof w);
        buf_printf(out, "  %-10s %-9s in %-20s ", d->name, site_kind_name(d->kind), w);
        if (i == ses->s.uplink)
            buf_puts(out, "the ISP's, on their wall\n");
        else
            buf_printf(out, "yours, %d already paid\n", site_kind_price(d->kind));
        n++;
    }
    if (!n) buf_puts(out, "  nothing at all.\n");
    buf_printf(out, "  %ld of the budget is already spent and %ld is left. THERE "
                    "IS NO `sell`:\n  money that leaves does not come back, so a "
                    "box you own and order again is\n  a box you have paid for "
                    "twice. `look` in goods in before you order.\n",
               ses->s.spent, ses->s.money);
}

static void do_help(const Session *ses, Buf *out)
{
    if (ses->where == SES_SEAT && ses->seat >= 0) {
        buf_printf(out,
            "you are sat at %s, which belongs to %s and not to you. It is a\n"
            "REAL SHELL on the same operating system every other machine in "
            "this\ngame runs, and there is one account on it and it is root -- "
            "so be\ncareful: the only thing stopping you is you.\n"
            "\n"
            "THE POINT OF BEING HERE is that their complaint is a fact about "
            "THIS\nmachine, and these read it off this machine rather than off "
            "a number\nin `service`:\n"
            "  ip addr                      has this card got an address at all\n"
            "  ip route                     and does it know a way off its own\n"
            "                               subnet\n"
            "  cat /etc/net/interfaces      how it was told to get one. A desk\n"
            "                               asks; `address dhcp` and no address\n"
            "                               means it asked and nothing answered\n"
            "  cat /etc/resolv.conf         which resolver it was given\n"
            "  ping <addr>                  from HERE, over the copper you laid\n"
            "  traceroute <addr>            where it stops\n"
            "  netstat -r routes   -P this port's own counters   -A the arp cache\n"
            "  ss    arp    tcpdump    svc    ps    dmesg\n"
            /* AND THE ONE THAT IS NOT LIKE THE OTHERS. A playtester sat at a
             * call centre's desk on a day it scored 0 of 18 calls with 29% of
             * its audio concealed, and found a machine in perfect health --
             * ping 3/3, traceroute clean, `ip addr` with 20,175 packets and
             * nothing dropped. Every tool above asks about NOW and about THIS
             * card, and a call is neither: it is over by the time you sit
             * down, and it was thrown away on somebody else's port. `voice`
             * is the only one here that answers about a day that has already
             * happened, so it has to be named where somebody would look. */
            "  voice                        what its calls actually sounded\n"
            "                               like, after they are over. The only\n"
            "                               one here that remembers yesterday --\n"
            "                               and it names the port that threw the\n"
            "                               audio away, which is usually not on\n"
            "                               this floor\n"
            "\n"
            "  stand                        get up. Their machine goes back to\n"
            "                               being a card on a wire, and anything\n"
            "                               you did to it goes with it -- it is\n"
            "                               theirs, and you are not keeping it\n",
            ses->s.dev[ses->seat].name, desk_person(ses, ses->seat));
        return;
    }
    if (ses->where == SES_SHELL) {
        buf_printf(out,
            "this is a REAL SHELL on %s -- the same operating system every\n"
            "other machine in this game runs, on an emulated processor.\n"
            "  ip addr | link | route | neigh   what the cards and the kernel\n"
            "                               table really hold. It SHOWS; there is\n"
            "                               no `ip addr add` on this machine\n"
            "  netstat        -r routes  -P the port itself  -A the arp cache\n"
            "  ping <addr>    traceroute <addr>    ss    arp    tcpdump\n"
            "  svc   ps   dmesg\n"
            "  cat /etc/net/interfaces      what its card is configured from\n"
            "\n"
            "AND WHEN THE BOX IS BROKEN RATHER THAN MISCONFIGURED. A mains\n"
            "failure leaves real damage on a real disk and these are what find\n"
            "it. A playtester repaired one through this console and said `I only\n"
            "knew to type fsck because the initrd told me to`:\n"
            "  fsck /dev/sda1               check and repair the filesystem. This\n"
            "                               is what the initrd tells you to run\n"
            "                               when the root will not mount, and the\n"
            "                               rescue medium (`rescue <box>` from the\n"
            "                               room) is how you get a shell to run it\n"
            "  pkg verify                   every shipped file against what is\n"
            "                               installed: what is MISSING, what has\n"
            "                               CHANGED, and where\n"
            "  pkg diff <path>              the shipped file against the one on\n"
            "                               the disk, line by line\n"
            "  pkg reinstall <name>         put the shipped ones back. It keeps\n"
            "                               what was there as a .pkgsave\n"
            "  netstat -F                   the running packet filter and the\n"
            "                               per-rule drop counts. `nft` is the\n"
            "                               daemon that LOADS /etc/nftables.conf,\n"
            "                               not a way to ask it anything\n"
            "  man                          the manuals it ships with, `man fsck`\n"
            "                               and `man pkg` included\n"
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
        buf_puts(out, "where                          which room this box and you are in\n"
                      "help                           this page again\n"
                      "unplug                         put the lead back on the cart\n");
        return;
    }
    buf_puts(out,
        "YOU ARE THE IT DEPARTMENT OF A BUILDING. Every switch, every metre of\n"
        "copper and every address in it is yours to order, carry and configure.\n"
        "Tenants move in on a schedule, they pay rent, and what they ask for is\n"
        "what walks you into the limits.\n"
        "\n");
    inventory(ses, out);
    buf_puts(out,
        "\n"
        "WHERE YOU ARE -- and it matters. You can only touch what is in the\n"
        "room with you, and walking is metres of real building.\n"
        "  where              floor, room, what is in reach, how far you walked\n"
        "  look               what is in this room, and the ways out of it\n"
        "  map                an ASCII plan of this floor\n"
        "  go <room>          walk. `go comms` `go f3.office` `go #41` `go core`\n"
        "                     -- a box's name walks you to the room it is in\n"
        "  lift <floor>       take the lift. Only floors in service have a button\n"
        "  open               put the next floor in service. Go and stand on it\n"
        "                     first -- by the stairs, because its lift button is\n"
        "                     not lit yet -- and it costs the landlord's fit-out\n"
        "                     charge, by the square metre of let space on it\n"
        "  desk               walk back and sit down at your own workstation,\n"
        "                     where the support tickets are\n"
        "\n"
        "BUYING, AND CARRYING IT IN. Kit is delivered to GOODS IN on the\n"
        "ground floor -- not to your hands and not to the room you are\n"
        "standing in. Getting it where it goes is a walk, and the walk is\n"
        "metres of real building.\n"
        "  buy <kind> [name]  switch8 120   switch24 400   router 650\n"
        "                     pc 480        server 1350\n"
        "  go goods           where the van left it\n"
        "  carry <box>        pick it up. One box: both hands are on it, so\n"
        "                     no drum of cable and no lead while you have it\n"
        "  go <room>          walk it there. It goes where you go\n"
        "  drop               put it down. That is where it lives, and every\n"
        "                     metre of copper is measured from there\n"
        "                     -- a box with a cable in it will not be picked\n"
        "                     up again until you `uncable` it\n"
        "\n"
        "CABLING, which is four things a person does and four things you type:\n"
        "  spool cat6         take a drum off the shelf. cat5, cat5e, cat6, fibre.\n"
        "                     A LINK RUNS AT THE SLOWEST OF THREE THINGS: the\n"
        "                     port at each end and the cable between. cat5 is a\n"
        "                     hundred megabit for good; cat6 does ten gigabit\n"
        "                     under 55 m and only into a port that has ten\n"
        "                     gigabit behind it -- a desk, a server and an\n"
        "                     eight-port switch are gigabit whatever you plug in.\n"
        "                     THE DRUM IS FREE AND THE RUN IS NOT: nothing is\n"
        "                     charged when you pick a drum up and nothing is\n"
        "                     refunded by `spool back`. You are billed once, for\n"
        "                     the metres of tray, at the moment the second end\n"
        "                     goes in and the link is made\n"
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
        "                     a shorthand, not a shortcut: it still walks, it\n"
        "                     charges every metre of the walk, and it WALKS YOU\n"
        "                     BACK to the room you typed it in -- so six runs\n"
        "                     out of one comms cupboard are six `cable` lines\n"
        "                     with nothing in between\n"
        "  uncable <n>        pull one out\n"
        "\n"
        "AND WHAT IT WOULD COST, BEFORE ANY OF IT IS BOUGHT. The tray metres are\n"
        "not the metres you walk and there is no way to guess them from the floor\n"
        "plan, so ask:\n"
        "  quote <room|box>   from the room you are standing in. One name is the\n"
        "                     common case: you are in the cupboard with the drum,\n"
        "                     wondering what the far end costs\n"
        "  quote <a> <b>      between two named ends, from wherever you are. An\n"
        "                     end is a box (`core`, `core:2`) or a room (`comms`,\n"
        "                     `f3.office`, `#41`)\n"
        "                     It prints the tray metres for the route, the price\n"
        "                     off the spool AND as a jack in every grade, the days\n"
        "                     the trade would take, and WHAT EACH GRADE COMES UP\n"
        "                     AT over that distance -- which is the whole cable\n"
        "                     decision on one screen. Where a box is named at each\n"
        "                     end it takes that port into account; where one is\n"
        "                     not, it says so rather than guessing. Nothing is\n"
        "                     bought, nothing is booked, nothing is charged, and\n"
        "                     you do not move\n"
        "\n"
        "OR HAVE THE RUN PUT IN FOR GOOD, which is the same metres bought the\n"
        "other way. A jack is a socket on a room's wall with the run behind it\n"
        "punched down onto one port at the far end. You do not pull it: a trade\n"
        "does, and that is days rather than a walk.\n"
        "  jack <box>:<port> [kind]   a jack on THIS room's wall, its far end on\n"
        "                     that port of that box -- which is then held for\n"
        "                     good and is not a free port again. Priced on the\n"
        "                     same tray metres as the spool, plus the fit-out,\n"
        "                     so it is ALWAYS dearer than running it once. It\n"
        "                     prints both prices as you buy it\n"
        "  patch <box>:<port> [j<n>]  a lead from a box in this room into it.\n"
        "                     That is all a box ever costs after the jack is in\n"
        "  jacks              every one you have, what is in it, and the day the\n"
        "                     trade comes for the ones that are still a booking\n"
        "  uncable <n>        on a lead takes the lead out and LEAVES THE JACK.\n"
        "                     That is the whole of what you paid the extra for:\n"
        "                     move the box, replace it, put a second one in the\n"
        "                     room, and the copper is already there\n"
        "                     -- and a jack ordered today is not a socket today,\n"
        "                     so a tenancy already striking is not one you can\n"
        "                     jack your way out of. `links` prints both\n"
        "\n"
        "CONFIGURING. You must be in the room with the box.\n"
        "  power <box> on|off        a pc and a server arrive switched off, in a\n"
        "                            box, on a pallet. Powering one on is what boots\n"
        "                            the operating system in it and what puts it on\n"
        "                            the network -- and nothing of an off box\n"
        "                            answers anything\n"
        "  addr <box>[:<nic>] <ip>/<bits>   gw <box> <ip>   resolver <box> <ip>\n"
        "                            `addr edge:1` is the SECOND socket on the back,\n"
        "                            which is how a router gets a LAN side as well as\n"
        "                            a WAN side\n"
        "  router <box> on|off       vlan <box> <port> <n>   (a switch's port)\n"
        "  subif <box> <nic> <vlan> <ip>/<bits>    trunk <box> <port> <v>..\n"
        "                            an address on a card FOR ONE VLAN, which is how\n"
        "                            one socket terminates a subnet per floor. It is\n"
        "                            an address like any other: a server addressed\n"
        "                            only on subinterfaces serves its floor's files\n"
        "                            and holds its pools exactly as eth0 would, and\n"
        "                            all of it is on its disk and comes back after a\n"
        "                            power cut\n"
        "  subif <box> <nic> <vlan> off            take one away again, with any\n"
        "                            pool that was answering on it. The card\n"
        "                            underneath is untouched\n"
        "  dhcpd <box> <first> <count> <bits> <gw> <dns>     dhcp <box>\n"
        "                            ONE POOL PER SEGMENT, AND THERE IS NO VLAN\n"
        "                            IN THIS LINE. The pool lands on whichever\n"
        "                            interface of that box already has an address\n"
        "                            inside <first>/<bits> -- eth0, or the subif\n"
        "                            carrying a tenancy's vlan -- and it answers\n"
        "                            on that one and no other. If no interface is\n"
        "                            on that subnet it is REFUSED and prints the\n"
        "                            interfaces the box does have\n"
        "                            SO A BOX SERVES SEVERAL VLANS BY BEING TOLD\n"
        "                            SEVERAL TIMES: `subif` it onto each vlan\n"
        "                            first, then one `dhcpd` line per subnet, up\n"
        "                            to eight. That is how the `a segment of its\n"
        "                            own` tenancies get addresses\n"
        "                            `dhcpd <box>` alone lists the pools it has,\n"
        "                            `dhcpd <box> off` stops all of them\n"
        "  ping <box> <ip>   trace <box> <ip>   resolve <box> <name>\n"
        "                     a real echo request, from that box, over the\n"
        "                     copper you laid. Nothing is reachable by default\n"
        "  get <box> <ip> <path>     fetch a page over TCP, from that box\n"
        "  httpd <box> [port]        it serves its own files over real TCP\n"
        "  dnsd <box>                and answers names for the tower. It says\n"
        "                            how many names it holds and where it sends\n"
        "                            what it has not got\n"
        "  dns <box> <name> <ip>     one name in that server's zone, written onto\n"
        "                            the box's disk beside its address. A name it\n"
        "                            has not got is forwarded to whatever\n"
        "                            `resolver <box> <ip>` set, so a floor's own\n"
        "                            server resolves the internet too\n"
        "  ups <box>          a battery under it, so a mains failure is not a\n"
        "                     filesystem to check in the morning\n"
        "  disk <box>         a new one, cloned off the old one\n"
        "\n"
        "THE CLOCK, AND WHAT IT COSTS YOU. Nothing comes back for you until a\n"
        "day passes, and a day is when the rent arrives and the bills do.\n"
        "  day [n]            advance the clock. Tenancies whose day has come\n"
        "                     move in, their people work over what you built,\n"
        "                     and rent arrives for the work that finished\n"
        "\n"
        "RENT IS PAID FOR A DAY'S WORK, NOT FOR A TENANCY. This is why `status`\n"
        "can say `0 taken in rent` with eighty addressed desks in the building:\n"
        "  - rent arrives only when a `day` passes. It is a thirtieth of the\n"
        "    monthly figure `demand` and `service` print, per served day\n"
        "  - a tenancy is SERVED on a day when four fifths of the work its\n"
        "    people attempted actually finished. `service` says how many\n"
        "    finished; `load` says which port ate the rest\n"
        "  - a desk only attempts work if it has LINK *and* an ADDRESS. Copper\n"
        "    with no address earns nothing, and neither does a moved-in tenancy\n"
        "    you have not cabled at all\n"
        "\n"
        "AND THE COMPLAINT CLOCK RUNS ON TWO FACTS, NOT ONE. A tenancy that\n"
        "has addressed desks is struck on any day fewer than four fifths of\n"
        "their work finishes. A tenancy you have not cabled at all gets THREE\n"
        "DAYS of fit-out and is then struck every day it still has no desk\n"
        "able to work -- they signed a lease, the desks are in the room, and\n"
        "no network at all is worse than a slow one. Either way it is three\n"
        "strikes IN A ROW: one bad day, two, and the third files a complaint,\n"
        "so a tenancy nobody has cabled files on the sixth day after they move\n"
        "in. A served day resets the count to zero; a filed complaint never\n"
        "un-files. Complaints from a third of your tenancies -- never fewer\n"
        "than three -- end the run, so the building gets more slack as you let\n"
        "it. `service` and `status` both print the number. `service` prints each\n"
        "tenancy's strike count and stars the ones that have filed, so nothing\n"
        "here is a surprise you have to have read this to see.\n"
        "  serve <tenant> <box> [cable] [vlan]\n"
        "                     run copper from a box in THIS room to a tenancy's\n"
        "                     desks, one cable each, by the metre. What it really\n"
        "                     does, because guessing it costs a rack of ports:\n"
        "                     - it takes the box's NEXT FREE PORT for each desk\n"
        "                       that has no cable in it yet, in order, and skips\n"
        "                       a desk that is already patched\n"
        "                     - the cable defaults to CAT5E. `serve 4 sw1 cat5`\n"
        "                       or `serve 4 sw1 cat6` says otherwise. A desk has\n"
        "                       a gigabit card in it, so cat6 to one buys\n"
        "                       nothing and cat5 costs it 900 Mb\n"
        "                     - the vlan is the last number: `serve 4 sw1 34` or\n"
        "                       `serve 4 sw1 cat6 34`. It does NOT need the vlan\n"
        "                       to exist first and it does not create anything --\n"
        "                       it makes each port it patches an ACCESS port in\n"
        "                       that vlan, as it patches it. The trunk back to\n"
        "                       the router and the router's `subif` are still\n"
        "                       yours to do\n"
        "                     - leave the vlan out and every port lands in the\n"
        "                       untagged default, which for a tenancy that asked\n"
        "                       for a segment of its own is the wrong answer\n"
        "                     - if the tenancy has more desks than the box has\n"
        "                       free holes it patches what fits, STOPS, and says\n"
        "                       how many have nowhere to go. The copper it did\n"
        "                       lay is laid and paid for. Buy another switch, put\n"
        "                       it in that room, and `serve` again -- the desks\n"
        "                       already patched are skipped\n"
        "                     it prints `N of M desks have a port`, which is LINK\n"
        "                     and not service: an address is a separate job\n"
        "  service            every tenancy: desks, how many have LINK, how many\n"
        "                     also have an ADDRESS, what finished, and strikes\n"
        "  status             the day, the money, the frames, the complaints\n"
        "  load               the eight busiest ports, and which is dropping\n"
        "  isp [mb]           what the circuit carries, what it costs a month,\n"
        "                     and when the next bill lands. `isp 100` resizes it\n"
        "  events             what the world has done to the kit overnight\n"
        "\n"
        "THEIR DESKS, AND THE PEOPLE AT THEM. A tenancy's computers are real\n"
        "machines on your network and the complaint is a fact about one of\n"
        "them. You can go and look at it from their side.\n"
        "  desks              every tenancy that has moved in: how many desks,\n"
        "                     what finished yesterday, and how many strikes\n"
        "  desks <tenant>     each of that tenancy's desks -- who sits at it,\n"
        "                     which room, its address, and whether it has link\n"
        "  sit <desk>         sit down at it. You must be in their office, so\n"
        "                     `go t1d3` first. It is a REAL SHELL on THEIR\n"
        "                     machine: `ip addr`, `ping`, `cat\n"
        "                     /etc/resolv.conf` and the rest, reading the same\n"
        "                     network your copper made\n"
        "  stand              get up again. The machine is theirs, so it goes\n"
        "                     back to being a card on a wire and nothing you\n"
        "                     left on it survives -- which is also why sitting\n"
        "                     at one desk costs what one machine costs and a\n"
        "                     tower full of them costs nothing\n"
        "                     `carry` on a tenant's kit is refused, and this is\n"
        "                     the same rule: you are here to fix it, not to own\n"
        "                     it\n"
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
        buf_printf(out, "refused: the lift does not move -- floor %d is not in "
                        "service and its\n  button is not lit.\n"
                        "  `open` puts the next floor into service.\n", f);
        return;
    }
    /* Ask for the floor you are on and you get told, not walked to the lift
     * and then told -- the walk is a cost and charging it for nothing is
     * the game taking metres off a player for a typo. */
    if (f == here_floor(ses)) { buf_puts(out, "you are on that floor already.\n"); return; }
    int from = lift_lobby(ses, here_floor(ses));
    int to = lift_lobby(ses, f);
    if (from < 0 || to < 0) { buf_puts(out, "there is no lift in this building.\n"); return; }
    if (from != ses->room && !walk_to(ses, from, out, false)) return;
    ses->room = to;
    /* THE LIFT IS WHY ANYBODY PUTS A SWITCH ON THE EIGHTH FLOOR. What is in
     * your hands rides up in it with you. */
    if (ses->carrying >= 0) site_move(&ses->s, ses->carrying, to);
    buf_printf(out, "floor %d.\n", f);
    do_look(ses, out);
}

/* WHAT PUTTING A FLOOR INTO SERVICE COSTS, and it used to be nothing.
 *
 * `open` was free, took no time and could be typed from anywhere, so there
 * was no reason not to open every floor in the tower in the first minute --
 * which made the one decision in the verb ("can I carry another floor yet?")
 * not a decision at all. Two things fix it and both are things about the
 * world rather than rules about the player:
 *
 *   - You have to be standing on the floor. Its lift button is not lit,
 *     which is the game's own rule, so the way up is the stairs and the
 *     stairs are metres bld_walk_all() already charges.
 *   - The landlord's fit-out is priced by the square metre of LETTABLE space
 *     on the floor: lighting, the riser, the fire panel, the lift stopping
 *     there. Big floors cost more to commission and are worth more in rent,
 *     which is the same arithmetic a landlord does.
 *
 * SITE_OPEN_PER_M2 IS A CHOSEN NUMBER, in the same sense as D25's two: it is
 * a defensible commissioning rate and nothing downstream of it is tuned. At
 * two pounds a metre a typical eight-hundred-metre office floor is about
 * sixteen hundred, against a starting budget of sixty thousand and a switch
 * at four hundred -- a floor is a real purchase and not a prohibition. */
#define SITE_OPEN_PER_M2  2

static long open_price(const Session *ses, int floor)
{
    double m2 = 0;
    for (int i = 0; i < ses->b.nrooms; i++)
        if (ses->b.rooms[i].floor == floor && ses->b.rooms[i].tenant)
            m2 += bld_room_area(&ses->b.rooms[i]);
    return (long)(m2 + 0.5) * SITE_OPEN_PER_M2;
}

static void do_open(Session *ses, Buf *out)
{
    if (ses->floors >= ses->b.floors) {
        buf_puts(out, "every floor in this tower is already in service.\n");
        return;
    }
    int f = ses->floors;
    int lets = 0, drops = 0;
    for (int i = 0; i < ses->b.nrooms; i++)
        if (ses->b.rooms[i].floor == f && ses->b.rooms[i].tenant) lets++;
    for (int i = 0; i < ses->s.ntenant; i++)
        if (ses->s.tenant[i].floor == f) drops += ses->s.tenant[i].drops;
    long fee = open_price(ses, f);

    /* YOU HAVE TO BE THERE. Not because a rule says so: the lift does not
     * stop at a floor nobody has opened, so the only way onto it is the
     * stairwell, and walking up it is metres of building like any other. */
    if (here_floor(ses) != f) {
        int up = bld_find(&ses->b, f, RM_STAIR);
        if (up < 0) up = bld_find(&ses->b, f, RM_LIFTLOBBY);
        if (up < 0) up = bld_find(&ses->b, f, RM_CORRIDOR);
        char w[48];
        room_label(ses, up, w, sizeof w);
        buf_printf(out, "floor %d is not in service and you are on floor %d. "
                        "Somebody has to be\n  standing on it to sign it off -- "
                        "and the lift button is not lit, so that\n  is the "
                        "stairs: `go #%d` (%s), then `open`.\n",
                   f, here_floor(ses), up, w);
        buf_printf(out, "  it will cost %ld: %d let space%s on it and the "
                        "landlord charges the fit-out\n  by the metre.\n",
                   fee, lets, lets == 1 ? "" : "s");
        return;
    }
    if (ses->s.money < fee) {
        buf_printf(out, "refused: floor %d is not in service -- its fit-out is %ld "
                        "and you have %ld.\n  A floor comes into service when it "
                        "has been paid for.\n", f, fee, ses->s.money);
        return;
    }
    ses->s.money -= fee;
    ses->s.spent += fee;
    ses->floors++;
    buf_printf(out, "floor %d is in service, %ld paid for the fit-out, %ld left. "
                    "%d let space%s\n  on it, wanting %d drop%s between them.\n",
               f, fee, ses->s.money, lets, lets == 1 ? "" : "s",
               drops, drops == 1 ? "" : "s");
}

/* ------------------------------------------------------------- the cart */
static void do_plug(Session *ses, const char *what, bool hdmi, Buf *out)
{
    int d;
    /* BOTH HANDS ARE ON THE BOX. A lead, a drum and a switch are three
     * things and a person has two hands; this is the only thing stopping a
     * player carrying a server round the building while typing at it. */
    if (ses->carrying >= 0) {
        buf_printf(out, "refused: no lead went in -- you are carrying %s in both "
                        "hands.\n  Put it down first: `drop`.\n",
                   ses->s.dev[ses->carrying].name);
        return;
    }
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
    if (site_kind_has_os(dev->kind) && !dev->powered) {
        /* A SERIAL LEAD IS NOT A POWER LEAD. It used to be: the first lead in
         * the back of a box was what installed and booted it, so the crash
         * cart was what put machines on the network. */
        buf_printf(out, "refused: no lead went in -- %s is switched off. A serial "
                        "lead reads a\n  console; it does not press the button. "
                        "`power %s on`.\n",
                   dev->name, dev->name);
        return;
    }
    ses->plugged = d;
    ses->hdmi = false;
    if (dev->kind == SDEV_PC || dev->kind == SDEV_SERVER) {
        ses->where = SES_SHELL;
        Machine *m = ses->mach[d];
        buf_printf(out, "serial console on %s.\n", dev->name);
        if (m)
            buf_printf(out, "[%s at %s]\n", m->boot.running ? "UP" : "DOWN",
                       boot_stage_name(m->boot.failed_at));
        buf_puts(out, "you are root on it. `help` for what this line is, "
                      "`unplug` to leave.\n");
        return;
    }
    ses->where = SES_MGMT;
    buf_printf(out, "management line on %s\n\n", dev->name);
    site_dump_dev_brief(&ses->s, d, out);
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

/* AND THE CHEAPEST DRUM IS ON THE SHELF TOO. `cat5` -- a hundred megabit,
 * for good -- has existed in the catalogue and in the netstack since the
 * pivot, and this function refused the word, so the one genuinely regrettable
 * cable in the game could not be bought by anybody playing it. The load gate
 * has measured a floor of desks filling a hundred megabit run to 97% since
 * D25; a player could not make that mistake, which meant "cheap copper" was
 * a sin the game named and did not sell. */
static CableKind kind_arg(const char *a, bool *ok)
{
    *ok = true;
    if (strcmp(a, "cat5e") == 0) return CAB_CAT5E;
    if (strcmp(a, "cat6") == 0)  return CAB_CAT6;
    if (strcmp(a, "fibre") == 0) return CAB_FIBRE;
    if (strcmp(a, "cat5") == 0)  return CAB_CAT5;
    *ok = false;
    return CAB_CAT6;
}

static void do_spool(Session *ses, int n, char *t[MAXTOK], Buf *out)
{
    if (n >= 2 && ses->carrying >= 0 && strcmp(t[1], "back") != 0) {
        buf_printf(out, "refused: the drum stays on the shelf -- you are carrying "
                        "%s, and a\n  drum of cable takes both hands too: `drop` "
                        "first.\n", ses->s.dev[ses->carrying].name);
        return;
    }
    /* `spool` on its own: what is in your hands. */
    if (n < 2) {
        if (ses->spool_kind < 0) {
            buf_puts(out, "your hands are empty. `spool cat6` takes a drum off "
                          "the shelf --\n  cat5, cat5e, cat6 or fibre, and the run is "
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
    if (!ok) { buf_printf(out, "no such cable: %s. cat5, cat5e, cat6 or fibre.\n", t[1]); return; }
    if (ses->cab_dev >= 0) {
        buf_printf(out, "refused: no fresh drum -- you are in the middle of a run, "
                        "one end in %s\n  port %d. Finish it, or `spool back` to "
                        "pull it out.\n",
                   ses->s.dev[ses->cab_dev].name, ses->cab_port);
        return;
    }
    /* THE DRUM IN YOUR HANDS IS THE DRUM IN YOUR HANDS.
     *
     * This reset the metres unconditionally, and `cable a b cat5e` calls it
     * with the kind spelled out on every run -- so a drum with 288 m left on
     * it silently became a fresh 305 m one, six times in a row, and printed
     * "you have 305 m of cat5e on the spool" immediately after `spool` had
     * said 288. The drum was effectively infinite and the count was a lie,
     * which is worse than either. Asking for what you are already holding is
     * not a trip to the store cupboard. */
    if (ses->spool_kind == (int)k) {
        buf_printf(out, "you already have the %s drum: %d m left on it.\n",
                   site_cable_name(k), ses->spool_left);
        return;
    }
    if (ses->spool_kind >= 0)
        buf_printf(out, "the %s drum goes back on the shelf.\n",
                   site_cable_name((CableKind)ses->spool_kind));
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
        buf_puts(out, "refused: nothing went into that port -- you have no spool "
                      "in your hands.\n"
                      "  `spool cat6`      take a drum, then plug an end in\n"
                      "  `plug <box>`      (no port) puts the crash cart's serial "
                      "lead in instead\n");
        return;
    }
    if (port < 0) {
        port = site_free_port(&ses->s, d);
        if (port < 0) {
            buf_printf(out, "refused: nothing went in -- %s has no free port left, "
                            "all %d are used.\n  That is a purchase, not a "
                            "mistake.\n",
                       ses->s.dev[d].name, ses->s.dev[d].nports);
            return;
        }
    }
    if (port >= ses->s.dev[d].nports) {
        buf_printf(out, "refused: nothing went in -- %s has %d port%s, numbered 0 "
                        "to %d, and there\n  is no port %d on the back of it. "
                        "`look` says its next free one.\n",
                   ses->s.dev[d].name,
                   ses->s.dev[d].nports, ses->s.dev[d].nports == 1 ? "" : "s",
                   ses->s.dev[d].nports - 1, port);
        return;
    }
    if (net_port_state(ses->s.net, ses->s.dev[d].node, port) != PORT_NOCABLE) {
        buf_printf(out, "refused: nothing went in -- %s port %d already has a "
                        "cable in it.\n  `links` says which, `uncable <n>` pulls "
                        "it out.\n", ses->s.dev[d].name, port);
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
    /* BOTH ENDS IN THE SAME BOX is a loop with no spanning tree, and the game
     * refuses it when you ask for it in one line. It used to be reachable in
     * two: leave an end in after a run that failed, plug the next end into
     * the same switch, and you get a broadcast storm off a line that does not
     * look like it made a cable at all. */
    if (ses->cab_dev == d) {
        buf_printf(out, "both ends would be in %s -- port %d and port %d. A cable "
                        "from a switch\n  back into itself is a loop, and this one "
                        "has no spanning tree in it.\n  `spool back` pulls the end "
                        "you already put in out again.\n",
                   ses->s.dev[d].name, ses->cab_port, port);
        return;
    }
    /* Both ends in hand. The copper goes through the tray, which is a
     * different route from the one you walked, and the drum has to have
     * enough on it -- a refusal that is arithmetic rather than a rule. */
    int ra = ses->s.dev[ses->cab_dev].room, rb = ses->s.dev[d].room;
    int m = (ra == BLD_NOROOM || rb == BLD_NOROOM) ? SITE_PATCH_M
                                                   : site_metres(&ses->s, ra, rb);
    if (m >= 0 && m > ses->spool_left) {
        buf_printf(out, "refused: no cable was laid -- the run is %d m through the "
                        "tray and there\n  are only %d m left on the spool. `spool "
                        "back`, take a fresh drum, and start\n  again -- or put the "
                        "box somewhere nearer. The end you already put in is\n  "
                        "still in %s port %d.\n",
                   m, ses->spool_left, ses->s.dev[ses->cab_dev].name,
                   ses->cab_port);
        return;
    }
    int l = site_cable(&ses->s, ses->cab_dev, ses->cab_port, d, port,
                       (CableKind)ses->spool_kind);
    if (l < 0) {
        buf_printf(out, "refused: %s\n", site_err_text(ses->s.err));
        /* A HOLE THAT IS NOT A HOLE. The pair is punched down on a panel and
         * terminated on a wall somewhere else in the building, and the only
         * thing that goes in this end is the jack's own lead -- so say which
         * jack, because the player paid for it and may have forgotten. */
        if (ses->s.err == SITE_EJACK) {
            int j = site_port_jack(&ses->s, d, port);
            if (j < 0) j = site_port_jack(&ses->s, ses->cab_dev, ses->cab_port);
            if (j >= 0)
                buf_printf(out, "  it is the far end of j%d. `patch <box>:<port> "
                                "j%d`, from the room that\n  jack is on the wall "
                                "of, is what goes in it.\n", j, j);
        }
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
                    "paid, %s", l, ses->s.dev[ses->cab_dev].name, ses->cab_port,
               ses->s.dev[d].name, port, lk->metres,
               site_cable_name((CableKind)lk->kind), lk->cost,
               st == PORT_UP ? "the port comes up" :
               st == PORT_TOOLONG ? "TOO LONG -- it does not come up" :
               "the port does not come up");
    /* WHAT IT CAME UP AT, said at the moment the money leaves, because that
     * is where the cable decision is made and it is the only moment the
     * player is thinking about it. A run that negotiated slower than the
     * drum it came off says which end held it back -- a gigabit box will not
     * clock ten gigabit however good the copper is, and a player who has
     * just paid cat 6 rates for a desk drop should find that out here rather
     * than six floors later. */
    if (st == PORT_UP) {
        /* The SLOWER of the two ends, because that is what the link carries
         * -- the ISP handoff is rate-limited to the circuit and a run into
         * it is not a gigabit however good the drum was. */
        int na = net_port_speed(ses->s.net, ses->s.dev[ses->cab_dev].node,
                                ses->cab_port);
        int nb = net_port_speed(ses->s.net, ses->s.dev[d].node, port);
        int neg = na < nb ? na : nb;
        buf_printf(out, " at %d Mb", neg);
        int ka = site_kind_port_mb(ses->s.dev[ses->cab_dev].kind, ses->cab_port);
        int kb = site_kind_port_mb(ses->s.dev[d].kind, port);
        int kit = ka < kb ? ka : kb;
        if (neg == kit && (lk->kind == CAB_CAT6 || lk->kind == CAB_FIBRE))
            buf_printf(out, ", which is what port %d of %s does whatever you "
                            "plug into it",
                       ka <= kb ? ses->cab_port : port,
                       ka <= kb ? ses->s.dev[ses->cab_dev].name
                                : ses->s.dev[d].name);
    }
    buf_puts(out, ".\n");
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
    if (a == b) {
        buf_puts(out, "refused: no cable was run -- both ends of it are the same "
                      "box.\n");
        return;
    }

    /* AN END LEFT IN A SOCKET FROM AN EARLIER RUN. `cable` is a macro for
     * four steps, and if one of them refuses, the end is still in the box --
     * exactly as it would be for a person. What must not happen is the NEXT
     * `cable` silently using it as its first end: that made `cable core:4
     * edge:0` answer `link 4: core:3 to core:4`, a switch cabled to itself,
     * from a line naming two different boxes. */
    if (ses->cab_dev >= 0) {
        buf_printf(out, "refused: no cable was run -- one end of the spool is "
                        "already in %s port %d,\n  from a run that did not finish. "
                        "`spool back` pulls it out, or walk to the\n  other end and "
                        "`plug` it in.\n",
                   ses->s.dev[ses->cab_dev].name, ses->cab_port);
        return;
    }
    int links_before = ses->s.nlink;
    /* WHERE YOU WERE STANDING WHEN YOU ASKED, because that is where you will
     * be standing when it is done.
     *
     * `cable` walked you to the far end and left you there, which made the
     * one thing a socket player most wants to do -- queue a rack's worth of
     * runs out of one comms cupboard -- impossible: a playtester queued seven
     * fibre runs from the MDF, the first walked them to f1, and the other six
     * all answered "you are in neither room". Every run needed an interleaved
     * `go mdf` that the macro itself had made necessary.
     *
     * So it walks back. Not a teleport and not a discount: walk_to() charges
     * the metres in both directions, which is what a person laying six runs
     * out of one cupboard really does with their legs. */
    int started_in = ses->room;

    /* THE REFUSALS COME BEFORE THE DRUM COMES OFF THE SHELF, and they used to
     * come after. `cable a b` typed in a room with neither box in it took a
     * fresh drum, THEN said "you are in neither room" -- so a line that
     * refused still left cable in the player's hands, and the next `carry`
     * was refused because of a drum nobody asked for. A refused line has to
     * leave the world exactly as it found it, which is what the gate in
     * sessioncheck.c now measures on every one of them.
     *
     * Start from whichever end you are standing with. */
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
        buf_printf(out, "refused: no cable was run -- you are in neither room. %s "
                        "is in %s and\n  %s is in %s. Walk to one of them first: "
                        "`go %s`, then the same line\n  again -- it will walk you "
                        "back here when it is done.\n",
                   ses->s.dev[a].name, wa, ses->s.dev[b].name, wb,
                   ses->s.dev[a].name);
        return;
    }

    if (ses->spool_kind < 0 || (n > 3)) {
        char *sp[MAXTOK]; int sn = 2;
        char kbuf[16];
        snprintf(kbuf, sizeof kbuf, "%s", n > 3 ? t[3] : "cat6");
        sp[0] = (char *)"spool"; sp[1] = kbuf;
        if (ses->cab_dev < 0) do_spool(ses, sn, sp, out);
    }
    spool_plug(ses, aend, out);
    if (ses->cab_dev < 0) return;                 /* the first end refused */
    if (dev_here(ses, b) || walk_to(ses, ses->s.dev[b].room, out, false))
        spool_plug(ses, bend, out);
    /* AND IF IT DID NOT MAKE A CABLE, IT LEAVES NOTHING BEHIND. One line, one
     * outcome: either there is a link or the drum is as it was. */
    if (ses->s.nlink == links_before && ses->cab_dev >= 0) {
        buf_printf(out, "refused: no cable was run. The end comes back out of %s "
                        "port %d and the\n  spool is whole again.\n",
                   ses->s.dev[ses->cab_dev].name, ses->cab_port);
        ses->cab_dev = -1;
    }
    /* AND YOU WALK BACK TO THE END YOU STARTED AT. The next `cable` out of
     * this cupboard is the whole reason the macro exists. */
    if (ses->room != started_in) {
        char w[48];
        room_label(ses, started_in, w, sizeof w);
        Buf back = {0};
        if (walk_to(ses, started_in, &back, true))
            buf_printf(out, "you walk back to %s, where you started, so the next "
                            "run comes off the\n  same drum in the same room.\n", w);
        if (back.len) buf_put(out, back.p, back.len);
        buf_free(&back);
    }
}

/* ---------------------------------------------------------------- the jack
 * THE OTHER WAY TO BUY THE SAME METRES, and the one job in this building you
 * do not do with your own hands. There is no drum here and no walk: you stand
 * in the room that is to have the socket, name the port at the far end you
 * want it punched down to, and book somebody to pull it. What that buys is
 * that the copper belongs to the ROOM -- the next box to stand here, and the
 * one after that, plug in with a lead.
 *
 * What it costs is the two things the spool never costs you. It is dearer
 * than the same run, up front, on a floor that may only ever hold one box;
 * and it is not there today. `day` is what makes the second one bite, and a
 * tenancy three days from a strike is not a tenancy you can jack your way
 * out of. */
static void do_jack(Session *ses, int n, char *t[MAXTOK], Buf *out)
{
    char box[64];
    snprintf(box, sizeof box, "%s", t[1]);
    char *colon = strchr(box, ':');
    int port = -1;
    if (colon) { *colon = 0; port = colon[1] ? atoi(colon + 1) : -1; }
    int home = dev_arg(ses, box);
    if (home < 0) {
        buf_printf(out, "there is no box called %s in this building. The far end "
                        "of a jack is a port\n  on a box you own -- usually the "
                        "core switch, or the floor's own.\n", box);
        return;
    }
    if (!colon) {
        buf_printf(out, "jack %s:<port> -- a jack is punched down onto ONE socket "
                        "at the far end, and\n  that socket is gone for good. "
                        "`show %s` says which are free.\n",
                   ses->s.dev[home].name, ses->s.dev[home].name);
        return;
    }
    if (port < 0) {
        port = site_free_port(&ses->s, home);
        if (port < 0) {
            buf_printf(out, "refused: no jack was booked -- %s has no free port "
                            "left, all %d are used.\n", ses->s.dev[home].name,
                       ses->s.dev[home].nports);
            return;
        }
    }
    /* THE TRADE DOES THE PULL, NOT YOU -- but the wall is the wall you are
     * standing at, because that is the decision: which room is worth
     * cabling. */
    char w[48];
    room_label(ses, ses->room, w, sizeof w);
    buf_printf(out, "you book the trade for %s.\n", w);
    char cmd[160];
    snprintf(cmd, sizeof cmd, "jack #%d %s:%d %s", ses->room,
             ses->s.dev[home].name, port,
             n > 2 ? t[2] : site_cable_name(CAB_CAT5E));
    site_cmd(&ses->s, cmd, out);
}

/* ---------------------------------------------------------------- the quote
 * WHAT IT WOULD COST, ASKED BEFORE IT IS PAID FOR.
 *
 * The tower charges by the metre and prints the metres at the moment the
 * money leaves, and until now that was the FIRST moment anybody could learn
 * them. A playtester at day 62 put it exactly: there is no way to measure a
 * run before paying for it, so exercising the marginal-copper rule is
 * guess-and-pay at ~110 a guess. And it is not only that rule -- cat5 against
 * cat5e against cat6, spool against jack, this cupboard against that one are
 * all decisions D27 built and all of them were made blind.
 *
 * It quotes BOTH ways, because both are things a person asks:
 *
 *   quote f3.office        from the room you are standing in, which is where
 *                          you are when you are wondering where to put a box
 *   quote core f8.comms    between two named places, which is what you ask
 *                          with the floor plan in front of you and your legs
 *                          still in the chair
 *
 * It costs nothing and it takes no time: see docs/decisions-d32.md. A quote
 * does not walk you anywhere either -- reading a plan is not a journey. */
static void do_quote(Session *ses, int n, char *t[MAXTOK], Buf *out)
{
    char end[2][80];
    const char *want[2];
    int nw = 0;
    if (n >= 3) { want[0] = t[1]; want[1] = t[2]; nw = 2; }
    else        { want[0] = t[1]; nw = 1; }

    for (int i = 0; i < nw; i++) {
        /* A BOX FIRST, because a box is where a run really ends and the port
         * on the back of it is half the answer about speed. */
        int d = dev_arg(ses, want[i]);
        if (d >= 0) {
            const char *colon = strchr(want[i], ':');
            if (colon && colon[1])
                snprintf(end[i], sizeof end[i], "%s:%d", ses->s.dev[d].name,
                         atoi(colon + 1));
            else
                snprintf(end[i], sizeof end[i], "%s", ses->s.dev[d].name);
            continue;
        }
        int r = room_arg(ses, want[i]);
        if (r < 0) {
            buf_printf(out, "there is no room or box called %s. An end of a quote "
                            "is a box (`core`,\n  `core:2`) or a room (`comms`, "
                            "`f3.office`, `#41`).\n", want[i]);
            return;
        }
        snprintf(end[i], sizeof end[i], "#%d", r);
    }
    char line[192];
    if (nw == 2) snprintf(line, sizeof line, "quote %s %s", end[0], end[1]);
    /* FROM WHERE YOU STAND. One name is the common case: you are in the
     * cupboard with the drum, wondering what the far end costs. */
    else snprintf(line, sizeof line, "quote #%d %s", ses->room, end[0]);
    site_cmd(&ses->s, line, out);
}

/* THE LEAD. A metre of moulded patch cord between a box in this room and the
 * faceplate on this room's wall, and the only thing anybody pays for after
 * the jack is in. */
static void do_patch(Session *ses, int n, char *t[MAXTOK], Buf *out)
{
    int d;
    if (!need_here(ses, t[1], &d, out)) return;
    int j = -1;
    if (n > 2) {
        const char *a = t[2];
        if (*a == 'j') a++;
        j = atoi(a);
        if (j < 0 || j >= ses->s.njack) {
            buf_printf(out, "there is no jack %s. `jacks` lists them, `look` says "
                            "which are on this\n  room's wall.\n", t[2]);
            return;
        }
    } else {
        j = site_room_jack(&ses->s, ses->room, 0, true);
        if (j < 0) {
            int any = site_room_jack(&ses->s, ses->room, 0, false);
            if (any < 0)
                buf_puts(out, "there is no jack on this wall. `jack <box>:<port>` "
                              "has one put in --\n  it costs more than the same run "
                              "off the spool and it takes days, and\n  what you get "
                              "for that is a run that stays when the box goes.\n");
            else
                buf_printf(out, "every jack in this room has a lead in it already. "
                                "`links` says which,\n  `uncable <n>` takes one "
                                "out -- and the jack stays in the wall.\n");
            return;
        }
    }
    /* `patch sw3 j0` and `patch sw3:` both mean the next free socket on the
     * back of it, the same as one end of the spool does. */
    const char *colon = strchr(t[1], ':');
    int port = (colon && colon[1]) ? atoi(colon + 1) : -1;
    if (port < 0) {
        port = site_free_port(&ses->s, d);
        if (port < 0) {
            buf_printf(out, "refused: nothing went in -- %s has no free port left, "
                            "all %d are used.\n", ses->s.dev[d].name,
                       ses->s.dev[d].nports);
            return;
        }
    }
    char cmd[128];
    snprintf(cmd, sizeof cmd, "patch %s:%d j%d", ses->s.dev[d].name, port, j);
    site_cmd(&ses->s, cmd, out);
}

/* ------------------------------------------- handing a line to site_cmd */
/* The verbs that name a box. On the management line the box is assumed, so
 * `addr 10.0.1.1/24` is rewritten into the spelling site_cmd wants -- and
 * the full spelling still works, because tower.gd's cart passes lines
 * through untouched and the two must not disagree. */
static const char *DEVVERB[] = {
    "addr", "gw", "router", "subif", "vlan", "trunk", "dhcpd", "dhcp",
    "resolver", "ping", "trace", "resolve", "get", "show",
    /* A service is something you start ON a box, so you are at the box. */
    "httpd", "dnsd", "dns",
    /* And so are a battery and a disk: both are something somebody carries
     * to the rack and fits, not something that happens from the MDF. */
    "ups", "disk", NULL
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
           strcmp(v, "dhcp") == 0 ||
           /* Starting or stopping a service is a decision about the box, so
            * it is written onto the box -- see sync_disk. Without these two
            * the disk kept saying "serving" after `dhcpd <box> off`, and the
            * next boot started a pool the player had switched off. */
           strcmp(v, "dhcpd") == 0 || strcmp(v, "dnsd") == 0 ||
           /* And a name in a zone, which is config in exactly the way an
            * address is: without this the tower's own resolver came back
            * from a reboot with an empty zone. */
           strcmp(v, "dns") == 0;
}

static void after_config(Session *ses, const char *verb, int dev, Buf *out)
{
    if (!is_config(verb) && strcmp(verb, "router") != 0) return;
    if (dev < 0 || dev >= ses->s.ndev) return;
    if (ses->mach[dev]) sync_disk(ses, dev);
    /* WHETHER THAT WILL SURVIVE THE POWER GOING OFF, said out loud, because
     * for a name server it sometimes will not. A zone goes into
     * /etc/net/services on the box's own disk and netd reads it back -- but
     * only a box with an operating system in it HAS a disk. A router or a
     * switch running dnsd holds its zone in memory and nowhere else, and a
     * player who is not told that finds out during a mains failure. */
    if ((strcmp(verb, "dns") == 0 || strcmp(verb, "dnsd") == 0) &&
        net_dnsd_running(ses->s.net, ses->s.dev[dev].node)) {
        if (ses->mach[dev] || site_kind_has_os(ses->s.dev[dev].kind))
            buf_puts(out, "  (on its disk, in /etc/net/services: netd starts "
                          "the server and reads the\n  zone back when it "
                          "boots)\n");
        else
            buf_printf(out, "  NOT ON ANY DISK. %s has no operating system in "
                            "it, so its zone is in memory\n  and a power cut "
                            "loses it. A name server the tower depends on "
                            "belongs on a\n  box with a disk.\n",
                       ses->s.dev[dev].name);
    }
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
/* HOW THE WORLD REACHES THE OPERATING SYSTEM INSIDE A BOX.
 *
 * A blackout in core/siteday.c has to leave real damage on a real disk, and
 * the disks are here: a Machine exists once somebody has switched the box on.
 * A box nobody has ever powered has no Machine and gets NULL, which is right
 * -- a machine that has never run has nothing in flight to lose. This never
 * creates one, because creating a machine costs 13.5 MB and the weather must
 * not be what installs the tower. */
static struct Machine_ *session_box(void *ctx, int dev)
{
    Session *ses = (Session *)ctx;
    if (dev < 0 || dev >= SITE_MAX_DEV) return NULL;
    return ses->mach[dev];
}

bool session_start(Session *ses, uint64_t seed, long budget)
{
    memset(ses, 0, sizeof *ses);
    ses->seed = seed;
    ses->plugged = -1;
    ses->spool_kind = -1;
    ses->cab_dev = -1;
    ses->carrying = -1;
    ses->seat = -1;
    if (!bld_generate(&ses->b, seed)) return false;
    if (!site_new(&ses->s, &ses->b, seed, budget)) { bld_free(&ses->b); return false; }
    ses->room = bld_find(&ses->b, 0, RM_MDF);
    if (ses->room < 0) { site_free(&ses->s); bld_free(&ses->b); return false; }
    /* The ground floor and the one above it, as the 3D shell starts. */
    ses->floors = ses->b.floors < 2 ? ses->b.floors : 2;
    site_boxes(&ses->s, session_box, ses);
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
    ses->seat = -1;
    ses->up = false;
    ses->where = SES_DESK;
}

void session_prompt(const Session *ses, char *out, size_t cap)
{
    switch (ses->where) {
    case SES_SHELL: snprintf(out, cap, "root@%s# ", ses->s.dev[ses->plugged].name); break;
    case SES_MGMT:  snprintf(out, cap, "mgmt@%s# ", ses->s.dev[ses->plugged].name); break;
    /* NOT `root@`, THOUGH THE ACCOUNT IS ROOT. There is one account on every
     * machine in this game and /bin/whoami says so, so a prompt claiming a
     * user called Ada would be the game lying in the one place a player
     * cannot look away from. What the prompt has to carry is the thing that
     * IS different: this box is not yours. */
    case SES_SEAT:  snprintf(out, cap, "desk:%s# ",
                             ses->seat >= 0 ? ses->s.dev[ses->seat].name : "?");
                    break;
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
    /* WHERE THE KIT WILL TURN UP, said once, on the way in. A player who
     * orders a switch and then cannot find it has been told nothing, and
     * this is the one sentence that stops that happening. */
    {
        int g = site_goods_room(&ses->s);
        if (g >= 0) {
            char w[48];
            room_label(ses, g, w, sizeof w);
            buf_printf(out, "anything you order is delivered to %s and you "
                            "carry it from there.\n", w);
            /* AND WHAT IS ALREADY LYING ON THAT FLOOR, said on the way in.
             * The 3D window pre-orders three boxes into goods in and used to
             * say nothing at all about them; a playtester read the help,
             * believed the building was empty, and bought two of them again.
             * Counted off the device table, so a session that really does
             * start empty says nothing here. */
            int k = 0;
            for (int i = 0; i < ses->s.ndev; i++)
                if (ses->s.dev[i].room == g && ses->s.dev[i].tenant == 0) k++;
            if (k) {
                buf_printf(out, "AND THERE IS ALREADY A DELIVERY ON THE FLOOR OF "
                                "IT -- %d box%s, paid for:\n", k,
                           k == 1 ? "" : "es");
                for (int i = 0; i < ses->s.ndev; i++) {
                    const SiteDev *d = &ses->s.dev[i];
                    if (d->room != g || d->tenant != 0) continue;
                    buf_printf(out, "  %-10s %-9s %d already paid\n", d->name,
                               site_kind_name(d->kind), site_kind_price(d->kind));
                }
                buf_puts(out, "do not order those again: there is no `sell` and "
                              "the money does not come back.\n");
            }
        }
    }
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
    if (n < 0) {
        buf_printf(out, "that line has more than %d words in it, and none of it "
                        "has been done.\n  Nothing here needs that many: the "
                        "longest is a trunk, and a vlan per\n  tenancy on the "
                        "fullest building is under forty. If you meant a "
                        "trunk,\n  set it from the tower rather than the "
                        "management line, or split it in two --\n  `trunk` adds "
                        "to what a port already carries.\n", MAXTOK);
        return true;
    }

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

    /* AND THE SAME THING IN SOMEBODY ELSE'S CHAIR. One word gets you out of
     * it, and it is `stand` because that is what you do; `unplug` is taken
     * too, because it is the word this game has already taught for "put the
     * thing down and be a person in a room again". */
    if (ses->where == SES_SEAT) {
        if (n && (strcmp(t[0], "stand") == 0 || strcmp(t[0], "unplug") == 0 ||
                  strcmp(t[0], "leave") == 0)) {
            do_stand(ses, out);
            return true;
        }
        if (n && strcmp(t[0], "help") == 0) { do_help(ses, out); return true; }
        /* THE NEXT CHAIR ALONG. Twenty desks in one office and a player
         * comparing two of them should not have to type `stand` between
         * every pair -- and this is still one machine at a time, because
         * standing up is what happens first and it is what frees the last
         * one. There is no `sit` program on the machine to shadow. */
        if (n > 1 && strcmp(t[0], "sit") == 0) {
            do_stand(ses, out);
            do_sit(ses, t[n - 1], out);
            return true;
        }
        if (!n) return true;
        Machine *m = ses->seat >= 0 ? ses->mach[ses->seat] : NULL;
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
    if (strcmp(t[0], "power") == 0) {
        if (n < 2) {
            buf_puts(out, "power which box on? `power <box> on` `power <box> off`\n");
            return true;
        }
        int d;
        if (!need_here(ses, t[1], &d, out)) return true;
        if (n < 3) {
            buf_printf(out, "%s is %s. `power %s %s`?\n", ses->s.dev[d].name,
                       ses->s.dev[d].powered ? "on" : "off", ses->s.dev[d].name,
                       ses->s.dev[d].powered ? "off" : "on");
            return true;
        }
        do_power(ses, d, strcmp(t[2], "on") == 0, out);
        return true;
    }
    /* THE LIVE MEDIUM ON THE CRASH CART, and the tower had no way to reach it.
     *
     * A machine whose root filesystem will not mount cannot produce a shell
     * from its own disk -- that is the whole point of the initrd stopping
     * where it does, and its own last line says "boot the rescue medium and
     * run `fsck /dev/sda1`". The break-fix half has had that medium since
     * D17 and reached it over the service processor; standing in a comms
     * cupboard with a crash cart there was no verb for it at all, so the
     * first thing a blackout produced was a machine the game told you how to
     * fix and gave you no way to. The stick is on the cart. */
    if (strcmp(t[0], "rescue") == 0 || strcmp(t[0], "eject") == 0) {
        bool in = strcmp(t[0], "rescue") == 0;
        if (n < 2) {
            buf_printf(out, "%s which box?\n", t[0]);
            return true;
        }
        int d;
        if (!need_here(ses, t[1], &d, out)) return true;
        if (!site_kind_has_os(ses->s.dev[d].kind)) {
            buf_printf(out, "refused: the stick stays on the cart -- %s has no "
                            "drive to put it in.\n  `rescue` is for a pc or a "
                            "server; %s has a management line, so `plug %s`.\n",
                       ses->s.dev[d].name, ses->s.dev[d].name,
                       ses->s.dev[d].name);
            return true;
        }
        if (!ses->s.dev[d].powered) {
            buf_printf(out, "refused: nothing booted -- %s is switched off. `power %s "
                            "on` first.\n", ses->s.dev[d].name, ses->s.dev[d].name);
            return true;
        }
        Machine *m = ses->mach[d];
        if (!m) {
            buf_printf(out, "refused: nothing booted -- %s has never been switched on.\n", ses->s.dev[d].name);
            return true;
        }
        m->sp_media = in;
        m->sp_bootdev = in ? 1 : 0;
        if (in) {
            machine_boot_rescue(m);
            buf_printf(out, "the stick goes in the front of %s and you hold the "
                            "reset button.\n\n", ses->s.dev[d].name);
        } else {
            machine_boot(m);
            netsite_apply(m);
            buf_printf(out, "the stick comes out of %s and it boots its own "
                            "disk again.\n\n", ses->s.dev[d].name);
        }
        buf_put(out, m->boot.console.p, m->boot.console.len);
        buf_printf(out, "\n[%s at %s]\n", m->boot.running ? "UP" : "DOWN",
                   boot_stage_name(m->boot.failed_at));
        if (in && m->boot.running)
            buf_printf(out, "`plug %s` for a shell on the live system. The box's "
                            "own disk is\n  /dev/sda1 and nothing has mounted "
                            "it.\n", ses->s.dev[d].name);
        return true;
    }
    if (strcmp(t[0], "unplug") == 0) {
        buf_puts(out, "there is no lead in anything.\n");
        return true;
    }
    /* ------------------------------------------------ sitting down at a desk */
    if (strcmp(t[0], "sit") == 0) {
        if (n < 2) {
            buf_puts(out, "sit at which desk? `desks` lists every tenancy's, "
                          "`look` says which are\n  in this room. You have to be "
                          "in their office: `go <desk>` walks you there.\n");
            return true;
        }
        /* `sit at t1d3` and `sit down at t1d3` are what people type. */
        int a = 1;
        while (a < n - 1 && (strcmp(t[a], "at") == 0 || strcmp(t[a], "down") == 0))
            a++;
        do_sit(ses, t[a], out);
        return true;
    }
    if (strcmp(t[0], "stand") == 0) {
        buf_puts(out, "you are already on your feet. `sit <desk>` sits down at "
                      "somebody's computer.\n");
        return true;
    }
    if (strcmp(t[0], "desks") == 0 || strcmp(t[0], "people") == 0) {
        do_desks(ses, n, t, out);
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
        /* A room in the order is somebody expecting delivery to the floor
         * they are on. Say where it really goes rather than silently naming
         * the box after the room they typed. */
        if (n > 2 && site_room_by_name(&ses->s, t[2]) >= 0) {
            buf_printf(out, "kit is not delivered to a room of your choosing. It "
                            "comes to goods in\n  and somebody carries it. `buy %s`, "
                            "then `go goods`, `carry`, walk, `drop`.\n", t[1]);
            return true;
        }
        int d = site_order(&ses->s, kind, n > 2 ? t[2] : NULL);
        if (d < 0) { buf_printf(out, "refused: %s\n", site_err_text(ses->s.err)); return true; }
        int goods = ses->s.dev[d].room;
        char w[48];
        room_label(ses, goods, w, sizeof w);
        buf_printf(out, "%s: a %s, %d port%s, %d paid, %ld left.\n",
                   ses->s.dev[d].name, site_kind_name(kind), ses->s.dev[d].nports,
                   ses->s.dev[d].nports == 1 ? "" : "s", site_kind_price(kind),
                   ses->s.money);
        /* WHERE IT IS, AND HOW FAR THAT IS FROM YOU. The delivery is the
         * start of a job, not the end of one, and the metres are the job. */
        buf_printf(out, "the van leaves it in %s", w);
        if (goods == ses->room) buf_puts(out, ", which is where you are standing.\n");
        else {
            double *dm = nom_alloc(sizeof(double) * (size_t)ses->b.nrooms);
            double m = bld_walk_all(&ses->b, ses->room, dm) ? dm[goods] : BLD_INF;
            nom_free(dm);
            if (m < BLD_INF) buf_printf(out, ", %d m from here.\n", (int)(m + 0.5));
            else buf_puts(out, ".\n");
            buf_printf(out, "  `go goods`, `carry %s`, walk it to where it goes, "
                            "`drop`.\n", ses->s.dev[d].name);
        }
        return true;
    }
    /* --------------------------------------------------- carrying it there */
    /* The four words that turn a delivery into a rack: carry, walk, drop.
     * Nothing here is a teleport and nothing here is free -- walk_to charges
     * the metres, one box at a time, because both hands are on it. */
    /* `lift` is the lift, and it is handled above: a verb that meant both
     * "ride to floor 3" and "pick that switch up" would be one typo away
     * from a walk nobody asked for. */
    if (strcmp(t[0], "carry") == 0 || strcmp(t[0], "pick") == 0) {
        int at = (strcmp(t[0], "pick") == 0 && n > 2 &&
                  strcmp(t[1], "up") == 0) ? 2 : 1;
        if (n <= at) {
            if (ses->carrying >= 0)
                buf_printf(out, "you are carrying %s. `drop` puts it down here.\n",
                           ses->s.dev[ses->carrying].name);
            else
                buf_puts(out, "carry what? `look` says what is in this room. Kit is "
                              "delivered to\n  goods in, so that is usually where it "
                              "is: `go goods`.\n");
            return true;
        }
        int d;
        if (!need_here(ses, t[at], &d, out)) return true;
        if (ses->carrying == d) {
            buf_printf(out, "you are already carrying %s.\n", ses->s.dev[d].name);
            return true;
        }
        if (ses->carrying >= 0) {
            buf_printf(out, "refused: %s is still in your hands and both your "
                            "hands are on it --\n  you did not pick %s up. `drop` "
                            "puts %s down here, and then\n  `carry %s`.\n",
                       ses->s.dev[ses->carrying].name, ses->s.dev[d].name,
                       ses->s.dev[ses->carrying].name, ses->s.dev[d].name);
            return true;
        }
        if (ses->spool_kind >= 0) {
            buf_printf(out, "refused: you have a drum of cable in your hands, so "
                            "%s is still on\n  the floor -- you did not pick it up. "
                            "`spool back` puts the drum on the\n  shelf, and then "
                            "`carry %s`.\n",
                       ses->s.dev[d].name, ses->s.dev[d].name);
            return true;
        }
        /* A tenant's computer is a tenant's computer. The model will happily
         * move it -- it is not cabled and not fixed to a wall -- but walking
         * out of a leased floor with the machine somebody works on is not a
         * thing the building's IT department gets to do. */
        if (ses->s.dev[d].tenant != 0) {
            buf_printf(out, "refused: %s belongs to the tenant on floor %d, not "
                            "to you, and it\n  stays where it is. Their kit is "
                            "theirs; you are here for the wall, the\n  cupboard "
                            "and the copper.\n",
                       ses->s.dev[d].name, ses->s.dev[d].floor);
            return true;
        }
        if (!site_move(&ses->s, d, ses->room)) {
            buf_printf(out, "refused: %s\n", site_err_text(ses->s.err));
            if (ses->s.err == SITE_ECABLED)
                buf_printf(out, "  %s is on the end of a cable. `links` says which "
                                "one, `uncable <n>` pulls\n  it out -- and the copper "
                                "is paid for, so moving a box costs the run.\n",
                           ses->s.dev[d].name);
            if (ses->s.err == SITE_EFIXED)
                buf_puts(out, "  the handoff is the ISP's, on their wall, in their "
                              "conduit.\n");
            if (ses->s.err == SITE_EJACK)
                buf_printf(out, "  a jack is punched down onto a socket on the back "
                                "of %s, and the other\n  end of that run is screwed "
                                "to a wall in another room. `jacks` says which.\n"
                                "  That is what `for good` meant when you bought "
                                "it: this box stays here.\n",
                           ses->s.dev[d].name);
            return true;
        }
        ses->carrying = d;
        buf_printf(out, "you pick %s up. It goes where you go until you `drop` it.\n",
                   ses->s.dev[d].name);
        return true;
    }
    if (strcmp(t[0], "drop") == 0 || strcmp(t[0], "put") == 0 ||
        strcmp(t[0], "place") == 0) {
        if (ses->carrying < 0) {
            buf_puts(out, "you are not carrying anything.\n");
            return true;
        }
        int d = ses->carrying;
        ses->carrying = -1;
        site_move(&ses->s, d, ses->room);
        char w[48];
        room_label(ses, ses->room, w, sizeof w);
        buf_printf(out, "%s is in %s now. %d port%s, and nothing in any of them "
                        "yet.\n", ses->s.dev[d].name, w, ses->s.dev[d].nports,
                   ses->s.dev[d].nports == 1 ? "" : "s");
        return true;
    }
    if (strcmp(t[0], "uncable") == 0 && n < 2) {
        buf_puts(out, "uncable which one? `links` numbers them.\n");
        return true;
    }
    if (strcmp(t[0], "cable") == 0) {
        if (n < 3) { buf_puts(out, "cable <box> <box> [cat5|cat5e|cat6|fibre]\n"); return true; }
        do_cable(ses, n, t, out);
        return true;
    }
    if (strcmp(t[0], "jack") == 0) {
        if (n < 2) {
            buf_puts(out, "jack <box>:<port> [cat5|cat5e|cat6|fibre]\n"
                          "  a permanent socket on THIS room's wall, with the run "
                          "behind it punched\n  down onto that port at the far end "
                          "for good. Priced by the same tray\n  metres the spool "
                          "is, plus the fit-out -- and it takes the trade days.\n"
                          "  `jacks` lists the ones you have.\n");
            return true;
        }
        do_jack(ses, n, t, out);
        return true;
    }
    if (strcmp(t[0], "quote") == 0) {
        if (n < 2) {
            buf_puts(out, "quote <room|box> [<room|box>]\n"
                          "  what that run would cost before you run it: the tray "
                          "metres, the price\n  in every grade, what each would "
                          "come up at over that distance, and the\n  same run as a "
                          "jack. One name quotes from the room you are standing "
                          "in.\n  Nothing is bought and nothing is charged.\n");
            return true;
        }
        do_quote(ses, n, t, out);
        return true;
    }
    if (strcmp(t[0], "patch") == 0) {
        if (n < 2) {
            buf_puts(out, "patch <box>:<port> [j<n>]\n"
                          "  a lead from a box in this room into a jack on this "
                          "room's wall. `look`\n  says which jacks are here.\n");
            return true;
        }
        do_patch(ses, n, t, out);
        return true;
    }

    /* TIME PASSES WHEREVER YOU ARE STANDING, and so does the phone call to
     * the ISP. `day` is the whole loop: tenants move in, their people work
     * over what you built, and the rent for the work that finished arrives. */
    if (strcmp(t[0], "day") == 0 || strcmp(t[0], "isp") == 0) {
        site_cmd(&ses->s, raw, out);
        return true;
    }
    /* RUNNING COPPER TO A TENANCY'S DESKS. You have to be at the box it
     * comes out of, because somebody is standing at that box with a drum. */
    if (strcmp(t[0], "serve") == 0) {
        if (n < 3) {
            buf_puts(out, "serve <tenant> <box> [cat5|cat5e|cat6|fibre] [vlan]\n"
                          "  one cable from that box to each of the tenancy's "
                          "desks, by the metre.\n  Name a vlan and every port it "
                          "patches goes into it as it is patched.\n");
            return true;
        }
        int d;
        if (!need_here(ses, t[2], &d, out)) return true;
        /* A TENANCY THAT HAS NOT MOVED IN YET HAS NO DESKS TO CABLE TO, and
         * saying so as "refused: no such device" sent a playtester hunting a
         * typo in a box name that was correct and in the room with them.
         * There is nothing to run copper to until their keys turn, and the
         * day that happens is already in `demand`. */
        int want = atoi(t[1]);
        for (int i = 0; i < ses->s.ntenant; i++) {
            const SiteTenant *tn = &ses->s.tenant[i];
            if (tn->tenant != want || tn->moved) continue;
            buf_printf(out, "tenancy %d does not move in until day %d, and their "
                            "desks arrive with\n  them -- there is nothing on that "
                            "floor to run copper to yet. It is day %d.\n"
                            "  `demand` lists who is coming and when. The switch "
                            "can go in early; the\n  drops cannot.\n",
                       want, tn->day, ses->s.day);
            return true;
        }
        site_cmd(&ses->s, raw, out);
        return true;
    }

    /* Reading the state costs nothing and needs no legs: it is a clipboard. */
    if (strcmp(t[0], "links") == 0 || strcmp(t[0], "money") == 0 ||
        strcmp(t[0], "demand") == 0 || strcmp(t[0], "frames") == 0 ||
        strcmp(t[0], "rooms") == 0 || strcmp(t[0], "uncable") == 0 ||
        strcmp(t[0], "credit") == 0 || strcmp(t[0], "status") == 0 ||
        strcmp(t[0], "service") == 0 || strcmp(t[0], "load") == 0 ||
        strcmp(t[0], "events") == 0 || strcmp(t[0], "jacks") == 0 ||
        (strcmp(t[0], "show") == 0)) {
        site_cmd(&ses->s, raw, out);
        return true;
    }

    /* Everything else that names a box: you have to be in the room with it. */
    if (is_devverb(t[0])) {
        /* WHICH BOX -- AND WHAT THE VERB WANTS AFTER IT. Naming the missing
         * box and stopping there left a player who typed `dhcpd` no better
         * off than before; the spelling lives in one table in core/site.c
         * and this is where it is asked for. */
        if (n < 2) {
            buf_printf(out, "%s which box?\n", t[0]);
            site_cmd(&ses->s, raw, out);
            return true;
        }
        int d;
        if (!need_here(ses, t[1], &d, out)) return true;
        site_cmd(&ses->s, raw, out);
        after_config(ses, t[0], d, out);
        return true;
    }

    buf_printf(out, "no such command: %s. `help` lists what you can do here.\n", t[0]);
    return true;
}
