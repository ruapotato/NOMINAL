/* bfmain.c — the break-fix harness. Proves the D17 gate: every break must
 * produce a coherent failure that the boot chain derived from the disk. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "nom.h"
#include "machine.h"
int machine_break_count(void);
const char *machine_break_desc(int i);

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--audit") == 0) {
        int n = machine_break_count(), solvable = 0;
        for (int i = 0; i < n; i++) {
            Machine m;
            /* find a seed that selects break i */
            char what[160] = "";
            uint64_t seed = 0;
            for (uint64_t s = 1; s < 100000; s++) {
                machine_install(&m, s);
                machine_break(&m, s, what, sizeof what);
                if (strcmp(what, machine_break_desc(i)) == 0) { seed = s; break; }
                machine_free(&m);
            }
            if (!seed) { printf("%2d  NO SEED  %s\n", i, machine_break_desc(i)); continue; }
            machine_boot(&m);
            printf("%2d  seed %-6llu %-9s %-4s %s\n     -> %s\n     ** %s\n",
                   i, (unsigned long long)seed,
                   boot_stage_name(m.boot.failed_at),
                   m.boot.running ? "UP" : "DOWN",
                   what, m.boot.reason,
                   m.boot.running ? "NOT BROKEN (bug)" : "ok");
            if (!m.boot.running) solvable++;
            machine_free(&m);
        }
        printf("\n%d/%d breaks stopped the boot\n", solvable, n);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--solve") == 0) {
        /* Solvability gate: pkg verify must SEE every break, and reinstalling
         * the package that owns the offending file must fix it. If a break is
         * invisible to verify, the player has no route to it. */
        int n = machine_break_count(), seen = 0, fixed = 0;
        for (int i = 0; i < n; i++) {
            Machine m; char what[160] = ""; uint64_t seed = 0;
            for (uint64_t s = 1; s < 100000; s++) {
                machine_install(&m, s);
                machine_break(&m, s, what, sizeof what);
                if (strcmp(what, machine_break_desc(i)) == 0) { seed = s; break; }
                machine_free(&m);
            }
            if (!seed) continue;
            Buf v = {0};
            pkg_verify(&m, NULL, &v);
            bool visible = !(v.len >= 30 && memcmp(v.p, "all files match", 15) == 0);
            /* reinstall every package: the biggest hammer the player has */
            Buf o = {0};
            for (int k = 0; k < m.npkg; k++) pkg_reinstall(&m, m.pkg[k]->name, &o);
            machine_boot(&m);
            printf("%2d %-8s %-8s %s\n", i, visible ? "visible" : "INVISIBLE",
                   m.boot.running ? "fixed" : "STILL-DOWN", what);
            if (visible) seen++;
            if (m.boot.running) fixed++;
            buf_free(&v); buf_free(&o); machine_free(&m);
        }
        printf("\n%d/%d visible to pkg verify, %d/%d fixed by reinstall\n",
               seen, n, fixed, n);
        return 0;
    }
    uint64_t seed = argc > 1 ? strtoull(argv[1], NULL, 10) : 4823;
    Machine m;
    machine_install(&m, seed);
    if (argc > 2 && strcmp(argv[2], "--pristine") == 0) { /* leave it working */ }
    else { char what[160]; machine_break(&m, seed, what, sizeof what);
           if (getenv("NOM_SPOIL")) fprintf(stderr, "[break: %s]\n", what); }
    machine_boot(&m);
    fwrite(m.boot.console.p, 1, m.boot.console.len, stdout);
    printf("\n[%s at %s]\n", m.boot.running ? "UP" : "DOWN",
           boot_stage_name(m.boot.failed_at));
    machine_free(&m);
    return 0;
}
