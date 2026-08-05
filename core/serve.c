/* serve.c — the TCP listener.
 *
 * The whole game must be playable through this socket with no GUI anywhere
 * near it. That is not a remote-control bolt-on: the desktop terminal and
 * this socket both call kernel_run(), which spawns /bin/sh ON THE MACHINE, so
 * there is exactly one implementation of what a command does and the two
 * cannot drift.
 *
 * Every connection gets its own machine, so two people can work different
 * tickets at once without sharing a namespace.
 *
 * Platform surface is deliberately tiny (socket/bind/listen/accept/select/
 * recv/send/close) so a winsock path stays a contained change. See D2.
 */
#include "nom.h"
#include "cpu.h"
#include "machine.h"
#include "kernel.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#ifndef _WIN32
#  include <signal.h>
#endif

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef int socklen_t;
#  define sock_close closesocket
#  define sock_errno WSAGetLastError()
#  define SOCK_EINTR WSAEINTR
   typedef SOCKET sock_t;
#  define BAD_SOCK INVALID_SOCKET
#else
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/select.h>
#  define sock_close close
#  define sock_errno errno
#  define SOCK_EINTR EINTR
   typedef int sock_t;
#  define BAD_SOCK (-1)
#endif

/* Winsock needs starting once per process; everything else is identical. */
static bool net_platform_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

#define MAX_CLIENTS 8
/* A LINE THAT DOES NOT FIT IS REFUSED, NOT SHORTENED.
 *
 * Characters past this used to be dropped on the floor with no mark of any
 * kind, so a long paste arrived as a different command from the one that was
 * sent -- the same silence that made a truncated glob delete the wrong files.
 * It is the machine's own argument limit now, and an overlong line says so
 * and runs nothing. */
#define LINE_CAP    NOM_ARG_MAX

typedef struct {
    sock_t   fd;
    Machine  m;          /* the CUSTOMER's machine                          */
    /* YOUR WORKSTATION. The socket serves the same game the desktop does --
     * you are at your own healthy box and reach theirs through its service
     * processor -- because a bench that plays a different game from the one
     * being shipped tests the wrong thing, and every playtest so far has run
     * through this socket. */
    Machine  desk;
    bool     desk_up;
    bool     live;
    char     line[LINE_CAP];
    size_t   len;
    bool     line_over;  /* this line was longer than the buffer */
} Client;

/* THE PROMPT SAYS WHICH MACHINE YOU ARE ON.
 *
 * It said `rescue#` always -- on your own workstation, on the customer's
 * console, and inside a chroot. Two playtesters lost track of where they were
 * and both reported it. Now it is the one piece of state you can never be
 * wrong about, because it is in front of every command you type. */
static const char *prompt_for(const Client *c)
{
    if (c->desk.sp_connected) return "root@node# ";
    return "you@desk# ";
}

static void send_all(sock_t fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        /* MSG_NOSIGNAL as well as the handler: belt and braces, and it is the
         * portable-ish way to say "a dead peer is an error, not a death". */
#ifdef MSG_NOSIGNAL
        int n = (int)send(fd, data + sent, (int)(len - sent), MSG_NOSIGNAL);
#else
        int n = (int)send(fd, data + sent, (int)(len - sent), 0);
#endif
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

static void send_str(sock_t fd, const char *s) { send_all(fd, s, strlen(s)); }

static void client_close(Client *c)
{
    if (c->fd != BAD_SOCK) sock_close(c->fd);
    c->fd = BAD_SOCK;
    if (c->live) { machine_free(&c->m); c->live = false; }
}

static void send_boot(Client *c)
{
    machine_boot(&c->m);
    send_all(c->fd, c->m.boot.console.p, c->m.boot.console.len);
    /* UP is not the same as WORKING, and the console has already scrolled
     * past whatever gave up. Say it at the end, where it will be read. */
    Buf sick = {0};
    int dead = kernel_health(&c->m, &sick);
    char tail[256];
    if (c->m.boot.running && dead)
        snprintf(tail, sizeof tail, "\n[UP at target, but %d service(s) are not running]\n",
                 dead);
    else
        snprintf(tail, sizeof tail, "\n[%s at %s]\n",
                 c->m.boot.running ? "UP" : "DOWN",
                 boot_stage_name(c->m.boot.failed_at));
    send_str(c->fd, tail);
    /* Damage the boot has not tripped over yet. Reported after the verdict,
     * because it is a different claim: the machine started, AND there is
     * still something wrong with it. */
    if (c->m.boot.running) {
        Buf left = {0};
        if (machine_outstanding(&c->m, &left) && left.len)
            send_all(c->fd, left.p, left.len);
        buf_free(&left);
    }
    if (dead) send_all(c->fd, sick.p, sick.len);
    buf_free(&sick);

    /* The machine booting is not the whole of the job. */
    if (c->m.boot.running && !dead) {
        Buf col = {0};
        if (machine_collateral(&c->m, &col)) send_all(c->fd, col.p, col.len);
        buf_free(&col);
    }
}

static void new_ticket(Client *c, uint64_t seed, int faults)
{
    if (c->live) machine_free(&c->m);
    machine_install(&c->m, seed);
    c->live = true;
    if (faults > 0) {
        char what[512];
        machine_break(&c->m, seed, faults, what, sizeof what);
    }
    /* One ticket in five is air-gapped: no service processor, no route, and
     * the only terminal you have is the person in front of it. */
    c->m.airgapped = machine_airgapped(seed);

    if (!c->desk_up) {
        machine_install(&c->desk, 1);
        machine_boot(&c->desk);
        c->desk_up = true;
    }
    c->desk.peer = &c->m;
    /* A new ticket is a new machine: you are not attached to it yet. This
     * persisted, so from the second ticket onwards the shell was already
     * "on their console" before you had reached it -- which is why `rcon
     * connect` was rejecting the address the header had just printed. */
    c->desk.sp_connected = false;
    /* The drive and the boot device belong to the machine they are in, so
     * they are reset on the CUSTOMER'S box. Setting the workstation's was
     * setting a field nothing reads. */
    c->m.sp_media = false;
    c->m.sp_bootdev = 0;
    snprintf(c->desk.peer_addr, sizeof c->desk.peer_addr,
             "10.0.2.%d", 60 + (int)(seed % 40));

    /* WHAT IS ACTUALLY WRONG WITH IT, not what is usually wrong with them.
     *
     * This said "Their machine is not coming up" on every ticket ever
     * issued, and it is not true of every ticket: seed 809 was sitting at a
     * login prompt with a degraded service, and seed 9090 booted perfectly
     * with a fault still on the disk. A player who reads "not coming up" and
     * then watches it come up has been told the first lie of the call by the
     * game itself, and after that they cannot use the blurb at all.
     *
     * The machine has already been broken and booted by now, so the answer is
     * a fact about it rather than a guess. It stays at the customer's level of
     * knowledge -- they can see the screen and they cannot see a service -- so
     * naming what is wrong is still the player's job. */
    Buf sick = {0};
    int dead = kernel_health(&c->m, &sick);
    buf_free(&sick);
    Buf left = {0};
    int rest = machine_outstanding(&c->m, &left) ? 1 : 0;
    buf_free(&left);
    const char *say;
    if (!c->m.boot.running)  say = "Their machine is not coming up.";
    else if (dead || rest)   say = "Their machine comes up, and something on it "
                                   "is not working.";
    else                     say = "They say it seems fine now, and they want "
                                   "somebody to be sure.";

    char hdr[512];
    snprintf(hdr, sizeof hdr,
             "\n--- ticket %s ---\n"
             "  %s is on the line. %s\n",
             c->m.id, customer_name(&c->m), say);
    send_str(c->fd, hdr);

    if (c->m.airgapped) {
        send_str(c->fd,
            "  it is not on any network -- there is no address to give you.\n"
            "  your only terminal on their machine is the person in front of\n"
            "  it. `ask` for what they can do; `ask 2 <command>` and they type\n"
            "  it and read back what they see.\n");
    } else {
        snprintf(hdr, sizeof hdr,
            "  they read you the address on the sticker: %s\n"
            "  `rcon connect %s` to reach it.\n",
            c->desk.peer_addr, c->desk.peer_addr);
        send_str(c->fd, hdr);
    }
    send_str(c->fd,
        "  you are at YOUR OWN workstation. everything you type runs HERE\n"
        "  unless you are on their console. `help` for the rest.\n\n");
}

/* One line from a client. Returns false to hang up. */
static bool client_line(Client *c)
{
    char *cmd = c->line;
    while (*cmd == ' ') cmd++;
    size_t n = strlen(cmd);
    while (n && (cmd[n-1] == '\r' || cmd[n-1] == ' ')) cmd[--n] = 0;

    /* The tail of this line was thrown away, so what is in the buffer is not
     * what was sent. Running the front of it would be running a command
     * nobody typed. */
    if (c->line_over) {
        c->line_over = false;
        send_str(c->fd,
            "\nthat line is longer than this connection can hold and the rest\n"
            "  of it was dropped, so NOTHING was run -- what is here is not\n"
            "  what you sent. shorten it, or let a glob expand on the machine.\n");
        return true;
    }

    /* `exit` is the shell's: inside a chroot it leaves the chroot, which is
     * the documented flow. Hanging up on it stranded the player. */
    if (strcmp(cmd, "quit") == 0) return false;

    /* THERE WAS NO WAY TO FINISH A JOB.
     *
     * A blind playtester repaired seven machines and wrote: "The customer
     * never says it's working, no ticket is marked resolved... `[UP at
     * target]` is the entire payoff and it's easy to miss." Worse, `rcon
     * power cycle` does not print that line at all, so their first fully
     * repaired ticket ended in silence. A shift made of jobs that never end
     * is not a shift.
     *
     * `done` is the hand-back, and it is a CLAIM the game checks rather than
     * a button that congratulates you. Signing off a machine that is still
     * broken is the mistake this job actually punishes -- so the refusal
     * names what is still wrong at the customer's level of knowledge and
     * leaves the diagnosis where it belongs. And a machine sitting on the
     * rescue medium is not repaired however healthy it looks: that image was
     * never broken. */
    if (strcmp(cmd, "done") == 0 || strcmp(cmd, "handback") == 0) {
        Buf hb = {0};
        machine_handback(&c->m, &hb);
        send_all(c->fd, hb.p, hb.len);
        buf_free(&hb);
        return true;
    }

    /* `boot` AND `rescue` ARE THE POWER BUTTON, AND YOU ARE NOT IN THE ROOM.
     *
     * They used to reach straight into the customer's Machine and run the
     * boot chain, from your own workstation, with nothing attached. On a
     * networked ticket, `rescue` typed before `rcon connect` printed a
     * byte-identical rescue boot -- zbios, "the customer disk is /dev/sda1
     * and is NOT mounted", the whole transcript -- and then left you at
     * `you@desk#`, on YOUR machine, where `cat /etc/hostname` says node-1.
     * Nothing had booted. A blind playtester believed they were on the rescue
     * medium for several minutes. The prompt was the only tell and a full
     * boot log above it drowns it out.
     *
     * On an AIR-GAPPED ticket it was worse, because the ticket says out loud
     * "it is not on any network -- there is no address to give you" and then
     * these two drove the machine anyway. A premise the game states and does
     * not enforce is not a premise.
     *
     * So they go through the service processor, like everything else that
     * touches that machine, and they leave it in the state they claim: after
     * `rescue`, `rcon status` says the medium is in and the boot device is
     * the medium, because it is. When there is no route, they refuse and name
     * the instrument that does exist. */
    if (strcmp(cmd, "boot") == 0 || strcmp(cmd, "rescue") == 0) {
        bool live = cmd[0] == 'r';
        if (c->m.airgapped) {
            send_str(c->fd, live ?
                "rescue: that machine has no service processor and no route --\n"
                "  it is not on any network, so nothing you type here reaches\n"
                "  it. The only hands in the room are the customer's:\n"
                "    ask                     what they can do from there\n"
                "    ask 2 <command>         they type it and read the screen\n"
                "                            back to you\n"
                :
                "boot: that machine has no service processor and no route --\n"
                "  it is not on any network, so nothing you type here reaches\n"
                "  it. The only hands in the room are the customer's:\n"
                "    ask                     what they can do from there\n");
            return true;
        }
        if (!c->desk.sp_connected) {
            char msg[512];
            snprintf(msg, sizeof msg,
                "%s: nothing here boots the customer's machine. You are at YOUR\n"
                "  OWN workstation -- this would have restarted the box you are\n"
                "  sitting at. Reach theirs first:\n"
                "    rcon connect %s\n"
                "  then `%s` again, or drive it by hand with `rcon media\n"
                "  insert`, `rcon boot %s`, `rcon power cycle`.\n",
                live ? "rescue" : "boot", c->desk.peer_addr,
                live ? "rescue" : "boot", live ? "media" : "disk");
            send_str(c->fd, msg);
            return true;
        }
        /* Attached: this IS the service processor, so set what it would have
         * been set to and let the same code run the boot. */
        c->m.sp_media   = live;
        c->m.sp_bootdev = live ? 1 : 0;
        if (live) {
            machine_boot_rescue(&c->m);
            send_all(c->fd, c->m.boot.console.p, c->m.boot.console.len);
        } else {
            send_boot(c);
        }
        return true;
    }

    /* TALKING TO HER IS A MENU, and the menu is the constraint.
     *
     * `ask` on its own is the list of things you can say from where the call
     * has got to; `ask <n>` says one; `ask 2 <command>` is the one option that
     * carries a command, which she types exactly as dictated or refuses whole.
     * There is no free-text channel to her any more and that is the point: the
     * cost of the air-gapped ticket is the round trip, not the wording. */
    if (strncmp(cmd, "ask", 3) == 0 && (cmd[3] == ' ' || !cmd[3])) {
        const char *a = cmd[3] ? cmd + 4 : "";
        while (*a == ' ') a++;
        Buf o = {0};
        if (*a >= '0' && *a <= '9') {
            int idx = atoi(a);
            while (*a >= '0' && *a <= '9') a++;
            while (*a == ' ') a++;
            customer_choose(&c->m, idx, a, &o);
        } else {
            if (*a) send_str(c->fd,
                "she is on the phone, not on chat. pick something to say:\n");
            customer_options(&c->m, &o);
            buf_puts(&o, "  `ask <n>` to say one. option 2 takes a command:\n"
                         "  `ask 2 dmesg -f error`\n");
        }
        send_all(c->fd, o.p, o.len);
        buf_free(&o);
        return true;
    }

    if (strncmp(cmd, "ticket", 6) == 0) {
        uint64_t seed = 0; int faults = 1;
        const char *a = cmd + 6;
        while (*a == ' ') a++;
        if (*a) seed = strtoull(a, (char **)&a, 10);
        while (*a == ' ') a++;
        if (*a) faults = atoi(a);
        if (!seed) seed = c->m.id[0] ? (uint64_t)atoi(c->m.id) + 1 : 4823;
        new_ticket(c, seed, faults);
        return true;
    }

    if (strcmp(cmd, "help") == 0) {
        send_str(c->fd,
            "you are at YOUR OWN workstation -- a healthy machine. the\n"
            "customer's is somewhere else. the prompt tells you which is which:\n"
            "  you@desk#    your machine\n"
            "  root@node#   theirs, over the console\n"
            "\n"
            "  boot              try to boot the customer's disk\n"
            "  rescue            boot the rescue medium -- this always works\n"
            "  done              hand the machine back. checks your claim: it\n"
            "                    must boot from ITS OWN disk with every service\n"
            "                    up and nothing left differing from what its\n"
            "                    packages shipped. this is how a job ends\n"
            "  ticket [seed] [n] take a new ticket (n = how many faults)\n"
            "  ask               what you can say to the customer right now.\n"
            "                    they are not technical and they are the only\n"
            "                    pair of hands in the room\n"
            "  ask <n>           say it\n"
            "  ask 2 <command>   dictate a command. they type exactly that and\n"
            "                    read back what they can see of the answer --\n"
            "                    which is the bottom of the screen and no more\n"
            "  quit              hang up (exit leaves a chroot, it does not disconnect)\n"
            "\n"
            "YOU ARE AT YOUR OWN WORKSTATION -- a healthy install of the same\n"
            "system. The customer's machine is somewhere else.\n"
            "  rcon connect <address>   attach to their service processor\n"
            "  rcon power off|on|cycle  their power button, remotely\n"
            "  rcon media insert|eject  put the rescue medium in their drive\n"
            "  rcon boot disk|media     what they boot from next time\n"
            "  rcon console             everything their machine has said\n"
            "once attached, what you type runs on THEIR machine.\n"
            "compare against your own box: it is the same system, working.\n"
            "\n"
            "IF THEY ARE AIR-GAPPED there is no route at all, and the person in\n"
            "front of the machine is your only terminal. `ask` lists what they\n"
            "can do: read the screen, put the rescue disc in, power cycle it,\n"
            "and `ask 2 <command>` to type one for you.\n"
            "\n"
            "START HERE. read what the machine said while it was failing:\n"
            "  dmesg             this boot\n"
            "  dmesg -1          the previous boot. Often the same as this\n"
            "                    one -- a broken machine breaks the same way\n"
            "                    twice -- but worth a look after you change\n"
            "                    something, to see what your change did\n"
            "  dmesg -r /mnt -1  the customer's previous boot, from rescue\n"
            "  dmesg -f <text>   only lines containing <text>\n"
            "the log tells you which LAYER broke. `pkg verify` is precise and\n"
            "slow and best asked about a package you already suspect.\n"
            "\n"
            "everything else runs on the machine. after `rescue`:\n"
            "  mount /dev/sda1 /mnt\n"
            "  for i in dev sys proc; do mount /$i /mnt/$i; done\n"
            "  chroot /mnt\n"
            "  pkg verify\n"
            "\n"
            "  ls /etc   ps   ns   stat <path>   mount   df   blkid   svc\n"
            "  grep   sed   wc   head   cat   cp   mv   rm   touch   chmod\n"
            "\n"
            "  ldd <program>     which libraries it needs, where each was\n"
            "                    found, and whether it is new enough. The way\n"
            "                    to answer \"why is THIS service dead and not\n"
            "                    that one\". Works on a mounted disk too:\n"
            "                    ldd /mnt/usr/sbin/httpd\n"
            "  pkg owns <path>   which package a file belongs to\n"
            "  pkg diff <path>|<package>   what changed, against what shipped\n"
            "  pkg --root /mnt <verb>      work on the disk WITHOUT chrooting,\n"
            "                    which is the only way in when its libc is\n"
            "                    broken and nothing on it will run\n"
            "  man <topic>       pkg, ldd, ns, sh ... `man` alone lists them\n"
            "\n"
            "editing: sed -i s/old/new/ <file>   sed -i /text/d <file>\n"
            "         quotes work: sed -i \"s/enabled: yes/enabled: no/\" f\n");
        return true;
    }

    if (!*cmd) return true;

    Buf out = {0};
    /* On YOUR machine, unless you have attached to theirs -- in which case
     * you are typing at their console, which is what a console is.
     *
     * EXCEPT `rcon` ITSELF, which always runs on your workstation. The
     * service processor belongs to the machine you are reaching FROM: their
     * box has no route to itself, so once you attached, every further rcon
     * command reported "no machine is reachable from here" and the whole
     * feature died after the first connect. A playtester hit it on every
     * ticket and never got to use media, boot or power at all. */
    bool is_rcon = strncmp(cmd, "rcon", 4) == 0 &&
                   (cmd[4] == 0 || cmd[4] == ' ');
    Machine *on = (c->desk.sp_connected && !is_rcon) ? &c->m : &c->desk;

    /* A CONSOLE ON A DEAD MACHINE HAS NO SHELL.
     *
     * David: "rcon boot disk, then power cycle, then connect gets me to the
     * same working shell, not a broken box I need to fix." He is right and it
     * was the worst thing in the build: kernel_run spawns /bin/sh off the
     * disk whatever the boot did, so attaching to a machine that died at
     * initrd still gave you a prompt. A service processor shows you the
     * machine's screen. If the machine never got to a shell, the screen has
     * no shell on it, and that IS the diagnosis. */
    if (on == &c->m) {
        Buf b = {0};
        if (kernel_console_dead(&c->m, cmd, &b)) {
            send_all(c->fd, b.p, b.len);
            buf_free(&b);
            send_str(c->fd, prompt_for(c));
            return true;
        }
        buf_free(&b);
    }
    kernel_run(on, cmd, &out);
    if (out.len) send_all(c->fd, out.p, out.len);
    buf_free(&out);
    return true;
}

int bench_serve(int port, bool verbose, uint64_t seed0)
{
    if (!net_platform_init()) { fprintf(stderr, "serve: winsock failed\n"); return 1; }
#ifndef _WIN32
    /* THE BUG THAT KILLED THREE PLAYTESTS. Writing to a socket whose peer has
     * gone raises SIGPIPE, and the default action for SIGPIPE is to kill the
     * process. Every client that hangs up while the server is mid-reply --
     * which is every client, because they hang up after each session -- had a
     * chance of taking the whole bench down with it, mid-diagnosis, for
     * somebody else. Three blind playtests ended early on this and I blamed
     * my own rebuilds for two of them. */
    signal(SIGPIPE, SIG_IGN);
#endif

    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == BAD_SOCK) { fprintf(stderr, "serve: socket failed\n"); return 1; }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "serve: cannot bind port %d\n", port);
        sock_close(ls);
        return 1;
    }
    listen(ls, 4);
    if (verbose) fprintf(stderr, "serve: listening on 127.0.0.1:%d\n", port);

    Client cl[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) { cl[i].fd = BAD_SOCK; cl[i].live = false; }
    uint64_t seq = seed0;

    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(ls, &rd);
        sock_t maxfd = ls;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (cl[i].fd == BAD_SOCK) continue;
            FD_SET(cl[i].fd, &rd);
            if (cl[i].fd > maxfd) maxfd = cl[i].fd;
        }
        if (select((int)maxfd + 1, &rd, NULL, NULL, NULL) < 0) {
            if (sock_errno == SOCK_EINTR) continue;
            break;
        }

        if (FD_ISSET(ls, &rd)) {
            sock_t fd = accept(ls, NULL, NULL);
            if (fd != BAD_SOCK) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd == BAD_SOCK) { slot = i; break; }
                if (slot < 0) { send_str(fd, "too many connections\n"); sock_close(fd); }
                else {
                    Client *c = &cl[slot];
                    c->fd = fd; c->len = 0; c->live = false;
                    send_str(fd, "NOMINAL support bench. `help` for what you can do.\n");
                    new_ticket(c, ++seq, 1);
                    send_str(fd, prompt_for(c));
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            Client *c = &cl[i];
            if (c->fd == BAD_SOCK || !FD_ISSET(c->fd, &rd)) continue;
            char buf[1024];
            int n = (int)recv(c->fd, buf, sizeof buf, 0);
            if (n <= 0) { client_close(c); continue; }
            for (int k = 0; k < n; k++) {
                char ch = buf[k];
                if (ch == '\n') {
                    c->line[c->len < LINE_CAP ? c->len : LINE_CAP - 1] = 0;
                    bool keep = client_line(c);
                    c->len = 0;
                    if (!keep) { client_close(c); break; }
                    send_str(c->fd, prompt_for(c));
                } else if (c->len < LINE_CAP - 1) {
                    c->line[c->len++] = ch;
                } else {
                    c->line_over = true;
                }
            }
        }
    }
    sock_close(ls);
    return 0;
}
