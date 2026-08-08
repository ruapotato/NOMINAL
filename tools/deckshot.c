/* EVERY DECK OF A GENERATED SHIP, AS A PLAN, one under the other.
 *
 * The station's decks were the thing four sessions of work could not fix:
 * identical rectangular plates, "six floors of essentially the same thing",
 * "giant square rooms full of nothing". This draws what the new generator
 * gives instead -- and the point of drawing it before anything is placed is
 * that the SHAPE of each deck is now a consequence of the hull rather than a
 * choice, so if the plans are boring the hull is wrong and no amount of room
 * dealing will save it.
 *
 *     build/deckshot <seed> > /tmp/decks.ppm
 */
#include "nom.h"
#include "ship.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define W 1100
#define H 1000

static unsigned char img[H][W][3];

static void px(int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    img[y][x][0] = (unsigned char)r;
    img[y][x][1] = (unsigned char)g;
    img[y][x][2] = (unsigned char)b;
}

/* Which body is this station in? Only for shading, so the reader can see the
 * command section, the neck and the engineering hull come apart. */
static int body_at(const Ship *s, double x)
{
    for (int i = 0; i < s->nhull; i++) {
        const Hull *h = &s->hull[i];
        if (x >= s->frame[h->frame0].x &&
            x <= s->frame[h->frame0 + h->nframe - 1].x) return h->kind;
    }
    return HULL_ENGINEERING;
}

int main(int argc, char **argv)
{
    uint64_t seed = argc > 1 ? (uint64_t)strtoull(argv[1], NULL, 10) : 1;
    Ship s;
    if (!ship_generate(&s, seed)) { fprintf(stderr, "no ship\n"); return 1; }

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            { img[y][x][0] = 14; img[y][x][1] = 17; img[y][x][2] = 21; }

    /* How many decks actually have floor in them, asked rather than assumed --
     * the envelope's height divided by the deck height is an upper bound and
     * the top and bottom of an elliptical hull have no standing room in them. */
    int lo = -1, hi = -1;
    for (int d = 0; d < 24; d++) {
        double a;
        if (!ship_deck_bounds(&s, d, NULL, NULL, NULL, NULL, &a)) continue;
        if (a < 40.0) continue;          /* a few square metres is not a deck */
        if (lo < 0) lo = d;
        hi = d;
    }
    if (lo < 0) { fprintf(stderr, "no decks\n"); return 1; }

    int ndeck = hi - lo + 1;
    /* ONE SCALE, SET BY WHICHEVER WAY ROUND RUNS OUT FIRST. The first version
     * fitted the ship's LENGTH to the page and let the beam do what it liked:
     * at 5.6 px/m a 62 m beam is 347 px in a 163 px band, so every deck was
     * drawn over the two below it and the picture was unreadable. */
    int band = (H - 20) / ndeck;
    double scale = (W - 60) / (double)(s.loa + 16);
    double fit_beam = (band - 6) / (double)(s.beam + 4);
    if (fit_beam < scale) scale = fit_beam;
    fprintf(stderr, "seed %llu: %d m, decks %d..%d (%d of them), %.1f px/m\n",
            (unsigned long long)seed, s.loa, lo, hi, ndeck, scale);

    for (int d = lo; d <= hi; d++) {
        int row = d - lo;
        int oy = 10 + row * band + band / 2;
        double area = 0;
        ship_deck_bounds(&s, d, NULL, NULL, NULL, NULL, &area);
        for (int x = 0; x <= s.loa; x++) {
            double hw = 0, hh = 0, cy = 0;
            if (!ship_section(&s, x, &hw, &hh, &cy)) continue;
            /* A SAMPLE PER PIXEL. Stepping a third of a metre at 5 px/m left
             * gaps down every deck: the plans came out as stipple. */
            double step = 1.0 / scale;
            for (double z = -hw - 1.0; z <= hw + 1.0; z += step) {
                if (!ship_deck_at(&s, d, x, z)) continue;
                int b = body_at(&s, x);
                int r = 146, g = 160, bl = 176;
                if (b == HULL_COMMAND) { r = 176; g = 196; bl = 214; }
                else if (b == HULL_NECK) { r = 210; g = 170; bl = 110; }
                px(30 + (int)((x + 8) * scale),
                   oy + (int)(z * scale), r, g, bl);
            }
        }
        fprintf(stderr, "  deck %2d at %5.1f m: %6.0f m2 of floor\n",
                d, ship_deck_floor(&s, d), area);
    }

    printf("P6\n%d %d\n255\n", W, H);
    fwrite(img, 1, sizeof img, stdout);
    return 0;
}
