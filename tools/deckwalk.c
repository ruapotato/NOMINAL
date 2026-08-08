/* CAN YOU WALK FROM THE BOW TO THE STERN ON THIS DECK?
 *
 * Written because the README now claims, of seed 1, that "deck 2 has floor in
 * the bow and floor in the stern and no walkable route between them" -- and
 * that claim was made by looking at a picture. This project's rule is that
 * every technical claim is true of the machine, verified by running it, so
 * either this agrees with the README or the README is wrong.
 *
 * A flood fill over the deck's own cells, from the forwardmost square metre of
 * floor. Four-connected, because a body does not squeeze through a diagonal.
 *
 *     build/deckwalk <seed>
 */
#include "nom.h"
#include "ship.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ZR 80          /* +/- metres either side of the keel */

int main(int argc, char **argv)
{
    uint64_t seed = argc > 1 ? (uint64_t)strtoull(argv[1], NULL, 10) : 1;
    Ship s;
    if (!ship_generate(&s, seed)) { fprintf(stderr, "no ship\n"); return 1; }
    printf("seed %llu: %d m, %d frames\n",
           (unsigned long long)seed, s.loa, s.nframe);

    int w = s.loa + 1, h = 2 * ZR + 1;
    unsigned char *cell = calloc((size_t)w * h, 1);
    int *queue = malloc(sizeof(int) * (size_t)w * h);
    if (!cell || !queue) return 1;

    for (int d = 0; d < 24; d++) {
        double area = 0;
        if (!ship_deck_bounds(&s, d, NULL, NULL, NULL, NULL, &area)) continue;
        if (area < 40.0) continue;

        memset(cell, 0, (size_t)w * h);
        int have = 0, bow = -1, stern = -1;
        for (int x = 0; x < w; x++)
            for (int z = -ZR; z <= ZR; z++)
                if (ship_deck_at(&s, d, x, z)) {
                    cell[(size_t)x * h + (z + ZR)] = 1;
                    have++;
                    if (bow < 0) bow = x;
                    stern = x;
                }
        if (have == 0) continue;

        /* flood from the forwardmost cell */
        int qn = 0, start = -1;
        for (int z = -ZR; z <= ZR && start < 0; z++)
            if (cell[(size_t)bow * h + (z + ZR)]) start = (int)((size_t)bow * h + (z + ZR));
        queue[qn++] = start;
        cell[start] = 2;
        int seen = 1;
        for (int qi = 0; qi < qn; qi++) {
            int u = queue[qi];
            int ux = u / h, uz = u % h;
            static const int DX[4] = { 1, -1, 0, 0 }, DZ[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = ux + DX[k], nz = uz + DZ[k];
                if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                size_t v = (size_t)nx * h + nz;
                if (cell[v] != 1) continue;
                cell[v] = 2;
                seen++;
                queue[qn++] = (int)v;
            }
        }
        /* is any of the sternmost floor in the same piece? */
        bool joined = false;
        for (int z = -ZR; z <= ZR && !joined; z++)
            if (cell[(size_t)stern * h + (z + ZR)] == 2) joined = true;

        printf("  deck %2d  %5d m2  bow x=%3d  stern x=%3d  "
               "%5d of %5d reachable from the bow  %s\n",
               d, have, bow, stern, seen, have,
               joined ? "BOW TO STERN ON FOOT"
                      : "the two ends are separate pieces");
    }
    free(cell); free(queue);

    /* AND THE WHOLE SHIP, which is the question that matters: David asked to
     * "make sure that you can get to everywhere on the ship. Either by turbo
     * lifts in certain areas or stairwells." Deck by deck is a diagnostic;
     * this is the claim. Started from the engineering hull, because that is
     * where the engineer starts every morning. */
    printf("  shafts:\n");
    for (int i = 0; i < s.nshaft; i++)
        printf("    %-5s at x=%3d z=%4d, decks %d..%d\n",
               s.shaft[i].kind == SHAFT_LIFT ? "lift" : "stair",
               s.shaft[i].x, s.shaft[i].z, s.shaft[i].deck0, s.shaft[i].deck1);

    /* the aftmost square metre of floor on the lowest deck with any */
    int sd = -1; double sx = 0, sz = 0;
    for (int d = 0; d < 24 && sd < 0; d++) {
        double a;
        if (!ship_deck_bounds(&s, d, NULL, NULL, NULL, NULL, &a) || a < 40) continue;
        for (int x = s.loa; x >= 0 && sd < 0; x--)
            for (int z = -ZR; z <= ZR && sd < 0; z++)
                if (ship_deck_at(&s, d, x, z)) { sd = d; sx = x; sz = z; }
    }
    long reached = 0, total = 0;
    bool all = ship_reach(&s, sd, sx, sz, &reached, &total);
    printf("  from deck %d at (%.0f,%.0f): %ld of %ld m2 reachable -- %s\n",
           sd, sx, sz, reached, total,
           all ? "THE WHOLE SHIP" : "part of the ship cannot be walked to");
    return all ? 0 : 2;
}
