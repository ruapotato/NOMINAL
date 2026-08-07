/* building.c — a tower that has to hold up.
 *
 * The layout is not a maze algorithm with rooms bolted on. It is the shape a
 * real tower has, because that shape is the one that survives the checks:
 *
 *     a fixed CORE in the middle -- stairs, lifts, risers, toilets, the
 *     comms cupboard -- computed ONCE for the whole building;
 *     a CORRIDOR RING two metres wide all the way around it;
 *     a band of rooms on the perimeter, every one of them fronting the ring.
 *
 * Everything hard follows from that single decision. Risers line up on every
 * floor because there is one core rectangle, not one per floor. The corridor
 * never dead-ends because a loop has no ends. Nobody walks through anybody
 * else's office because every room's only door is onto common space.
 *
 * THE CORNERS ARE THE PART THAT BITES. The ring is inset from the facade, so
 * the four regions diagonally outside its corners touch it at a POINT and a
 * point is not a door. The fix is in split_span(): every cut in the north and
 * south bands is constrained to lie between the core's own x extents, which
 * forces the end rooms to wrap the corner far enough to get a real edge on
 * the ring. Get that wrong and the plan looks perfect and has four offices
 * you cannot enter.
 *
 * Setbacks work the other way round from how you would write them: the TOP
 * floor plate is generated first and the lower ones grow outward from it.
 * Shrinking downward-up means the core is sized from a floor that a later
 * setback can cut into, and then the riser is outside the building.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "building.h"

#define DIAG        1.4142135623730951
/* A cable leaves the tray at the ceiling and drops to the socket. Both ends
 * pay it, which is why a two-metre patch across a corridor is not two metres. */
#define TRAY_DROP   2.7
/* Walked length of the flights and landings between two floors, as a multiple
 * of the storey height. A lift carries you the rise itself, plus the walk in
 * and out of the car. */
#define STAIR_RUN   2.2
#define LIFT_BOARD  4.0
/* THE LADDER IN THE RISER, and why it costs more than the stairs.
 *
 * The owner: "there's a room in called riser, that seems to be an empty
 * elevator shaft... potentially the riser room should be left kind of a
 * corridor where you run cables. But with a ladder so you can actually climb
 * up and down."
 *
 * So a riser is a room you can walk into now, and the ladder is a way up. It
 * is deliberately the WORST way up: a storey of ladder is dearer, in walked
 * metres, than a storey of stairs, because a ladder is slow and you climb it
 * one rung at a time with your hands. It exists so that a person who is IN
 * the riser -- following a cable, looking at what is in it -- can get to the
 * next floor's riser without walking the whole corridor to the stairwell and
 * back. It is not a shortcut anybody would route a journey through, and the
 * numbers say so rather than a rule saying so. */
#define LADDER_CLIMB 3.4

#define CIDX(b,f,x,y) ((((size_t)(f) * (size_t)(b)->h + (size_t)(y)) * (size_t)(b)->w) + (size_t)(x))

static uint16_t cell_at(const Building *b, int f, int x, int y)
{
    if (f < 0 || f >= b->floors || x < 0 || y < 0 || x >= b->w || y >= b->h)
        return BLD_NOROOM;
    return b->cell[CIDX(b, f, x, y)];
}

static int cell_kind(const Building *b, int f, int x, int y)
{
    uint16_t r = cell_at(b, f, x, y);
    return r == BLD_NOROOM ? -1 : b->rooms[r].kind;
}

int bld_room_at(const Building *b, int floor, int x, int y)
{
    uint16_t r = cell_at(b, floor, x, y);
    return r == BLD_NOROOM ? -1 : (int)r;
}

double bld_room_area(const Room *r)
{
    return (double)(r->x1 - r->x0) * (double)(r->y1 - r->y0);
}

const char *bld_kind_name(int k)
{
    static const char *N[RM_KIND_COUNT] = {
        "corridor", "lobby", "lift lobby", "lift shaft", "stairwell",
        "riser", "comms cupboard", "MDF", "toilets", "plant", "goods in",
        "office", "residence", "server room", "retail"
    };
    return (k >= 0 && k < RM_KIND_COUNT) ? N[k] : "?";
}

char bld_kind_char(int k)
{
    static const char C[RM_KIND_COUNT] = {
        ' ', 'L', 'l', 'V', 'X', 'R', 'C', 'M', 'W', 'P', 'G',
        'o', 'r', 'S', 'e'
    };
    return (k >= 0 && k < RM_KIND_COUNT) ? C[k] : '?';
}

const char *bld_floor_kind_name(int k)
{
    static const char *N[FL_KIND_COUNT] = { "ground", "office", "residential", "plant" };
    return (k >= 0 && k < FL_KIND_COUNT) ? N[k] : "?";
}

const char *bld_check_name(int c)
{
    static const char *N[BC_COUNT] = {
        "floors stack, no floating plates",
        "every square metre is in exactly one room",
        "rooms are a usable size",
        "risers, lifts and stairs align on every floor",
        "every door has a room on both sides",
        "every room has a way in",
        "the corridor is one loop with no dead ends",
        "every room is reachable on foot",
        "nobody crosses another tenant's space",
        "the floor's mix makes sense",
        "distances are a metric",
        "walking and cabling are different numbers"
    };
    return (c >= 0 && c < BC_COUNT) ? N[c] : "?";
}

int bld_find(const Building *b, int floor, int kind)
{
    for (int i = 0; i < b->nrooms; i++)
        if (b->rooms[i].floor == floor && b->rooms[i].kind == kind) return i;
    return -1;
}

/* ------------------------------------------------------------- generation */

static int add_room(Building *b, int floor, int kind, int tenant,
                    int x0, int y0, int x1, int y1)
{
    if (b->nrooms >= BLD_MAX_ROOMS) return -1;
    if (x1 <= x0 || y1 <= y0) return -1;
    Room *r = &b->rooms[b->nrooms];
    r->floor = (uint8_t)floor; r->kind = (uint8_t)kind;
    r->tenant = (uint8_t)tenant; r->pad = 0;
    r->x0 = (int16_t)x0; r->y0 = (int16_t)y0;
    r->x1 = (int16_t)x1; r->y1 = (int16_t)y1;
    return b->nrooms++;
}

/* A door on the edge between (x,y) and its neighbour in `dir`. Both sides are
 * looked up in the painted grid, so a door into open air simply cannot be
 * recorded: it returns false and the building fails to generate. */
static bool add_door(Building *b, int floor, int x, int y, int dir)
{
    int nx = x + (dir == 0), ny = y + (dir == 1);
    uint16_t a = cell_at(b, floor, x, y), c = cell_at(b, floor, nx, ny);
    if (a == BLD_NOROOM || c == BLD_NOROOM || a == c) return false;
    if (b->ndoors >= BLD_MAX_DOORS) return false;
    Door *d = &b->doors[b->ndoors++];
    d->a = a; d->b = c; d->x = (int16_t)x; d->y = (int16_t)y;
    d->floor = (uint8_t)floor; d->dir = (uint8_t)dir;
    b->edge[CIDX(b, floor, x, y)] |= (uint8_t)(dir == 0 ? 1 : 2);
    return true;
}

/* Cut [lo,hi) into n rooms. Every interior cut is forced into [cutlo,cuthi]
 * and no two cuts come closer than minw. For the north and south bands that
 * window is the core's x extent, which is what drags the end rooms around the
 * ring corner far enough to have a door. */
static int split_span(Rng *r, int lo, int hi, int cutlo, int cuthi,
                      int minw, int target, int minrooms, int maxrooms,
                      int *cuts)
{
    int span = hi - lo;
    int n = target > 0 ? span / target : 1;
    if (n < minrooms) n = minrooms;
    if (n > maxrooms) n = maxrooms;
    while (n > minrooms && (n - 2) * minw > (cuthi - cutlo)) n--;
    if (n < 1) n = 1;
    for (int i = 0; i < n - 1; i++) {
        int base = cutlo + (int)((long)(i + 1) * (long)(cuthi - cutlo) / n);
        int j = base + rng_range(r, -2, 2);
        int prev = (i == 0) ? cutlo : cuts[i - 1] + minw;
        int top  = cuthi - (n - 2 - i) * minw;
        if (j < prev) j = prev;
        if (j > top)  j = top;
        cuts[i] = j;
    }
    return n;
}

/* The mix on a floor, given what the floor is for. The list is walked in ring
 * order, so a tenant gets a run of adjacent rooms rather than a scatter. */
static int perimeter_kind(FloorKind fk)
{
    switch (fk) {
    case FL_GROUND:      return RM_RETAIL;
    case FL_OFFICE:      return RM_OFFICE;
    case FL_RESIDENTIAL: return RM_RESIDENCE;
    default:             return RM_PLANT;
    }
}

bool bld_generate(Building *b, uint64_t seed)
{
    memset(b, 0, sizeof *b);
    b->seed = seed;
    Rng r; rng_seed(&r, seed);

    b->floors       = rng_range(&r, 5, BLD_MAX_FLOORS - 4);
    b->floor_height = 3.2 + 0.2 * (double)rng_range(&r, 0, 3);

    /* The TOP plate first. The core is sized from the smallest floor, so no
     * later setback can cut a riser out of the building. */
    int tw = rng_range(&r, 38, 44);
    int th = rng_range(&r, 22, 26);

    int shx0[BLD_MAX_FLOORS] = {0}, shy0[BLD_MAX_FLOORS] = {0};
    int shx1[BLD_MAX_FLOORS] = {0}, shy1[BLD_MAX_FLOORS] = {0};
    int bx = 4, by = 4;   /* how much footprint the setbacks may still eat */
    int nsb = rng_range(&r, 0, 2);
    for (int e = 0; e < nsb; e++) {
        int f = rng_range(&r, 2, b->floors - 1);
        int a0 = rng_range(&r, 0, bx < 2 ? bx : 2); bx -= a0;
        int a1 = rng_range(&r, 0, bx < 2 ? bx : 2); bx -= a1;
        int c0 = rng_range(&r, 0, by < 2 ? by : 2); by -= c0;
        int c1 = rng_range(&r, 0, by < 2 ? by : 2); by -= c1;
        for (int k = f; k < b->floors; k++) {
            shx0[k] += a0; shx1[k] += a1; shy0[k] += c0; shy1[k] += c1;
        }
    }
    int top = b->floors - 1;
    b->w = tw + shx0[top] + shx1[top];
    b->h = th + shy0[top] + shy1[top];
    if (b->w > BLD_MAX_W || b->h > BLD_MAX_H) return false;

    for (int f = 0; f < b->floors; f++) {
        b->fx0[f] = (int16_t)shx0[f];              b->fy0[f] = (int16_t)shy0[f];
        b->fx1[f] = (int16_t)(b->w - shx1[f]);     b->fy1[f] = (int16_t)(b->h - shy1[f]);
    }

    /* Ring and core, from the top plate, in the building's own coordinates. */
    b->ring_x0 = (int16_t)(b->fx0[top] + 6); b->ring_y0 = (int16_t)(b->fy0[top] + 6);
    b->ring_x1 = (int16_t)(b->fx1[top] - 6); b->ring_y1 = (int16_t)(b->fy1[top] - 6);
    b->core_x0 = (int16_t)(b->ring_x0 + 2);  b->core_y0 = (int16_t)(b->ring_y0 + 2);
    b->core_x1 = (int16_t)(b->ring_x1 - 2);  b->core_y1 = (int16_t)(b->ring_y1 - 2);
    int CW = b->core_x1 - b->core_x0, CH = b->core_y1 - b->core_y0;
    if (CW < 21 || CH < 6) return false;

    /* --- the core, laid out once and reused verbatim on every floor ------
     * stairwell | toilets | toilets | lifts | riser over comms.
     * The riser and the comms cupboard share a slot with a door between them,
     * because a comms cupboard that is not against the riser makes every
     * cable on the floor longer for no reason. */
    int ws = 4, w1 = 3, w2 = 3, wl = 6, wrc = 4;
    int spare = CW - (ws + w1 + w2 + wl + wrc);
    for (int i = 0; spare > 0; i++, spare--) {
        switch (i % 5) {
        case 0: wl++;  break;
        case 1: ws++;  break;
        case 2: wrc++; break;
        case 3: w1++;  break;
        default: w2++; break;
        }
    }
    bool mirror = rng_range(&r, 0, 1) != 0;
    int order[5] = { 0, 1, 2, 3, 4 };     /* stair, wc, wc, lift, riser+comms */
    int wid[5]   = { ws, w1, w2, wl, wrc };
    if (mirror) {
        for (int i = 0; i < 2; i++) {
            int t = order[i]; order[i] = order[4 - i]; order[4 - i] = t;
            t = wid[i]; wid[i] = wid[4 - i]; wid[4 - i] = t;
        }
    }

    b->cell = nom_alloc((size_t)b->floors * (size_t)b->w * (size_t)b->h * sizeof(uint16_t));
    b->edge = nom_alloc((size_t)b->floors * (size_t)b->w * (size_t)b->h);
    for (size_t i = 0; i < (size_t)b->floors * (size_t)b->w * (size_t)b->h; i++)
        b->cell[i] = BLD_NOROOM;
    memset(b->edge, 0, (size_t)b->floors * (size_t)b->w * (size_t)b->h);

    /* Which floor is what. Offices at the bottom, homes above them, and now
     * and then a plant floor on the roof -- the stacking of a real mixed-use
     * tower, and the reason a residential floor's cabling problem is a
     * different problem from an office floor's. */
    int resi_from = rng_range(&r, 2, b->floors);
    bool plant_top = rng_range(&r, 0, 99) < 35 && b->floors >= 6;
    for (int f = 0; f < b->floors; f++) {
        b->fkind[f] = (uint8_t)(f == 0 ? FL_GROUND
                      : (plant_top && f == b->floors - 1) ? FL_PLANT
                      : (f >= resi_from) ? FL_RESIDENTIAL : FL_OFFICE);
    }

    int tenant_next = 1;

    for (int f = 0; f < b->floors; f++) {
        FloorKind fk = (FloorKind)b->fkind[f];
        int fx0 = b->fx0[f], fy0 = b->fy0[f], fx1 = b->fx1[f], fy1 = b->fy1[f];
        int rx0 = b->ring_x0, ry0 = b->ring_y0, rx1 = b->ring_x1, ry1 = b->ring_y1;
        int cx0 = b->core_x0, cy0 = b->core_y0, cx1 = b->core_x1, cy1 = b->core_y1;

        /* The ring, as four legs. They are separate rectangles because a Room
         * is a rectangle; circulation is open between them, and the gate
         * checks the loop is actually connected rather than trusting that. */
        add_room(b, f, RM_CORRIDOR, 0, rx0, ry0, rx1, ry0 + 2);
        add_room(b, f, RM_CORRIDOR, 0, rx0, ry1 - 2, rx1, ry1);
        add_room(b, f, RM_CORRIDOR, 0, rx0, ry0 + 2, rx0 + 2, ry1 - 2);
        add_room(b, f, RM_CORRIDOR, 0, rx1 - 2, ry0 + 2, rx1, ry1 - 2);

        /* The core. */
        int x = cx0, stair = -1, lobby = -1, lifta = -1, liftb = -1;
        int riser = -1, comms = -1, wcs[2] = { -1, -1 }, nwc = 0;
        for (int s = 0; s < 5; s++) {
            int wdt = wid[s], x1 = x + wdt;
            switch (order[s]) {
            case 0:
                stair = add_room(b, f, RM_STAIR, 0, x, cy0, x1, cy1);
                break;
            case 1: case 2:
                wcs[nwc++] = add_room(b, f, RM_TOILET, 0, x, cy0, x1, cy1);
                break;
            case 3:
                lobby = add_room(b, f, RM_LIFTLOBBY, 0, x, cy0, x1, cy0 + 3);
                lifta = add_room(b, f, RM_LIFT, 0, x, cy0 + 3, x + wdt / 2, cy1);
                liftb = add_room(b, f, RM_LIFT, 0, x + wdt / 2, cy0 + 3, x1, cy1);
                break;
            default:
                riser = add_room(b, f, RM_RISER, 0, x, cy0, x1, cy0 + 3);
                comms = add_room(b, f, RM_COMMS, 0, x, cy0 + 3, x1, cy1);
                break;
            }
            x = x1;
        }
        if (stair < 0 || lobby < 0 || riser < 0 || comms < 0 || nwc != 2) return false;

        /* The perimeter band. North and south run the full width and wrap the
         * corners; east and west take what is left between them. */
        int first_peri = b->nrooms;
        int cuts[16], n;
        int pk = perimeter_kind(fk);

        n = split_span(&r, fx0, fx1, cx0, cx1, 4, 9, 2, 5, cuts);
        for (int i = 0; i < n; i++) {
            int a = i ? cuts[i - 1] : fx0, c = (i < n - 1) ? cuts[i] : fx1;
            add_room(b, f, pk, 0, a, fy0, c, ry0);
        }
        n = split_span(&r, ry0, ry1, ry0 + 4, ry1 - 4, 4, 8, 1, 2, cuts);
        for (int i = 0; i < n; i++) {
            int a = i ? cuts[i - 1] : ry0, c = (i < n - 1) ? cuts[i] : ry1;
            add_room(b, f, pk, 0, fx0, a, rx0, c);
        }
        n = split_span(&r, fx0, fx1, cx0, cx1, 4, 9, 2, 5, cuts);
        for (int i = 0; i < n; i++) {
            int a = i ? cuts[i - 1] : fx0, c = (i < n - 1) ? cuts[i] : fx1;
            add_room(b, f, pk, 0, a, ry1, c, fy1);
        }
        n = split_span(&r, ry0, ry1, ry0 + 4, ry1 - 4, 4, 8, 1, 2, cuts);
        for (int i = 0; i < n; i++) {
            int a = i ? cuts[i - 1] : ry0, c = (i < n - 1) ? cuts[i] : ry1;
            add_room(b, f, pk, 0, rx1, a, fx1, c);
        }
        int last_peri = b->nrooms;

        /* Give the perimeter its purpose. */
        if (fk == FL_GROUND) {
            /* The entrance faces the lift lobby across the north corridor;
             * the MDF goes in the south band nearest the riser, because the
             * building's uplink has to get into the riser and every metre of
             * that is a metre the player pays for. */
            int best_n = -1, best_s = -1;
            long bd_n = 1L << 30, bd_s = 1L << 30;
            int rc = (b->rooms[riser].x0 + b->rooms[riser].x1) / 2;
            int lc = (b->rooms[lobby].x0 + b->rooms[lobby].x1) / 2;
            for (int i = first_peri; i < last_peri; i++) {
                Room *rm = &b->rooms[i];
                if (rm->y1 <= ry0) {
                    long d = rm->x0 + rm->x1 - 2 * lc; if (d < 0) d = -d;
                    if (d < bd_n) { bd_n = d; best_n = i; }
                } else if (rm->y0 >= ry1) {
                    long d = rm->x0 + rm->x1 - 2 * rc; if (d < 0) d = -d;
                    if (d < bd_s) { bd_s = d; best_s = i; }
                }
            }
            if (best_n >= 0) b->rooms[best_n].kind = RM_LOBBY;
            if (best_s >= 0) b->rooms[best_s].kind = RM_MDF;
            int nset = 0;
            for (int i = first_peri; i < last_peri && nset < 2; i++) {
                if (i == best_n || i == best_s) continue;
                b->rooms[i].kind = (uint8_t)(nset == 0 ? RM_GOODS : RM_PLANT);
                nset++;
            }
        } else if (fk == FL_OFFICE || fk == FL_RESIDENTIAL) {
            int per = last_peri - first_peri;
            if (fk == FL_RESIDENTIAL) {
                for (int i = first_peri; i < last_peri; i++)
                    b->rooms[i].tenant = (uint8_t)(tenant_next < 250 ? tenant_next++ : 250);
            } else {
                int nt = rng_range(&r, 1, per >= 8 ? 3 : 2);
                int at = first_peri;
                for (int t = 0; t < nt; t++) {
                    int take = (per - (at - first_peri)) / (nt - t);
                    if (take < 1) take = 1;
                    int id = tenant_next < 250 ? tenant_next++ : 250;
                    for (int i = at; i < at + take && i < last_peri; i++)
                        b->rooms[i].tenant = (uint8_t)id;
                    at += take;
                }
                for (int i = at; i < last_peri; i++)
                    b->rooms[i].tenant = b->rooms[last_peri - 1].tenant;
                /* Somebody on this floor keeps their own kit in a cupboard of
                 * their own, which is a different cable problem from the
                 * comms cupboard in the core. */
                if (rng_range(&r, 0, 99) < 30 && per > 2) {
                    int pick = first_peri + rng_range(&r, 0, per - 1);
                    b->rooms[pick].kind = RM_SERVER;
                }
            }
        }

        /* --- paint, then hang the doors -------------------------------- */
        for (int i = 0; i < b->nrooms; i++) {
            Room *rm = &b->rooms[i];
            if (rm->floor != f) continue;
            for (int yy = rm->y0; yy < rm->y1; yy++)
                for (int xx = rm->x0; xx < rm->x1; xx++) {
                    size_t ci = CIDX(b, f, xx, yy);
                    if (b->cell[ci] != BLD_NOROOM) return false;  /* overlap */
                    b->cell[ci] = (uint16_t)i;
                }
        }

        /* Core doors: everything opens onto the ring except the lift cars,
         * which open into their lobby, and the riser, which opens into the
         * comms cupboard. */
        for (int i = 0; i < 2; i++) {
            Room *rm = &b->rooms[wcs[i]];
            if (!add_door(b, f, (rm->x0 + rm->x1) / 2, cy0 - 1, 1)) return false;
        }
        {
            Room *rm = &b->rooms[stair];
            if (!add_door(b, f, (rm->x0 + rm->x1) / 2, cy0 - 1, 1)) return false;
        }
        {
            Room *rm = &b->rooms[lobby];
            if (!add_door(b, f, (rm->x0 + rm->x1) / 2, cy0 - 1, 1)) return false;
        }
        {
            Room *ra = &b->rooms[lifta], *rb = &b->rooms[liftb];
            if (!add_door(b, f, (ra->x0 + ra->x1) / 2, cy0 + 2, 1)) return false;
            if (!add_door(b, f, (rb->x0 + rb->x1) / 2, cy0 + 2, 1)) return false;
        }
        {
            Room *rm = &b->rooms[comms];
            if (!add_door(b, f, (rm->x0 + rm->x1) / 2, cy1 - 1, 1)) return false;
            if (!add_door(b, f, (rm->x0 + rm->x1) / 2, cy0 + 2, 1)) return false;
        }

        /* Perimeter doors, each onto the leg of the ring it actually touches. */
        for (int i = first_peri; i < last_peri; i++) {
            Room *rm = &b->rooms[i];
            bool ok;
            if (rm->y1 <= ry0) {                 /* north band */
                int lo = rm->x0 > rx0 ? rm->x0 : rx0;
                int hi = rm->x1 < rx1 ? rm->x1 : rx1;
                if (hi <= lo) return false;
                ok = add_door(b, f, (lo + hi - 1) / 2, ry0 - 1, 1);
            } else if (rm->y0 >= ry1) {          /* south band */
                int lo = rm->x0 > rx0 ? rm->x0 : rx0;
                int hi = rm->x1 < rx1 ? rm->x1 : rx1;
                if (hi <= lo) return false;
                ok = add_door(b, f, (lo + hi - 1) / 2, ry1 - 1, 1);
            } else if (rm->x1 <= rx0) {          /* west band */
                ok = add_door(b, f, rx0 - 1, (rm->y0 + rm->y1 - 1) / 2, 0);
            } else {                             /* east band */
                ok = add_door(b, f, rx1 - 1, (rm->y0 + rm->y1 - 1) / 2, 0);
            }
            if (!ok) return false;
        }
    }
    b->ntenants = tenant_next - 1;

    /* --- the cable network ------------------------------------------------
     * Nodes: one per room (its service point), plus every corridor cell,
     * which is where the tray runs. Cable goes up into the tray, along it,
     * through the comms cupboard into the riser, and vertically in the riser
     * ONLY. It does not go down the stairs, which is the whole reason the two
     * distances differ. */
    {
        size_t ncells = (size_t)b->floors * (size_t)b->w * (size_t)b->h;
        int *cellnode = nom_alloc(ncells * sizeof(int));
        for (size_t i = 0; i < ncells; i++) cellnode[i] = -1;
        int nn = b->nrooms;
        for (int f = 0; f < b->floors; f++)
            for (int y = 0; y < b->h; y++)
                for (int x = 0; x < b->w; x++)
                    if (cell_kind(b, f, x, y) == RM_CORRIDOR)
                        cellnode[CIDX(b, f, x, y)] = nn++;

        int cap = 4096, ne = 0;
        int *ea = nom_alloc((size_t)cap * sizeof(int));
        int *eb = nom_alloc((size_t)cap * sizeof(int));
        double *ew = nom_alloc((size_t)cap * sizeof(double));
        #define PUSH_EDGE(A,B,W) do {                                    \
            if (ne == cap) { cap *= 2;                                   \
                ea = nom_realloc(ea, (size_t)cap * sizeof(int));         \
                eb = nom_realloc(eb, (size_t)cap * sizeof(int));         \
                ew = nom_realloc(ew, (size_t)cap * sizeof(double)); }    \
            ea[ne] = (A); eb[ne] = (B); ew[ne] = (W); ne++; } while (0)

        /* tray along the corridor */
        for (int f = 0; f < b->floors; f++)
            for (int y = 0; y < b->h; y++)
                for (int x = 0; x < b->w; x++) {
                    int u = cellnode[CIDX(b, f, x, y)];
                    if (u < 0) continue;
                    if (cell_kind(b, f, x + 1, y) == RM_CORRIDOR)
                        PUSH_EDGE(u, cellnode[CIDX(b, f, x + 1, y)], 1.0);
                    if (cell_kind(b, f, x, y + 1) == RM_CORRIDOR)
                        PUSH_EDGE(u, cellnode[CIDX(b, f, x, y + 1)], 1.0);
                }
        /* drops through every door */
        for (int i = 0; i < b->ndoors; i++) {
            Door *d = &b->doors[i];
            int ax = d->x, ay = d->y, bx2 = d->x + (d->dir == 0), by2 = d->y + (d->dir == 1);
            for (int side = 0; side < 2; side++) {
                int room = side ? d->b : d->a;
                int ox = side ? ax : bx2, oy = side ? ay : by2;
                int other = side ? d->a : d->b;
                if (b->rooms[room].kind == RM_CORRIDOR) continue;
                Room *rm = &b->rooms[room];
                int rcx = (rm->x0 + rm->x1) / 2, rcy = (rm->y0 + rm->y1) / 2;
                int dx = rcx - (side ? bx2 : ax), dy = rcy - (side ? by2 : ay);
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (b->rooms[other].kind == RM_CORRIDOR) {
                    int u = cellnode[CIDX(b, d->floor, ox, oy)];
                    if (u >= 0) PUSH_EDGE(room, u, (double)(dx + dy) + TRAY_DROP);
                } else if (room < other) {
                    Room *rn = &b->rooms[other];
                    int ncx = (rn->x0 + rn->x1) / 2, ncy = (rn->y0 + rn->y1) / 2;
                    int ddx = rcx - ncx, ddy = rcy - ncy;
                    if (ddx < 0) ddx = -ddx;
                    if (ddy < 0) ddy = -ddy;
                    PUSH_EDGE(room, other, (double)(ddx + ddy));
                }
            }
        }
        /* the riser: the only way up */
        for (int f = 0; f + 1 < b->floors; f++) {
            int a = bld_find(b, f, RM_RISER), c = bld_find(b, f + 1, RM_RISER);
            if (a >= 0 && c >= 0) PUSH_EDGE(a, c, b->floor_height);
        }
        #undef PUSH_EDGE

        b->cg_n = nn; b->cg_nedge = ne * 2;
        b->cg_head = nom_alloc((size_t)(nn + 1) * sizeof(int));
        b->cg_to   = nom_alloc((size_t)(ne * 2 + 1) * sizeof(int));
        b->cg_w    = nom_alloc((size_t)(ne * 2 + 1) * sizeof(double));
        int *cnt = nom_alloc((size_t)(nn + 1) * sizeof(int));
        memset(cnt, 0, (size_t)(nn + 1) * sizeof(int));
        for (int i = 0; i < ne; i++) { cnt[ea[i]]++; cnt[eb[i]]++; }
        b->cg_head[0] = 0;
        for (int i = 0; i < nn; i++) b->cg_head[i + 1] = b->cg_head[i] + cnt[i];
        memcpy(cnt, b->cg_head, (size_t)nn * sizeof(int));
        for (int i = 0; i < ne; i++) {
            b->cg_to[cnt[ea[i]]] = eb[i]; b->cg_w[cnt[ea[i]]++] = ew[i];
            b->cg_to[cnt[eb[i]]] = ea[i]; b->cg_w[cnt[eb[i]]++] = ew[i];
        }
        nom_free(cnt); nom_free(ea); nom_free(eb); nom_free(ew); nom_free(cellnode);
    }
    return true;
}

void bld_free(Building *b)
{
    nom_free(b->cell); nom_free(b->edge);
    nom_free(b->cg_head); nom_free(b->cg_to); nom_free(b->cg_w);
    b->cell = NULL; b->edge = NULL;
    b->cg_head = NULL; b->cg_to = NULL; b->cg_w = NULL;
}

/* ------------------------------------------------------------ the metrics */

typedef struct { int n, cap; int *node; double *key; } PQ;

static void pq_push(PQ *q, int node, double key)
{
    if (q->n == q->cap) {
        q->cap *= 2;
        q->node = nom_realloc(q->node, (size_t)q->cap * sizeof(int));
        q->key  = nom_realloc(q->key,  (size_t)q->cap * sizeof(double));
    }
    int i = q->n++;
    q->node[i] = node; q->key[i] = key;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (q->key[p] <= q->key[i]) break;
        int tn = q->node[p]; q->node[p] = q->node[i]; q->node[i] = tn;
        double tk = q->key[p]; q->key[p] = q->key[i]; q->key[i] = tk;
        i = p;
    }
}

static int pq_pop(PQ *q, double *key)
{
    if (!q->n) return -1;
    int out = q->node[0]; *key = q->key[0];
    q->n--;
    q->node[0] = q->node[q->n]; q->key[0] = q->key[q->n];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, rr = l + 1, m = i;
        if (l < q->n && q->key[l] < q->key[m]) m = l;
        if (rr < q->n && q->key[rr] < q->key[m]) m = rr;
        if (m == i) break;
        int tn = q->node[m]; q->node[m] = q->node[i]; q->node[i] = tn;
        double tk = q->key[m]; q->key[m] = q->key[i]; q->key[i] = tk;
        i = m;
    }
    return out;
}

/* Can a person step from one cell to the next? Inside a room, yes. Between
 * two corridor legs, yes -- circulation is open. Otherwise only through a
 * recorded door. A riser is a shaft: nobody walks into one. */
static bool step_ok(const Building *b, int f, int x, int y, int nx, int ny)
{
    uint16_t a = cell_at(b, f, x, y), c = cell_at(b, f, nx, ny);
    if (a == BLD_NOROOM || c == BLD_NOROOM) return false;
    if (a == c) return true;
    if (b->rooms[a].kind == RM_CORRIDOR && b->rooms[c].kind == RM_CORRIDOR) return true;
    if (nx == x + 1 && ny == y) return (b->edge[CIDX(b, f, x, y)] & 1) != 0;
    if (nx == x - 1 && ny == y) return (b->edge[CIDX(b, f, nx, ny)] & 1) != 0;
    if (ny == y + 1 && nx == x) return (b->edge[CIDX(b, f, x, y)] & 2) != 0;
    if (ny == y - 1 && nx == x) return (b->edge[CIDX(b, f, x, ny)] & 2) != 0;
    return false;
}

static bool same_space(const Building *b, int f, int x, int y, int nx, int ny)
{
    uint16_t a = cell_at(b, f, x, y), c = cell_at(b, f, nx, ny);
    if (a == BLD_NOROOM || c == BLD_NOROOM) return false;
    if (b->rooms[a].kind == RM_RISER) return false;
    return a == c || (b->rooms[a].kind == RM_CORRIDOR && b->rooms[c].kind == RM_CORRIDOR);
}

/* Walking distance, in metres, over the cell grid. Diagonals are allowed
 * inside one space and never through a doorway, which keeps a corridor run
 * honest without letting anyone cut a corner through a wall. */
static bool walk_cells(const Building *b, int sf, int sx, int sy, double *dist)
{
    size_t n = (size_t)b->floors * (size_t)b->w * (size_t)b->h;
    for (size_t i = 0; i < n; i++) dist[i] = BLD_INF;
    if (cell_at(b, sf, sx, sy) == BLD_NOROOM) return false;
    PQ q = { 0, 1024, nom_alloc(1024 * sizeof(int)), nom_alloc(1024 * sizeof(double)) };
    dist[CIDX(b, sf, sx, sy)] = 0.0;
    pq_push(&q, (int)CIDX(b, sf, sx, sy), 0.0);
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    while (q.n) {
        double d; int u = pq_pop(&q, &d);
        if (d > dist[u] + 1e-12) continue;
        int f = (int)((size_t)u / ((size_t)b->w * (size_t)b->h));
        int rem = (int)((size_t)u % ((size_t)b->w * (size_t)b->h));
        int y = rem / b->w, x = rem % b->w;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!step_ok(b, f, x, y, nx, ny)) continue;
            size_t v = CIDX(b, f, nx, ny);
            if (d + 1.0 < dist[v] - 1e-12) { dist[v] = d + 1.0; pq_push(&q, (int)v, dist[v]); }
        }
        for (int sxd = -1; sxd <= 1; sxd += 2)
            for (int syd = -1; syd <= 1; syd += 2) {
                int nx = x + sxd, ny = y + syd;
                if (!same_space(b, f, x, y, nx, ny)) continue;
                if (!same_space(b, f, x, y, nx, y) || !same_space(b, f, x, y, x, ny)) continue;
                size_t v = CIDX(b, f, nx, ny);
                if (d + DIAG < dist[v] - 1e-12) { dist[v] = d + DIAG; pq_push(&q, (int)v, dist[v]); }
            }
        int kind = cell_kind(b, f, x, y);
        if (kind == RM_STAIR || kind == RM_LIFT || kind == RM_RISER) {
            double cost = kind == RM_STAIR ? b->floor_height * STAIR_RUN
                        : kind == RM_LIFT  ? b->floor_height + LIFT_BOARD
                                           : b->floor_height * LADDER_CLIMB;
            for (int df = -1; df <= 1; df += 2) {
                int nf = f + df;
                if (nf < 0 || nf >= b->floors) continue;
                if (cell_kind(b, nf, x, y) != kind) continue;
                size_t v = CIDX(b, nf, x, y);
                if (d + cost < dist[v] - 1e-12) { dist[v] = d + cost; pq_push(&q, (int)v, dist[v]); }
            }
        }
    }
    nom_free(q.node); nom_free(q.key);
    return true;
}

static void room_centre(const Building *b, int ri, int *x, int *y)
{
    const Room *r = &b->rooms[ri];
    *x = (r->x0 + r->x1) / 2; *y = (r->y0 + r->y1) / 2;
}

bool bld_walk_all(const Building *b, int src, double *out)
{
    if (src < 0 || src >= b->nrooms) return false;
    size_t n = (size_t)b->floors * (size_t)b->w * (size_t)b->h;
    double *d = nom_alloc(n * sizeof(double));
    int sx, sy; room_centre(b, src, &sx, &sy);
    bool ok = walk_cells(b, b->rooms[src].floor, sx, sy, d);
    for (int i = 0; i < b->nrooms; i++) {
        int x, y; room_centre(b, i, &x, &y);
        out[i] = ok ? d[CIDX(b, b->rooms[i].floor, x, y)] : BLD_INF;
    }
    nom_free(d);
    return ok;
}

bool bld_cable_all(const Building *b, int src, double *out)
{
    if (src < 0 || src >= b->nrooms || !b->cg_head) return false;
    int n = b->cg_n;
    double *d = nom_alloc((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) d[i] = BLD_INF;
    PQ q = { 0, 1024, nom_alloc(1024 * sizeof(int)), nom_alloc(1024 * sizeof(double)) };
    d[src] = 0.0; pq_push(&q, src, 0.0);
    while (q.n) {
        double dd; int u = pq_pop(&q, &dd);
        if (dd > d[u] + 1e-12) continue;
        for (int e = b->cg_head[u]; e < b->cg_head[u + 1]; e++) {
            int v = b->cg_to[e];
            double nd = dd + b->cg_w[e];
            if (nd < d[v] - 1e-12) { d[v] = nd; pq_push(&q, v, nd); }
        }
    }
    for (int i = 0; i < b->nrooms; i++) out[i] = d[i];
    nom_free(d); nom_free(q.node); nom_free(q.key);
    return true;
}

/* ------------------------------------------------------------- the checks */

static void fail(Buf *out, int *fails, int c, const char *fmt, ...)
{
    if (fails) fails[c]++;
    if (!out) return;
    va_list ap; va_start(ap, fmt);
    char line[512];
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    buf_printf(out, "  %-42s %s\n", bld_check_name(c), line);
}

/* Flood the walkable cells of one floor from a starting cell, honouring a
 * tenant filter: only common parts and rooms belonging to `tenant` may be
 * crossed. Returns a bitmap of visited cells. */
static void flood(const Building *b, int f, int sx, int sy, uint8_t *seen,
                  int tenant_ok, int room_ok, bool vertical)
{
    size_t n = (size_t)b->floors * (size_t)b->w * (size_t)b->h;
    memset(seen, 0, n);
    int *stack = nom_alloc(n * sizeof(int));
    int sp = 0;
    seen[CIDX(b, f, sx, sy)] = 1;
    stack[sp++] = (int)CIDX(b, f, sx, sy);
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    while (sp) {
        int u = stack[--sp];
        int uf = (int)((size_t)u / ((size_t)b->w * (size_t)b->h));
        int rem = (int)((size_t)u % ((size_t)b->w * (size_t)b->h));
        int y = rem / b->w, x = rem % b->w;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!step_ok(b, uf, x, y, nx, ny)) continue;
            uint16_t rid = cell_at(b, uf, nx, ny);
            if (tenant_ok >= 0 && b->rooms[rid].tenant != 0 && (int)rid != room_ok) continue;
            size_t v = CIDX(b, uf, nx, ny);
            if (!seen[v]) { seen[v] = 1; stack[sp++] = (int)v; }
        }
        if (!vertical) continue;
        int kind = cell_kind(b, uf, x, y);
        if (kind == RM_STAIR || kind == RM_LIFT)
            for (int df = -1; df <= 1; df += 2) {
                int nf = uf + df;
                if (nf < 0 || nf >= b->floors) continue;
                if (cell_kind(b, nf, x, y) != kind) continue;
                size_t v = CIDX(b, nf, x, y);
                if (!seen[v]) { seen[v] = 1; stack[sp++] = (int)v; }
            }
    }
    nom_free(stack);
}

int bld_check(const Building *b, Buf *out, int *fails)
{
    int bad = 0;
    int local[BC_COUNT] = {0};
    if (!fails) fails = local;
    int before[BC_COUNT];
    memcpy(before, fails, sizeof before);

    size_t ncells = (size_t)b->floors * (size_t)b->w * (size_t)b->h;

    /* 1. floors stack, and nothing floats. */
    for (int f = 0; f + 1 < b->floors; f++)
        if (b->fx0[f + 1] < b->fx0[f] || b->fy0[f + 1] < b->fy0[f] ||
            b->fx1[f + 1] > b->fx1[f] || b->fy1[f + 1] > b->fy1[f])
            fail(out, fails, BC_STACK, "floor %d is not inside floor %d", f + 1, f);

    /* 2. tessellation: every metre of every plate is in exactly one room, and
     *    no room hangs over the edge of its plate. */
    {
        uint8_t *cover = nom_alloc(ncells);
        memset(cover, 0, ncells);
        for (int i = 0; i < b->nrooms; i++) {
            const Room *r = &b->rooms[i];
            int f = r->floor;
            if (r->x0 < b->fx0[f] || r->y0 < b->fy0[f] ||
                r->x1 > b->fx1[f] || r->y1 > b->fy1[f]) {
                fail(out, fails, BC_TESSELLATE,
                     "room %d (%s) hangs over the edge of floor %d",
                     i, bld_kind_name(r->kind), f);
                continue;
            }
            for (int y = r->y0; y < r->y1; y++)
                for (int x = r->x0; x < r->x1; x++) {
                    size_t ci = CIDX(b, f, x, y);
                    if (cover[ci]) fail(out, fails, BC_TESSELLATE,
                                        "floor %d (%d,%d) is in two rooms", f, x, y);
                    cover[ci] = 1;
                }
        }
        for (int f = 0; f < b->floors; f++)
            for (int y = b->fy0[f]; y < b->fy1[f]; y++)
                for (int x = b->fx0[f]; x < b->fx1[f]; x++)
                    if (!cover[CIDX(b, f, x, y)])
                        fail(out, fails, BC_TESSELLATE,
                             "floor %d (%d,%d) is in no room at all", f, x, y);
        nom_free(cover);
    }

    /* 3. no slivers, and a room somebody works in is big enough to work in. */
    for (int i = 0; i < b->nrooms; i++) {
        const Room *r = &b->rooms[i];
        if (r->x1 - r->x0 < 2 || r->y1 - r->y0 < 2)
            fail(out, fails, BC_ROOMSIZE, "room %d (%s) is %dx%d m",
                 i, bld_kind_name(r->kind), r->x1 - r->x0, r->y1 - r->y0);
        bool occupied = r->kind == RM_OFFICE || r->kind == RM_RESIDENCE ||
                        r->kind == RM_RETAIL || r->kind == RM_SERVER ||
                        r->kind == RM_MDF;
        if (occupied && bld_room_area(r) < 20.0)
            fail(out, fails, BC_ROOMSIZE, "room %d (%s) is only %.0f m2",
                 i, bld_kind_name(r->kind), bld_room_area(r));
    }

    /* 4. the services line up. A riser that jinks sideways between floors is
     *    the one thing that would make this look fake AND break the cabling. */
    for (int kind = RM_LIFT; kind <= RM_RISER; kind++) {
        for (int f = 0; f + 1 < b->floors; f++) {
            int na = 0, nb2 = 0;
            const Room *A[4], *B[4];
            for (int i = 0; i < b->nrooms; i++) {
                if (b->rooms[i].kind != kind) continue;
                if (b->rooms[i].floor == f && na < 4) A[na++] = &b->rooms[i];
                if (b->rooms[i].floor == f + 1 && nb2 < 4) B[nb2++] = &b->rooms[i];
            }
            if (na != nb2) {
                fail(out, fails, BC_ALIGN, "%s: %d on floor %d, %d on floor %d",
                     bld_kind_name(kind), na, f, nb2, f + 1);
                continue;
            }
            for (int i = 0; i < na; i++)
                if (A[i]->x0 != B[i]->x0 || A[i]->y0 != B[i]->y0 ||
                    A[i]->x1 != B[i]->x1 || A[i]->y1 != B[i]->y1)
                    fail(out, fails, BC_ALIGN,
                         "%s moves from (%d,%d) on floor %d to (%d,%d) on floor %d",
                         bld_kind_name(kind), A[i]->x0, A[i]->y0, f,
                         B[i]->x0, B[i]->y0, f + 1);
        }
        if (kind == RM_RISER)
            for (int f = 0; f < b->floors; f++)
                if (bld_find(b, f, RM_RISER) < 0)
                    fail(out, fails, BC_ALIGN, "floor %d has no riser", f);
    }

    /* 5. every door has a room on both sides, they are different rooms, and
     *    the door sits on the boundary of both of their rectangles. */
    for (int i = 0; i < b->ndoors; i++) {
        const Door *d = &b->doors[i];
        int nx = d->x + (d->dir == 0), ny = d->y + (d->dir == 1);
        uint16_t a = cell_at(b, d->floor, d->x, d->y), c = cell_at(b, d->floor, nx, ny);
        if (a == BLD_NOROOM || c == BLD_NOROOM) {
            fail(out, fails, BC_DOORS, "door %d on floor %d opens into open air", i, d->floor);
            continue;
        }
        if (a == c) { fail(out, fails, BC_DOORS, "door %d joins a room to itself", i); continue; }
        if (a != d->a || c != d->b)
            fail(out, fails, BC_DOORS, "door %d records rooms %u,%u but stands between %u,%u",
                 i, d->a, d->b, a, c);
        const Room *ra = &b->rooms[a], *rc = &b->rooms[c];
        bool onwall = (d->dir == 0) ? (ra->x1 == rc->x0) : (ra->y1 == rc->y0);
        if (!onwall)
            fail(out, fails, BC_DOORS, "door %d is not on the wall between its rooms", i);
        for (int j = 0; j < i; j++)
            if (b->doors[j].floor == d->floor && b->doors[j].x == d->x &&
                b->doors[j].y == d->y && b->doors[j].dir == d->dir)
                fail(out, fails, BC_DOORS, "door %d is a duplicate of door %d", i, j);
    }

    /* 6. every room has a way in. Corridors are their own way in. */
    for (int i = 0; i < b->nrooms; i++) {
        if (b->rooms[i].kind == RM_CORRIDOR) continue;
        bool has = false;
        for (int j = 0; j < b->ndoors && !has; j++)
            if (b->doors[j].a == i || b->doors[j].b == i) has = true;
        if (!has) fail(out, fails, BC_ROOMDOOR, "room %d (%s) on floor %d has no door",
                       i, bld_kind_name(b->rooms[i].kind), b->rooms[i].floor);
    }

    /* 7. the corridor is one loop. Every corridor cell has at least two
     *    corridor neighbours -- which is what "no pointless dead end" means
     *    when you write it down -- and they are all one connected piece. */
    for (int f = 0; f < b->floors; f++) {
        int total = 0, first = -1;
        for (int y = 0; y < b->h; y++)
            for (int x = 0; x < b->w; x++) {
                if (cell_kind(b, f, x, y) != RM_CORRIDOR) continue;
                total++;
                if (first < 0) first = (int)CIDX(b, f, x, y);
                int nbr = 0;
                if (cell_kind(b, f, x + 1, y) == RM_CORRIDOR) nbr++;
                if (cell_kind(b, f, x - 1, y) == RM_CORRIDOR) nbr++;
                if (cell_kind(b, f, x, y + 1) == RM_CORRIDOR) nbr++;
                if (cell_kind(b, f, x, y - 1) == RM_CORRIDOR) nbr++;
                if (nbr < 2)
                    fail(out, fails, BC_CORRIDOR, "floor %d (%d,%d) is a corridor dead end",
                         f, x, y);
            }
        if (first < 0) { fail(out, fails, BC_CORRIDOR, "floor %d has no corridor", f); continue; }
        uint8_t *seen = nom_alloc(ncells);
        int fy = (first % (b->w * b->h)) / b->w, fx = (first % (b->w * b->h)) % b->w;
        flood(b, f, fx, fy, seen, -1, -1, false);
        int reached = 0;
        for (int y = 0; y < b->h; y++)
            for (int x = 0; x < b->w; x++)
                if (cell_kind(b, f, x, y) == RM_CORRIDOR && seen[CIDX(b, f, x, y)]) reached++;
        if (reached != total)
            fail(out, fails, BC_CORRIDOR, "floor %d: corridor is in more than one piece (%d/%d)",
                 f, reached, total);
        nom_free(seen);
    }

    /* 8. every room in the building is reachable on foot from the MDF, which
     *    is where a player starts the day. A riser is a shaft and is
     *    deliberately not walkable; it is reached by cable only. */
    {
        int mdf = bld_find(b, 0, RM_MDF);
        if (mdf < 0) fail(out, fails, BC_PROGRAM, "there is no MDF");
        else {
            uint8_t *seen = nom_alloc(ncells);
            int sx, sy; room_centre(b, mdf, &sx, &sy);
            flood(b, 0, sx, sy, seen, -1, -1, true);
            for (int i = 0; i < b->nrooms; i++) {
                if (b->rooms[i].kind == RM_RISER) continue;
                int x, y; room_centre(b, i, &x, &y);
                if (!seen[CIDX(b, b->rooms[i].floor, x, y)])
                    fail(out, fails, BC_REACH, "room %d (%s) on floor %d cannot be walked to",
                         i, bld_kind_name(b->rooms[i].kind), b->rooms[i].floor);
            }
            nom_free(seen);
        }
    }

    /* 9. nobody crosses anybody else's space. Structurally: a private room's
     *    doors all open onto common parts. And, on floor by floor, the flood
     *    from the lift lobby with every OTHER tenant's space closed off still
     *    gets there. */
    for (int i = 0; i < b->ndoors; i++) {
        const Door *d = &b->doors[i];
        int ta = b->rooms[d->a].tenant, tb = b->rooms[d->b].tenant;
        if (ta && tb && ta != tb)
            fail(out, fails, BC_PRIVACY, "door %d joins tenant %d directly to tenant %d",
                 i, ta, tb);
    }
    {
        uint8_t *seen = nom_alloc(ncells);
        for (int i = 0; i < b->nrooms; i++) {
            if (!b->rooms[i].tenant) continue;
            int f = b->rooms[i].floor;
            int lob = bld_find(b, f, RM_LIFTLOBBY);
            if (lob < 0) { fail(out, fails, BC_PROGRAM, "floor %d has no lift lobby", f); break; }
            int sx, sy; room_centre(b, lob, &sx, &sy);
            flood(b, f, sx, sy, seen, 1, i, false);
            int x, y; room_centre(b, i, &x, &y);
            if (!seen[CIDX(b, f, x, y)])
                fail(out, fails, BC_PRIVACY,
                     "room %d (tenant %d) can only be reached through somebody else's space",
                     i, b->rooms[i].tenant);
        }
        nom_free(seen);
    }

    /* 10. the mix. Every floor gets a comms cupboard, a stair, a lift lobby
     *     and toilets; the ground floor gets the MDF and goods in. */
    for (int f = 0; f < b->floors; f++) {
        static const int NEED[] = { RM_COMMS, RM_STAIR, RM_LIFTLOBBY, RM_TOILET, RM_CORRIDOR };
        for (size_t k = 0; k < sizeof NEED / sizeof *NEED; k++)
            if (bld_find(b, f, NEED[k]) < 0)
                fail(out, fails, BC_PROGRAM, "floor %d has no %s", f, bld_kind_name(NEED[k]));
        int nsame = 0, prev = -1, worst = 0;
        for (int i = 0; i < b->nrooms; i++) {
            if (b->rooms[i].floor != f) continue;
            int k = b->rooms[i].kind;
            if (k == RM_TOILET) { if (prev == k) nsame++; else nsame = 1; prev = k; }
            else { prev = k; nsame = 0; }
            if (nsame > worst) worst = nsame;
        }
        if (worst > 2) fail(out, fails, BC_PROGRAM, "floor %d is %d toilets in a row", f, worst);
    }
    if (bld_find(b, 0, RM_GOODS) < 0) fail(out, fails, BC_PROGRAM, "there is nowhere to take a delivery");

    /* 11 and 12. The two distances: a metric each, and not the same number.
     *     Sampled rather than exhaustive, because the point is to catch a
     *     broken graph, and a broken graph is broken for every pair. */
    {
        int samp[8], ns = 0, ncross = 0;
        for (int i = 0; i < b->nrooms && ns < 8; i++) {
            const Room *r = &b->rooms[i];
            if (r->kind == RM_RISER || r->kind == RM_CORRIDOR) continue;
            if (ns && i < samp[ns - 1] + b->nrooms / 9) continue;
            samp[ns++] = i;
        }
        double *w[8], *c[8];
        for (int i = 0; i < ns; i++) {
            w[i] = nom_alloc((size_t)b->nrooms * sizeof(double));
            c[i] = nom_alloc((size_t)b->nrooms * sizeof(double));
            bld_walk_all(b, samp[i], w[i]);
            bld_cable_all(b, samp[i], c[i]);
        }
        for (int i = 0; i < ns; i++) {
            if (w[i][samp[i]] != 0.0 || c[i][samp[i]] != 0.0)
                fail(out, fails, BC_METRIC, "a room is not zero metres from itself");
            for (int j = 0; j < ns; j++) {
                double dw = w[i][samp[j]], dc = c[i][samp[j]];
                if (dw >= BLD_INF || dc >= BLD_INF) {
                    fail(out, fails, BC_METRIC, "no route between rooms %d and %d",
                         samp[i], samp[j]);
                    continue;
                }
                if (i != j && (dw <= 0.0 || dc <= 0.0))
                    fail(out, fails, BC_METRIC, "rooms %d and %d are zero metres apart",
                         samp[i], samp[j]);
                if (nom_fabs(dw - w[j][samp[i]]) > 1e-6)
                    fail(out, fails, BC_METRIC, "walking %d->%d is %.2f but %d->%d is %.2f",
                         samp[i], samp[j], dw, samp[j], samp[i], w[j][samp[i]]);
                if (nom_fabs(dc - c[j][samp[i]]) > 1e-6)
                    fail(out, fails, BC_METRIC, "cabling %d->%d is %.2f but %d->%d is %.2f",
                         samp[i], samp[j], dc, samp[j], samp[i], c[j][samp[i]]);
                for (int k = 0; k < ns; k++) {
                    if (w[i][samp[k]] + w[k][samp[j]] < dw - 1e-6)
                        fail(out, fails, BC_METRIC,
                             "walking %d->%d is longer than going via %d", samp[i], samp[j], samp[k]);
                    if (c[i][samp[k]] + c[k][samp[j]] < dc - 1e-6)
                        fail(out, fails, BC_METRIC,
                             "cabling %d->%d is longer than going via %d", samp[i], samp[j], samp[k]);
                }
                if (b->rooms[samp[i]].floor != b->rooms[samp[j]].floor) {
                    ncross++;
                    if (nom_fabs(dw - dc) < 1e-9)
                        fail(out, fails, BC_DIFFER,
                             "rooms %d and %d are %.2f m apart both on foot and by cable",
                             samp[i], samp[j], dw);
                }
            }
        }
        /* A CHECK THAT LOOKED AT NOTHING HAS NOT PASSED. If the sample never
         * happened to straddle two floors then "walking and cabling differ"
         * was never tested, and a gate that reports a pass on an empty
         * comparison is the failure mode this project keeps rediscovering. */
        if (!ncross) fail(out, fails, BC_DIFFER,
                          "the sample never straddled two floors, so nothing was compared");

        /* The cable has to be able to get from the MDF to every comms
         * cupboard in the building, or the tower has no uplink. */
        int mdf = bld_find(b, 0, RM_MDF);
        if (mdf >= 0) {
            double *cm = nom_alloc((size_t)b->nrooms * sizeof(double));
            bld_cable_all(b, mdf, cm);
            for (int f = 0; f < b->floors; f++) {
                int cc = bld_find(b, f, RM_COMMS);
                if (cc >= 0 && cm[cc] >= BLD_INF)
                    fail(out, fails, BC_METRIC, "no cable route from the MDF to floor %d", f);
            }
            nom_free(cm);
        }
        for (int i = 0; i < ns; i++) { nom_free(w[i]); nom_free(c[i]); }
    }

    for (int i = 0; i < BC_COUNT; i++) if (fails[i] != before[i]) bad++;
    return bad;
}

/* ------------------------------------------------------------- the picture */

void bld_floorplan(const Building *b, int floor, Buf *out)
{
    if (floor < 0 || floor >= b->floors) { buf_printf(out, "no such floor\n"); return; }
    int W = 2 * b->w + 1, H = 2 * b->h + 1;
    char *g = nom_alloc((size_t)W * (size_t)H);
    memset(g, ' ', (size_t)W * (size_t)H);
    #define AT(gx,gy) g[(size_t)(gy) * (size_t)W + (size_t)(gx)]

    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++) {
            int k = cell_kind(b, floor, x, y);
            AT(2 * x + 1, 2 * y + 1) = k < 0 ? ' ' : bld_kind_char(k);
        }
    /* Walls where two different spaces meet, a gap where there is a door, and
     * nothing at all between two legs of the corridor -- that junction is
     * open, and drawing a wall there would be a lie about the plan. */
    for (int y = 0; y <= b->h; y++)
        for (int x = 0; x <= b->w; x++) {
            if (x < b->w) {
                uint16_t a = cell_at(b, floor, x, y - 1), c = cell_at(b, floor, x, y);
                bool wall = a != c;
                if (a != BLD_NOROOM && c != BLD_NOROOM &&
                    b->rooms[a].kind == RM_CORRIDOR && b->rooms[c].kind == RM_CORRIDOR) wall = false;
                bool door = y > 0 && a != BLD_NOROOM && c != BLD_NOROOM &&
                            (b->edge[CIDX(b, floor, x, y - 1)] & 2);
                if (a == BLD_NOROOM && c == BLD_NOROOM) wall = false;
                AT(2 * x + 1, 2 * y) = door ? '.' : wall ? '-' : ' ';
            }
            if (y < b->h) {
                uint16_t a = cell_at(b, floor, x - 1, y), c = cell_at(b, floor, x, y);
                bool wall = a != c;
                if (a != BLD_NOROOM && c != BLD_NOROOM &&
                    b->rooms[a].kind == RM_CORRIDOR && b->rooms[c].kind == RM_CORRIDOR) wall = false;
                bool door = a != BLD_NOROOM && c != BLD_NOROOM && x > 0 &&
                            (b->edge[CIDX(b, floor, x - 1, y)] & 1);
                if (a == BLD_NOROOM && c == BLD_NOROOM) wall = false;
                AT(2 * x, 2 * y + 1) = door ? '.' : wall ? '|' : ' ';
            }
        }
    for (int gy = 0; gy < H; gy += 2)
        for (int gx = 0; gx < W; gx += 2) {
            bool any = false;
            if (gx > 0     && AT(gx - 1, gy) != ' ') any = true;
            if (gx < W - 1 && AT(gx + 1, gy) != ' ') any = true;
            if (gy > 0     && AT(gx, gy - 1) != ' ') any = true;
            if (gy < H - 1 && AT(gx, gy + 1) != ' ') any = true;
            AT(gx, gy) = any ? '+' : ' ';
        }

    buf_printf(out, "seed %llu  floor %d of %d  (%s)  %.1f m above the ground\n",
               (unsigned long long)b->seed, floor, b->floors - 1,
               bld_floor_kind_name(b->fkind[floor]),
               b->floor_height * floor);
    buf_printf(out, "plate %d x %d m, %d m2, one character per metre\n\n",
               b->fx1[floor] - b->fx0[floor], b->fy1[floor] - b->fy0[floor],
               (b->fx1[floor] - b->fx0[floor]) * (b->fy1[floor] - b->fy0[floor]));

    for (int gy = 0; gy < H; gy++) {
        int end = W;
        while (end > 0 && AT(end - 1, gy) == ' ') end--;
        buf_put(out, &AT(0, gy), (size_t)end);
        buf_putc(out, '\n');
    }

    buf_printf(out, "\nlegend   '.' a door   ");
    int seen[RM_KIND_COUNT] = {0}, col = 0;
    for (int i = 0; i < b->nrooms; i++) {
        int k = b->rooms[i].kind;
        if (b->rooms[i].floor != floor || seen[k]) continue;
        seen[k] = 1;
        if (col++ % 4 == 0) buf_printf(out, "\n  ");
        buf_printf(out, "'%c' %-16s", bld_kind_char(k), bld_kind_name(k));
    }
    buf_printf(out, "\n");
    nom_free(g);
    #undef AT
}
