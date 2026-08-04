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

#endif /* GSYS_H */
