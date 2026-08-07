/* netstack.h — a real network, from the copper up.
 *
 * WHY THIS IS NOT A REACHABILITY MODEL.
 *
 * Every other system in this project is emulated rather than described: the
 * cpu executes instructions, the loader resolves real sonames, the boot chain
 * fails at the stage where a real file is really wrong. A network that
 * answered "can A reach B?" from a graph search would be the one place the
 * game asked the player to trust a rule nobody can inspect -- and the faults
 * worth diagnosing are precisely the ones that no rule predicts.
 *
 * So there are frames. A frame is bytes. It leaves a port, travels a cable
 * that has a length, arrives at another port, and is looked at by whatever is
 * on the other end. A switch does not know about IP. IP does not know about
 * cables. Nothing above a layer is allowed to reach past it for an answer the
 * layer below would have refused to give, because every interesting fault in
 * this file is exactly that refusal happening for real:
 *
 *   - a wrong netmask reaches on-net and fails off-net, because the ARP
 *     decision is arithmetic on the address and not a rule anyone wrote;
 *   - a duplicate address answers two ARP requests and the fdb flaps, because
 *     the switch learns from whatever spoke last;
 *   - a VLAN mismatch drops a frame at the ingress of a switch port, so the
 *     cable is up, the link light is on, and nothing works;
 *   - a loop with no spanning tree floods a broadcast back to the switch that
 *     sent it, forever, and the queue fills. Nobody special-cased a storm.
 *     A storm is what a loop DOES.
 *
 * COST. A tower may hold hundreds of machines, so per-host state is fixed and
 * small (about 1 KB: interfaces, routes, an ARP cache, firewall rules). The
 * expensive parts -- socket buffers, the frame queue, the trace ring -- are
 * global pools sized for the whole world, because at any instant almost no
 * host has a socket open and only one frame is on any given wire.
 *
 * DETERMINISM. One Rng, seeded from the world seed. No pointer values enter
 * any decision, no host clock is read, and the frame queue is drained in a
 * fixed order with ties broken by arrival index. Same seed, same trace.
 *
 * (The file is netstack.c and not net.c because core/net.c is already the
 * playtest socket listener -- the one place this program talks to the real
 * host network, which is exactly what this file is not.)
 */
#ifndef NOM_NETSTACK_H
#define NOM_NETSTACK_H

#include "nom.h"

/* ------------------------------------------------------------------ sizes */
/* SIZED FOR A TENANTED TOWER, not for a lab bench. Every desk a tenancy
 * moves in is a real card in a real broadcast domain, and thirty-six
 * tenancies asking for three hundred and fifty drops is what the building
 * generator really produces. The pools below are the whole world: one
 * allocation, no growth, and net_world_bytes() prints what it cost. */
#define NET_NODES_MAX   400     /* hosts and switches together              */
#define NET_PORTS_MAX  1200     /* global pool; a switch takes many         */
#define NET_CABLES_MAX  600
#define NET_SWPORTS      24     /* the biggest switch we sell               */
/* HOLES IN THE BACK OF THE BOX, and interfaces configured on them. They are
 * not the same number and a building is what proves it: a core router
 * terminates one subnet per tenant, and it does that down ONE trunk cable
 * with a tagged subinterface per vlan. Four sockets is what the hardware
 * has; thirty-four is what may be configured on them. Only the sockets cost
 * a port out of the global pool, which is why this split exists at all --
 * allocating NET_IF_MAX ports per host put a twelve-floor tower over
 * NET_PORTS_MAX with nothing plugged into any of them. */
#define NET_HOST_NICS     4     /* physical sockets on one host             */
#define NET_IF_MAX       34     /* interfaces, incl. tagged subinterfaces   */
/* A ROUTER'S NEIGHBOUR TABLE, and it has to be bigger than the building.
 * At sixty-four this was the first thing to break in a tenanted tower and it
 * broke silently: a router terminating a floor of desks evicted an entry per
 * new conversation, re-ARPed for a neighbour it had known a millisecond ago,
 * and every transfer in the building stalled at about seventy desks -- which
 * looked exactly like congestion and was not. Real gear holds thousands. */
#define NET_ARP_MAX     512
#define NET_ROUTE_MAX     8
#define NET_FW_MAX       12
#define NET_FDB_MAX     512     /* forwarding entries on one switch         */
#define NET_SWITCH_MAX   48     /* how many nodes may be switches           */
/* Sockets are no longer rare. A floor of desks pulling files at the same
 * moment is a hundred connections, each with a client end, a server end and
 * a listener above it, and the busy period is precisely when they all exist
 * at once.
 *
 * AND A DESK IS NOT ONE CONNECTION. A machine on a desk pulls its page and
 * its file at the same time, the way a machine does, so the busy period of a
 * full tower is two connections per desk with an end at each side of each:
 * three hundred and fifty drops is what the building generator really makes,
 * so fourteen hundred sockets is what a full tower really wants. At eight
 * hundred the pool ran out before the network did, and a connection that
 * cannot be opened because the world is out of sockets is a bottleneck
 * nobody built and nobody can see -- exactly the kind of fake ceiling the
 * sixty-four entry ARP cache was. */
#define NET_SOCK_MAX   2000
#define NET_LEASE_MAX   254     /* a /24 of pool, which is what one is     */
/* POOLS ON ONE BOX, and why there is more than one. A DHCP server serves a
 * SEGMENT, and a router with a subinterface per vlan is on several of them
 * at once -- so it plausibly runs a pool per vlan, or serves one vlan and
 * not the others. One pool per box meant the router that served the first
 * tenancy answered every other tenancy's broadcast with the first tenancy's
 * addresses, on a segment its pool had nothing to do with. */
#define NET_POOL_MAX      8
#define NET_ZONE_MAX     64
#define NET_ALIAS_MAX    64     /* extra addresses, pooled across the world */
#define NET_QUEUE_MAX  4096     /* frames in flight anywhere in the world   */
/* WHAT A PORT WILL HOLD BACK. Bytes of egress buffer on one socket: past
 * this the port tail-drops, and the drop is counted on the port, which is
 * where netstat -P will show it. Forty-eight kilobytes is an ordinary
 * per-port buffer on ordinary switching silicon, and it is the number that
 * decides whether an oversubscribed uplink merely gets slow or starts
 * losing things. */
#define NET_PORT_BUFFER 49152
#define NET_FRAME_MAX  1518     /* 1500 MTU + 14 ethernet + 4 for a tag     */
#define NET_TRACE_MAX   512
#define NET_TRACE_LINE   96
/* THE FRAME CAPTURE, which is a different thing from the trace above. The
 * trace is one line per EVENT, taken wherever in the stack the event
 * happened, and it is the whole world's -- every node's events are in the
 * one ring. tcpdump(8) needs something else: one line per FRAME, taken at
 * the card of ONE machine, in the direction it crossed it, with the fields
 * that were really in the header. That is what this ring holds. */
#define NET_PCAP_MAX    256
#define NET_PCAP_LINE   160
/* THE RECEIVE WINDOW, AND IT IS THE REAL ONE. A connection cannot go faster
 * than a window per round trip, so this number and the per-hop millisecond
 * in port_tx together decide what one transfer can do: twelve kilobytes over
 * a six millisecond round trip is about sixteen megabits, which is what one
 * desk pulling one file really gets on a healthy LAN in this world. It is
 * here rather than buried because it is the ceiling every other number in
 * the load model has to live under. */
#define NET_RXBUF      12288
#define NET_TXBUF      12288
#define NET_NAME_MAX     24

/* ------------------------------------------------------------------- L1  */
/* Cable types. The length matters (the building generator hands us real
 * distances) because copper has a limit and exceeding it is a fault the
 * player builds rather than one we hide. */
typedef enum {
    CAB_CAT5E = 0,     /* 1 Gb, 100 m                                       */
    CAB_CAT6,          /* 1 Gb, 100 m; 10 Gb to 55 m                        */
    CAB_FIBRE,         /* 10 Gb, 2000 m                                     */
    /* A HUNDRED MEGABIT, AND CHEAP. Cat 5 is still in a great many walls
     * and is still the cheapest thing on the shelf, and a run of it under a
     * floor of desks is the most ordinary bottleneck there is. It is last in
     * this list because the numbers above it were already written down and
     * an enum whose values move is a bug in every save file. */
    CAB_CAT5,          /* 100 Mb, 100 m                                     */
    CAB_KIND_COUNT
} CableKind;

typedef enum { DUPLEX_HALF = 0, DUPLEX_FULL = 1 } Duplex;

/* Why a port is not passing traffic. The player reads these words, so they
 * are the vocabulary of every link problem in the game. */
typedef enum {
    PORT_DOWN_ADMIN = 0,   /* somebody turned it off                        */
    PORT_NOCABLE,          /* nothing is plugged in                         */
    PORT_TOOLONG,          /* the run is past what the cable can carry      */
    PORT_UP
} PortState;

/* ------------------------------------------------------------------- L2  */
#define ETH_HLEN      14
#define ETH_P_IP      0x0800
#define ETH_P_ARP     0x0806
#define ETH_P_8021Q   0x8100
#define VLAN_NONE     0       /* an access port with no vlan set is vlan 1  */
#define VLAN_DEFAULT  1
/* THE RANGE OF A VLAN ID, in one place. 802.1Q spends twelve bits on it and
 * reserves 0 and 4095, so 1..4094 is what a real switch takes -- and it is
 * what site_subif() has always taken. A trunk's allowed set is a bit per id
 * so that a trunk can carry any vlan a subinterface can wear. */
#define VLAN_ID_MAX   4094
#define VLAN_WORDS    (((VLAN_ID_MAX + 1) + 31) / 32)

typedef enum { PORT_ACCESS = 0, PORT_TRUNK = 1 } PortMode;

/* ------------------------------------------------------------------- L3  */
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP  17

#define ICMP_ECHOREPLY   0
#define ICMP_UNREACH     3
#define ICMP_ECHO        8
#define ICMP_TIMXCEED   11
/* Codes under ICMP_UNREACH. The player diagnoses with these: "network
 * unreachable" comes from a router with no route, "host unreachable" from the
 * last hop when ARP got no answer, and they mean genuinely different repairs. */
#define ICMP_UNREACH_NET   0
#define ICMP_UNREACH_HOST  1
#define ICMP_UNREACH_PORT  3

/* What a ping came back as. */
typedef enum {
    PING_OK = 0,
    PING_TIMEOUT,          /* nothing came back at all                      */
    PING_NET_UNREACH,      /* a router said it had no route                 */
    PING_HOST_UNREACH,     /* the last hop got no ARP answer                */
    PING_TTL_EXCEEDED,     /* a loop in the routing, counted down honestly  */
    PING_NO_ROUTE,         /* our own routing table has nothing to try      */
    PING_IF_DOWN           /* the interface it would leave by is not up     */
} PingResult;

/* ------------------------------------------------------------------- L4  */
typedef enum {
    TCP_CLOSED = 0, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RCVD, TCP_ESTABLISHED,
    TCP_FIN_WAIT_1, TCP_FIN_WAIT_2, TCP_CLOSE_WAIT, TCP_CLOSING,
    TCP_LAST_ACK, TCP_TIME_WAIT, TCP_STATE_COUNT
} TcpState;

const char *tcp_state_name(TcpState s);

#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10

/* -------------------------------------------------------------- firewall */
typedef enum { FW_IN = 0, FW_OUT, FW_FORWARD } FwChain;
typedef enum { FW_ACCEPT = 0, FW_DROP, FW_REJECT } FwAction;
#define FW_ANY_PROTO  0
#define FW_ANY_PORT   0

/* ------------------------------------------------------------------ node */
typedef enum { NODE_HOST = 0, NODE_SWITCH } NodeKind;

typedef struct Net Net;

/* --------------------------------------------------------------- world   */
Net  *net_new(uint64_t seed);
void  net_free(Net *n);
/* Advance the world by `ticks`. One tick is one millisecond of wire time. */
void  net_step(Net *n, int ticks);
uint64_t net_now(const Net *n);

/* --------------------------------------------------------------- L1      */
int   net_add_host(Net *n, const char *name);
/* A box with the sockets it really has on the back of it, each of them a
 * real card. A pc has one and a router has four, and `show` and `netstat`
 * are then counting the same holes. */
int   net_add_host_nics(Net *n, const char *name, int nics);
int   net_add_switch(Net *n, const char *name, int nports);
/* ------------------------------------------------- A HOLE NOBODY CAN SEE
 * A socket on the back of a box that is not one of the holes in the room.
 *
 * The ISP's handoff has ONE customer port -- that is the whole shape of the
 * first decision the player makes, and giving it a second one they could
 * cable into would undo it. It also has, obviously, a way out to the rest of
 * the ISP, because otherwise the box on the wall is not a handoff, it is a
 * wall. This is that way out: a real card with a real MAC on a real port
 * that carries real frames, allocated OUTSIDE the node's contiguous run of
 * sockets so that net_node_ports(), `show`, `cable` and every count of what
 * is free are unchanged by it.
 *
 * Returns the interface index, or -1. Cable it with net_wan_cable(). */
int   net_wan_nic(Net *n, int node);
int   net_wan_cable(Net *n, int a, int aifx, int b, int bport, int metres,
                    CableKind k);
int   net_node_count(const Net *n);
const char *net_node_name(const Net *n, int node);
int   net_node_ports(const Net *n, int node);
/* Run a cable. `metres` is the real distance through the building. Returns a
 * cable id, or -1 if either port is already occupied. */
int   net_cable(Net *n, int a, int aport, int b, int bport, int metres, CableKind k);
/* Pull it out. Frames already on the wire are lost, because they are. */
void  net_uncable(Net *n, int cable);
void  net_port_admin(Net *n, int node, int port, bool up);
/* HOW MUCH THIS PORT HOLDS WHILE IT WAITS FOR THE WIRE, in bytes. 0 restores
 * NET_PORT_BUFFER, which is what every port that nobody sets is. This is the
 * one axis on which a dear switch is genuinely better than a cheap one with
 * the same number of holes: bandwidth decides how fast a queue drains and
 * this decides how much burst it can absorb before it drops, which is what
 * voice and a web host's opening rush actually die of. */
void  net_port_set_buffer(Net *n, int node, int port, uint32_t bytes);
uint32_t net_port_buffer(const Net *n, int node, int port);
PortState net_port_state(const Net *n, int node, int port);
int   net_port_speed(const Net *n, int node, int port);   /* Mb/s, 0 if down */
Duplex net_port_duplex(const Net *n, int node, int port);
void  net_port_set_duplex(Net *n, int node, int port, Duplex d);
uint64_t net_port_tx(const Net *n, int node, int port);
uint64_t net_port_rx(const Net *n, int node, int port);
uint64_t net_port_drops(const Net *n, int node, int port);
/* ------------------------------------------------------- what a wire costs
 * A frame is bytes, and bytes take time to clock onto copper: 1514 of them
 * is twelve microseconds of a gigabit and a hundred and twenty-one of a
 * hundred megabit. Nothing above L1 knows that, and everything above L1
 * feels it, which is the whole point.
 *
 * A port that is still busy with the last frame holds the next one in its
 * egress buffer. That wait is real latency -- it lands in the round trip a
 * ping prints -- and a buffer that is already full tail-drops, on the port,
 * into the counter net_dump_ports already prints. There is no second load
 * model anywhere: congestion here is the same frames, later or never. */
/* Microseconds of frames this port has queued but not yet clocked out. 0 on
 * an idle port; tens of thousands on one that is being asked for more than
 * it can carry. */
uint64_t net_port_queue_us(const Net *n, int node, int port);
/* Microseconds this port has spent actually transmitting, ever. Divided by
 * elapsed wire time that is utilisation, and it is measured rather than
 * modelled. */
uint64_t net_port_busy_us(const Net *n, int node, int port);
/* Start the utilisation stopwatch again. The tx/rx/drop counters are NOT
 * touched: those are lifetime counters on a real switch and a player reads
 * them expecting them to be. */
void  net_port_busy_reset(Net *n, int node, int port);
/* WHY A PORT DROPPED, not just how many times. net_port_drops() is the
 * total; these four are the causes it is made of, and they sum to it. A
 * report that names a cause must read the cause, because "drop 1804" with
 * the wrong explanation beside it is worse than "drop 1804" on its own.
 *   qdrops   the egress buffer would not hold the wait
 *   nolink   offered to a port with no link: the frame never left
 *   swdrops  refused on ingress: blocked port, tagged frame on an access
 *            port, or a vlan the trunk does not carry
 *   worldq   the world ran out of in-flight frame slots                 */
uint64_t net_port_qdrops(const Net *n, int node, int port);
uint64_t net_port_nolink(const Net *n, int node, int port);
uint64_t net_port_swdrops(const Net *n, int node, int port);
uint64_t net_port_worldq(const Net *n, int node, int port);
/* The deepest a port's egress queue has been, in microseconds. At a gigabit
 * a 48 KB buffer is 393us and at ten it is 39us, so this is a microsecond
 * number and rounding it to milliseconds prints 0 for a port that is
 * dropping. */
uint64_t net_port_qpeak_us(const Net *n, int node, int port);
/* THE CIRCUIT, WHICH IS NOT THE CABLE. What an ISP hands over is not the
 * speed of the fibre in the street, it is what they have sold you, and the
 * media converter on the wall is what enforces it. Rate-limit a port to
 * `mb` megabits; 0 puts it back to whatever the cable can carry. */
void  net_port_rate(Net *n, int node, int port, int mb);
int   net_port_rate_of(const Net *n, int node, int port);

/* --------------------------------------------------------------- L2      */
void  net_set_mac(Net *n, int node, int ifx, const uint8_t mac[6]);
void  net_get_mac(const Net *n, int node, int ifx, uint8_t out[6]);
/* Which port an interface hangs off. A host with two NICs has two. */
void  net_if_port(Net *n, int node, int ifx, int port);
void  net_if_up(Net *n, int node, int ifx, bool up);
void  net_port_vlan(Net *n, int node, int port, int vlan);
void  net_port_mode(Net *n, int node, int port, PortMode m);
/* Let a vlan across a trunk. A trunk carries nothing until told to. Returns
 * false for a vlan outside 1..4094 or a port that is not there, so a caller
 * can refuse the line instead of answering "set" to nothing -- which is what
 * it did for every vlan above 32 until the allowed set became a bitmap. */
bool  net_trunk_allow(Net *n, int node, int port, int vlan);
/* Take one back off, and take them all off. A setting that can only be added
 * to cannot be corrected, only added to. */
bool  net_trunk_deny(Net *n, int node, int port, int vlan);
void  net_trunk_clear(Net *n, int node, int port);
/* READ IT BACK. net_trunk_allows() is one vlan; net_trunk_allowed() fills
 * `out` with the ids in ascending order and returns HOW MANY THERE ARE,
 * which may be more than `cap` -- so a caller that wants to know it did not
 * get all of them can. Both report the allowed set only: the native vlan
 * crosses a trunk untagged whether or not it is in that set. */
bool  net_trunk_allows(const Net *n, int node, int port, int vlan);
int   net_trunk_allowed(const Net *n, int node, int port, int *out, int cap);
/* "native 1 allows 11-23 (13 vlans)" -- the one printer for that fact, so
 * `show <box>` and the verb that sets it cannot say different things. */
void  net_dump_trunk(const Net *n, int node, int port, Buf *out);
/* Tag frames leaving a host interface, for a machine plugged into a trunk. */
void  net_if_vlan(Net *n, int node, int ifx, int vlan);
/* A TAGGED SUBINTERFACE ON A CARD. Returns the interface index, existing or
 * new, or -1. It adds one; it never overwrites the card underneath, which is
 * the difference between a router with a WAN side and a LAN side and a router
 * with one address. Subinterfaces are numbered above the sockets. */
int   net_if_subif(Net *n, int node, int nic, int vlan);
/* Remove one. A socket cannot be removed -- it is a hole in a box. */
bool  net_if_del(Net *n, int node, int ifx);
/* Which socket an interface hangs off (-1 if none), what tag it wears, and
 * whether it is there at all. */
int   net_if_nic(const Net *n, int node, int ifx);
int   net_if_get_vlan(const Net *n, int node, int ifx);
/* What that interface is called on the box -- eth1, or eth0.13 for a tagged
 * subinterface. The one name a player who cannot see the box has to tell a
 * socket from a vlan riding on it, so everything that prints an interface
 * prints this and they cannot drift. */
void  net_if_name(const Net *n, int node, int ifx, char *out, size_t cap);
bool  net_if_exists(const Net *n, int node, int ifx);
int   net_fdb_count(const Net *n, int node);
/* Forget everything learned. A real switch command, and the honest way to
 * find out whether a problem is a stale forwarding entry: clear it and see
 * whether the switch relearns the same thing. */
void  net_fdb_flush(Net *n, int node);
/* -1 if this switch has not learned that address in that vlan. */
int   net_fdb_lookup(const Net *n, int node, const uint8_t mac[6], int vlan);
void  net_stp(Net *n, int node, bool on);

/* --------------------------------------------------------------- L3      */
uint32_t net_ip(int a, int b, int c, int d);
uint32_t net_mask_bits(int bits);
int   net_mask_len(uint32_t mask);
bool  net_parse_ip(const char *s, uint32_t *out);
void  net_fmt_ip(uint32_t ip, char *out, size_t cap);
void  net_fmt_mac(const uint8_t mac[6], char *out, size_t cap);

void  net_if_addr(Net *n, int node, int ifx, uint32_t ip, uint32_t mask);
/* A SECOND ADDRESS ON THE SAME INTERFACE. One machine answering for many
 * addresses is ordinary -- a web server with forty virtual hosts on it does
 * exactly this -- and it is how the whole fake internet fits on one box
 * without pretending each of its sites is a separate machine. Aliases are
 * held in a small pool for the world rather than per host, because almost no
 * host has one. */
bool  net_if_alias(Net *n, int node, uint32_t ip);
/* Give every one of them back. A box that stops being thirty web servers has
 * to stop answering for their addresses, or it goes on being them. */
void  net_if_alias_clear(Net *n, int node);
uint32_t net_if_get_addr(const Net *n, int node, int ifx);
uint32_t net_if_get_mask(const Net *n, int node, int ifx);
void  net_route_add(Net *n, int node, uint32_t dst, uint32_t mask, uint32_t gw, int ifx);
void  net_route_clear(Net *n, int node);
void  net_set_gateway(Net *n, int node, uint32_t gw);
/* WHAT IS ON THE BOX NOW, so that something else can write it down. A site
 * device that grows a real operating system has to have the address, the
 * mask and the gateway the player already gave it copied onto its disk --
 * otherwise its own netd would configure the card from a file that says
 * something else, and the machine would disagree with the network it is
 * plugged into. 0 when there is no default route. */
uint32_t net_get_gateway(const Net *n, int node);
uint32_t net_get_resolver(const Net *n, int node);
void  net_forwarding(Net *n, int node, bool on);
void  net_arp_flush(Net *n, int node);
/* Forget one neighbour. False when there was no such entry. */
bool  net_arp_del(Net *n, int node, uint32_t ip);
int   net_arp_count(const Net *n, int node);
/* Resolve for real: emits a request, runs the world, waits for a reply. */
bool  net_arp_resolve(Net *n, int node, uint32_t ip, uint8_t out[6]);
bool  net_arp_cached(const Net *n, int node, uint32_t ip, uint8_t out[6]);
PingResult net_ping(Net *n, int node, uint32_t dst, int *rtt_ms);
const char *net_ping_text(PingResult r);
/* How many hops away, the way traceroute counts: send with a rising TTL and
 * read the ICMP that comes back. */
int   net_traceroute(Net *n, int node, uint32_t dst, uint32_t *hops, int maxhops);

/* --------------------------------------------------------------- L4      */
int   net_udp_open(Net *n, int node, uint16_t port);
int   net_udp_send(Net *n, int sock, uint32_t dst, uint16_t dport,
                   const void *data, int len);
int   net_udp_recv(Net *n, int sock, void *data, int cap, uint32_t *src,
                   uint16_t *sport);
int   net_tcp_listen(Net *n, int node, uint16_t port);
/* Non-blocking: returns a socket in SYN_SENT. Drive it with net_step. */
int   net_tcp_connect(Net *n, int node, uint32_t dst, uint16_t dport);
/* Connect and run the world until the handshake finishes or times out. */
int   net_tcp_connect_wait(Net *n, int node, uint32_t dst, uint16_t dport);
int   net_tcp_accept(Net *n, int lsock);
int   net_tcp_send(Net *n, int sock, const void *data, int len);
int   net_tcp_recv(Net *n, int sock, void *data, int cap);
void  net_tcp_close(Net *n, int sock);
TcpState net_tcp_state(const Net *n, int sock);
int   net_sock_node(const Net *n, int sock);
void  net_sock_free(Net *n, int sock);
/* Every socket on this machine, gone. What a reboot does, and what applying
 * a new configuration has to do before it opens the new ones -- otherwise
 * the old listener is still bound and the port looks taken. */
void  net_close_all(Net *n, int node);
/* EVERYBODY WENT HOME. Free every TCP connection anywhere in the world that
 * has had no traffic for `idle_ms` of wire time, leaving listeners alone.
 * A stack really does give up on a connection nobody is using, and without
 * it a busy period that ended with three hundred half-finished transfers
 * leaves three hundred sockets held for the next one -- which looks exactly
 * like a network that has stopped working, and is not. Returns how many. */
int   net_tcp_reap(Net *n, int idle_ms);
/* Take a machine off the network without forgetting it exists: sockets shut,
 * addresses and routes gone, cables out, but the node and its factory MAC
 * kept so that plugging the same box back in is the same box. That identity
 * is what makes a DHCP server hand it the lease it had before, and it is why
 * this is not the same as deleting a host. */
void  net_release_host(Net *n, int node);

/* ----------------------------------------------------------------- voice
 *
 * WHY THIS IS A DIFFERENT KIND OF TRAFFIC AND NOT A NUMBER BESIDE ONE.
 *
 * Everything else this stack can be asked for is throughput-shaped: a
 * transfer either finishes or it does not, and the thing that stops it
 * finishing is a port that is full. A call is the opposite. It needs almost
 * nothing -- a G.711 stream is 86 kb/s on the wire, a thousandth of a
 * gigabit -- and it is ruined by three things a file transfer does not even
 * notice: a packet lost (there is no time to ask for it again), a packet
 * late (the buffer that smooths the wire out has already played the silence
 * where it should have gone), and a path so long that the two people start
 * talking over each other. So a tower can be at twenty per cent utilisation,
 * with every transfer finishing, and the calls unusable -- and the fix is
 * not more bandwidth, it is where the bulk traffic goes.
 *
 * None of that is computed beside the stack. A stream is real UDP datagrams
 * at a fixed rate and size, through the same ports, the same queues and the
 * same drops as everything else; the receiver timestamps them as they land
 * and the numbers below are arithmetic on those timestamps. If the wire is
 * clear the numbers are boring, because nothing made them otherwise.
 *
 * COST: about 130 bytes of world per stream, plus the two UDP sockets a real
 * call would hold -- one at each end.
 */
/* G.711 at 20ms: 160 bytes of audio and 12 of RTP header, which is the
 * datagram every desk phone in the world sends fifty times a second. */
#define NET_VOICE_PAYLOAD  172
#define NET_VOICE_PTIME     20
/* THE DE-JITTER BUFFER, and it is what turns a late packet into silence.
 * A receiver holds the audio back by this long so that a packet which took
 * a detour still arrives before its turn to be played. A packet that lands
 * after its playout instant cannot be played at all -- the silence has
 * already gone out -- so it is discarded, exactly as a real one is. Sixty
 * milliseconds is three packets, which is an ordinary fixed buffer. It is
 * also the whole reason jitter HURTS rather than merely being measurable. */
#define NET_VOICE_JITTER_MS 60
#define NET_VOICE_MAX      128     /* concurrent calls in the world        */

typedef struct {
    int      from, to;             /* nodes: who is talking to whom        */
    uint32_t dst;                  /* the address the audio is sent to     */
    uint16_t dport;
    int      ptime_ms, payload;    /* the shape of the stream              */
    uint32_t sent, received, reordered;
    /* WHAT THE RECEIVER EXPECTED, by sequence number, which is the only way
     * a receiver can know: highest seen minus first seen plus one. */
    uint32_t expected, lost, late;
    /* CONCEALED = lost + late: audio frames that had no sound to play. This
     * is the number that IS the call quality, and it is a count of packets
     * rather than a score somebody scaled. */
    uint32_t concealed;
    int      conceal_ppm;          /* concealed per million of expected    */
    /* One-way delay, in microseconds. `base_us` is the minimum ever seen,
     * which is the path's fixed cost -- propagation, serialisation, a tick
     * a hop -- and everything above it is queueing. */
    uint32_t delay_min_us, delay_avg_us, delay_max_us, base_us;
    /* RFC 3550's interarrival jitter: the smoothed mean deviation of the
     * transit time, J += (|D| - J)/16, kept in microseconds. */
    uint32_t jitter_us;
    /* WHERE IT HURT, gathered on this stream's own packets as they crossed
     * the world rather than guessed afterwards from the busiest port. */
    int      queue_node, queue_port;  /* deepest queue one of them sat in  */
    uint32_t queue_us;
    int      drop_node, drop_port;    /* where they were thrown away       */
    uint32_t drops;
} VoiceStats;

/* Start a stream: `payload` bytes of UDP from `from` to `dst`:`dport` every
 * `ptime_ms` milliseconds, received by a socket on node `to`. The
 * destination node is named rather than looked up from the address on
 * purpose: a call to an address the routing cannot reach then shows up as a
 * stream with a hundred per cent loss, which is a fault a player can find,
 * instead of failing to start. Returns a stream id, or -1. */
int   net_voice_start(Net *n, int from, int to, uint32_t dst, uint16_t dport,
                      int payload, int ptime_ms);
/* One ordinary call: G.711, 20ms, to the usual RTP port. */
int   net_voice_call(Net *n, int from, int to, uint32_t dst);
void  net_voice_stop(Net *n, int stream);
/* Every stream with this node at either end, hung up. Called for you when a
 * host is released, because a box that has been carried out of the building
 * is not on a call. */
void  net_voice_stop_node(Net *n, int node);
bool  net_voice_active(const Net *n, int stream);
int   net_voice_count(const Net *n);
bool  net_voice_stats(const Net *n, int stream, VoiceStats *out);
/* Forget the measurement and keep talking. What a player does before
 * changing something and listening again. */
void  net_voice_reset(Net *n, int stream);
/* THE ANSWER IN WORDS. Numbers say a call is bad; this says what made it
 * bad, and names the port if a port did it. It reads the same state
 * net_voice_stats returns and invents nothing. */
void  net_voice_verdict(const Net *n, int stream, Buf *out);
/* Every stream at this node, one line each, for a tool that prints them. */
void  net_dump_voice(const Net *n, int node, Buf *out);

/* ------------------------------------------- what a machine REMEMBERS
 *
 * WHY A LIVE READING IS NOT ENOUGH, and this is the whole reason the type
 * exists. Everything above is measured on a stream that is still up. The
 * person who needs it is sitting at the desk AFTERWARDS: the busy period is
 * over, every call has been hung up, `ss` shows no sockets, and the machine
 * looks perfectly healthy because at this instant it is. A counter that
 * dies with the stream can only ever answer "is the call bad right now",
 * which is the one question nobody is at the desk to ask.
 *
 * So a stream that ENDS is folded into the machines at both ends of it,
 * exactly as an interface's tx/rx counters outlive the packets they
 * counted, and it survives standing up out of the chair because it is state
 * of the NODE and not of the Machine somebody booted to read it.
 *
 * WHAT A RUN IS. Not a "day" -- this file has never heard of days and is
 * not going to start. A run is a contiguous set of calls: the log is
 * cleared when a call starts at a machine that is not already on one, and
 * closed as each call ends. A tenancy that dials every morning and hangs up
 * every evening therefore leaves exactly yesterday's calls in it, without
 * anything above having to say so.
 *
 * THE ONE SHORTCUT, stated rather than hidden. `in` is what this machine
 * heard and measured for itself. `out` is what the FAR END heard of what
 * this machine sent, and no endpoint can know that on its own -- a real one
 * is told, in RTCP receiver reports (RFC 3550) and the VoIP metrics block
 * (RFC 3611), which carries concealment exactly like this. This world does
 * not put RTCP frames on the wire; the report is handed over when the call
 * ends. That is a shortcut in the TRANSPORT of the number and not in the
 * number: every packet it counts really crossed a port and was really
 * dropped or really late.
 */
typedef struct {
    uint32_t calls;                /* streams folded into this direction   */
    uint32_t sent;                 /* datagrams the sender put on the wire */
    uint32_t expected;             /* what the receiver could tell was sent*/
    uint32_t received;
    uint32_t lost, late, concealed;
    int      conceal_ppm;          /* of expected, over the whole run      */
    uint32_t delay_us, jitter_us;  /* the worst call of the run            */
} VoiceLeg;

typedef struct {
    bool       any;                /* a call at this machine has ended     */
    uint64_t   first_ms, last_ms;  /* wire time the run opened and closed  */
    VoiceLeg   out, in;
    bool       worst_set;
    bool       worst_out;          /* the worst call was one it SENT       */
    VoiceStats worst;              /* kept whole, so the verdict can name
                                    * the port that did it                 */
} VoiceLog;

/* The calls this machine has finished. False if it is not a host at all. */
bool  net_voice_log(const Net *n, int node, VoiceLog *out);
/* Wipe it -- what a player does before listening again. */
void  net_voice_log_clear(Net *n, int node);
/* The record in words, with the verdict on the worst call in it. This is
 * what `voice` prints on a machine with a shell. */
void  net_dump_voice_log(const Net *n, int node, Buf *out);

/* --------------------------------------------------------------- filter  */
void  net_fw_add(Net *n, int node, FwChain c, int proto, uint16_t dport,
                 uint32_t srcnet, uint32_t srcmask, FwAction a);
void  net_fw_clear(Net *n, int node);
uint64_t net_fw_hits(const Net *n, int node, int rule);
int   net_fw_count(const Net *n, int node);
/* EVERYTHING THIS BOX HAS THROWN AWAY, summed over the rules that throw
 * things away. A diagnostic that came back with nothing can compare this
 * across the attempt and say whether the far end refused it -- which is a
 * fact off the counter, not a diagnosis. */
uint64_t net_fw_drops(const Net *n, int node);

/* -------------------------------------------------------------- services */
/* A POOL IS SCOPED TO THE SEGMENT IT SERVES, and the segment is not a
 * seventh argument: it is the interface of this box whose own address is
 * inside the pool's subnet. A router with three subinterfaces can run three
 * pools, one per vlan, by calling this three times -- and a DISCOVER that
 * arrives on an interface no pool is scoped to is answered by silence,
 * however loudly it was broadcast. Returns false, and starts nothing, when
 * no interface of this box is on that subnet: a pool with no segment under
 * it has nobody to serve and would only poison somebody else's.
 * net_dhcpd_scope() answers which interface it would be. */
bool  net_dhcpd(Net *n, int node, uint32_t first, int count, uint32_t mask,
                uint32_t gw, uint32_t dns);
int   net_dhcpd_scope(const Net *n, int node, uint32_t first, uint32_t mask);
/* Stop serving addresses. Every pool on the box, the sockets with them, and
 * the leases they held. Returns how many pools were stopped, so a caller can
 * tell "stopped" from "it was not serving anything". */
int   net_dhcpd_stop(Net *n, int node);
/* What this box is serving, pool by pool, for anything that has to print it.
 * Returns false past the last pool. */
int   net_dhcpd_pools(const Net *n, int node);
bool  net_dhcpd_pool(const Net *n, int node, int i, int *ifx, uint32_t *first,
                     int *count, uint32_t *mask, uint32_t *gw, uint32_t *dns);
/* Stop everything this host serves and give the sockets back. A box that has
 * been switched off is not serving anything, and this is how the stack is
 * told so -- otherwise reconfiguring the card would put the listeners back,
 * because that is exactly what net_close_all() is now careful to do. */
void  net_services_stop(Net *n, int node);
int   net_dhcpd_leases(const Net *n, int node);
/* The same count for ONE pool, `i` being its ordinal among the used pools --
 * the ordinal net_dhcpd_pool() takes. A box with a pool per vlan needs this
 * to say which of its segments is empty. */
int   net_dhcpd_pool_leases(const Net *n, int node, int i);
/* Ask for an address the way a client does: discover, offer, request, ack.
 * Returns true only if a lease was really granted. */
bool  net_dhcp_client(Net *n, int node, int ifx);
uint32_t net_dhcp_lease_of(const Net *n, int node, const uint8_t mac[6]);

void  net_dnsd(Net *n, int node);
/* Stop being a name server: the socket goes, the zone goes, and any query
 * this box had out to its forwarder goes with them. The zone is config in
 * exactly the way a DHCP pool is, so it comes back the way a pool does --
 * off the disk, replayed by netd -- and not by surviving in the stack. */
void  net_dnsd_stop(Net *n, int node);
/* What this box is serving, for anything that has to say so out loud. A
 * tower that cannot print the services a box runs is a tower where a service
 * can quietly stop running. */
bool  net_dnsd_running(const Net *n, int node);
int   net_httpd_port(const Net *n, int node);   /* 0 when it serves nothing */
/* GIVE A NAME SERVER A NAME. Setting a name that is already in the zone
 * overwrites it rather than adding a second answer, so replaying a zone off
 * a disk twice leaves one record and not two. Returns false only when the
 * zone is full. */
bool  net_dns_record(Net *n, int node, const char *name, uint32_t ip);
/* And read it back, which is how the tower prints a zone and how it writes
 * one onto a disk. `i` runs over the used records, densely. */
int   net_dns_record_count(const Net *n, int node);
bool  net_dns_record_at(const Net *n, int node, int i, char *name, size_t cap,
                        uint32_t *ip);
void  net_set_resolver(Net *n, int node, uint32_t server);
/* WHERE A NAME SERVER SENDS WHAT IT DOES NOT KNOW. A resolver with a zone
 * and no forwarder answers NXDOMAIN to the whole internet, which is why an
 * internal DNS server that is not a forwarder is worse than none: every desk
 * pointed at it loses the world. The forwarder is this box's own resolver --
 * the one line in its own resolv.conf -- because a box that has been told
 * where to ask has been told where to ask. Zero when it has no forwarder,
 * and zero when the forwarder is one of this box's own addresses, which is
 * a loop and is not a forwarder. */
uint32_t net_dns_forwarder(const Net *n, int node);
/* WHAT CAME BACK, not merely whether something did. rcode 3 is NXDOMAIN and
 * it is an ANSWER: the server is up, it is authoritative, and the name does
 * not exist. Silence is a server that is not there. The two have completely
 * different repairs, so the two have different values here. */
typedef enum {
    RESOLVE_OK = 0,
    RESOLVE_NXDOMAIN,      /* the server answered: no such name              */
    RESOLVE_NODATA,        /* the name exists and has no address record      */
    RESOLVE_TIMEOUT,       /* nothing came back before the client gave up    */
    RESOLVE_NO_RESOLVER    /* nothing to ask: no nameserver configured       */
} ResolveResult;
ResolveResult net_resolve_ex(Net *n, int node, const char *name, uint32_t *out);
/* A real query over UDP to the configured resolver. */
bool  net_resolve(Net *n, int node, const char *name, uint32_t *out);

void  net_httpd(Net *n, int node, uint16_t port);
/* WHO IS BEHIND THIS NODE. Opaque here on purpose: netstack does not know
 * what a Machine is and must not learn. netsite.c puts one here when it pins
 * a booted box onto a node, and the web daemon above asks -- because a node
 * with a real machine behind it serves that machine's DOCUMENT ROOT, off its
 * disk, rather than a page out of a table. */
void  net_host_set_owner(Net *n, int node, void *owner);
void *net_host_owner(const Net *n, int node);
/* ------------------------------------------------------------ the internet
 * WHERE THE WEB LIVES, which is not here.
 *
 * A handoff is a box on a wall with somebody else's network behind it. Tell
 * the world which node that is and netsite.c builds the somebody else: the
 * ISP's core router, its resolver, and the machines that serve the web, on
 * their own segments, reached by real routing over the rate-limited circuit.
 * Nothing is built until the world takes its first tick, so a Site that is
 * generated and never played costs nothing. */
void  net_isp_declare(Net *n, int handoff);
int   net_isp_handoff(const Net *n);
/* What netsite.c booted out there, so that net_free can hand it back. Opaque
 * for the same reason net_host_owner is. */
/* WHICH BOX OUT THERE THE WEB COMES FROM. Everything in a tenant's day that
 * arrives "from the internet" -- an office's files, a studio's ingest, a
 * hosting company's visitors -- has to come from a machine that is really on
 * the internet, or the circuit it is supposed to cross is not crossed. Forces
 * the world to be built if it has not been. -1 when there is no internet on
 * this Net at all, which is every world but a tower's. */
int   net_isp_web_node(Net *n);
uint32_t net_isp_web_addr(Net *n);
void  net_isp_set_web(Net *n, uint32_t ip);
void  net_isp_own(Net *n, int slot, void *p);
void *net_isp_owned(const Net *n, int slot);
/* The nodes the ISP world built are found with net_node_by_name(). */
/* A real GET: connect, send a request line, read a response, tear down. */
int   net_http_get(Net *n, int node, uint32_t ip, uint16_t port,
                   const char *path, Buf *out);

/* ------------------------------------------------------------ inspection */
void  net_trace(Net *n, bool on);
void  net_trace_clear(Net *n);
int   net_trace_count(const Net *n);
const char *net_trace_line(const Net *n, int i);
void  net_dump_trace(const Net *n, Buf *out);
/* The frame capture: off until somebody asks, because a ring nobody reads is
 * memory nobody is paying for. One line per frame that really crossed a
 * card, in fields tcpdump(8) formats: see pcap_frame() in netstack.c for the
 * order, which is the only place that decides it. */
void  net_pcap(Net *n, bool on);
bool  net_pcap_on(const Net *n);
void  net_pcap_clear(Net *n);
int   net_pcap_count(const Net *n, int node);
void  net_dump_pcap(const Net *n, int node, Buf *out);
void  net_dump_ifaces(const Net *n, int node, Buf *out);
void  net_dump_routes(const Net *n, int node, Buf *out);
void  net_dump_arp(const Net *n, int node, Buf *out);
void  net_dump_sockets(const Net *n, int node, Buf *out);
void  net_dump_fdb(const Net *n, int node, Buf *out);
void  net_dump_ports(const Net *n, int node, Buf *out);
/* The same, without the sockets that have nothing in them: a twenty-four
 * port switch is two interesting lines and twenty-two of "no link". */
void  net_dump_ports_used(const Net *n, int node, Buf *out);
/* The RUNNING ruleset, with the count of what each rule has actually
 * dropped. A filter you cannot read is the one fault with no evidence at
 * all: the packet does not arrive and nothing anywhere says why. */
void  net_dump_fw(const Net *n, int node, Buf *out);
/* Frames handled anywhere in the last second of wire time. A quiet network is
 * a handful; a broadcast storm is thousands, and this is how the player sees
 * one without us ever deciding that a loop exists. */
uint64_t net_load(const Net *n);
uint64_t net_queue_drops(const Net *n);
/* What the whole world costs, in bytes. One allocation, fixed at compile
 * time, the same whether the tower holds one switch or four hundred cards. */
size_t net_world_bytes(void);
int   net_node_by_name(const Net *n, const char *name);

#endif /* NOM_NETSTACK_H */
