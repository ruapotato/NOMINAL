/* net.c — the local TCP listener.
 *
 * Everything a player can do at the desktop must also be doable over this
 * socket: it is how Claude plays the game without a human present, and it is
 * how a human attaches with telnet, tmux and vim. It is not a remote-control
 * bolt-on — the desktop terminal and this socket call the identical
 * shell_exec(), so they cannot drift.
 *
 * Platform surface is deliberately tiny (socket/bind/listen/accept/select/
 * recv/send/close) so a winsock path stays a contained change. See D2.
 */
#include "nom.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

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
    sock_t fd;
    Shell *sh;
    char   line[LINE_CAP];
    size_t len;
    bool   overflow;
} Client;

static void send_all(sock_t fd, const char *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, data + off, len - off, 0);
        if (n <= 0) {
            if (n < 0 && sock_errno == SOCK_EINTR) continue;
            return;
        }
        off += (size_t)n;
    }
}

static void client_close(Client *c)
{
    if (c->fd != BAD_SOCK) sock_close(c->fd);
    if (c->sh) shell_free(c->sh);
    c->fd = BAD_SOCK;
    c->sh = NULL;
    c->len = 0;
    c->overflow = false;
}

/* Feed one complete line to the shell and ship the response. */
static bool client_line(Client *c, Sim *sim)
{
    (void)sim;
    c->line[c->len] = 0;
    /* tolerate telnet's CRLF */
    size_t n = c->len;
    while (n && (c->line[n - 1] == '\r' || c->line[n - 1] == '\n')) c->line[--n] = 0;

    Buf out;
    buf_init(&out);
    bool keep = shell_exec(c->sh, c->line, &out);
    if (out.len) send_all(c->fd, out.p, out.len);
    buf_free(&out);
    c->len = 0;
    return keep;
}

int net_serve(Sim *sim, int port, bool verbose)
{
    if (!net_platform_init()) { fprintf(stderr, "nominal: cannot start sockets\n"); return 1; }

    sock_t lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == BAD_SOCK) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* local only, by design */
    addr.sin_port = htons((uint16_t)port);

    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); sock_close(lfd); return 1; }
    if (listen(lfd, 4) < 0) { perror("listen"); sock_close(lfd); return 1; }

    if (verbose) {
        fprintf(stderr, "nominal: listening on 127.0.0.1:%d\n", port);
        fprintf(stderr, "nominal: try  telnet 127.0.0.1 %d   then 'help'\n", port);
        fflush(stderr);
    }

    Client cl[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) { cl[i].fd = BAD_SOCK; cl[i].sh = NULL; cl[i].len = 0; }

    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(lfd, &rd);
        sock_t maxfd = lfd;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (cl[i].fd != BAD_SOCK) { FD_SET(cl[i].fd, &rd); if (cl[i].fd > maxfd) maxfd = cl[i].fd; }

        int r = select((int)maxfd + 1, &rd, NULL, NULL, NULL);
        if (r < 0) { if (sock_errno == SOCK_EINTR) continue; perror("select"); break; }

        if (FD_ISSET(lfd, &rd)) {
            sock_t fd = accept(lfd, NULL, NULL);
            if (fd != BAD_SOCK) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd == BAD_SOCK) { slot = i; break; }

                if (slot < 0) {
                    const char *busy = "-ERR too many sessions\n.\n";
                    send_all(fd, busy, strlen(busy));
                    sock_close(fd);
                } else {
                    cl[slot].fd = fd;
                    cl[slot].sh = shell_new(sim);
                    cl[slot].len = 0;
                    cl[slot].overflow = false;
                    char hello[256];
                    int hn = snprintf(hello, sizeof hello,
                        "+OK NOMINAL/1 station shell, scenario cold-ship, seed %llu\n"
                        "type 'help' for commands; every response ends with a lone '.'\n.\n",
                        (unsigned long long)sim->seed);
                    send_all(fd, hello, (size_t)hn);
                    if (verbose) fprintf(stderr, "nominal: session %d opened\n", slot);
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (cl[i].fd == BAD_SOCK || !FD_ISSET(cl[i].fd, &rd)) continue;
            char tmp[2048];
            ssize_t n = recv(cl[i].fd, tmp, sizeof tmp, 0);
            if (n <= 0) {
                if (verbose) fprintf(stderr, "nominal: session %d closed\n", i);
                client_close(&cl[i]);
                continue;
            }
            bool keep = true;
            for (ssize_t k = 0; k < n && keep; k++) {
                char ch = tmp[k];
                if (ch == '\n') {
                    if (cl[i].overflow) {
                        const char *msg = "-ERR line too long\n.\n";
                        send_all(cl[i].fd, msg, strlen(msg));
                        cl[i].overflow = false;
                        cl[i].len = 0;
                    } else {
                        keep = client_line(&cl[i], sim);
                    }
                } else if (cl[i].len < LINE_CAP - 1) {
                    cl[i].line[cl[i].len++] = ch;
                } else {
                    cl[i].overflow = true;
                }
            }
            if (!keep) client_close(&cl[i]);
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) if (cl[i].fd != BAD_SOCK) client_close(&cl[i]);
    sock_close(lfd);
    return 0;
}
