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
#define SITE_MAX_CONDUIT 400
/* WHAT ONE RUN OF CONDUIT CARRIES, in watts, and what a strip will pass.
 *
 * 1500 is chosen so that it is a DECISION rather than a formality: a proper
 * server draws 350 and a rack server 700, so one run feeds four of the small
 * ones or two of the big ones and then you go back to the core for another.
 * A cupboard with a switch, two servers and a spare is already thinking about
 * its second run, which is the point -- "you have to run fresh conduits from
 * the power core once they've hit a maximum load".
 *
 * The strip passes the same, because a strip is a junction and not a
 * substation: it cannot give you more than what arrives at it. */
#define SITE_CONDUIT_W   1500
#define SITE_MAX_TENANT  200
#define SITE_MAX_JACK    200
#define SITE_MAX_SOCKET  240     /* outlets ORDERED. The built-in ones are  */
                                 /* a function of the room, not a table.    */
#define SITE_PATCH_M       3     /* a patch lead at each end of every run   */

/* WHAT YOU START WITH, AND WHY IT IS THIS NUMBER.
 *
 * It was 60,000, written out four times -- twice in core/bfmain.c, once in
 * core/session.c and once in game/scripts/tower.gd -- which is this project's
 * standing defect in its purest form: one fact with four answers, and no way
 * to change it without finding all of them.
 *
 * The number itself was the bigger problem. A blind playtester played to day
 * 70 and reported the opening as the weakest part of the game: "Days 1-20 are
 * too easy and slightly boring... money accumulates... there is no pressure
 * at all in the first three tenancies; the build is mechanical and there is
 * no decision in it." The owner had already reached the same conclusion from
 * the other side -- "I suspect the default gear given to the player is too
 * much... just enough to get off the ground, not enough to keep the whole
 * system running until day thirty" -- and asked for grades of kit so that
 * what to buy is a decision. Grades landed in D43. They decide nothing while
 * the player can afford the best of everything twelve times over.
 *
 * So the number was MEASURED against the thing it has to make hard, on seed
 * 7008, playing the opening out:
 *
 *   a first tenancy that actually pays        4,901   switch24, server, router
 *   the same build with the cheap server      4,011   and it earns NOTHING:
 *                                                     20 of 80 transfers, no
 *                                                     rent, because a 100 Mb
 *                                                     port cannot carry 20 desks
 *   the same build with the dear server       6,951
 *
 * 6,000 sits between the middle build and the dear one. You can afford a
 * tower that works; you cannot afford to stop thinking about it. The cheap
 * server is a real trap with a real cost, and the difference is legible --
 * `load` names the port, `show` says its egress buffer is full.
 *
 * IT IS DELIBERATELY POSSIBLE TO SPEND THIS BADLY AND BE STUCK. The owner
 * ruled on exactly that case for the network -- "that is end game, not a
 * failure of the game, but a you did not do it right, try again" -- and the
 * same answer applies to the money. There is no `sell`. */
#define SITE_OPENING_MONEY 6000

/* ---------------------------------------------------------- the catalogue */
/* What the player can order. The port counts are the limit that bites first
 * and the prices are what the tenants have to cover. */
/* AND THE GRADES, WHICH ARE THE DECISION THE FIRST TWENTY DAYS DID NOT HAVE.
 *
 * A blind playtester who reached day 70: *"Days 1-20 are too easy and
 * slightly boring... money accumulates... there is no pressure at all in the
 * first three tenancies; the build is mechanical and there is no decision in
 * it."* There was one switch worth buying, one server worth buying, and the
 * money to buy either without thinking.
 *
 * There are three grades of each now, and the rule they obey is the rule
 * this whole project is built on: THE DIFFERENCE IS A SPEC, NOT A QUALITY
 * NUMBER. Nothing anywhere multiplies anything by a grade. A `switch4` is
 * four sockets that clock a hundred megabits, and everything that follows
 * from that -- a queue that fills ten times sooner on the same burst, drops
 * that `load` counts and `show <box>` gives the reason for in words, a floor
 * that outgrows four holes -- follows because netstack is doing arithmetic
 * with the number on the box.
 *
 * The axes, and every one of them is already measured somewhere:
 *
 *   PORT SPEED   site_kind_port_mb(), read off the wire by netstack, printed
 *                by `load` and `show`. This is the durability axis: the
 *                egress buffer is the same 48 KB on every port in the game,
 *                so a 100 Mb port drains it ten times slower and drops on a
 *                burst a gigabit port rides out.
 *   PORT COUNT   the limit that bites first, and the reason a floor that
 *                fills up costs a second box and a second riser.
 *   DISK LIFE    site_kind_disk_days(), the days of average use before the
 *                platter starts losing sectors. A cheap disk in a busy
 *                server is the first thing in this game to come back for you.
 *   A BATTERY    whether it rides a mains dip at all. The dear server has
 *                one in it; on anything else `ups <box>` buys one later.
 *
 * AND THE UPGRADE PATH IS THE GAME'S EXISTING PHYSICAL ONE. There is no
 * `upgrade` verb and there is not going to be one: you order the better box,
 * it lands in goods in, you carry it up, you cable it, you address it and
 * you move the service onto it -- and the copper you already paid for is
 * charged again, because it is. */
typedef enum {
    SDEV_UPLINK = 0,    /* the ISP handoff. Given, not bought.              */
    /* THE CHEAP END. Four sockets at a hundred megabits: enough to get one
     * office off the ground and not enough to keep it there. It is a real
     * unmanaged desktop switch and it is priced like one. */
    SDEV_SWITCH4,
    SDEV_SWITCH8,
    SDEV_SWITCH24,
    SDEV_ROUTER,
    SDEV_PC,
    /* A SMALL-OFFICE SERVER: one hundred-megabit card, and a disk rated for
     * half the life of the one in the proper box. It will serve a floor's
     * files on day three and it is what the floor outgrows first. */
    SDEV_MINITOWER,
    SDEV_SERVER,
    /* AND THE ONE THE LATE GAME NEEDS: two ten-gigabit cards, a disk rated
     * for twice the life, and a battery in it. The README names the floor
     * server's own gigabit card as the binding port in a planned tower, and
     * this is the box that unbinds it -- for two and a half times the money
     * and the whole physical job of moving the service across. */
    SDEV_RACKSERVER,
    /* A DESK. The tenant's own computer, on the tenant's own desk, which
     * they carried in themselves the day they got the keys. It is not for
     * sale and it is not the player's: what the player sells is the port it
     * is plugged into. It is a real card in a real broadcast domain and it
     * generates real frames, which is the only reason any of the rest of
     * this file has anything to do. */
    SDEV_DESK,
    /* THE PLAYER'S OWN WORKSTATION. The machine in the MDF the browser, the
     * files app and the desktop terminal all run on -- and it is a box like
     * every other box: one gigabit socket, an operating system, a plug in the
     * wall and a name in `look`. It is not for sale, because it was there on
     * the morning of day one and cost the landlord nothing; that is a fact
     * about the CATALOGUE and not a rule about what may be done with it.
     * Carry it, cable it, unplug it, stand it in a cupboard on floor six --
     * every refusal that applies to a pc applies to this and no others do.
     * site_new() cables it to uplink:0, which is the handoff's ONLY port, so
     * the first switch the player buys costs them a re-cable of their own
     * machine. See the note above site_workstation(). */
    SDEV_WORKSTATION,
    /* ------------------------------------------------------------- POWER
     *
     * The owner, redirecting the whole game towards a station: "instead of
     * money being the stress point, it's pure design... essentially I'd want
     * to reuse everything we've already done, but making you have to run
     * power conduits to certain places just like you run ethernet." And on
     * the shape of it: "we'll have to add in something like a power strip
     * that allows you to take a conduit and plug in multiple devices to the
     * end of it. Including other conduits so that you can fork a conduit...
     * when you hover over a conduit, it'll tell you its percent of
     * utilisation. So you have to run fresh conduits from the power core
     * once they've hit a maximum load."
     *
     * That is the network's own shape with one number instead of two. A
     * conduit is a run, priced by the metre off the same cable graph. A
     * strip is the switch of it: one input, several outputs, and an output
     * takes a load OR another strip, which is the fork. And the thing a
     * player watches is UTILISATION -- what is drawn through a run against
     * what it carries -- which is what `load` already prints for a wire.
     *
     * THE CORE IS GIVEN, LIKE THE HANDOFF. It was there before you were,
     * there is one on the ground floor's plant room, and everything with a
     * light on traces back to it or it is dark. */
    SDEV_POWERCORE,
    SDEV_STRIP,
    SDEV_KIND_COUNT
} SiteDevKind;

const char *site_kind_name(int kind);
int   site_kind_by_name(const char *name);
int   site_kind_ports(int kind);      /* sockets on the back of it          */
int   site_kind_price(int kind);      /* pounds                             */
bool  site_kind_is_switch(int kind);
/* Is it one of the three grades of server? A file server is any of them: what
 * differs is the card, the disk and the battery, not what it does. */
bool  site_kind_is_server(int kind);
/* HOW LONG THE DISK IN IT IS RATED FOR, in days of average use -- the same
 * unit `events` prints the wear percentage against. A box at full load wears
 * about five times as fast as an idle one, so this is a rating and not a
 * countdown, and `disk <box>` puts a new one in for a fraction of the price
 * of the box. 0 for anything with no disk in it. */
int   site_kind_disk_days(int kind);
/* Bytes of burst one of its ports will hold while it waits for the wire.
 * Speed is how fast a queue drains; this is how much it can hold while it
 * does, and a switch with more ports holds more. */
int   site_kind_port_buffer(int kind);
/* WHAT ONE OF THESE DRAWS, in watts, when it is running. 0 for the things
 * that are not on your power: the handoff is the ISP's and a tenant's own
 * desk is on their own supply, which is the same sentence the heat model and
 * the old outlet model both made about a desk. */
int   site_kind_watts(int kind);

/* Does one arrive with a battery under it? The dear server does; on anything
 * else `ups <box>` buys one afterwards for the same result and more money.  */
bool  site_kind_has_ups(int kind);
/* IS IT IN THE SHOP? Three kinds of device in this catalogue are not the
 * landlord's to buy -- the ISP's handoff, a tenant's own desk, and the
 * player's own workstation -- and until D41 the supplier's catalogue page
 * inferred that from `price > 0`, which happened to be right and was not the
 * fact it wanted to ask (D40 says so and asks for this). site_install() and
 * site_order() refuse anything this returns false for, so what the shop
 * advertises and what the building accepts are one predicate. */
bool  site_kind_for_sale(int kind);
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
    SITE_ENOMAINS,    /* the room has no socket left to plug it into          */
    /* AND THE OTHER HALF OF WHAT THAT USED TO MEAN. These are two different
     * facts and they were one code with one sentence: a pc standing in goods
     * in, which has two free outlets, was refused with "there is no free
     * outlet on that room's wall" -- printed one line above a table saying
     * there were two -- and sent to buy a third. The room is fine. The LEAD
     * is not in, and the verb for that is `mains <box> on`. */
    SITE_EUNPLUGGED,  /* there is no lead from that box to a wall socket      */
    SITE_ECIRCUIT,
    /* A conduit that would feed the thing it comes out of. */
    SITE_ELOOP,
    SITE_ENOWALL,     /* the old outlet refusal, while outlets are still  */
                      /* in the tree at all. See site_mains_sync().       */    /* the room is on one circuit and it is full            */
    /* AN ERROR ABOUT SUBNETS, FROM A VERB THAT TAKES MEGABITS. `isp 0` and
     * `isp -5` both answered "that is the network or broadcast address of
     * its own subnet, not a machine's", because site_isp() reached for
     * SITE_EADDR for want of anything better. The circuit is a number of
     * megabits and its refusal has to be about megabits. */
    SITE_EMBIT,       /* not a circuit size: `isp <mb>` takes megabits        */
    /* AND THE OTHER HALF OF SITE_EPOOL. One code carried two facts and led
     * with the false one: a 180-address pool refused for being the ninth on
     * the box was told "a pool of no addresses serves nobody, and a box
     * holds eight pools at most". Two things happened; only one of them
     * ever happened at a time. */
    SITE_EPOOLS,      /* that box already holds all the pools it can hold     */
    /* A TENANCY WITH A DATE ON IT. `serve 3 sw2b` on day 6 for a tenancy
     * that moves in on day 11 answered "no such device" -- about a line with
     * no device in it -- while `serve 99` for a tenancy that will never
     * exist got a sentence that named the right verb. The good message
     * existed and the case a player really hits did not use it. */
    SITE_ENOTIN,      /* that tenancy has not moved in yet                    */
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
    /* THE PLUG, AND IT IS A DIFFERENT FACT FROM THE BUTTON. `powered` is
     * whether the button has been pressed; this is whether there is a wall
     * socket on the other end of the lead. A box that is not in one cannot
     * be switched on at all -- the button does nothing, which is what a
     * button on an unplugged machine does. See site_mains() below. */
    uint8_t  mains;
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
    /* THE LEAD THE BUILDING CAME WITH, and there is exactly one of them.
     *
     * On the morning the player gets the keys their own workstation is in the
     * handoff's only port, on a lead nobody paid for. When they buy their
     * first switch and cable it to the handoff, that lead comes out: a socket
     * takes one plug, and refusing would make the first switch anybody buys
     * an error message instead of a decision. It does NOT go anywhere else --
     * the workstation is off the network, and the shop with it, until the
     * player patches it into whatever they just put in front of it. That is
     * the escape route, performed forwards, on day one.
     *
     * It is the ONLY lead in the game with this property and it has it once:
     * pull it and it is gone, and every lead after it is an ordinary lead
     * that refuses with SITE_EBUSY like everything else in this file. */
    uint8_t  factory;
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

/* ============================================================ POWER =======
 *
 * The owner, walking into his own tower for the first time in a while:
 *
 *   "The server in the default rack isn't booting, but it's also not plugged
 *    into any power... Each room should have at least one power outlet. We
 *    also need power logic, so you plug in servers into the actual wall.
 *    Potentially have a way to view the mini map for the entire area and
 *    request/order additional power for a fee. That then installs the power
 *    outlet into that room."
 *
 * A ROOM HAS SOCKETS AND THEY RUN OUT. Not a resource bar: a count of holes
 * in a wall, decided by what kind of space the building generator made and
 * how big it is. A comms cupboard is a cupboard with a spur off the
 * landlord's board in it; an office is wired for people, so it has a socket
 * every few metres; a toilet has the shaver socket and nothing else. So
 * putting a fourth box in a floor's cupboard is a decision with a bill
 * attached, and putting one in a corridor is not a plan.
 *
 * PER ROOM, NOT PER WALL, and the reason is D23's rule rather than
 * simplicity. The Building gives a room a kind and an area and no wall
 * geometry any of this code can address; a socket with a position would be a
 * coordinate the MODEL had to invent and the WINDOW had to be the authority
 * on, which is the inversion this project exists to avoid. It is also not a
 * decision: a lead reaches any wall of one room, so which wall it is on
 * changes nothing a player chooses. The count is the decision. Where the
 * faceplate is drawn is the window's business and it may put it anywhere.
 *
 * AND THE CIRCUIT IS FINITE TOO. You may have as many again put in as the
 * room was built with -- site_room_outlets_max() -- and after that the room
 * is on one final circuit and there is no more power to bring into it. That
 * is the limit that cannot be bought out of, and it is what stops the floor
 * plan being scenery: some rooms are places you put equipment and some are
 * not.
 */
typedef struct {
    uint16_t room;             /* the wall it went on                       */
    int      day;              /* the day the sparky came                   */
    int      cost;             /* what it cost. Never refunded.             */
} SiteSocket;

/* ===================================================== A RUN OF CONDUIT
 *
 * The same shape as a SiteLink and deliberately not the same struct: a
 * conduit carries no frames, has no netstack cable behind it and negotiates
 * nothing, so sharing SiteLink would mean four fields that are always zero
 * and one -- `cable` -- that would have to lie. What IS shared is the thing
 * worth sharing: the metres come from bld_cable_all() and the price from
 * site_run_cost(), so a conduit and a patch lead between the same two rooms
 * cost the same to run, which is the parity rule every cable in this game
 * already has.
 *
 * `from` is a core or a strip and `fport` is which of its outputs. `to` is
 * the thing being fed: a load takes the whole device -- a lead goes in the
 * back of a box, there is nothing to choose -- and a strip takes its input.
 * So the tree is unambiguous without inventing a power port on every kind of
 * equipment in the catalogue. */
typedef struct {
    int16_t  from, fport;      /* a core or a strip, and which output       */
    int16_t  to;               /* the load, or the strip being fed          */
    uint16_t room_a, room_b;
    int      metres;
    int      cost;
    int      watts;            /* what this grade of conduit carries        */
    uint8_t  live;             /* 0 once pulled out                         */
} SiteConduit;

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
/* What this trade COUNTS in `tried`/`finished`: an office finishes transfers,
 * a call centre calls, a web host visitors, a studio uploads. One word in one
 * place, so `service` and the person at the desk cannot disagree. */
const char *site_tenant_kind_unit(int k, bool plural);
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
    /* HOW MANY OF THEIR DESKS HAVE EVER ASKED FOR A LEASE. A desk asks when
     * a day runs and not before, so twenty cabled desks with no address on
     * the evening they were patched is not a fault at all -- it is a day
     * that has not happened yet. `service` used to read `addressed == 0` and
     * say "nothing is serving dhcp on their segment", which sent a player
     * who had built it correctly off to re-check a working pool. This is
     * counted where the request is really made, so the sentence is about
     * what happened rather than about what a report inferred. */
    int      leases_asked;
} SiteTenant;

/* HOW A DAY WENT, for the whole site. Every number in here was counted
 * during the busy period; none of it is a model sitting beside the netstack.
 * `site_dump_day` prints it and `--loadcheck` asserts on it. */
typedef struct {
    int  day;
    int  tenants_in;        /* moved in                                    */
    int  tenants_served;    /* connected, addressed and finishing work     */
    int  desks, connected;
    /* EVERY UNIT OF WORK THE TOWER CARRIED, which is not the same total as
     * the tenancies' own `tried`/`finished` and must not be printed as if it
     * were: a voice agent's CRM transfers are real traffic this building has
     * to move and are not what the call centre is judged on. `--loadcheck`
     * measures the tower with these; every player-facing report says the
     * day's work with `site_day_work`, which sums the rows instead. */
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
    int      ndev, nlink, njack, nsock;
    SiteDev  dev[SITE_MAX_DEV];
    SiteLink link[SITE_MAX_LINK];
    SiteConduit cond[SITE_MAX_CONDUIT];
    int      ncond;
    SiteJack jack[SITE_MAX_JACK];
    SiteSocket sock[SITE_MAX_SOCKET];
    int      uplink;           /* the device that exists on day one         */
    int      ws;               /* ...and the one you sit at. See site_new() */
    int      yielded;          /* the box whose factory lead the last cable
                                * pulled out, or -1. See SiteLink.factory.  */
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
/* An empty site: the building, the ISP's socket in the MDF, the player's own
 * workstation in the MDF with its lead in that socket, and a budget. There is
 * no switch, no address and no connectivity to anything the player owns until
 * somebody makes some. */
bool site_new(Site *s, const Building *b, uint64_t seed, long budget);
/* THE MACHINE THE PLAYER SITS AT, as a device index, or -1. The browser, the
 * files app and the desktop terminal run on THIS box -- so pulling its lead
 * out takes the supplier's website away, which is the point of it being a box
 * at all. It obeys every rule an ordinary pc obeys and gets no others: it can
 * be carried, re-cabled, unplugged from the wall and switched off, and the
 * consequences are the consequences. There is always a move, because the
 * thing is standing in a room a person can walk to. */
int  site_workstation(const Site *s);
/* Is the lead in that port the one the building came with? The link index, or
 * -1. See SiteLink.factory: it is the one lead in the game that gives way to
 * whatever the player puts in front of it, and every surface that would
 * otherwise refuse the port has to ask this rather than assume. */
int  site_port_factory(const Site *s, int dev, int port);
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
/* IS THERE ANYTHING IN THAT SOCKET? Asked of the site's own link table, which
 * is where a lead being in a hole is recorded -- and NOT of the netstack,
 * because a port with no power behind it reads DOWN rather than NOCABLE. That
 * difference printed "8/8 ports used" and "next free port switch8:0" about the
 * same uncabled switch, in goods in, in one line of `look`: two facts about
 * one box, from two sources, disagreeing. site_free_port() has always read
 * this one; now everything that counts holes does. */
bool site_port_used(const Site *s, int dev, int port);
/* How many of that box's sockets have something in them, by the same count. */
int  site_ports_used(const Site *s, int dev);
/* And how many a lead can still go into: nports, less the leads, less the
 * ports held for good by a jack. `show <box>` prints this beside the total
 * and the highest port number, because a playtester read "1 socket" over
 * "1 more socket on the back of it, with nothing in it", added them, and
 * spent a session trying to cable a port 1 that has never existed. */
int  site_ports_spare(const Site *s, int dev);
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

/* ------------------------------------------------------- AND THE WALL ----
 * THE OTHER HALF OF THE BUTTON, and until D37 it did not exist: every box
 * ever installed drew power from nowhere, so `power srv on` on a machine
 * standing in an empty cupboard with no lead in the back of it worked. See
 * the note above SiteSocket.
 *
 * A box is on mains or it is not, and a box that is not cannot be switched
 * on -- site_power() refuses with SITE_ENOMAINS and the refusal is the
 * diagnosis. An appliance has no button, so for a switch and a router the
 * plug IS the button and site_mains() is what turns it on and off.
 */
/* The sockets the building was wired with, what the room has now (that plus
 * the ones bought), the most its circuit will ever carry, and how many of
 * them have a plug in them. Every one is a pure reading of the room and the
 * device table. */
/* ------------------------------------------------------------- conduits */
/* Run one, from an output of a core or a strip to a load or another strip.
 * Priced by the metre off the building's own cable graph, exactly as copper
 * is. Returns the run, or -1 and s->err. */
int  site_conduit(Site *s, int from, int fport, int to);
/* THE SAME RUN, WITHOUT PICKING THE END. Finds the nearest source with a hole
 * in it -- nearest along the trays the cable will really lie in -- and runs
 * it. For a client driving this over a socket, which cannot see the room.
 * Same metres, same price, same refusals. */
int  site_feed(Site *s, int to);
bool site_unconduit(Site *s, int run);
/* What is drawn through this run: everything downstream of it, added up. */
int  site_conduit_load(const Site *s, int run);
/* And as a percentage of what it carries -- the number that goes on the
 * hover. Over 100 is a run that has tripped. */
int  site_conduit_pct(const Site *s, int run);
/* Is there a live path from this device back to a core, with no run on it
 * carrying more than it can? -1 in `why` gets the run that tripped. */
bool site_dev_fed(const Site *s, int dev, int *tripped);
int  site_conduit_count(const Site *s);
/* Make every device's `mains` agree with the conduit tree. Called by every
 * path that changes the tree; see the note on the definition. */
void site_mains_sync(Site *s);
void site_dump_conduits(const Site *s, Buf *out);

int  site_room_outlets_built(const Site *s, int room);
int  site_room_outlets(const Site *s, int room);
int  site_room_outlets_max(const Site *s, int room);
int  site_room_outlets_used(const Site *s, int room);
int  site_room_outlets_free(const Site *s, int room);
/* Which box is in the nth used socket of a room, or -1. This is the surface
 * the 3D window reads to draw a faceplate with a lead in it. */
int  site_room_outlet_dev(const Site *s, int room, int nth);

/* Put the plug in, or pull it out.
 *
 * PULLING IT OUT OF A RUNNING MACHINE IS A BLACKOUT WITH ONE MACHINE IN IT,
 * and it is not softened: the box goes down the way core/siteday.c takes a
 * box down when the building loses the mains, with the same damage, in the
 * same words, in `events`. A box on a battery gets what a battery is for --
 * the load transfers, nomups sees no utility power coming back, and it shuts
 * the machine down in an orderly way, so it comes up in the morning with
 * nothing to check. That is the second place the two hundred and twenty
 * pounds pays for itself, and the first one the player chose. */
bool site_mains(Site *s, int dev, bool on);

/* HAVE ANOTHER SOCKET PUT IN. Charged now and in for good -- there is no
 * verb that takes one out and nothing is refunded, the same as a jack.
 *
 * It is NOT days. A jack already owns "a trade has to come and that is the
 * clock", and D23's own record says why a second copy would be worth
 * nothing: with nothing else different, waiting is free once the player
 * learns to order early. Power is also the one thing that must never be a
 * maze in the first five minutes -- a player who has just carried a server
 * up eight floors into a full cupboard needs a way out of it today. So an
 * outlet is money, now, and the price is the run: a flat fit-out plus the
 * tray metres from that room back to the riser the power comes up, which is
 * geometry the building already knows and which makes a socket in a far
 * corner of a floor dearer than one in the cupboard against the shaft. */
long site_outlet_price(const Site *s, int room);
int  site_outlet(Site *s, int room);          /* the socket, or -1 + s->err */
/* THE POWER MAP. Every room on a floor that has sockets or kit in it: what
 * it was wired with, what has been added, what is plugged in, what is free
 * and what another one would cost. `floor` of -1 is the whole building. This
 * is the "mini map for the entire area" the request asked for, and it is the
 * model's answer rather than the window's. */
void site_dump_outlets(const Site *s, int floor, Buf *out);

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
int  site_room_by_name(const Site *s, const char *spec);
/* HOW MANY ROOMS THAT SPELLING REALLY MATCHES, because one of them is what
 * site_room_by_name() hands back and it never said there were twelve.
 * `#41` is 1, `f1.comms` is 1, `f2.office` on a let floor is 12. `first`,
 * when it is not NULL, gets the room the shorthand resolves to. */
int  site_room_name_matches(const Site *s, const char *spec, int *first);
/* And the sentence a verb prints when it was more than one: which room it
 * picked, whose it is, what the others are, and the two spellings that name
 * one for certain. Prints nothing at all when the name was unambiguous. */
void site_room_ambiguity(const Site *s, const char *spec, int picked, Buf *out);   /* "f3.comms", "#41" */
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

/* THE DAY'S WORK, SUMMED FROM THE ROWS `service` PRINTS -- and there is no
 * second place it is counted.
 *
 * `status` and the `day` line used to print SiteDay.sessions, which is every
 * unit of work the TOWER carried, while `service` printed each tenancy's own
 * `tried`/`finished`, which is what that tenancy is JUDGED on. A voice
 * agent's CRM traffic is the first and not the second, so on a two-tenancy
 * playtest the headline said 134 transfers and the rows said 80 + 18 = 98,
 * with nothing anywhere saying a call carries two more transfers behind it.
 * Both numbers were true and the player could not reconcile them.
 *
 * So the headline is now literally the sum of the rows: this walks the same
 * integers `site_dump_service` prints and adds them up. `unit` comes back as
 * the trade's own word when every tenancy that did any work is in the same
 * trade, and "jobs" when the building holds a mix -- so the headline never
 * calls a call a transfer again. Pass NULL for anything you do not want. */
void site_day_work(const Site *s, int *done, int *tried, const char **unit);

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
/* TAKE ONE BOX DOWN THE WAY THE MAINS TAKES THEM ALL DOWN. The blackout path
 * in core/siteday.c, for one machine, because a player who pulls a plug out
 * of a running server has done exactly what the building does at 04:12 and
 * the machine must not be able to tell the difference. Returns true if it
 * went down dirty; false if a battery let it stop cleanly, or if it was not
 * running in the first place. */
bool site_unclean_stop(Site *s, int dev);
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
/* AND THE LEGENDS, WHICH ARE NO LONGER PRINTED EVERY TIME.
 *
 * Both pages carried thirty-odd lines of explanation on every call, so by
 * day 60 a blind playtester's `service` was ninety per cent legend and the
 * four `**` lines of the day-31 disaster were nearly lost in it. They asked
 * for the legend behind `service ?`, and this is it -- `service ?` and
 * `load ?` in the shell.
 *
 * The split is not "shorten it". The short page keeps every number that is a
 * MEASUREMENT of this building today, plus the one instruction that changes
 * what a player does with the rows. The legend keeps the sentences that
 * explain what a column MEANS, which are the same on day 1 and day 60 and
 * are therefore the part worth reading once. Nothing was deleted, and
 * --sitecheck asserts sentence by sentence that everything that used to be
 * on the page is still reachable from it. */
void site_dump_service_legend(const Site *s, Buf *out);
void site_dump_load_legend(const Site *s, Buf *out);

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
