/* session.h — one player, in one building, over one socket.
 *
 * THE RULE THIS FILE EXISTS TO KEEP. From D23: *"the view is never the
 * source of truth. Ordering, carrying, cabling and configuring must all be
 * drivable through a scriptable interface... If it cannot be played over a
 * socket, it cannot be tested, and it will rot."*
 *
 * It had rotted. core/serve.c served the break-fix game -- a ticket, a
 * customer, a workstation and a shell -- and core/site.c served the tower,
 * and the only way to reach the tower was `./build/bf --sitesh`, a different
 * process playing a different game. A blind playtester could have one half
 * or the other and never both, which is precisely the thing D23 said would
 * kill the project's only working quality mechanism.
 *
 * This is the join. A Session is a PERSON: they are somewhere, they can walk
 * to somewhere else, and what they can touch is what is in the room with
 * them. Everything the 3D shell can do -- look round, walk, take the lift,
 * open a floor, push the crash cart up to a box, plug a serial lead into it,
 * run cable off a spool -- is a verb here, calling the same site_* functions
 * game/scripts/tower.gd calls. Neither view is the truth; this struct is.
 *
 * POSITION IS NOT DECORATION. bld_walk_all() gives real metres through real
 * doors, and a switch you have not walked to is a switch you cannot
 * configure. That is the whole reason the building exists, and faking it
 * away to make scripting easier would leave the scriptable interface testing
 * a different game from the one being shipped -- which is the mistake this
 * file is fixing.
 */
#ifndef NOM_SESSION_H
#define NOM_SESSION_H

#include "nom.h"
#include "site.h"
#include "machine.h"

/* Where the words you type are going. */
typedef enum {
    SES_DESK = 0,   /* sitting at your workstation: the break-fix game     */
    SES_BODY,       /* standing in a room of the tower                     */
    SES_MGMT,       /* at a managed box's management line, via the cart    */
    SES_SHELL,      /* at a real shell on a real machine, via the cart     */
    /* SAT IN SOMEBODY ELSE'S CHAIR. A tenancy's desk is a real card in a
     * real broadcast domain, and until somebody sits at it there is no
     * operating system behind that card at all -- because a booted Machine
     * measures 18.3 MB of resident memory on this build (D23 records 13.5 MB
     * of allocation; the rest is the allocator's) and a full tower is 176
     * desks, which would be 3.2 GB. See do_sit() in session.c:
     * the machine is built when you pull the chair out and freed when you
     * stand up, so the tower pays for ONE of them, ever, and only while
     * somebody is looking at it. */
    SES_SEAT,
    /* THE LEAD IS IN AND THERE IS NOTHING ON THE OTHER END OF IT.
     *
     * The owner, on the crash cart: *"if the thing's not powered on, it
     * shouldn't offer a prompt at all. If it's not booting, it shouldn't
     * offer a prompt at all. Potentially maybe a no-connection prompt that
     * gives you the option to attempt to power cycle whatever you're
     * attached to. That way you can watch boot up messages."*
     *
     * This is that state, and it is not a shell with the commands taken
     * away. There IS no shell: a serial line carries what the far end sends
     * and a machine with no power in it sends nothing, so the line takes the
     * four things a person standing at the rack can actually do -- the
     * button, the plug, the live medium on the cart, and putting the lead
     * back -- and answers everything else with the silence that is really
     * there. That silence IS the diagnosis, which is why it is a state and
     * not a refusal: the player is attached to the box, watching, and one
     * `power on` away from the boot messages. */
    SES_NOCON
} SesWhere;

typedef struct {
    bool     up;               /* the tower has been generated             */
    uint64_t seed;
    Building b;
    Site     s;
    int      where;            /* SesWhere                                 */
    int      room;             /* the room you are standing in             */
    int      floors;           /* floors in service. The lift knows.       */
    long     walked;           /* metres, cumulative. It is a cost.        */
    int      plugged;          /* device the cart's lead is in, or -1      */
    bool     hdmi;             /* which lead                               */
    /* THE SPOOL IN YOUR HANDS. A cable run is not an outcome you ask for,
     * it is four things a person does: pick up a drum, put one end in a
     * port, walk to the other end, put the other end in. David: *"for
     * things like cabling, we should have an easy way for agents to do what
     * a person would do moving around."* If the socket could lay a cable
     * without the walk it would be playing a different game from the 3D and
     * would stop being a test of it. -1 in spool_kind means empty hands. */
    /* WHAT IS IN YOUR HANDS. Kit is delivered to goods in on the ground
     * floor and it does not get anywhere else on its own: somebody picks it
     * up, walks it there, and puts it down. That walk is metres of real
     * building and it is why where you put a box is a decision.
     *
     * One box, because both hands are on it -- which is also why you cannot
     * be holding a drum of cable at the same time. Nothing here invents a
     * weight: the catalogue is five things a person can lift, and the
     * limits are the ones the object really has. -1 is empty hands. */
    int      carrying;         /* the device you are carrying, or -1       */
    int      spool_kind;       /* CableKind, or -1                         */
    int      spool_left;       /* metres left on the drum                  */
    int      cab_dev, cab_port; /* the end already in a socket, or -1      */
    /* A BOX YOU HAVE ACTUALLY OPENED. A booted machine is 13.5 MB, so one
     * is installed the first time somebody plugs a serial lead into a pc or
     * a server and never before. Its address comes off its own disk and its
     * node is the one the player's cable is already in. */
    Machine *mach[SITE_MAX_DEV];
    /* THE CHAIR YOU ARE SITTING IN, or -1. One, because a person has one
     * backside: this is the whole of the cap on what the desks cost, and it
     * is a fact about bodies rather than a budget somebody chose. The
     * Machine at mach[seat] exists for exactly as long as this is set. */
    int      seat;
} Session;

/* The tower, and you in the MDF of it. */
bool session_start(Session *ses, uint64_t seed, long budget);
void session_end(Session *ses);

/* ONE LINE, ONE THING. Returns true if the session took the line. At the
 * desk it takes only `tower`, so everything the break-fix game already
 * understood still reaches it untouched. */
bool session_line(Session *ses, const char *line, Buf *out);

/* What to print in front of the cursor. Never lies about where you are:
 * two playtesters lost track and both reported it. */
void session_prompt(const Session *ses, char *out, size_t cap);

/* The gate. Called from core/sitecheck.c. */
int  session_selfcheck(int *passed, int *total);

#endif /* NOM_SESSION_H */
