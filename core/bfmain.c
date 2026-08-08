/* bfmain.c — the break-fix harness.
 *
 * Proves the two D17 gates over RANDOM corruption, which is the only kind
 * there is now:
 *   --survey N   what do N random tickets look like? are they diverse?
 *   --solve  N   can pkg verify see them, and does reinstall fix them?
 *   <seed>       play one: print the console the customer would send you
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "nom.h"
#include "machine.h"
#include "kernel.h"
#include "building.h"
#include "netstack.h"
#include "site.h"
#include "session.h"

/* The network's own gate. See core/netcheck.c. */
int net_selfcheck(void);

/* Free the space and the inodes that are scratch BY DEFINITION.
 *
 * A full disk is not a package problem and no amount of reinstalling helps:
 * every file is exactly right, there is simply nowhere to put the next one.
 * A filesystem out of inodes cannot be helped by freeing bytes at all. What
 * both have in common is that the answer is in a directory whose whole
 * purpose is to hold things nobody owns -- a log, a spool, a cache -- so this
 * removes what no package owns from exactly those places and nothing else.
 *
 * It knows nothing about which fault was injected. It is a rule about what
 * those directories ARE. `prefix` is "/mnt" when the disk is mounted under a
 * rescue system and "" when we are standing inside it.
 */
static void free_scratch(Machine *m, const char *prefix, Buf *o)
{
    char cmd[NOM_PATH_MAX * 2];
    snprintf(cmd, sizeof cmd, "rm %s/var/log/messages", prefix);
    kernel_run(m, cmd, o);

    static const char *SCRATCH[] = {
        "/var/spool/cron", "/var/cache", "/tmp", NULL };
    for (int s = 0; SCRATCH[s]; s++) {
        VNode *d = vfs_resolve(&m->disk, SCRATCH[s], NULL);
        for (VNode *kid = d ? d->child : NULL; kid; ) {
            VNode *next = kid->next;
            char full[NOM_PATH_MAX];
            snprintf(full, sizeof full, "%s/%s", SCRATCH[s], kid->name);
            if (kid->kind == VN_FILE && !pkg_owns(m, full)) {
                snprintf(cmd, sizeof cmd, "rm %s%s", prefix, full);
                kernel_run(m, cmd, o);
            }
            kid = next;
        }
    }
}

/* A TYPED LINE THAT DOES NOT FIT MUST NOT BECOME TWO COMMANDS.
 *
 * These loops read with fgets into a fixed buffer, and fgets leaves the rest
 * of the line in the stream -- so a command longer than the buffer ran its
 * first half and then ran its own tail as a second command. Pasting twenty
 * paths at 30 bytes each produced `rm /mnt/var/cache/...pkg /mnt/var/c` and
 * then `ache/package-016.pkg: command not found`, which names a file that
 * does not exist, from a command nobody typed. The buffer is NOM_ARG_MAX now,
 * the same ceiling the machine itself has, and anything past it is swallowed
 * and reported rather than executed. */
static bool read_line(char *line, size_t cap)
{
    if (!fgets(line, (int)cap, stdin)) return false;
    size_t l = strlen(line);
    if (l && line[l-1] != '\n') {
        /* Eat the rest of the line so its tail cannot be run as a command. */
        int ch, over = 0;
        while ((ch = getchar()) != EOF && ch != '\n') over++;
        printf("this line is longer than %zu bytes and %d more were dropped.\n"
               "  nothing was run. the machine's own argument limit is the same\n"
               "  size, so shorten it or let a glob expand it on the machine.\n",
               cap - 1, over);
        line[0] = 0;
        return true;
    }
    while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
    return true;
}


/* ------------------------------------------------------- --askcheck helpers --
 *
 * The gate below drives the customer through her own published interface --
 * customer_options() and customer_choose() -- rather than reaching inside her.
 * That is deliberate: the front end being built against this API can only see
 * what these two functions say, so anything the gate cannot reach from here is
 * something the game cannot rely on either.
 */

/* The id of an option by its exact label, or -1 if she is not offering it. */
static int ac_find(Machine *m, const char *label)
{
    Buf l = {0};
    int found = -1;
    customer_options(m, &l);
    for (size_t i = 0; i < l.len && found < 0; ) {
        size_t e = i;
        while (e < l.len && l.p[e] != '\n') e++;
        char line[200];
        size_t n = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, l.p + i, n); line[n] = 0;
        int id = 0; char lab[160] = "";
        if (sscanf(line, "  [%d] %159[^\n]", &id, lab) == 2 &&
            strcmp(lab, label) == 0) found = id;
        i = e < l.len ? e + 1 : e;
    }
    buf_free(&l);
    return found;
}

/* A ticket in one of five states a call actually reaches. Every state is
 * arrived at THROUGH HER, because that is the only way an air-gapped machine
 * can be driven at all -- which also means this exercises the actions rather
 * than reaching past them. */
static void ac_state(Machine *m, uint64_t seed, int mode, Buf *o)
{
    char what[512];
    machine_install(m, seed);
    machine_break(m, seed, 1, what, sizeof what);
    m->airgapped = machine_airgapped(seed);
    if (mode == 4) {
        customer_choose(m, ac_find(m, "ask her to turn it off"), NULL, o);
        return;
    }
    if (mode >= 1)
        customer_choose(m, ac_find(m, "ask her to put the rescue disc in"), NULL, o);
    if (mode >= 2)
        customer_choose(m, ac_find(m, "ask her to turn it off and on again"), NULL, o);
    if (mode >= 3)
        customer_choose(m, ac_find(m, "\"can I have you run:\"  <command>"),
                        "mount /dev/sda1 /mnt", o);
}

/* Did the machine really print this? `slack` allows the one or two characters
 * a person transposes; with it NULL nothing is allowed at all. */
static bool ac_onscreen(const Machine *m, const char *quoted, int *misread)
{
    size_t q = strlen(quoted);
    if (!q) return true;
    const Buf *c = &m->boot.console;
    bool near = false;
    for (size_t i = 0; i < c->len; ) {
        size_t e = i;
        while (e < c->len && c->p[e] != '\n') e++;
        size_t n = e - i;
        if (n >= q) {
            int diff = 0;
            for (size_t k = 0; k < q; k++) if (c->p[i + k] != quoted[k]) diff++;
            if (diff == 0) return true;
            if (diff <= 2 && misread) near = true;
        }
        i = e < c->len ? e + 1 : e;
    }
    if (near && misread) { (*misread)++; return true; }
    return false;
}

/* Every line she reads back, checked against what is actually on that screen.
 * This is the one that keeps her fair: she is allowed to be slow, narrow and
 * a bit deaf, and she is not allowed to invent a byte. */
static bool ac_quoted(const Machine *m, const char *p, size_t len, int *misread)
{
    if (!p) return true;
    for (size_t i = 0; i < len; ) {
        size_t e = i;
        while (e < len && p[e] != '\n') e++;
        if (e - i > 6 && memcmp(p + i, "    | ", 6) == 0) {
            char line[300];
            size_t n = e - i - 6;
            if (n >= sizeof line) n = sizeof line - 1;
            memcpy(line, p + i + 6, n); line[n] = 0;
            if (!ac_onscreen(m, line, misread)) return false;
        }
        i = e < len ? e + 1 : e;
    }
    return true;
}

static bool ac_quotes_real(const Machine *m, const char *p, size_t len, int *mis)
{ return ac_quoted(m, p, len, mis); }

static bool ac_quotes_exact(const Machine *m, const char *p, size_t len)
{ return ac_quoted(m, p, len, NULL); }

/* SHE NEVER USES A TECHNICAL WORD SHE WAS NOT READ. The two exemptions are
 * exactly that: a command she was dictated and is repeating back, and a line
 * off the screen. Both are things somebody read to her. */
static bool ac_no_jargon(const char *p, size_t len, const char **words)
{
    if (!p) return true;
    for (size_t i = 0; i < len; ) {
        size_t e = i;
        while (e < len && p[e] != '\n') e++;
        if (e - i > 3 && memcmp(p + i, "  \"", 3) == 0 &&
            !(e - i > 26 && memcmp(p + i, "  \"Alright... I have typed", 26) == 0)) {
            char line[400];
            size_t n = e - i;
            if (n >= sizeof line) n = sizeof line - 1;
            for (size_t k = 0; k < n; k++)
                line[k] = (p[i + k] >= 'A' && p[i + k] <= 'Z')
                          ? (char)(p[i + k] + 32) : p[i + k];
            line[n] = 0;
            for (int w = 0; words[w]; w++)
                if (strstr(line, words[w])) return false;
        }
        i = e < len ? e + 1 : e;
    }
    return true;
}

/* A CHECK THAT HAS NEVER FIRED IS NOT A CHECK. Every gate in this repo has
 * at some point reported a steady, confident number about a class of failure
 * it could not see. So the building gate is itself gated: take a good tower,
 * break it one specific way, and the check that owns that failure has to go
 * off. If it does not, the 200/200 above means nothing.
 *
 * Returns the check that must fire, or -1 if this mutation does not apply to
 * this seed. */
static int bld_mutate(Building *b, int which)
{
    int top = b->floors - 1;
    switch (which) {
    case BC_STACK:      /* the top plate overhangs the one below it */
        if (top < 1) return -1;
        b->fx1[top] = (int16_t)(b->fx1[top - 1] + 1);
        return BC_STACK;
    case BC_TESSELLATE: { /* A SEALED VOID: a metre with no hull round it and
                           * no way out to space.
                           *
                           * This used to shrink an office by a metre, which
                           * left a hole in a plate that was filled edge to
                           * edge. A station's plate is NOT filled -- the
                           * corners between the arms are vacuum -- so that
                           * hole now floods to space through the arm's own
                           * end and the check is right not to complain. The
                           * decoy has to make the thing the check is really
                           * about: a void the outside cannot reach. Take a
                           * bite out of a room on the INSIDE of the ring,
                           * which is enclosed on all four sides. */
        int r = -1;
        for (int i = 0; i < b->nrooms; i++) {
            const Room *rm = &b->rooms[i];
            if (rm->floor != 1 || rm->kind != RM_TOILET) continue;
            if (rm->x1 - rm->x0 < 3 || rm->y1 - rm->y0 < 3) continue;
            r = i; break;
        }
        if (r < 0) return -1;
        /* Shrink the RECTANGLE, because that is what the check reads: the
         * heads are in the middle of the hub with a stairwell one side and
         * the lifts the other, so the strip this leaves is enclosed. */
        b->rooms[r].x1 = (int16_t)(b->rooms[r].x1 - 1);
        return BC_TESSELLATE; }
    case BC_ROOMSIZE: { /* a room one metre wide */
        int r = bld_find(b, 1, RM_OFFICE);
        if (r < 0) r = bld_find(b, 1, RM_RESIDENCE);
        if (r < 0) return -1;
        b->rooms[r].y1 = (int16_t)(b->rooms[r].y0 + 1);
        return BC_ROOMSIZE; }
    case BC_ALIGN: {    /* the riser steps sideways between two floors */
        int r = bld_find(b, 1, RM_RISER);
        if (r < 0) return -1;
        b->rooms[r].x0 = (int16_t)(b->rooms[r].x0 + 1);
        b->rooms[r].x1 = (int16_t)(b->rooms[r].x1 + 1);
        return BC_ALIGN; }
    case BC_DOORS:      /* a door that says it stands somewhere it does not */
        if (!b->ndoors) return -1;
        b->doors[0].a = (uint16_t)((b->doors[0].a + 3) % b->nrooms);
        return BC_DOORS;
    case BC_ROOMDOOR:   /* an office with no way in */
    case BC_REACH:
        for (int i = 0; i < b->ndoors; i++) {
            int ka = b->rooms[b->doors[i].a].kind, kb = b->rooms[b->doors[i].b].kind;
            if (ka != RM_OFFICE && ka != RM_RESIDENCE &&
                kb != RM_OFFICE && kb != RM_RESIDENCE) continue;
            b->edge[(((size_t)b->doors[i].floor * (size_t)b->h + (size_t)b->doors[i].y)
                     * (size_t)b->w) + (size_t)b->doors[i].x] = 0;
            b->doors[i] = b->doors[--b->ndoors];
            return which;
        }
        return -1;
    case BC_CORRIDOR: { /* the ring cut in two: two pieces, four dead ends.
                         * Cutting it ONCE is not enough and that is the point
                         * -- a loop survives one break, which is why a loop is
                         * what a building has. */
        int cut = 0;
        for (int i = 0; i < b->nrooms && cut < 2; i++) {
            const Room *rm = &b->rooms[i];
            if (rm->floor != 1 || rm->kind != RM_CORRIDOR) continue;
            /* A LEG OF THE RING, not an arm's spine. The station's arms are
             * corridors too, and cutting two of those leaves the ring whole
             * -- which is the honest answer and not a miss, so the decoy has
             * to cut the ring itself. A ring leg lies against the hub. */
            if (rm->x0 < b->ring_x0 || rm->x1 > b->ring_x1) continue;
            if (rm->y0 < b->ring_y0 || rm->y1 > b->ring_y1) continue;
            if (rm->x1 - rm->x0 <= 6) continue;      /* a north/south leg */
            /* NOT AT THE MIDDLE AND NOT AT A CORNER.
             *
             * An arm's spine meets the ring at the centre of the leg and is
             * five metres wide, so a one-metre cut there is BRIDGED by the
             * arm. And the ring is four metres wide now, so its corners are
             * four-by-four blocks joined to the leg round the turn -- a cut
             * two metres in is bridged by the corner. Both of those are true
             * and rather nice properties of building the station this way,
             * and neither is a miss by the check. A quarter of the way along
             * is clear of both. */
            int x = rm->x0 + (rm->x1 - rm->x0) / 4;
            for (int y = rm->y0; y < rm->y1; y++)
                b->cell[(((size_t)1 * (size_t)b->h + (size_t)y) * (size_t)b->w) + (size_t)x]
                    = BLD_NOROOM;
            cut++;
        }
        return cut == 2 ? BC_CORRIDOR : -1; }
    case BC_PRIVACY: {  /* one tenant's only way in is through another's */
        for (int i = 0; i < b->ndoors; i++) {
            int a = b->doors[i].a, bb = b->doors[i].b;
            if (b->rooms[a].kind == RM_CORRIDOR || b->rooms[bb].kind == RM_CORRIDOR) continue;
            b->rooms[a].tenant = 251; b->rooms[bb].tenant = 252;
            return BC_PRIVACY;
        }
        return -1; }
    case BC_PROGRAM: {  /* the building's uplink has nowhere to land */
        int m = bld_find(b, 0, RM_MDF);
        if (m < 0) return -1;
        b->rooms[m].kind = RM_RETAIL;
        return BC_PROGRAM; }
    case BC_METRIC:     /* the riser no longer joins one floor to the next */
        for (int u = 0; u < b->nrooms; u++) {
            if (b->rooms[u].kind != RM_RISER) continue;
            for (int e = b->cg_head[u]; e < b->cg_head[u + 1]; e++)
                if (b->cg_to[e] < b->nrooms && b->rooms[b->cg_to[e]].kind == RM_RISER)
                    b->cg_to[e] = u;
        }
        return BC_METRIC;
    default:
        return -1;
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--building") == 0) {
        /* THE BUILDING IS GAMEPLAY DATA, SO IT GETS A GATE LIKE EVERYTHING
         * ELSE. A tower that looks fine in a render and has an office with no
         * door, or a riser that steps sideways between floors, would be
         * discovered by a player halfway through paying for a cable run. Every
         * claim building.h makes is checked here, over N seeds, and a failure
         * prints the seed so it can be looked at with --floorplan. */
        int n = argc > 2 ? atoi(argv[2]) : 200, bad = 0;
        int fails[BC_COUNT] = {0}, hit[BC_COUNT] = {0};
        int nogen = 0, ndeterm = 0;
        long rooms = 0, doors = 0, floors = 0; double area = 0;
        double diffsum = 0; long diffn = 0;
        for (int i = 0; i < n; i++) {
            uint64_t seed = 7000 + (uint64_t)i;
            Building b;
            if (!bld_generate(&b, seed)) { nogen++; bad++;
                printf("seed %llu produced no building at all\n",
                       (unsigned long long)seed);
                continue; }
            /* Same seed, same tower, always -- the whole save format depends
             * on it, because a saved game stores the seed and not the walls. */
            Building b2;
            bool same = bld_generate(&b2, seed);
            if (same) {
                same = b2.nrooms == b.nrooms && b2.ndoors == b.ndoors &&
                       b2.floors == b.floors &&
                       memcmp(b2.rooms, b.rooms, sizeof(Room) * (size_t)b.nrooms) == 0 &&
                       memcmp(b2.doors, b.doors, sizeof(Door) * (size_t)b.ndoors) == 0;
            }
            if (same) bld_free(&b2); else { ndeterm++; }
            int before[BC_COUNT]; memcpy(before, fails, sizeof before);
            Buf why = {0};
            int nf = bld_check(&b, &why, fails);
            if (nf || !same) {
                bad++;
                printf("seed %llu is not a coherent building:\n",
                       (unsigned long long)seed);
                if (!same) printf("  %-42s a second generate gave a different tower\n",
                                  "the same seed gives the same tower");
                if (why.len > 900) { fwrite(why.p, 1, 900, stdout); printf("  ...\n"); }
                else fwrite(why.p, 1, why.len, stdout);
            }
            for (int c = 0; c < BC_COUNT; c++) if (fails[c] != before[c]) hit[c]++;
            buf_free(&why);

            rooms += b.nrooms; doors += b.ndoors; floors += b.floors;
            for (int f = 0; f < b.floors; f++)
                area += (b.fx1[f] - b.fx0[f]) * (double)(b.fy1[f] - b.fy0[f]);
            /* How far apart ARE the two numbers? If a player is choosing
             * between carrying the box and running the cable, the answer has
             * to be worth thinking about. */
            int mdf = bld_find(&b, 0, RM_MDF);
            if (mdf >= 0) {
                double *w = nom_alloc(sizeof(double) * (size_t)b.nrooms);
                double *c = nom_alloc(sizeof(double) * (size_t)b.nrooms);
                bld_walk_all(&b, mdf, w); bld_cable_all(&b, mdf, c);
                for (int rr = 0; rr < b.nrooms; rr++) {
                    if (b.rooms[rr].kind != RM_COMMS) continue;
                    if (w[rr] >= BLD_INF || c[rr] >= BLD_INF) continue;
                    double d = w[rr] - c[rr]; if (d < 0) d = -d;
                    diffsum += d; diffn++;
                }
                nom_free(w); nom_free(c);
            }
            bld_free(&b);
        }
        /* Now break one on purpose, once per check, and demand a complaint. */
        int mut = 0, caught = 0;
        for (int c = 0; c < BC_COUNT; c++) {
            for (int t = 0; t < 8; t++) {
                Building m;
                if (!bld_generate(&m, 9100 + (uint64_t)t)) continue;
                int want = bld_mutate(&m, c);
                if (want < 0) { bld_free(&m); continue; }
                int mf[BC_COUNT] = {0};
                bld_check(&m, NULL, mf);
                mut++;
                if (mf[want]) caught++;
                else printf("A BUILDING BROKEN ON PURPOSE PASSED: nothing complained about"
                            " \"%s\" (seed %d)\n", bld_check_name(want), 9100 + t);
                bld_free(&m);
                break;
            }
        }
        if (caught != mut) bad++;

        printf("\n");
        for (int c = 0; c < BC_COUNT; c++)
            printf("  %-46s %d/%d\n", bld_check_name(c), n - hit[c], n);
        if (nogen)   printf("  %-46s %d/%d\n", "the generator produced a building", n - nogen, n);
        if (ndeterm) printf("  %-46s %d/%d\n", "the same seed gives the same tower", n - ndeterm, n);
        if (floors)
            printf("\naverage tower: %.1f floors, %.0f rooms, %.0f doors, %.0f m2 of plate\n",
                   (double)floors / n, (double)rooms / n, (double)doors / n, area / floors);
        if (diffn)
            printf("Engineering to a comms cupboard: walking and cabling differ by %.1f m on average\n",
                   diffsum / (double)diffn);
        printf("%d/%d deliberately broken buildings were caught by the check that owns them\n",
               caught, mut);
        printf("\n%d/%d buildings coherent\n", n - bad, n);
        return bad ? 1 : 0;
    }

    if (argc > 2 && strcmp(argv[1], "--floorplan") == 0) {
        uint64_t seed = strtoull(argv[2], NULL, 10);
        int floor = argc > 3 ? atoi(argv[3]) : 0;
        Building b;
        if (!bld_generate(&b, seed)) { printf("seed %llu makes no building\n",
                                              (unsigned long long)seed); return 1; }
        Buf o = {0};
        bld_floorplan(&b, floor, &o);
        fwrite(o.p, 1, o.len, stdout);
        buf_free(&o);
        /* The two numbers, on this floor, so the difference is not a claim in
         * a comment. */
        int comms = bld_find(&b, floor, RM_COMMS), mdf = bld_find(&b, 0, RM_MDF);
        if (comms >= 0 && mdf >= 0) {
            double *w = nom_alloc(sizeof(double) * (size_t)b.nrooms);
            double *c = nom_alloc(sizeof(double) * (size_t)b.nrooms);
            bld_walk_all(&b, mdf, w); bld_cable_all(&b, mdf, c);
            printf("\nfrom Engineering on the lowest deck to this deck's comms cupboard:\n");
            printf("  carrying it   %6.1f m   (corridors, doors, stairs or the lift)\n", w[comms]);
            printf("  cabling it    %6.1f m   (tray, comms cupboard, riser)\n", c[comms]);
            nom_free(w); nom_free(c);
        }
        bld_free(&b);
        return 0;
    }

    /* THE NETWORK GATE. Frames on a wire, and every layer above them, checked
     * by building a topology and doing something ordinary to it. The
     * assertions live in netcheck.c because they are long and this file is
     * already long; what matters is that they are here, scored like every
     * other gate, and that they run in milliseconds. */
    if (argc > 1 && strcmp(argv[1], "--netcheck") == 0)
        return net_selfcheck();

    /* THE JOIN. A building with no network in it and a network with no
     * building around it were each finished and each useless. --sitecheck is
     * the gate on the seam between them. */
    if (argc > 1 && strcmp(argv[1], "--sitecheck") == 0)
        return site_selfcheck();

    /* THE LOOP. A clock, tenants who really use the network, load that hurts
     * for reasons the tools can find, and the calibration: the same tower
     * built naively and built properly, played out floor by floor, with the
     * floor each one falls over on printed rather than claimed. */
    if (argc > 1 && strcmp(argv[1], "--loadcheck") == 0) {
        int load_selfcheck(void);
        return load_selfcheck();
    }

    /* AND THE WORLD BREAKING THE MACHINES. A blackout, a disk that has run
     * too long, a cupboard with too much in it -- each one damaging a real
     * disk through core/breaker.c and each one repaired here with the tools
     * the break-fix half already had. See core/eventcheck.c. */
    if (argc > 1 && strcmp(argv[1], "--eventcheck") == 0) {
        int event_selfcheck(void);
        return event_selfcheck();
    }

    /* THE SITE, OVER A PIPE. The 3D view cannot be playtested and blind
     * agents found roughly forty bugs in this project, so ordering,
     * carrying, cabling and configuring all have to be reachable from a
     * terminal. `--sitesh <seed>` reads one operation per line on stdin and
     * is the same set of calls the view will make; `--site <seed>` shows an
     * empty tower and what its tenants are going to ask for. */
    /* THE WHOLE SESSION, OVER A PIPE. `--sitesh` drives site_cmd() and
     * nothing else: it can cable two boxes together without anybody walking
     * anywhere, which is not the game the 3D shell plays. `--towersh` is the
     * game: a person standing in a room, with the same verbs the socket
     * serves, so a script and a playtester exercise identical code. */
    if (argc > 2 && strcmp(argv[1], "--towersh") == 0) {
        Session ses;
        uint64_t seed = strtoull(argv[2], NULL, 10);
        long budget = argc > 3 ? strtol(argv[3], NULL, 10) : SITE_OPENING_MONEY;
        if (!session_new_game(&ses, seed, budget)) {
            printf("seed %llu makes no station with Engineering in it\n",
                   (unsigned long long)seed);
            return 1;
        }
        Buf o = {0};
        session_line(&ses, "look", &o);
        fwrite(o.p, 1, o.len, stdout);
        char line[NOM_ARG_MAX];
        while (fgets(line, sizeof line, stdin)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
            if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;
            buf_clear(&o);
            char p[64];
            session_prompt(&ses, p, sizeof p);
            printf("%s%s\n", p, line);
            session_line(&ses, line, &o);
            fwrite(o.p, 1, o.len, stdout);
            fflush(stdout);
        }
        buf_free(&o);
        session_end(&ses);
        return 0;
    }

    if (argc > 2 && (strcmp(argv[1], "--sitesh") == 0 ||
                     strcmp(argv[1], "--site") == 0)) {
        uint64_t seed = strtoull(argv[2], NULL, 10);
        long budget = argc > 3 ? strtol(argv[3], NULL, 10) : SITE_OPENING_MONEY;
        Building b;
        if (!bld_generate(&b, seed)) {
            printf("seed %llu makes no building\n", (unsigned long long)seed);
            return 1;
        }
        Site s;
        if (!site_new(&s, &b, seed, budget)) {
            printf("seed %llu has nowhere for the uplink to land\n",
                   (unsigned long long)seed);
            bld_free(&b);
            return 1;
        }
        Buf o = {0};
        if (strcmp(argv[1], "--site") == 0) {
            buf_printf(&o, "building %llu: %d floors, %d rooms, %d tenancies\n\n",
                       (unsigned long long)seed, b.floors, b.nrooms, b.ntenants);
            site_dump(&s, &o);
            buf_putc(&o, '\n');
            site_dump_demand(&s, &o);
            fwrite(o.p, 1, o.len, stdout);
        } else {
            char line[512];
            while (fgets(line, sizeof line, stdin)) {
                size_t n = strlen(line);
                while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
                if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;
                buf_clear(&o);
                site_cmd(&s, line, &o);
                fwrite(o.p, 1, o.len, stdout);
                fflush(stdout);
            }
        }
        buf_free(&o);
        site_free(&s);
        bld_free(&b);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--health") == 0) {
        /* A PRISTINE machine must be healthy: it boots, and every service it
         * started is still running. Without this, a service could sit in a
         * respawn loop on every machine in the game and the only thing that
         * would notice is a playtester. One did. */
        int n = argc > 2 ? atoi(argv[2]) : 20, bad = 0;
        for (int i = 0; i < n; i++) {
            Machine m;
            machine_install(&m, (uint64_t)(3000 + i));
            machine_boot(&m);
            const char *c = m.boot.console.p ? m.boot.console.p : "";
            bool up = m.boot.running;
            Buf sick = {0};
            bool died = kernel_health(&m, &sick) > 0
                     || strstr(c, "died --") || strstr(c, "respawning too fast")
                     || strstr(c, "refusing to start");
            buf_free(&sick);
            /* THE MENU MUST BE THE FILE. zbl drew three hardcoded lines and
             * then announced "booting entry 0 of 1" underneath them, so the
             * box advertised a rescue entry and a single-user entry that the
             * config did not hold and that nothing could select. Count the
             * rows between the rules and make them agree with what zbl says
             * it is choosing from. */
            bool menu_lies = false;
            {
                const char *bar = strstr(c, "  +---");
                const char *of  = strstr(c, "zbl: booting entry ");
                if (bar && of) {
                    int rows = 0;
                    for (const char *p = strchr(bar, '\n'); p; p = strchr(p + 1, '\n')) {
                        if (strncmp(p + 1, "  +---", 6) == 0) break;
                        if (strncmp(p + 1, "  |", 3) == 0) rows++;
                        else break;
                    }
                    int nent = 0;
                    if (sscanf(of, "zbl: booting entry %*d of %d", &nent) == 1)
                        menu_lies = rows != nent;
                    if (menu_lies)
                        printf("MENU seed %d: the box has %d row(s) and zbl says "
                               "it is choosing from %d\n", 3000 + i, rows, nent);
                }
            }
            if (!up || died || menu_lies) {
                bad++;
                printf("UNHEALTHY seed %d%s%s%s\n", 3000 + i,
                       up ? "" : " (did not boot)", died ? " (a service died)" : "",
                       menu_lies ? " (the boot menu disagrees with the config)" : "");
                if (bad == 1) fwrite(c, 1, m.boot.console.len, stdout);
            }
            machine_free(&m);
        }
        printf("\n%d/%d pristine machines boot with every service healthy\n",
               n - bad, n);
        return bad ? 1 : 0;
    }

    if (argc > 2 && strcmp(argv[1], "--survey") == 0) {
        int n = atoi(argv[2]);
        int nf = argc > 3 ? atoi(argv[3]) : 1;
        int stage[BOOT_STAGE_COUNT] = {0}, made = 0;
        /* distinct failure lines, to prove the content is not a lookup */
        static char seen[4096][160]; int nseen = 0;
        for (int i = 0; i < n; i++) {
            Machine m; char what[512];
            machine_install(&m, (uint64_t)(1000 + i));
            if (machine_break(&m, (uint64_t)(1000 + i), nf, what, sizeof what)) {
                made++;
                stage[m.boot.failed_at]++;
                /* A MACHINE THAT BOOTS STILL HAS SOMETHING TO SAY.
                 *
                 * This counted distinct `boot.reason` strings, and a machine
                 * that comes UP has an empty one -- it did not fail at a
                 * stage, it reached the target with something sick on it. So
                 * every UP-but-sick ticket collapsed into one bucket, and as
                 * that class grew from nothing to thirty of a hundred and
                 * fifty, the number this gate reports sat perfectly still.
                 *
                 * An agent that had just doubled the fault set reported "57
                 * before, 57 after, and I did not touch the counter to make
                 * it move" -- which was the honest answer and also the
                 * diagnosis. The metric could not see a whole class of
                 * ticket, so it would have gone on reporting no progress
                 * while the game got better. What the customer complains
                 * about on a booted machine is the health complaint; count
                 * that. */
                char msg[160];
                if (m.boot.reason[0]) {
                    snprintf(msg, sizeof msg, "%s", m.boot.reason);
                } else {
                    Buf sick = {0};
                    kernel_health(&m, &sick);
                    if (!sick.len) machine_outstanding(&m, &sick);
                    /* THE FIRST LINE IS A HEADING, NOT A COMPLAINT.
                     *
                     * Taking it verbatim moved this count from 57 to 58,
                     * because both health and outstanding open with a fixed
                     * sentence ending in a colon -- so thirty tickets went
                     * from sharing an empty boot.reason to sharing one
                     * heading. Same bucket, new label, and I nearly reported
                     * it as progress. Skip the headings and take the first
                     * line that names something. */
                    size_t at = 0, n1 = 0;
                    msg[0] = 0;
                    while (at < sick.len) {
                        size_t e = at;
                        while (e < sick.len && sick.p[e] != '\n') e++;
                        size_t s2 = at;
                        while (s2 < e && sick.p[s2] == ' ') s2++;
                        size_t len = e - s2;
                        bool heading = len == 0 || sick.p[e - 1] == ':';
                        if (!heading) {
                            n1 = len > sizeof msg - 1 ? sizeof msg - 1 : len;
                            memcpy(msg, sick.p + s2, n1);
                            msg[n1] = 0;
                            break;
                        }
                        at = e + 1;
                    }
                    if (!n1) snprintf(msg, sizeof msg, "(up, and nothing said why)");
                    buf_free(&sick);
                }
                bool dup = false;
                for (int k = 0; k < nseen; k++)
                    if (strcmp(seen[k], msg) == 0) dup = true;
                if (!dup && nseen < 4096)
                    snprintf(seen[nseen++], 160, "%s", msg);
                if (i < 12)
                    printf("seed %-5d %-10s %s\n           %s\n",
                           1000 + i, boot_stage_name(m.boot.failed_at), what,
                           m.boot.reason);
            }
            machine_free(&m);
        }
        printf("\n%d/%d seeds produced a ticket\n", made, n);
        printf("%d DISTINCT failure messages\n", nseen);
        printf("where they fail:\n");
        for (int s = 0; s < BOOT_STAGE_COUNT; s++)
            if (stage[s]) printf("  %-10s %d\n", boot_stage_name((BootStage)s), stage[s]);
        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "--solve") == 0) {
        int n = atoi(argv[2]), visible = 0, fixed = 0, made = 0, handed = 0;
        for (int i = 0; i < n; i++) {
            Machine m; char what[512];
            machine_install(&m, (uint64_t)(5000 + i));
            if (!machine_break(&m, (uint64_t)(5000 + i), 1, what, sizeof what)) {
                machine_free(&m); continue;
            }
            made++;
            Buf v = {0};
            pkg_verify(&m, NULL, &v);
            bool sees = !(v.len >= 15 && memcmp(v.p, "all files match", 15) == 0);
            if (sees) visible++;

            /* The repair ladder a competent player would work through, run as
             * REAL COMMANDS on the machine. This gate therefore proves the
             * guest tools actually work, not merely that the host could patch
             * the disk behind their back.
             *
             * Note what is NOT here: there is no step that knows which fault
             * was injected. Every step is something you would try anyway. */
            Buf o = {0};
            machine_boot_rescue(&m);
            /* Nothing will mount a dirty filesystem, so this has to come
             * first -- which is exactly the order a real repair happens in. */
            kernel_run(&m, "fsck /dev/sda1", &o);
            kernel_run(&m, "mount /dev/sda1 /mnt", &o);
            kernel_run(&m, "for i in dev sys proc; do mount /$i /mnt/$i; done", &o);

            /* SPACE BEFORE REPAIR, which is the order the previous
             * administrator's notes give twice and for this exact reason: a
             * reinstall onto a FULL disk truncates the file it is restoring
             * and then cannot write it back, so the repair itself destroys
             * /etc/passwd and the machine comes up with no account for root.
             * One seed in sixty did precisely that, and the ladder had been
             * freeing space at the END, where it is too late to help. */
            free_scratch(&m, "/mnt", &o);

            /* Repair from OUTSIDE first. If the disk's libc is the wrong
             * version, nothing on it will run at all -- so chrooting in and
             * using its tools is not an option, and this is the only way
             * back. Same reason rpm and dpkg have --root. */
            for (int k = 0; k < m.npkg; k++) {
                char cmd[160];
                /* --force, because a ladder is a blunt instrument by
                 * definition. A PERSON should not use it without looking:
                 * without the flag, reinstall now keeps locally modified
                 * config, which is the whole point of the flag existing. */
                snprintf(cmd, sizeof cmd, "pkg --root /mnt reinstall --force %s",
                         m.pkg[k]->name);
                kernel_run(&m, cmd, &o);
            }
            /* Make sure every directory a package installs into can actually
             * be entered. A file reported UNREADABLE whose content is right is
             * a permissions problem one level up, and no amount of
             * reinstalling fixes a parent directory -- no manifest lists one.
             *
             * This step does not know which fault was injected: the directory
             * list is DERIVED from the package database, which is the same
             * thing a person would do after seeing UNREADABLE next to a file
             * they can see is fine. */
            for (int k = 0; k < m.npkg; k++) {
                for (int f = 0; f < m.pkg[k]->nfiles; f++) {
                    const char *fp = m.pkg[k]->file[f].path;
                    /* EVERY DIRECTORY ON THE WAY, not just the last one. A
                     * mode that bars the way to /var bars the way to
                     * everything under it, and chmodding only the immediate
                     * parent of each file left the whole tree unreachable
                     * with the parent looking perfect. A person reading
                     * UNREADABLE next to a file walks UP until the listing
                     * works; this does the same thing. */
                    for (const char *slash = strchr(fp + 1, '/'); slash;
                         slash = strchr(slash + 1, '/')) {
                        char dir[NOM_PATH_MAX], cmd[NOM_PATH_MAX + 24];
                        size_t dl = (size_t)(slash - fp);
                        if (dl >= sizeof dir) break;
                        memcpy(dir, fp, dl);
                        dir[dl] = 0;
                        snprintf(cmd, sizeof cmd, "chmod 755 /mnt%s", dir);
                        kernel_run(&m, cmd, &o);
                    }
                }
            }

            kernel_run(&m, "chroot /mnt", &o);

            /* a unit no package owns was never installed: remove it */
            for (int k = 0; k < m.npkg; k++) { }
            {
                VNode *d = vfs_resolve(&m.disk, "/etc/services.d", NULL);
                for (VNode *kid = d ? d->child : NULL; kid; ) {
                    VNode *next = kid->next;
                    char full[NOM_PATH_MAX];
                    snprintf(full, sizeof full, "/etc/services.d/%s", kid->name);
                    if (!pkg_owns(&m, full)) {
                        char cmd[NOM_PATH_MAX + 8];
                        snprintf(cmd, sizeof cmd, "rm %s", full);
                        kernel_run(&m, cmd, &o);
                    }
                    kid = next;
                }
            }
            for (int k = 0; k < m.npkg; k++) {
                char cmd[128];
                snprintf(cmd, sizeof cmd, "pkg reinstall --force %s", m.pkg[k]->name);
                kernel_run(&m, cmd, &o);
            }
            /* And again from inside, for anything the repair itself wrote. */
            free_scratch(&m, "", &o);
            kernel_run(&m, "mkinitrd", &o);
            kernel_run(&m, "zbl-mkconfig", &o);
            kernel_run(&m, "zbl-install /dev/sda", &o);

            m.on_rescue = false;
            m.nmount = 0;
            machine_boot(&m);
            Buf sick = {0};
            int dead = kernel_health(&m, &sick);
            /* AND CAN IT BE HANDED BACK. A repaired machine that the game
             * will not let you sign off is a job that never ends, which is
             * exactly what a playtester found: they repaired seven machines
             * and no ticket ever closed. The ladder proves the tools can fix
             * it; this proves the game agrees they did. */
            if (m.boot.running && dead == 0) {
                fixed++;
                Buf hb = {0};
                if (machine_handback(&m, &hb)) handed++;
                else printf("NOT HANDED BACK seed %d: %s\n%.*s\n",
                            5000 + i, what, (int)hb.len, hb.p);
                buf_free(&hb);
            }
            else if (!m.boot.running)
                printf("UNFIXABLE seed %d: %s\n           %s\n",
                       5000 + i, what, m.boot.reason);
            else
                printf("STILL SICK seed %d: %s\n%.*s",
                       5000 + i, what, (int)sick.len, sick.p);
            buf_free(&sick);
            buf_free(&v); buf_free(&o); machine_free(&m);
        }
        printf("\n%d tickets: %d visible to pkg verify, %d repaired by the tools\n",
               made, visible, fixed);
        printf("%d of those handed back and closed\n", handed);
        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "--peel") == 0) {
        /* Multi-fault tickets are only worth having if they PEEL: fix the
         * thing the console blames and a different failure is waiting
         * underneath. This walks a ticket the way a competent player would --
         * verify, repair the first offender, boot again -- and checks that
         * each round lands somewhere new. */
        int n = atoi(argv[2]), nf = argc > 3 ? atoi(argv[3]) : 3;
        int converged = 0, layered = 0;
        for (int i = 0; i < n; i++) {
            Machine m; char what[512];
            machine_install(&m, (uint64_t)(9000 + i));
            if (!machine_break(&m, (uint64_t)(9000 + i), nf, what, sizeof what)) {
                machine_free(&m); continue;
            }
            BootStage prev = m.boot.failed_at;
            int rounds = 0, distinct = 1;
            for (; rounds < 12 && !m.boot.running; rounds++) {
                Buf v = {0};
                pkg_verify(&m, NULL, &v);
                char path[NOM_PATH_MAX] = "";
                if (v.len && memcmp(v.p, "all files match", 15) != 0)
                    sscanf(v.p, "%255s", path);
                if (!path[0]) break;
                const Package *pk = pkg_owns(&m, path);
                buf_free(&v);
                if (!pk) break;
                Buf o = {0};
                pkg_reinstall(&m, pk->name, &o);
                buf_free(&o);
                machine_boot(&m);
                if (!m.boot.running && m.boot.failed_at != prev) distinct++;
                prev = m.boot.failed_at;
            }
            if (m.boot.running) converged++;
            if (distinct > 1) layered++;
            if (i < 6) printf("seed %d: %d repairs, failed at %d different stages\n",
                              9000 + i, rounds, distinct);
            machine_free(&m);
        }
        printf("\n%d/%d tickets converged to a booting machine\n", converged, n);
        printf("%d/%d moved the failure to a new stage at least once\n", layered, n);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--serve") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : 7777;
        return bench_serve(port, true, argc > 3 ? strtoull(argv[3], NULL, 10) : 4800);
    }

    /* --desk: THE WORKFLOW, as it actually is.
     *
     * You are not sitting at the broken machine. You are at your own
     * workstation -- a healthy install of the same system, which is what makes
     * "compare it against mine" a real move -- and the customer's box is
     * reachable only through its service processor, the way iDRAC or iLO is.
     *
     * So this shell runs on YOUR machine. `rcon connect <address>` attaches to
     * theirs, `rcon power cycle` restarts it and shows you the console, and
     * `rcon media insert` puts the rescue medium in its virtual drive. There
     * is no command here that reaches inside their disk without going through
     * the service processor first, because there is no such thing on a real
     * support desk either.
     */
    /* --askcheck: is the customer honest, and does she constrain you?
     *
     * This replaces --toolcheck and --jsoncheck, and it is worth saying what
     * those were FOR: both existed only to police a language model. One
     * measured whether it could work out which action a sentence was asking
     * for; the other whether the runbook author was inventing commands. Both
     * took minutes of model time to answer questions that only arise because
     * there was a model.
     *
     * There is no model now, so the questions have changed. What can go wrong
     * with a deterministic customer is different and much more checkable:
     *   - she offers something she cannot actually do
     *   - she does something other than exactly what was dictated
     *   - she runs a truncated command instead of refusing the whole one
     *   - she reads back a line the machine never printed
     *   - she says a technical word nobody read to her
     *   - the screen never scrolls off, so the round trip costs nothing
     * Every one of those is mechanical, and this answers all of them across a
     * spread of ticket states in milliseconds.
     */
    /* --mancheck: does every manual page describe THIS machine? Runs the
     * command examples out of the pages on a real booted machine. See
     * core/mancheck.c for what it will and will not execute. */
    if (argc > 1 && strcmp(argv[1], "--mancheck") == 0) {
        extern int man_check(void);
        return man_check();
    }

    if (argc > 1 && strcmp(argv[1], "--askcheck") == 0) {
        int pass = 0, total = 0, shown = 0;
        int misreads = 0;
        #define AC(cond, ...) do { total++; if (cond) pass++; else { \
            printf("NO  "); printf(__VA_ARGS__); printf("\n"); shown++; } } while (0)

        /* WORDS SHE COULD ONLY KNOW IF SOMEBODY READ THEM TO HER. She is
         * allowed to repeat a command back -- that is what "I have typed
         * ..." is -- and allowed to read the screen out. Everything else she
         * says is her own, and her own vocabulary has none of these in it. */
        static const char *JARGON[] = {
            "kernel","initrd","package","filesystem","symlink","uuid",
            "daemon","libc","inode","partition","bootloader","chroot",
            "mount","fsck","repository","corrupt","permission","config",
            "service","binary","syntax","directory","stderr","reinstall",
            NULL
        };

        for (int si = 0; si < 6; si++) {
            uint64_t seed = (uint64_t)(7100 + si * 13);
            for (int mode = 0; mode < 5; mode++) {
                Machine m;
                Buf junk = {0};
                ac_state(&m, seed, mode, &junk);
                buf_free(&junk);

                /* WHAT IS ON OFFER. Parse it the way a front end has to. */
                Buf lst = {0};
                customer_options(&m, &lst);
                int ids[64], nid = 0;
                char labels[64][96];
                for (size_t i = 0; i < lst.len && nid < 64; ) {
                    size_t e = i;
                    while (e < lst.len && lst.p[e] != '\n') e++;
                    char line[160];
                    size_t n = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
                    memcpy(line, lst.p + i, n); line[n] = 0;
                    int id = 0; char lab[96] = "";
                    if (sscanf(line, "  [%d] %95[^\n]", &id, lab) == 2) {
                        ids[nid] = id;
                        snprintf(labels[nid], sizeof labels[0], "%s", lab);
                        nid++;
                    }
                    i = e < lst.len ? e + 1 : e;
                }
                AC(nid >= 3, "seed %llu mode %d: only %d options offered -- a "
                             "list with no way forward", (unsigned long long)seed,
                             mode, nid);
                buf_free(&lst);

                /* AND NOTHING ELSE IS. An id that is not on the list must
                 * be refused rather than half-done: this is the check that
                 * stops a front end driving her into a state she cannot be
                 * in. Done first, while the state is the one just described. */
                for (int id = 1; id < 30; id++) {
                    bool on = false;
                    for (int k = 0; k < nid; k++) if (ids[k] == id) on = true;
                    if (on) continue;
                    Buf o = {0};
                    size_t before = m.boot.console.len;
                    customer_choose(&m, id, "echo probe", &o);
                    /* A refusal changes nothing, and it is HER refusing --
                     * a spoken line about what she can see, not a menu error
                     * message. The one exception is a number nobody offered
                     * at all, which is a front end bug and says so. */
                    AC(o.p && m.boot.console.len == before &&
                       (memcmp(o.p, "  \"", 3) == 0 ||
                        strstr(o.p, "there is no option")),
                       "seed %llu mode %d: option %d is not offered and "
                       "answered out of character or acted anyway",
                       (unsigned long long)seed, mode, id);
                    AC(!(o.p && strstr(o.p, "cannot do that")),
                       "seed %llu mode %d: option %d refused in the game's "
                       "voice instead of hers",
                       (unsigned long long)seed, mode, id);
                    buf_free(&o);
                }

                /* EVERY OFFERED OPTION IS ACTIONABLE.
                 *
                 * Walked on one machine, re-asking what is on offer before
                 * each one, because that is how a call actually goes: the
                 * disc goes in, the box comes up, and what she can do next is
                 * different. An option that was offered and then refused is
                 * the failure this is looking for. */
                for (int id = 1; id < 30; id++) {
                    char lab[160] = "";
                    Buf l = {0};
                    customer_options(&m, &l);
                    for (size_t i = 0; i < l.len; ) {
                        size_t e = i;
                        while (e < l.len && l.p[e] != '\n') e++;
                        char line[200];
                        size_t n = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
                        memcpy(line, l.p + i, n); line[n] = 0;
                        int got = 0; char t2[160] = "";
                        if (sscanf(line, "  [%d] %159[^\n]", &got, t2) == 2 &&
                            got == id)
                            snprintf(lab, sizeof lab, "%s", t2);
                        i = e < l.len ? e + 1 : e;
                    }
                    buf_free(&l);
                    if (!lab[0]) continue;
                    Buf o = {0};
                    customer_choose(&m, id, strstr(lab, "run:") ? "echo probe" : NULL,
                                    &o);
                    AC(o.len > 0 && !strstr(o.p ? o.p : "", "cannot do that"),
                       "seed %llu mode %d: option %d (%s) was offered and "
                       "refused", (unsigned long long)seed, mode, id, lab);
                    AC(ac_quotes_real(&m, o.p, o.len, &misreads),
                       "seed %llu mode %d: option %d read back a line the "
                       "machine never printed", (unsigned long long)seed, mode,
                       id);
                    AC(ac_no_jargon(o.p, o.len, JARGON),
                       "seed %llu mode %d: option %d put a technical word in "
                       "her mouth", (unsigned long long)seed, mode, id);
                    buf_free(&o);
                }
                machine_free(&m);
            }
        }

        /* THE DICTATED COMMAND, which is the whole air-gapped puzzle.
         *
         * Exactly what was said reaches the shell, or nothing does. The
         * middle case -- a shortened version of what was said, run and
         * reported as though it were the real thing -- is the bug this pins
         * down, because it has happened once and it is invisible from the
         * player's side. */
        for (int si = 0; si < 6; si++) {
            uint64_t seed = (uint64_t)(7100 + si * 13);
            Machine m;
            Buf junk = {0};
            ac_state(&m, seed, 3, &junk);      /* rescue shell, disk mounted */
            buf_free(&junk);
            int run = ac_find(&m, "\"can I have you run:\"  <command>");
            AC(run == 2, "the dictate option is not on the list in a rescue "
                         "shell (seed %llu)", (unsigned long long)seed);
            if (run > 0) {
                Buf o = {0};
                customer_choose(&m, run, "echo nominal-probe", &o);
                char want[64];
                snprintf(want, sizeof want, "user@%s:~# echo nominal-probe", m.id);
                AC(m.boot.console.p && strstr(m.boot.console.p, want),
                   "seed %llu: the machine did not see exactly what was "
                   "dictated", (unsigned long long)seed);
                AC(o.p && strstr(o.p, "| nominal-probe"),
                   "seed %llu: she did not read the answer back",
                   (unsigned long long)seed);
                buf_free(&o);

                /* THE CANONICAL AIR-GAPPED REPAIR MUST FIT. Eighty-odd
                 * characters of sed with two uuids in it: a typing limit
                 * below this does not make the ticket harder, it makes it
                 * impossible. */
                o = (Buf){0};
                size_t was = m.boot.console.len;
                customer_choose(&m, run,
                    "sed -i s/c603-2d03-bafe-e442/8f41-2c07-a19d-5be3/ "
                    "/mnt/boot/zbl/zbl.cfg", &o);
                AC(!(o.p && strstr(o.p, "more than I can type")),
                   "seed %llu: she will not type the canonical repair (%d "
                   "character limit)", (unsigned long long)seed,
                   0);
                (void)was;
                buf_free(&o);

                /* AND A LINE NOBODY COULD DICTATE OVER A PHONE IS REFUSED
                 * WHOLE. Not truncated, not attempted: nothing runs. */
                char huge[512];
                memset(huge, 'x', sizeof huge - 1);
                huge[sizeof huge - 1] = 0;
                memcpy(huge, "echo ", 5);
                o = (Buf){0};
                size_t before = m.boot.console.len;
                customer_choose(&m, run, huge, &o);
                AC(o.p && strstr(o.p, "more than I can type"),
                   "seed %llu: a 511-character command was not pushed back on",
                   (unsigned long long)seed);
                AC(m.boot.console.len == before,
                   "seed %llu: something was run from a command she said she "
                   "could not type", (unsigned long long)seed);
                buf_free(&o);
            }
            machine_free(&m);
        }

        /* SCROLL-OFF, which is the favourite line in the game and is now a
         * rule: she can see the bottom of the screen and no more, and paging
         * up costs a round trip each time. */
        for (int si = 0; si < 6; si++) {
            uint64_t seed = (uint64_t)(7100 + si * 13);
            Machine m;
            Buf junk = {0};
            ac_state(&m, seed, 3, &junk);
            buf_free(&junk);
            Buf o = {0};
            customer_choose(&m, 2, "ls -l /mnt/etc", &o);
            AC(o.p && strstr(o.p, "scrolled off"),
               "seed %llu: a screenful of output did not scroll off anything",
               (unsigned long long)seed);
            int more = ac_find(&m, "yes -- scroll back up and read me what came before");
            AC(more > 0, "seed %llu: nothing on offer to read the rest of it",
               (unsigned long long)seed);
            buf_free(&o);
            if (more > 0) {
                /* Paging up must reach the top and say so, and every page has
                 * to be real. */
                int guard = 0;
                bool top = false;
                while (ac_find(&m, "yes -- scroll back up and read me what came before") > 0
                       && guard++ < 60) {
                    Buf p = {0};
                    customer_choose(&m, more, NULL, &p);
                    AC(ac_quotes_real(&m, p.p, p.len, &misreads),
                       "seed %llu: a page she scrolled back to was not on the "
                       "screen", (unsigned long long)seed);
                    if (p.p && strstr(p.p, "top of it")) top = true;
                    buf_free(&p);
                }
                AC(guard < 60, "seed %llu: scrolling back never reached the top",
                   (unsigned long long)seed);
                (void)top;
            }

            /* A SECOND LOOK IS EXACT. She misreads a character now and then,
             * and the cure is asking her to read it again -- so the second
             * reading must be the machine's own bytes, every time. */
            Buf r = {0};
            customer_choose(&m, 2, "dmesg", &r);
            buf_free(&r);
            int again = ac_find(&m, "ask her to read that last bit again, carefully");
            AC(again > 0, "seed %llu: no way to ask her to read it again",
               (unsigned long long)seed);
            if (again > 0) {
                Buf p = {0};
                customer_choose(&m, again, NULL, &p);
                int nomis = 0;
                AC(ac_quotes_exact(&m, p.p, p.len),
                   "seed %llu: she misread something on a second look",
                   (unsigned long long)seed);
                (void)nomis;
                buf_free(&p);
            }
            machine_free(&m);
        }

        /* NOWHERE TO TYPE IS AN ANSWER, NOT A MISSING OPTION.
         *
         * Dictating a command at a machine that stopped halfway through
         * starting is the first thing a player tries, and taking the option
         * off the list does not stop them trying -- it means the game says no
         * on her behalf, while the ticket header is still telling them to use
         * it. She is standing in front of the thing: let her say what she can
         * see, and run nothing. */
        for (int si = 0; si < 6; si++) {
            uint64_t seed = (uint64_t)(7100 + si * 13);
            Machine m;
            Buf o = {0};
            ac_state(&m, seed, 0, &o);
            buf_free(&o);
            int run = ac_find(&m, "\"can I have you run:\"  <command>");
            AC(run > 0, "seed %llu: the dictate option is not on the list, "
                        "and the ticket header advertises it",
               (unsigned long long)seed);
            if (run > 0 && !m.boot.running) {
                o = (Buf){0};
                size_t before = m.boot.console.len;
                customer_choose(&m, run, "dmesg", &o);
                AC(o.p && memcmp(o.p, "  \"", 3) == 0 &&
                   strstr(o.p, "nowhere to type"),
                   "seed %llu: she did not say why there is nowhere to type "
                   "it", (unsigned long long)seed);
                AC(m.boot.console.len == before,
                   "seed %llu: something ran on a machine with no prompt",
                   (unsigned long long)seed);
                buf_free(&o);
            }
            machine_free(&m);
        }

        /* A REBOOT TAKES THE OLD SCREEN WITH IT. Paging back up through a
         * console the machine has since replaced would have her reading out
         * lines that were printed before it was restarted, as though they
         * were in front of her now. */
        {
            Machine m;
            Buf o = {0};
            ac_state(&m, 7113, 3, &o);
            buf_free(&o);
            o = (Buf){0};
            customer_choose(&m, ac_find(&m, "\"can I have you run:\"  <command>"),
                            "ls -l /mnt/etc", &o);
            buf_free(&o);
            AC(ac_find(&m, "yes -- scroll back up and read me what came before") > 0,
               "a long listing left nothing to scroll back through");
            o = (Buf){0};
            customer_choose(&m, ac_find(&m, "ask her to turn it off and on again"),
                            NULL, &o);
            buf_free(&o);
            AC(ac_find(&m, "yes -- scroll back up and read me what came before") < 0 &&
               ac_find(&m, "ask her to read that last bit again, carefully") < 0,
               "she is still offering to read back a screen the machine "
               "replaced when it restarted");
            machine_free(&m);
        }

        /* SAME SEED, SAME CALL. The whole reason for taking the model out. */
        {
            Buf a = {0}, b = {0};
            for (int pass2 = 0; pass2 < 2; pass2++) {
                Buf *o = pass2 ? &b : &a;
                Machine m;
                ac_state(&m, 7113, 0, o);
                static const int SCRIPT[] = { 5, 6, 19, 20, 1, 17, 14, 2, 3, 4, 21 };
                for (size_t i = 0; i < sizeof SCRIPT / sizeof SCRIPT[0]; i++) {
                    customer_options(&m, o);
                    customer_choose(&m, SCRIPT[i],
                                    SCRIPT[i] == 2 ? "dmesg" : NULL, o);
                }
                machine_free(&m);
            }
            AC(a.len == b.len && a.len && memcmp(a.p, b.p, a.len) == 0,
               "the same seed produced two different calls");
            buf_free(&a); buf_free(&b);
        }

        /* AND THE MISREADING ACTUALLY HAPPENS. A rule nobody ever meets is a
         * comment, not a mechanic -- and this counter is the only thing that
         * would notice if a change quietly switched it off. */
        AC(misreads > 0, "she never once misread a character in the whole run");

        printf("\n%d/%d askcheck assertions pass  (%d misreads seen)\n",
               pass, total, misreads);
        if (!shown) printf("every option offered was actionable, every "
                           "dictated command ran whole or not at all\n");
        #undef AC
        return pass == total ? 0 : 1;
    }

    if (argc > 1 && strcmp(argv[1], "--desk") == 0) {
        uint64_t seed = argc > 2 ? strtoull(argv[2], NULL, 10) : 4823;

        static Machine cust;
        char what[512] = "";
        machine_break(&cust, seed, argc > 3 ? atoi(argv[3]) : 1, what, sizeof what);

        /* ONE TICKET IN FIVE IS AIR-GAPPED: a secure site, a factory floor,
         * a box that was never on a network. You cannot reach it at all, and
         * the only terminal you have is the person standing in front of it.
         * Every command costs a round trip through somebody who does not know
         * what any of it means, which is a completely different kind of hard
         * from anything else in the game. */
        cust.airgapped = machine_airgapped(seed);

        static Machine desk;
        machine_install(&desk, 1);          /* healthy, always */
        machine_boot(&desk);
        desk.peer = &cust;
        snprintf(desk.peer_addr, sizeof desk.peer_addr, "10.0.2.%d",
                 60 + (int)(seed % 40));

        printf("%s", desk.boot.console.p ? desk.boot.console.p : "");
        printf("\n--- ticket %llu ---\n", (unsigned long long)(seed % 10000));
        /* The same blurb the socket prints, and for the same reason: "not
         * coming up" was hard-coded and was wrong on every ticket where the
         * machine came up. See new_ticket() in serve.c. */
        {
            Buf sick = {0};
            int dead = kernel_health(&cust, &sick);
            buf_free(&sick);
            Buf left = {0};
            int rest = machine_outstanding(&cust, &left) ? 1 : 0;
            buf_free(&left);
            const char *say;
            if (!cust.boot.running) say = "Their machine is not coming up.";
            else if (dead || rest)  say = "Their machine comes up, and something "
                                          "on it is not working.";
            else                    say = "They say it seems fine now, and they "
                                          "want somebody to be sure.";
            printf("  %s is on the line. %s\n", customer_name(&cust), say);
        }
        if (cust.airgapped) {
            printf("  it is not on any network -- there is no address to give you.\n");
            printf("  you are at YOUR workstation, and your only terminal on their\n");
            printf("  machine is %s. `ask 2 <command>` and they will read back\n",
                   customer_name(&cust));
            printf("  whatever they can see of the answer.\n");
        } else {
            printf("  they read you the address on the sticker: %s\n", desk.peer_addr);
            printf("  you are at YOUR workstation. `rcon connect %s` to reach theirs.\n",
                   desk.peer_addr);
        }
        printf("  `ask` for what you can say to them; `ask <n>` to say it.\n\n");

        char line[NOM_ARG_MAX];
        while (read_line(line, sizeof line)) {
            if (!line[0]) continue;
            if (strcmp(line, "quit") == 0) break;
            /* The same hand-back the socket server offers, through the same
             * function. Two front ends that can disagree about whether a job
             * is finished are two different games. */
            if (strcmp(line, "done") == 0 || strcmp(line, "handback") == 0) {
                Buf hb = {0};
                machine_handback(&cust, &hb);
                fwrite(hb.p, 1, hb.len, stdout);
                buf_free(&hb);
                printf("you@desk# ");
                fflush(stdout);
                continue;
            }
            if (strncmp(line, "ask", 3) == 0 && (line[3] == ' ' || !line[3])) {
                const char *a = line[3] ? line + 4 : "";
                while (*a == ' ') a++;
                Buf o = {0};
                if (*a >= '0' && *a <= '9') {
                    int idx = atoi(a);
                    while (*a >= '0' && *a <= '9') a++;
                    while (*a == ' ') a++;
                    customer_choose(&cust, idx, a, &o);
                } else {
                    if (*a) printf("she is on the phone, not on chat. pick "
                                   "something to say:\n");
                    customer_options(&cust, &o);
                    buf_puts(&o, "  `ask <n>` to say one. option 2 takes a "
                                 "command: `ask 2 dmesg -f error`\n");
                }
                fwrite(o.p, 1, o.len, stdout);
                buf_free(&o);
                printf("you@desk# ");
                fflush(stdout);
                continue;
            }
            Buf o = {0};
            kernel_run(&desk, line, &o);
            fwrite(o.p, 1, o.len, stdout);
            buf_free(&o);
            printf("you@desk# ");
            fflush(stdout);
        }
        machine_free(&desk);
        machine_free(&cust);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--sh") == 0) {
        /* An interactive session against one machine: the whole game, with no
         * GUI anywhere near it. Each line is executed by /bin/sh ON the
         * machine, so this shell and the desktop's terminal cannot diverge. */
        uint64_t sd = argc > 2 ? strtoull(argv[2], NULL, 10) : 4823;
        int nf = argc > 3 ? atoi(argv[3]) : 1;
        Machine m; char what[512] = "";
        machine_install(&m, sd);
        /* A REBOOT HERE DESTROYS A WHOLE CLASS OF TICKET.
         *
         * machine_break has already booted the machine -- that is how it
         * knows the ticket is a ticket -- and its console is right there.
         * Booting a second time threw that away and, worse, silently repaired
         * every fault whose whole nature is that a running process is out of
         * step with a file: the daemon simply read the config again on the way
         * up. So `--sh` could never show a stale-configuration ticket, which
         * is the one the previous administrator's notes spend a whole item on.
         * Only boot when there is no ticket to look at. */
        if (nf > 0) machine_break(&m, sd, nf, what, sizeof what);
        else        machine_boot(&m);
        fwrite(m.boot.console.p, 1, m.boot.console.len, stdout);
        {
            Buf sick = {0};
            int dead = kernel_health(&m, &sick);
            if (m.boot.running && dead) {
                printf("\n[UP at target, but %d service(s) are not right]\n", dead);
                fwrite(sick.p, 1, sick.len, stdout);
            } else {
                printf("\n[%s at %s]\n", m.boot.running ? "UP" : "DOWN",
                       boot_stage_name(m.boot.failed_at));
            }
            buf_free(&sick);
        }
        if (getenv("NOM_SPOIL")) printf("[break: %s]\n", what);

        /* One long-lived process owns the session, so cd and bind persist. */
        char line[NOM_ARG_MAX];
        for (;;) {
            printf("rescue# ");
            fflush(stdout);
            if (!read_line(line, sizeof line)) break;
            if (!line[0]) continue;
            /* `exit` belongs to the shell: in a chroot it leaves the chroot.
             * Only `quit` hangs up. */
            if (strcmp(line, "quit") == 0) break;
            if (strcmp(line, "help") == 0) {
                printf("boot     boot the customer's disk and watch the console\n"
                       "rescue   boot the rescue medium (always works)\n"
                       "exit     leave\n"
                       "anything else runs on the machine; try `help` there too\n");
                continue;
            }
            /* Same routing as the socket, so the two front ends cannot offer
             * different games. */
            if (strncmp(line, "ask", 3) == 0 && (line[3] == ' ' || !line[3])) {
                const char *a = line[3] ? line + 4 : "";
                while (*a == ' ') a++;
                Buf o = {0};
                if (*a >= '0' && *a <= '9') {
                    int idx = atoi(a);
                    while (*a >= '0' && *a <= '9') a++;
                    while (*a == ' ') a++;
                    customer_choose(&m, idx, a, &o);
                } else {
                    customer_options(&m, &o);
                    buf_puts(&o, "  `ask <n>` to say one. option 2 takes a "
                                 "command: `ask 2 dmesg -f error`\n");
                }
                fwrite(o.p, 1, o.len, stdout);
                buf_free(&o);
                continue;
            }
            if (strcmp(line, "rescue") == 0) {
                machine_boot_rescue(&m);
                fwrite(m.boot.console.p, 1, m.boot.console.len, stdout);
                continue;
            }
            if (strcmp(line, "boot") == 0) {
                m.on_rescue = false;
                m.nmount = 0;
                machine_boot(&m);
                fwrite(m.boot.console.p, 1, m.boot.console.len, stdout);
                {
                    Buf sick = {0};
                    int dead = kernel_health(&m, &sick);
                    if (m.boot.running && dead) {
                        printf("[UP at target, but %d service(s) are not running]\n",
                               dead);
                        fwrite(sick.p, 1, sick.len, stdout);
                    } else {
                        printf("[%s at %s]\n", m.boot.running ? "UP" : "DOWN",
                               boot_stage_name(m.boot.failed_at));
                    }
                    buf_free(&sick);
                }
                /* Same claim as the socket and the one-shot path: the machine
                 * started AND there is still something wrong with it. */
                if (m.boot.running) {
                    Buf left = {0};
                    if (machine_outstanding(&m, &left) && left.len)
                        fwrite(left.p, 1, left.len, stdout);
                    buf_free(&left);
                }
                if (m.boot.running) {
                    Buf col = {0};
                    if (machine_collateral(&m, &col))
                        fwrite(col.p, 1, col.len, stdout);
                    buf_free(&col);
                }
                continue;
            }
            Buf out = {0};
            kernel_run(&m, line, &out);
            /* A command that says nothing leaves out.p NULL, and fwrite is
             * declared never to take a null pointer even for zero bytes --
             * which UBSan reports. Silent commands used to be rare; now that
             * `X=5` and a successful `mkdir` are both silent, they are not. */
            if (out.len) fwrite(out.p, 1, out.len, stdout);
            buf_free(&out);
        }
        machine_free(&m);
        return 0;
    }

    uint64_t seed = argc > 1 ? strtoull(argv[1], NULL, 10) : 4823;
    int nfaults = argc > 2 ? atoi(argv[2]) : 1;
    Machine m; char what[512] = "";
    machine_install(&m, seed);
    if (nfaults > 0) machine_break(&m, seed, nfaults, what, sizeof what);
    else machine_boot(&m);
    fwrite(m.boot.console.p, 1, m.boot.console.len, stdout);
    {
        Buf sick = {0};
        int dead = kernel_health(&m, &sick);
        if (m.boot.running && dead) {
            printf("\n[UP at target, but %d service(s) are not right]\n", dead);
            fwrite(sick.p, 1, sick.len, stdout);
        } else {
            printf("\n[%s at %s]\n", m.boot.running ? "UP" : "DOWN",
                   boot_stage_name(m.boot.failed_at));
        }
        /* Damage the boot has not tripped over yet -- same claim the socket
         * makes, so the two front ends cannot disagree about whether a ticket
         * is finished. */
        if (m.boot.running) {
            Buf left = {0};
            if (machine_outstanding(&m, &left) && left.len)
                fwrite(left.p, 1, left.len, stdout);
            buf_free(&left);
        }
        buf_free(&sick);
    }
    if (getenv("NOM_SPOIL")) printf("[break: %s]\n", what);
    machine_free(&m);
    return 0;
}
