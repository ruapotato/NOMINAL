/* kernel.c — the host side of the machine: syscalls backed by the real
 * filesystem, and process spawn.
 *
 * This is what a guest binary is actually talking to when it executes `ecall`.
 * Everything here is a deterministic function of the machine's disk and the
 * program's own behaviour. There is no clock, no host filesystem, no entropy.
 *
 * SPAWN, and why it exists in this shape: a real boot is a chain of programs,
 * each of which can be corrupted independently. Rather than build an MMU and a
 * scheduler to get that, spawn runs the child on its own fresh CPU to
 * completion and returns its exit code. It is exactly the "run this and wait"
 * that an rc script does, and it gives every stage of the boot its own
 * separately-breakable binary.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "nom.h"
#include "cpu.h"
#include "abi.h"
#include "machine.h"
#include "kernel.h"

#define FD_MAX      16
#define SPAWN_DEPTH  8
/* A boot that never finishes is a real failure, and a real one to diagnose.
 * The budget is per program and generous: a correct guest uses a tiny
 * fraction of it. */
#define PROC_BUDGET  40000000ull

typedef struct {
    bool  used;
    char  path[NOM_PATH_MAX];
    Buf   data;        /* the whole file, read at open */
    size_t pos;
    bool  writable;
} Fd;

struct Proc {
    Machine *m;
    Buf     *console;
    Fd       fd[FD_MAX];
    char     arg[NOM_PATH_MAX];
    int      depth;
    uint64_t icount_total;   /* charged across the whole spawn tree */
};

/* Read a NUL-terminated string out of guest memory, bounded. */
static bool guest_str(Cpu *c, uint64_t addr, char *out, size_t outsz)
{
    for (size_t i = 0; i < outsz; i++) {
        uint8_t ch;
        if (!cpu_read(c, addr + i, &ch, 1)) return false;
        out[i] = (char)ch;
        if (!ch) return true;
    }
    return false;              /* unterminated: the guest is corrupt */
}

static int alloc_fd(Proc *p)
{
    for (int i = 3; i < FD_MAX; i++) if (!p->fd[i].used) return i;
    return -1;
}

static int64_t sys_open(Proc *p, Cpu *c, uint64_t pathp, int64_t flags)
{
    char path[NOM_PATH_MAX];
    if (!guest_str(c, pathp, path, sizeof path)) return -1;

    bool dangling = false;
    VNode *n = vfs_resolve(&p->m->disk, path, &dangling);
    if (!n && (flags & O_CREAT)) {
        n = vfs_mkfile(&p->m->disk, path, "");
        if (!n) return -1;
    }
    if (!n || dangling) return -1;
    if (n->kind == VN_DIR) return -1;

    int fd = alloc_fd(p);
    if (fd < 0) return -1;
    Fd *f = &p->fd[fd];
    memset(f, 0, sizeof *f);
    f->used = true;
    f->writable = (flags & (O_WRONLY | O_RDWR)) != 0;
    snprintf(f->path, sizeof f->path, "%s", path);
    if (!(flags & O_TRUNC))
        buf_put(&f->data, n->data.p, n->data.len);
    if (flags & O_APPEND) f->pos = f->data.len;
    return fd;
}

static int64_t sys_read(Proc *p, Cpu *c, int64_t fd, uint64_t buf, int64_t len)
{
    if (fd < 3 || fd >= FD_MAX || !p->fd[fd].used || len < 0) return -1;
    Fd *f = &p->fd[fd];
    size_t left = f->data.len > f->pos ? f->data.len - f->pos : 0;
    size_t n = (size_t)len < left ? (size_t)len : left;
    if (n && !cpu_write(c, buf, f->data.p + f->pos, n)) return -1;
    f->pos += n;
    return (int64_t)n;
}

static int64_t sys_write(Proc *p, Cpu *c, int64_t fd, uint64_t buf, int64_t len)
{
    if (len < 0 || len > (1 << 20)) return -1;
    char *tmp = nom_alloc((size_t)len + 1);
    if (!cpu_read(c, buf, tmp, (size_t)len)) { nom_free(tmp); return -1; }

    if (fd == 1 || fd == 2) {                     /* the console */
        if (p->console) buf_put(p->console, tmp, (size_t)len);
        nom_free(tmp);
        return len;
    }
    if (fd < 3 || fd >= FD_MAX || !p->fd[fd].used || !p->fd[fd].writable) {
        nom_free(tmp);
        return -1;
    }
    Fd *f = &p->fd[fd];
    if (f->pos != f->data.len) buf_clear(&f->data);   /* no seeking yet */
    buf_put(&f->data, tmp, (size_t)len);
    f->pos = f->data.len;
    nom_free(tmp);
    return len;
}

static int64_t sys_close(Proc *p, int64_t fd)
{
    if (fd < 3 || fd >= FD_MAX || !p->fd[fd].used) return -1;
    Fd *f = &p->fd[fd];
    if (f->writable) {
        VNode *n = vfs_lookup(&p->m->disk, f->path);
        if (n && n->kind == VN_FILE) {
            buf_clear(&n->data);
            buf_put(&n->data, f->data.p, f->data.len);
        }
    }
    buf_free(&f->data);
    memset(f, 0, sizeof *f);
    return 0;
}

static int64_t sys_readdir(Proc *p, Cpu *c, uint64_t pathp, int64_t idx,
                           uint64_t buf, int64_t len)
{
    char path[NOM_PATH_MAX];
    if (!guest_str(c, pathp, path, sizeof path)) return -1;
    VNode *d = vfs_resolve(&p->m->disk, path, NULL);
    if (!d || d->kind != VN_DIR) return -1;
    int64_t i = 0;
    for (VNode *k = d->child; k; k = k->next, i++) {
        if (i != idx) continue;
        size_t nl = strlen(k->name);
        if ((int64_t)nl + 1 > len) return -1;
        if (!cpu_write(c, buf, k->name, nl + 1)) return -1;
        return (int64_t)nl;
    }
    return -1;                                     /* past the end */
}

static int64_t sys_stat(Proc *p, Cpu *c, uint64_t pathp, uint64_t sbuf)
{
    char path[NOM_PATH_MAX];
    if (!guest_str(c, pathp, path, sizeof path)) return -1;
    VNode *ln = vfs_lookup(&p->m->disk, path);
    if (!ln) return -1;
    NomStat st;
    memset(&st, 0, sizeof st);
    if (ln->kind == VN_LINK) {
        /* stat follows the link; a dangling one is a genuine failure and the
         * guest is entitled to see it as one. */
        bool dangling = false;
        VNode *t = vfs_resolve(&p->m->disk, path, &dangling);
        if (!t || dangling) return -1;
        ln = t;
    }
    st.mode = (int32_t)ln->mode;
    st.size = (int64_t)ln->data.len;
    st.kind = ln->kind == VN_DIR  ? NOM_KIND_DIR
            : ln->kind == VN_DEV  ? NOM_KIND_DEV
            : ln->kind == VN_LINK ? NOM_KIND_LINK : NOM_KIND_FILE;
    if (!cpu_write(c, sbuf, &st, sizeof st)) return -1;
    return 0;
}

static int64_t sys_getarg(Proc *p, Cpu *c, uint64_t buf, int64_t len)
{
    size_t n = strlen(p->arg);
    if ((int64_t)n + 1 > len) return -1;
    if (!cpu_write(c, buf, p->arg, n + 1)) return -1;
    return (int64_t)n;
}

/* ------------------------------------------------------------- syscall -- */

static int64_t kernel_syscall(Cpu *c, int64_t n, int64_t a0, int64_t a1,
                              int64_t a2, void *ctx)
{
    Proc *p = (Proc *)ctx;
    switch (n) {
    case SYS_write:   return sys_write(p, c, a0, (uint64_t)a1, a2);
    case SYS_read:    return sys_read (p, c, a0, (uint64_t)a1, a2);
    case SYS_close:   return sys_close(p, a0);
    case SYS_open:    return sys_open (p, c, (uint64_t)a0, a1);
    case SYS_readdir: return sys_readdir(p, c, (uint64_t)a0, a1, (uint64_t)a2,
                                         (int64_t)NOM_NAME_MAX);
    case SYS_stat:    return sys_stat (p, c, (uint64_t)a0, (uint64_t)a1);
    case SYS_getarg:  return sys_getarg(p, c, (uint64_t)a0, a1);
    case SYS_spawn: {
        char path[NOM_PATH_MAX], arg[NOM_PATH_MAX] = "";
        if (!guest_str(c, (uint64_t)a0, path, sizeof path)) return SPAWN_ENOENT;
        if (a1 && !guest_str(c, (uint64_t)a1, arg, sizeof arg)) return SPAWN_ENOENT;
        return kernel_spawn(p->m, path, arg, p->console, p->depth + 1, NULL, 0);
    }
    case SYS_exit:
        c->exit_code = a0;
        c->trap = TRAP_EXIT;
        return 0;
    default:
        return -1;                 /* unknown syscalls fail; they never crash */
    }
}

/* ---------------------------------------------------------------- spawn -- */

/* Say why a program could not be run, on the console, the way a loader does.
 * Without this a nested spawn fails silently and the player is left with the
 * last thing that DID work, which is evidence pointing at the wrong file. */
static int64_t spawn_fail(Buf *console, char *err, size_t errsz, int64_t code,
                          const char *fmt, ...)
{
    va_list ap;
    char line[NOM_ERR_MAX];
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (err && errsz) snprintf(err, errsz, "%s", line);
    if (console) { buf_puts(console, line); buf_putc(console, '\n'); }
    return code;
}

int64_t kernel_spawn(Machine *m, const char *path, const char *arg,
                     Buf *console, int depth, char *err, size_t errsz)
{
    if (err && errsz) err[0] = '\0';
    if (depth > SPAWN_DEPTH)
        return spawn_fail(console, err, errsz, SPAWN_EDEPTH,
                          "%s: too many nested programs", path);

    bool dangling = false;
    VNode *ln = vfs_lookup(&m->disk, path);
    VNode *n = vfs_resolve(&m->disk, path, &dangling);
    if (dangling)
        return spawn_fail(console, err, errsz, SPAWN_ENOENT,
                          "%s -> %s: no such file", path, ln ? ln->target : "?");
    if (!n)
        return spawn_fail(console, err, errsz, SPAWN_ENOENT, "%s: not found", path);
    if (n->kind != VN_FILE)
        return spawn_fail(console, err, errsz, SPAWN_ENOENT,
                          "%s: not a regular file", path);
    if (!(n->mode & 0111))
        return spawn_fail(console, err, errsz, SPAWN_EPERM,
                          "%s: permission denied (mode %04o)", path, n->mode);

    Cpu c;
    cpu_init(&c);
    char lerr[128] = "";
    if (!cpu_load_elf(&c, (const uint8_t *)n->data.p, n->data.len,
                      lerr, sizeof lerr)) {
        cpu_free(&c);
        return spawn_fail(console, err, errsz, SPAWN_ENOEXEC, "%s: %s", path, lerr);
    }

    Proc p;
    memset(&p, 0, sizeof p);
    p.m = m;
    p.console = console;
    p.depth = depth;
    snprintf(p.arg, sizeof p.arg, "%s", arg ? arg : "");

    c.syscall = kernel_syscall;
    c.ctx = &p;

    CpuTrap t;
    do {
        t = cpu_run(&c, 1000000);
    } while (t == TRAP_BUDGET && c.icount < PROC_BUDGET);

    int64_t rc;
    if (t == TRAP_EXIT) {
        rc = c.exit_code;
    } else if (t == TRAP_BUDGET) {
        rc = spawn_fail(console, err, errsz, SPAWN_EFAULT,
                        "%s: still running after %llu instructions -- killed",
                        path, (unsigned long long)c.icount);
    } else {
        /* A trap is the machine catching a program doing something impossible.
         * The pc matters: it is where in the binary the damage bit. */
        rc = spawn_fail(console, err, errsz, SPAWN_EFAULT,
                        "%s: %s at pc 0x%llx", path, cpu_trap_name(t),
                        (unsigned long long)c.pc);
    }

    for (int i = 0; i < FD_MAX; i++) if (p.fd[i].used) sys_close(&p, i);
    cpu_free(&c);
    return rc;
}
