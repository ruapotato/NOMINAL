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
            Buf sick = {0};
            bool died = kernel_health(&m, &sick) > 0
                     || strstr(c, "died --") || strstr(c, "respawning too fast")
                     || strstr(c, "refusing to start");
            buf_free(&sick);
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
    /* --jsoncheck: does the runbook author invent things?
     *
     * A playtester: "the runbook author hallucinating commands is the single
     * worst thing here -- it is the one character whose job is to be
     * authoritative." He told them to run `svc list`, which does not exist,
     * and `rm /tmp/*.tmp` when the shell had no globbing.
     *
     * So this asks him twelve questions, several of them invitations to make
     * something up, and fails him for naming any command the machine does not
     * have. It cannot check whether an ANSWER is true -- that needs a person
     * -- but a fabricated command is mechanically detectable and is the
     * failure that destroys trust fastest.
     */
    if (argc > 1 && strcmp(argv[1], "--jsoncheck") == 0) {
        static const char *Q[] = {
            "a .svc file has no exec line, what does that mean",
            "how do I list every service on the machine",
            "how do I delete four hundred files in /tmp",
            "how do I see which package owns a file",
            "how do I check the filesystem for errors",
            "what do I do about a machine that boots but has no network",
            "how do I roll back a package upgrade",
            "how do I see the previous boot's log",
            "how do I find out why one service will not start",
            "how can I list the open network connections",
            "how do I search the whole disk for a file by name",
            "how do I edit a file on the customer's disk from the rescue medium",
            NULL
        };
        /* Every program this machine actually has. */
        static const char *REAL[] = {
            "dmesg","svc","pkg","ldd","df","blkid","mount","umount","fsck",
            "ls","cat","stat","chmod","cp","mv","rm","touch","grep","sed",
            "head","wc","echo","ps","ns","kill","chroot","man","links",
            "mkinitrd","zbl-mkconfig","zbl-install","rcon","sh","init","rc",
            "svcinit","login","getty","mountall","whoami","uname","for",
            "tail","du","mkdir","find","netstat","seq","rev","rot13",
            NULL
        };
        /* WITHOUT A MODEL THIS GATE PASSES, WHICH IS WORSE THAN FAILING.
         *
         * It scores answers for naming only real commands, and a binary with
         * no model gives no answers at all -- so it scored a clean 12/12 by
         * saying nothing. A green light from an unplugged instrument is the
         * most expensive kind of wrong. */
#ifndef NOM_LLM
        printf("--jsoncheck needs the model: this binary was built without "
               "it.\n  rebuild with `make bf NOM_LLM=1` -- with no model "
               "there are no answers\n  to check, and it would score a "
               "perfect and completely empty pass.\n");
        return 2;
#endif
        /* Things a model reaches for that are not here. */
        /* THIS LIST GOES STALE, AND A STALE ONE FAILS THE HONEST ANSWER.
         * `tail` and `du` were on it, and both have been real commands since
         * the shell grew them -- so Ben naming `du -sh /var`, which is the
         * correct answer to "what is using up the disk", was scored as an
         * invention. Anything added to the userland has to come off here. */
        static const char *FAKE[] = {
            "systemctl","journalctl","less","more","vi","vim",
            "nano","apt","apt-get","yum","dnf","rpm","dpkg","service",
            "ss","ifconfig","ip","top","htop","lsof","awk",
            "curl","wget","tar","gzip","which","whereis","locate","tree",
            NULL
        };
        /* AND BEN, WHO IS HELD TO THE SAME RULE.
         *
         * He was briefed to know nothing at all, so he could not name a tool
         * and never did -- three sessions of him restating the question back
         * to the player. He knows the vocabulary now, which means he can name
         * a command, which means he can invent one. The enforcement in
         * colleague_ask has always covered both of them; what was missing was
         * the measurement, so these are asked in the same run and scored the
         * same way. His answers are questions, so they are also the place an
         * invented tool would do the most damage: a wrong steer phrased as
         * "have you checked X" is one a player will go and check. */
        static const char *BQ[] = {
            "the machine boots but httpd is not answering",
            "I think the initrd is corrupt",
            "the boot stops with an emergency shell and a uuid on the screen",
            "one service will not start and I do not know why",
            "how do I see what is using up the disk",
            "the library is the wrong version, what would you do",
            NULL
        };
        Machine m;
        machine_install(&m, 1);
        machine_boot(&m);
        int basked = 0, bclean = 0;
        int asked = 0, clean = 0, refused = 0;
        /* Same selector as --toolcheck, for the same reason: a full run is
         * ten minutes of model, and a question you are working on is one. */
        const char *only = argc > 2 ? argv[2] : NULL;
        for (int round = 0; round < 2; round++) {
        const char *who = round ? "manager" : "coworker";
        const char **QS = round ? Q : BQ;
        for (int i = 0; QS[i]; i++) {
            if (only && !strstr(QS[i], only)) continue;
            Buf o = {0};
            colleague_ask(&m, who, QS[i], &o);
            if (round) asked++; else basked++;
            const char *txt = o.p ? o.p : "";
            int bad = 0;
            char first[64] = "";
            /* ONLY INSIDE BACKTICKS. "the service is down", "which package",
             * "find out why" are ordinary English, and counting them as
             * invented commands made the harness accuse Json of things he had
             * not done -- a measurement that cries wolf is worse than none.
             * A model writing a command writes it as `cmd`. */
            for (const char *q = strchr(txt, '`'); q && !bad; ) {
                const char *e = strchr(q + 1, '`');
                if (!e) break;
                char span[128];
                size_t n2 = (size_t)(e - q - 1);
                if (n2 >= sizeof span) n2 = sizeof span - 1;
                memcpy(span, q + 1, n2);
                span[n2] = 0;
                /* the first word of the span is the program */
                char prog[64] = "";
                size_t k = 0;
                for (const char *w = span; *w && *w != ' ' && k < sizeof prog - 1; w++)
                    prog[k++] = *w;
                prog[k] = 0;
                for (int f = 0; FAKE[f]; f++) {
                    if (strcmp(prog, FAKE[f]) == 0) {
                        bad = 1;
                        snprintf(first, sizeof first, "%s", prog);
                        break;
                    }
                }
                q = strchr(e + 1, '`');
            }
            if (!bad) { if (round) clean++; else bclean++; }
            /* A REFUSAL SCORES CLEAN, AND THAT IS A HOLE IN THIS GATE.
             *
             * When a reply names something invented, colleague_ask throws it
             * away, retries once, and falls back to "I would have to look at
             * that one" -- which names no invented command and therefore
             * counts as a pass. Twelve refusals would print 12/12. The gate
             * is still right that nothing was fabricated, but a character who
             * refuses every question is not a working character, so the
             * refusals are counted out loud where the score cannot hide them. */
            bool gaveup = strstr(txt, "I would have to look at that one") != NULL;
            if (gaveup) refused++;
            printf("%-3s %-5s %-50.50s%s%s\n",
                   bad ? "NO" : gaveup ? "--" : "ok",
                   round ? "json" : "ben", QS[i],
                   bad ? "  invented: " : "", bad ? first : "");
            buf_free(&o);
        }
        if (!round)
            printf("\n%d/%d of Ben's answers named only commands this machine "
                   "has\n\n", bclean, basked);
        }
        (void)REAL;
        if (only)
            printf("\n  (a SELECTION, not the gate)\n");
        if (refused)
            printf("\n%d of the %d answers, Ben's and Json's together, were "
                   "refusals: the reply\n  named something invented twice and "
                   "was thrown away. Marked --, and they\n  are not evidence "
                   "that anybody answered well.\n", refused, asked + basked);
        printf("\n%d/%d answers named only commands this machine has\n",
               clean, asked);
        machine_free(&m);
        return (clean == asked && bclean == basked) ? 0 : 1;
    }

    /* --toolcheck: can the model actually decide what was asked for?
     *
     * David's bar: "If the model can't handle that we will cut the air gap
     * idea completely." So this measures it against phrasings I did NOT write
     * the parser around, including his own example verbatim. It prints what
     * the model chose against what a person would choose, and a score.
     */
    if (argc > 1 && strcmp(argv[1], "--toolcheck") == 0) {
        /* `alt` is a SECOND answer that is also right, for the one case where
         * two answers are. It is not a way to make a failure pass: adding one
         * means writing down why both are correct behaviour at the machine. */
        static const struct { const char *say, *want, *alt; } T[] = {
          { "Can I have you enter: 'ls /' and read back what you see.", "RUN ls /" },
          { "could you type ls /boot for me",                    "RUN ls /boot" },
          { "type df -h",                                        "RUN df -h" },
          { "at the prompt, put in cat /etc/fstab please",        "RUN cat /etc/fstab" },
          { "would you mind running pkg verify and telling me what it says",
                                                                 "RUN pkg verify" },
          { "go ahead and enter mount /dev/sda1 /mnt",            "RUN mount /dev/sda1 /mnt" },
          /* The model answers `dmesg | tail` here, which is BETTER than what I
           * expected -- it read "the last few lines" and used a pipe. Pipes
           * work on this machine, so it is correct. The test was wrong.
           *
           * Since the customer learned to do two things in one breath it
           * answers "RUN dmesg THEN SCREEN" instead, which is the same
           * instruction split in two -- and the tool call refuses to join a
           * RUN to anything, because reading the answer back is what RUN IS.
           * What reaches the machine is `dmesg`, and the customer then reads
           * back the last six lines of it, which is what was asked for. Both
           * are right; neither is the truncation this gate is here to catch. */
          { "punch in dmesg and read me the last few lines",      "RUN dmesg | tail",
                                                                  "RUN dmesg" },
          { "I need you to key in blkid",                         "RUN blkid" },
          { "reboot the computer",                                "POWER" },
          { "turn it off and on again please",                    "POWER" },
          { "can you power cycle the box for me",                 "POWER" },
          { "give it a restart",                                  "POWER" },
          { "pop the recovery disc in the drive",                 "DISC" },
          { "put the rescue cd in",                               "DISC" },
          /* THE WAY BACK OUT. The disc action was insert-only, so a
           * playtester who had repaired an air-gapped machine could not get
           * it off the live medium: every phrasing of "take it out" came back
           * "it is already in there", and the ticket could be solved and
           * never finished. These are their words, verbatim. */
          { "take the disc out of the drive",                     "EJECT" },
          { "eject the rescue disc",                              "EJECT" },
          { "please remove the rescue disc and put it back in the drawer",
                                                                  "EJECT" },
          /* One sentence, two jobs. This did the power cycle, silently
           * dropped the eject, and the machine came up on the medium again
           * with nothing said about the half that was not done. */
          { "take the rescue disc out and turn it off and on again",
                                                                  "EJECT THEN POWER" },
          /* A dictated line longer than the classifier could return came back
           * cut -- and was RUN. `sed -i s/old/new/ /` is not a shorter
           * version of what was said, it is a different command, and it ran
           * against a different path. The canonical air-gapped repair is
           * exactly this long, so the only right answer is the whole of it. */
          { "type sed -i s/c603-2d03-bafe-e442/8f41-2c07-a19d-5be3/ /mnt/boot/zbl/zbl.cfg",
            "RUN sed -i s/c603-2d03-bafe-e442/8f41-2c07-a19d-5be3/ /mnt/boot/zbl/zbl.cfg" },
          { "is it plugged in at the wall?",                      "CABLE" },
          { "what does the screen say",                           "SCREEN" },
          { "read out what is on the monitor",                    "SCREEN" },
          { "when did it last work properly?",                    "NONE" },
          { "have you deleted anything recently",                 "NONE" },
          { "was there a power cut on Tuesday",                   "NONE" },
          /* THE LAST QUESTION OF EVERY REPAIR, and it was being answered by
           * reading one line of console out -- the same three lines, on every
           * question that was not about the screen. Whether they have their
           * computer back is a question only the person can answer. */
          { "is your machine working again",                      "NONE" },
          { "can you log in now",                                 "NONE" },
          { "did anyone reboot it before you rang?",              "NONE" },
          /* The model answers SCREEN here and I have stopped calling that
           * wrong. Asked whether they can see the screen, a real customer
           * says "yes, it says..." -- describing it IS the answer. Changed
           * the expectation rather than the model, and saying so out loud
           * because quietly moving a target is how a gate stops meaning
           * anything. */
          { "can you see the screen from where you are?",         "SCREEN" },
        };
        int n = (int)(sizeof T / sizeof T[0]);
        int ok = 0;
        /* A GATE THAT CANNOT RUN MUST SAY SO, NOT SCORE ZERO.
         *
         * `make bf` without NOM_LLM=1 links no model, every classification
         * comes back NONE, and this printed 4/22 -- the four cases whose
         * right answer happens to be NONE. That reads as the model having
         * catastrophically regressed, and I spent real time hunting a bug
         * that was a missing build flag. A measurement taken with the
         * instrument unplugged is not a low number, it is not a number. */
#ifndef NOM_LLM
        printf("--toolcheck needs the model: this binary was built without "
               "it.\n  rebuild with `make bf NOM_LLM=1` -- without a model "
               "every answer is NONE\n  and the score is meaningless, not "
               "bad.\n");
        return 2;
#endif
        /* A whole run is a few minutes of model, which is a slow loop to
         * develop against. An optional substring selects the cases you are
         * working on -- `--toolcheck disc` -- and the score then says how
         * many OF THOSE passed, so a partial run can never be mistaken for
         * the gate. */
        const char *only = argc > 2 ? argv[2] : NULL;
        Machine m;
        machine_install(&m, 1);
        machine_boot(&m);
        int ran = 0;
        for (int i = 0; i < n; i++) {
            if (only && !strstr(T[i].say, only)) continue;
            char got[600];       /* a whole dictated command line, and then some */
            customer_tool_probe(T[i].say, got, sizeof got);
            bool hit = strcmp(got, T[i].want) == 0 ||
                       (T[i].alt && strcmp(got, T[i].alt) == 0);
            ran++;
            if (hit) ok++;
            printf("%-3s %-52.52s -> %-26s (want %s)\n",
                   hit ? "ok" : "NO", T[i].say, got, T[i].want);
        }
        n = ran;
        printf("\n%d/%d  tool calls correct%s\n", ok, n,
               only ? "  (a SELECTION, not the gate)" : "");
        machine_free(&m);
        return ok == n ? 0 : 1;
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
            printf("  machine is %s. `ask type <command>` and they will read back\n",
                   customer_name(&cust));
            printf("  whatever appears on the screen.\n");
        } else {
            printf("  they read you the address on the sticker: %s\n", desk.peer_addr);
            printf("  you are at YOUR workstation. `rcon connect %s` to reach theirs.\n",
                   desk.peer_addr);
        }
        printf("  `ask <question>` to talk to them.\n\n");

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
            if (strncmp(line, "ask ", 4) == 0) {
                Buf a = {0};
                customer_ask(&cust, line + 4, &a);
                fwrite(a.p, 1, a.len, stdout);
                buf_free(&a);
                continue;
            }
            if (strncmp(line, "ben ", 4) == 0 || strncmp(line, "json ", 5) == 0) {
                Buf a = {0};
                colleague_ask(&cust, line[0] == 'b' ? "coworker" : "manager",
                              line + (line[0] == 'b' ? 4 : 5), &a);
                fwrite(a.p, 1, a.len, stdout);
                buf_free(&a);
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
            /* Three people. Same routing as the socket, so the two front
             * ends cannot offer different games. */
            if (strncmp(line, "ben ", 4) == 0 || strncmp(line, "sam ", 4) == 0) {
                Buf a = {0};
                colleague_ask(&m, "coworker", line + 4, &a);
                fwrite(a.p, 1, a.len, stdout);
                buf_free(&a);
                continue;
            }
            if (strncmp(line, "json ", 5) == 0 || strncmp(line, "boss ", 5) == 0) {
                Buf a = {0};
                colleague_ask(&m, "manager", line + 5, &a);
                fwrite(a.p, 1, a.len, stdout);
                buf_free(&a);
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
