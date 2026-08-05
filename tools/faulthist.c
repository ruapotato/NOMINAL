/* faulthist.c — how often does a player actually MEET each fault?
 *
 * A blind playtester sampled fifty-five ticket openings and wrote: "the
 * generator deals about a fifth of the fault classes its own documentation
 * describes". There was no way to argue with that, because there was no way
 * to count: the harness could FORCE a fault (NOM_FORCE_FAULT) and prove it
 * works, which says nothing about whether it is ever dealt.
 *
 * This deals N tickets down the real play path -- machine_install then
 * machine_break, exactly what `ticket` does -- and counts what each one was
 * made of. Faults with a zero are the deliverable: either they are never
 * chosen, or they are chosen and always fail their own viability check, and
 * the two have completely different fixes.
 *
 *   build/faulthist [N] [nfaults] [first-seed]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nom.h"
#include "machine.h"
#include "kernel.h"

#define MAXTAG 128

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 400;
    int nf = argc > 2 ? atoi(argv[2]) : 1;
    uint64_t first = argc > 3 ? strtoull(argv[3], NULL, 10) : 1;

    char tag[MAXTAG][64];
    int  cnt[MAXTAG] = {0}, ntag = 0;
    int  nboot = 0, nup = 0, nair = 0, nnofault = 0;
    static char dealt[4096][64];
    /* Runs of identical ticket SHAPE across adjacent seeds: a player takes
     * tickets in sequence, so clustering is felt even when the marginal
     * distribution is perfect. */
    int runlen = 0, worstrun = 0; char lastshape[8] = "";

    for (int i = 0; i < n; i++) {
        uint64_t seed = first + (uint64_t)i;
        Machine m; char what[512] = "";
        machine_install(&m, seed);
        machine_break(&m, seed, nf, what, sizeof what);
        m.airgapped = machine_airgapped(seed);

        Buf sick = {0};
        int dead = kernel_health(&m, &sick);
        buf_free(&sick);
        Buf left = {0};
        int rest = machine_outstanding(&m, &left) ? 1 : 0;
        buf_free(&left);

        char shape[8];
        snprintf(shape, sizeof shape, "%c%c",
                 m.boot.running ? 'u' : 'd', m.airgapped ? 'a' : 'n');
        if (strcmp(shape, lastshape) == 0) {
            if (++runlen > worstrun) { worstrun = runlen; }
        } else { runlen = 1; snprintf(lastshape, sizeof lastshape, "%s", shape); }

        if (!m.boot.running) nboot++; else nup++;
        if (m.airgapped) nair++;
        /* THE CHECK THAT MATTERS: could the player close this ticket without
         * touching the machine? Asking the hand-back is not the same as
         * asking whether the machine is broken -- seed 1234 was a real fault
         * that `done` repaired on its way to judging it. */
        {
            Buf hb = {0};
            if (machine_handback(&m, &hb)) {
                nnofault++;
                printf("NO FAULT: seed %llu closes with no repair  [%s]\n",
                       (unsigned long long)seed, breaker_dealt());
            }
            buf_free(&hb);
        }
        (void)dead; (void)rest;

        /* Split the dealt string into names. */
        if (i < 4096) snprintf(dealt[i], sizeof dealt[i], "%s", breaker_dealt());
        char d[512]; snprintf(d, sizeof d, "%s", breaker_dealt());
        for (char *t = strtok(d, " "); t; t = strtok(NULL, " ")) {
            int f = -1;
            for (int k = 0; k < ntag; k++) if (strcmp(tag[k], t) == 0) f = k;
            if (f < 0 && ntag < MAXTAG) {
                f = ntag++; snprintf(tag[f], sizeof tag[f], "%s", t);
            }
            if (f >= 0) cnt[f]++;
        }
        machine_free(&m);
    }

    /* Every structural fault, including the ones that never came up: a zero
     * is the entire point of this tool and it cannot print a row it never
     * saw unless the table itself is the source. */
    int nstruct = breaker_fault_count();
    printf("\n--- %d tickets, %d fault(s) each, seeds %llu..%llu ---\n",
           n, nf, (unsigned long long)first, (unsigned long long)(first + n - 1));
    int zero = 0;
    for (int i = 0; i < nstruct; i++) {
        const char *nm = breaker_fault_name(i);
        int c = 0;
        for (int k = 0; k < ntag; k++) if (strcmp(tag[k], nm) == 0) c = cnt[k];
        if (!c) zero++;
        printf("%-22s %5d  %5.2f%%\n", nm, c, 100.0 * c / n);
    }
    for (int k = 0; k < ntag; k++) {
        if (tag[k][0] != '-' && strncmp(tag[k], "stale", 5) != 0) continue;
        printf("%-22s %5d  %5.2f%%\n", tag[k], cnt[k], 100.0 * cnt[k] / n);
    }
    printf("\n%d/%d structural faults never dealt in %d tickets\n",
           zero, nstruct, n);
    /* THE NUMBER THE PLAYTESTER WAS ACTUALLY REPORTING. Nobody plays four
     * hundred tickets; they play a dozen, in a row, and what they feel is how
     * many different questions those twelve asked. */
    {
        int best = 99, worst2 = 0; double sum = 0; int wins = 0;
        for (int s = 0; s + 12 <= n; s++) {
            int distinct = 0;
            for (int a = s; a < s + 12; a++) {
                bool seen = false;
                for (int b = s; b < a; b++) if (strcmp(dealt[b], dealt[a]) == 0) seen = true;
                if (!seen && dealt[a][0]) distinct++;
            }
            sum += distinct; wins++;
            if (distinct < best) best = distinct;
            if (distinct > worst2) worst2 = distinct;
        }
        if (wins) printf("distinct fault classes per 12 consecutive tickets: "
                         "mean %.1f, worst %d, best %d\n", sum / wins, best, worst2);
    }
    /* And the coverage question: hand a player as many tickets as there are
     * faults in the table. How much of the table do they meet? An
     * independent draw answers about 63% of it however long the table is;
     * that is what "the documentation is writing cheques" sounds like. */
    {
        double sum = 0; int wins = 0, wsz = nstruct;
        for (int s = 0; s + wsz <= n; s++) {
            int distinct = 0;
            for (int f = 0; f < nstruct; f++) {
                const char *nm = breaker_fault_name(f);
                for (int a = s; a < s + wsz; a++)
                    if (strcmp(dealt[a], nm) == 0) { distinct++; break; }
            }
            sum += distinct; wins++;
        }
        if (wins) printf("designed faults met in %d consecutive tickets: "
                         "mean %.1f of %d (%.0f%%)\n",
                         wsz, sum / wins, nstruct, 100.0 * sum / wins / nstruct);
    }
    printf("won't boot %d (%.0f%%)  up-but-wrong %d (%.0f%%)  "
           "air-gapped %d (%.0f%%)  longest run of one shape %d\n",
           nboot, 100.0 * nboot / n, nup, 100.0 * nup / n,
           nair, 100.0 * nair / n, worstrun);
    printf("machines handed over with NOTHING wrong: %d\n", nnofault);
    return nnofault ? 1 : 0;
}
