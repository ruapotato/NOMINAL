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
    static const char *WATCH[] = { "/etc/net/interfaces", "/etc/resolv.conf", NULL };
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
    if (kernel_svc_running(m, "httpd"))
        net_tcp_listen(n, node, (uint16_t)svc_port(m, "httpd", "Listen", 80));
    if (kernel_svc_running(m, "postfix"))
        net_tcp_listen(n, node, 25);
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

    /* Start from nothing every time. A reconfiguration is a reconfiguration:
     * leaving the old address on would make a deleted line invisible. */
    net_if_addr(n, node, 0, 0, 0);
    net_route_clear(n, node);
    net_set_resolver(n, node, 0);
    net_arp_flush(n, node);
    net_close_all(n, node);

    /* NO NETWORK DAEMON, NO NETWORK. netd is what applies the config; if it
     * refused to start -- because the file is missing, or names an interface
     * udev did not create -- then nothing applied it, and the interface is
     * down. That is one real mechanism producing a whole family of symptoms
     * further up, and none of them are written down anywhere. */
    if (!netd_up(m)) { net_if_up(n, node, 0, false); return node; }
    net_if_up(n, node, 0, true);
    /* The listeners come up whether or not addressing succeeds: a daemon
     * with a socket open on a machine that never got an address is a real
     * and quite confusing state, and netstat should show it. */
    bind_services(n, m, node);

    char addr[64] = "", gw[64] = "", nm[64] = "", ns[64] = "";
    if (!cfg_field(m, "/etc/net/interfaces", "address", addr, sizeof addr))
        return node;                         /* configured with no address */

    uint32_t ip = 0, mask = net_mask_bits(SITE_MASK);
    if (strcmp(addr, "dhcp") == 0) {
        /* Really ask. Really wait. Really fail if nothing answers. */
        if (!net_dhcp_client(n, node, 0)) return node;
    } else {
        if (!net_parse_ip(addr, &ip)) return node;   /* an address that is not one */
        if (cfg_field(m, "/etc/net/interfaces", "netmask", nm, sizeof nm)) {
            uint32_t pm;
            if (net_parse_ip(nm, &pm)) mask = pm;
            else { int b = small_int(nm); if (b > 0 && b <= 32) mask = net_mask_bits(b); }
        }
        net_if_addr(n, node, 0, ip, mask);
        if (cfg_field(m, "/etc/net/interfaces", "gateway", gw, sizeof gw)) {
            uint32_t g;
            if (net_parse_ip(gw, &g)) net_set_gateway(n, node, g);
        }
    }
    /* The resolver is a separate file and a separate mistake. */
    if (cfg_field(m, "/etc/resolv.conf", "nameserver", ns, sizeof ns)) {
        uint32_t s;
        if (net_parse_ip(ns, &s)) net_set_resolver(n, node, s);
    }
    return node;
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
    default: break;
    }
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
    if (m->net_home) { m->net_home = NULL; m->net_node = 0; m->net_cfg = 0; return; }
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
