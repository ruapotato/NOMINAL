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

#define SITE_MAX_DEV     400
#define SITE_MAX_LINK    600
#define SITE_MAX_TENANT  200
#define SITE_MAX_JACK    200
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
    /* A DESK. The tenant's own computer, on the tenant's own desk, which
     * they carried in themselves the day they got the keys. It is not for
     * sale and it is not the player's: what the player sells is the port it
     * is plugged into. It is a real card in a real broadcast domain and it
     * generates real frames, which is the only reason any of the rest of
     * this file has anything to do. */
    SDEV_DESK,
    SDEV_KIND_COUNT
} SiteDevKind;

const char *site_kind_name(int kind);
int   site_kind_by_name(const char *name);
int   site_kind_ports(int kind);      /* sockets on the back of it          */
int   site_kind_price(int kind);      /* pounds                             */
bool  site_kind_is_switch(int kind);
/* Has an operating system in it, and therefore a power button. A switch and
 * a router are appliances: they come up with the socket they are plugged
 * into. A pc and a server are computers, and a computer that nobody has
 * switched on is not on the network. */
bool  site_kind_has_os(int kind);
/* WHAT ONE SOCKET ON THE BACK OF THAT BOX WILL CLOCK, in megabits. A link
 * runs at the slowest of the port at each end and the cable between them,
 * and this is the port half -- which nothing in this game had an opinion
 * about until D27, so a cat 6 patch lead to a desk negotiated ten gigabit
 * and the cable grade was a free choice. Desks, PCs, servers and the cheap
 * eight-port switch are gigabit; a twenty-four port switch is gigabit with
 * an SFP+ pair on ports 22 and 23; a router's four sockets are ten gigabit.
 * So ten gigabit is bought, and it is landed on the right hole. */
int   site_kind_port_mb(int kind, int port);
/* Cable, by the metre AND by the run -- the ends of a run are terminated and
 * tested by a person, and that is the larger half of what a desk drop costs.
 * Which is why the route matters and why the number of runs matters. */
int   site_cable_price(CableKind k, int metres);
const char *site_cable_name(CableKind k);
/* AND THE OTHER WAY OF PAYING FOR THE SAME METRES. A jack is that run pulled
 * once, terminated into a faceplate on the wall and punched down on a panel
 * port at the far end, and left there. It costs the copper, the labour and a
 * fit-out premium on top -- so it is always dearer than the same run off the
 * spool -- and what it buys is that the NEXT box in that room reaches the far
 * end for the price of a patch lead. See the long note above site_jack(). */
int   site_jack_price(CableKind k, int metres);
int   site_jack_lead_price(void);
/* How many days the trade takes, from the metres they have to pull. This is
 * the half of the decision that money cannot buy back: the spool is in your
 * hands now and a jack is not there until the day it is there. */
int   site_jack_days(int metres);

/* WHAT A RUN OF THAT GRADE OVER THAT MANY METRES NEGOTIATES, in megabits, or
 * 0 for a run past what the cable carries at all.
 *
 * It is not a table. Copper's reach and the speed it settles at are the
 * netstack's rules and live nowhere else, so this LAYS ONE -- two ports and a
 * cable, in a world of its own -- and reads net_port_speed off it, which is
 * the same function `show` and `load` print from. A quote that restated the
 * fifty-five metre rule in a second place would be the bug this project has
 * shipped three times already; this one cannot drift, because it is the
 * measurement.
 *
 * The PORT at each end still has the last word (site_kind_port_mb), so this
 * is the cable's half of the answer and never the whole of it. */
int   site_cable_speed(CableKind k, int metres);
/* All four grades at that distance in one pass, out[CAB_KIND_COUNT]. */
void  site_cable_speeds(int metres, int *out);

/* THE LAST TEN METRES OF WHAT COPPER CARRIES ARE THE MARGIN, and a run that
 * long takes CRC errors under load and eventually retrains to a hundred
 * megabits. The behaviour is core/siteday.c's and the number lives there;
 * this is the same number where the QUOTE can read it, and `--sitecheck`
 * plays a run at each side of it rather than trusting that the two agree. */
#define SITE_COPPER_MARGIN_M  90

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
    SITE_ECABLED,     /* it has a cable in it: you cannot walk off with it  */
    SITE_EFIXED,      /* the ISP's handoff is screwed to somebody's wall    */
    SITE_EADDR,       /* the network or broadcast address of its own subnet */
    SITE_EVLAN,       /* not a vlan number                                  */
    SITE_ENOTSW,      /* only a switch has ports with vlans on them         */
    SITE_EOFF,        /* it is switched off, and an off box is not on a net */
    SITE_ENOBTN,      /* an appliance has no power button                   */
    SITE_ESEG,        /* no interface of that box is on that pool's subnet   */
    SITE_EPOOL,       /* a pool of no addresses, or no room for another one  */
    SITE_EZONE,       /* that name server's zone is full                     */
    SITE_EEARLY,      /* the trade has not been yet: the jack is not a socket */
    SITE_EJACK,       /* that port is punched down to a jack, for good        */
    SITE_ERR_COUNT
} SiteErr;
const char *site_err_text(int e);

/* EVERY VERB THE TOWER ANSWERS TO, and the smallest number of words it can
 * do anything with. A gate walks this so that no verb can answer a short
 * line by denying it is a verb -- which is the same lie as a help text
 * naming a command that does not exist, arriving from the other side. */
int         site_verb_count(void);
const char *site_verb_name(int i);
int         site_verb_arity(int i);

typedef struct {
    uint8_t  kind;             /* SiteDevKind                               */
    uint8_t  floor;
    uint8_t  tenant;           /* who it belongs to; 0 = the landlord       */
    uint16_t room;             /* BLD_NOROOM for the handoff, which is outside */
    int      nports;
    int      node;             /* the netstack node                         */
    uint8_t  powered;          /* an OS box arrives switched off            */
    char     name[NET_NAME_MAX];
    /* ------------------------------------------- how it has been treated
     * The world breaks machines out of THESE, not out of a timer. A disk
     * that has served a floor's files for a month has done more work than
     * one nobody has touched, and it is the work that wears it out; a box
     * on a UPS rides a mains failure out. See core/siteday.c. */
    uint8_t  ups;              /* somebody bought it a battery              */
    int      run_days;         /* days it has spent switched on             */
    int      wear;             /* disk wear, in days-of-average-use         */
    uint8_t  warned;           /* its disk has started complaining in syslog*/
    uint8_t  hot_warned;       /* the room it is in has started cooking     */
    /* HOW MANY SECTORS THIS DISK HAS ALREADY LOST, and it is not a
     * statistic. The first one is kept away from the files the boot chain
     * reads, so the box comes up and can be worked on. The second one is
     * not: by then the player has had a fortnight of SMART warnings, a
     * lost file and an entry in `events`, and `disk <box>` has been a
     * hundred and forty pounds away the whole time. See core/siteday.c. */
    uint8_t  lost;
} SiteDev;

typedef struct {
    int16_t  a, b, aport, bport;
    uint16_t room_a, room_b;
    int      metres;
    int      cost;
    uint8_t  kind;             /* CableKind                                 */
    int      cable;            /* netstack cable id; -1 once pulled out     */
    /* A RUN WITH NO MARGIN LEFT IN IT. Copper carries a hundred metres and
     * the last ten of them are the ones the standard spends on margin: a
     * run that long works, and works less well every day it is asked to
     * carry a floor. `errs` is measured off how hard the port really
     * worked, so a marginal run nobody uses never degrades and a marginal
     * riser under a floor of desks does. Once `slow` is set the link
     * negotiates a hundred megabits, which `load` and `show` both print.
     * See core/siteday.c. */
    int      errs;
    uint8_t  slow;
    /* THE RUN WAS ALREADY IN THE WALL. -1 is copper you pulled yourself off
     * the spool and paid for by the metre; anything else is the jack this
     * lead is plugged into, and `cost` is then the lead and not the run. */
    int16_t  jack;
} SiteLink;

/* A PERMANENT JACK: a socket on the wall of a room, and the run behind it.
 *
 * D23 promised this and the tower never had it, so the first blind playtester
 * of the tower went looking for a verb that did not exist. The reason it is
 * worth having is that it is not a more expensive cable: it is a cable that
 * belongs to the ROOM instead of to the box. The run is pulled once, the far
 * end is punched down on a panel port that is then gone for good, and every
 * box that ever stands in that room afterwards reaches the far end for the
 * price of a factory patch lead.
 *
 * So the trade-off has two teeth and neither of them is money alone:
 *   - it costs MORE than the same run off the spool, and it costs it up
 *     front, on a floor that may never hold a second box;
 *   - and it is not there today. Somebody has to come and pull it, and that
 *     is site_jack_days() of the clock -- which is the same clock a tenancy's
 *     three days of fit-out and three strikes are counted on.
 * A player six days from a complaint cannot jack their way out of it, and a
 * player who spools every riser pays for every riser again the first time a
 * floor grows a second switch or a box moves. */
typedef struct {
    uint16_t room;             /* the wall the faceplate is on              */
    int16_t  home;             /* the box the far end is punched down into  */
    int16_t  hport;            /* and the port it holds, for good           */
    int      metres;           /* tray metres: bld_cable_all(), as the spool*/
    int      cost;             /* what the install cost. Never refunded.    */
    uint8_t  kind;             /* CableKind                                 */
    int      ordered;          /* the day the trade was booked              */
    int      ready;            /* the day it is a socket rather than a plan */
    int      link;             /* the lead in it now, as a site link, or -1 */
    int      leads;            /* leads bought for it over the whole run    */
    int      lead_spend;       /* and what they came to                     */
} SiteJack;

/* ================================================= WHAT KIND OF BUSINESS
 *
 * Until D30 every tenancy was the same business. They differed in how many
 * desks they had, whether they wanted a segment, and whether they wanted a
 * server -- and every desk did the identical thing: a page, three files,
 * three concurrent, a couple of megabytes. So a floor only ever asked one
 * question, "is there enough throughput", and a playtester who reached day
 * 62 said what that costs: *"Cable is a bill I paid with a rule, not a bill
 * I sweated. I made the riser decision on floor 1 and then repeated it on
 * floors 2 and 3 without thinking."*
 *
 * These are four businesses whose demand is a different SHAPE, so the right
 * build for one floor is the wrong build for another. Not flavour text: each
 * one is measured on a different axis of the same netstack, and each one is
 * unhappy for its own reason, in its own words, in `service`.
 *
 *   OFFICE   bursty, throughput-shaped, tolerant of latency. The baseline,
 *            and what every tenancy used to be.
 *   WEBHOST  their traffic arrives from OUTSIDE. It loads the circuit and
 *            the path IN -- the one direction nothing in this game tested --
 *            and what they are buying is uptime, not speed. A day their
 *            origin was unreachable is a day you owe them money back.
 *   VOICE    almost no bandwidth and no tolerance at all. Real UDP, 50
 *            packets a second per call, out to the carrier and back; ruined
 *            by loss, by jitter and by a queue somebody else filled.
 *   STUDIO   a content creator: sustained heavy UPLOAD against a hard
 *            deadline. A stream that arrives late is not a slow stream, it
 *            is a dropped stream, and there is no partial credit.
 *
 * THE RENT FOLLOWS. A studio pays three times an office for the same square
 * metres and will saturate an uplink nobody sized for it; a web host pays
 * two and a bit and hands the money back on a day they were down. `demand`
 * prints the kind, what they will want and the rent before the lease is
 * signed, because a price you cannot see before you sign is not a decision.
 */
typedef enum {
    TEN_OFFICE = 0,
    TEN_WEBHOST,
    TEN_VOICE,
    TEN_STUDIO,
    TEN_KIND_COUNT
} SiteTenantKind;

const char *site_tenant_kind_name(int k);      /* "office", "web host", ... */
/* One line: what this industry will ask the network for. `demand` prints it
 * per row, so the shape of the bill is legible before the lease is signed. */
const char *site_tenant_kind_wants(int k);
/* What they pay for the same square metres, as a percentage of an office.
 * The premium is the trade: a studio is worth taking if you have built for
 * it and ruinous if you have not. */
int         site_tenant_rent_pct(int k);

/* A tenancy, and what it wants. Derived from the building's own Room.tenant
 * and from the seed, so the same tower always fills the same way. */
typedef struct {
    uint8_t  floor, tenant;
    uint8_t  kind;             /* SiteTenantKind: what business they are in */
    uint16_t room;             /* the first room they hold                  */
    uint8_t  drops;            /* ports they need                           */
    uint8_t  own_segment;      /* they will not share a broadcast domain    */
    uint8_t  wants_server;     /* and they want a machine of their own      */
    int      day;              /* when they move in                         */
    int      rent;             /* pounds a month                            */
    /* ------------------------------------------------- once they are in */
    uint8_t  moved;            /* they have the keys and their desks are in */
    uint8_t  strikes;          /* consecutive days their work did not finish*/
    uint8_t  complained;       /* they have filed one. It does not un-file. */
    int      desk0, ndesk;     /* their desks, as devices in dev[]          */
    /* What the last busy period actually did. Measured from the sessions
     * that really ran, not predicted from anything.
     *
     * `tried` and `finished` are THE UNITS THIS TENANCY IS JUDGED ON, which
     * is not the same thing for every industry: transfers for an office,
     * calls for a voice business, visitor sessions for a web host, streams
     * for a studio. That is the point -- a voice tenancy is not unhappy
     * because a file transfer was slow, and `service` has to be able to say
     * so. The site-wide totals in SiteDay count every unit of work the tower
     * was asked for, whatever kind it was. */
    int      tried, finished;
    int      worst_ms;         /* the slowest desk's transfer               */
    long     bytes;            /* what their people pulled that day         */
    /* ------------------------------------- and the industry's own numbers
     * Only the ones their kind measures are filled in; the rest are zero.
     * These exist so that `service` can say WHY in the tenant's own terms
     * rather than printing "8/20" at a business that does not count
     * transfers. Every one of them was counted off the netstack. */
    int      conceal_ppm;      /* voice: audio frames with nothing to play  */
    int      jitter_us;        /* voice: RFC 3550 interarrival jitter       */
    int      delay_ms;         /* voice: worst one-way delay of any call    */
    long     up_kb;            /* studio: what really went up               */
    long     up_want_kb;       /* studio: what had to, to keep the streams  */
    uint8_t  down;             /* webhost: nothing answered from outside    */
    long     sla;              /* webhost: rent handed back for a day down  */
    /* WHICH SERVER TOOK THEIR FILES. A tenancy whose own machine is off, or
     * who never had one, is served by whatever else is powered -- their
     * floor's if there is one, otherwise anything at all, possibly six
     * floors down through a riser somebody has already filled. That is the
     * right behaviour and it used to be silent, which made the resulting
     * slowness unattributable. -1 is nobody: no server was powered. */
    int      files_dev;
} SiteTenant;

/* HOW A DAY WENT, for the whole site. Every number in here was counted
 * during the busy period; none of it is a model sitting beside the netstack.
 * `site_dump_day` prints it and `--loadcheck` asserts on it. */
typedef struct {
    int  day;
    int  tenants_in;        /* moved in                                    */
    int  tenants_served;    /* connected, addressed and finishing work     */
    int  desks, connected;
    int  sessions, finished;
    long bytes;
    long rent;              /* taken today                                 */
    long bill;              /* the circuit, on the day of the month it lands*/
    /* WHAT A WEB HOST'S OUTAGE COST YOU ON TOP OF THE RENT YOU DID NOT EARN.
     * Their contract is uptime; a day their origin answered nothing is a day
     * the landlord hands a day's rent back. Zero on every other kind. */
    long sla;
    int  worst_ms;
    uint64_t frames, drops; /* frames the site handled; frames it lost     */
    /* The port that was asked for the most, and how hard. This is the
     * summary; `show <box>` is the evidence, and it is `show` because a
     * switch port has no shell behind it to run netstat on. */
    char hot[NET_NAME_MAX + 8];
    int  hot_util;          /* percent of the busy period it was clocking  */
    int  complaints_today;
    int  events;            /* what the world did overnight                */
} SiteDay;

/* ================================= THE WORLD BREAKING THE MACHINES ========
 *
 * The requirement, standing since the pivot and in the owner's words: *"for
 * things like a blackout, could cause disk corruption and you need to find
 * the corruption and fix it much like how we already have it. We just fanning
 * out making the world a lot bigger."*
 *
 * Sixty-two fault types existed and every one was proven findable and
 * repairable -- and nothing in the running tower could cause a single one of
 * them. These are the causes. Each one damages a real machine through
 * core/breaker.c, leaves its evidence in that machine's own syslog and in the
 * site's log below, and is avoidable or survivable by something the player
 * could have bought or done differently. */
typedef enum {
    SEV_NONE = 0,
    SEV_POWERCUT,    /* the building lost mains overnight                   */
    SEV_UPS_HELD,    /* and a box on a battery rode it out                  */
    SEV_DOWN_DIRTY,  /* a box that was running went down unclean            */
    SEV_DISK_WARN,   /* a disk has started reallocating sectors             */
    SEV_DISK_FAIL,   /* and has now lost one                                */
    SEV_HEAT_WARN,   /* a room has more kit in it than it can shed heat for */
    SEV_HEAT_TRIP,   /* and a box in it has shut itself down                */
    /* ---------------------------------------------------------------- D28
     * A blackout used to damage every box it took down the same way, so
     * three servers down was one puzzle three times. It is not one cause
     * any more, and these are the causes that were added to it. */
    SEV_DISK_BOOT,   /* a disk nobody replaced took a file the boot reads   */
    SEV_LINK_WARN,   /* a marginal copper run is taking errors under load   */
    SEV_LINK_SLOW,   /* and has retrained down to a hundred megabits        */
    SEV_KIND_COUNT
} SiteEventKind;

typedef struct {
    int      day;
    uint8_t  kind;
    int16_t  dev;              /* the box, or -1 for the building itself    */
    char     what[128];        /* one line, in the words the player reads   */
} SiteEvent;

#define SITE_MAX_EVENT 128

/* THE WORLD CANNOT REACH THE DISK ON ITS OWN, and that is a feature. A Site
 * knows there is a box in a room; the operating system inside it is held by
 * whoever booted it -- a Session, or a gate. So an event that damages a
 * machine asks the owner for it, and gets NULL back for a box nobody has ever
 * switched on, which is right: a machine that has never run has nothing in
 * flight to lose. */
struct Machine_;
typedef struct Machine_ *(*SiteBoxFn)(void *ctx, int dev);

typedef struct {
    const Building *b;         /* borrowed: the caller owns the tower       */
    Net     *net;
    uint64_t seed;
    int      ndev, nlink, njack;
    SiteDev  dev[SITE_MAX_DEV];
    SiteLink link[SITE_MAX_LINK];
    SiteJack jack[SITE_MAX_JACK];
    int      uplink;           /* the device that exists on day one         */
    uint32_t wan_isp, wan_you, wan_mask;
    long     money, spent;
    int      ntenant;
    SiteTenant tenant[SITE_MAX_TENANT];
    int      err;              /* why the last operation refused            */
    /* ------------------------------------------------------- the clock */
    /* Nothing in this game came back for the player because nothing ever
     * advanced. A day is the unit: tenants move in on their day, their
     * people do a day's work over the network the player built, and the
     * rent for a day that worked arrives that evening. */
    int      day;
    int      isp_mb;           /* what the circuit carries, in megabits     */
    int      complaints;       /* filed, cumulative                         */
    long     rent_taken;
    uint8_t  over;             /* the run ended                             */
    char     over_why[128];
    SiteDay  last;             /* how the last day went                     */
    /* ------------------------------------------------------- the weather */
    SiteEvent ev[SITE_MAX_EVENT];
    int       nev;             /* the log wraps; `events` prints the tail   */
    int       ev_total;        /* everything that has ever happened         */
    SiteBoxFn box;             /* how to reach the OS inside a box          */
    void     *boxctx;
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
 * s->err set. THE PRIMITIVE, not the game: nothing a player types reaches
 * this with a room of their choosing, because kit is delivered and carried.
 * The gates below build worlds with it; site_order() is what play uses. */
int  site_install(Site *s, int kind, int room, const char *name);

/* --------------------------------------------------------- goods in */
/* WHERE A DELIVERY LANDS. Not your inventory: a room, on the ground floor,
 * with a roller door -- and the generator guarantees one (BC_PROGRAM fails a
 * tower that has nowhere to take a delivery). The lobby is the fallback, and
 * it is where a driver leaves a pallet when nobody answers the bell. */
int  site_goods_room(const Site *s);
/* Order kit. It is charged for, and it arrives in goods in, however many
 * floors away from the person who ordered it that happens to be. Returns the
 * device, or -1 with s->err set. */
int  site_order(Site *s, int kind, const char *name);
/* Carry it somewhere else. The same box, a different room -- the walk is the
 * cost, and the building is what measures it.
 *
 * REFUSED WHILE ANYTHING IS PLUGGED INTO IT. That is not a rule about
 * difficulty; it is the shape of the object. A box on the end of a cable
 * does not move without the cable coming out, so `uncable` first, and pay
 * for the copper again when you have decided where the thing lives. */
bool site_move(Site *s, int dev, int room);
/* Is there a cable in it? What makes site_move refuse, asked separately so a
 * view can grey the action out rather than let a player try it. */
bool site_dev_cabled(const Site *s, int dev);
/* Run a cable between two ports. The length is the tray route through the
 * building and the price is by the metre. A run past what the cable can
 * carry is NOT refused -- it is laid, it is paid for, and it does not come
 * up, which is what happens. */
int  site_cable(Site *s, int a, int aport, int b, int bport, CableKind k);
void site_uncable(Site *s, int link);
PortState site_link_state(const Site *s, int link);
/* The tray distance between two rooms, patch leads included, or -1. */
int  site_metres(const Site *s, int room_a, int room_b);
/* THE METRES A RUN BETWEEN THESE TWO ROOMS REALLY IS -- site_metres(), plus
 * the one rule about them that is not distance: the ISP's handoff is outside
 * the building (BLD_NOROOM) and the lead into it is a patch lead. site_cable,
 * site_jack and the quote all price the same run, so all three ask this. */
int  site_run_metres(const Site *s, int room_a, int room_b);

/* ------------------------------------------------------------- the quote
 * WHAT A RUN WOULD COST, BEFORE THE MONEY LEAVES. Every cable decision D27
 * built -- which grade, spool or jack, this cupboard or that one -- was made
 * blind: a playtester at day 62 could not tell a 39 m office from a 92 m one
 * on the same floor and called exercising the marginal-copper rule
 * "guess-and-pay at ~110 a guess".
 *
 * `room_a`/`room_b` are the two ends. `dev_a`/`dev_b` are the boxes at them
 * when the player named boxes, or -1 when they named a room -- and the
 * difference is the honest part: with no box at an end, the port that decides
 * the speed does not exist yet, and this says so rather than guessing.
 *
 * Nothing is bought, nothing is booked, and nothing is charged. */
void site_dump_quote(const Site *s, int room_a, int room_b,
                     int dev_a, int port_a, int dev_b, int port_b, Buf *out);

/* ------------------------------------------------------------- the jack */
/* HAVE A JACK PUT IN. `room` gets a faceplate; the run behind it goes to
 * `home`:`hport`, which is held by the jack from this moment and is not a
 * free socket again for the rest of the run -- you have bought a punched-down
 * panel port, not a cable you can move. Charged now, in full, at
 * site_jack_price() of the tray metres. Returns the jack, or -1 with s->err.
 *
 * IT IS NOT A SOCKET UNTIL `ready`. Nothing plugs into it before that day,
 * which is the whole point: cable off the spool is in your hands and a trade
 * is in the diary. */
int  site_jack(Site *s, int room, int home, int hport, CableKind k);
/* Plug a box that is standing in the jack's room into it, with a lead. Makes
 * a real link, on the jack's own metres and grade, for site_jack_lead_price()
 * -- and `uncable` on that link leaves the jack in the wall. Returns the
 * link, or -1 with s->err. */
int  site_patch(Site *s, int jack, int dev, int port);
/* Is that socket on the back of that box punched down to a jack? A held port
 * is not a free port: site_free_port, site_cable and `serve` all step over
 * it, because the pair is terminated on a panel and there is no hole. */
int  site_port_jack(const Site *s, int dev, int port);   /* jack, or -1 */
/* The jacks on the wall of one room, in order. `nth` counts from 0; -1 when
 * there is no such one. `free_only` skips the ones with a lead in them. */
int  site_room_jack(const Site *s, int room, int nth, bool free_only);
/* Every jack in the building, or the ones in one room when `room` is not
 * BLD_NOROOM: what it cost, what is in it, and when the trade finishes. */
void site_dump_jacks(const Site *s, int room, Buf *out);

/* THE POWER BUTTON, and it is the join between the box and the wire.
 *
 * A machine that had never been switched on used to answer a ping, because
 * the address was written onto its network node the moment the player typed
 * it and nothing anywhere asked whether the thing was running. Then plugging
 * a serial lead in booted it for the first time and it became LESS reachable,
 * because its real firewall finally started. Two machines, married by the
 * crash cart.
 *
 * So: powering on is what puts a box on the wire, and what it answers after
 * that is its own operating system's business. Powering off takes the
 * addresses, the routes, the sockets and the filter with it -- they were
 * never on the box, they were in its memory. */
bool site_power(Site *s, int dev, bool on);

/* Configuration, one line of a real config file at a time. `ifx` is the card:
 * 0 is the first socket on the back, 1 the second, and an index above the
 * sockets is a subinterface site_subif already made. THIS IS HOW A ROUTER
 * GETS A SECOND ADDRESS -- there used to be no verb that did, so a router
 * could not have a WAN side and a LAN side, so it could not route. */
bool site_addr(Site *s, int dev, int ifx, uint32_t ip, uint32_t mask);
bool site_gateway(Site *s, int dev, uint32_t gw);
bool site_forwarding(Site *s, int dev, bool on);
/* A tagged subinterface: how one router terminates many subnets down one
 * trunk, which is how anybody with more vlans than sockets does it. It ADDS
 * an interface to the named card and never touches the card itself. An
 * address of zero removes it. */
bool site_subif(Site *s, int dev, int nic, int vlan, uint32_t ip, uint32_t mask);
bool site_port_vlan(Site *s, int dev, int port, int vlan);
/* Make a switch port a trunk, and let one vlan across it. A `vlan` of 0
 * makes the trunk and allows nothing, which is where a real trunk starts.
 * It ADDS -- so a trunk may be built a line at a time -- and 1..4094 is the
 * range, the same range `subif` takes. Anything else is refused rather than
 * accepted and dropped. */
bool site_port_trunk(Site *s, int dev, int port, int vlan);
/* Take a vlan back off a trunk, or all of them with a `vlan` of 0. Without
 * this a trunk could only ever be added to, so a vlan put on the wrong
 * uplink stayed there for the rest of the run. */
bool site_port_trunk_off(Site *s, int dev, int port, int vlan);
/* What that trunk carries now, ascending. Returns how many there are, which
 * may exceed `cap`; `out` may be NULL to just count. */
int  site_port_trunk_list(Site *s, int dev, int port, int *out, int cap);
/* A DHCP POOL, AND THE SEGMENT IT SERVES. There is no vlan argument: the
 * segment is the interface of that box whose own address is inside the
 * pool's subnet, so a router with three subinterfaces runs three pools by
 * being called three times, and a pool with no leg of the box under it is
 * refused rather than left broadcasting into somebody else's tenancy. */
bool site_dhcpd(Site *s, int dev, uint32_t first, int count, uint32_t mask,
                uint32_t gw, uint32_t dns);
/* Stop serving addresses. Returns how many pools it stopped, so "stopped
 * three" and "it was not serving anything" are different sentences. */
int  site_dhcpd_stop(Site *s, int dev);
/* What that box is serving, in words, for `show` and for `dhcpd <box>`. */
void site_dump_dhcpd(const Site *s, int dev, Buf *out);
bool site_dhcp(Site *s, int dev);          /* ask for a lease, for real     */
bool site_resolver(Site *s, int dev, uint32_t ns);
bool site_dnsd(Site *s, int dev);
/* ONE NAME ON A NAME SERVER OF YOUR OWN. Without this, `dnsd <box>` started
 * a server with an empty zone and no way to fill it, which could only ever
 * answer "no such host" -- so the only working resolver in the tower was the
 * ISP's, and every lookup on every floor hairpinned through the router to
 * reach it. Setting a name that is already served changes where it points.
 * Refused, with SITE_EZONE, when the zone is full. */
bool site_dns(Site *s, int dev, const char *name, uint32_t ip);
/* What that box will answer, and where it sends what it cannot: for
 * `dnsd <box>` and for `show`. */
void site_dump_dnsd(const Site *s, int dev, Buf *out);
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

/* ============================================================== THE LOOP
 *
 * The first blind playtester of the tower counted fifteen decisions an hour
 * and then said the thing that mattered: *"They felt like MY decisions; they
 * did not yet feel like decisions that would come back for me."* Nothing
 * came back because nothing advanced. This is the clock, and everything it
 * turns.
 *
 * A DAY: tenancies whose day has come move in and their desks arrive; the
 * desks ask for addresses the way a computer asks for one; then the busy
 * period runs -- every desk in the building doing a real day's work through
 * the real stack at the same time -- and what finished is what the tenant
 * pays for.
 *
 * WHY THE BUSY PERIOD AND NOT THE WHOLE DAY. Four seconds of wire time, at
 * the moment of the day when everybody is doing something. A network is
 * sized for its peak and fails at its peak; simulating the other 86,396
 * seconds would be the same arithmetic with more zeroes in it. The window
 * is `SITE_BUSY_MS` and it is the same for everybody, so a comparison
 * between two builds is a comparison of the builds.
 */
#define SITE_BUSY_MS      4000
/* WHAT ONE DESK DOES IN THAT WINDOW, and where it goes. These two numbers
 * are the demand side of the whole difficulty curve, and they are the only
 * numbers in the loop that were chosen rather than derived -- so they are
 * chosen to be a defensible busy-period figure for one person at one desk
 * and nothing else. Two megabytes off a file server in four seconds is four
 * megabits a second, which is one video call or one ordinary document being
 * opened; three hundred kilobytes off the internet is another half a megabit.
 *
 * WHICH WIRES CARRY THEM IS THE PLAYER'S ARCHITECTURE, and that is where the
 * curve actually comes from. File traffic goes to the nearest server the
 * player has put up -- their own tenancy's if they have one, otherwise
 * whichever one exists -- so a server on the floor keeps a floor's worth of
 * megabits off the riser and a server in the basement does not. Internet
 * traffic crosses the ISP circuit whatever anybody does. */
#define SITE_DESK_FILE_KB 1536
#define SITE_DESK_WEB_KB   384
/* HOW MANY OF THOSE ARE OPEN AT ONCE, and this is the number the difficulty
 * curve actually turns on, so here is the whole of the reasoning.
 *
 * A desk used to have exactly one file transfer and one page in the busy
 * period. That made the demand of a whole building small enough that a
 * gigabit carried the naive build's one basement server to nine tenancies at
 * sixty per cent -- the tower stopped falling over at all, once the port
 * arithmetic was fixed and a port could no longer congest for a reason that
 * was not on the wire (the model note in port_tx()).
 *
 * The lever is NOT the size of the file. It was tried: four times the bytes
 * in one transfer, and the run fell apart at ONE tenancy on an empty network,
 * because a single TCP flow across this stack carries about fifteen megabits
 * and no more -- window over round trip, nothing to do with the building. A
 * six megabyte file cannot be pulled inside a four second window however
 * good the network is, so the metric stopped measuring the network. Bytes
 * per transfer are therefore left where they were, at a document.
 *
 * What a person's machine really has open in the busiest four seconds of
 * their day is more than one thing: the document they asked for, the mail
 * client, the sync client putting back what they saved. Three concurrent
 * pulls off the file server, of a document and a half each, is four and a
 * half megabytes and nine megabits a second per desk sustained -- which is
 * what a wired desk with a gigabit card in it does, and is why it has one. */
#define SITE_DESK_FILES     3
/* What the landlord's circuit carries until somebody buys a bigger one. */
#define SITE_ISP_MB_DEFAULT 500

/* ================================ WHAT THE OTHER THREE INDUSTRIES ASK FOR
 *
 * The same status as the two numbers above: chosen, not derived, and chosen
 * to be a defensible busy-period figure for that business and nothing else.
 * Everything downstream of them is arithmetic the netstack does, and every
 * byte of them goes through core/netstack.c as frames on ports.
 *
 * A VOICE CALL, and it is the netstack's own -- `net_voice_call`, two
 * streams, one each way, real UDP at NET_VOICE_PAYLOAD every NET_VOICE_PTIME
 * through the same ports, queues and drops as everything else. A G.711
 * stream is 86 kb/s on the wire, so a twenty-seat call centre asks for a
 * fiftieth of what ONE office desk pulls and no amount of bandwidth will
 * help them.
 *
 * WHAT MAKES A CALL BAD IS CONCEALMENT, which is the stack's word and not a
 * score anybody scaled: an audio frame with no sound to play, because the
 * packet was lost or because it arrived after the de-jitter buffer had
 * already played the silence where it should have gone. That is why loss
 * AND jitter both land in one number, and it is counted in packets. Two per
 * cent of the audio missing is where a call stops being a call, which is the
 * figure the telephony industry uses; a hundred and fifty milliseconds of
 * one-way delay is where two people start talking over each other, which is
 * G.114's. Both directions have to be good, because a call is two streams
 * and a floor's congestion is usually only in one of them. */
#define SITE_VOICE_CONCEAL_PPM  20000   /* 2% of the audio frames          */
#define SITE_VOICE_DELAY_MS       150   /* one way, G.114                  */
/* A VOICE DESK STILL HAS A COMPUTER ON IT. One page and one file: the agent
 * has a CRM open, and it is a quarter of what an office desk pulls. It is
 * NOT what they are judged on. */
#define SITE_VOICE_FILES       1

/* A WEB HOST. Visitors, arriving from the internet in the busy period, each
 * pulling a page and its images off the tenancy's origin server. Twenty-four
 * concurrent visitors at a quarter of a megabyte is six megabytes INBOUND --
 * small in bytes and it crosses the circuit, the router and the riser in the
 * direction nothing else in this tower ever loads. What they buy is that it
 * answers at all: nineteen of every twenty, or the day did not count. */
#define SITE_WEB_HITS         24
#define SITE_WEB_HIT_KB      256
#define SITE_WEB_UP_NUM       19    /* served when hits_ok/hits >= 19/20    */
#define SITE_WEB_UP_DEN       20
/* And their own staff, who are a handful of people and not a floor of them. */
#define SITE_WEB_FILES         1

/* A STUDIO. One sustained upload per suite, for the whole busy period, to an
 * ingest on the far side of the landlord's circuit. Three megabytes in four
 * seconds is six megabits a second -- a 1080p stream with its audio -- and it
 * is UP, which is the one direction nothing in this game has ever stressed.
 *
 * IT IS ALL OR NOTHING, and that is the whole difference between this and an
 * office. A file that arrives late is a slow morning; a stream that arrives
 * late is a stream that dropped, and the viewers have gone. So there is no
 * partial credit: every kilobyte, inside the window, or it did not happen. */
/* AND IT IS CONCURRENCY, NOT SIZE, for the reason SITE_DESK_FILES gives at
 * length: one TCP flow across this stack carries about fifteen megabits and
 * no more -- window over round trip -- so a single upload sized near that
 * ceiling stops measuring the network and starts measuring the ceiling. It
 * was tried at six megabits a second in one flow and every stream in a
 * perfectly healthy tower landed a few kilobytes short, which is the same
 * mistake in the other direction. A suite pushes the stream and the archive
 * copy at once, which is what an edit suite really has open, and each of
 * them is comfortably inside what one connection can carry. */
#define SITE_STREAM_KB      2048
#define SITE_STREAM_LEGS       2
#define SITE_STREAM_PORT    1935
/* AND A SUITE STILL OPENS THE PROJECT. An edit bay pulls its media off a
 * server like everybody else -- it is where the footage is -- so a studio
 * floor wants a file server AND a circuit, which is the point: they are the
 * one trade whose demand pulls in both directions at once. It is not what
 * they are judged on; the stream is. */
#define SITE_STUDIO_FILES      1

/* THE DESKS ARRIVE WITH THE TENANT. `serve` is the player's half: it runs
 * copper from a box they own to the tenancy's desks, as many as there are
 * free ports for, and charges for every metre. Returns how many desks got a
 * port, or -1 with s->err set. */
int  site_serve(Site *s, int tenant, int dev, CableKind k);
/* The same, putting each port it patches into `vlan` as it patches it. Zero
 * leaves them in the untagged default, which is what site_serve does. */
int  site_serve_vlan(Site *s, int tenant, int dev, CableKind k, int vlan);
/* How many of a tenancy's desks have a cable in them and an address on the
 * card. Service is this, and then whether the work finishes. */
int  site_tenant_connected(const Site *s, int tenant);
int  site_tenant_addressed(const Site *s, int tenant);
/* WHY THAT TENANCY IS UNHAPPY, IN ITS OWN TERMS, and empty when they are not.
 * A voice tenancy is not unhappy because transfers did not finish -- they are
 * unhappy because the calls broke up, and a web host is unhappy because they
 * were down. One sentence, out of the numbers their own industry counts. */
void site_tenant_why(const Site *s, int tenant, char *out, int cap);
/* Whether four fifths -- or, for a web host, nineteen twentieths -- of what
 * that tenancy was promised really happened yesterday. One place, because the
 * rent, the strike and the `service` page all have to agree about it. */
bool site_tenant_served(const Site *s, int tenant);

/* ONE DAY. Moves people in, runs the busy period, takes the rent, counts the
 * strikes and files the complaints. Returns false once the run is over --
 * `s->over_why` says which way. */
bool site_day(Site *s, SiteDay *rep);
/* Several, stopping early if the run ends. */
bool site_advance(Site *s, int days, Buf *out);

/* What a circuit costs: the ISP's price for `mb` megabits a month -- and it
 * is really taken, on the thirtieth day, out of the same account the rent
 * goes into. `site_isp_days_to_bill` is how many days until the next one, so
 * `isp` and `status` can say it rather than leave the player to count. */
long site_isp_price(int mb);
int  site_isp_days_to_bill(const Site *s);
/* How many filed complaints end the run. A third of the tenancies that have
 * moved in, rounded up, never fewer than three -- so the building gets more
 * slack as you let it, rather than more brittle. `service` prints it. */
int  site_complaints_allowed(const Site *s);
bool site_isp(Site *s, int mb);

/* ------------------------------------------------------------ the weather */
/* Tell the site how to reach the operating system inside a box. Without this
 * a power cut still takes the machines down and still logs itself; it just
 * cannot damage a disk, because there is no disk in reach. */
void site_boxes(Site *s, SiteBoxFn fn, void *ctx);
/* Is a mains failure due on this day? A pure function of the seed and the
 * day: no state, nothing rolled at runtime, so the same seed always has its
 * blackout on the same morning. */
bool site_mains_fails_on(uint64_t seed, int day);
/* Fit a battery to a box. It is the difference between a machine that rides a
 * blackout out and one that comes back with a filesystem to check, which is
 * what makes buying one a decision rather than a purchase. */
long site_ups_price(void);
bool site_ups(Site *s, int dev);
/* Swap the disk in a box and copy what was on it across. Resets the wear, so
 * the box stops being days away from losing a sector -- and copies whatever
 * damage is already there, because that is what a clone does. */
long site_disk_price(void);
bool site_disk(Site *s, int dev);
/* The watts of kit in a room, what that room can shed, and the second as a
 * percentage of the first. The heat model is these three numbers and the
 * room's own area out of the building generator; nothing else. */
int  site_room_watts(const Site *s, int room);
int  site_room_capacity(const Site *s, int room);
int  site_room_heat(const Site *s, int room);
/* What the world has done, newest last, and the condition of the kit. */
void site_dump_events(const Site *s, Buf *out);

void site_dump_day(const Site *s, Buf *out);
/* Every tenancy that is in, how many desks are up, and how their last day
 * went. This is the page a player reads to see a complaint coming. */
void site_dump_service(const Site *s, Buf *out);
/* The busiest ports in the building, most-used first: the summary whose
 * evidence is `show <box>` on the box named in it -- a switch has no
 * shell to type netstat into. */
void site_dump_load(const Site *s, Buf *out);

/* ------------------------------------------------------------ inspection */
void site_dump(const Site *s, Buf *out);
void site_dump_links(const Site *s, Buf *out);
void site_dump_rooms(const Site *s, int floor, Buf *out);
void site_dump_dev(Site *s, int dev, Buf *out);
/* The same, minus the empty sockets. */
void site_dump_dev_brief(Site *s, int dev, Buf *out);

/* ONE LINE OF TEXT, ONE OPERATION. The whole game, over a pipe. Returns
 * false only when the line was not understood; a refusal is a true return
 * with the reason written into `out`, because a refusal is an answer. */
bool site_cmd(Site *s, const char *line, Buf *out);

/* The gate. See core/sitecheck.c. */
int  site_selfcheck(void);
/* And the session on top of it, scored into the same total. */
int  session_selfcheck(int *passed, int *total);

#endif /* NOM_SITE_H */
