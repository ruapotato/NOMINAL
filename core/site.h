/* site.h — the network the PLAYER builds, in the building the generator made.
 *
 * WHAT THIS IS NOT. It is not a generated topology. There was one, for an
 * afternoon, and it was wrong for the same reason the break-fix game was
 * wrong: a network somebody else built and then broke is a puzzle with an
 * author, and the tools can name the file. David: *"we're gonna start with
 * almost nothing and you order things and you're the one gonna be building
 * the infrastructure. Not something that pre-exists that goes wrong.
 * Something that goes wrong based on your architecture and the additional
 * strains of requirements like growing customer base."*
 *
 * So on day one a site holds exactly one thing: the ISP's handoff, in the
 * MDF, because a landlord does not lay their own fibre to the exchange.
 * Everything past that socket -- every switch, every router, every metre of
 * copper, every address, every vlan -- is a call into this file.
 *
 * WHAT MAKES IT A GAME IS THE LIMITS, and every one of them is real:
 *
 *   - a switch has the ports it has. The twenty-fifth machine on a
 *     twenty-four port switch has nowhere to go, and net_cable() says so.
 *   - a subnet holds what its mask allows. A /29 runs out at six machines
 *     and a /24 at two hundred and fifty-four, by the same arithmetic, and
 *     a DHCP pool that has run out hands out nothing at all.
 *   - copper carries a hundred metres. bld_cable_all() in a tall tower
 *     produces runs longer than that, so the limit is reachable by building
 *     badly rather than by anyone authoring a fault.
 *   - a broadcast domain costs every machine in it. Nothing enforces a size:
 *     a switch floods what it does not know, every card in the domain has to
 *     look at it, and site_host_frames() counts those frames. A flat tower
 *     is measurably worse than a segmented one, and the measurement is the
 *     argument.
 *
 * DEMAND IS THE STRAIN. Tenancies arrive on a schedule derived from the
 * seed, each wanting a number of drops, sometimes a segment of their own,
 * sometimes a server. Their arithmetic is what walks the player into the
 * limits above. Nobody has to design a difficulty curve.
 *
 * EVERYTHING IS SCRIPTABLE, and that is not a nicety. Blind agent playtests
 * found roughly forty bugs in this project and they cannot navigate a 3D
 * building. site_cmd() takes one line of text and does one thing, so a whole
 * tower can be built, mis-built and diagnosed over a pipe. The 3D view will
 * call the same functions; it is never the source of truth.
 */
#ifndef NOM_SITE_H
#define NOM_SITE_H

#include "nom.h"
#include "building.h"
#include "netstack.h"

#define SITE_MAX_DEV      64
#define SITE_MAX_LINK    128
#define SITE_MAX_TENANT  200
#define SITE_PATCH_M       3     /* a patch lead at each end of every run   */

/* ---------------------------------------------------------- the catalogue */
/* What the player can order. The port counts are the limit that bites first
 * and the prices are what the tenants have to cover. */
typedef enum {
    SDEV_UPLINK = 0,    /* the ISP handoff. Given, not bought.              */
    SDEV_SWITCH8,
    SDEV_SWITCH24,
    SDEV_ROUTER,
    SDEV_PC,
    SDEV_SERVER,
    SDEV_KIND_COUNT
} SiteDevKind;

const char *site_kind_name(int kind);
int   site_kind_by_name(const char *name);
int   site_kind_ports(int kind);      /* sockets on the back of it          */
int   site_kind_price(int kind);      /* pounds                             */
bool  site_kind_is_switch(int kind);
/* Cable, by the metre, which is why the route matters. */
int   site_cable_price(CableKind k, int metres);
const char *site_cable_name(CableKind k);

/* Why an operation refused. The player reads these, so they are the whole
 * vocabulary of "you cannot do that". */
typedef enum {
    SITE_OK = 0,
    SITE_ENODEV,      /* no such device                                     */
    SITE_ENOROOM,     /* no such room in this building                      */
    SITE_ENOPORT,     /* that device has not got that port                  */
    SITE_EBUSY,       /* something is already plugged into it               */
    SITE_ENOROUTE,    /* no cable route between those two rooms             */
    SITE_ESPACE,      /* the site is full: no more devices or cables        */
    SITE_EMONEY,      /* not enough money                                   */
    SITE_EIFACE,      /* no such interface, or it is not that kind of box   */
    SITE_ERR_COUNT
} SiteErr;
const char *site_err_text(int e);

typedef struct {
    uint8_t  kind;             /* SiteDevKind                               */
    uint8_t  floor;
    uint8_t  tenant;           /* who it belongs to; 0 = the landlord       */
    uint16_t room;             /* BLD_NOROOM for the handoff, which is outside */
    int      nports;
    int      node;             /* the netstack node                         */
    char     name[NET_NAME_MAX];
} SiteDev;

typedef struct {
    int16_t  a, b, aport, bport;
    uint16_t room_a, room_b;
    int      metres;
    int      cost;
    uint8_t  kind;             /* CableKind                                 */
    int      cable;            /* netstack cable id; -1 once pulled out     */
} SiteLink;

/* A tenancy, and what it wants. Derived from the building's own Room.tenant
 * and from the seed, so the same tower always fills the same way. */
typedef struct {
    uint8_t  floor, tenant;
    uint16_t room;             /* the first room they hold                  */
    uint8_t  drops;            /* ports they need                           */
    uint8_t  own_segment;      /* they will not share a broadcast domain    */
    uint8_t  wants_server;     /* and they want a machine of their own      */
    int      day;              /* when they move in                         */
    int      rent;             /* pounds a month                            */
} SiteTenant;

typedef struct {
    const Building *b;         /* borrowed: the caller owns the tower       */
    Net     *net;
    uint64_t seed;
    int      ndev, nlink;
    SiteDev  dev[SITE_MAX_DEV];
    SiteLink link[SITE_MAX_LINK];
    int      uplink;           /* the device that exists on day one         */
    uint32_t wan_isp, wan_you, wan_mask;
    long     money, spent;
    int      ntenant;
    SiteTenant tenant[SITE_MAX_TENANT];
    int      err;              /* why the last operation refused            */
} Site;

/* ------------------------------------------------------------- day one */
/* An empty site: the building, the ISP's socket in the MDF, and a budget.
 * There is no switch, no cable, no address and no connectivity of any kind
 * until somebody makes some. */
bool site_new(Site *s, const Building *b, uint64_t seed, long budget);
void site_free(Site *s);
void site_credit(Site *s, long amount);

/* --------------------------------------------------------- the operations */
/* These are exactly what the 3D view will call. Nothing the view can do is
 * absent from here, which is the rule that keeps the game testable. */

/* Put a box in a room. Charges for it. Returns the device, or -1 with
 * s->err set. */
int  site_install(Site *s, int kind, int room, const char *name);
/* Run a cable between two ports. The length is the tray route through the
 * building and the price is by the metre. A run past what the cable can
 * carry is NOT refused -- it is laid, it is paid for, and it does not come
 * up, which is what happens. */
int  site_cable(Site *s, int a, int aport, int b, int bport, CableKind k);
void site_uncable(Site *s, int link);
PortState site_link_state(const Site *s, int link);
/* The tray distance between two rooms, patch leads included, or -1. */
int  site_metres(const Site *s, int room_a, int room_b);

/* Configuration, one line of a real config file at a time. */
bool site_addr(Site *s, int dev, int ifx, uint32_t ip, uint32_t mask);
bool site_gateway(Site *s, int dev, uint32_t gw);
bool site_forwarding(Site *s, int dev, bool on);
/* A tagged subinterface: how one router terminates many subnets down one
 * trunk, which is how anybody with more vlans than sockets does it. */
bool site_subif(Site *s, int dev, int ifx, int nic, int vlan,
                uint32_t ip, uint32_t mask);
bool site_port_vlan(Site *s, int dev, int port, int vlan);
bool site_port_trunk(Site *s, int dev, int port, int vlan);
bool site_dhcpd(Site *s, int dev, uint32_t first, int count, uint32_t mask,
                uint32_t gw, uint32_t dns);
bool site_dhcp(Site *s, int dev);          /* ask for a lease, for real     */
bool site_resolver(Site *s, int dev, uint32_t ns);
bool site_dnsd(Site *s, int dev);
bool site_httpd(Site *s, int dev, int port);

/* How many machines a mask has room for: 254 on a /24, 6 on a /29, and the
 * reason a floor that grows has to be re-addressed. */
int  site_hosts_in_mask(uint32_t mask);

/* --------------------------------------------------------------- lookups */
int  site_dev_by_name(const Site *s, const char *name);
int  site_room_by_name(const Site *s, const char *spec);   /* "f3.comms", "#41" */
int  site_free_port(const Site *s, int dev);               /* -1 if full     */

/* ----------------------------------------------------------- measurement */
/* Frames that arrived at a machine's network card, totalled over the site.
 * A machine in a big broadcast domain has to look at everybody's ARP; this
 * is what that costs, counted rather than asserted. */
uint64_t site_host_frames(const Site *s);
/* Everything one card has been made to look at. */
uint64_t site_dev_frames(const Site *s, int dev);

/* --------------------------------------------------------------- demand */
/* The order tenancies move in, and what each of them wants. */
int  site_demand_upto(const Site *s, int day, int *out, int cap);
void site_dump_demand(const Site *s, Buf *out);

/* ------------------------------------------------------------ inspection */
void site_dump(const Site *s, Buf *out);
void site_dump_links(const Site *s, Buf *out);
void site_dump_rooms(const Site *s, int floor, Buf *out);
void site_dump_dev(Site *s, int dev, Buf *out);

/* ONE LINE OF TEXT, ONE OPERATION. The whole game, over a pipe. Returns
 * false only when the line was not understood; a refusal is a true return
 * with the reason written into `out`, because a refusal is an answer. */
bool site_cmd(Site *s, const char *line, Buf *out);

/* The gate. See core/sitecheck.c. */
int  site_selfcheck(void);
/* And the session on top of it, scored into the same total. */
int  session_selfcheck(int *passed, int *total);

#endif /* NOM_SITE_H */
