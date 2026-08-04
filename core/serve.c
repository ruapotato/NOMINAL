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
#define LINE_CAP    8192

typedef struct {
    sock_t   fd;
    Machine  m;
    bool     live;
    char     line[LINE_CAP];
    size_t   len;
} Client;

static void send_all(sock_t fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = (int)send(fd, data + sent, (int)(len - sent), 0);
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
    char tail[256];
    snprintf(tail, sizeof tail, "\n[%s at %s]\n",
             c->m.boot.running ? "UP" : "DOWN",
             boot_stage_name(c->m.boot.failed_at));
    send_str(c->fd, tail);
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
    char hdr[256];
    snprintf(hdr, sizeof hdr,
             "\n--- ticket %s: this machine will not boot ---\n", c->m.id);
    send_str(c->fd, hdr);
    {
        Buf i = {0};
        customer_intro(&c->m, &i);
        send_all(c->fd, i.p, i.len);
        buf_free(&i);
    }
    send_boot(c);
}

/* One line from a client. Returns false to hang up. */
static bool client_line(Client *c)
{
    char *cmd = c->line;
    while (*cmd == ' ') cmd++;
    size_t n = strlen(cmd);
    while (n && (cmd[n-1] == '\r' || cmd[n-1] == ' ')) cmd[--n] = 0;

    /* `exit` is the shell's: inside a chroot it leaves the chroot, which is
     * the documented flow. Hanging up on it stranded the player. */
    if (strcmp(cmd, "quit") == 0) return false;

    if (strcmp(cmd, "boot") == 0) {
        c->m.on_rescue = false;
        c->m.nmount = 0;
        send_boot(c);
        return true;
    }

    if (strncmp(cmd, "ask", 3) == 0 && (cmd[3] == ' ' || !cmd[3])) {
        Buf a = {0};
        customer_ask(&c->m, cmd[3] ? cmd + 4 : "", &a);
        send_all(c->fd, a.p, a.len);
        buf_free(&a);
        return true;
    }

    if (strcmp(cmd, "rescue") == 0) {
        machine_boot_rescue(&c->m);
        send_all(c->fd, c->m.boot.console.p, c->m.boot.console.len);
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
            "you are at a rescue shell with the customer's disk mounted.\n"
            "\n"
            "  boot              try to boot the customer's disk\n"
            "  rescue            boot the rescue medium -- this always works\n"
            "  ticket [seed] [n] take a new ticket (n = how many faults)\n"
            "  ask <question>    talk to the customer. They are not technical\n"
            "                    and they are the only pair of hands in the\n"
            "                    room. Try: what do you see on the screen /\n"
            "                    turn it off and on again / put the rescue\n"
            "                    disc in / have you deleted anything\n"
            "  quit              hang up (exit leaves a chroot, it does not disconnect)\n"
            "\n"
            "everything else runs on the machine. after `rescue`:\n"
            "  mount /dev/sda1 /mnt\n"
            "  for i in dev sys proc; do mount /$i /mnt/$i; done\n"
            "  chroot /mnt\n"
            "  pkg verify\n"
            "\n"
            "  ls /etc   ps   ns   stat <path>   pkg owns <path>   mount\n");
        return true;
    }

    if (!*cmd) return true;

    Buf out = {0};
    kernel_run(&c->m, cmd, &out);
    if (out.len) send_all(c->fd, out.p, out.len);
    buf_free(&out);
    return true;
}

int bench_serve(int port, bool verbose, uint64_t seed0)
{
    if (!net_platform_init()) { fprintf(stderr, "serve: winsock failed\n"); return 1; }

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
                    send_str(fd, "rescue# ");
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
                    send_str(c->fd, "rescue# ");
                } else if (c->len < LINE_CAP - 1) {
                    c->line[c->len++] = ch;
                }
            }
        }
    }
    sock_close(ls);
    return 0;
}
