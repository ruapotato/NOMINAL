/* netstack.c — frames on a wire, and everything above them. See netstack.h.
 *
 * Read this file downwards. It is built in the order the layers are built in
 * reality, and no function is allowed to call one from a layer above it:
 *
 *   1. bytes, checksums and the trace ring
 *   2. L1: ports, cables, and a queue of frames that are physically in flight
 *   3. L2: ethernet framing, and a switch that LEARNS rather than knows
 *   4. L3: ARP, IP, routing, ICMP
 *   5. L4: UDP, and a TCP with a real handshake and a real teardown
 *   6. the filter, which drops a real packet at a real point in the path
 *   7. DHCP, DNS and HTTP, as protocols on the wire rather than as answers
 *   8. inspection: what the player can see from inside the machine
 *
 * The rule that keeps it honest: nothing here ever asks "is A reachable from
 * B". The only question anyone asks is "what do I do with these bytes".
 */
#include "netstack.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ bytes */
/* Network byte order, done by hand. The host's endianness is not allowed to
 * reach a frame: a trace recorded on one machine has to read identically on
 * the other, and that is a determinism claim we test. */
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* The one's-complement sum every IP protocol uses. It is computed for real,
 * so a packet whose header gets mangled really is discarded by the receiver
 * rather than by a flag saying it was mangled. */
static uint16_t cksum(const uint8_t *p, int len, uint32_t start)
{
    uint32_t sum = start;
    while (len > 1) { sum += get16(p); p += 2; len -= 2; }
    if (len) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)(~sum & 0xffff);
}

/* "Let the routing decide what the source address is." It cannot be 0,
 * because 0.0.0.0 is a source address a real machine really sends from. */
#define IP_SRC_AUTO 0xffffffffu

static const uint8_t MAC_BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t MAC_ZERO[6]  = { 0, 0, 0, 0, 0, 0 };

static bool mac_eq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }
/* A multicast address has the low bit of the first octet set. Broadcast is
 * the all-ones case of it, which is why a switch floods both. */
static bool mac_group(const uint8_t *a) { return (a[0] & 1) != 0; }

uint32_t net_ip(int a, int b, int c, int d)
{
    return ((uint32_t)(a & 255) << 24) | ((uint32_t)(b & 255) << 16) |
           ((uint32_t)(c & 255) << 8)  |  (uint32_t)(d & 255);
}
uint32_t net_mask_bits(int bits)
{
    if (bits <= 0) return 0;
    if (bits >= 32) return 0xffffffffu;
    return (uint32_t)(0xffffffffu << (32 - bits));
}
int net_mask_len(uint32_t mask)
{
    int n = 0;
    for (int i = 31; i >= 0; i--) { if (mask & (1u << i)) n++; else break; }
    return n;
}
bool net_parse_ip(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int part = 0, digits = 0, acc = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            acc = acc * 10 + (*p - '0');
            if (++digits > 3 || acc > 255) return false;
        } else if (*p == '.' || *p == 0) {
            if (!digits) return false;
            v = (v << 8) | (uint32_t)acc;
            acc = 0; digits = 0;
            if (++part == 4) { if (*p) return false; *out = v; return true; }
            if (!*p) return false;
        } else return false;
    }
}
void net_fmt_ip(uint32_t ip, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255,
             (ip >> 8) & 255, ip & 255);
}
void net_fmt_mac(const uint8_t mac[6], char *out, size_t cap)
{
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char *tcp_state_name(TcpState s)
{
    static const char *N[TCP_STATE_COUNT] = {
        "CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD", "ESTABLISHED",
        "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT", "CLOSING",
        "LAST_ACK", "TIME_WAIT"
    };
    return (s >= 0 && s < TCP_STATE_COUNT) ? N[s] : "?";
}

const char *net_ping_text(PingResult r)
{
    switch (r) {
    case PING_OK:            return "reply";
    case PING_TIMEOUT:       return "no answer";
    case PING_NET_UNREACH:   return "destination net unreachable";
    case PING_HOST_UNREACH:  return "destination host unreachable";
    case PING_TTL_EXCEEDED:  return "time exceeded in transit";
    case PING_NO_ROUTE:      return "network is unreachable";
    case PING_IF_DOWN:       return "network interface is down";
    }
    return "?";
}

/* ------------------------------------------------------------- structures */

typedef struct {
    uint8_t  mac[6];
    int      port;         /* which port of this switch it was seen on      */
    int      vlan;
    uint64_t seen;         /* tick it last spoke; entries age out           */
    bool     used;
} FdbEntry;

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint64_t seen;
    bool     used;
    bool     pending;      /* a request is out and nothing has answered yet */
    /* One packet held while we wait for the answer, exactly as a real host
     * does. Without it the FIRST packet to any new neighbour is always lost,
     * which is a bug that looks like intermittent packet loss.
     *
     * THE FRAME ITSELF IS NOT HERE. It used to be, and it made an ARP cache
     * entry fifteen hundred bytes wide: sixty-four of them on each of four
     * hundred hosts is forty megabytes of the world spent on packets that
     * are almost never held. At any instant a handful of neighbours anywhere
     * are unresolved, so the held frames are a pool for the world and this
     * is an index into it, -1 when there is nothing waiting. */
    int      hold;
    int      holdlen;
    int      holdif;
} ArpEntry;

/* Frames waiting on an ARP answer, anywhere in the world. */
#define NET_HOLD_MAX  96

typedef struct {
    uint32_t dst, mask, gw;
    int      ifx;
    bool     used;
} Route;

typedef struct {
    uint8_t  chain, proto, action;
    uint16_t dport;
    uint32_t srcnet, srcmask;
    uint64_t hits;
    bool     used;
} FwRule;

typedef struct {
    uint8_t  mac[6];
    uint32_t ip, mask;
    int      port;         /* global port id, -1 if this NIC is not wired   */
    int      vlan;         /* non-zero: this NIC tags its own frames        */
    bool     up;           /* the admin state of the interface, not the port */
    bool     used;
    uint64_t rx_pkt, tx_pkt, rx_drop;
} Iface;

typedef struct {
    uint8_t  mac[6];
    uint32_t ip;
    uint64_t expires;
    bool     used;
} Lease;

typedef struct {
    char     name[48];
    uint32_t ip;
    bool     used;
} Record;

/* A host. Kept deliberately flat and fixed: this is the structure a tower
 * with three hundred machines pays for, once per machine. */
typedef struct {
    Iface    ifc[NET_IF_MAX];
    Route    rt[NET_ROUTE_MAX];
    ArpEntry arp[NET_ARP_MAX];
    FwRule   fw[NET_FW_MAX];
    bool     forwarding;
    uint16_t next_eph;     /* deterministic ephemeral port allocation       */
    /* Services this host runs. A service is a socket plus a little state;
     * the state is here rather than in the socket because a daemon outlives
     * any one connection. */
    bool     dhcpd;
    uint32_t pool_first, pool_mask, pool_gw, pool_dns;
    int      pool_count;
    Lease    lease[NET_LEASE_MAX];
    bool     dnsd;
    Record   zone[NET_ZONE_MAX];
    uint32_t resolver;
    bool     httpd;
    uint16_t http_port;
    /* What the last ICMP error about our traffic said. This is how ping
     * distinguishes "a router refused" from "nothing came back", and it is
     * set by the ICMP input path and by nothing else. */
    uint8_t  icmp_err_type, icmp_err_code;
    uint32_t icmp_err_from;
    uint64_t icmp_err_at;
} Host;

typedef struct {
    FdbEntry fdb[NET_FDB_MAX];
    bool     stp;          /* spanning tree: off by default, as cheap
                            * unmanaged switches genuinely are            */
    uint64_t flooded;
} Switch;

typedef struct {
    int      node;
    int      index;        /* which port of that node                       */
    int      cable;        /* -1 if nothing is plugged in                   */
    bool     admin_up;
    Duplex   duplex;
    PortMode mode;
    int      vlan;         /* access: the vlan; trunk: the native vlan      */
    uint32_t allow;        /* trunk: bitmask of vlans 1..32 permitted       */
    uint64_t tx, rx, drops;
    /* SERIALISATION. `busy_us` is the absolute microsecond at which the last
     * frame this port accepted will have finished clocking out. Anything
     * offered before then waits behind it, and that wait is latency the
     * player can measure with ping. `qdrops` is what the buffer would not
     * hold. `busy_total` is time on the wire, which is utilisation. */
    uint64_t busy_us, busy_total, qdrops, qpeak_us;
    int      rate_mb;      /* forced circuit rate; 0 = whatever the cable is */
    bool     used;
    /* Spanning tree put this port in blocking. It carries no data and it
     * still shows a link light, which is precisely why a blocked port is
     * confusing to look at and worth being able to look at. */
    bool     blocked;
} Port;

typedef struct {
    int  a, b;             /* global port ids                               */
    int  metres;
    CableKind kind;
    bool used;
} Cable;

typedef struct {
    NodeKind kind;
    char     name[NET_NAME_MAX];
    int      port0, nports;   /* a contiguous run in the global port pool   */
    int      sub;             /* index into hosts[] or switches[]           */
    bool     used;
} Node;

typedef struct {
    bool     used;
    int      node;
    uint8_t  proto;           /* IP_PROTO_TCP or IP_PROTO_UDP              */
    uint16_t lport, rport;
    uint32_t laddr, raddr;
    TcpState state;
    uint32_t snd_nxt, snd_una, snd_isn;
    uint32_t rcv_nxt;
    uint16_t rwnd;            /* what the peer told us it can take         */
    uint8_t  rx[NET_RXBUF];
    int      rxlen;
    uint8_t  tx[NET_TXBUF];
    int      txlen;           /* bytes queued, not yet acknowledged        */
    int      txsent;          /* of those, how many are on the wire        */
    bool     fin_queued;      /* close() was called; send FIN after data   */
    bool     fin_sent;
    uint64_t last_tx;         /* for the retransmission timer              */
    uint64_t timer;           /* TIME_WAIT / connection timeout            */
    int      listener;        /* a child socket remembers its parent       */
    int      accepted;        /* a parent hands the child over once        */
    /* UDP keeps one datagram, which is all any of our protocols needs at a
     * time and keeps a socket affordable. */
    uint32_t dgram_src; uint16_t dgram_sport; int dgram_len;
    /* Which interface it arrived on. A server answering a client that has no
     * address yet cannot route the reply -- it has to put it back out of the
     * hole it came in by, which is the whole reason DHCP works at all. */
    int      dgram_if;
    uint8_t  dgram[512];
    /* Set when the daemon that owns this socket is the world's, not a
     * player's -- HTTP, DNS and DHCP servers are driven from net_step. */
    uint8_t  service;
    /* BYTES STILL TO SEND. A page of a few hundred characters fits in one
     * write; a file of two megabytes does not, and the difference is the
     * whole reason capacity is felt at all. The daemon pushes what the send
     * buffer will take on every poll and comes back for the rest, which is
     * what a real server does and what makes a transfer take TIME. */
    int      svc_left;
} Sock;
#define SVC_NONE  0
#define SVC_HTTPD 1
#define SVC_DNSD  2
#define SVC_DHCPD 3

typedef struct {
    int      inport;       /* the port it is arriving ON                    */
    uint32_t due;          /* tick it lands                                 */
    uint16_t len;
    uint32_t seq;          /* enqueue order: ties in `due` break by this    */
    int      next;         /* the next frame due in the same millisecond    */
    bool     used;
    uint8_t  data[NET_FRAME_MAX];
} InFlight;

/* WHEN A FRAME LANDS, INDEXED BY THE MILLISECOND IT LANDS IN. The queue used
 * to be swept end to end on every delivery looking for the oldest frame that
 * was due, which is fine for the dozen frames a ping makes and quadratic for
 * the half million a tenanted floor makes in its busy period. A frame's
 * delay is bounded and small -- one tick of propagation, at most eight more
 * for two kilometres of fibre, and at most four of egress queue, because a
 * port with more than its buffer behind it drops instead of queueing -- so
 * "which millisecond" is a ring of buckets and delivery is a list walk.
 * Order within a millisecond is still enqueue order, so the trace is the
 * trace it always was. */
#define NET_DUE_RING  64

struct Net {
    Rng      rng;
    uint64_t now;
    Node     node[NET_NODES_MAX];
    int      nnode;
    Port     port[NET_PORTS_MAX];
    int      nport;
    Cable    cable[NET_CABLES_MAX];
    int      ncable;
    Host     host[NET_NODES_MAX];
    int      nhost;
    Switch   sw[NET_SWITCH_MAX];
    int      nsw;
    Sock     sock[NET_SOCK_MAX];
    /* Extra addresses a host answers for. Pooled, because one machine in the
     * world has thirty of them and every other machine has none. */
    struct { int node; uint32_t ip; bool used; } alias[NET_ALIAS_MAX];
    struct { uint8_t data[NET_FRAME_MAX]; bool used; } hold[NET_HOLD_MAX];
    InFlight q[NET_QUEUE_MAX];
    int      qfree;                    /* head of the unused list           */
    int      qhead[NET_DUE_RING];      /* frames due in that millisecond    */
    int      qtail[NET_DUE_RING];
    uint32_t qseq;
    uint64_t qdrops;
    /* Frames handled in a sliding window, which is how a storm becomes
     * visible without anyone deciding a loop exists. */
    uint64_t handled, window_start, window_count, load;
    bool     tracing;
    char     trace[NET_TRACE_MAX][NET_TRACE_LINE];
    int      ntrace, tracehead;
    uint8_t  next_mac;     /* deterministic factory MAC allocation          */
};

/* Recomputed whenever the copper changes, because that is when a spanning
 * tree really does reconverge. Declared here because cabling is L1 and the
 * tree is L2, and L1 must be able to say "the topology moved" without
 * knowing what a tree is. */
static void stp_recompute(Net *n);

/* ------------------------------------------------------------------ trace */
/* One line per frame, at the point the frame is really handled. If a player
 * cannot see a frame they cannot diagnose a network, so this is not a debug
 * aid -- it is the packet capture, and it is the same text `nettrace` prints
 * inside the machine. */
static void trace(Net *n, const char *fmt, ...)
{
    if (!n->tracing) return;
    va_list ap;
    char line[NET_TRACE_LINE];
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    int slot = (n->tracehead + n->ntrace) % NET_TRACE_MAX;
    if (n->ntrace == NET_TRACE_MAX) {
        slot = n->tracehead;
        n->tracehead = (n->tracehead + 1) % NET_TRACE_MAX;
    } else n->ntrace++;
    memcpy(n->trace[slot], line, sizeof line);
    n->trace[slot][NET_TRACE_LINE - 1] = 0;
}

void net_trace(Net *n, bool on) { n->tracing = on; }
void net_trace_clear(Net *n)    { n->ntrace = 0; n->tracehead = 0; }
int  net_trace_count(const Net *n) { return n->ntrace; }
const char *net_trace_line(const Net *n, int i)
{
    if (i < 0 || i >= n->ntrace) return "";
    return n->trace[(n->tracehead + i) % NET_TRACE_MAX];
}
void net_dump_trace(const Net *n, Buf *out)
{
    for (int i = 0; i < n->ntrace; i++) {
        buf_puts(out, net_trace_line(n, i));
        buf_putc(out, '\n');
    }
}

/* ------------------------------------------------------------ world setup */
Net *net_new(uint64_t seed)
{
    Net *n = (Net *)nom_alloc(sizeof *n);
    memset(n, 0, sizeof *n);
    rng_seed(&n->rng, seed ^ 0x6e65747374616b31ull);
    n->next_mac = 1;
    for (int i = 0; i < NET_PORTS_MAX; i++) n->port[i].cable = -1;
    for (int i = 0; i < NET_QUEUE_MAX; i++) n->q[i].next = i + 1;
    n->q[NET_QUEUE_MAX - 1].next = -1;
    n->qfree = 0;
    for (int i = 0; i < NET_DUE_RING; i++) n->qhead[i] = n->qtail[i] = -1;
    return n;
}
void net_free(Net *n) { if (n) nom_free(n); }
uint64_t net_now(const Net *n) { return n->now; }

static Host *host_of(Net *n, int node)
{
    if (node < 0 || node >= n->nnode || !n->node[node].used) return NULL;
    if (n->node[node].kind != NODE_HOST) return NULL;
    return &n->host[n->node[node].sub];
}
static const Host *chost_of(const Net *n, int node)
{
    return host_of((Net *)n, node);
}
static Switch *sw_of(Net *n, int node)
{
    if (node < 0 || node >= n->nnode || !n->node[node].used) return NULL;
    if (n->node[node].kind != NODE_SWITCH) return NULL;
    return &n->sw[n->node[node].sub];
}
/* Global port id for (node, index), or -1. */
static int pid_of(const Net *n, int node, int idx)
{
    if (node < 0 || node >= n->nnode || !n->node[node].used) return -1;
    if (idx < 0 || idx >= n->node[node].nports) return -1;
    return n->node[node].port0 + idx;
}

/* Every NIC leaves the factory with an address. It is allocated from a
 * counter rather than at random so that the same tower built from the same
 * seed has the same addresses -- a trace you can compare is worth more than a
 * MAC that looks convincingly vendor-assigned. */
static void factory_mac(Net *n, uint8_t out[6])
{
    uint32_t k = n->next_mac++;
    out[0] = 0x02;                 /* locally administered, as an emulator's
                                    * addresses honestly are               */
    out[1] = 0x4e; out[2] = 0x4f;  /* "NO"                                 */
    out[3] = (uint8_t)(k >> 16);
    out[4] = (uint8_t)(k >> 8);
    out[5] = (uint8_t)k;
}

static int add_node(Net *n, NodeKind k, const char *name, int nports)
{
    if (n->nnode >= NET_NODES_MAX) return -1;
    if (n->nport + nports > NET_PORTS_MAX) return -1;
    int id = n->nnode++;
    Node *nd = &n->node[id];
    memset(nd, 0, sizeof *nd);
    nd->used = true;
    nd->kind = k;
    snprintf(nd->name, sizeof nd->name, "%s", name ? name : "node");
    nd->port0 = n->nport;
    nd->nports = nports;
    for (int i = 0; i < nports; i++) {
        Port *p = &n->port[n->nport + i];
        memset(p, 0, sizeof *p);
        p->used = true;
        p->node = id;
        p->index = i;
        p->cable = -1;
        p->admin_up = true;
        p->duplex = DUPLEX_FULL;
        p->mode = PORT_ACCESS;
        p->vlan = VLAN_DEFAULT;
        p->allow = 0;
    }
    n->nport += nports;
    return id;
}

/* EVERY HOLE IN THE BACK OF THE BOX IS A REAL CARD.
 *
 * It used to be one card on port 0 and three sockets wired to nothing. A
 * frame arriving on port 1 landed on a port that reported `up`, counted a
 * rx, and then found no interface that claimed it -- so it was dropped with
 * nothing anywhere saying so, and a router had one working port. Moving the
 * same cable to port 0 fixed it, which is the exact shape of a bug that
 * makes a competent person think they are the problem.
 *
 * So a host is created with one interface per socket: eth0 on port 0, eth1
 * on port 1, each with its own factory address and no IP until somebody
 * gives it one. Tagged subinterfaces are stacked ON TOP of these, from
 * index nports upwards, by net_if_subif. */
int net_add_host_nics(Net *n, const char *name, int nics)
{
    if (n->nhost >= NET_NODES_MAX) return -1;
    if (nics < 1) nics = 1;
    if (nics > NET_HOST_NICS) nics = NET_HOST_NICS;
    int id = add_node(n, NODE_HOST, name, nics);
    if (id < 0) return -1;
    n->node[id].sub = n->nhost++;
    Host *h = &n->host[n->node[id].sub];
    memset(h, 0, sizeof *h);
    h->next_eph = 49152;
    for (int i = 0; i < NET_IF_MAX; i++) h->ifc[i].port = -1;
    for (int i = 0; i < nics; i++) {
        h->ifc[i].used = true;
        h->ifc[i].up = true;
        h->ifc[i].port = n->node[id].port0 + i;
        factory_mac(n, h->ifc[i].mac);
    }
    return id;
}

int net_add_host(Net *n, const char *name)
{
    return net_add_host_nics(n, name, NET_HOST_NICS);
}

int net_add_switch(Net *n, const char *name, int nports)
{
    if (n->nsw >= NET_SWITCH_MAX) return -1;
    if (nports < 1) nports = 1;
    if (nports > NET_SWPORTS) nports = NET_SWPORTS;
    int id = add_node(n, NODE_SWITCH, name, nports);
    if (id < 0) return -1;
    n->node[id].sub = n->nsw++;
    memset(&n->sw[n->node[id].sub], 0, sizeof(Switch));
    return id;
}

int net_node_count(const Net *n) { return n->nnode; }
const char *net_node_name(const Net *n, int node)
{
    if (node < 0 || node >= n->nnode) return "";
    return n->node[node].name;
}
int net_node_ports(const Net *n, int node)
{
    if (node < 0 || node >= n->nnode) return 0;
    return n->node[node].nports;
}
int net_node_by_name(const Net *n, const char *name)
{
    for (int i = 0; i < n->nnode; i++)
        if (n->node[i].used && strcmp(n->node[i].name, name) == 0) return i;
    return -1;
}

/* ====================================================================== L1
 * The wire. A link exists between two ports or it does not, and everything
 * above this file's midpoint has no way to ask any other question about it.
 */

/* What a cable can carry, and how far. These are the real numbers, because
 * the building generator hands out real distances and a 140 m run of cat6
 * failing is a fault the player built rather than one we authored. */
static int cable_limit_m(CableKind k)
{
    switch (k) {
    case CAB_CAT5E:  return 100;
    case CAB_CAT6:   return 100;
    case CAB_FIBRE:  return 2000;
    case CAB_CAT5:   return 100;
    default:         return 100;
    }
}
static int cable_speed_mb(CableKind k, int metres)
{
    switch (k) {
    case CAB_CAT5E:  return 1000;
    /* Cat 6 does ten gigabit, but only over a short run. Past 55 m it
     * negotiates down to a gigabit -- it does not fail, it gets slower, and
     * "the link is up and the backup takes all night" is a real ticket. */
    case CAB_CAT6:   return metres <= 55 ? 10000 : 1000;
    case CAB_FIBRE:  return 10000;
    /* Cat 5 is a hundred megabit and no amount of shortening the run makes
     * it anything else. It is the cheapest line in the catalogue and it is
     * the one a player regrets. */
    case CAB_CAT5:   return 100;
    default:         return 100;
    }
}

int net_cable(Net *n, int a, int aport, int b, int bport, int metres, CableKind k)
{
    int pa = pid_of(n, a, aport), pb = pid_of(n, b, bport);
    if (pa < 0 || pb < 0 || pa == pb) return -1;
    if (n->port[pa].cable >= 0 || n->port[pb].cable >= 0) return -1;
    if (n->ncable >= NET_CABLES_MAX) return -1;
    int id = -1;
    for (int i = 0; i < NET_CABLES_MAX; i++) if (!n->cable[i].used) { id = i; break; }
    if (id < 0) return -1;
    if (id >= n->ncable) n->ncable = id + 1;
    Cable *c = &n->cable[id];
    c->used = true; c->a = pa; c->b = pb;
    c->metres = metres < 0 ? 0 : metres;
    c->kind = k;
    n->port[pa].cable = id;
    n->port[pb].cable = id;
    stp_recompute(n);
    return id;
}

void net_uncable(Net *n, int cable)
{
    if (cable < 0 || cable >= NET_CABLES_MAX || !n->cable[cable].used) return;
    Cable *c = &n->cable[cable];
    /* Frames already on this wire are gone. They are electricity in a cable
     * somebody just pulled out; nothing delivers them and nothing reports
     * them. That is the whole difference between unplugging and a flag. */
    for (int b = 0; b < NET_DUE_RING; b++) {
        int prev = -1, i = n->qhead[b];
        while (i >= 0) {
            int next = n->q[i].next;
            int ip = n->q[i].inport;
            if (ip == c->a || ip == c->b) {
                if (prev < 0) n->qhead[b] = next; else n->q[prev].next = next;
                if (n->qtail[b] == i) n->qtail[b] = prev;
                n->q[i].used = false;
                n->q[i].next = n->qfree;
                n->qfree = i;
            } else prev = i;
            i = next;
        }
    }
    n->port[c->a].cable = -1;
    n->port[c->b].cable = -1;
    /* The wire is gone, so whatever was still being clocked onto it is gone
     * with it. A port with no cable in it is not busy. */
    n->port[c->a].busy_us = n->port[c->b].busy_us = 0;
    c->used = false;
    stp_recompute(n);
}

void net_port_admin(Net *n, int node, int port, bool up)
{
    int p = pid_of(n, node, port);
    if (p >= 0) n->port[p].admin_up = up;
}
void net_port_set_duplex(Net *n, int node, int port, Duplex d)
{
    int p = pid_of(n, node, port);
    if (p >= 0) n->port[p].duplex = d;
}

/* The state of one port, worked out from the copper. Note the ORDER: admin
 * down beats no cable beats too long, because that is the order in which a
 * person would fix them. */
static PortState port_state(const Net *n, int p)
{
    if (p < 0 || p >= NET_PORTS_MAX || !n->port[p].used) return PORT_NOCABLE;
    if (!n->port[p].admin_up) return PORT_DOWN_ADMIN;
    int cid = n->port[p].cable;
    if (cid < 0 || !n->cable[cid].used) return PORT_NOCABLE;
    const Cable *c = &n->cable[cid];
    if (c->metres > cable_limit_m(c->kind)) return PORT_TOOLONG;
    int other = (c->a == p) ? c->b : c->a;
    /* A link needs BOTH ends. Shutting a switch port down really does put
     * the light out on the machine at the far end of the cable. */
    if (!n->port[other].admin_up) return PORT_NOCABLE;
    return PORT_UP;
}

static int port_rate_mb(const Net *n, int p);

PortState net_port_state(const Net *n, int node, int port)
{
    return port_state(n, pid_of(n, node, port));
}
int net_port_speed(const Net *n, int node, int port)
{
    int p = pid_of(n, node, port);
    if (port_state(n, p) != PORT_UP) return 0;
    /* WHAT THE PORT REALLY DOES, which is the circuit when there is one and
     * the cable otherwise. Printing the cable's speed for a rate-limited
     * handoff was a number that disagreed with every other number about the
     * same port, including the one the frames actually obey. */
    return port_rate_mb(n, p);
}
Duplex net_port_duplex(const Net *n, int node, int port)
{
    int p = pid_of(n, node, port);
    return p < 0 ? DUPLEX_FULL : n->port[p].duplex;
}
uint64_t net_port_tx(const Net *n, int node, int port)
{ int p = pid_of(n, node, port); return p < 0 ? 0 : n->port[p].tx; }
uint64_t net_port_rx(const Net *n, int node, int port)
{ int p = pid_of(n, node, port); return p < 0 ? 0 : n->port[p].rx; }
uint64_t net_port_drops(const Net *n, int node, int port)
{ int p = pid_of(n, node, port); return p < 0 ? 0 : n->port[p].drops; }
uint64_t net_port_qdrops(const Net *n, int node, int port)
{ int p = pid_of(n, node, port); return p < 0 ? 0 : n->port[p].qdrops; }
uint64_t net_port_busy_us(const Net *n, int node, int port)
{ int p = pid_of(n, node, port); return p < 0 ? 0 : n->port[p].busy_total; }
void net_port_busy_reset(Net *n, int node, int port)
{ int p = pid_of(n, node, port); if (p >= 0) n->port[p].busy_total = 0; }
uint64_t net_port_queue_us(const Net *n, int node, int port)
{
    int p = pid_of(n, node, port);
    if (p < 0) return 0;
    uint64_t now_us = n->now * 1000ull;
    return n->port[p].busy_us > now_us ? n->port[p].busy_us - now_us : 0;
}
void net_port_rate(Net *n, int node, int port, int mb)
{
    int p = pid_of(n, node, port);
    if (p >= 0) n->port[p].rate_mb = mb < 0 ? 0 : mb;
}
int net_port_rate_of(const Net *n, int node, int port)
{
    int p = pid_of(n, node, port);
    return p < 0 ? 0 : n->port[p].rate_mb;
}

/* What this port really clocks bits at: the circuit if somebody sold us one,
 * otherwise whatever the copper in it negotiated. */
static int port_rate_mb(const Net *n, int p)
{
    const Port *pt = &n->port[p];
    int cid = pt->cable;
    int cab = (cid >= 0 && n->cable[cid].used)
              ? cable_speed_mb(n->cable[cid].kind, n->cable[cid].metres) : 100;
    if (pt->rate_mb > 0 && pt->rate_mb < cab) return pt->rate_mb;
    return cab;
}

/* Put bytes on the wire. This is the only way anything leaves a node, and it
 * is where a missing cable stops being an abstraction. */
static void port_tx(Net *n, int p, const uint8_t *data, int len)
{
    if (p < 0 || len < 14 || len > NET_FRAME_MAX) return;
    Port *pt = &n->port[p];
    if (port_state(n, p) != PORT_UP) { pt->drops++; return; }
    Cable *c = &n->cable[pt->cable];
    int other = (c->a == p) ? c->b : c->a;

    /* ------------------------------------------------ what the wire costs
     * Bits divided by megabits per second is microseconds, exactly. If the
     * port is still clocking out the frame before this one, this frame waits
     * behind it -- and if the wait is already deeper than the egress buffer
     * will hold, the port drops it, here, on the port, into the counter
     * `netstat -P` prints.
     *
     * This is the only place in the program where congestion exists. There
     * is no load number kept beside the netstack and consulted: a link that
     * is oversubscribed is a link whose busy_us has run ahead of the clock,
     * and everything the player can see -- latency in ping, timeouts in get,
     * retransmissions in TCP, drops on a port -- is that one fact arriving
     * through the layers that really carry it. */
    int mb = port_rate_mb(n, p);
    uint64_t now_us = n->now * 1000ull;
    uint64_t serial_us = ((uint64_t)len * 8 + (uint64_t)mb - 1) / (uint64_t)mb;
    uint64_t start = pt->busy_us > now_us ? pt->busy_us : now_us;
    uint64_t wait  = start - now_us;
    uint64_t buf_us = ((uint64_t)NET_PORT_BUFFER * 8 + (uint64_t)mb - 1) / (uint64_t)mb;
    if (wait > buf_us) { pt->drops++; pt->qdrops++; return; }

    /* Find a slot. A full queue is a saturated network, and a saturated
     * network drops -- which is exactly what a broadcast storm looks like
     * from inside, and it is why we do not need a storm detector. */
    int slot = n->qfree;
    if (slot < 0) { n->qdrops++; pt->drops++; return; }
    n->qfree = n->q[slot].next;

    pt->tx++;
    pt->busy_us = start + serial_us;
    pt->busy_total += serial_us;
    if (wait > pt->qpeak_us) pt->qpeak_us = wait;

    InFlight *f = &n->q[slot];
    f->used = true;
    f->inport = other;
    f->len = (uint16_t)len;
    f->seq = n->qseq++;
    /* PROPAGATION ONLY, rounded to the tick. A hundred metres of copper is
     * half a microsecond, so everything on a LAN lands on the next tick; the
     * length is here because a run across a campus does not, and because the
     * number has to come from the cable rather than from a constant.
     *
     * NOT serialisation. A frame takes the same time on this wire whether the
     * link negotiated a gigabit or ten, so net_port_speed is something the
     * player can read and diagnose from and is not yet something they can
     * feel. Saying otherwise in this comment would be the first lie in the
     * file; when transfers get long enough for the difference to matter, the
     * byte count and the speed are both right here. */
    /* Propagation, plus the milliseconds this frame spends waiting its turn
     * on a port that is already busy. An idle wire adds nothing and every
     * existing check sees the number it always saw; a wire being asked for
     * more than it carries adds tens of milliseconds, and that is what a
     * player reads off ping. */
    uint32_t delay = 1 + (uint32_t)(c->metres / 250)
                       + (uint32_t)((wait + serial_us) / 1000);
    /* The ring holds every delay this stack can produce. A frame that
     * somehow wanted longer would land in the wrong millisecond rather than
     * be lost, so it is clamped where the arithmetic is, not hidden. */
    if (delay >= NET_DUE_RING) delay = NET_DUE_RING - 1;
    f->due = (uint32_t)(n->now + delay);
    memcpy(f->data, data, (size_t)len);
    f->next = -1;
    int b = (int)(f->due % NET_DUE_RING);
    if (n->qtail[b] < 0) n->qhead[b] = slot;
    else n->q[n->qtail[b]].next = slot;
    n->qtail[b] = slot;
}

/* ====================================================================== L2
 * Ethernet. Fourteen bytes of header, an optional four-byte tag, and a
 * switch that knows nothing until something tells it.
 */

static void eth_build(uint8_t *fr, const uint8_t dst[6], const uint8_t src[6],
                      uint16_t type)
{
    memcpy(fr, dst, 6);
    memcpy(fr + 6, src, 6);
    put16(fr + 12, type);
}

/* True if this frame carries an 802.1Q tag, and what vlan it is. */
static bool eth_tagged(const uint8_t *fr, int len, int *vlan)
{
    if (len < 18) return false;
    if (get16(fr + 12) != ETH_P_8021Q) return false;
    if (vlan) *vlan = get16(fr + 14) & 0x0fff;
    return true;
}
/* The ethertype of the payload, past any tag. */
static uint16_t eth_type(const uint8_t *fr, int len)
{
    int v;
    if (eth_tagged(fr, len, &v)) return get16(fr + 16);
    return get16(fr + 12);
}
static int eth_payload_off(const uint8_t *fr, int len)
{
    int v;
    return eth_tagged(fr, len, &v) ? 18 : 14;
}

/* Insert a tag into a frame that has none. */
static int eth_tag(uint8_t *fr, int len, int vlan)
{
    int v;
    if (eth_tagged(fr, len, &v)) return len;
    if (len + 4 > NET_FRAME_MAX) return len;
    memmove(fr + 18, fr + 14, (size_t)(len - 14));
    uint16_t inner = get16(fr + 12);
    put16(fr + 12, ETH_P_8021Q);
    put16(fr + 14, (uint16_t)(vlan & 0x0fff));
    put16(fr + 16, inner);
    return len + 4;
}
/* Take one out. */
static int eth_untag(uint8_t *fr, int len)
{
    int v;
    if (!eth_tagged(fr, len, &v)) return len;
    uint16_t inner = get16(fr + 16);
    memmove(fr + 14, fr + 18, (size_t)(len - 18));
    put16(fr + 12, inner);
    return len - 4;
}

void net_set_mac(Net *n, int node, int ifx, const uint8_t mac[6])
{
    Host *h = host_of(n, node);
    if (!h || ifx < 0 || ifx >= NET_IF_MAX) return;
    h->ifc[ifx].used = true;
    memcpy(h->ifc[ifx].mac, mac, 6);
}
void net_get_mac(const Net *n, int node, int ifx, uint8_t out[6])
{
    const Host *h = chost_of(n, node);
    if (!h || ifx < 0 || ifx >= NET_IF_MAX) { memset(out, 0, 6); return; }
    memcpy(out, h->ifc[ifx].mac, 6);
}
void net_if_port(Net *n, int node, int ifx, int port)
{
    Host *h = host_of(n, node);
    if (!h || ifx < 0 || ifx >= NET_IF_MAX) return;
    int p = pid_of(n, node, port);
    if (!h->ifc[ifx].used) {
        h->ifc[ifx].used = true;
        h->ifc[ifx].up = true;
        factory_mac(n, h->ifc[ifx].mac);
    }
    h->ifc[ifx].port = p;
}
void net_if_up(Net *n, int node, int ifx, bool up)
{
    Host *h = host_of(n, node);
    if (h && ifx >= 0 && ifx < NET_IF_MAX) h->ifc[ifx].up = up;
}
void net_if_vlan(Net *n, int node, int ifx, int vlan)
{
    Host *h = host_of(n, node);
    if (h && ifx >= 0 && ifx < NET_IF_MAX) h->ifc[ifx].vlan = vlan;
}
/* A TAGGED SUBINTERFACE ON A CARD THAT ALREADY EXISTS, which is how one
 * socket terminates a subnet per vlan. It never touches the card underneath
 * it -- that was the whole bug: a verb documented as "add a subinterface"
 * reconfigured eth0 instead, so a router had one address and could not have a
 * WAN side and a LAN side at the same time.
 *
 * Asking twice for the same vlan on the same card gives the same interface
 * back, because it is the same interface. */
int net_if_subif(Net *n, int node, int nic, int vlan)
{
    Host *h = host_of(n, node);
    if (!h || vlan < 1 || vlan > 4094) return -1;
    int p = pid_of(n, node, nic);
    if (p < 0) return -1;
    for (int i = 0; i < NET_IF_MAX; i++)
        if (h->ifc[i].used && h->ifc[i].port == p && h->ifc[i].vlan == vlan) return i;
    /* Above the cards: eth0..eth<nports-1> are sockets and nothing else may
     * take one of those numbers. */
    for (int i = n->node[node].nports; i < NET_IF_MAX; i++) {
        if (h->ifc[i].used) continue;
        h->ifc[i].used = true;
        h->ifc[i].up = true;
        h->ifc[i].port = p;
        h->ifc[i].vlan = vlan;
        factory_mac(n, h->ifc[i].mac);
        return i;
    }
    return -1;
}
/* Take one away again. A card is a hole in the box and does not go anywhere;
 * a subinterface is configuration and does. */
bool net_if_del(Net *n, int node, int ifx)
{
    Host *h = host_of(n, node);
    if (!h || ifx < n->node[node].nports || ifx >= NET_IF_MAX) return false;
    if (!h->ifc[ifx].used) return false;
    memset(&h->ifc[ifx], 0, sizeof h->ifc[ifx]);
    h->ifc[ifx].port = -1;
    return true;
}
/* Which socket an interface hangs off, and what tag it wears. Both are
 * printed, because a player who cannot see the box has no other way to find
 * out which card an address is on. */
int net_if_nic(const Net *n, int node, int ifx)
{
    const Host *h = chost_of(n, node);
    if (!h || ifx < 0 || ifx >= NET_IF_MAX || !h->ifc[ifx].used) return -1;
    int p = h->ifc[ifx].port;
    return p < 0 ? -1 : n->port[p].index;
}
int net_if_get_vlan(const Net *n, int node, int ifx)
{
    const Host *h = chost_of(n, node);
    return (h && ifx >= 0 && ifx < NET_IF_MAX) ? h->ifc[ifx].vlan : 0;
}
bool net_if_exists(const Net *n, int node, int ifx)
{
    const Host *h = chost_of(n, node);
    return h && ifx >= 0 && ifx < NET_IF_MAX && h->ifc[ifx].used;
}
void net_port_vlan(Net *n, int node, int port, int vlan)
{
    int p = pid_of(n, node, port);
    if (p >= 0) n->port[p].vlan = vlan <= 0 ? VLAN_DEFAULT : vlan;
}
void net_port_mode(Net *n, int node, int port, PortMode m)
{
    int p = pid_of(n, node, port);
    if (p >= 0) n->port[p].mode = m;
}
void net_trunk_allow(Net *n, int node, int port, int vlan)
{
    int p = pid_of(n, node, port);
    if (p >= 0 && vlan >= 1 && vlan <= 32) n->port[p].allow |= 1u << (vlan - 1);
}
/* SPANNING TREE, as the thing that is ABSENT by default.
 *
 * The interesting state is the one where this does nothing: an unmanaged
 * switch out of a box has no spanning tree, and a second cable between two
 * switches is then a broadcast storm. So the algorithm below is not here to
 * make the network work -- it is here so that turning it ON is a repair the
 * player can make and watch take effect.
 *
 * Bridge id is the node id, which is the order the player racked them in.
 * Root is the lowest. Every switch has one root port, towards the root; for
 * every link that is not in the tree, the end further from the root blocks,
 * and a tie goes to the higher bridge id. That is enough of 802.1D to break
 * every cycle exactly once, deterministically.
 */
static void stp_recompute(Net *n)
{
    int dist[NET_NODES_MAX], parent[NET_NODES_MAX];
    for (int i = 0; i < NET_PORTS_MAX; i++) n->port[i].blocked = false;
    for (int i = 0; i < n->nnode; i++) { dist[i] = -1; parent[i] = -1; }

    int root = -1;
    for (int i = 0; i < n->nnode; i++) {
        Switch *s = sw_of(n, i);
        if (s && s->stp) { root = i; break; }
    }
    if (root < 0) return;                    /* nobody is running it */

    /* Breadth first from the root, over switch-to-switch links only. */
    int queue[NET_NODES_MAX], qh = 0, qt = 0;
    dist[root] = 0; queue[qt++] = root;
    while (qh < qt) {
        int u = queue[qh++];
        int first = n->node[u].port0, last = first + n->node[u].nports;
        for (int p = first; p < last; p++) {
            int cid = n->port[p].cable;
            if (cid < 0 || !n->cable[cid].used) continue;
            int other = (n->cable[cid].a == p) ? n->cable[cid].b : n->cable[cid].a;
            int v = n->port[other].node;
            Switch *sv = sw_of(n, v);
            if (!sv || !sv->stp) continue;
            if (dist[v] < 0) { dist[v] = dist[u] + 1; parent[v] = u; queue[qt++] = v; }
        }
    }

    /* Every link not on a shortest path from the root gets one end blocked. */
    for (int cid = 0; cid < NET_CABLES_MAX; cid++) {
        if (!n->cable[cid].used) continue;
        int pa = n->cable[cid].a, pb = n->cable[cid].b;
        int na = n->port[pa].node, nb = n->port[pb].node;
        Switch *sa = sw_of(n, na), *sb = sw_of(n, nb);
        if (!sa || !sb || !sa->stp || !sb->stp) continue;
        if (dist[na] < 0 || dist[nb] < 0) continue;
        /* A tree edge: the child's root port and the parent's designated
         * port, and it is the child's ONE way home, so leave both open. */
        bool tree = false;
        if (parent[nb] == na || parent[na] == nb) {
            /* Only the first such link found between this pair is the tree
             * edge; a second parallel cable is a cycle of length two. */
            int child = (parent[nb] == na) ? nb : na;
            int cport = (child == nb) ? pb : pa;
            int rootport = -1;
            int f = n->node[child].port0, l = f + n->node[child].nports;
            for (int p = f; p < l; p++) {
                int c2 = n->port[p].cable;
                if (c2 < 0 || !n->cable[c2].used) continue;
                int o = (n->cable[c2].a == p) ? n->cable[c2].b : n->cable[c2].a;
                if (n->port[o].node == parent[child]) { rootport = p; break; }
            }
            tree = (cport == rootport);
        }
        if (tree) continue;
        /* Block the far end: greater distance, then greater bridge id. */
        int block = (dist[na] != dist[nb]) ? (dist[na] > dist[nb] ? pa : pb)
                                           : (na > nb ? pa : pb);
        n->port[block].blocked = true;
    }
}

void net_stp(Net *n, int node, bool on)
{
    Switch *s = sw_of(n, node);
    if (s) { s->stp = on; stp_recompute(n); }
}

/* Is this vlan permitted to leave by this port? An access port carries one
 * vlan and a trunk carries what it was told to. A trunk that was never told
 * anything carries nothing, which is the commonest new-switch mistake there
 * is and is not a special case here -- it falls out of an empty mask. */
static bool port_carries(const Net *n, int p, int vlan)
{
    const Port *pt = &n->port[p];
    if (pt->mode == PORT_ACCESS) return pt->vlan == vlan;
    if (pt->vlan == vlan) return true;                 /* the native vlan */
    if (vlan < 1 || vlan > 32) return false;
    return (pt->allow & (1u << (vlan - 1))) != 0;
}

static void fdb_learn(Net *n, Switch *s, const uint8_t mac[6], int port, int vlan)
{
    if (mac_group(mac)) return;    /* nothing is ever sourced from a group */
    int free_slot = -1, oldest = -1;
    for (int i = 0; i < NET_FDB_MAX; i++) {
        FdbEntry *e = &s->fdb[i];
        if (e->used && e->vlan == vlan && mac_eq(e->mac, mac)) {
            /* The address moved, or two machines are using it. Either way a
             * switch believes whoever spoke last, and a table that flaps is
             * the honest symptom of a duplicate address. */
            e->port = port;
            e->seen = n->now;
            return;
        }
        if (!e->used) { if (free_slot < 0) free_slot = i; }
        else if (oldest < 0 || e->seen < s->fdb[oldest].seen) oldest = i;
    }
    int i = free_slot >= 0 ? free_slot : oldest;
    if (i < 0) return;
    FdbEntry *e = &s->fdb[i];
    e->used = true;
    memcpy(e->mac, mac, 6);
    e->port = port;
    e->vlan = vlan;
    e->seen = n->now;
}

/* The standard five minutes. An entry that ages out means the switch floods
 * again, which is why a quiet machine becomes briefly noisy to find. */
#define FDB_AGE_MS 300000

static int fdb_find(Net *n, Switch *s, const uint8_t mac[6], int vlan)
{
    for (int i = 0; i < NET_FDB_MAX; i++) {
        FdbEntry *e = &s->fdb[i];
        if (!e->used || e->vlan != vlan || !mac_eq(e->mac, mac)) continue;
        if (n->now - e->seen > FDB_AGE_MS) { e->used = false; return -1; }
        return e->port;
    }
    return -1;
}

int net_fdb_count(const Net *n, int node)
{
    const Switch *s = sw_of((Net *)n, node);
    if (!s) return 0;
    int k = 0;
    for (int i = 0; i < NET_FDB_MAX; i++) if (s->fdb[i].used) k++;
    return k;
}
void net_fdb_flush(Net *n, int node)
{
    Switch *s = sw_of(n, node);
    if (s) for (int i = 0; i < NET_FDB_MAX; i++) s->fdb[i].used = false;
}
int net_fdb_lookup(const Net *n, int node, const uint8_t mac[6], int vlan)
{
    Switch *s = sw_of((Net *)n, node);
    if (!s) return -1;
    int p = fdb_find((Net *)n, s, mac, vlan);
    if (p < 0) return -1;
    return n->port[p].index;
}

static void host_rx(Net *n, int node, int ifx, uint8_t *fr, int len);

/* One frame arriving at one switch port. Everything a switch is happens
 * here, in the order the silicon does it: classify, admit, learn, forward. */
static void switch_rx(Net *n, int node, int p, uint8_t *fr, int len)
{
    Switch *s = sw_of(n, node);
    if (!s) return;
    Port *in = &n->port[p];

    /* A blocked port neither learns nor forwards. The link is up, the light
     * is on, and nothing crosses -- which is what spanning tree looks like
     * to someone who does not know it is running. */
    if (in->blocked) { in->drops++; return; }

    /* Ingress classification. An access port takes untagged frames and puts
     * them in its vlan; a tagged frame arriving at an access port is not
     * something an access port can express, and it is dropped. */
    int vlan;
    bool tagged = eth_tagged(fr, len, &vlan);
    if (in->mode == PORT_ACCESS) {
        if (tagged) { in->drops++; trace(n, "%s p%d drop tagged frame on access port",
                                         n->node[node].name, in->index); return; }
        vlan = in->vlan;
    } else {
        if (!tagged) vlan = in->vlan;          /* the native vlan */
        else { len = eth_untag(fr, len); }     /* work untagged internally */
        if (!port_carries(n, p, vlan)) {
            in->drops++;
            trace(n, "%s p%d drop vlan %d not allowed", n->node[node].name,
                  in->index, vlan);
            return;
        }
    }

    fdb_learn(n, s, fr + 6, p, vlan);

    const uint8_t *dst = fr;
    int out = mac_group(dst) ? -1 : fdb_find(n, s, dst, vlan);

    if (out >= 0 && out != p && !n->port[out].blocked) {
        uint8_t copy[NET_FRAME_MAX];
        memcpy(copy, fr, (size_t)len);
        int olen = len;
        if (n->port[out].mode == PORT_TRUNK && n->port[out].vlan != vlan)
            olen = eth_tag(copy, olen, vlan);
        port_tx(n, out, copy, olen);
        return;
    }
    if (out == p) return;    /* it came from where we would send it */

    /* Unknown, or a group address: flood. This is the line that makes a loop
     * into a storm, and it is deliberately not guarded. Spanning tree, when
     * the switch has it, is what stops it -- and a switch without it does
     * exactly this until the queue is full. */
    s->flooded++;
    int first = n->node[node].port0, last = first + n->node[node].nports;
    for (int q = first; q < last; q++) {
        if (q == p) continue;
        if (n->port[q].blocked) continue;
        if (!port_carries(n, q, vlan)) continue;
        uint8_t copy[NET_FRAME_MAX];
        memcpy(copy, fr, (size_t)len);
        int olen = len;
        if (n->port[q].mode == PORT_TRUNK && n->port[q].vlan != vlan)
            olen = eth_tag(copy, olen, vlan);
        port_tx(n, q, copy, olen);
    }
}

/* Deliver one frame that has finished travelling. */
static void frame_land(Net *n, int p, uint8_t *fr, int len)
{
    Port *pt = &n->port[p];
    pt->rx++;
    n->handled++;
    n->window_count++;
    int node = pt->node;
    if (n->node[node].kind == NODE_SWITCH) { switch_rx(n, node, p, fr, len); return; }

    Host *h = host_of(n, node);
    if (!h) return;
    /* ONE PORT MAY CARRY SEVERAL INTERFACES. A router on a trunk has a
     * tagged subinterface per vlan and they all hang off the same socket, so
     * the frame goes to whichever one claims its tag -- and to none of them
     * if no interface claims it, which is the drop the player is looking at.
     * This used to stop at the first interface on the port and drop
     * everything the first interface did not want, which made a router able
     * to terminate exactly one vlan. */
    int owner = -1;
    for (int i = 0; i < NET_IF_MAX; i++) {
        if (!h->ifc[i].used || h->ifc[i].port != p) continue;
        if (owner < 0) owner = i;               /* who counts the drop */
        if (!h->ifc[i].up) continue;
        int vlan = 0;
        bool tagged = eth_tagged(fr, len, &vlan);
        /* A machine on an access port receives untagged frames. If it is on
         * a trunk it was configured with a vlan, and a frame for a different
         * one is not its. This is what makes a vlan mismatch look like a
         * perfectly good cable carrying nothing. */
        if (h->ifc[i].vlan) {
            if (!tagged || vlan != h->ifc[i].vlan) continue;
            len = eth_untag(fr, len);
        } else if (tagged) continue;
        host_rx(n, node, i, fr, len);
        return;
    }
    if (owner >= 0) h->ifc[owner].rx_drop++;
}

/* One tick of wire time: land everything that is due, oldest first. */
static void net_tick(Net *n)
{
    n->now++;
    int b = (int)(n->now % NET_DUE_RING);
    int i = n->qhead[b];
    n->qhead[b] = n->qtail[b] = -1;
    while (i >= 0) {
        InFlight *f = &n->q[i];
        int next = f->next;
        f->used = false;
        uint8_t data[NET_FRAME_MAX];
        int len = f->len;
        int p = f->inport;
        memcpy(data, f->data, (size_t)len);
        f->next = n->qfree;
        n->qfree = i;
        /* A frame lands on a port whose cable has since been pulled: it is
         * gone, and the count above already forgot it. */
        if (port_state(n, p) == PORT_UP) frame_land(n, p, data, len);
        i = next;
    }
}

uint64_t net_load(const Net *n) { return n->load; }
uint64_t net_queue_drops(const Net *n) { return n->qdrops; }
size_t   net_world_bytes(void) { return sizeof(Net); }

/* ====================================================================== L3
 * IP. Addresses, masks, a routing table, ARP, and the ICMP errors that are
 * how a player finds out WHICH thing is wrong.
 *
 * The single most important function in this section is route_pick, because
 * everything the brief calls an emergent fault comes out of it: a wrong mask
 * changes which branch a destination takes, and neither branch knows that a
 * mask can be wrong.
 */

void net_if_addr(Net *n, int node, int ifx, uint32_t ip, uint32_t mask)
{
    Host *h = host_of(n, node);
    if (!h || ifx < 0 || ifx >= NET_IF_MAX) return;
    if (!h->ifc[ifx].used) {
        h->ifc[ifx].used = true;
        h->ifc[ifx].up = true;
        h->ifc[ifx].port = -1;
        factory_mac(n, h->ifc[ifx].mac);
    }
    h->ifc[ifx].ip = ip;
    h->ifc[ifx].mask = mask;
}
bool net_if_alias(Net *n, int node, uint32_t ip)
{
    if (!host_of(n, node)) return false;
    for (int i = 0; i < NET_ALIAS_MAX; i++)
        if (n->alias[i].used && n->alias[i].node == node && n->alias[i].ip == ip)
            return true;
    for (int i = 0; i < NET_ALIAS_MAX; i++)
        if (!n->alias[i].used) {
            n->alias[i].used = true;
            n->alias[i].node = node;
            n->alias[i].ip = ip;
            return true;
        }
    return false;
}
static bool alias_held(const Net *n, int node, uint32_t ip)
{
    for (int i = 0; i < NET_ALIAS_MAX; i++)
        if (n->alias[i].used && n->alias[i].node == node && n->alias[i].ip == ip)
            return true;
    return false;
}

uint32_t net_if_get_addr(const Net *n, int node, int ifx)
{
    const Host *h = chost_of(n, node);
    return (h && ifx >= 0 && ifx < NET_IF_MAX) ? h->ifc[ifx].ip : 0;
}
uint32_t net_if_get_mask(const Net *n, int node, int ifx)
{
    const Host *h = chost_of(n, node);
    return (h && ifx >= 0 && ifx < NET_IF_MAX) ? h->ifc[ifx].mask : 0;
}

void net_route_add(Net *n, int node, uint32_t dst, uint32_t mask, uint32_t gw, int ifx)
{
    Host *h = host_of(n, node);
    if (!h) return;
    for (int i = 0; i < NET_ROUTE_MAX; i++)
        if (!h->rt[i].used) {
            h->rt[i].used = true;
            h->rt[i].dst = dst & mask;
            h->rt[i].mask = mask;
            h->rt[i].gw = gw;
            h->rt[i].ifx = ifx;
            return;
        }
}
void net_route_clear(Net *n, int node)
{
    Host *h = host_of(n, node);
    if (h) for (int i = 0; i < NET_ROUTE_MAX; i++) h->rt[i].used = false;
}
void net_set_gateway(Net *n, int node, uint32_t gw)
{
    Host *h = host_of(n, node);
    if (!h) return;
    /* Replace any existing default rather than stacking a second one: a
     * machine with two default routes is a fault we are not modelling yet,
     * and pretending to model it would be worse than not. */
    for (int i = 0; i < NET_ROUTE_MAX; i++)
        if (h->rt[i].used && h->rt[i].mask == 0) h->rt[i].used = false;
    net_route_add(n, node, 0, 0, gw, -1);
}
uint32_t net_get_gateway(const Net *n, int node)
{
    const Host *h = host_of((Net *)n, node);
    if (!h) return 0;
    for (int i = 0; i < NET_ROUTE_MAX; i++)
        if (h->rt[i].used && h->rt[i].mask == 0) return h->rt[i].gw;
    return 0;
}
uint32_t net_get_resolver(const Net *n, int node)
{
    const Host *h = host_of((Net *)n, node);
    return h ? h->resolver : 0;
}
void net_forwarding(Net *n, int node, bool on)
{
    Host *h = host_of(n, node);
    if (h) h->forwarding = on;
}

/* Which interface, and to whom do we hand it. Longest prefix wins, connected
 * subnets included -- and a connected subnet is derived from the mask on the
 * interface, every time, so a wrong mask changes the answer here and nowhere
 * else in the program. */
static int route_pick(Net *n, Host *h, uint32_t dst, uint32_t *nh)
{
    int best_len = -1, best_if = -1;
    uint32_t best_nh = 0;
    (void)n;

    for (int i = 0; i < NET_IF_MAX; i++) {
        Iface *f = &h->ifc[i];
        if (!f->used || !f->up || !f->ip || !f->mask) continue;
        if ((dst & f->mask) != (f->ip & f->mask)) continue;
        int len = net_mask_len(f->mask);
        if (len > best_len) { best_len = len; best_if = i; best_nh = dst; }
    }
    for (int i = 0; i < NET_ROUTE_MAX; i++) {
        Route *r = &h->rt[i];
        if (!r->used) continue;
        if ((dst & r->mask) != r->dst) continue;
        int len = net_mask_len(r->mask);
        if (len < best_len) continue;
        /* A tie between a connected subnet and a static route of the same
         * length goes to the connected one, as it does everywhere. */
        if (len == best_len && best_if >= 0 && r->gw == 0) continue;
        int ifx = r->ifx;
        uint32_t next = r->gw ? r->gw : dst;
        if (ifx < 0) {
            /* The route names a gateway and no interface, which is how a
             * default route is usually written. The interface is whichever
             * one the gateway is ON -- and if no interface's subnet contains
             * it, the route is unusable. That is a real and very common
             * misconfiguration: a default gateway outside your own subnet. */
            ifx = -1;
            for (int k = 0; k < NET_IF_MAX; k++) {
                Iface *f = &h->ifc[k];
                if (!f->used || !f->up || !f->ip || !f->mask) continue;
                if ((next & f->mask) == (f->ip & f->mask)) { ifx = k; break; }
            }
            if (ifx < 0) continue;
        }
        best_len = len; best_if = ifx; best_nh = next;
    }
    if (best_if < 0) return -1;
    *nh = best_nh;
    return best_if;
}

/* ------------------------------------------------------------------- ARP */
/* The pool of frames waiting on an answer. Take one, put it back, and copy
 * it out when the answer arrives -- and if the pool is empty the packet is
 * simply not held, which is the same first-packet loss a real host has when
 * its own queue is full. */
static void arp_unhold(Net *n, ArpEntry *e)
{
    if (e->hold >= 0) n->hold[e->hold].used = false;
    e->hold = -1;
    e->holdlen = 0;
}
static void arp_hold(Net *n, ArpEntry *e, const uint8_t *pkt, int len, int ifx)
{
    arp_unhold(n, e);
    if (len <= 0 || len > NET_FRAME_MAX) return;
    for (int i = 0; i < NET_HOLD_MAX; i++)
        if (!n->hold[i].used) {
            n->hold[i].used = true;
            memcpy(n->hold[i].data, pkt, (size_t)len);
            e->hold = i;
            e->holdlen = len;
            e->holdif = ifx;
            return;
        }
}
static ArpEntry *arp_find(Host *h, uint32_t ip)
{
    for (int i = 0; i < NET_ARP_MAX; i++)
        if (h->arp[i].used && h->arp[i].ip == ip) return &h->arp[i];
    return NULL;
}
static ArpEntry *arp_slot(Net *n, Host *h, uint32_t ip)
{
    ArpEntry *e = arp_find(h, ip);
    if (e) return e;
    int oldest = -1;
    for (int i = 0; i < NET_ARP_MAX; i++) {
        if (!h->arp[i].used) { e = &h->arp[i]; break; }
        if (oldest < 0 || h->arp[i].seen < h->arp[oldest].seen) oldest = i;
    }
    if (!e) { e = &h->arp[oldest]; arp_unhold(n, e); }
    memset(e, 0, sizeof *e);
    e->hold = -1;
    e->used = true;
    e->ip = ip;
    e->seen = n->now;
    return e;
}
void net_arp_flush(Net *n, int node)
{
    Host *h = host_of(n, node);
    if (h) for (int i = 0; i < NET_ARP_MAX; i++) {
        if (h->arp[i].used) arp_unhold(n, &h->arp[i]);
        h->arp[i].used = false;
    }
}
int net_arp_count(const Net *n, int node)
{
    const Host *h = chost_of(n, node);
    if (!h) return 0;
    int k = 0;
    for (int i = 0; i < NET_ARP_MAX; i++)
        if (h->arp[i].used && !h->arp[i].pending) k++;
    return k;
}
bool net_arp_cached(const Net *n, int node, uint32_t ip, uint8_t out[6])
{
    Host *h = host_of((Net *)n, node);
    if (!h) return false;
    ArpEntry *e = arp_find(h, ip);
    if (!e || e->pending) return false;
    if (out) memcpy(out, e->mac, 6);
    return true;
}

/* Put a frame on the interface's port, tagging it if the interface was told
 * to. This is the only place L3 touches L2. */
static void if_tx(Net *n, Host *h, int ifx, const uint8_t dst[6],
                  uint16_t type, const uint8_t *payload, int plen)
{
    Iface *f = &h->ifc[ifx];
    if (!f->used || !f->up || f->port < 0) return;
    if (plen > 1500) return;
    uint8_t fr[NET_FRAME_MAX];
    eth_build(fr, dst, f->mac, type);
    memcpy(fr + ETH_HLEN, payload, (size_t)plen);
    int len = ETH_HLEN + plen;
    if (len < 60) { memset(fr + len, 0, (size_t)(60 - len)); len = 60; }
    if (f->vlan) len = eth_tag(fr, len, f->vlan);
    f->tx_pkt++;
    port_tx(n, f->port, fr, len);
}

/* A real ARP packet: 28 bytes, ethernet over IPv4, opcode 1 or 2. */
static void arp_emit(Net *n, int node, Host *h, int ifx, int op,
                     uint32_t tip, const uint8_t tmac[6], const uint8_t dst[6])
{
    Iface *f = &h->ifc[ifx];
    uint8_t a[28];
    put16(a + 0, 1);            /* hardware: ethernet   */
    put16(a + 2, ETH_P_IP);     /* protocol: IPv4       */
    a[4] = 6; a[5] = 4;
    put16(a + 6, (uint16_t)op);
    memcpy(a + 8, f->mac, 6);
    put32(a + 14, f->ip);
    memcpy(a + 18, tmac, 6);
    put32(a + 24, tip);
    char s1[20], s2[20];
    net_fmt_ip(f->ip, s1, sizeof s1);
    net_fmt_ip(tip, s2, sizeof s2);
    if (op == 1) trace(n, "%s arp who-has %s tell %s", n->node[node].name, s2, s1);
    else         trace(n, "%s arp reply %s is-at %02x:%02x:%02x:%02x:%02x:%02x",
                       n->node[node].name, s1, f->mac[0], f->mac[1], f->mac[2],
                       f->mac[3], f->mac[4], f->mac[5]);
    if_tx(n, h, ifx, dst, ETH_P_ARP, a, sizeof a);
}

static void ip_send_frame(Net *n, int node, Host *h, int ifx,
                          const uint8_t *pkt, int len, const uint8_t dmac[6])
{
    (void)node;
    if_tx(n, h, ifx, dmac, ETH_P_IP, pkt, len);
}

/* ------------------------------------------------------------- the filter */
/* One place. A packet is tested here and dropped here, and nothing above
 * knows that a rule exists -- which is why a firewall rule produces a
 * timeout and not an error message, exactly as it does in life. */
static bool fw_pass(Host *h, FwChain chain, uint8_t proto, uint16_t dport,
                    uint32_t src)
{
    for (int i = 0; i < NET_FW_MAX; i++) {
        FwRule *r = &h->fw[i];
        if (!r->used || r->chain != (uint8_t)chain) continue;
        if (r->proto != FW_ANY_PROTO && r->proto != proto) continue;
        if (r->dport != FW_ANY_PORT && r->dport != dport) continue;
        if (r->srcmask && (src & r->srcmask) != r->srcnet) continue;
        r->hits++;
        return r->action == FW_ACCEPT;
    }
    return true;      /* the default policy is accept, and it is stated here */
}

void net_fw_add(Net *n, int node, FwChain c, int proto, uint16_t dport,
                uint32_t srcnet, uint32_t srcmask, FwAction a)
{
    Host *h = host_of(n, node);
    if (!h) return;
    for (int i = 0; i < NET_FW_MAX; i++)
        if (!h->fw[i].used) {
            h->fw[i].used = true;
            h->fw[i].chain = (uint8_t)c;
            h->fw[i].proto = (uint8_t)proto;
            h->fw[i].dport = dport;
            h->fw[i].srcnet = srcnet & srcmask;
            h->fw[i].srcmask = srcmask;
            h->fw[i].action = (uint8_t)a;
            h->fw[i].hits = 0;
            return;
        }
}
void net_fw_clear(Net *n, int node)
{
    Host *h = host_of(n, node);
    if (h) for (int i = 0; i < NET_FW_MAX; i++) h->fw[i].used = false;
}
uint64_t net_fw_hits(const Net *n, int node, int rule)
{
    const Host *h = chost_of(n, node);
    if (!h || rule < 0 || rule >= NET_FW_MAX || !h->fw[rule].used) return 0;
    return h->fw[rule].hits;
}
int net_fw_count(const Net *n, int node)
{
    const Host *h = chost_of(n, node);
    if (!h) return 0;
    int k = 0;
    for (int i = 0; i < NET_FW_MAX; i++) if (h->fw[i].used) k++;
    return k;
}

/* --------------------------------------------------------------- IP out  */
static void ip_input(Net *n, int node, int ifx, uint8_t *pkt, int len);

/* Build and send one IP datagram. Returns a PingResult-shaped answer so the
 * caller learns WHY it could not be sent, which is the difference between
 * "no route" and "nobody answered". */
/* `force_if` is -1 for everything that gets routed. It is an interface index
 * for the one case that genuinely cannot be routed: a limited broadcast to
 * 255.255.255.255, which is not on any subnet by definition and which is how
 * a machine with no address at all asks for one. */
static PingResult ip_output_if(Net *n, int node, int force_if, uint32_t dst,
                               uint8_t proto, const uint8_t *payload, int plen,
                               int ttl, uint32_t force_src, bool from_forward)
{
    Host *h = host_of(n, node);
    if (!h || plen < 0 || plen > 1480) return PING_NO_ROUTE;

    uint32_t nh = dst;
    int ifx = force_if;
    if (ifx < 0) ifx = route_pick(n, h, dst, &nh);
    if (ifx < 0 || ifx >= NET_IF_MAX || !h->ifc[ifx].used) return PING_NO_ROUTE;
    Iface *f = &h->ifc[ifx];
    if (!f->up) return PING_IF_DOWN;
    if (f->port >= 0 && port_state(n, f->port) != PORT_UP) return PING_IF_DOWN;

    /* IP_SRC_AUTO, not zero, means "pick the address of the interface it is
     * going out of". Zero is a REAL source address -- it is what a machine
     * that has never had one puts in a DHCP discover -- and treating it as
     * "unset" silently rewrote those packets to the address the client was
     * trying to get. The checksum was then computed over one source and the
     * header carried another, every such datagram was discarded by the
     * receiver as corrupt, and DHCP renewal and every DNS query failed for a
     * reason no layer could report. */
    uint32_t src = (force_src == IP_SRC_AUTO) ? f->ip : force_src;

    uint8_t pkt[NET_FRAME_MAX];
    memset(pkt, 0, 20);
    pkt[0] = 0x45;                       /* v4, 20-byte header  */
    put16(pkt + 2, (uint16_t)(20 + plen));
    put16(pkt + 4, (uint16_t)(n->rng.s & 0xffff));   /* id */
    pkt[8] = (uint8_t)ttl;
    pkt[9] = proto;
    put32(pkt + 12, src);
    put32(pkt + 16, dst);
    put16(pkt + 10, cksum(pkt, 20, 0));
    memcpy(pkt + 20, payload, (size_t)plen);
    int len = 20 + plen;

    if (!fw_pass(h, from_forward ? FW_FORWARD : FW_OUT, proto,
                 (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) && plen >= 4
                     ? get16(payload + 2) : 0, src))
        return PING_TIMEOUT;             /* dropped: silence, as a drop is */

    /* A packet to ourselves never reaches a wire. */
    for (int i = 0; i < NET_IF_MAX; i++)
        if (h->ifc[i].used && h->ifc[i].ip && h->ifc[i].ip == dst) {
            ip_input(n, node, i, pkt, len);
            return PING_OK;
        }
    if (alias_held(n, node, dst)) { ip_input(n, node, ifx, pkt, len); return PING_OK; }

    uint8_t dmac[6];
    bool bcast = (dst == 0xffffffffu) ||
                 (f->mask && (dst | ~f->mask) == dst && (dst & f->mask) == (f->ip & f->mask));
    if (bcast) {
        memcpy(dmac, MAC_BCAST, 6);
        ip_send_frame(n, node, h, ifx, pkt, len, dmac);
        return PING_OK;
    }

    ArpEntry *e = arp_find(h, nh);
    if (e && !e->pending && n->now - e->seen <= 120000) {
        ip_send_frame(n, node, h, ifx, pkt, len, e->mac);
        return PING_OK;
    }
    /* Not resolved. Hold ONE packet, ask, and let the answer send it. If
     * nothing answers, this is where "destination host unreachable" comes
     * from -- the last hop, not the source. */
    e = arp_slot(n, h, nh);
    e->pending = true;
    e->seen = n->now;
    arp_hold(n, e, pkt, len, ifx);
    arp_emit(n, node, h, ifx, 1, nh, MAC_ZERO, MAC_BCAST);
    return PING_OK;
}

static PingResult ip_output(Net *n, int node, uint32_t dst, uint8_t proto,
                            const uint8_t *payload, int plen, int ttl,
                            uint32_t force_src, bool from_forward)
{
    return ip_output_if(n, node, -1, dst, proto, payload, plen, ttl,
                        force_src, from_forward);
}

/* An ICMP error about somebody else's packet. RFC shape: eight bytes of
 * unused, then the offending header and the first eight bytes of its
 * payload, so the sender can tell WHICH of its conversations died. */
static void icmp_error(Net *n, int node, uint8_t type, uint8_t code,
                       const uint8_t *orig, int origlen, uint32_t src_hint)
{
    if (origlen < 20) return;
    uint32_t back = get32(orig + 12);
    if (!back || back == 0xffffffffu) return;
    /* Never answer an ICMP error with an ICMP error. */
    if (orig[9] == IP_PROTO_ICMP && origlen >= 21 &&
        orig[20] != ICMP_ECHO && orig[20] != ICMP_ECHOREPLY) return;
    int keep = origlen < 28 ? origlen : 28;
    uint8_t m[8 + 28];
    memset(m, 0, sizeof m);
    m[0] = type; m[1] = code;
    memcpy(m + 8, orig, (size_t)keep);
    put16(m + 2, cksum(m, 8 + keep, 0));
    ip_output(n, node, back, IP_PROTO_ICMP, m, 8 + keep, 64, src_hint, false);
}

static void udp_input(Net *n, int node, int ifx, uint32_t src, uint32_t dst,
                      const uint8_t *seg, int len);
static void tcp_input(Net *n, int node, int ifx, uint32_t src, uint32_t dst,
                      const uint8_t *seg, int len);

/* Is this address one of ours, or one that everyone on our wire answers to? */
static int ip_is_local(const Net *n, int node, Host *h, uint32_t dst, bool *bcast)
{
    *bcast = false;
    if (dst == 0xffffffffu) { *bcast = true; return 1; }
    if (alias_held(n, node, dst)) return 1;
    for (int i = 0; i < NET_IF_MAX; i++) {
        Iface *f = &h->ifc[i];
        if (!f->used || !f->up) continue;
        if (f->ip && f->ip == dst) return 1;
        if (f->mask && (dst & f->mask) == (f->ip & f->mask) &&
            (dst | ~f->mask) == dst) { *bcast = true; return 1; }
    }
    return 0;
}

static void icmp_input(Net *n, int node, int ifx, uint32_t src, uint32_t dst,
                       const uint8_t *m, int len)
{
    Host *h = host_of(n, node);
    if (!h || len < 8) return;
    if (m[0] == ICMP_ECHO) {
        uint8_t r[NET_FRAME_MAX - 40];
        int rl = len > (int)sizeof r ? (int)sizeof r : len;
        memcpy(r, m, (size_t)rl);
        r[0] = ICMP_ECHOREPLY;
        put16(r + 2, 0);
        put16(r + 2, cksum(r, rl, 0));
        char s[20]; net_fmt_ip(src, s, sizeof s);
        trace(n, "%s icmp echo reply to %s", n->node[node].name, s);
        ip_output(n, node, src, IP_PROTO_ICMP, r, rl, 64, dst, false);
        return;
    }
    if (m[0] == ICMP_ECHOREPLY) {
        h->icmp_err_type = ICMP_ECHOREPLY;
        h->icmp_err_code = 0;
        h->icmp_err_from = src;
        h->icmp_err_at = n->now;
        return;
    }
    if (m[0] == ICMP_UNREACH || m[0] == ICMP_TIMXCEED) {
        h->icmp_err_type = m[0];
        h->icmp_err_code = m[1];
        h->icmp_err_from = src;
        h->icmp_err_at = n->now;
        char s[20]; net_fmt_ip(src, s, sizeof s);
        trace(n, "%s icmp %s from %s", n->node[node].name,
              m[0] == ICMP_TIMXCEED ? "time exceeded" : "unreachable", s);
    }
    (void)ifx;
}

static void ip_input(Net *n, int node, int ifx, uint8_t *pkt, int len)
{
    Host *h = host_of(n, node);
    if (!h || len < 20) return;
    if ((pkt[0] >> 4) != 4) return;
    int hlen = (pkt[0] & 15) * 4;
    if (hlen < 20 || hlen > len) return;
    /* The checksum is computed, not trusted. A mangled header is discarded
     * here and nowhere reports why, because nothing knows. */
    if (cksum(pkt, hlen, 0) != 0) { h->ifc[ifx].rx_drop++; return; }
    int total = get16(pkt + 2);
    if (total < hlen || total > len) total = len;
    uint32_t src = get32(pkt + 12), dst = get32(pkt + 16);
    uint8_t proto = pkt[9];

    bool bcast = false;
    if (ip_is_local(n, node, h, dst, &bcast)) {
        uint16_t dport = 0, sport = 0;
        if ((proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) && total >= hlen + 4) {
            sport = get16(pkt + hlen);
            dport = get16(pkt + hlen + 2);
        }
        /* IS THIS THE ANSWER TO SOMETHING WE ASKED?
         *
         * Every real filter has this, and a ruleset with `policy drop` is
         * unusable without it: the reply to your own outbound connection
         * arrives on a high port nobody would ever write a rule for, so a
         * machine that filtered it could make connections and never hear
         * back. Tracked by looking for a socket already holding the exact
         * four-tuple -- which is the connection, so this is not a shortcut
         * around the filter, it is the filter's established state. */
        bool established = false;
        if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP)
            for (int i = 0; i < NET_SOCK_MAX; i++) {
                Sock *c = &n->sock[i];
                if (!c->used || c->node != node || c->proto != proto) continue;
                if (c->state == TCP_LISTEN && c->proto == IP_PROTO_TCP) continue;
                if (c->lport != dport) continue;
                if (c->proto == IP_PROTO_UDP) { established = true; break; }
                if (c->rport == sport && c->raddr == src) { established = true; break; }
            }
        if (!established && !fw_pass(h, FW_IN, proto, dport, src)) {
            h->ifc[ifx].rx_drop++;
            trace(n, "%s filter drop proto %d port %d", n->node[node].name, proto, dport);
            return;
        }
        h->ifc[ifx].rx_pkt++;
        if (proto == IP_PROTO_ICMP) icmp_input(n, node, ifx, src, dst, pkt + hlen, total - hlen);
        else if (proto == IP_PROTO_UDP) udp_input(n, node, ifx, src, dst, pkt + hlen, total - hlen);
        else if (proto == IP_PROTO_TCP) tcp_input(n, node, ifx, src, dst, pkt + hlen, total - hlen);
        else icmp_error(n, node, ICMP_UNREACH, 2 /* protocol */, pkt, total, dst);
        return;
    }

    /* Not ours. A machine that was not told to route drops it silently --
     * which is why "I set the gateway and it still does not work" is a
     * diagnosis about the GATEWAY and not about the client. */
    if (!h->forwarding) { h->ifc[ifx].rx_drop++; return; }
    if (bcast) return;

    if (pkt[8] <= 1) {
        trace(n, "%s ttl expired, icmp time exceeded", n->node[node].name);
        icmp_error(n, node, ICMP_TIMXCEED, 0, pkt, total, h->ifc[ifx].ip);
        return;
    }
    uint32_t nh = 0;
    int oif = route_pick(n, h, dst, &nh);
    if (oif < 0) {
        char s[20]; net_fmt_ip(dst, s, sizeof s);
        trace(n, "%s no route to %s, icmp net unreachable", n->node[node].name, s);
        icmp_error(n, node, ICMP_UNREACH, ICMP_UNREACH_NET, pkt, total, h->ifc[ifx].ip);
        return;
    }
    if (!fw_pass(h, FW_FORWARD, proto,
                 (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) && total >= hlen + 4
                     ? get16(pkt + hlen + 2) : 0, src)) {
        trace(n, "%s filter drop forwarded packet", n->node[node].name);
        return;
    }

    /* Decrement and repair the checksum, exactly as a router does. */
    pkt[8]--;
    put16(pkt + 10, 0);
    put16(pkt + 10, cksum(pkt, hlen, 0));

    Iface *of = &h->ifc[oif];
    if (!of->up || of->port < 0 || port_state(n, of->port) != PORT_UP) {
        icmp_error(n, node, ICMP_UNREACH, ICMP_UNREACH_HOST, pkt, total, h->ifc[ifx].ip);
        return;
    }
    ArpEntry *e = arp_find(h, nh);
    if (e && !e->pending) { ip_send_frame(n, node, h, oif, pkt, total, e->mac); return; }
    e = arp_slot(n, h, nh);
    if (!e->pending || n->now - e->seen > 1000) {
        e->pending = true;
        e->seen = n->now;
        arp_hold(n, e, pkt, total, oif);
        arp_emit(n, node, h, oif, 1, nh, MAC_ZERO, MAC_BCAST);
    }
}

static void arp_input(Net *n, int node, int ifx, const uint8_t *a, int len)
{
    Host *h = host_of(n, node);
    if (!h || len < 28) return;
    if (get16(a) != 1 || get16(a + 2) != ETH_P_IP) return;
    int op = get16(a + 6);
    uint32_t sip = get32(a + 14), tip = get32(a + 24);
    const uint8_t *smac = a + 8;

    /* Learn the sender either way. This is how a duplicate address poisons a
     * whole segment: the cache believes the last machine to speak, and both
     * of them keep speaking. */
    if (sip) {
        ArpEntry *e = arp_slot(n, h, sip);
        memcpy(e->mac, smac, 6);
        e->pending = false;
        e->seen = n->now;
        if (e->holdlen && e->hold >= 0) {
            uint8_t held[NET_FRAME_MAX];
            int hl = e->holdlen, hi = e->holdif;
            memcpy(held, n->hold[e->hold].data, (size_t)hl);
            arp_unhold(n, e);
            ip_send_frame(n, node, h, hi, held, hl, e->mac);
        }
    }
    if (op != 1) return;
    /* Do WE have that address? Two machines that both do will both answer. */
    for (int i = 0; i < NET_IF_MAX; i++) {
        Iface *f = &h->ifc[i];
        if (!f->used || !f->up || !f->ip || f->ip != tip) continue;
        arp_emit(n, node, h, i, 2, sip, smac, smac);
        return;
    }
    /* An alias is answered for out of the interface the question arrived on,
     * which is what makes thirty virtual hosts on one NIC reachable. */
    if (alias_held(n, node, tip) && h->ifc[ifx].used && h->ifc[ifx].up) {
        Iface *f = &h->ifc[ifx];
        uint32_t save = f->ip;
        f->ip = tip;
        arp_emit(n, node, h, ifx, 2, sip, smac, smac);
        f->ip = save;
    }
    (void)ifx;
}

static void host_rx(Net *n, int node, int ifx, uint8_t *fr, int len)
{
    Host *h = host_of(n, node);
    if (!h) return;
    Iface *f = &h->ifc[ifx];
    /* Not addressed to this NIC and not a group address: the hardware never
     * hands it up. No promiscuous mode, because a machine that quietly saw
     * everybody else's traffic would make VLANs pointless. */
    if (!mac_eq(fr, f->mac) && !mac_group(fr)) { return; }
    int off = eth_payload_off(fr, len);
    uint16_t type = eth_type(fr, len);
    if (type == ETH_P_ARP) arp_input(n, node, ifx, fr + off, len - off);
    else if (type == ETH_P_IP) ip_input(n, node, ifx, fr + off, len - off);
}

/* Resolve for real: emit a request and run the world until an answer lands. */
bool net_arp_resolve(Net *n, int node, uint32_t ip, uint8_t out[6])
{
    Host *h = host_of(n, node);
    if (!h) return false;
    if (net_arp_cached(n, node, ip, out)) return true;
    uint32_t nh = 0;
    int ifx = route_pick(n, h, ip, &nh);
    if (ifx < 0) return false;
    ArpEntry *e = arp_slot(n, h, ip);
    e->pending = true;
    e->seen = n->now;
    arp_emit(n, node, h, ifx, 1, ip, MAC_ZERO, MAC_BCAST);
    for (int i = 0; i < 200; i++) {
        net_step(n, 1);
        if (net_arp_cached(n, node, ip, out)) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ ping */
PingResult net_ping(Net *n, int node, uint32_t dst, int *rtt_ms)
{
    Host *h = host_of(n, node);
    if (!h) return PING_NO_ROUTE;
    uint64_t t0 = n->now;
    h->icmp_err_at = 0;
    h->icmp_err_type = 0xff;

    uint8_t m[16];
    memset(m, 0, sizeof m);
    m[0] = ICMP_ECHO;
    put16(m + 4, 0x4e4f);              /* our identifier, "NO" */
    /* The sequence number comes off the world clock, not out of a static:
     * a counter that lives in the function is shared between every Net in
     * the process, and two runs of the same seed would then differ. */
    put16(m + 6, (uint16_t)n->now);
    put16(m + 2, cksum(m, sizeof m, 0));

    char s[20]; net_fmt_ip(dst, s, sizeof s);
    trace(n, "%s icmp echo request to %s", n->node[node].name, s);
    PingResult r = ip_output(n, node, dst, IP_PROTO_ICMP, m, sizeof m, 64, IP_SRC_AUTO, false);
    if (r == PING_NO_ROUTE || r == PING_IF_DOWN) return r;

    for (int i = 0; i < 1000; i++) {
        net_step(n, 1);
        if (h->icmp_err_at <= t0) continue;
        if (h->icmp_err_type == ICMP_ECHOREPLY) {
            if (rtt_ms) *rtt_ms = (int)(n->now - t0);
            return PING_OK;
        }
        if (h->icmp_err_type == ICMP_TIMXCEED) return PING_TTL_EXCEEDED;
        if (h->icmp_err_type == ICMP_UNREACH)
            return h->icmp_err_code == ICMP_UNREACH_NET ? PING_NET_UNREACH
                                                        : PING_HOST_UNREACH;
    }
    /* Nothing came back and no router complained. If the last hop never got
     * an ARP answer, that is a host that is not there -- and the host that
     * knows it is the one holding the unanswered entry, which is us when the
     * destination is on our own wire. */
    uint32_t nh = 0;
    int ifx = route_pick(n, h, dst, &nh);
    if (ifx >= 0) {
        ArpEntry *e = arp_find(h, nh);
        if (e && e->pending) return PING_HOST_UNREACH;
    }
    return PING_TIMEOUT;
}

int net_traceroute(Net *n, int node, uint32_t dst, uint32_t *hops, int maxhops)
{
    Host *h = host_of(n, node);
    if (!h) return 0;
    int found = 0;
    for (int ttl = 1; ttl <= maxhops; ttl++) {
        uint64_t t0 = n->now;
        h->icmp_err_at = 0;
        h->icmp_err_type = 0xff;
        uint8_t m[16];
        memset(m, 0, sizeof m);
        m[0] = ICMP_ECHO;
        put16(m + 4, 0x4e4f);
        put16(m + 6, (uint16_t)ttl);
        put16(m + 2, cksum(m, sizeof m, 0));
        if (ip_output(n, node, dst, IP_PROTO_ICMP, m, sizeof m, ttl, IP_SRC_AUTO, false) != PING_OK)
            break;
        bool got = false;
        for (int i = 0; i < 500 && !got; i++) {
            net_step(n, 1);
            if (h->icmp_err_at > t0) got = true;
        }
        if (!got) { hops[found++] = 0; continue; }
        hops[found++] = h->icmp_err_from;
        if (h->icmp_err_type == ICMP_ECHOREPLY) break;
        if (h->icmp_err_type == ICMP_UNREACH) break;
    }
    return found;
}

/* ====================================================================== L4
 * UDP, and a TCP that is honest about being TCP: a three-way handshake with
 * real initial sequence numbers, byte-numbered data, a receive window that
 * is the size of the buffer we actually have, retransmission on a timer, and
 * a four-way teardown with a TIME_WAIT at the end of it.
 *
 * It is not congestion-controlled and it does not reassemble out-of-order
 * segments -- on a switched LAN with a deterministic queue nothing arrives
 * out of order, and pretending otherwise would be code no test could reach.
 * Everything it DOES claim, it does.
 */

static int sock_alloc(Net *n, int node, uint8_t proto)
{
    for (int i = 0; i < NET_SOCK_MAX; i++) {
        if (n->sock[i].used) continue;
        Sock *s = &n->sock[i];
        memset(s, 0, sizeof *s);
        s->used = true;
        s->node = node;
        s->proto = proto;
        s->listener = -1;
        s->accepted = -1;
        s->dgram_if = -1;
        return i;
    }
    return -1;
}
void net_sock_free(Net *n, int sock)
{
    if (sock >= 0 && sock < NET_SOCK_MAX) n->sock[sock].used = false;
}
void net_release_host(Net *n, int node)
{
    Host *h = host_of(n, node);
    if (!h) return;
    net_close_all(n, node);
    for (int i = 0; i < NET_ROUTE_MAX; i++) h->rt[i].used = false;
    for (int i = 0; i < NET_ARP_MAX; i++)   h->arp[i].used = false;
    for (int i = 0; i < NET_FW_MAX; i++)    h->fw[i].used = false;
    h->forwarding = false;
    h->resolver = 0;
    h->dhcpd = h->dnsd = h->httpd = false;
    for (int i = 0; i < NET_ALIAS_MAX; i++)
        if (n->alias[i].used && n->alias[i].node == node) n->alias[i].used = false;
    int first = n->node[node].port0, last = first + n->node[node].nports;
    for (int p = first; p < last; p++)
        if (n->port[p].cable >= 0) net_uncable(n, n->port[p].cable);
    for (int i = 0; i < NET_IF_MAX; i++) {
        Iface *f = &h->ifc[i];
        if (!f->used) continue;
        f->ip = f->mask = 0;
        f->vlan = 0;
        f->up = (i == 0);
        f->rx_pkt = f->tx_pkt = f->rx_drop = 0;
        /* The MAC stays. It is burned into the card. */
    }
}

void net_close_all(Net *n, int node)
{
    for (int i = 0; i < NET_SOCK_MAX; i++)
        if (n->sock[i].used && n->sock[i].node == node) n->sock[i].used = false;
}
int net_sock_node(const Net *n, int sock)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return -1;
    return n->sock[sock].node;
}
TcpState net_tcp_state(const Net *n, int sock)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return TCP_CLOSED;
    return n->sock[sock].state;
}

static uint16_t eph_port(Host *h)
{
    if (h->next_eph < 49152 || h->next_eph >= 65500) h->next_eph = 49152;
    return h->next_eph++;
}

/* Is any socket on this host already using that local port? */
static bool port_taken(Net *n, int node, uint8_t proto, uint16_t p)
{
    for (int i = 0; i < NET_SOCK_MAX; i++)
        if (n->sock[i].used && n->sock[i].node == node &&
            n->sock[i].proto == proto && n->sock[i].lport == p) return true;
    return false;
}

/* ------------------------------------------------------------------- UDP */
int net_udp_open(Net *n, int node, uint16_t port)
{
    Host *h = host_of(n, node);
    if (!h) return -1;
    if (!port) { do { port = eph_port(h); } while (port_taken(n, node, IP_PROTO_UDP, port)); }
    else if (port_taken(n, node, IP_PROTO_UDP, port)) return -1;
    int s = sock_alloc(n, node, IP_PROTO_UDP);
    if (s < 0) return -1;
    n->sock[s].lport = port;
    n->sock[s].state = TCP_LISTEN;   /* "open", for a datagram socket */
    return s;
}

static int udp_send_from(Net *n, int sock, int force_if, uint32_t src,
                         uint32_t dst, uint16_t dport, const void *data, int len)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return -1;
    Sock *s = &n->sock[sock];
    if (len < 0 || len > 1400) return -1;
    uint8_t seg[1408];
    put16(seg + 0, s->lport);
    put16(seg + 2, dport);
    put16(seg + 4, (uint16_t)(8 + len));
    put16(seg + 6, 0);
    memcpy(seg + 8, data, (size_t)len);
    /* The pseudo-header is part of the checksum, which is why a datagram that
     * arrives at the wrong address is discarded rather than delivered. */
    uint32_t ps = (src >> 16) + (src & 0xffff) + (dst >> 16) + (dst & 0xffff) +
                  IP_PROTO_UDP + (uint32_t)(8 + len);
    uint16_t ck = cksum(seg, 8 + len, ps);
    put16(seg + 6, ck ? ck : 0xffff);
    char a[20], b[20];
    net_fmt_ip(src, a, sizeof a); net_fmt_ip(dst, b, sizeof b);
    trace(n, "%s udp %s:%u > %s:%u len %d", n->node[s->node].name,
          a, s->lport, b, dport, len);
    PingResult r = ip_output_if(n, s->node, force_if, dst, IP_PROTO_UDP, seg,
                                8 + len, 64, src, false);
    return r == PING_OK ? len : -1;
}

int net_udp_send(Net *n, int sock, uint32_t dst, uint16_t dport,
                 const void *data, int len)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return -1;
    /* The source has to be known HERE, not further down, because it is in
     * the checksum. Working it out twice -- once for the pseudo-header and
     * once for the header -- is how the two came to disagree. */
    Host *h = host_of(n, n->sock[sock].node);
    if (!h) return -1;
    uint32_t nh = 0;
    int ifx = route_pick(n, h, dst, &nh);
    if (ifx < 0) return -1;
    return udp_send_from(n, sock, -1, h->ifc[ifx].ip, dst, dport, data, len);
}

int net_udp_recv(Net *n, int sock, void *data, int cap, uint32_t *src,
                 uint16_t *sport)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return -1;
    Sock *s = &n->sock[sock];
    if (!s->dgram_len) return 0;
    int k = s->dgram_len < cap ? s->dgram_len : cap;
    memcpy(data, s->dgram, (size_t)k);
    if (src) *src = s->dgram_src;
    if (sport) *sport = s->dgram_sport;
    s->dgram_len = 0;
    return k;
}

static void udp_input(Net *n, int node, int ifx, uint32_t src, uint32_t dst,
                      const uint8_t *seg, int len)
{
    if (len < 8) return;
    uint16_t sport = get16(seg), dport = get16(seg + 2);
    int dlen = get16(seg + 4) - 8;
    if (dlen < 0 || dlen > len - 8) dlen = len - 8;
    if (get16(seg + 6)) {
        uint32_t ps = (src >> 16) + (src & 0xffff) + (dst >> 16) + (dst & 0xffff) +
                      IP_PROTO_UDP + (uint32_t)(8 + dlen);
        if (cksum(seg, 8 + dlen, ps) != 0) return;
    }
    for (int i = 0; i < NET_SOCK_MAX; i++) {
        Sock *s = &n->sock[i];
        if (!s->used || s->node != node || s->proto != IP_PROTO_UDP) continue;
        if (s->lport != dport) continue;
        if (dlen > (int)sizeof s->dgram) dlen = (int)sizeof s->dgram;
        memcpy(s->dgram, seg + 8, (size_t)dlen);
        s->dgram_len = dlen;
        s->dgram_src = src;
        s->dgram_sport = sport;
        s->dgram_if = ifx;
        return;
    }
    /* Nothing is listening. That is a port unreachable, and it is the answer
     * that tells a player "the machine is up and the service is not". */
    (void)dst;
}

/* ------------------------------------------------------------------- TCP */
#define TCP_HLEN   20
#define TCP_RTO    200      /* ms before we resend                          */
#define TCP_MSL     30      /* TIME_WAIT is twice this                      */
#define TCP_MSS   1400

static uint16_t tcp_window(Sock *s)
{
    int free_space = NET_RXBUF - s->rxlen;
    return (uint16_t)(free_space < 0 ? 0 : free_space);
}

static void tcp_send(Net *n, Sock *s, uint8_t flags, uint32_t seq,
                     const uint8_t *data, int dlen)
{
    uint8_t seg[TCP_HLEN + TCP_MSS];
    memset(seg, 0, TCP_HLEN);
    put16(seg + 0, s->lport);
    put16(seg + 2, s->rport);
    put32(seg + 4, seq);
    put32(seg + 8, (flags & TCP_ACK) ? s->rcv_nxt : 0);
    seg[12] = (TCP_HLEN / 4) << 4;
    seg[13] = flags;
    put16(seg + 14, tcp_window(s));
    if (dlen > 0) memcpy(seg + TCP_HLEN, data, (size_t)dlen);
    uint32_t ps = (s->laddr >> 16) + (s->laddr & 0xffff) +
                  (s->raddr >> 16) + (s->raddr & 0xffff) +
                  IP_PROTO_TCP + (uint32_t)(TCP_HLEN + dlen);
    put16(seg + 16, cksum(seg, TCP_HLEN + dlen, ps));

    char a[20], b[20];
    net_fmt_ip(s->laddr, a, sizeof a);
    net_fmt_ip(s->raddr, b, sizeof b);
    /* Sequence numbers are shown RELATIVE to the handshake, because that is
     * the only way a capture is readable by a person -- except on the SYN
     * itself, which carries the absolute initial number. That number is
     * drawn per connection from the world seed, so it is also the thing that
     * makes two traces of the same topology under different seeds differ. */
    if (flags & TCP_SYN)
        trace(n, "%s tcp %s:%u > %s:%u [%s%s] isn %u",
              n->node[s->node].name, a, s->lport, b, s->rport,
              "S", (flags & TCP_ACK) ? "." : "", (unsigned)seq);
    else
        trace(n, "%s tcp %s:%u > %s:%u [%s%s%s] seq %u len %d",
              n->node[s->node].name, a, s->lport, b, s->rport,
              (flags & TCP_ACK) ? "." : "",
              (flags & TCP_FIN) ? "F" : "", (flags & TCP_RST) ? "R" : "",
              (unsigned)(seq - s->snd_isn), dlen);
    ip_output(n, s->node, s->raddr, IP_PROTO_TCP, seg, TCP_HLEN + dlen, 64,
              s->laddr, false);
    s->last_tx = n->now;
}

/* Send whatever is queued and not yet on the wire, then the FIN if the
 * application has closed and everything before it has gone. */
static void tcp_pump(Net *n, Sock *s)
{
    if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT) return;
    while (s->txsent < s->txlen) {
        int win = s->rwnd - s->txsent;
        if (win <= 0) break;              /* the peer's window is shut */
        int m = s->txlen - s->txsent;
        if (m > TCP_MSS) m = TCP_MSS;
        if (m > win) m = win;
        tcp_send(n, s, TCP_ACK | TCP_PSH, s->snd_una + (uint32_t)s->txsent,
                 s->tx + s->txsent, m);
        s->txsent += m;
    }
    if (s->fin_queued && !s->fin_sent && s->txsent == s->txlen) {
        s->fin_sent = true;
        tcp_send(n, s, TCP_ACK | TCP_FIN, s->snd_una + (uint32_t)s->txlen, NULL, 0);
        s->state = (s->state == TCP_CLOSE_WAIT) ? TCP_LAST_ACK : TCP_FIN_WAIT_1;
        s->timer = n->now + 4000;
    }
}

int net_tcp_listen(Net *n, int node, uint16_t port)
{
    if (!host_of(n, node)) return -1;
    if (port_taken(n, node, IP_PROTO_TCP, port)) return -1;
    int i = sock_alloc(n, node, IP_PROTO_TCP);
    if (i < 0) return -1;
    n->sock[i].lport = port;
    n->sock[i].state = TCP_LISTEN;
    return i;
}

int net_tcp_connect(Net *n, int node, uint32_t dst, uint16_t dport)
{
    Host *h = host_of(n, node);
    if (!h) return -1;
    uint32_t nh = 0;
    int ifx = route_pick(n, h, dst, &nh);
    if (ifx < 0) return -1;
    /* Pick the port BEFORE the socket exists. Asking whether a port is taken
     * after allocating the socket finds the socket itself, and the loop that
     * skipped to the next free one never terminated. */
    uint16_t lport = eph_port(h);
    for (int guard = 0; guard < 16384 && port_taken(n, node, IP_PROTO_TCP, lport); guard++)
        lport = eph_port(h);
    int i = sock_alloc(n, node, IP_PROTO_TCP);
    if (i < 0) return -1;
    Sock *s = &n->sock[i];
    s->laddr = h->ifc[ifx].ip;
    s->raddr = dst;
    s->rport = dport;
    s->lport = lport;
    /* The initial sequence number is drawn, not zero. It is drawn from the
     * world Rng so a replay reproduces it exactly. */
    s->snd_isn = (uint32_t)(rng_next(&n->rng) & 0xffffffffu);
    s->snd_una = s->snd_isn;
    s->snd_nxt = s->snd_isn + 1;
    s->rwnd = NET_TXBUF;
    s->state = TCP_SYN_SENT;
    s->timer = n->now + 6000;
    tcp_send(n, s, TCP_SYN, s->snd_isn, NULL, 0);
    return i;
}

int net_tcp_connect_wait(Net *n, int node, uint32_t dst, uint16_t dport)
{
    int s = net_tcp_connect(n, node, dst, dport);
    if (s < 0) return -1;
    for (int i = 0; i < 2000; i++) {
        net_step(n, 1);
        if (n->sock[s].state == TCP_ESTABLISHED) return s;
        if (!n->sock[s].used || n->sock[s].state == TCP_CLOSED) { net_sock_free(n, s); return -1; }
    }
    net_sock_free(n, s);
    return -1;
}

int net_tcp_accept(Net *n, int lsock)
{
    if (lsock < 0 || lsock >= NET_SOCK_MAX || !n->sock[lsock].used) return -1;
    for (int i = 0; i < NET_SOCK_MAX; i++) {
        Sock *s = &n->sock[i];
        if (!s->used || s->listener != lsock) continue;
        if (s->accepted == 0) continue;              /* already handed over */
        if (s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT) {
            s->accepted = 0;
            return i;
        }
    }
    return -1;
}

int net_tcp_send(Net *n, int sock, const void *data, int len)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return -1;
    Sock *s = &n->sock[sock];
    if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT) return -1;
    int room = NET_TXBUF - s->txlen;
    if (len > room) len = room;
    if (len <= 0) return 0;
    memcpy(s->tx + s->txlen, data, (size_t)len);
    s->txlen += len;
    s->snd_nxt += (uint32_t)len;
    tcp_pump(n, s);
    return len;
}

int net_tcp_recv(Net *n, int sock, void *data, int cap)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return -1;
    Sock *s = &n->sock[sock];
    if (!s->rxlen) return 0;
    int k = s->rxlen < cap ? s->rxlen : cap;
    memcpy(data, s->rx, (size_t)k);
    memmove(s->rx, s->rx + k, (size_t)(s->rxlen - k));
    s->rxlen -= k;
    return k;
}

void net_tcp_close(Net *n, int sock)
{
    if (sock < 0 || sock >= NET_SOCK_MAX || !n->sock[sock].used) return;
    Sock *s = &n->sock[sock];
    if (s->state == TCP_LISTEN || s->state == TCP_CLOSED) { s->used = false; return; }
    if (s->state == TCP_SYN_SENT) { s->used = false; return; }
    s->fin_queued = true;
    tcp_pump(n, s);
}

/* An RST for a segment nobody wanted. This is what makes a closed port
 * refuse instantly instead of timing out, and the two feel completely
 * different to a player holding a stopwatch. */
static void tcp_reset(Net *n, int node, uint32_t src, uint32_t dst,
                      const uint8_t *seg)
{
    Sock t;
    memset(&t, 0, sizeof t);
    t.node = node;
    t.laddr = dst;
    t.raddr = src;
    t.lport = get16(seg + 2);
    t.rport = get16(seg + 0);
    t.rcv_nxt = get32(seg + 4) + 1;
    tcp_send(n, &t, TCP_RST | TCP_ACK, get32(seg + 8), NULL, 0);
}

static void tcp_input(Net *n, int node, int ifx, uint32_t src, uint32_t dst,
                      const uint8_t *seg, int len)
{
    (void)ifx;
    if (len < TCP_HLEN) return;
    uint32_t ps = (src >> 16) + (src & 0xffff) + (dst >> 16) + (dst & 0xffff) +
                  IP_PROTO_TCP + (uint32_t)len;
    if (cksum(seg, len, ps) != 0) return;
    uint16_t sport = get16(seg), dport = get16(seg + 2);
    uint32_t seq = get32(seg + 4), ack = get32(seg + 8);
    int hl = (seg[12] >> 4) * 4;
    if (hl < TCP_HLEN || hl > len) return;
    uint8_t flags = seg[13];
    uint16_t win = get16(seg + 14);
    const uint8_t *data = seg + hl;
    int dlen = len - hl;

    /* An exact match first: a connection is the four-tuple, and a listener
     * on the same port must not swallow a segment belonging to a child. */
    Sock *s = NULL;
    int si = -1;
    for (int i = 0; i < NET_SOCK_MAX; i++) {
        Sock *c = &n->sock[i];
        if (!c->used || c->node != node || c->proto != IP_PROTO_TCP) continue;
        if (c->state == TCP_LISTEN) continue;
        if (c->lport == dport && c->rport == sport &&
            c->raddr == src && (c->laddr == dst || c->laddr == 0)) { s = c; si = i; break; }
    }

    if (!s) {
        if (flags & TCP_RST) return;
        for (int i = 0; i < NET_SOCK_MAX; i++) {
            Sock *l = &n->sock[i];
            if (!l->used || l->node != node || l->proto != IP_PROTO_TCP) continue;
            if (l->state != TCP_LISTEN || l->lport != dport) continue;
            if (!(flags & TCP_SYN)) break;
            int ci = sock_alloc(n, node, IP_PROTO_TCP);
            if (ci < 0) return;
            Sock *c = &n->sock[ci];
            c->node = node;
            c->laddr = dst; c->raddr = src;
            c->lport = dport; c->rport = sport;
            c->listener = i;
            c->accepted = -1;
            c->service = l->service;
            c->rcv_nxt = seq + 1;
            c->snd_isn = (uint32_t)(rng_next(&n->rng) & 0xffffffffu);
            c->snd_una = c->snd_isn;
            c->snd_nxt = c->snd_isn + 1;
            c->rwnd = win ? win : 1;
            c->state = TCP_SYN_RCVD;
            c->timer = n->now + 6000;
            tcp_send(n, c, TCP_SYN | TCP_ACK, c->snd_isn, NULL, 0);
            return;
        }
        /* No socket and no listener: refuse it, honestly and immediately. */
        if (!(flags & TCP_RST)) tcp_reset(n, node, src, dst, seg);
        return;
    }

    if (flags & TCP_RST) { s->state = TCP_CLOSED; s->used = false; return; }
    if (win) s->rwnd = win;

    switch (s->state) {
    case TCP_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
            ack == s->snd_isn + 1) {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->state = TCP_ESTABLISHED;
            tcp_send(n, s, TCP_ACK, s->snd_una, NULL, 0);
        }
        return;
    case TCP_SYN_RCVD:
        if ((flags & TCP_ACK) && ack == s->snd_isn + 1) {
            s->snd_una = ack;
            s->state = TCP_ESTABLISHED;
        }
        break;
    default: break;
    }

    if (s->state == TCP_CLOSED) return;

    /* Acknowledgement: retire what the peer has confirmed. */
    if (flags & TCP_ACK) {
        uint32_t acked = ack - s->snd_una;
        if (acked > 0 && acked <= (uint32_t)s->txlen + 1) {
            int k = (int)acked;
            if (k > s->txlen) k = s->txlen;      /* the extra one is our FIN */
            if (k > 0) {
                memmove(s->tx, s->tx + k, (size_t)(s->txlen - k));
                s->txlen -= k;
                s->txsent -= k;
                if (s->txsent < 0) s->txsent = 0;
            }
            s->snd_una = ack;
        }
        if (s->state == TCP_FIN_WAIT_1 && s->fin_sent && ack == s->snd_una &&
            s->txlen == 0) s->state = TCP_FIN_WAIT_2;
        if (s->state == TCP_LAST_ACK && s->txlen == 0) {
            s->state = TCP_CLOSED;
            s->used = false;
            return;
        }
        if (s->state == TCP_CLOSING) { s->state = TCP_TIME_WAIT; s->timer = n->now + 2 * TCP_MSL; }
    }

    /* Data, in order only. Anything else is dropped and the sender's timer
     * will bring it back, which is what a real receiver does. */
    bool need_ack = false;
    if (dlen > 0) {
        if (seq == s->rcv_nxt) {
            int room = NET_RXBUF - s->rxlen;
            int take = dlen < room ? dlen : room;
            if (take > 0) {
                memcpy(s->rx + s->rxlen, data, (size_t)take);
                s->rxlen += take;
                s->rcv_nxt += (uint32_t)take;
            }
        }
        need_ack = true;
    }

    if ((flags & TCP_FIN) && seq + (uint32_t)dlen == s->rcv_nxt) {
        s->rcv_nxt++;
        need_ack = true;
        if (s->state == TCP_ESTABLISHED) s->state = TCP_CLOSE_WAIT;
        else if (s->state == TCP_FIN_WAIT_2) { s->state = TCP_TIME_WAIT; s->timer = n->now + 2 * TCP_MSL; }
        else if (s->state == TCP_FIN_WAIT_1) { s->state = TCP_CLOSING; }
    }
    if (need_ack) tcp_send(n, s, TCP_ACK, s->snd_una + (uint32_t)s->txsent, NULL, 0);
    tcp_pump(n, s);
    (void)si;
}

/* Timers: retransmission, TIME_WAIT, and the give-up on a handshake that
 * nobody is answering. Called once per tick. */
static void tcp_timers(Net *n)
{
    for (int i = 0; i < NET_SOCK_MAX; i++) {
        Sock *s = &n->sock[i];
        if (!s->used || s->proto != IP_PROTO_TCP) continue;
        switch (s->state) {
        case TCP_TIME_WAIT:
            if (n->now >= s->timer) { s->state = TCP_CLOSED; s->used = false; }
            break;
        case TCP_SYN_SENT:
        case TCP_SYN_RCVD:
            if (n->now >= s->timer) { s->state = TCP_CLOSED; s->used = false; break; }
            if (n->now - s->last_tx >= TCP_RTO)
                tcp_send(n, s, s->state == TCP_SYN_SENT ? TCP_SYN : (TCP_SYN | TCP_ACK),
                         s->snd_isn, NULL, 0);
            break;
        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT:
            if (s->txsent > 0 && n->now - s->last_tx >= TCP_RTO) {
                /* Everything from snd_una again. Go-back-N, which is what a
                 * stack with no selective acknowledgement really does. */
                s->txsent = 0;
                tcp_pump(n, s);
            }
            break;
        case TCP_FIN_WAIT_1:
        case TCP_LAST_ACK:
        case TCP_CLOSING:
            if (n->now >= s->timer) { s->state = TCP_CLOSED; s->used = false; break; }
            if (n->now - s->last_tx >= TCP_RTO)
                tcp_send(n, s, TCP_ACK | TCP_FIN, s->snd_una + (uint32_t)s->txlen, NULL, 0);
            break;
        default: break;
        }
    }
}

/* ================================================================ services
 * DHCP, DNS and HTTP as PROTOCOLS. Every one of them can be unreachable, can
 * be pointed at the wrong place, and can run out -- because each of those is
 * a state of the exchange rather than a branch in a lookup table.
 */

/* --------------------------------------------------------------- DHCP    */
#define DHCP_MAGIC 0x63825363u
#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5
#define DHCPNAK      6
#define DHCP_LEASE_MS 3600000

void net_dhcpd(Net *n, int node, uint32_t first, int count, uint32_t mask,
               uint32_t gw, uint32_t dns)
{
    Host *h = host_of(n, node);
    if (!h) return;
    h->dhcpd = true;
    h->pool_first = first;
    h->pool_count = count > NET_LEASE_MAX ? NET_LEASE_MAX : count;
    h->pool_mask = mask;
    h->pool_gw = gw;
    h->pool_dns = dns;
    for (int i = 0; i < NET_LEASE_MAX; i++) h->lease[i].used = false;
    int s = net_udp_open(n, node, 67);
    if (s >= 0) n->sock[s].service = SVC_DHCPD;
}
void net_dhcpd_stop(Net *n, int node)
{
    Host *h = host_of(n, node);
    if (!h) return;
    h->dhcpd = false;
    for (int i = 0; i < NET_SOCK_MAX; i++)
        if (n->sock[i].used && n->sock[i].node == node &&
            n->sock[i].service == SVC_DHCPD) n->sock[i].used = false;
}
int net_dhcpd_leases(const Net *n, int node)
{
    const Host *h = chost_of(n, node);
    if (!h) return 0;
    int k = 0;
    for (int i = 0; i < NET_LEASE_MAX; i++)
        if (h->lease[i].used && h->lease[i].expires > n->now) k++;
    return k;
}
uint32_t net_dhcp_lease_of(const Net *n, int node, const uint8_t mac[6])
{
    const Host *h = chost_of(n, node);
    if (!h) return 0;
    for (int i = 0; i < NET_LEASE_MAX; i++)
        if (h->lease[i].used && mac_eq(h->lease[i].mac, mac)) return h->lease[i].ip;
    return 0;
}

static int dhcp_build(uint8_t *m, int op, uint32_t xid, const uint8_t mac[6],
                      uint32_t yiaddr, uint32_t siaddr)
{
    memset(m, 0, 240);
    m[0] = (uint8_t)op; m[1] = 1; m[2] = 6; m[3] = 0;
    put32(m + 4, xid);
    put32(m + 16, yiaddr);
    put32(m + 20, siaddr);
    memcpy(m + 28, mac, 6);
    put32(m + 236, DHCP_MAGIC);
    return 240;
}
static int dhcp_opt(uint8_t *m, int off, int code, const void *v, int len)
{
    m[off++] = (uint8_t)code;
    m[off++] = (uint8_t)len;
    memcpy(m + off, v, (size_t)len);
    return off + len;
}
static int dhcp_opt32(uint8_t *m, int off, int code, uint32_t v)
{
    uint8_t b[4]; put32(b, v);
    return dhcp_opt(m, off, code, b, 4);
}
/* Find one option. Returns its length, or -1. */
static int dhcp_find(const uint8_t *m, int len, int code, const uint8_t **out)
{
    if (len < 244 || get32(m + 236) != DHCP_MAGIC) return -1;
    int i = 240;
    while (i + 1 < len) {
        int c = m[i];
        if (c == 255) return -1;
        if (c == 0) { i++; continue; }
        int l = m[i + 1];
        if (i + 2 + l > len) return -1;
        if (c == code) { if (out) *out = m + i + 2; return l; }
        i += 2 + l;
    }
    return -1;
}

/* Which address does this client get? A lease it already holds, or the first
 * free one in the pool. When there is no free one the server does not answer
 * -- a pool CAN exhaust, and when it does, the machines that were switched on
 * last are the ones with no address. */
static uint32_t dhcp_assign(Net *n, Host *h, const uint8_t mac[6])
{
    for (int i = 0; i < h->pool_count; i++)
        if (h->lease[i].used && mac_eq(h->lease[i].mac, mac)) {
            h->lease[i].expires = n->now + DHCP_LEASE_MS;
            return h->lease[i].ip;
        }
    for (int i = 0; i < h->pool_count; i++)
        if (!h->lease[i].used || h->lease[i].expires <= n->now) {
            h->lease[i].used = true;
            memcpy(h->lease[i].mac, mac, 6);
            h->lease[i].ip = h->pool_first + (uint32_t)i;
            h->lease[i].expires = n->now + DHCP_LEASE_MS;
            return h->lease[i].ip;
        }
    return 0;
}

static void dhcpd_poll(Net *n, int sock)
{
    Sock *s = &n->sock[sock];
    if (!s->dgram_len) return;
    Host *h = host_of(n, s->node);
    uint8_t m[512];
    int len = s->dgram_len;
    memcpy(m, s->dgram, (size_t)len);
    int ifx = s->dgram_if;
    s->dgram_len = 0;
    if (!h || !h->dhcpd || len < 244 || m[0] != 1) return;

    const uint8_t *o = NULL;
    if (dhcp_find(m, len, 53, &o) < 1) return;
    int type = o[0];
    uint32_t xid = get32(m + 4);
    uint8_t cmac[6];
    memcpy(cmac, m + 28, 6);
    uint32_t self = (ifx >= 0 && ifx < NET_IF_MAX) ? h->ifc[ifx].ip : 0;

    if (type == DHCPDISCOVER) {
        uint32_t give = dhcp_assign(n, h, cmac);
        if (!give) {
            trace(n, "%s dhcp pool exhausted, no offer", n->node[s->node].name);
            return;                       /* a full pool is silence */
        }
        /* The offer does not commit the lease -- the request does. We hold
         * the slot, which is exactly how a real server behaves and exactly
         * why a pool can be exhausted by machines that never came back. */
        uint8_t r[512];
        int rl = dhcp_build(r, 2, xid, cmac, give, self);
        uint8_t t = DHCPOFFER;
        rl = dhcp_opt(r, rl, 53, &t, 1);
        rl = dhcp_opt32(r, rl, 1, h->pool_mask);
        if (h->pool_gw)  rl = dhcp_opt32(r, rl, 3, h->pool_gw);
        if (h->pool_dns) rl = dhcp_opt32(r, rl, 6, h->pool_dns);
        rl = dhcp_opt32(r, rl, 51, DHCP_LEASE_MS / 1000);
        rl = dhcp_opt32(r, rl, 54, self);
        r[rl++] = 255;
        char a[20]; net_fmt_ip(give, a, sizeof a);
        trace(n, "%s dhcp offer %s", n->node[s->node].name, a);
        udp_send_from(n, sock, ifx, self, 0xffffffffu, 68, r, rl);
        return;
    }
    if (type == DHCPREQUEST) {
        uint32_t want = 0;
        const uint8_t *ro = NULL;
        if (dhcp_find(m, len, 50, &ro) == 4) want = get32(ro);
        uint32_t give = dhcp_assign(n, h, cmac);
        uint8_t r[512];
        uint8_t t;
        int rl;
        if (!give || (want && want != give)) {
            rl = dhcp_build(r, 2, xid, cmac, 0, self);
            t = DHCPNAK;
            rl = dhcp_opt(r, rl, 53, &t, 1);
            rl = dhcp_opt32(r, rl, 54, self);
            r[rl++] = 255;
            trace(n, "%s dhcp nak", n->node[s->node].name);
        } else {
            rl = dhcp_build(r, 2, xid, cmac, give, self);
            t = DHCPACK;
            rl = dhcp_opt(r, rl, 53, &t, 1);
            rl = dhcp_opt32(r, rl, 1, h->pool_mask);
            if (h->pool_gw)  rl = dhcp_opt32(r, rl, 3, h->pool_gw);
            if (h->pool_dns) rl = dhcp_opt32(r, rl, 6, h->pool_dns);
            rl = dhcp_opt32(r, rl, 51, DHCP_LEASE_MS / 1000);
            rl = dhcp_opt32(r, rl, 54, self);
            r[rl++] = 255;
            char a[20]; net_fmt_ip(give, a, sizeof a);
            trace(n, "%s dhcp ack %s", n->node[s->node].name, a);
        }
        udp_send_from(n, sock, ifx, self, 0xffffffffu, 68, r, rl);
    }
}

bool net_dhcp_client(Net *n, int node, int ifx)
{
    Host *h = host_of(n, node);
    if (!h || ifx < 0 || ifx >= NET_IF_MAX || !h->ifc[ifx].used) return false;
    int s = net_udp_open(n, node, 68);
    if (s < 0) return false;
    uint32_t xid = (uint32_t)(rng_next(&n->rng) & 0xffffffffu);

    uint8_t m[512];
    int ml = dhcp_build(m, 1, xid, h->ifc[ifx].mac, 0, 0);
    uint8_t t = DHCPDISCOVER;
    ml = dhcp_opt(m, ml, 53, &t, 1);
    m[ml++] = 255;
    trace(n, "%s dhcp discover", n->node[node].name);
    udp_send_from(n, s, ifx, 0, 0xffffffffu, 67, m, ml);

    uint32_t offer = 0, mask = 0, gw = 0, dns = 0, server = 0;
    for (int i = 0; i < 400 && !offer; i++) {
        net_step(n, 1);
        uint8_t r[512];
        int rl = net_udp_recv(n, s, r, sizeof r, NULL, NULL);
        if (rl < 244) continue;
        if (get32(r + 4) != xid || !mac_eq(r + 28, h->ifc[ifx].mac)) continue;
        const uint8_t *o = NULL;
        if (dhcp_find(r, rl, 53, &o) < 1 || o[0] != DHCPOFFER) continue;
        offer = get32(r + 16);
        if (dhcp_find(r, rl, 1, &o) == 4) mask = get32(o);
        if (dhcp_find(r, rl, 3, &o) == 4) gw = get32(o);
        if (dhcp_find(r, rl, 6, &o) == 4) dns = get32(o);
        if (dhcp_find(r, rl, 54, &o) == 4) server = get32(o);
    }
    if (!offer) { net_sock_free(n, s); return false; }

    ml = dhcp_build(m, 1, xid, h->ifc[ifx].mac, 0, server);
    t = DHCPREQUEST;
    ml = dhcp_opt(m, ml, 53, &t, 1);
    ml = dhcp_opt32(m, ml, 50, offer);
    ml = dhcp_opt32(m, ml, 54, server);
    m[ml++] = 255;
    trace(n, "%s dhcp request", n->node[node].name);
    udp_send_from(n, s, ifx, 0, 0xffffffffu, 67, m, ml);

    for (int i = 0; i < 400; i++) {
        net_step(n, 1);
        uint8_t r[512];
        int rl = net_udp_recv(n, s, r, sizeof r, NULL, NULL);
        if (rl < 244) continue;
        if (get32(r + 4) != xid || !mac_eq(r + 28, h->ifc[ifx].mac)) continue;
        const uint8_t *o = NULL;
        if (dhcp_find(r, rl, 53, &o) < 1) continue;
        if (o[0] == DHCPNAK) break;
        if (o[0] != DHCPACK) continue;
        net_if_addr(n, node, ifx, get32(r + 16), mask ? mask : net_mask_bits(24));
        if (gw) net_set_gateway(n, node, gw);
        if (dns) h->resolver = dns;
        net_sock_free(n, s);
        return true;
    }
    net_sock_free(n, s);
    return false;
}

/* ---------------------------------------------------------------- DNS    */
void net_dnsd(Net *n, int node)
{
    Host *h = host_of(n, node);
    if (!h) return;
    h->dnsd = true;
    int s = net_udp_open(n, node, 53);
    if (s >= 0) n->sock[s].service = SVC_DNSD;
}
void net_dns_record(Net *n, int node, const char *name, uint32_t ip)
{
    Host *h = host_of(n, node);
    if (!h) return;
    for (int i = 0; i < NET_ZONE_MAX; i++)
        if (!h->zone[i].used) {
            h->zone[i].used = true;
            snprintf(h->zone[i].name, sizeof h->zone[i].name, "%s", name);
            h->zone[i].ip = ip;
            return;
        }
}
void net_set_resolver(Net *n, int node, uint32_t server)
{
    Host *h = host_of(n, node);
    if (h) h->resolver = server;
}

/* A name as DNS carries it: length-prefixed labels, terminated by a zero. */
static int dns_put_name(uint8_t *p, const char *name)
{
    int off = 0;
    const char *s = name;
    while (*s) {
        const char *dot = s;
        while (*dot && *dot != '.') dot++;
        int l = (int)(dot - s);
        if (l > 63) l = 63;
        p[off++] = (uint8_t)l;
        memcpy(p + off, s, (size_t)l);
        off += l;
        s = *dot ? dot + 1 : dot;
    }
    p[off++] = 0;
    return off;
}
static int dns_get_name(const uint8_t *p, int len, int off, char *out, int cap)
{
    int o = 0;
    while (off < len && p[off]) {
        int l = p[off++];
        if (l > 63 || off + l > len) return -1;
        if (o && o < cap - 1) out[o++] = '.';
        for (int i = 0; i < l && o < cap - 1; i++) out[o++] = (char)p[off + i];
        off += l;
    }
    out[o] = 0;
    return off + 1;
}

static void dnsd_poll(Net *n, int sock)
{
    Sock *s = &n->sock[sock];
    if (!s->dgram_len) return;
    Host *h = host_of(n, s->node);
    uint8_t m[512];
    int len = s->dgram_len;
    memcpy(m, s->dgram, (size_t)len);
    uint32_t from = s->dgram_src;
    uint16_t fport = s->dgram_sport;
    s->dgram_len = 0;
    if (!h || !h->dnsd || len < 12) return;
    if (get16(m + 2) & 0x8000) return;      /* it is already an answer */
    if (get16(m + 4) != 1) return;

    char qname[128];
    int qend = dns_get_name(m, len, 12, qname, sizeof qname);
    if (qend < 0 || qend + 4 > len) return;
    uint16_t qtype = get16(m + qend);

    uint32_t ip = 0;
    for (int i = 0; i < NET_ZONE_MAX; i++)
        if (h->zone[i].used && strcmp(h->zone[i].name, qname) == 0) { ip = h->zone[i].ip; break; }

    uint8_t r[512];
    int rl = 0;
    put16(r + 0, get16(m));
    /* Standard query response, authoritative, recursion available. rcode 3
     * is NXDOMAIN, and it is a real answer: the name does not exist, which
     * is different from the server not being there. */
    put16(r + 2, (uint16_t)(0x8580 | (ip ? 0 : 3)));
    put16(r + 4, 1);
    put16(r + 6, (uint16_t)(ip ? 1 : 0));
    put16(r + 8, 0);
    put16(r + 10, 0);
    rl = 12;
    memcpy(r + rl, m + 12, (size_t)(qend + 4 - 12));
    rl += qend + 4 - 12;
    if (ip && qtype == 1) {
        put16(r + rl, 0xc00c); rl += 2;          /* a pointer back to the name */
        put16(r + rl, 1); rl += 2;               /* type A     */
        put16(r + rl, 1); rl += 2;               /* class IN   */
        put32(r + rl, 300); rl += 4;             /* ttl        */
        put16(r + rl, 4); rl += 2;
        put32(r + rl, ip); rl += 4;
    }
    trace(n, "%s dns %s -> %s", n->node[s->node].name, qname, ip ? "A" : "NXDOMAIN");
    net_udp_send(n, sock, from, fport, r, rl);
}

bool net_resolve(Net *n, int node, const char *name, uint32_t *out)
{
    Host *h = host_of(n, node);
    if (!h || !h->resolver) return false;
    int s = net_udp_open(n, node, 0);
    if (s < 0) return false;
    uint16_t id = (uint16_t)(rng_next(&n->rng) & 0xffff);
    uint8_t m[512];
    put16(m + 0, id);
    put16(m + 2, 0x0100);        /* standard query, recursion desired */
    put16(m + 4, 1);
    put16(m + 6, 0); put16(m + 8, 0); put16(m + 10, 0);
    int ml = 12 + dns_put_name(m + 12, name);
    put16(m + ml, 1); ml += 2;
    put16(m + ml, 1); ml += 2;
    trace(n, "%s dns query %s", n->node[node].name, name);
    if (net_udp_send(n, s, h->resolver, 53, m, ml) < 0) { net_sock_free(n, s); return false; }

    for (int i = 0; i < 600; i++) {
        net_step(n, 1);
        uint8_t r[512];
        int rl = net_udp_recv(n, s, r, sizeof r, NULL, NULL);
        if (rl < 12 || get16(r) != id) continue;
        int an = get16(r + 6);
        if (!an) { net_sock_free(n, s); return false; }
        char qn[128];
        int qend = dns_get_name(r, rl, 12, qn, sizeof qn);
        if (qend < 0) break;
        int off = qend + 4;
        for (int k = 0; k < an && off + 12 <= rl; k++) {
            if ((r[off] & 0xc0) == 0xc0) off += 2;
            else { char t[128]; off = dns_get_name(r, rl, off, t, sizeof t); if (off < 0) break; }
            uint16_t type = get16(r + off);
            int rdl = get16(r + off + 8);
            off += 10;
            if (type == 1 && rdl == 4) {
                if (out) *out = get32(r + off);
                net_sock_free(n, s);
                return true;
            }
            off += rdl;
        }
        break;
    }
    /* A resolver that is not there does not say so. It times out, which is
     * why "name resolution takes five seconds and then fails" is the sound
     * of a dead nameserver and not of a wrong one. */
    net_sock_free(n, s);
    return false;
}

/* --------------------------------------------------------------- HTTP    */
/* The fake internet's CONTENT lives in net_sites.c and is not touched. What
 * changes is how it gets to the browser: it is served over the TCP above,
 * out of a socket, by a daemon that can be stopped, filtered or unplugged. */
bool net_fetch(const char *ip, const char *path, Buf *out);

void net_httpd(Net *n, int node, uint16_t port)
{
    Host *h = host_of(n, node);
    if (!h) return;
    h->httpd = true;
    h->http_port = port ? port : 80;
    int s = net_tcp_listen(n, node, h->http_port);
    if (s >= 0) n->sock[s].service = SVC_HTTPD;
}

static void httpd_poll(Net *n, int sock)
{
    Sock *s = &n->sock[sock];
    if (s->state == TCP_LISTEN) return;
    if (s->service != SVC_HTTPD || s->listener < 0) return;
    if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT) return;
    if (s->accepted == 0) return;            /* somebody else is handling it */
    /* STILL SENDING THE LAST ONE. A big object leaves in as many writes as
     * the send buffer has room for, over as many milliseconds as the wire
     * takes -- which is exactly how a saturated uplink turns a two megabyte
     * file into a transfer that does not finish inside the working day. */
    if (s->svc_left > 0) {
        static const uint8_t filler[512] = { 0 };
        while (s->svc_left > 0) {
            int want = s->svc_left < (int)sizeof filler ? s->svc_left : (int)sizeof filler;
            int k = net_tcp_send(n, sock, filler, want);
            if (k <= 0) break;
            s->svc_left -= k;
        }
        if (s->svc_left == 0) s->fin_queued = true;
        tcp_pump(n, s);
        return;
    }
    if (!s->rxlen) return;

    /* Wait for the blank line that ends a request. */
    bool done = false;
    for (int i = 0; i + 1 < s->rxlen; i++) {
        if (s->rx[i] == '\n' && (s->rx[i + 1] == '\n' ||
            (s->rx[i + 1] == '\r' && i + 2 < s->rxlen && s->rx[i + 2] == '\n'))) {
            done = true; break;
        }
    }
    if (!done && s->rxlen < NET_RXBUF) return;

    char req[NET_RXBUF + 1];
    int rl = s->rxlen;
    memcpy(req, s->rx, (size_t)rl);
    req[rl] = 0;
    s->rxlen = 0;

    char path[192] = "/";
    if (strncmp(req, "GET ", 4) == 0) {
        const char *p = req + 4;
        int k = 0;
        while (*p && *p != ' ' && *p != '\r' && *p != '\n' && k < (int)sizeof path - 1)
            path[k++] = *p++;
        path[k] = 0;
    }

    /* AN OBJECT OF A GIVEN SIZE. `/n/2048` is two thousand and forty-eight
     * kilobytes of file, and it exists because the pages in net_sites.c are
     * a few hundred bytes each and a network is not tested by a few hundred
     * bytes. Every desk in a tenanted floor asks for one of these in the
     * busy period; that is what a file server is FOR, and it is the traffic
     * whose path through the building the player's architecture decides. */
    if (strncmp(path, "/n/", 3) == 0) {
        long kb = 0;
        for (const char *dp = path + 3; *dp >= '0' && *dp <= '9'; dp++)
            kb = kb * 10 + (*dp - '0');
        if (kb < 0) kb = 0;
        if (kb > 65536) kb = 65536;
        char hdr[128];
        int hl = snprintf(hdr, sizeof hdr,
                          "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\n"
                          "Content-Length: %ld\r\n\r\n", kb * 1024);
        net_tcp_send(n, sock, hdr, hl);
        s->svc_left = (int)(kb * 1024);
        trace(n, "%s http 200 %s (%ld KB)", n->node[s->node].name, path, kb);
        s->service = SVC_HTTPD;
        if (s->svc_left == 0) s->fin_queued = true;
        tcp_pump(n, s);
        return;
    }

    char selfip[20];
    net_fmt_ip(s->laddr, selfip, sizeof selfip);
    Buf body;
    buf_init(&body);
    bool have = net_fetch(selfip, path, &body);

    Buf resp;
    buf_init(&resp);
    if (have) buf_printf(&resp, "HTTP/1.0 200 OK\r\nContent-Type: text/nomml\r\n"
                                "Content-Length: %u\r\n\r\n", (unsigned)body.len);
    else      buf_printf(&resp, "HTTP/1.0 404 Not Found\r\nContent-Type: text/nomml\r\n"
                                "Content-Length: 0\r\n\r\n");
    if (have && body.len) buf_put(&resp, body.p, body.len);
    trace(n, "%s http %s %s", n->node[s->node].name, have ? "200" : "404", path);

    /* Push it into the send buffer; TCP takes it from there at whatever rate
     * the receiver's window allows, which is why a big page arrives in
     * several segments and a trace shows them. */
    int off = 0;
    while (off < (int)resp.len) {
        int k = net_tcp_send(n, sock, resp.p + off, (int)resp.len - off);
        if (k <= 0) break;
        off += k;
    }
    s->service = SVC_HTTPD;
    s->fin_queued = true;              /* HTTP/1.0: the close IS the length */
    tcp_pump(n, s);
    buf_free(&body);
    buf_free(&resp);
}

int net_http_get(Net *n, int node, uint32_t ip, uint16_t port,
                 const char *path, Buf *out)
{
    int s = net_tcp_connect_wait(n, node, ip, port ? port : 80);
    if (s < 0) return -1;
    char req[320];
    int rl = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u\r\n\r\n",
                      path && *path ? path : "/", (ip >> 24) & 255, (ip >> 16) & 255,
                      (ip >> 8) & 255, ip & 255);
    net_tcp_send(n, s, req, rl);

    Buf raw;
    buf_init(&raw);
    bool closed = false;
    for (int i = 0; i < 8000 && !closed; i++) {
        net_step(n, 1);
        if (!n->sock[s].used) { closed = true; break; }
        uint8_t b[512];
        int k;
        while ((k = net_tcp_recv(n, s, b, sizeof b)) > 0) buf_put(&raw, b, (size_t)k);
        if (n->sock[s].state == TCP_CLOSE_WAIT && !n->sock[s].rxlen) {
            net_tcp_close(n, s);
            for (int j = 0; j < 200; j++) {
                net_step(n, 1);
                if (!n->sock[s].used) break;
                while ((k = net_tcp_recv(n, s, b, sizeof b)) > 0) buf_put(&raw, b, (size_t)k);
            }
            closed = true;
        }
    }
    if (n->sock[s].used) { net_tcp_close(n, s); net_step(n, 5); if (n->sock[s].used) net_sock_free(n, s); }

    /* Split the headers off. A response with no blank line is a truncated
     * one, and it fails rather than being guessed at. */
    int status = -1;
    if (raw.len >= 12 && strncmp(raw.p, "HTTP/1.", 7) == 0)
        status = (raw.p[9] - '0') * 100 + (raw.p[10] - '0') * 10 + (raw.p[11] - '0');
    size_t body = 0;
    for (size_t i = 0; i + 1 < raw.len; i++) {
        if (raw.p[i] == '\n' && raw.p[i + 1] == '\n') { body = i + 2; break; }
        if (raw.p[i] == '\n' && i + 2 < raw.len && raw.p[i + 1] == '\r' && raw.p[i + 2] == '\n') { body = i + 3; break; }
    }
    if (out && body && body <= raw.len) buf_put(out, raw.p + body, raw.len - body);
    buf_free(&raw);
    return status;
}

/* ------------------------------------------------------------------ step */
static void service_poll(Net *n)
{
    for (int i = 0; i < NET_SOCK_MAX; i++) {
        Sock *s = &n->sock[i];
        if (!s->used) continue;
        switch (s->service) {
        case SVC_DHCPD: dhcpd_poll(n, i); break;
        case SVC_DNSD:  dnsd_poll(n, i);  break;
        case SVC_HTTPD: httpd_poll(n, i); break;
        default: break;
        }
    }
}

void net_step(Net *n, int ticks)
{
    for (int t = 0; t < ticks; t++) {
        net_tick(n);
        tcp_timers(n);
        service_poll(n);
        /* The load window. A hundred ticks is short enough that a storm is
         * visible immediately and long enough that a ping is not mistaken
         * for one. */
        if (n->now - n->window_start >= 100) {
            n->load = n->window_count;
            n->window_count = 0;
            n->window_start = n->now;
        }
    }
}

/* ============================================================= inspection
 * Everything above this line is invisible unless a program inside the
 * machine can print it. These are the functions the guest tools read
 * through, and they are the same text in every front end.
 */
/* WHAT AN INTERFACE IS CALLED. eth1 is the second socket on the back;
 * eth0.30 is a tagged subinterface riding on the first one. The two are
 * different things and a player who cannot see the box has only this name to
 * tell them apart. */
static void if_name(const Net *n, int node, int ifx, char *out, size_t cap)
{
    const Host *h = chost_of(n, node);
    int nic = net_if_nic(n, node, ifx);
    int vlan = h ? h->ifc[ifx].vlan : 0;
    if (ifx < n->node[node].nports || nic < 0) snprintf(out, cap, "eth%d", ifx);
    else if (vlan) snprintf(out, cap, "eth%d.%d", nic, vlan);
    else snprintf(out, cap, "eth%d:%d", nic, ifx);
}

void net_dump_ifaces(const Net *n, int node, Buf *out)
{
    const Host *h = chost_of(n, node);
    if (!h) return;
    for (int i = 0; i < NET_IF_MAX; i++) {
        const Iface *f = &h->ifc[i];
        if (!f->used) continue;
        char mac[20], ip[20];
        net_fmt_mac(f->mac, mac, sizeof mac);
        net_fmt_ip(f->ip, ip, sizeof ip);
        PortState ps = f->port >= 0 ? port_state(n, f->port) : PORT_NOCABLE;
        char nm[24];
        if_name(n, node, i, nm, sizeof nm);
        buf_printf(out, "%s: %s %s link/ether %s\n", nm,
                   f->up ? "UP" : "DOWN",
                   ps == PORT_UP ? "LOWER_UP" :
                   ps == PORT_NOCABLE ? "NO-CARRIER" :
                   ps == PORT_TOOLONG ? "NO-CARRIER(cable too long)" : "DOWN",
                   mac);
        if (f->ip) buf_printf(out, "    inet %s/%d\n", ip, net_mask_len(f->mask));
        else       buf_puts(out, "    inet none\n");
        if (f->vlan) buf_printf(out, "    vlan %d\n", f->vlan);
        buf_printf(out, "    RX %llu  TX %llu  dropped %llu\n",
                   (unsigned long long)f->rx_pkt, (unsigned long long)f->tx_pkt,
                   (unsigned long long)f->rx_drop);
    }
}

void net_dump_routes(const Net *n, int node, Buf *out)
{
    const Host *h = chost_of(n, node);
    if (!h) return;
    char d[20], g[20];
    for (int i = 0; i < NET_IF_MAX; i++) {
        const Iface *f = &h->ifc[i];
        if (!f->used || !f->ip || !f->mask || !f->up) continue;
        net_fmt_ip(f->ip & f->mask, d, sizeof d);
        char nm[24];
        if_name(n, node, i, nm, sizeof nm);
        buf_printf(out, "%s/%d dev %s scope link\n", d, net_mask_len(f->mask), nm);
    }
    for (int i = 0; i < NET_ROUTE_MAX; i++) {
        const Route *r = &h->rt[i];
        if (!r->used) continue;
        net_fmt_ip(r->dst, d, sizeof d);
        net_fmt_ip(r->gw, g, sizeof g);
        if (!r->mask) buf_printf(out, "default via %s\n", g);
        else if (r->gw) buf_printf(out, "%s/%d via %s\n", d, net_mask_len(r->mask), g);
        else buf_printf(out, "%s/%d dev eth%d\n", d, net_mask_len(r->mask), r->ifx);
    }
    if (h->forwarding) buf_puts(out, "ip_forward 1\n");
}

void net_dump_arp(const Net *n, int node, Buf *out)
{
    const Host *h = chost_of(n, node);
    if (!h) return;
    for (int i = 0; i < NET_ARP_MAX; i++) {
        const ArpEntry *e = &h->arp[i];
        if (!e->used) continue;
        char ip[20], mac[20];
        net_fmt_ip(e->ip, ip, sizeof ip);
        net_fmt_mac(e->mac, mac, sizeof mac);
        buf_printf(out, "%-16s %s %s\n", ip, e->pending ? "(incomplete)" : mac,
                   e->pending ? "" : "ether");
    }
}

void net_dump_sockets(const Net *n, int node, Buf *out)
{
    char l[20], r[20];
    for (int i = 0; i < NET_SOCK_MAX; i++) {
        const Sock *s = &n->sock[i];
        if (!s->used || s->node != node) continue;
        net_fmt_ip(s->laddr, l, sizeof l);
        net_fmt_ip(s->raddr, r, sizeof r);
        if (s->proto == IP_PROTO_UDP)
            buf_printf(out, "udp   %s:%-5u %-21s %s\n", s->laddr ? l : "*", s->lport,
                       "*:*", "OPEN");
        else if (s->state == TCP_LISTEN)
            buf_printf(out, "tcp   %s:%-5u %-21s %s\n", "*", s->lport, "*:*", "LISTEN");
        else {
            char rem[40];
            snprintf(rem, sizeof rem, "%s:%u", r, s->rport);
            buf_printf(out, "tcp   %s:%-5u %-21s %s\n", l, s->lport, rem,
                       tcp_state_name(s->state));
        }
    }
}

void net_dump_fdb(const Net *n, int node, Buf *out)
{
    const Switch *s = sw_of((Net *)n, node);
    if (!s) return;
    for (int i = 0; i < NET_FDB_MAX; i++) {
        const FdbEntry *e = &s->fdb[i];
        if (!e->used) continue;
        char mac[20];
        net_fmt_mac(e->mac, mac, sizeof mac);
        buf_printf(out, "%s  vlan %-4d port %-3d age %llus\n", mac, e->vlan,
                   n->port[e->port].index,
                   (unsigned long long)((n->now - e->seen) / 1000));
    }
}

void net_dump_fw(const Net *n, int node, Buf *out)
{
    const Host *h = chost_of(n, node);
    if (!h) return;
    for (int i = 0; i < NET_FW_MAX; i++) {
        const FwRule *r = &h->fw[i];
        if (!r->used) continue;
        buf_printf(out, "%-8s ", r->chain == FW_IN ? "input" :
                                 r->chain == FW_OUT ? "output" : "forward");
        if (r->proto == FW_ANY_PROTO) buf_puts(out, "any  ");
        else buf_printf(out, "%-4s ", r->proto == IP_PROTO_TCP ? "tcp" :
                                      r->proto == IP_PROTO_UDP ? "udp" :
                                      r->proto == IP_PROTO_ICMP ? "icmp" : "?");
        if (r->dport) buf_printf(out, "dport %-6u", r->dport);
        else          buf_puts(out, "any port   ");
        if (r->srcmask) {
            char a[20];
            net_fmt_ip(r->srcnet, a, sizeof a);
            buf_printf(out, " from %s/%d", a, net_mask_len(r->srcmask));
        }
        buf_printf(out, " %-6s  matched %llu\n",
                   r->action == FW_ACCEPT ? "accept" :
                   r->action == FW_DROP ? "drop" : "reject",
                   (unsigned long long)r->hits);
    }
}

/* Twenty-four sockets with nothing in them is twenty-four lines of "no link",
 * and it was printed in full every time somebody put a lead in a switch --
 * which buries the two ports that matter under the twenty-two that do not.
 * `empties` says whether they are worth the paper. */
static void dump_ports(const Net *n, int node, Buf *out, bool empties)
{
    if (node < 0 || node >= n->nnode || !n->node[node].used) return;
    int first = n->node[node].port0, quiet = 0;
    for (int i = 0; i < n->node[node].nports; i++) {
        int p = first + i;
        PortState st = port_state(n, p);
        if (!empties && st == PORT_NOCABLE && n->port[p].cable < 0) { quiet++; continue; }
        const char *w = st == PORT_UP ? "up" :
                        st == PORT_DOWN_ADMIN ? "admin down" :
                        st == PORT_TOOLONG ? "no link (run too long)" : "no link";
        buf_printf(out, "port %-2d %-24s", i, w);
        if (st == PORT_UP) {
            const Cable *c = &n->cable[n->port[p].cable];
            int mb = port_rate_mb(n, p);
            buf_printf(out, " %dMb %s %dm", mb,
                       n->port[p].duplex == DUPLEX_FULL ? "full" : "half", c->metres);
            /* The circuit is not the cable, and when they disagree the
             * player needs to know which number is which. */
            if (n->port[p].rate_mb > 0 && n->port[p].rate_mb < cable_speed_mb(c->kind, c->metres))
                buf_printf(out, " (%s carries %dMb; the circuit is %dMb)",
                           c->kind == CAB_FIBRE ? "fibre" : "the cable",
                           cable_speed_mb(c->kind, c->metres), n->port[p].rate_mb);
        }
        if (n->port[p].blocked) buf_puts(out, " STP-BLOCKING");
        if (n->node[node].kind == NODE_SWITCH) {
            if (n->port[p].mode == PORT_TRUNK) buf_printf(out, " trunk native %d", n->port[p].vlan);
            else buf_printf(out, " access vlan %d", n->port[p].vlan);
        }
        buf_printf(out, " tx %llu rx %llu drop %llu",
                   (unsigned long long)n->port[p].tx,
                   (unsigned long long)n->port[p].rx,
                   (unsigned long long)n->port[p].drops);
        /* THE EVIDENCE, not a red bar. A port that is behind prints how far
         * behind it is; a port that has thrown frames away prints how many
         * and says the reason in words, because "drop 4120" on its own is
         * the fault with no explanation. */
        uint64_t now_us = n->now * 1000ull;
        uint64_t q = n->port[p].busy_us > now_us ? n->port[p].busy_us - now_us : 0;
        if (q) buf_printf(out, " queue %llums", (unsigned long long)(q / 1000));
        if (n->port[p].qdrops)
            buf_printf(out, "\n        %llu of those drops were this port's egress "
                            "buffer full: it was\n        offered more than %dMb "
                            "would carry (peak queue %llums)",
                       (unsigned long long)n->port[p].qdrops,
                       st == PORT_UP ? port_rate_mb(n, p) : 0,
                       (unsigned long long)(n->port[p].qpeak_us / 1000));
        buf_putc(out, '\n');
    }
    if (quiet)
        buf_printf(out, "%d more socket%s on the back of it, with nothing in "
                        "%s\n", quiet, quiet == 1 ? "" : "s",
                   quiet == 1 ? "it" : "them");
}

void net_dump_ports(const Net *n, int node, Buf *out)
{
    dump_ports(n, node, out, true);
}
void net_dump_ports_used(const Net *n, int node, Buf *out)
{
    dump_ports(n, node, out, false);
}
