#ifndef NOMINAL_SHIP_H
#define NOMINAL_SHIP_H
/* THE SHIP, IN METRES.
 *
 * This replaces core/building.h. The old generator made a plate per deck out
 * of rectangles and dealt rooms into it, which is how you draw an office and
 * is why every attempt to make it read as a spacecraft failed: the shape was
 * a shoebox before anything was placed in it.
 *
 * A ship is described the way a ship really is described -- by FRAMES. A frame
 * is a slice across the hull at some station along the keel, and the hull is
 * what you get by lofting one frame into the next. Every deck, every room and
 * every metre of cable afterwards has to fit inside that envelope, which is the
 * right way round: the hull is the fact and the interior is fitted to it.
 *
 * COORDINATES. x runs from the bow (0) to the stern (loa). y is up. z is to
 * starboard. Everything is metres, and the game charges for them.
 */

#include "nom.h"

/* A FRAME PER METRE OF KEEL, so the deck fitter never has to interpolate a
 * wall position -- which means this has to be longer than the longest ship,
 * with room to spare. It was 96 against a 171 m hull, and frame_add() drops
 * silently when it is full: the first render came out with the whole stern
 * missing and the drive ring hanging in space behind nothing. */
#define SHIP_MAX_FRAME  320
#define SHIP_MAX_HULL    8

/* One slice across the hull. `half_w` is the half beam and `half_h` the half
 * height, so a frame is an ellipse 2*half_w by 2*half_h centred on the keel
 * and lifted by `cy`. A flat-bottomed hull is a positive cy with a smaller
 * lower lobe; the generator uses that for the command section, which wants to
 * read as a wide low wedge rather than a tube. */
typedef struct {
    int16_t x;          /* station along the keel, metres from the bow */
    int16_t half_w;     /* half beam                                   */
    int16_t half_h;     /* half height                                 */
    int16_t cy;         /* how far the section's centre sits above the keel */
} Frame;

/* WHAT A RUN OF FRAMES IS FOR. The hull is not one solid: it is a few named
 * bodies, and the names are what decide where systems logically live. */
typedef enum {
    HULL_COMMAND = 0,   /* the bow: bridge, and the crew's own spaces      */
    HULL_NECK,          /* the spine joining the two, and the riser in it  */
    HULL_ENGINEERING,   /* the deep hull: computer core, power, life support */
    HULL_KIND_COUNT
} HullKind;

typedef struct {
    uint8_t kind;
    int16_t frame0, nframe;   /* a run of frames[] */
} Hull;

/* THE DRIVE RING. Two pylons off the engineering hull carrying a torus the
 * ship's keel passes through -- which is where David's donut ends up, after
 * three attempts to make a doughnut-shaped STATION read as anything other than
 * a corridor bent in a circle. As a drive it is the silhouette. */
typedef struct {
    int16_t cx;         /* where on the keel the ring is centred */
    int16_t radius;     /* to the middle of the torus tube       */
    int16_t tube;       /* the tube's own radius                 */
    int16_t pylon_w;    /* the pylons' thickness                 */
} Ring;

/* ------------------------------------------------------------------ shafts
 *
 * David: "make sure that you can get to everywhere on the ship. Either by
 * turbo lifts in certain areas or stairwells. Potentially both. You might have
 * to go down to a deck to cross the neck and then go up, for example."
 *
 * That last sentence is the design. A hull this shape does NOT give a walkable
 * route between the bow and the stern on every deck -- measured on seed 1,
 * deck 2 has 2287 m2 of floor and only 220 of it is reachable from the bow --
 * and the answer is not to bend the hull until it does. The answer is vertical
 * connection, and the detour is the game: a severed shaft is a part of the ship
 * you now have to go round.
 *
 * A shaft is a small footprint punched through a run of decks. Where it can go
 * is a fact about the hull -- it needs floor at that spot on every deck it
 * serves -- so they are SEARCHED for rather than placed. */
#define SHIP_MAX_SHAFT 10
#define SHIP_SHAFT_R    2      /* half-width of the shaft, metres */

typedef enum { SHAFT_LIFT = 0, SHAFT_STAIR } ShaftKind;

typedef struct {
    uint8_t kind;
    int16_t x, z;             /* centre, metres */
    int16_t deck0, deck1;     /* the run of decks it serves, inclusive */
} Shaft;

typedef struct {
    uint64_t seed;
    int      loa;              /* length overall, metres */
    int      beam;             /* widest half beam * 2   */
    Frame    frame[SHIP_MAX_FRAME];
    int      nframe;
    Hull     hull[SHIP_MAX_HULL];
    int      nhull;
    Ring     ring;
    Shaft    shaft[SHIP_MAX_SHAFT];
    int      nshaft;
    int      decks;            /* how many decks the envelope will take */
    int      deck_h;           /* floor to floor, metres                */
} Ship;

/* Build one. Deterministic in the seed, like everything else here. */
bool ship_generate(Ship *s, uint64_t seed);
void ship_free(Ship *s);

/* The hull's half beam and half height at any station, interpolated between
 * the frames either side -- which is what both the renderer and, later, the
 * deck fitter ask. Returns false off the ends of the ship. */
bool ship_section(const Ship *s, double x, double *half_w, double *half_h,
                  double *cy);

/* Is this point inside the pressure hull? The one question the deck fitter
 * will ask a few hundred thousand times. */
bool ship_inside(const Ship *s, double x, double y, double z);

/* ------------------------------------------------------------------ decks
 *
 * A deck is a horizontal slice of the hull, and where it EXISTS is a fact
 * about the hull rather than a plan somebody drew: there is deck at (x,z) only
 * where the pressure hull has standing room above the floor. That is what
 * makes the command section a broad lozenge, the engineering hull a narrow
 * one, and the neck a small connection that only some decks pass through --
 * and it is why routing anything from the bow to the stern is a real problem
 * rather than a corridor somebody chose to draw.
 *
 * SHIP_HEADROOM is what a deck needs to be walkable. Below it the space is
 * still inside the hull, and it is where tanks, trays and the awkward crawl
 * spaces go -- but it is not deck. */
#define SHIP_HEADROOM 2.4

/* The floor height of a deck, in metres above the keel. */
double ship_deck_floor(const Ship *s, int deck);

/* Is there walkable deck here? */
bool ship_deck_at(const Ship *s, int deck, double x, double z);

/* The deck's extent, for anything that wants to walk it or draw it. Returns
 * false when the deck is empty. */
bool ship_deck_bounds(const Ship *s, int deck, double *x0, double *x1,
                      double *z0, double *z1, double *area);

/* Fill in s->shaft[]. Called by ship_generate(). */
int  ship_place_shafts(Ship *s);

/* How many square metres of deck there are, and how many of them can be walked
 * to from `from_deck` at (fx,fz) using the decks and the shafts. Equal means
 * the ship is entirely reachable on foot. */
bool ship_reach(const Ship *s, int from_deck, double fx, double fz,
                long *reached, long *total);


const char *ship_hull_name(int kind);

#endif
