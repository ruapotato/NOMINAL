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
    SES_SHELL       /* at a real shell on a real machine, via the cart     */
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
    int      spool_kind;       /* CableKind, or -1                         */
    int      spool_left;       /* metres left on the drum                  */
    int      cab_dev, cab_port; /* the end already in a socket, or -1      */
    /* A BOX YOU HAVE ACTUALLY OPENED. A booted machine is 13.5 MB, so one
     * is installed the first time somebody plugs a serial lead into a pc or
     * a server and never before. Its address comes off its own disk and its
     * node is the one the player's cable is already in. */
    Machine *mach[SITE_MAX_DEV];
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
