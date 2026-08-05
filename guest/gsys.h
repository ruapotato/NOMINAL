/* gsys.h — the guest side of the ABI.
 *
 * Freestanding: no libc, no malloc, no static initialisers that need a
 * runtime. Everything a guest program needs is here, and it is deliberately
 * small -- these programs are meant to be readable by a player who opens them
 * up, not just executable.
 */
#ifndef GSYS_H
#define GSYS_H

typedef unsigned long  u64;
typedef long           i64;
typedef unsigned int   u32;
typedef int            i32;

#define NOM_GUEST 1
#include "../core/abi.h"

static inline i64 sysc(i64 n, i64 a, i64 b, i64 c)
{
    register i64 x10 __asm__("a0") = a;
    register i64 x11 __asm__("a1") = b;
    register i64 x12 __asm__("a2") = c;
    register i64 x17 __asm__("a7") = n;
    __asm__ volatile("ecall" : "+r"(x10) : "r"(x11), "r"(x12), "r"(x17) : "memory");
    return x10;
}

static inline u64 g_strlen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }

static inline void g_write(int fd, const char *s, u64 n) { sysc(SYS_write, fd, (i64)s, (i64)n); }
static inline void g_puts(const char *s)  { g_write(1, s, g_strlen(s)); }
static inline void g_putln(const char *s) { g_puts(s); g_puts("\n"); }
static inline void g_exit(int code) { sysc(SYS_exit, code, 0, 0); for (;;) { } }

static inline int  g_open(const char *p, int flags) { return (int)sysc(SYS_open, (i64)p, flags, 0); }
static inline i64  g_read(int fd, char *b, u64 n)   { return sysc(SYS_read, fd, (i64)b, (i64)n); }
static inline void g_close(int fd)                  { sysc(SYS_close, fd, 0, 0); }
static inline i64  g_readdir(const char *dir, int i, char *buf)
                                                    { return sysc(SYS_readdir, (i64)dir, i, (i64)buf); }
static inline int  g_stat(const char *p, NomStat *st) { return (int)sysc(SYS_stat, (i64)p, (i64)st, 0); }
static inline i64  g_spawn(const char *p, const char *arg) { return sysc(SYS_spawn, (i64)p, (i64)arg, 0); }
static inline i64  g_getarg(char *buf, u64 n) { return sysc(SYS_getarg, (i64)buf, (i64)n, 0); }
static inline int  g_bootsec(int write) { return (int)sysc(SYS_bootsec, write, 0, 0); }
static inline i64  g_rootuuid(char *b, u64 n) { return sysc(SYS_rootuuid, (i64)b, (i64)n, 0); }
static inline i64  g_dns(const char *n, char *b, u64 c) { return sysc(SYS_dns, (i64)n, (i64)b, (i64)c); }
static inline i64  g_http(const char *ip, const char *p, char *b) { return sysc(SYS_http, (i64)ip, (i64)p, (i64)b); }
static inline int  g_kill(int pid, int sig) { return (int)sysc(SYS_kill, pid, sig, 0); }
static inline int  g_sigpend(void) { return (int)sysc(SYS_sigpend, 0, 0, 0); }
static inline i64  g_pipe(const char *p, const char *a) { return sysc(SYS_pipe, (i64)p, (i64)a, 0); }
static inline void g_pipeout(void) { sysc(SYS_pipeout, 0, 0, 0); }

/* Read all of stdin. Filters use this when they are given no file, which is
 * what makes them usable in a pipeline. */
static inline i64 g_slurp_stdin(char *buf, u64 cap)
{
    u64 got = 0;
    for (;;) {
        i64 n = sysc(SYS_read, 0, (i64)(buf + got), (i64)(cap - 1 - got));
        if (n <= 0) break;
        got += (u64)n;
        if (got >= cap - 1) break;
    }
    buf[got] = 0;
    return (i64)got;
}

static inline i64  g_svcstart(const char *p, const char *n, int r) { return sysc(SYS_svcstart, (i64)p, (i64)n, r); }
static inline i64  g_readlink(const char *p, char *b, u64 n) { return sysc(SYS_readlink, (i64)p, (i64)b, (i64)n); }
static inline int  g_getpid(void) { return (int)sysc(SYS_getpid, 0, 0, 0); }
static inline int  g_bind(const char *target, const char *at) { return (int)sysc(SYS_bind, (i64)target, (i64)at, 0); }
static inline int  g_unbind(const char *at) { return (int)sysc(SYS_unbind, (i64)at, 0, 0); }
static inline int  g_chdir(const char *p) { return (int)sysc(SYS_chdir, (i64)p, 0, 0); }
static inline i64  g_getcwd(char *b, u64 n) { return sysc(SYS_getcwd, (i64)b, (i64)n, 0); }
static inline i64  g_repo(const char *pkg, const char *path, char *buf)
                        { return sysc(SYS_repo, (i64)pkg, (i64)path, (i64)buf); }

/* Decimal, without a libc. */
static inline void g_putn(i64 v)
{
    char t[24]; int i = 0; int neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (!u) t[i++] = '0';
    while (u) { t[i++] = (char)('0' + u % 10); u /= 10; }
    char o[26]; int j = 0;
    if (neg) o[j++] = '-';
    while (i) o[j++] = t[--i];
    g_write(1, o, (u64)j);
}

static inline void g_putoct(unsigned v, int digits)
{
    char o[12]; int j = 0;
    for (int i = digits - 1; i >= 0; i--) o[j++] = (char)('0' + ((v >> (3 * i)) & 7));
    g_write(1, o, (u64)j);
}

/* FNV-1a, so a guest can tell whether a file matches what a package shipped.
 * Deterministic and tiny; this is a corruption check, not a security one. */
static inline unsigned long g_hash(const char *p, u64 n)
{
    unsigned long h = 1469598103934665603UL;
    for (u64 i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211UL; }
    return h;
}

static inline void g_puthex(unsigned long v)
{
    char o[16]; const char *hex = "0123456789abcdef";
    for (int i = 0; i < 16; i++) o[i] = hex[(v >> ((15 - i) * 4)) & 15];
    g_write(1, o, 16);
}

/* argv, split from the single argument string the ABI hands us.
 *
 * QUOTING. Splitting on whitespace and nothing else meant no argument
 * containing a space could be expressed anywhere on this system -- a
 * playtester found that `sed -i s/enabled: yes/enabled: no/ f` was not merely
 * awkward but IMPOSSIBLE, in either quoting style, and had to reinstall a
 * whole package to change one word. Single and double quotes both group, a
 * backslash escapes the next character, and the quotes are removed as the
 * shell removes them. Done here rather than in sh.c so that every program
 * gets it, since every program splits its arguments through this one
 * function. */
#define GARGS 8
static inline int g_argv(char *arg, char **v)
{
    int n = 0;
    char *w = arg;                    /* write cursor: we rewrite in place */
    while (*arg && n < GARGS) {
        while (*arg == ' ' || *arg == '\t') arg++;
        if (!*arg) break;
        v[n++] = w;
        while (*arg && *arg != ' ' && *arg != '\t') {
            if (*arg == '\\' && arg[1]) { arg++; *w++ = *arg++; continue; }
            if (*arg == '"' || *arg == '\'') {
                char q = *arg++;
                while (*arg && *arg != q) {
                    /* Inside double quotes only \" and \\ are escapes, as in
                     * every real shell. Consuming ALL backslashes here ate the
                     * ones meant for the program: `sed "s|a|x\ty|"` reached sed
                     * as a literal t, so the tool could never see an escape the
                     * user typed for it. Single quotes escape nothing at all. */
                    if (q == '"' && *arg == '\\' &&
                        (arg[1] == '"' || arg[1] == '\\')) arg++;
                    *w++ = *arg++;
                }
                if (*arg) arg++;      /* closing quote */
                continue;
            }
            *w++ = *arg++;
        }
        if (*arg) arg++;
        *w++ = 0;
    }
    return n;
}

/* A number in a fixed-width right-aligned column, then two spaces.
 *
 * `ls` separated the size from the name with a TAB, and the terminal in the
 * desktop does not expand tabs -- so every line read as "0bin", "4096etc",
 * and David reasonably asked why all his folders started with a zero. A
 * column that is always the same width needs no tab. */
static inline void g_putpad(i64 v, int width)
{
    char b[24];
    int n = 0;
    if (v == 0) b[n++] = '0';
    i64 t = v;
    while (t > 0) { b[n++] = (char)('0' + (t % 10)); t /= 10; }
    for (int i = n; i < width; i++) g_puts(" ");
    for (int i = n - 1; i >= 0; i--) { char c[2] = { b[i], 0 }; g_puts(c); }
    g_puts("  ");
}

/* Does `hay` contain `needle`? */
static inline int g_contains(const char *hay, const char *needle)
{
    for (u64 i = 0; hay[i]; i++) {
        u64 k = 0;
        while (needle[k] && hay[i + k] == needle[k]) k++;
        if (!needle[k]) return 1;
    }
    return 0;
}

/* Read a whole file into a caller-supplied buffer. Returns length, or -1.
 * Guests own their buffers: there is no allocator on this machine. */
static inline i64 g_slurp(const char *path, char *buf, u64 cap)
{
    int fd = g_open(path, O_RDONLY);
    if (fd < 0) return -1;
    u64 got = 0;
    for (;;) {
        i64 n = g_read(fd, buf + got, cap - 1 - got);
        if (n <= 0) break;
        got += (u64)n;
        if (got >= cap - 1) break;
    }
    g_close(fd);
    buf[got] = 0;
    return (i64)got;
}

static inline int g_streq(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }

static inline int g_endswith(const char *s, const char *suf)
{
    u64 sl = g_strlen(s), pl = g_strlen(suf);
    if (pl > sl) return 0;
    for (u64 i = 0; i < pl; i++) if (s[sl - pl + i] != suf[i]) return 0;
    return 1;
}

static inline void g_copy(char *d, const char *s, u64 cap)
{ u64 i = 0; for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }

static inline void g_cat(char *d, const char *s, u64 cap)
{ u64 n = g_strlen(d), i = 0; for (; s[i] && n + i + 1 < cap; i++) d[n + i] = s[i]; d[n + i] = 0; }

/* Trim leading and trailing whitespace in place. */
static inline char *g_trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    u64 n = g_strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
    return s;
}

/* Every program records what it was linked against, in a section the loader
 * reads before it will run the binary. This is how a libc upgrade can break
 * everything at once: the check is real, performed against the library
 * actually installed on the machine, by code that was trying to run the
 * program. */
#ifndef NOM_NO_NEEDS
/* A program that compresses defines NOM_NEEDS_LIBZ before including this, and
 * its dependency list grows a second line. That matters for more than
 * realism: when EVERY binary needs exactly the same libraries, a bad library
 * breaks the whole machine at once and the ticket is over in one step. A
 * library only some programs need breaks only those programs, and a machine
 * where the web server and the mailer are dead while ssh, cron and the
 * firewall are perfectly happy is a much better question to be asked. */
#ifdef NOM_NEEDS_LIBZ
__attribute__((section(".nomneed"), used))
static const char _nomneed[] = "libc.so.6 2.38\nlibz.so.1 1.3\n";
#else
__attribute__((section(".nomneed"), used))
static const char _nomneed[] = "libc.so.6 2.38\n";
#endif
#endif

#endif /* GSYS_H */
