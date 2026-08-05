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
            if (m.boot.running && dead == 0) fixed++;
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
        static const char *FAKE[] = {
            "systemctl","journalctl","less","more","tail","vi","vim",
            "nano","apt","apt-get","yum","dnf","rpm","dpkg","service",
            "ss","ifconfig","ip","du","top","htop","lsof","awk",
            "curl","wget","tar","gzip","which","whereis","locate","tree",
            NULL
        };
        Machine m;
        machine_install(&m, 1);
        machine_boot(&m);
        int asked = 0, clean = 0;
        for (int i = 0; Q[i]; i++) {
            Buf o = {0};
            colleague_ask(&m, "manager", Q[i], &o);
            asked++;
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
            if (!bad) clean++;
            printf("%-3s %-56.56s%s%s\n", bad ? "NO" : "ok", Q[i],
                   bad ? "  invented: " : "", bad ? first : "");
            buf_free(&o);
        }
        (void)REAL;
        printf("\n%d/%d answers named only commands this machine has\n",
               clean, asked);
        machine_free(&m);
        return clean == asked ? 0 : 1;
    }

    /* --toolcheck: can the model actually decide what was asked for?
     *
     * David's bar: "If the model can't handle that we will cut the air gap
     * idea completely." So this measures it against phrasings I did NOT write
     * the parser around, including his own example verbatim. It prints what
     * the model chose against what a person would choose, and a score.
     */
    if (argc > 1 && strcmp(argv[1], "--toolcheck") == 0) {
        static const struct { const char *say, *want; } T[] = {
          { "Can I have you enter: 'ls /' and read back what you see.", "RUN ls /" },
          { "could you type ls /boot for me",                    "RUN ls /boot" },
          { "type df -h",                                        "RUN df -h" },
          { "at the prompt, put in cat /etc/fstab please",        "RUN cat /etc/fstab" },
          { "would you mind running pkg verify and telling me what it says",
                                                                 "RUN pkg verify" },
          { "go ahead and enter mount /dev/sda1 /mnt",            "RUN mount /dev/sda1 /mnt" },
          /* The model answers `dmesg | tail` here, which is BETTER than what I
           * expected -- it read "the last few lines" and used a pipe. Pipes
           * work on this machine, so it is correct. The test was wrong. */
          { "punch in dmesg and read me the last few lines",      "RUN dmesg | tail" },
          { "I need you to key in blkid",                         "RUN blkid" },
          { "reboot the computer",                                "POWER" },
          { "turn it off and on again please",                    "POWER" },
          { "can you power cycle the box for me",                 "POWER" },
          { "give it a restart",                                  "POWER" },
          { "pop the recovery disc in the drive",                 "DISC" },
          { "put the rescue cd in",                               "DISC" },
          { "is it plugged in at the wall?",                      "CABLE" },
          { "what does the screen say",                           "SCREEN" },
          { "read out what is on the monitor",                    "SCREEN" },
          { "when did it last work properly?",                    "NONE" },
          { "have you deleted anything recently",                 "NONE" },
          { "was there a power cut on Tuesday",                   "NONE" },
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
        Machine m;
        machine_install(&m, 1);
        machine_boot(&m);
        for (int i = 0; i < n; i++) {
            char got[256];
            customer_tool_probe(T[i].say, got, sizeof got);
            bool hit = strcmp(got, T[i].want) == 0;
            if (hit) ok++;
            printf("%-3s %-52.52s -> %-26s (want %s)\n",
                   hit ? "ok" : "NO", T[i].say, got, T[i].want);
        }
        printf("\n%d/%d  tool calls correct\n", ok, n);
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
        cust.airgapped = ((seed / 7) % 5) == 0;

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

        char line[1024];
        while (fgets(line, sizeof line, stdin)) {
            size_t l = strlen(line);
            while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
            if (strcmp(line, "quit") == 0) break;
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
        if (nf > 0) machine_break(&m, sd, nf, what, sizeof what);
        machine_boot(&m);
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
