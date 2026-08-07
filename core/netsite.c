/* netsite.c — the network the machine is actually plugged into.
 *
 * THE OLD SHORTCUT, AND WHY IT HAD TO GO. `links` used to call a function
 * that looked a hostname up in a table and returned the page. The page was
 * good writing and it still is -- net_sites.c is untouched -- but the FETCH
 * was a lie: there was no query, no connection, and nothing that could fail
 * for a reason a player could diagnose. "It works by address but not by
 * name" was a comment in that file describing a diagnosis the machine could
 * not actually produce.
 *
 * Now it can. This file builds one real site network for the process:
 *
 *     10.0.2.2   gw      the router, and the DHCP server
 *     10.0.2.3   ns1     a nameserver holding the zone from net_sites.c
 *     10.0.2.20  web     the whole web, on one box with thirty addresses
 *                        on its interface, which is how virtual hosting has
 *                        always worked
 *
 * and plugs the customer's machine into the switch beside them. Its address,
 * its mask, its default route and its resolver are READ OFF ITS OWN DISK,
 * out of /etc/net/interfaces and /etc/resolv.conf, every time the config
 * changes. So the faults that were already in the image stop being cosmetic:
 *
 *   - /etc/resolv.conf pointing at 10.0.2.9 is a machine talking to an
 *     address nothing answers on. The query goes out, nothing comes back,
 *     and it takes the timeout to find that out -- which is what a dead
 *     resolver sounds like. Nobody wrote a "resolver is wrong" branch.
 *   - a machine whose interfaces file says `address dhcp` genuinely asks,
 *     and genuinely gets an answer from a server with a finite pool.
 *   - netd not running means the interface is not configured, so there is
 *     no address, so nothing resolves and nothing connects. That chain is
 *     three real mechanisms, not a flag.
 *
 * ONE NETWORK FOR THE PROCESS. A Net is about a megabyte and a host inside
 * one is about a kilobyte, so the world is shared and the per-machine cost
 * is the kilobyte. A tower of three hundred machines pays for three hundred
 * kilobytes, not three hundred megabytes.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"
#include "netstack.h"
#include "abi.h"

/* The zone, and the pages. Both live in net_sites.c, which is content. */
int  net_site_hosts(int i, const char **host, const char **ip);
bool kernel_svc_running(Machine *m, const char *name);

/* IS THE NETWORK DAEMON UP. The unit is called "net" and the binary it runs
 * is /usr/sbin/netd, and both spellings are in the image and in the wiki, so
 * both are accepted here. Asking for only one of them meant the daemon was
 * always reported dead, every machine had an interface that was never
 * configured, and nothing on the network worked on a machine that was
 * perfectly healthy. */
static bool netd_up(Machine *m)
{
    return kernel_svc_running(m, "net") || kernel_svc_running(m, "netd");
}

static Net *SITE;
static int  SW = -1, GW = -1, NS = -1, WEB = -1;
static int  next_port;
/* PORTS THAT HAVE COME FREE.
 *
 * The harness works a hundred tickets one after another, and every machine
 * it finishes with is freed. Without this, each one left a node holding a
 * switch port and a DHCP lease it would never give back: the twenty-fifth
 * ticket found the switch full, the pool exhausted, and a machine that could
 * not get an address -- and, worse, what a ticket's network looked like
 * depended on how many tickets had run before it, which is exactly the kind
 * of hidden order-dependence that makes a seed stop reproducing.
 *
 * A released box keeps its node and its MAC, so plugging it back in gets the
 * same lease from the same server. It is the same box. */
#define FREED_MAX 32
static struct { int node, port; } freed[FREED_MAX];
static int nfreed;
/* Which build of the world this is. A machine that was plugged into an
 * earlier one holds a node id that means nothing now, and this is how it
 * finds out rather than quietly reading somebody else's interface. */
static uint32_t SITE_GEN = 1;

/* A small decimal, for the "/24" spelling of a netmask. No libc atoi here:
 * this file is host side, but the value comes off a customer's disk and a
 * config full of letters must give 0 rather than whatever strtol felt. */
static int small_int(const char *s)
{
    int v = 0;
    if (!s || !*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10 + (*s - '0');
        if (v > 999) return 0;
    }
    return v;
}

#define SITE_MASK  24
#define SITE_NET   "10.0.2."

/* Build the site once. Deterministic: same order, same addresses, same MACs,
 * every run. */
void netsite_reset(void);

static Net *site(void)
{
    if (SITE) return SITE;
    SITE = net_new(0x51e0);
    /* Twenty-four ports: the switch in the comms cupboard, and the reason a
     * twenty-fifth machine has nowhere to plug in. */
    SW  = net_add_switch(SITE, "sw-comms", 24);
    GW  = net_add_host(SITE, "gw");
    NS  = net_add_host(SITE, "ns1");
    WEB = net_add_host(SITE, "web");
    next_port = 0;
    net_cable(SITE, GW,  0, SW, next_port++, 3, CAB_CAT5E);
    net_cable(SITE, NS,  0, SW, next_port++, 3, CAB_CAT5E);
    net_cable(SITE, WEB, 0, SW, next_port++, 3, CAB_CAT5E);

    net_if_addr(SITE, GW,  0, net_ip(10, 0, 2, 2),  net_mask_bits(SITE_MASK));
    net_if_addr(SITE, NS,  0, net_ip(10, 0, 2, 3),  net_mask_bits(SITE_MASK));
    net_if_addr(SITE, WEB, 0, net_ip(10, 0, 2, 20), net_mask_bits(SITE_MASK));
    net_forwarding(SITE, GW, true);

    /* The pool starts at .100 so that the static addresses the image ships
     * with -- .15 among them -- are below it and cannot collide. */
    net_dhcpd(SITE, GW, net_ip(10, 0, 2, 100), 24, net_mask_bits(SITE_MASK),
              net_ip(10, 0, 2, 2), net_ip(10, 0, 2, 3));

    /* Load the zone into a real nameserver, and give the web server every
     * address the zone points at. Both come out of the same table, so a
     * page nobody can name is impossible and a name that points nowhere is
     * impossible -- the two cannot drift, because there is one source. */
    net_dnsd(SITE, NS);
    for (int i = 0; ; i++) {
        const char *h = NULL, *ip = NULL;
        if (!net_site_hosts(i, &h, &ip)) break;
        uint32_t a = 0;
        if (!net_parse_ip(ip, &a)) continue;
        net_dns_record(SITE, NS, h, a);
        if (a != net_ip(10, 0, 2, 20)) net_if_alias(SITE, WEB, a);
    }
    net_httpd(SITE, WEB, 80);
    return SITE;
}

/* WHICH NETWORK THIS MACHINE IS ON. Its own tower's, if it stands in one;
 * otherwise the single world this file keeps for the break-fix game. Every
 * syscall below goes through here, so a shell on a server in the player's
 * building reads and writes the player's network and nothing else. */
static Net *homenet(Machine *m)
{
    return (m && m->net_home) ? (Net *)m->net_home : site();
}

/* Put this machine on a node that already exists, in a network somebody else
 * owns. The cable is already in it -- the player ran it -- so there is no
 * port to allocate and nothing to plug in. */
void netsite_pin(Machine *m, struct Net *n, int node)
{
    if (!m || !n || node < 0) return;
    m->net_home = n;
    m->net_node = node;
    m->net_port = -1;
    m->net_gen  = SITE_GEN;
    m->net_cfg  = 0;          /* so the next syscall re-reads the disk */
}

/* ------------------------------------------------------ the machine's own */
/* One value that changes whenever anything the network depends on changes.
 * Cheaper than re-reading four files on every syscall, and it means an edit
 * to resolv.conf takes effect the moment somebody makes it. */
static uint32_t cfg_hash(Machine *m)
{
    static const char *WATCH[] = { "/etc/net/interfaces", "/etc/resolv.conf",
                                   "/etc/net/services", NULL };
    uint32_t h = 2166136261u;
    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    for (int i = 0; WATCH[i]; i++) {
        VNode *n = vfs_resolve(fs, WATCH[i], NULL);
        if (!n || n->kind != VN_FILE) { h = h * 16777619u + 0xff; continue; }
        for (size_t k = 0; k < n->data.len; k++)
            h = (h ^ (unsigned char)n->data.p[k]) * 16777619u;
    }
    /* A network daemon that is not running is a machine with no address. */
    h = h * 16777619u + (netd_up(m) ? 1u : 2u);
    /* And a service that is not running is a port nobody is listening on.
     * The port itself comes out of the daemon's own state file, so stopping
     * httpd, or editing its config and never reloading it, changes what is
     * really bound -- not just what a tool prints. */
    static const char *SVC[] = { "sshd", "httpd", "postfix", NULL };
    for (int i = 0; SVC[i]; i++) {
        h = h * 16777619u + (kernel_svc_running(m, SVC[i]) ? 3u : 5u);
        char sp[NOM_PATH_MAX];
        snprintf(sp, sizeof sp, "/run/%s.state", SVC[i]);
        VNode *sn = vfs_resolve(fs, sp, NULL);
        for (size_t k = 0; sn && sn->kind == VN_FILE && k < sn->data.len; k++)
            h = (h ^ (unsigned char)sn->data.p[k]) * 16777619u;
    }
    if (!h) h = 1;
    return h;
}

/* The port a running daemon actually opened.
 *
 * Read out of /run/<name>.state, which is the line the PROCESS loaded, and
 * only falling back to the config file when there is no state file. That
 * order is the whole point: a daemon running with a stale configuration is
 * listening on the old port, and a tool that read the new file would say
 * otherwise and be wrong. */
static int svc_port(Machine *m, const char *svc, const char *key, int dflt)
{
    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    char sp[NOM_PATH_MAX];
    snprintf(sp, sizeof sp, "/run/%s.state", svc);
    VNode *sn = vfs_resolve(fs, sp, NULL);
    if (sn && sn->kind == VN_FILE && sn->data.len) {
        /* Two lines: the config it read, then the first real line of it. */
        const char *nl = memchr(sn->data.p, '\n', sn->data.len);
        if (nl) {
            const char *v = nl + 1, *e = sn->data.p + sn->data.len;
            size_t kl = strlen(key);
            while (v < e && (*v == ' ' || *v == '\t')) v++;
            if ((size_t)(e - v) > kl && strncmp(v, key, kl) == 0) {
                v += kl;
                while (v < e && (*v == ' ' || *v == '\t' || *v == ':' || *v == '=')) v++;
                int p = 0;
                while (v < e && *v >= '0' && *v <= '9') p = p * 10 + (*v++ - '0');
                if (p > 0 && p < 65536) return p;
            }
        }
    }
    return dflt;
}

/* Open the sockets the running services really have open. */
static void bind_services(Net *n, Machine *m, int node)
{
    if (kernel_svc_running(m, "sshd"))
        net_tcp_listen(n, node, (uint16_t)svc_port(m, "sshd", "Port", 22));
    /* A WEB SERVER THAT SERVES. This used to open a bare listening socket,
     * so a machine whose httpd was running accepted connections and then
     * answered nothing at all -- and worse, it held the port, so the site's
     * own `httpd <box>` could not bind and the box served nobody however
     * many times you started it. If the service is running on the disk, the
     * daemon that answers is what listens. */
    if (kernel_svc_running(m, "httpd"))
        net_httpd(n, node, (uint16_t)svc_port(m, "httpd", "Listen", 80));
    if (kernel_svc_running(m, "postfix"))
        net_tcp_listen(n, node, 25);
}

/* WHAT THIS BOX SERVES, OFF ITS OWN DISK.
 *
 * An address lives in /etc/net/interfaces and comes back when the box does.
 * A DHCP pool the player started lived in the stack and nowhere else, so a
 * server that was serving twenty desks came back from a power cut addressed,
 * booted, `svc`-clean -- and handing out nothing, with no line anywhere
 * saying so. A machine that reports a service running while the tower serves
 * nothing from it is the worst kind of wrong this project can be.
 *
 * So the tower writes what it started onto the disk beside the address (see
 * sync_disk in core/session.c) and netd starts it again here, from the file.
 * One source of truth: a line in the file is a pool, no line is no pool, and
 * `dhcpd <box> off` takes the line out.
 *
 *     dhcpd <first> <count> <bits> <gw> <dns>
 *     dnsd
 *     record <name> <ip>
 *
 * The pool is scoped by netstack to the interface whose address is inside
 * it, so a box that comes back with a different address does not come back
 * serving somebody else's subnet: the pool simply does not start, which is
 * the honest outcome and is visible in `dhcpd <box>`.
 *
 * THAT SCOPING USED TO COST A FLOOR SERVER ITS POOLS ALTOGETHER, and it was
 * recorded as a known limit rather than fixed: sync_disk wrote `iface eth0`
 * and nothing else, so a pool on a tagged subinterface came back looking for
 * an interface that no longer existed and did not start. It was the right
 * call while a subinterface was a router's business; D27 made a per-floor
 * vlan server the recommended build and it stopped being one. The disk names
 * every interface now -- see read_ifaces() below -- so the subinterface is
 * back before start_services() runs and the pool lands where it did.
 */
#define SVC_FILE "/etc/net/services"

static const char *next_word(const char *p, const char *end, char *out, size_t cap)
{
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    size_t o = 0;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && o < cap - 1)
        out[o++] = *p++;
    out[o] = 0;
    return p;
}

static void start_services(Net *n, Machine *m, int node)
{
    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    VNode *f = vfs_resolve(fs, SVC_FILE, NULL);
    if (!f || f->kind != VN_FILE) return;
    const char *p = f->data.p, *end = p + f->data.len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        char verb[16];
        const char *q = next_word(p, nl, verb, sizeof verb);
        if (strcmp(verb, "dhcpd") == 0) {
            char a[24], c[16], b[16], g[24], d[24];
            q = next_word(q, nl, a, sizeof a);
            q = next_word(q, nl, c, sizeof c);
            q = next_word(q, nl, b, sizeof b);
            q = next_word(q, nl, g, sizeof g);
            q = next_word(q, nl, d, sizeof d);
            uint32_t first = 0, gw = 0, dns = 0;
            int bits = small_int(b), count = small_int(c);
            net_parse_ip(g, &gw);
            net_parse_ip(d, &dns);
            if (net_parse_ip(a, &first) && bits > 0 && bits <= 32 && count > 0)
                net_dhcpd(n, node, first, count, net_mask_bits(bits), gw, dns);
        } else if (strcmp(verb, "dnsd") == 0) {
            net_dnsd(n, node);
        } else if (strcmp(verb, "record") == 0) {
            /* A NAME THIS BOX SERVES. Same argument as the pool above: a
             * zone the player typed lived in the stack and nowhere else, so
             * a reboot turned the tower's own resolver into a box that
             * answered `no such host` about every machine in the building.
             * A record line with no `dnsd` line before it starts nothing,
             * which is the honest reading of a zone with no server. */
            char nm[64], a[24];
            uint32_t rip = 0;
            q = next_word(q, nl, nm, sizeof nm);
            q = next_word(q, nl, a, sizeof a);
            if (nm[0] && net_parse_ip(a, &rip) && net_dnsd_running(n, node))
                net_dns_record(n, node, nm, rip);
        }
        p = nl < end ? nl + 1 : nl;
    }
}

/* The first value of `key` in a config file, in the shape netd reads. */
static bool cfg_field(Machine *m, const char *path, const char *key,
                      char *out, size_t cap)
{
    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    VNode *n = vfs_resolve(fs, path, NULL);
    if (!n || n->kind != VN_FILE) return false;
    size_t kl = strlen(key);
    const char *p = n->data.p, *end = p + n->data.len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        const char *t = p;
        while (t < nl && (*t == ' ' || *t == '\t')) t++;
        if (t < nl && *t != '#' && (size_t)(nl - t) > kl &&
            strncmp(t, key, kl) == 0 && (t[kl] == ' ' || t[kl] == '\t')) {
            const char *v = t + kl;
            while (v < nl && (*v == ' ' || *v == '\t')) v++;
            size_t o = 0;
            while (v < nl && *v != ' ' && *v != '\t' && *v != '#' && o < cap - 1)
                out[o++] = *v++;
            out[o] = 0;
            if (out[0]) return true;
        }
        p = nl < end ? nl + 1 : nl;
    }
    return false;
}

/* --------------------------------------------- every card, not just eth0 */
/* WHAT AN `iface` LINE NAMES.
 *
 * eth0 is the first socket on the back; eth1.13 is a tagged subinterface
 * riding on the second one, and NAMING ONE IS WHAT CREATES IT. That is the
 * whole of the fix for the worst bug a playtest has found in this file: a
 * floor server addressed on three vlans was written to disk as `iface eth0`
 * and nothing else, so a mains failure took its subinterfaces, its addresses
 * and its DHCP pools with it while the file on its own disk went on
 * describing a machine that no longer existed. The disk is the one source of
 * truth for a configuration, so the disk has to be able to SAY subinterface.
 *
 * Returns the interface index, or -1 for a name this box cannot have. */
static int if_by_name(Net *n, int node, const char *nm)
{
    if (nm[0] != 'e' || nm[1] != 't' || nm[2] != 'h') return -1;
    const char *p = nm + 3;
    if (*p < '0' || *p > '9') return -1;
    int nic = 0;
    while (*p >= '0' && *p <= '9' && nic < 1000) nic = nic * 10 + (*p++ - '0');
    if (!*p) {                                    /* a socket, eth0..ethN-1 */
        return nic < net_node_ports(n, node) ? nic : -1;
    }
    if (*p != '.') return -1;
    p++;
    if (*p < '0' || *p > '9') return -1;
    int vlan = 0;
    while (*p >= '0' && *p <= '9' && vlan < 10000) vlan = vlan * 10 + (*p++ - '0');
    if (*p) return -1;
    return net_if_subif(n, node, nic, vlan);
}

/* One `iface` stanza, as read off the disk. */
typedef struct {
    int      ifx;
    bool     dhcp, has_ip;
    uint32_t ip, mask;
} IfCfg;

/* Read /etc/net/interfaces into a list of stanzas, creating any tagged
 * subinterface a stanza names. A file with one `iface eth0` in it -- which
 * is every machine the break-fix half of the game ships -- comes out of here
 * as exactly one entry, which is what it was before this existed.
 *
 * THE FIRST STANZA FALLS BACK TO INTERFACE 0 when its name is not one this
 * box could have. An image whose config names a card udev did not create is
 * a real fault this game generates, and the honest reading of it is "the
 * config for this machine's one card", which is what it has always been. */
static int read_ifaces(Machine *m, Net *n, int node, IfCfg *out, int cap)
{
    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    VNode *f = vfs_resolve(fs, "/etc/net/interfaces", NULL);
    if (!f || f->kind != VN_FILE) return 0;
    int nif = 0;
    IfCfg *cur = NULL;
    const char *p = f->data.p, *end = p + f->data.len;
    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        const char *t = p;
        while (t < nl && (*t == ' ' || *t == '\t')) t++;
        if (t < nl && *t != '#') {
            char key[24], val[64];
            const char *q = next_word(t, nl, key, sizeof key);
            q = next_word(q, nl, val, sizeof val);
            if (strcmp(key, "iface") == 0) {
                cur = NULL;
                if (nif < cap) {
                    int ifx = if_by_name(n, node, val);
                    if (ifx < 0 && nif == 0) ifx = 0;
                    if (ifx >= 0) {
                        /* The same card named twice is one card. */
                        for (int i = 0; i < nif; i++)
                            if (out[i].ifx == ifx) { cur = &out[i]; break; }
                        if (!cur) {
                            cur = &out[nif++];
                            memset(cur, 0, sizeof *cur);
                            cur->ifx = ifx;
                            cur->mask = net_mask_bits(SITE_MASK);
                        }
                    }
                }
            } else if (cur && strcmp(key, "address") == 0) {
                if (strcmp(val, "dhcp") == 0) cur->dhcp = true;
                else if (net_parse_ip(val, &cur->ip)) cur->has_ip = true;
            } else if (cur && strcmp(key, "netmask") == 0) {
                uint32_t pm;
                if (net_parse_ip(val, &pm)) cur->mask = pm;
                else { int b = small_int(val); if (b > 0 && b <= 32) cur->mask = net_mask_bits(b); }
            }
        }
        p = nl < end ? nl + 1 : nl;
    }
    return nif;
}

/* Attach this machine to the site and make its node agree with its disk.
 * Returns the node id, or 0 if it could not be plugged in at all. */
static int attach(Machine *m)
{
    Net *n = homenet(m);
    uint32_t want = cfg_hash(m);
    /* A node id only means anything in the world it was allocated in -- but
     * a pinned node was allocated in a world this file did not build, and
     * its generation counter has nothing to say about it. */
    if (!m->net_home && m->net_gen != SITE_GEN) { m->net_node = 0; m->net_cfg = 0; }
    if (m->net_node && m->net_cfg == want) return m->net_node;

    if (!m->net_node && !m->net_home) {
        /* Somebody else's old slot, if there is one: same node, same card,
         * same port, and therefore the same lease. */
        if (nfreed) {
            nfreed--;
            m->net_node = freed[nfreed].node;
            m->net_gen = SITE_GEN;
            m->net_port = freed[nfreed].port;
            net_cable(n, m->net_node, 0, SW, m->net_port, 14, CAB_CAT5E);
        }
    }
    if (!m->net_node && !m->net_home) {
        if (next_port >= 24) {
            /* The switch is full. In a building that is a real limit and the
             * player buys another switch; in the harness, which works a
             * hundred tickets one after another and frees each machine, it
             * only means the world has accumulated boxes that are gone. Lay
             * a new one. Anything still holding a node id is told by the
             * generation that its id has expired. */
            netsite_reset();
            n = site();
        }
        char name[NET_NAME_MAX];
        snprintf(name, sizeof name, "box-%s", m->id[0] ? m->id : "0");
        int id = net_add_host(n, name);
        if (id < 0) return 0;
        m->net_port = next_port++;
        net_cable(n, id, 0, SW, m->net_port, 14, CAB_CAT5E);
        m->net_node = id;
        m->net_gen = SITE_GEN;
    }
    int node = m->net_node;
    m->net_cfg = want;

    /* WHAT THE DISK SAYS THIS BOX'S CARDS ARE, read before anything is torn
     * down, because reading it is what creates the subinterfaces a stanza
     * names and the tear-down below has to know which ones to keep. */
    IfCfg want_if[NET_IF_MAX];
    int nwant = read_ifaces(m, n, node, want_if, NET_IF_MAX);

    /* Start from nothing every time. A reconfiguration is a reconfiguration:
     * leaving the old address on would make a deleted line invisible -- and
     * a subinterface the file no longer names is a deleted line of exactly
     * the same kind, so it goes too. A socket is a hole in the box and
     * cannot go; it loses its address instead. */
    for (int i = 0; i < NET_IF_MAX; i++) {
        if (!net_if_exists(n, node, i)) continue;
        bool keep = false;
        for (int k = 0; k < nwant; k++) if (want_if[k].ifx == i) { keep = true; break; }
        if (!keep && i >= net_node_ports(n, node)) net_if_del(n, node, i);
        else net_if_addr(n, node, i, 0, 0);
    }
    net_route_clear(n, node);
    net_set_resolver(n, node, 0);
    net_arp_flush(n, node);
    net_close_all(n, node);
    /* And the pools go, because the file below is what puts them back. A
     * reconfiguration is a reconfiguration: leaving a pool running would
     * make a line somebody deleted invisible, which is the same fault as
     * leaving the old address on. */
    net_dhcpd_stop(n, node);
    /* And the zone, for the same reason: the file below is what puts the
     * names back, so a record somebody deleted disappears instead of living
     * on in a daemon nothing restarted. */
    net_dnsd_stop(n, node);

    /* NO NETWORK DAEMON, NO NETWORK. netd is what applies the config; if it
     * refused to start -- because the file is missing, or names an interface
     * udev did not create -- then nothing applied it, and the interface is
     * down. That is one real mechanism producing a whole family of symptoms
     * further up, and none of them are written down anywhere. */
    if (!netd_up(m)) {
        for (int i = 0; i < NET_IF_MAX; i++)
            if (net_if_exists(n, node, i)) net_if_up(n, node, i, false);
        return node;
    }
    for (int k = 0; k < nwant; k++) net_if_up(n, node, want_if[k].ifx, true);
    net_if_up(n, node, 0, true);
    /* The listeners come up whether or not addressing succeeds: a daemon
     * with a socket open on a machine that never got an address is a real
     * and quite confusing state, and netstat should show it. */
    bind_services(n, m, node);

    /* EVERY CARD THE FILE NAMES, not the first address in it. A floor server
     * doing three vlans' DHCP has three addresses and no reason to have one
     * on eth0 at all. */
    bool addressed = false;
    for (int k = 0; k < nwant; k++) {
        IfCfg *c = &want_if[k];
        if (c->dhcp) {
            /* Really ask. Really wait. Really fail if nothing answers. */
            if (net_dhcp_client(n, node, c->ifx)) addressed = true;
        } else if (c->has_ip) {
            net_if_addr(n, node, c->ifx, c->ip, c->mask);
            addressed = true;
        }
    }
    if (!addressed) return node;             /* configured with no address */

    char gw[64] = "", ns[64] = "";
    if (cfg_field(m, "/etc/net/interfaces", "gateway", gw, sizeof gw)) {
        uint32_t g;
        if (net_parse_ip(gw, &g)) net_set_gateway(n, node, g);
    }
    /* The resolver is a separate file and a separate mistake. */
    if (cfg_field(m, "/etc/resolv.conf", "nameserver", ns, sizeof ns)) {
        uint32_t s;
        if (net_parse_ip(ns, &s)) net_set_resolver(n, node, s);
    }
    /* And what it serves, which is now on the disk beside what it is. */
    start_services(n, m, node);
    return node;
}

/* MAKE THE WIRE AGREE WITH THE MACHINE, right now.
 *
 * Every other caller of attach() is a syscall from inside the box, which is
 * the honest order: the machine asks, and the network answers as it is. But
 * a machine that has just been switched on has not made a syscall yet, and
 * something has to be the moment its card gets configured. This is that
 * moment, and it configures nothing when netd is not running -- so a box
 * whose boot failed is on no network at all, which is the point. */
void netsite_apply(Machine *m)
{
    if (m) (void)attach(m);
}

/* THE NODE WAS EMPTIED WHILE THE MACHINE WAS NOT LOOKING.
 *
 * attach() skips its work when the files it watches have not changed since
 * it last ran, which is right for a syscall and wrong the moment something
 * OUTSIDE this file clears the node -- and switching a box off does exactly
 * that (site_power -> power_down, which is what a power cut IS). The disk is
 * unchanged, so the hash still matched, so a server switched back on after a
 * mains failure applied NOTHING: it sat there with `cat /etc/net/interfaces`
 * naming an address its own kernel did not have, which is the founding rule
 * broken on the worst morning of a run. Whoever empties the node says so
 * here, and the next attach re-reads the disk. */
void netsite_stale(Machine *m)
{
    if (m) m->net_cfg = 0;
}

/* ------------------------------------------------------------ the syscalls */
/* Resolve a name by sending a query and waiting for a packet. */
bool netsite_dns(Machine *m, const char *name, char *out, size_t cap)
{
    int node = attach(m);
    if (!node) return false;
    uint32_t ip = 0;
    if (!net_resolve(homenet(m), node, name, &ip)) return false;
    net_fmt_ip(ip, out, cap);
    return true;
}

/* Fetch a page by opening a connection to an address and speaking HTTP. */
bool netsite_http(Machine *m, const char *ipstr, const char *path, Buf *out)
{
    int node = attach(m);
    if (!node) return false;
    uint32_t ip = 0;
    if (!net_parse_ip(ipstr, &ip)) return false;
    int status = net_http_get(homenet(m), node, ip, 80, path, out);
    return status == 200;
}

/* Everything a program inside the machine can be shown about the network. */
void netsite_info(Machine *m, int op, Buf *out)
{
    int node = attach(m);
    Net *n = homenet(m);
    if (!node) { buf_puts(out, "no network interface\n"); return; }
    switch (op) {
    case NETINFO_IFACE:  net_dump_ifaces(n, node, out); break;
    case NETINFO_ROUTE:  net_dump_routes(n, node, out); break;
    case NETINFO_ARP:    net_dump_arp(n, node, out);    break;
    case NETINFO_SOCK:   net_dump_sockets(n, node, out); break;
    case NETINFO_TRACE:  net_dump_trace(n, out);        break;
    case NETINFO_PORT:   net_dump_ports(n, node, out);  break;
    case NETINFO_FW:     net_dump_fw(n, node, out);     break;
    case NETINFO_PCAP:   net_dump_pcap(n, node, out);   break;
    case NETINFO_VOICE:  net_dump_voice_log(n, node, out); break;
    case NETINFO_VOICENOW: net_dump_voice(n, node, out); break;
    default: break;
    }
}

/* Traceroute, from inside the machine, over the real stack.
 *
 * net_traceroute() is the thing itself: a probe per TTL, and whatever ICMP
 * comes back read off the host's own error state. All this does is put the
 * hops into text, one per line, and a hop that answered nothing is a `*`
 * rather than a guess. The last line is the destination only when the
 * destination really replied. */
void netsite_traceroute(Machine *m, const char *dst, Buf *out)
{
    int node = attach(m);
    if (!node) { buf_puts(out, "ifdown\n"); return; }
    uint32_t ip = 0;
    if (!net_parse_ip(dst, &ip)) { buf_puts(out, "badaddr\n"); return; }
    uint32_t hops[16];
    int nh = net_traceroute(homenet(m), node, ip, hops, 12);
    if (nh <= 0) { buf_puts(out, "noroute\n"); return; }
    for (int i = 0; i < nh; i++) {
        char a[20];
        if (hops[i]) net_fmt_ip(hops[i], a, sizeof a);
        else         snprintf(a, sizeof a, "*");
        buf_printf(out, "%d %s\n", i + 1, a);
    }
}

/* Forget one neighbour: `arp -d`. */
int netsite_arp_del(Machine *m, const char *addr)
{
    int node = attach(m);
    if (!node) return -1;
    uint32_t ip = 0;
    if (!net_parse_ip(addr, &ip)) return -1;
    return net_arp_del(homenet(m), node, ip) ? 0 : -1;
}

/* Ping, from inside the machine, over the real stack. */
int netsite_ping(Machine *m, const char *dst, int *rtt)
{
    int node = attach(m);
    if (!node) return PING_IF_DOWN;
    uint32_t ip = 0;
    if (!net_parse_ip(dst, &ip)) return PING_NO_ROUTE;
    return (int)net_ping(homenet(m), node, ip, rtt);
}
const char *netsite_ping_text(int r) { return net_ping_text((PingResult)r); }

/* Install a firewall rule that really drops packets. Called by nft(8) once
 * it has parsed the ruleset off the disk, so the file is the source of
 * truth and the running filter is what the file says. */
void netsite_fw_clear(Machine *m)
{
    int node = attach(m);
    if (node) net_fw_clear(homenet(m), node);
}
void netsite_fw_add(Machine *m, int chain, int proto, int dport, int drop)
{
    int node = attach(m);
    if (!node) return;
    net_fw_add(homenet(m), node, (FwChain)chain, proto, (uint16_t)dport, 0, 0,
               drop ? FW_DROP : FW_ACCEPT);
}

/* Turn the packet capture on and off. It is off by default because a ring
 * of five hundred lines is five hundred lines of memory nobody asked for. */
void netsite_trace(Machine *m, int on)
{
    (void)attach(m);
    net_trace(homenet(m), on != 0);
    if (on) net_trace_clear(homenet(m));
}

/* And the frame capture, which is the other ring: one line per frame at this
 * machine's card, which is what tcpdump(8) reads. Turning it on clears it,
 * so what a capture holds is what has happened since somebody asked. */
void netsite_pcap(Machine *m, int on)
{
    (void)attach(m);
    net_pcap(homenet(m), on != 0);
    if (on) net_pcap_clear(homenet(m));
}

/* Forget the world. Called when the harness starts a fresh ticket, so one
 * run of the solver cannot leave leases and cache entries lying about for
 * the next -- which would make the second ticket depend on the first. */
/* This machine is going away. Give the wire back. */
void netsite_detach(Machine *m)
{
    if (!m) return;
    /* A PINNED NODE IS NOT OURS TO GIVE BACK. It is a switch port in
     * somebody's building with a cable in it; the box being freed does not
     * unplug it, and handing it to the free list would let the next
     * break-fix machine be issued a node in a network it has never seen. */
    if (m->net_home) {
        /* And it stops being the box behind that node: a web server whose
         * machine has been carried out serves nothing, rather than serving
         * the last thing that stood there. */
        if (net_host_owner((Net *)m->net_home, m->net_node) == m)
            net_host_set_owner((Net *)m->net_home, m->net_node, NULL);
        m->net_home = NULL; m->net_node = 0; m->net_cfg = 0; return;
    }
    if (!m->net_node || !SITE || m->net_gen != SITE_GEN) {
        m->net_node = 0; m->net_cfg = 0;
        return;
    }
    net_release_host(SITE, m->net_node);
    if (nfreed < FREED_MAX) {
        freed[nfreed].node = m->net_node;
        freed[nfreed].port = m->net_port;
        nfreed++;
    }
    m->net_node = 0;
    m->net_cfg = 0;
}

void netsite_reset(void)
{
    if (SITE) net_free(SITE);
    SITE = NULL;
    SW = GW = NS = WEB = -1;
    next_port = 0;
    nfreed = 0;
    SITE_GEN++;
    if (!SITE_GEN) SITE_GEN = 1;
}

/* ======================================================================= *
 * THE INTERNET, OUT PAST THE HANDOFF
 *
 * Until D42 the whole web was one box: site_new() gave the ISP's handoff
 * every address in net_sites.c and an httpd, so `links halbert.co.uk` from
 * a machine in the building opened a connection to the wall socket three
 * metres away and got a page out of a C array. It worked, and every property
 * of the network the rest of this program models -- distance, a rate-limited
 * circuit, routing, a resolver that can be down on its own -- stopped at the
 * demarcation point.
 *
 * Now the far side is built:
 *
 *   uplink      198.51.100.1/30   the handoff, on the MDF wall. One customer
 *               198.51.100.5/30   socket; wan0 is the way out and no player
 *                                 can see it or cable into it. It answers
 *                                 DNS at .1 as it always has, but its zone
 *                                 is EMPTY and it forwards -- so the name
 *                                 service the tower is pointed at is the
 *                                 ISP's resolver, one hop further away, and
 *                                 losing that resolver is not the same fault
 *                                 as losing the circuit.
 *   isp-core    198.51.100.6/30   the ISP's router. Four cards: the
 *               198.51.100.9/29   backhaul, its own service segment, and the
 *               10.0.2.1/24       two hosting segments.
 *               10.0.3.1/24
 *   isp-ns      198.51.100.10/29  A REAL MACHINE. NomnixOS, booted, netd
 *                                 reading its own /etc/net/interfaces, and
 *                                 the whole zone in /etc/net/services on its
 *                                 own disk.
 *   halbert     10.0.2.73/24      A REAL MACHINE. The supplier. Its httpd
 *                                 serves /srv/www off its own filesystem,
 *                                 and the catalogue in there is PRINTED off
 *                                 core/site.c's KIT[] when the disk is laid
 *                                 down -- so it cannot drift from what the
 *                                 counter charges, and it is a file that one
 *                                 day somebody can edit.
 *   www         10.0.2.20/24      Not a machine: one box holding the
 *               10.0.3.10/24      addresses of the web's other twenty-odd
 *                                 names, served out of net_sites.c's table
 *                                 the way they always were. See the note on
 *                                 cost below.
 *
 * WHAT A REAL BOX COSTS, MEASURED. A booted machine is 13.0 MB (rss over 32
 * of them, 507952 kB - 294156 kB / 16) and 92 ms to install and boot. The
 * table has 30 names on 23 addresses; thirty machines would be 390 MB and
 * 2.8 s on every tower the harness builds, and --sitecheck builds 71 of them.
 * So the two boxes that a player can have a relationship with are real and
 * the rest of the web is a hosting box, which is what the rest of the web
 * is. The two that are real are the two that can BREAK in ways that mean
 * something: the shop is what the money goes through, and the resolver is
 * the difference between "the internet is gone" and "the internet is fine
 * and you cannot look anything up".
 *
 * AND IT IS BUILT LAZILY, on the world's first tick (net_step). A Site that
 * is generated and never played -- most of --building, and every seed the
 * scenario generator throws away -- pays nothing at all.
 * ======================================================================= */

#define ISP_HAND_WAN   198, 51, 100, 5
#define ISP_CORE_WAN   198, 51, 100, 6
#define ISP_CORE_SVC   198, 51, 100, 9
#define ISP_NS_IP      198, 51, 100, 10
#define ISP_SVC_BITS   29
#define WWW_A_GW       10, 0, 2, 1
#define WWW_A_IP       10, 0, 2, 20
#define WWW_B_GW       10, 0, 3, 1
#define WWW_B_IP       10, 0, 3, 10
#define SHOP_IP        10, 0, 2, 73
/* THE HOSTING BOX'S PUBLIC ADDRESS, and the reason it needs one.
 *
 * The whole of this web is addressed in 10.0.2.0/24 and 10.0.3.0/24, which
 * is private space, which is where the player's own tower is built. A tower
 * on 10.0.0.0/16 -- the shape --loadcheck recommends and every gate builds
 * -- contains 10.0.2.20 in its OWN connected subnet, so a desk asking for it
 * ARPs on its own floor and never sends a packet at all. That is a real
 * collision and it is older than this record; what is new is that a
 * tenancy's whole day now goes to this box, so it could not be left alone.
 *
 * The honest repair is to move the web into documentation space, and that is
 * net_sites.c's twenty-three addresses, image.c's /etc/hosts, every page that
 * quotes one, and the break-fix game's own site network. It was not done
 * today; see D42. This is the hosting box's public address, on the same card,
 * and it is what a day's traffic is aimed at. */
#define WWW_PUBLIC     203, 0, 113, 20
#define SHOP_HOST      "halbert.co.uk"

/* Every page of one host, and its body. net_sites.c. */
int  net_site_page(int i, const char **host, const char **ip, const char **path);
bool net_fetch(const char *ip, const char *path, Buf *out);

/* ------------------------------------------------------ a box's own disk */
/* WHAT A WEB SERVER SERVES.
 *
 * The daemon in netstack calls this instead of net_fetch. A node with a
 * machine behind it serves that machine's DocumentRoot -- the directory its
 * own /etc/httpd/httpd.conf names, which httpd(8) refused to start without
 * -- and a node with nothing behind it is served out of the table by address
 * as it always was.
 *
 * That is the line this whole record is about. A price list that lives in a
 * C string can never be changed by anything inside the game; a price list
 * that is a file on a disk that a real daemon reads is a thing a player can
 * one day reach. Nothing here lets them yet. Everything here is what they
 * would have to get past. */
bool netsite_www(Net *n, int node, const char *selfip, const char *path, Buf *out)
{
    Machine *m = (Machine *)net_host_owner(n, node);
    if (!m) return net_fetch(selfip, path, out);

    char root[160];
    if (!cfg_field(m, "/etc/httpd/httpd.conf", "DocumentRoot", root, sizeof root))
        return false;                      /* configured with no document root */
    if (!path || !*path) path = "/";
    /* A REQUEST IS NOT A PATH UNTIL IT HAS BEEN CHECKED. `GET /../etc/shadow`
     * is the oldest trick there is and it would work perfectly against a vfs
     * that resolves `..` honestly. The document root is a root. */
    for (const char *p = path; *p; p++)
        if (p[0] == '.' && p[1] == '.') return false;

    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    char full[NOM_PATH_MAX];
    static const char *IDX[] = { "index", "index.html", NULL };
    VNode *f = NULL;
    if (strcmp(path, "/") == 0) {
        for (int i = 0; IDX[i] && !f; i++) {
            snprintf(full, sizeof full, "%s/%s", root, IDX[i]);
            f = vfs_resolve(fs, full, NULL);
            if (f && f->kind != VN_FILE) f = NULL;
        }
    } else {
        snprintf(full, sizeof full, "%s%s", root, path);
        f = vfs_resolve(fs, full, NULL);
        if (f && f->kind != VN_FILE) f = NULL;
    }
    if (!f) return false;                             /* a real 404 */
    buf_put(out, f->data.p, f->data.len);
    return true;
}

/* ------------------------------------------------------ the boxes out there */
/* A pristine machine, then the four files that make it this box. Everything
 * a NomnixOS machine needs to be on a network is on its own disk and is read
 * off its own disk by netd, so making one of the ISP's servers is writing
 * files and booting it -- not a second configuration path. */
static void isp_write(Machine *m, const char *path, const char *body)
{
    vfs_mkfile(&m->disk, path, body);
}

/* THE FILTER IN FRONT OF THE SHOP.
 *
 * /etc/nftables.conf is read at boot by nft(8) running INSIDE the machine,
 * which calls netsite_fw_add through a syscall -- the same path a player's
 * own `nft` takes on their own server. So this is not a description of a
 * filter, it is one, and it is what will have to be got past on the day
 * somebody tries to order a switch for nothing.
 *
 * Port 80 is open because it is a shop. ICMP is open because the supplier's
 * own front page says *"it is on 10.0.2.73 and it answers a ping"* and
 * --mancheck runs that ping against this machine: a page in this game does
 * not get to be wrong about the box it is served from.
 *
 * SSH IS DROPPED BY A RULE AND NOT BY THE POLICY, and the difference is the
 * whole reason this comment is long. netstack's `policy drop` disposes of
 * what no rule named AND nothing is listening for -- it is not a way to shut
 * a port a service has open, and net_dump_fw says so in the paragraph it
 * prints. The shop is running sshd. So a chain policy alone left port 22
 * open on the supplier's box, and the first version of this ruleset was a
 * filter that looked closed and was not. A named drop is what closes it, and
 * a named drop is also what somebody will one day have to get past. */
static const char *SHOP_NFT =
    "# halbert trade supply -- edge filter. ssh is NOT open from outside.\n"
    "table inet filter {\n"
    "  chain input {\n"
    "    type filter hook input priority 0; policy drop;\n"
    "    tcp dport { 22 } drop\n"
    "    icmp accept\n"
    "    tcp dport { 80 } accept\n"
    "  }\n}\n";

/* The resolver answers questions and lets people find out it is there. */
static const char *NS_NFT =
    "# the ISP's resolver. Queries and echoes, and nothing else.\n"
    "table inet filter {\n"
    "  chain input {\n"
    "    type filter hook input priority 0; policy drop;\n"
    "    tcp dport { 22, 80 } drop\n"
    "    icmp accept\n"
    "    udp dport { 53 } accept\n"
    "  }\n}\n";

/* THE SHOP'S DOCUMENT ROOT, PRINTED RATHER THAN TYPED.
 *
 * Every page halbert.co.uk serves becomes a real file under its DocumentRoot,
 * and the body is whatever net_sites.c renders for that page AT THE MOMENT
 * THE DISK IS LAID DOWN -- which for the two generated pages means it is
 * printed off core/site.c's KIT[] through site_kind_*(), exactly as it was
 * when it was printed at fetch time. The guarantee D40 bought is unchanged:
 * a price on that page is the catalogue's price because it was read from the
 * catalogue, and there is still no second copy of it anywhere.
 *
 * What HAS changed is that it is now a file, so it can be read with `cat`,
 * hashed, edited, and one day disagreed with. That is the point. */
/* Install a pristine NomnixOS and write the handful of files that make it
 * this particular box. Everything a machine needs to be on a network is on
 * its own disk and is read off its own disk by netd, so making one of the
 * ISP's servers is writing files -- not a second configuration path that
 * could disagree with the first. */
static Machine *isp_install(uint64_t seed, const char *addr, const char *bits,
                            const char *gw, const char *ns, const char *nft)
{
    Machine *m = (Machine *)nom_alloc(sizeof *m);
    machine_install(m, seed);
    char buf[256];
    snprintf(buf, sizeof buf,
             "# the ISP's own kit. Not yours, and not on your network.\n"
             "iface eth0\n  address %s\n  netmask %s\n  gateway %s\n",
             addr, bits, gw);
    isp_write(m, "/etc/net/interfaces", buf);
    snprintf(buf, sizeof buf, "nameserver %s\n", ns);
    isp_write(m, "/etc/resolv.conf", buf);
    if (nft) isp_write(m, "/etc/nftables.conf", nft);
    return m;
}

/* PINNED BEFORE IT IS BOOTED, so that the filter nft(8) loads at boot, the
 * address netd reads and the sockets its services open all land on the node
 * it is really going to live on. Booting first and pinning afterwards put
 * the ruleset and the listeners on a node in another world, and the box came
 * up filtering nothing on a network it was not on. */
static void isp_start(Net *n, int node, Machine *m)
{
    netsite_pin(m, n, node);
    net_host_set_owner(n, node, m);
    machine_boot(m);
    netsite_apply(m);
}

static void shop_disk(Machine *m)
{
    const char *host, *ip, *path;
    for (int i = 0; net_site_page(i, &host, &ip, &path); i++) {
        if (strcmp(host, SHOP_HOST) != 0) continue;
        Buf body;
        buf_init(&body);
        if (net_fetch(ip, path, &body) && body.len) {
            char full[NOM_PATH_MAX];
            if (strcmp(path, "/") == 0) snprintf(full, sizeof full, "/srv/www/index");
            else                        snprintf(full, sizeof full, "/srv/www%s", path);
            /* buf is not NUL-terminated by contract; vfs_mkfile takes a
             * string, so terminate a copy. */
            char *z = (char *)nom_alloc(body.len + 1);
            memcpy(z, body.p, body.len);
            z[body.len] = 0;
            vfs_mkfile(&m->disk, full, z);
            nom_free(z);
        }
        buf_free(&body);
    }
    isp_write(m, "/etc/httpd/httpd.conf",
              "Listen 80\nDocumentRoot /srv/www\nServerName halbert.co.uk\n");
}

/* ---------------------------------------------------------------- build */
void netsite_isp_build(Net *n, int hand)
{
    if (!n || hand < 0) return;

    /* The handoff stops being the internet. It keeps its address, its
     * circuit and its DNS socket -- everything the tower is configured
     * against -- and gives back the thirty addresses it was pretending to
     * be and the web server it was pretending to run. */
    net_if_alias_clear(n, hand);
    net_dnsd_stop(n, hand);
    net_dnsd(n, hand);
    net_set_resolver(n, hand, net_ip(ISP_NS_IP));

    int wan = net_wan_nic(n, hand);
    if (wan < 0) return;
    net_if_addr(n, hand, wan, net_ip(ISP_HAND_WAN), net_mask_bits(30));

    int core = net_add_host_nics(n, "isp-core", 4);
    int sw   = net_add_switch(n, "isp-sw", 8);
    int www  = net_add_host_nics(n, "www", 2);
    int shop = net_add_host_nics(n, "halbert", 1);
    int ns   = net_add_host_nics(n, "isp-ns", 1);
    if (core < 0 || sw < 0 || www < 0 || shop < 0 || ns < 0) return;

    /* Fibre, because it is a carrier's own plant and because the one thing
     * out here that is allowed to be the bottleneck is the circuit the
     * landlord bought. */
    net_wan_cable(n, hand, wan, core, 0, 400, CAB_FIBRE);
    net_cable(n, core, 1, sw, 0, 5, CAB_FIBRE);
    net_cable(n, www,  0, sw, 1, 5, CAB_CAT6);
    net_cable(n, shop, 0, sw, 2, 5, CAB_CAT6);
    net_cable(n, core, 2, www, 1, 5, CAB_CAT6);
    net_cable(n, core, 3, ns,  0, 5, CAB_CAT6);

    net_if_addr(n, core, 0, net_ip(ISP_CORE_WAN), net_mask_bits(30));
    net_if_addr(n, core, 1, net_ip(WWW_A_GW),     net_mask_bits(24));
    net_if_addr(n, core, 2, net_ip(WWW_B_GW),     net_mask_bits(24));
    net_if_addr(n, core, 3, net_ip(ISP_CORE_SVC), net_mask_bits(ISP_SVC_BITS));
    net_forwarding(n, core, true);
    net_set_gateway(n, core, net_ip(ISP_HAND_WAN));

    /* The way to the web, from the handoff. Longer than the 10.0.0.0/8 that
     * site_new points back down the customer's own circuit, so it wins --
     * which is exactly how a provider's more specific route wins in the real
     * table, and it is arithmetic rather than a special case. */
    net_route_add(n, hand, net_ip(10, 0, 2, 0), net_mask_bits(24),
                  net_ip(ISP_CORE_WAN), -1);
    net_route_add(n, hand, net_ip(10, 0, 3, 0), net_mask_bits(24),
                  net_ip(ISP_CORE_WAN), -1);
    net_route_add(n, hand, net_ip(198, 51, 100, 8), net_mask_bits(ISP_SVC_BITS),
                  net_ip(ISP_CORE_WAN), -1);

    /* THE HOSTING BOX, which is thirty names on one card, which is how
     * virtual hosting has always worked and is the honest shape for a web of
     * jokes nobody administers. The shop is not among them: it is a machine.
     */
    net_if_addr(n, www, 0, net_ip(WWW_A_IP), net_mask_bits(24));
    net_if_addr(n, www, 1, net_ip(WWW_B_IP), net_mask_bits(24));
    net_set_gateway(n, www, net_ip(WWW_A_GW));
    net_if_alias(n, www, net_ip(WWW_PUBLIC));
    net_isp_set_web(n, net_ip(WWW_PUBLIC));
    net_route_add(n, core, net_ip(WWW_PUBLIC), net_mask_bits(32),
                  net_ip(WWW_A_IP), -1);
    net_route_add(n, hand, net_ip(203, 0, 113, 0), net_mask_bits(24),
                  net_ip(ISP_CORE_WAN), -1);
    uint32_t shop_ip = net_ip(SHOP_IP);
    for (int i = 0; ; i++) {
        const char *h = NULL, *ipstr = NULL;
        if (!net_site_hosts(i, &h, &ipstr)) break;
        uint32_t a = 0;
        if (!net_parse_ip(ipstr, &a)) continue;
        if (a == shop_ip) continue;                   /* that one is a box */
        if ((a >> 24) == 127) continue;               /* somebody's loopback joke */
        if (a == net_ip(WWW_A_IP) || a == net_ip(WWW_B_IP)) continue;
        net_if_alias(n, www, a);
    }
    net_httpd(n, www, 80);

    /* ------------------------------------------------------ the real boxes */
    /* The zone, on a disk. netd replays /etc/net/services at every
     * configuration, so the resolver's answers come off its own filesystem
     * and stopping netd on it stops the whole web resolving -- while the
     * shop's address goes on working, which is the diagnosis this record
     * exists for. */
    Machine *mns = isp_install(0x15b0001ull, "198.51.100.10", "29",
                               "198.51.100.9", "198.51.100.10", NS_NFT);
    Buf zone;
    buf_init(&zone);
    buf_puts(&zone, "dnsd\n");
    for (int i = 0; ; i++) {
        const char *h = NULL, *ipstr = NULL;
        if (!net_site_hosts(i, &h, &ipstr)) break;
        buf_printf(&zone, "record %s %s\n", h, ipstr);
    }
    char *z = (char *)nom_alloc(zone.len + 1);
    memcpy(z, zone.p, zone.len);
    z[zone.len] = 0;
    isp_write(mns, "/etc/net/services", z);
    nom_free(z);
    buf_free(&zone);
    isp_start(n, ns, mns);

    Machine *msh = isp_install(0x15b0002ull, "10.0.2.73", "24",
                               "10.0.2.1", "198.51.100.10", SHOP_NFT);
    shop_disk(msh);
    isp_start(n, shop, msh);

    net_isp_own(n, 0, mns);
    net_isp_own(n, 1, msh);
}

/* The world this internet was built in is going away. netstack calls this
 * from net_free, because a harness that plays seventy towers would otherwise
 * leak two booted machines a tower. */
void netsite_net_freed(Net *n)
{
    for (int i = 0; i < 4; i++) {
        Machine *m = (Machine *)net_isp_owned(n, i);
        if (!m) continue;
        net_isp_own(n, i, NULL);
        m->net_home = NULL;         /* the world is going, not the wire */
        m->net_node = 0;
        machine_free(m);
        nom_free(m);
    }
}
