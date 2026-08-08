/* THE SHIP GENERATOR.
 *
 * David: "let's actually drop space station entirely and make this a
 * spaceship... I don't want to rip off the Star Trek ship shape, but along
 * those lines would be excellent."
 *
 * THE SHAPE, AND WHY IT IS THIS ONE. Four sessions of trying to make a station
 * read as anything other than an office block failed for one reason: the
 * generator drew a plate per deck out of rectangles, so the silhouette was a
 * shoebox before a single room was placed. This starts at the other end. The
 * hull is described first, in frames, and everything inside it is fitted
 * afterwards -- which is how a real hull is drawn and the only way the outside
 * can be the thing that decides the inside.
 *
 * The family, without being the Enterprise:
 *
 *   - a COMMAND section at the bow that is a wide low wedge rather than a
 *     saucer -- flat-bottomed, deep at the front, tapering aft;
 *   - a NECK, narrow in beam and tall, which is where the riser goes and where
 *     everything has to route through;
 *   - an ENGINEERING hull, the deep body carrying the core, the power and life
 *     support;
 *   - and a DRIVE RING at the stern that the keel passes through, on two
 *     pylons.
 *
 * The ring is where David's donut finally belongs. He asked three times for a
 * doughnut-shaped station and each attempt read as a corridor bent in a circle,
 * because a torus you WALK is just a bent corridor. A torus you never enter, at
 * the back, is the silhouette.
 */

#include "ship.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

const char *ship_hull_name(int kind)
{
    switch (kind) {
    case HULL_COMMAND:     return "command";
    case HULL_NECK:        return "neck";
    case HULL_ENGINEERING: return "engineering";
    default:               return "?";
    }
}

/* ---------------------------------------------------------------- frames */

static void frame_add(Ship *s, int x, int hw, int hh, int cy)
{
    if (s->nframe >= SHIP_MAX_FRAME) return;
    Frame *f = &s->frame[s->nframe++];
    f->x = (int16_t)x; f->half_w = (int16_t)hw;
    f->half_h = (int16_t)hh; f->cy = (int16_t)cy;
}

static void hull_add(Ship *s, int kind, int frame0, int nframe)
{
    if (s->nhull >= SHIP_MAX_HULL) return;
    Hull *h = &s->hull[s->nhull++];
    h->kind = (uint8_t)kind;
    h->frame0 = (int16_t)frame0;
    h->nframe = (int16_t)nframe;
}

/* A LOFT BETWEEN TWO STATIONS, in whole metres, so a frame lands on every
 * metre of the keel and the deck fitter never has to interpolate a wall
 * position. `ease` bends the run: 0 is a straight taper, 1 is a curve that
 * leaves both ends tangent to the axis, which is what stops the command
 * section looking like a doorstop. */
static double ease_in_out(double t, double amount)
{
    double smooth = t * t * (3.0 - 2.0 * t);
    return t + (smooth - t) * amount;
}

static void loft(Ship *s, int x0, int hw0, int hh0, int cy0,
                 int x1, int hw1, int hh1, int cy1, double ease, bool skip_first)
{
    if (x1 <= x0) return;
    for (int x = x0 + (skip_first ? 1 : 0); x <= x1; x++) {
        double t = (double)(x - x0) / (double)(x1 - x0);
        double e = ease_in_out(t, ease);
        frame_add(s, x,
                  (int)lround(hw0 + (hw1 - hw0) * e),
                  (int)lround(hh0 + (hh1 - hh0) * e),
                  (int)lround(cy0 + (cy1 - cy0) * e));
    }
}

/* ------------------------------------------------------------- the build */

bool ship_generate(Ship *s, uint64_t seed)
{
    if (!s) return false;
    memset(s, 0, sizeof *s);
    s->seed = seed;

    Rng r;
    rng_seed(&r, seed ^ 0x51119a0000ull);

    /* THE PROPORTIONS, and they are a ship's proportions rather than numbers
     * that looked nice. Length overall is the one thing that varies much: a
     * hull is a hull, and a family of ships that differ in every dimension at
     * once reads as a family of unrelated objects. */
    int loa   = 168 + (int)rng_range(&r, 0, 44);      /* 168..212 m */
    /* THE COMMAND SECTION IS THE SHIP, and the first render is why that is
     * written down. At 32% of the length against a 54% engineering hull the
     * silhouette was a barge with a pointed nose: the secondary hull was
     * longer, wider and taller than the primary one, so the eye read the whole
     * thing as one slab. The primary hull leads. */
    int cmd_l = (int)lround(loa * 0.42);              /* the bow wedge  */
    int nek_l = (int)lround(loa * 0.12);              /* the spine      */
    int eng_l = loa - cmd_l - nek_l;

    /* THE DECK HEIGHT IS THE UNIT EVERYTHING ELSE IS BUILT FROM. 3 m floor to
     * floor is what the station used and what the doors, the trays and the
     * ladders are all sized against, so it does not move. */
    s->deck_h = 3;

    int cmd_hw = 28 + (int)rng_range(&r, 0, 8);       /* half beam at the widest */
    int cmd_hh = 7;                                   /* the wedge is LOW        */
    /* AND THE ENGINEERING HULL IS SMALLER THAN IT, in every dimension. It was
     * nearly as wide as the command section and taller, which left no contrast
     * between the two bodies at all. */
    int eng_hw = 11 + (int)rng_range(&r, 0, 3);
    int eng_hh = 9;
    /* WHERE EACH BODY SITS ON THE KEEL. The command section rides HIGH and the
     * engineering hull hangs BELOW it -- that offset is most of what makes a
     * two-hull ship read as two hulls. The first version had them the other way
     * round, with the secondary hull sitting above the primary, and the result
     * looked like a barge with a nose on it. */
    int cmd_cy = 13;
    int eng_cy = 6;

    /* --- THE COMMAND SECTION. A wedge: sharp at the bow, widest about two
     * thirds aft, and it keeps a flat bottom by carrying its centre high. */
    int f0 = s->nframe;
    frame_add(s, 0, 3, 2, cmd_cy);
    loft(s, 0, 3, 2, cmd_cy,
         (int)lround(cmd_l * 0.22), (int)lround(cmd_hw * 0.55), cmd_hh - 2, cmd_cy,
         1.0, true);
    loft(s, (int)lround(cmd_l * 0.22), (int)lround(cmd_hw * 0.55), cmd_hh - 2, cmd_cy,
         (int)lround(cmd_l * 0.68), cmd_hw, cmd_hh, cmd_cy, 1.0, true);
    loft(s, (int)lround(cmd_l * 0.68), cmd_hw, cmd_hh, cmd_cy,
         cmd_l, (int)lround(cmd_hw * 0.62), cmd_hh - 1, cmd_cy, 0.7, true);
    hull_add(s, HULL_COMMAND, f0, s->nframe - f0);

    /* --- THE NECK. Narrow across, tall up and down: a fin rather than a tube,
     * which is what makes the ship read as having a spine at all. Every run of
     * cable and every walk between the two halves goes through here, and the
     * riser is in it. */
    int nek_hw = 5, nek_hh = 10;
    int f1 = s->nframe;
    loft(s, cmd_l, (int)lround(cmd_hw * 0.62), cmd_hh - 1, cmd_cy,
         cmd_l + nek_l, nek_hw, nek_hh, (cmd_cy + eng_cy) / 2, 1.0, true);
    hull_add(s, HULL_NECK, f1, s->nframe - f1);

    /* --- THE ENGINEERING HULL. Deep and roughly round: the pressure vessel
     * that holds the core, the power plant and life support, and the only part
     * of the ship with enough height in it for a two-deck machinery space. */
    int f2 = s->nframe;
    int e0 = cmd_l + nek_l;
    loft(s, e0, nek_hw, nek_hh, (cmd_cy + eng_cy) / 2,
         e0 + (int)lround(eng_l * 0.25), eng_hw, eng_hh, eng_cy, 1.0, true);
    loft(s, e0 + (int)lround(eng_l * 0.25), eng_hw, eng_hh, eng_cy,
         e0 + (int)lround(eng_l * 0.74), eng_hw, eng_hh, eng_cy, 0.0, true);
    loft(s, e0 + (int)lround(eng_l * 0.74), eng_hw, eng_hh, eng_cy,
         loa, (int)lround(eng_hw * 0.6), (int)lround(eng_hh * 0.7), eng_cy, 0.8, true);
    hull_add(s, HULL_ENGINEERING, f2, s->nframe - f2);

    s->loa = loa;
    s->beam = cmd_hw * 2;

    /* --- THE DRIVE RING, round the stern, with the keel through the middle of
     * it. Its radius is set off the engineering hull rather than chosen, so a
     * fatter ship gets a bigger ring and the silhouette stays in proportion. */
    s->ring.cx = (int16_t)(e0 + (int)lround(eng_l * 0.72));
    s->ring.radius = (int16_t)(eng_hw + 15 + (int)rng_range(&r, 0, 5));
    s->ring.tube = 4;
    s->ring.pylon_w = 3;

    /* --- HOW MANY DECKS THE ENVELOPE WILL TAKE. Measured off the hull rather
     * than chosen: the tallest section decides, and the answer is what the
     * deck fitter will have to live inside. */
    int tallest = 0;
    for (int i = 0; i < s->nframe; i++)
        if (s->frame[i].half_h * 2 > tallest) tallest = s->frame[i].half_h * 2;
    s->decks = tallest / s->deck_h;
    ship_place_shafts(s);
    return s->nframe > 8;
}

void ship_free(Ship *s) { if (s) memset(s, 0, sizeof *s); }

/* ---------------------------------------------------------------- decks */

double ship_deck_floor(const Ship *s, int deck)
{
    /* FROM THE KEEL UP, in whole deck heights, so decks in the command section
     * and decks in the engineering hull are at the SAME heights and a walk
     * between them is level. A ship whose two halves had their own deck
     * spacing would need a ramp at every junction. */
    return (double)deck * (double)s->deck_h;
}

bool ship_deck_at(const Ship *s, int deck, double x, double z)
{
    double y = ship_deck_floor(s, deck);
    /* Standing room: the floor has to be inside the hull, and so does the
     * space a person occupies above it. */
    return ship_inside(s, x, y + 0.1, z) &&
           ship_inside(s, x, y + SHIP_HEADROOM, z);
}

bool ship_deck_bounds(const Ship *s, int deck, double *x0, double *x1,
                      double *z0, double *z1, double *area)
{
    double ax0 = 1e9, ax1 = -1e9, az0 = 1e9, az1 = -1e9;
    double a = 0.0;
    for (int x = 0; x <= s->loa; x++) {
        double hw = 0, hh = 0, cy = 0;
        if (!ship_section(s, x, &hw, &hh, &cy)) continue;
        for (double z = -hw; z <= hw; z += 1.0) {
            if (!ship_deck_at(s, deck, x, z)) continue;
            a += 1.0;
            if (x < ax0) ax0 = x;
            if (x > ax1) ax1 = x;
            if (z < az0) az0 = z;
            if (z > az1) az1 = z;
        }
    }
    if (a <= 0.0) return false;
    if (x0) *x0 = ax0;  if (x1) *x1 = ax1;
    if (z0) *z0 = az0;  if (z1) *z1 = az1;
    if (area) *area = a;
    return true;
}

/* --------------------------------------------------------------- shafts */

/* Does a shaft centred here have floor on every deck from d0 to d1? A shaft is
 * a hole through the ship and it has to land on something at both ends and
 * everywhere between, or it opens into vacuum. */
static bool shaft_fits(const Ship *s, double x, double z, int d0, int d1)
{
    for (int d = d0; d <= d1; d++) {
        for (int dx = -SHIP_SHAFT_R; dx <= SHIP_SHAFT_R; dx++)
            for (int dz = -SHIP_SHAFT_R; dz <= SHIP_SHAFT_R; dz++)
                if (!ship_deck_at(s, d, x + dx, z + dz)) return false;
    }
    return true;
}

/* The longest run of decks a shaft at (x,z) could serve, and how many that is.
 * Searched rather than chosen: where a shaft CAN go is a fact about the hull. */
static int shaft_run(const Ship *s, double x, double z, int *best0, int *best1)
{
    int best = 0, b0 = -1, b1 = -1;
    for (int d0 = 0; d0 < 24; d0++) {
        if (!shaft_fits(s, x, z, d0, d0)) continue;
        int d1 = d0;
        while (d1 + 1 < 24 && shaft_fits(s, x, z, d0, d1 + 1)) d1++;
        if (d1 - d0 + 1 > best) { best = d1 - d0 + 1; b0 = d0; b1 = d1; }
        d0 = d1;
    }
    if (best0) *best0 = b0;
    if (best1) *best1 = b1;
    return best;
}

int ship_place_shafts(Ship *s)
{
    s->nshaft = 0;
    /* GREEDY, AND SPREAD OUT. Take the spot that serves the most decks, then
     * refuse anything within 24 m of one already placed -- otherwise every
     * shaft lands in the same fat part of the hull and half the ship is a long
     * walk from any of them. The spacing is the only number here that is a
     * choice rather than a measurement, and it is roughly how far anybody will
     * tolerate walking to a lift. */
    for (int n = 0; n < SHIP_MAX_SHAFT; n++) {
        double bx = -1, bz = 0;
        int brun = 0, b0 = -1, b1 = -1;
        for (int x = 4; x <= s->loa - 4; x += 2) {
            double hw = 0, hh = 0, cy = 0;
            if (!ship_section(s, x, &hw, &hh, &cy)) continue;
            for (double z = -hw; z <= hw; z += 2.0) {
                bool near = false;
                for (int i = 0; i < s->nshaft && !near; i++) {
                    double dx = x - s->shaft[i].x, dz = z - s->shaft[i].z;
                    if (dx * dx + dz * dz < 24.0 * 24.0) near = true;
                }
                if (near) continue;
                int d0, d1;
                int run = shaft_run(s, x, z, &d0, &d1);
                if (run > brun) { brun = run; bx = x; bz = z; b0 = d0; b1 = d1; }
            }
        }
        if (brun < 2 || bx < 0) break;      /* a shaft serving one deck is a room */
        Shaft *sh = &s->shaft[s->nshaft++];
        /* A LIFT WHERE IT IS WORTH ONE, STAIRS WHERE IT IS NOT. Four decks or
         * more gets a turbolift; a short hop gets a stairwell, which is what a
         * ship really does and means a lift failure is not automatically a
         * deck nobody can reach. */
        sh->kind = (uint8_t)(brun >= 4 ? SHAFT_LIFT : SHAFT_STAIR);
        sh->x = (int16_t)bx; sh->z = (int16_t)bz;
        sh->deck0 = (int16_t)b0; sh->deck1 = (int16_t)b1;
    }
    return s->nshaft;
}

/* ------------------------------------------------------------ reachable */

bool ship_reach(const Ship *s, int from_deck, double fx, double fz,
                long *reached, long *total)
{
    int w = s->loa + 1;
    int zr = s->beam / 2 + s->ring.radius + 4;
    int h = 2 * zr + 1;
    int nd = 24;
    size_t n = (size_t)nd * w * h;
    unsigned char *cell = nom_alloc(n);
    int *q = nom_alloc(sizeof(int) * n);
    if (!cell || !q) return false;
    memset(cell, 0, n);

    long have = 0;
    for (int d = 0; d < nd; d++)
        for (int x = 0; x < w; x++)
            for (int z = -zr; z <= zr; z++)
                if (ship_deck_at(s, d, x, z)) {
                    cell[((size_t)d * w + x) * h + (z + zr)] = 1;
                    have++;
                }

    size_t start = (((size_t)from_deck * w + (int)fx) * h + ((int)fz + zr));
    if (from_deck < 0 || from_deck >= nd || cell[start] != 1) {
        nom_free(cell); nom_free(q);
        if (total) *total = have;
        if (reached) *reached = 0;
        return false;
    }
    int qn = 0;
    q[qn++] = (int)start; cell[start] = 2;
    long seen = 1;
    static const int DX[4] = { 1, -1, 0, 0 }, DZ[4] = { 0, 0, 1, -1 };
    for (int qi = 0; qi < qn; qi++) {
        int u = q[qi];
        int d = u / (w * h), rem = u % (w * h);
        int x = rem / h, z = rem % h - zr;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], nz = z + DZ[k];
            if (nx < 0 || nx >= w || nz < -zr || nz > zr) continue;
            size_t v = (((size_t)d * w + nx) * h + (nz + zr));
            if (cell[v] != 1) continue;
            cell[v] = 2; seen++; q[qn++] = (int)v;
        }
        /* AND UP OR DOWN, where a shaft passes through this square metre. */
        for (int i = 0; i < s->nshaft; i++) {
            const Shaft *sh = &s->shaft[i];
            if (abs(x - sh->x) > SHIP_SHAFT_R || abs(z - sh->z) > SHIP_SHAFT_R)
                continue;
            for (int nd2 = sh->deck0; nd2 <= sh->deck1; nd2++) {
                if (nd2 == d) continue;
                size_t v = (((size_t)nd2 * w + x) * h + (z + zr));
                if (cell[v] != 1) continue;
                cell[v] = 2; seen++; q[qn++] = (int)v;
            }
        }
    }
    nom_free(cell); nom_free(q);
    if (reached) *reached = seen;
    if (total) *total = have;
    return seen == have;
}

/* ------------------------------------------------------------ questions */

bool ship_section(const Ship *s, double x, double *half_w, double *half_h,
                  double *cy)
{
    if (!s || s->nframe < 2) return false;
    if (x < s->frame[0].x || x > s->frame[s->nframe - 1].x) return false;
    for (int i = 1; i < s->nframe; i++) {
        if (x > s->frame[i].x) continue;
        const Frame *a = &s->frame[i - 1], *b = &s->frame[i];
        double span = (double)(b->x - a->x);
        double t = span > 0 ? (x - a->x) / span : 0.0;
        if (half_w) *half_w = a->half_w + (b->half_w - a->half_w) * t;
        if (half_h) *half_h = a->half_h + (b->half_h - a->half_h) * t;
        if (cy)     *cy     = a->cy + (b->cy - a->cy) * t;
        return true;
    }
    return false;
}

bool ship_inside(const Ship *s, double x, double y, double z)
{
    double hw, hh, cy;
    if (!ship_section(s, x, &hw, &hh, &cy)) return false;
    if (hw <= 0.0 || hh <= 0.0) return false;
    double dz = z / hw;
    double dy = (y - cy) / hh;
    return dz * dz + dy * dy <= 1.0;
}
