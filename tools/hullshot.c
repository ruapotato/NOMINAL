/* THREE ORTHOGRAPHIC VIEWS OF A GENERATED HULL, as a PNG-able PPM.
 *
 * The first thing to get right about this ship is its silhouette, and the way
 * you judge a hull is the way a naval architect does: profile, plan and bow.
 * Rendering it in Godot first would mean building a mesh pipeline before
 * knowing whether the shape is worth one, so this draws straight off the model
 * -- ship_inside() and the ring -- and nothing between the two can flatter it.
 *
 *     build/hullshot <seed> > /tmp/hull.ppm
 */
#include "nom.h"
#include "ship.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define W 1200
#define H 900

static unsigned char img[H][W][3];

static void px(int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    img[y][x][0] = (unsigned char)r;
    img[y][x][1] = (unsigned char)g;
    img[y][x][2] = (unsigned char)b;
}

/* Is (x,y,z) in metres inside the ring's torus, or one of its two pylons? */
static bool in_ring(const Ship *s, double x, double y, double z)
{
    const Ring *g = &s->ring;
    double dx = x - g->cx;
    double rr = sqrt(y * y + z * z);
    double dr = rr - g->radius;
    if (dx * dx + dr * dr <= (double)g->tube * g->tube) return true;
    /* the pylons: two slabs from the hull out to the ring, port and starboard */
    if (fabs(dx) <= g->pylon_w && fabs(y) <= g->pylon_w) {
        double hw = 0, hh = 0, cy = 0;
        ship_section(s, x, &hw, &hh, &cy);
        if (fabs(z) >= hw * 0.6 && fabs(z) <= g->radius) return true;
    }
    return false;
}

static bool solid(const Ship *s, double x, double y, double z)
{
    return ship_inside(s, x, y, z) || in_ring(s, x, y, z);
}

/* One orthographic view. `axis` 0 = profile (look from starboard, x/y),
 * 1 = plan (from above, x/z), 2 = bow (from ahead, z/y). */
static void view(const Ship *s, int axis, int ox, int oy, int vw, int vh,
                 double scale)
{
    double reach = s->beam;
    if (s->ring.radius * 2 > reach) reach = s->ring.radius * 2;
    for (int py_ = 0; py_ < vh; py_++) {
        for (int px_ = 0; px_ < vw; px_++) {
            double a = (px_ - vw * 0.5) / scale;
            double b = (vh * 0.5 - py_) / scale;
            bool hit = false;
            /* march the third axis; the hull is convex enough in section that
             * a metre step never misses it */
            for (double c = -reach; c <= reach && !hit; c += 0.5) {
                double x, y, z;
                if (axis == 0)      { x = a + s->loa * 0.5; y = b; z = c; }
                else if (axis == 1) { x = a + s->loa * 0.5; z = b; y = c; }
                else                { z = a; y = b; x = c + s->loa * 0.5; }
                if (solid(s, x, y, z)) hit = true;
            }
            if (hit) {
                /* shade by which body it is, so the sections read apart */
                double x = (axis == 2) ? s->loa * 0.5 : a + s->loa * 0.5;
                int r = 150, g = 170, bl = 190;
                for (int i = 0; i < s->nhull; i++) {
                    const Hull *h = &s->hull[i];
                    int fx0 = s->frame[h->frame0].x;
                    int fx1 = s->frame[h->frame0 + h->nframe - 1].x;
                    if (x >= fx0 && x <= fx1) {
                        if (h->kind == HULL_COMMAND)     { r = 176; g = 196; bl = 214; }
                        else if (h->kind == HULL_NECK)   { r = 120; g = 140; bl = 160; }
                        else                              { r = 146; g = 160; bl = 176; }
                    }
                }
                px(ox + px_, oy + py_, r, g, bl);
            }
        }
    }
}

int main(int argc, char **argv)
{
    uint64_t seed = argc > 1 ? (uint64_t)strtoull(argv[1], NULL, 10) : 1;
    Ship s;
    if (!ship_generate(&s, seed)) { fprintf(stderr, "no ship\n"); return 1; }
    fprintf(stderr, "seed %llu: %d m long, %d m beam, %d frames, "
                    "ring r=%d at x=%d, %d decks of %d m\n",
            (unsigned long long)seed, s.loa, s.beam, s.nframe,
            s.ring.radius, s.ring.cx, s.decks, s.deck_h);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            { img[y][x][0] = 14; img[y][x][1] = 17; img[y][x][2] = 21; }

    double scale = (W - 80) / (double)(s.loa + 20);
    view(&s, 0, 40,  20, W - 80, 260, scale);          /* profile */
    view(&s, 1, 40, 300, W - 80, 300, scale);          /* plan    */
    view(&s, 2, W / 2 - 220, 620, 440, 260, scale);    /* bow     */

    printf("P6\n%d %d\n255\n", W, H);
    fwrite(img, 1, sizeof img, stdout);
    return 0;
}
