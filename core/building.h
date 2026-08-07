/* building.h — the tower, as space.
 *
 * The building is the board. A player carries a switch to a room, runs cable
 * off a spool that costs by the metre, or pays for a permanent jack that
 * costs by distance -- so the geometry is not scenery, it is the price list.
 * Everything here is in METRES on an integer grid, and every number the game
 * charges for is derived from it.
 *
 * Two distances, and they are deliberately not the same number:
 *
 *   bld_walk_all()   how far a PERSON goes: through doors, along corridors,
 *                    down the stairs or in the lift.
 *   bld_cable_all()  how far a CABLE goes: up into the tray, along the
 *                    corridor ceiling, through the comms cupboard into the
 *                    riser, and vertically inside it.
 *
 * A person cannot walk up a riser and a cable does not go down the stairs.
 * That gap is the decision the player is making when they choose a route, so
 * the generator has to make it real rather than assert it.
 *
 * The view never owns any of this. There is no 3D anywhere below, and
 * `--floorplan` prints one in ASCII precisely so that the check does not
 * depend on a renderer existing.
 */
#ifndef BUILDING_H
#define BUILDING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nom.h"

#define BLD_MAX_FLOORS  16
#define BLD_MAX_ROOMS   640
#define BLD_MAX_DOORS   768
#define BLD_MAX_W       56
#define BLD_MAX_H       36
#define BLD_NOROOM      0xFFFFu
#define BLD_INF         1e18

/* What a space is FOR. A floor is a mix that makes sense together: an office
 * floor is offices, a lift lobby, toilets and a comms cupboard. It is not
 * three bathrooms in a row, which is what a purely random draw produces. */
typedef enum {
    RM_CORRIDOR = 0,
    RM_LOBBY,        /* entrance hall, ground floor */
    RM_LIFTLOBBY,    /* the bit you wait in, in front of the lifts */
    RM_LIFT,         /* the shaft itself; aligned on every floor */
    RM_STAIR,        /* stairwell; aligned on every floor */
    RM_RISER,        /* services shaft; aligned, and you cannot walk into it */
    RM_COMMS,        /* the floor's comms cupboard, against the riser */
    RM_MDF,          /* main frame room: where the building's uplink lands */
    RM_TOILET,
    RM_PLANT,
    RM_GOODS,        /* goods in. Hardware arrives HERE, not in an inventory */
    RM_OFFICE,
    RM_RESIDENCE,
    RM_SERVER,       /* a tenant's own server room */
    RM_RETAIL,
    RM_BRIDGE,       /* the top deck: command consoles, and a crew at them */
    RM_KIND_COUNT
} RoomKind;

/* What the floor as a whole is, which is what decides the mix. */
typedef enum { FL_GROUND, FL_OFFICE, FL_RESIDENTIAL, FL_PLANT, FL_BRIDGE,
               FL_KIND_COUNT } FloorKind;

typedef struct {
    uint8_t floor;
    uint8_t kind;     /* RoomKind */
    uint8_t tenant;   /* 0 = common parts. Nobody crosses another one's space */
    uint8_t pad;
    int16_t x0, y0, x1, y1;   /* metres, half-open: [x0,x1) x [y0,y1) */
} Room;

/* A door is an EDGE between two cells, not a point floating in a room. It is
 * stored on the lower-coordinate side: dir 0 means the other room is the cell
 * at (x+1,y), dir 1 means (x,y+1). Stated this way a door cannot open into a
 * wall, because there is no way to write one that does not have a room on
 * both sides -- and the gate checks that anyway. */
typedef struct {
    uint16_t a, b;          /* room indices; a is the (x,y) side */
    int16_t  x, y;
    uint8_t  floor, dir;
} Door;

typedef struct {
    uint64_t seed;
    int      floors;
    int      w, h;              /* the base footprint, in metres */
    double   floor_height;

    /* Per-floor footprint. Floor f+1 is contained in floor f: setbacks are
     * allowed, floating floor plates are not. */
    int16_t  fx0[BLD_MAX_FLOORS], fy0[BLD_MAX_FLOORS];
    int16_t  fx1[BLD_MAX_FLOORS], fy1[BLD_MAX_FLOORS];
    uint8_t  fkind[BLD_MAX_FLOORS];

    /* The core and the corridor ring around it. One rectangle for the whole
     * building: this is why the risers line up. */
    int16_t  core_x0, core_y0, core_x1, core_y1;
    int16_t  ring_x0, ring_y0, ring_x1, ring_y1;

    int      nrooms, ndoors, ntenants;
    Room     rooms[BLD_MAX_ROOMS];
    Door     doors[BLD_MAX_DOORS];

    uint16_t *cell;      /* floors*h*w room index, BLD_NOROOM outside */
    uint8_t  *edge;      /* bit0: door to (x+1,y)  bit1: door to (x,y+1) */

    /* The cable network, as a graph: node i<nrooms is a room's service point,
     * the rest are tray cells above the corridor. */
    int      cg_n, cg_nedge;
    int     *cg_head, *cg_to;
    double  *cg_w;
} Building;

bool  bld_generate(Building *b, uint64_t seed);
void  bld_free(Building *b);

const char *bld_kind_name(int kind);
char        bld_kind_char(int kind);
const char *bld_floor_kind_name(int kind);

/* Room lookups. -1 when there is none. */
int   bld_find(const Building *b, int floor, int kind);
int   bld_room_at(const Building *b, int floor, int x, int y);
double bld_room_area(const Room *r);

/* Shortest distance in metres from `src` to every room, BLD_INF where there
 * is no route. `out` must hold b->nrooms doubles. */
bool  bld_walk_all (const Building *b, int src, double *out);
bool  bld_cable_all(const Building *b, int src, double *out);

/* The checks, by name, so the gate can say which one failed. */
typedef enum {
    BC_STACK = 0, BC_TESSELLATE, BC_ROOMSIZE, BC_ALIGN, BC_DOORS,
    BC_ROOMDOOR, BC_CORRIDOR, BC_REACH, BC_PRIVACY, BC_PROGRAM,
    BC_METRIC, BC_DIFFER, BC_COUNT
} BldCheck;
const char *bld_check_name(int c);

/* Returns the number of failures; appends one line per failure to `out`,
 * and if `fails` is given, sets fails[c] non-zero for each failing check. */
int   bld_check(const Building *b, Buf *out, int *fails);

/* An ASCII plan of one floor, walls and all, with a legend. */
void  bld_floorplan(const Building *b, int floor, Buf *out);

#endif /* BUILDING_H */
