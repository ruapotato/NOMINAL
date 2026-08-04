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

/* D21: locked in. Qwen2.5-3B-Instruct scores 100/100 on tools/persona_eval
 * at 5.2s a reply, which reads as a person thinking rather than a machine
 * being slow. Apache-2.0, so it is sellable. */
#define NOM_DEFAULT_MODEL "game/models/Qwen2.5-3B-Instruct-Q4_K_M.gguf"

int main(int argc, char **argv)
{
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
            bool died = strstr(c, "died --") || strstr(c, "respawning too fast")
                     || strstr(c, "refusing to start");
            if (!up || died) {
                bad++;
                printf("UNHEALTHY seed %d%s%s\n", 3000 + i,
                       up ? "" : " (did not boot)", died ? " (a service died)" : "");
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
                bool dup = false;
                for (int k = 0; k < nseen; k++)
                    if (strcmp(seen[k], m.boot.reason) == 0) dup = true;
                if (!dup && nseen < 4096)
                    snprintf(seen[nseen++], 160, "%s", m.boot.reason);
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
        int n = atoi(argv[2]), visible = 0, fixed = 0, made = 0;
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
            /* Repair from OUTSIDE first. If the disk's libc is the wrong
             * version, nothing on it will run at all -- so chrooting in and
             * using its tools is not an option, and this is the only way
             * back. Same reason rpm and dpkg have --root. */
            for (int k = 0; k < m.npkg; k++) {
                char cmd[160];
                snprintf(cmd, sizeof cmd, "pkg --root /mnt reinstall %s",
                         m.pkg[k]->name);
                kernel_run(&m, cmd, &o);
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
                snprintf(cmd, sizeof cmd, "pkg reinstall %s", m.pkg[k]->name);
                kernel_run(&m, cmd, &o);
            }
            kernel_run(&m, "mkinitrd", &o);
            kernel_run(&m, "zbl-mkconfig", &o);
            kernel_run(&m, "zbl-install /dev/sda", &o);

            m.on_rescue = false;
            m.nmount = 0;
            machine_boot(&m);
            if (m.boot.running) fixed++;
            else printf("UNFIXABLE seed %d: %s\n           %s\n",
                        5000 + i, what, m.boot.reason);
            buf_free(&v); buf_free(&o); machine_free(&m);
        }
        printf("\n%d tickets: %d visible to pkg verify, %d repaired by the tools\n",
               made, visible, fixed);
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

#ifdef NOM_LLM
    /* Load the customer's voice if the weights are there. Failure is silent
     * and harmless: the scripted persona answers instead. */
    {
        bool llm_load(const char *);
        const char *mp = getenv("NOM_MODEL");
        llm_load(mp ? mp : NOM_DEFAULT_MODEL);
    }
#endif

    if (argc > 1 && strcmp(argv[1], "--serve") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : 7777;
        return bench_serve(port, true, argc > 3 ? strtoull(argv[3], NULL, 10) : 4800);
    }

    if (argc > 1 && strcmp(argv[1], "--sh") == 0) {
        /* An interactive session against one machine: the whole game, with no
         * GUI anywhere near it. Each line is executed by /bin/sh ON the
         * machine, so this shell and the desktop's terminal cannot diverge. */
        uint64_t sd = argc > 2 ? strtoull(argv[2], NULL, 10) : 4823;
        int nf = argc > 3 ? atoi(argv[3]) : 1;
        Machine m; char what[512] = "";
        machine_install(&m, sd);
        if (nf > 0) machine_break(&m, sd, nf, what, sizeof what);
        machine_boot(&m);
        fwrite(m.boot.console.p, 1, m.boot.console.len, stdout);
        printf("\n[%s at %s]\n", m.boot.running ? "UP" : "DOWN",
               boot_stage_name(m.boot.failed_at));
        if (getenv("NOM_SPOIL")) printf("[break: %s]\n", what);

        /* One long-lived process owns the session, so cd and bind persist. */
        char line[512];
        for (;;) {
            printf("rescue# ");
            fflush(stdout);
            if (!fgets(line, sizeof line, stdin)) break;
            size_t L = strlen(line);
            while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
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
            if (strncmp(line, "ask", 3) == 0 && (line[3] == ' ' || !line[3])) {
                Buf a = {0};
                customer_ask(&m, line[3] ? line + 4 : "", &a);
                fwrite(a.p, 1, a.len, stdout);
                buf_free(&a);
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
                printf("[%s at %s]\n", m.boot.running ? "UP" : "DOWN",
                       boot_stage_name(m.boot.failed_at));
                continue;
            }
            Buf out = {0};
            kernel_run(&m, line, &out);
            fwrite(out.p, 1, out.len, stdout);
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
    printf("\n[%s at %s]\n", m.boot.running ? "UP" : "DOWN",
           boot_stage_name(m.boot.failed_at));
    if (getenv("NOM_SPOIL")) printf("[break: %s]\n", what);
    machine_free(&m);
    return 0;
}
