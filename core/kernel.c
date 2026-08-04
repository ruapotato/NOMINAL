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
#include "ns.h"
#include "kernel.h"

const char *net_dns(const char *host);
bool net_fetch(const char *ip, const char *path, Buf *out);

#define FD_MAX      16
#define SPAWN_DEPTH  8
/* A boot that never finishes is a real failure, and a real one to diagnose.
 * The budget is per program and generous: a correct guest uses a tiny
 * fraction of it. */
#define PROC_BUDGET  40000000ull

typedef struct {
    bool  used;
    Vfs  *fs;                 /* which filesystem this path is really on */
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
    int      pid;            /* index into m->proc                       */
    ProcInfo *info;          /* our own row: cwd and namespace live here */

    /* PIPES. A process holds one buffer that is both the output of the last
     * stage it ran and the input of the next. `a | b | c` is three children
     * in a row, each reading what the previous one wrote. There is no
     * concurrency and none is needed: these are filters, not conversations. */
    Buf      pipe;
    Buf     *stdin_from;     /* fd 0 reads from here, if set              */
    size_t   stdin_pos;
    bool     capture;        /* fd 1 goes to the parent's pipe, not the console */
    Buf     *capture_into;
};

/* A long-lived service. Declared up here rather than beside its own code
 * because the syscall handler needs to see it: kill() has to find the
 * target, and the target is a daemon. */
struct Daemon {
    Cpu      cpu;
    Proc     proc;
    char     name[40];
    char     path[NOM_PATH_MAX];
    bool     running;
    int      restart_policy;   /* 0 never, 1 on-failure, 2 always */
    int      restarts;
    bool     gave_up;
    int      pending_sig;      /* delivered when the daemon next asks */
    int64_t  exit_code;
    char     died[NOM_ERR_MAX];
};

/* Which filesystem is a path actually on, and where on it?
 *
 * Four transforms, in the order a real kernel applies them:
 *   1. relative -> absolute, against the process's cwd
 *   2. chroot   -> the process's root is prepended, so "/" means /mnt
 *   3. namespace-> plan 9 bindings, longest prefix (ns.c)
 *   4. mounts   -> longest mountpoint wins; the remainder is the path on
 *                  THAT filesystem
 *
 * Step 4 is what makes `mount /dev/sda1 /mnt` real: below /mnt, lookups stop
 * happening on the rescue medium and start happening on the customer's disk.
 */
static Vfs *resolve_fs(Proc *p, const char *in, char *out, size_t outsz)
{
    Machine *m = p->m;
    char abs[NOM_PATH_MAX * 2], rooted[NOM_PATH_MAX * 2], nsr[NOM_PATH_MAX * 2];

    vfs_normalize(p->info ? p->info->cwd : "/", in, abs, sizeof abs);

    const char *root = (p->info && p->info->root[0]) ? p->info->root : "/";
    if (strcmp(root, "/") != 0)
        snprintf(rooted, sizeof rooted, "%s%s", root, abs);
    else
        snprintf(rooted, sizeof rooted, "%s", abs);

    if (p->info) ns_resolve(&p->info->ns, rooted, nsr, sizeof nsr);
    else         snprintf(nsr, sizeof nsr, "%s", rooted);

    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    int best = -1;
    size_t bestlen = 0;
    for (int i = 0; i < m->nmount; i++) {
        if (!m->mount[i].used) continue;
        if (!m->mount[i].fs) continue;      /* virtual: nothing is layered */
        size_t al = strlen(m->mount[i].at);
        if (strncmp(nsr, m->mount[i].at, al) != 0) continue;
        if (!(al == 1 || nsr[al] == 0 || nsr[al] == '/')) continue;
        if (best < 0 || al > bestlen) { best = i; bestlen = al; }
    }
    if (best >= 0) {
        const char *rest = (bestlen == 1) ? nsr : nsr + bestlen;
        if (!*rest) rest = "/";
        if (m->mount[best].sub[0]) {
            char j[NOM_PATH_MAX * 2];
            snprintf(j, sizeof j, "%s%s", m->mount[best].sub, rest);
            vfs_normalize("/", j, out, outsz);
        } else {
            snprintf(out, outsz, "%s", rest);
        }
        return m->mount[best].fs;
    }
    snprintf(out, outsz, "%s", nsr);
    return fs;
}

/* Resolve for MOUNT and friends: cwd, chroot and namespace, but deliberately
 * NOT the mount table. A mountpoint is a name in the process's view of the
 * world; running it through the mount table would rewrite /mnt/dev back into
 * /dev on the underlying disk and mount it over the wrong place. */
static void resolve_ns(Proc *p, const char *in, char *out, size_t outsz)
{
    char abs[NOM_PATH_MAX * 2], rooted[NOM_PATH_MAX * 2];
    vfs_normalize(p->info ? p->info->cwd : "/", in, abs, sizeof abs);
    const char *root = (p->info && p->info->root[0]) ? p->info->root : "/";
    if (strcmp(root, "/") != 0) snprintf(rooted, sizeof rooted, "%s%s", root, abs);
    else                        snprintf(rooted, sizeof rooted, "%s", abs);
    if (p->info) ns_resolve(&p->info->ns, rooted, out, outsz);
    else         snprintf(out, outsz, "%s", rooted);
}

/* The common case: callers that only want the path, on the machine's own
 * current root filesystem. */
static void resolve(Proc *p, const char *in, char *out, size_t outsz)
{
    resolve_fs(p, in, out, outsz);
}

/* ------------------------------------------------------------- /proc ----
 * Synthesised from the process table, never read off the disk -- exactly as
 * on a real system. Corrupting the customer's filesystem therefore cannot
 * forge a process, and /proc stays trustworthy when everything else is not.
 * That is a property worth having in a game about deciding what to believe.
 */

static bool proc_split(const char *path, int *pid, char *leaf, size_t leafsz,
                       Proc *self)
{
    if (strncmp(path, "/proc", 5) != 0) return false;
    if (path[5] != '/' ) { *pid = -1; leaf[0] = 0; return path[5] == 0; }
    const char *p = path + 6;
    if (strncmp(p, "self", 4) == 0 && (p[4] == 0 || p[4] == '/')) {
        *pid = self ? self->pid : 1;
        p += 4;
    } else {
        int v = 0; bool any = false;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); any = true; }
        if (!any) return false;
        *pid = v;
    }
    if (*p == '/') p++;
    snprintf(leaf, leafsz, "%s", p);
    return true;
}

static ProcInfo *proc_by_pid(Machine *m, int pid)
{
    for (int i = 0; i < m->nproc; i++)
        if (m->proc[i].pid == pid) return &m->proc[i];
    return NULL;
}

/* Fill `out` with the contents of a /proc file. Returns false if there is no
 * such file. */
static bool proc_read(Machine *m, Proc *self, const char *path, Buf *out)
{
    int pid; char leaf[64];
    if (!proc_split(path, &pid, leaf, sizeof leaf, self)) return false;
    if (pid < 0) return false;                    /* /proc itself is a dir */
    ProcInfo *pi = proc_by_pid(m, pid);
    if (!pi) return false;

    if (strcmp(leaf, "status") == 0) {
        buf_printf(out, "name %s\n", pi->name);
        buf_printf(out, "pid %d\n", pi->pid);
        buf_printf(out, "ppid %d\n", pi->ppid);
        buf_printf(out, "state %s\n", pi->alive ? "running" : "exited");
        buf_printf(out, "exit %lld\n", (long long)pi->exit_code);
        buf_printf(out, "instructions %llu\n", (unsigned long long)pi->icount);
        return true;
    }
    if (strcmp(leaf, "cmdline") == 0) {
        buf_printf(out, "%s%s%s\n", pi->name, pi->arg[0] ? " " : "", pi->arg);
        return true;
    }
    if (strcmp(leaf, "cwd") == 0) { buf_printf(out, "%s\n", pi->cwd); return true; }
    if (strcmp(leaf, "ns") == 0)  { ns_print(&pi->ns, out); return true; }
    return false;
}

static const char *PROC_FILES[] = { "status", "cmdline", "cwd", "ns", NULL };

static bool link_check(Machine *m, Vfs *fs, const char *needs,
                       char *err, size_t errsz);
static int64_t spawn_fail(Buf *console, char *err, size_t errsz, int64_t code,
                          const char *fmt, ...);
static int64_t kernel_syscall(Cpu *c, int64_t n, int64_t a0, int64_t a1,
                              int64_t a2, void *ctx);

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
    char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
    if (!guest_str(c, pathp, raw, sizeof raw)) return -1;
    Vfs *fs = resolve_fs(p, raw, path, sizeof path);

    /* /proc is generated, read-only, and not on any disk. */
    {
        Buf pb = {0};
        if (proc_read(p->m, p, path, &pb)) {
            int pfd = alloc_fd(p);
            if (pfd < 0) { buf_free(&pb); return -1; }
            Fd *pf = &p->fd[pfd];
            memset(pf, 0, sizeof *pf);
            pf->used = true;
            snprintf(pf->path, sizeof pf->path, "%s", path);
            buf_put(&pf->data, pb.p, pb.len);
            buf_free(&pb);
            return pfd;
        }
    }

    bool dangling = false;
    VNode *n = vfs_resolve(fs, path, &dangling);
    if (!n && (flags & O_CREAT)) {
        n = vfs_mkfile(fs, path, "");
        if (!n) return -1;
    }
    if (!n || dangling) return -1;
    if (n->kind == VN_DIR) return -1;
    /* Mode is enforced for read and write, not just execute. There is no uid
     * model yet, so there is no root override -- which means `chmod 000` on a
     * config file really does break the thing that reads it, and that is a
     * fault worth being able to have. */
    bool want_write = (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) != 0;
    if (!want_write && !(n->mode & 0444)) return -1;
    if (want_write  && !(n->mode & 0222) && !(flags & O_CREAT)) return -1;

    int fd = alloc_fd(p);
    if (fd < 0) return -1;
    Fd *f = &p->fd[fd];
    memset(f, 0, sizeof *f);
    f->used = true;
    f->writable = (flags & (O_WRONLY | O_RDWR)) != 0;
    f->fs = fs;
    snprintf(f->path, sizeof f->path, "%s", path);
    if (!(flags & O_TRUNC))
        buf_put(&f->data, n->data.p, n->data.len);
    if (flags & O_APPEND) f->pos = f->data.len;
    return fd;
}

static int64_t sys_read(Proc *p, Cpu *c, int64_t fd, uint64_t buf, int64_t len)
{
    if (fd == 0) {                                 /* stdin, from a pipe */
        if (!p->stdin_from || len < 0) return 0;
        size_t left = p->stdin_from->len > p->stdin_pos
                    ? p->stdin_from->len - p->stdin_pos : 0;
        size_t n = (size_t)len < left ? (size_t)len : left;
        if (n && !cpu_write(c, buf, p->stdin_from->p + p->stdin_pos, n)) return -1;
        p->stdin_pos += n;
        return (int64_t)n;
    }
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

    if (fd == 1 || fd == 2) {
        /* A captured stdout goes to the pipe. stderr always goes to the
         * console, because an error message that vanishes into a pipe is how
         * you lose an afternoon. */
        if (fd == 1 && p->capture && p->capture_into)
            buf_put(p->capture_into, tmp, (size_t)len);
        else if (p->console)
            buf_put(p->console, tmp, (size_t)len);
        nom_free(tmp);
        return len;
    }
    if (fd < 3 || fd >= FD_MAX || !p->fd[fd].used || !p->fd[fd].writable) {
        nom_free(tmp);
        return -1;
    }
    Fd *f = &p->fd[fd];
    if (strncmp(f->path, "/proc", 5) == 0) { nom_free(tmp); return -1; }
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
        VNode *n = vfs_lookup(f->fs ? f->fs : &p->m->disk, f->path);
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
    char raw[NOM_PATH_MAX], path[NOM_PATH_MAX], name[NOM_NAME_MAX];
    if (!guest_str(c, pathp, raw, sizeof raw)) return -1;
    Vfs *fs = resolve_fs(p, raw, path, sizeof path);

    /* /proc listings come from the process table */
    {
        int pid; char leaf[64];
        if (proc_split(path, &pid, leaf, sizeof leaf, p)) {
            if (pid < 0) {                        /* ls /proc -> the pids */
                if (idx < 0 || idx >= p->m->nproc) return -1;
                snprintf(name, sizeof name, "%d", p->m->proc[idx].pid);
            } else {                              /* ls /proc/N -> its files */
                if (!proc_by_pid(p->m, pid)) return -1;
                int n = 0;
                while (PROC_FILES[n]) n++;
                if (idx < 0 || idx >= n) return -1;
                snprintf(name, sizeof name, "%s", PROC_FILES[idx]);
            }
            size_t nl = strlen(name);
            if ((int64_t)nl + 1 > len) return -1;
            return cpu_write(c, buf, name, nl + 1) ? (int64_t)nl : -1;
        }
    }

    VNode *d = vfs_resolve(fs, path, NULL);
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
    char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
    if (!guest_str(c, pathp, raw, sizeof raw)) return -1;
    Vfs *fs = resolve_fs(p, raw, path, sizeof path);

    {
        int pid; char leaf[64];
        if (proc_split(path, &pid, leaf, sizeof leaf, p)) {
            NomStat st; memset(&st, 0, sizeof st);
            st.mode = 0555;
            if (pid < 0 || !leaf[0]) { st.kind = NOM_KIND_DIR; }
            else {
                Buf pb = {0};
                if (!proc_read(p->m, p, path, &pb)) { buf_free(&pb); return -1; }
                st.kind = NOM_KIND_FILE;
                st.size = (int64_t)pb.len;
                buf_free(&pb);
            }
            return cpu_write(c, sbuf, &st, sizeof st) ? 0 : -1;
        }
    }

    VNode *ln = vfs_lookup(fs, path);
    if (!ln) return -1;
    NomStat st;
    memset(&st, 0, sizeof st);
    if (ln->kind == VN_LINK) {
        /* stat follows the link; a dangling one is a genuine failure and the
         * guest is entitled to see it as one. */
        bool dangling = false;
        VNode *t = vfs_resolve(fs, path, &dangling);
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
    case SYS_getpid:  return p->pid;
    case SYS_bind: {
        char t[NOM_PATH_MAX], at[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, t, sizeof t)) return -1;
        if (!guest_str(c, (uint64_t)a1, at, sizeof at)) return -1;
        if (!p->info) return -1;
        char ta[NOM_PATH_MAX * 2], aa[NOM_PATH_MAX * 2];
        vfs_normalize(p->info->cwd, t, ta, sizeof ta);
        vfs_normalize(p->info->cwd, at, aa, sizeof aa);
        return ns_bind(&p->info->ns, ta, aa, NULL, 0) ? 0 : -1;
    }
    case SYS_unbind: {
        char at[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, at, sizeof at)) return -1;
        if (!p->info) return -1;
        char aa[NOM_PATH_MAX * 2];
        vfs_normalize(p->info->cwd, at, aa, sizeof aa);
        return ns_unbind(&p->info->ns, aa) ? 0 : -1;
    }
    case SYS_chdir: {
        char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, raw, sizeof raw)) return -1;
        Vfs *cfs = resolve_fs(p, raw, path, sizeof path);
        int pid; char leaf[64];
        bool isproc = proc_split(path, &pid, leaf, sizeof leaf, p);
        VNode *d = isproc ? NULL : vfs_resolve(cfs, path, NULL);
        if (!isproc && (!d || d->kind != VN_DIR)) return -1;
        if (p->info) {
            char abs[NOM_PATH_MAX * 2];
            vfs_normalize(p->info->cwd, raw, abs, sizeof abs);
            snprintf(p->info->cwd, sizeof p->info->cwd, "%s", abs);
        }
        return 0;
    }
    case SYS_getcwd: {
        const char *cw = p->info ? p->info->cwd : "/";
        size_t n = strlen(cw);
        if ((int64_t)n + 1 > a1) return -1;
        return cpu_write(c, (uint64_t)a0, cw, n + 1) ? (int64_t)n : -1;
    }
    case SYS_chmod: {
        char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, raw, sizeof raw)) return -1;
        Vfs *mfs = resolve_fs(p, raw, path, sizeof path);
        VNode *n = vfs_lookup(mfs, path);
        if (!n) return -1;
        n->mode = (unsigned)(a1 & 0777);
        return 0;
    }
    case SYS_dns: {
        char name[128];
        if (!guest_str(c, (uint64_t)a0, name, sizeof name)) return -1;
        const char *ip = net_dns(name);
        if (!ip) return -1;
        size_t n = strlen(ip);
        if ((int64_t)n + 1 > a2) return -1;
        return cpu_write(c, (uint64_t)a1, ip, n + 1) ? (int64_t)n : -1;
    }
    case SYS_http: {
        char ip[64], path[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, ip, sizeof ip)) return -1;
        if (!guest_str(c, (uint64_t)a1, path, sizeof path)) return -1;
        Buf b = {0};
        int64_t r = -1;
        if (net_fetch(ip, path, &b) && b.len < (1u << 16) &&
            cpu_write(c, (uint64_t)a2, b.p, b.len + 1))
            r = (int64_t)b.len;
        buf_free(&b);
        return r;
    }
    case SYS_kill: {
        /* Leave the signal pending on the target. Nothing is interrupted --
         * there is no preemption here -- so a daemon sees it the next time it
         * looks, which is exactly what a cooperative system can promise and
         * exactly enough for "re-read your configuration". */
        struct Daemon *ds = (struct Daemon *)p->m->daemon;
        if (!ds) return -1;
        for (int i = 0; i < p->m->ndaemon; i++) {
            if (!ds[i].running) continue;
            if (ds[i].proc.info && ds[i].proc.info->pid == (int)a0) {
                ds[i].pending_sig = (int)a1;
                return 0;
            }
        }
        return -1;
    }
    case SYS_sigpend: {
        struct Daemon *ds = (struct Daemon *)p->m->daemon;
        if (!ds) return 0;
        for (int i = 0; i < p->m->ndaemon; i++) {
            if (&ds[i].proc != p) continue;
            int sig = ds[i].pending_sig;
            ds[i].pending_sig = 0;
            return sig;
        }
        return 0;
    }
    case SYS_pipe: {
        /* One stage of a pipeline. The child reads what this process's pipe
         * currently holds and writes into a fresh buffer, which then becomes
         * the pipe -- so the next stage reads this one's output. */
        char path[NOM_PATH_MAX], arg[NOM_PATH_MAX] = "";
        if (!guest_str(c, (uint64_t)a0, path, sizeof path)) return SPAWN_ENOENT;
        if (a1 && !guest_str(c, (uint64_t)a1, arg, sizeof arg)) return SPAWN_ENOENT;
        Buf next = {0};
        int64_t rc = kernel_spawn_piped(p->m, path, arg, p->console, p->depth + 1,
                                        p, &p->pipe, &next);
        buf_free(&p->pipe);
        p->pipe = next;
        return rc;
    }
    case SYS_pipeout:
        if (p->console && p->pipe.len) buf_put(p->console, p->pipe.p, p->pipe.len);
        buf_clear(&p->pipe);
        return 0;
    case SYS_svcstart: {
        char path[NOM_PATH_MAX], name[64] = "";
        if (!guest_str(c, (uint64_t)a0, path, sizeof path)) return -1;
        if (a1) guest_str(c, (uint64_t)a1, name, sizeof name);
        char rp[NOM_PATH_MAX];
        resolve(p, path, rp, sizeof rp);
        return kernel_start_daemon(p->m, rp, "", name[0] ? name : path,
                                   (int)a2, p->console);
    }
    case SYS_fsck: {
        char dev[64];
        if (!guest_str(c, (uint64_t)a0, dev, sizeof dev)) return -1;
        Buf b = {0};
        int r = machine_fsck(p->m, dev, &b);
        if (b.len < (size_t)a2 && !cpu_write(c, (uint64_t)a1, b.p, b.len + 1)) r = -1;
        buf_free(&b);
        return r;
    }
    case SYS_bootsec:
        /* a0 != 0 writes one, which is what zbl-install does */
        if (a0) p->m->bootsector = true;
        return p->m->bootsector ? 1 : 0;
    case SYS_rootuuid: {
        const char *u = p->m->root_uuid;
        size_t n = strlen(u);
        if ((int64_t)n + 1 > a1) return -1;
        return cpu_write(c, (uint64_t)a0, u, n + 1) ? (int64_t)n : -1;
    }
    case SYS_unlink: {
        char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, raw, sizeof raw)) return -1;
        Vfs *ufs = resolve_fs(p, raw, path, sizeof path);
        return vfs_remove(ufs, path) ? 0 : -1;
    }
    case SYS_readlink: {
        char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, raw, sizeof raw)) return -1;
        Vfs *lfs = resolve_fs(p, raw, path, sizeof path);
        VNode *n = vfs_lookup(lfs, path);
        if (!n || n->kind != VN_LINK) return -1;
        size_t tl = strlen(n->target);
        if ((int64_t)tl + 1 > a2) return -1;
        return cpu_write(c, (uint64_t)a1, n->target, tl + 1) ? (int64_t)tl : -1;
    }
    case SYS_mount: {
        char dev[64], raw[NOM_PATH_MAX], at[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, dev, sizeof dev)) return -1;
        if (!guest_str(c, (uint64_t)a1, raw, sizeof raw)) return -1;
        resolve_ns(p, raw, at, sizeof at);
        char rdev[NOM_PATH_MAX];
        if ((int)a2 & MNT_BIND) { resolve_ns(p, dev, rdev, sizeof rdev);
                                  snprintf(dev, sizeof dev, "%s", rdev); }
        return machine_mount(p->m, dev, at, (int)a2) ? 0 : -1;
    }
    case SYS_umount: {
        char raw[NOM_PATH_MAX], at[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, raw, sizeof raw)) return -1;
        resolve_ns(p, raw, at, sizeof at);
        return machine_umount(p->m, at) ? 0 : -1;
    }
    case SYS_chroot: {
        char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
        if (!guest_str(c, (uint64_t)a0, raw, sizeof raw)) return -1;
        /* The one escape hatch: step back out to the real root. A process
         * cannot normally leave its chroot, but the session is a person at a
         * rescue console and they must be able to put the disk down. */
        if (strcmp(raw, "//LEAVE") == 0) {
            if (!p->info || !p->info->root[0] ||
                strcmp(p->info->root, "/") == 0) return -1;
            p->info->root[0] = '\0';
            snprintf(p->info->cwd, sizeof p->info->cwd, "/");
            return 0;
        }
        Vfs *rfs = resolve_fs(p, raw, path, sizeof path);
        VNode *d = vfs_resolve(rfs, path, NULL);
        if (!d || d->kind != VN_DIR) return -1;
        if (!p->info) return -1;
        /* chroot is relative to the CURRENT root, so chrooting twice nests
         * rather than escaping -- which is the property that makes it a
         * containment tool and not just a path prefix. */
        char abs[NOM_PATH_MAX * 2], newroot[NOM_PATH_MAX * 2];
        vfs_normalize(p->info->cwd, raw, abs, sizeof abs);
        const char *cur = p->info->root[0] ? p->info->root : "/";
        if (strcmp(cur, "/") == 0) snprintf(newroot, sizeof newroot, "%s", abs);
        else snprintf(newroot, sizeof newroot, "%s%s", cur, abs);
        snprintf(p->info->root, sizeof p->info->root, "%s", newroot);
        snprintf(p->info->cwd, sizeof p->info->cwd, "/");
        return 0;
    }
    case SYS_mounts: {
        Buf b = {0};
        for (int i = 0; i < p->m->nmount; i++) {
            if (!p->m->mount[i].used) continue;
            buf_printf(&b, "%s on %s%s\n", p->m->mount[i].dev,
                       p->m->mount[i].at,
                       p->m->mount[i].sub[0] ? " (bind)" : "");
        }
        int64_t r = -1;
        if ((int64_t)b.len < a1 && cpu_write(c, (uint64_t)a0, b.p, b.len))
            r = (int64_t)b.len;
        buf_free(&b);
        return r;
    }
    case SYS_repo: {
        char pkg[64], path[NOM_PATH_MAX];
        /* Re-read the channel from the disk on every fetch, because the
         * player can edit the repo file and the next fetch must honour it. */
        machine_read_channel(p->m);
        if (!guest_str(c, (uint64_t)a0, pkg, sizeof pkg)) return -1;
        if (!guest_str(c, (uint64_t)a1, path, sizeof path)) return -1;
        Buf b = {0};
        bool ok = pkg_file_content(p->m, pkg, path, &b);
        int64_t r = -1;
        if (ok) {
            /* a2 is the buffer; its size is fixed by the ABI at 64k */
            if (b.len <= (1u << 16) && cpu_write(c, (uint64_t)a2, b.p, b.len))
                r = (int64_t)b.len;
        }
        buf_free(&b);
        return r;
    }
    case SYS_spawn: {
        char path[NOM_PATH_MAX], arg[NOM_PATH_MAX] = "";
        if (!guest_str(c, (uint64_t)a0, path, sizeof path)) return SPAWN_ENOENT;
        if (a1 && !guest_str(c, (uint64_t)a1, arg, sizeof arg)) return SPAWN_ENOENT;
        return kernel_spawn_p(p->m, path, arg, p->console, p->depth + 1, p, NULL, 0);
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

int64_t kernel_spawn_as(Machine *m, const char *path, const char *arg,
                        Buf *console, int depth, Proc *parent,
                        ProcInfo *as, char *err, size_t errsz);

/* Set on the next spawn, consumed by it. Threading two more parameters
 * through every caller of kernel_spawn_as would be worse than this, and the
 * whole thing is single-threaded. */
static Buf *g_next_stdin;
static Buf *g_next_capture;

int64_t kernel_spawn_piped(Machine *m, const char *path, const char *arg,
                           Buf *console, int depth, Proc *parent,
                           Buf *in, Buf *out)
{
    g_next_stdin = in;
    g_next_capture = out;
    int64_t rc = kernel_spawn_as(m, path, arg, console, depth, parent, NULL,
                                 NULL, 0);
    g_next_stdin = NULL;
    g_next_capture = NULL;
    return rc;
}

int64_t kernel_spawn_as(Machine *m, const char *path, const char *arg,
                        Buf *console, int depth, Proc *parent,
                        ProcInfo *as, char *err, size_t errsz)
{
    if (err && errsz) err[0] = '\0';
    if (depth > SPAWN_DEPTH)
        return spawn_fail(console, err, errsz, SPAWN_EDEPTH,
                          "%s: too many nested programs", path);

    /* The program name is looked up in the PARENT's namespace, because that
     * is who said it. A child that inherits a bind can be handed a path its
     * parent could not have resolved, and vice versa. */
    char rpath[NOM_PATH_MAX];
    Vfs *pfs;
    if (parent) {
        pfs = resolve_fs(parent, path, rpath, sizeof rpath);
    } else if (as) {
        Proc tmp; memset(&tmp, 0, sizeof tmp);
        tmp.m = m; tmp.info = as;
        pfs = resolve_fs(&tmp, path, rpath, sizeof rpath);
    } else {
        snprintf(rpath, sizeof rpath, "%s", path);
        pfs = m->on_rescue ? &m->rescue : &m->disk;
    }
    path = rpath;

    bool dangling = false;
    VNode *ln = vfs_lookup(pfs, path);
    VNode *n = vfs_resolve(pfs, path, &dangling);
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

    /* THE DYNAMIC LINKER. Before a single instruction runs, every library the
     * binary was linked against has to be present, on a path ld.so.conf names,
     * and at a compatible version. This is why a bad libc upgrade takes the
     * whole machine down at once -- including the tools you would reach for to
     * fix it, which is exactly why the rescue medium exists. */
    {
        char needs[512];
        if (cpu_elf_needs((const uint8_t *)n->data.p, n->data.len,
                          needs, sizeof needs)) {
            char lderr[NOM_ERR_MAX];
            if (!link_check(m, pfs, needs, lderr, sizeof lderr))
                return spawn_fail(console, err, errsz, SPAWN_ENOEXEC,
                                  "%s: %s", path, lderr);
        }
    }

    Cpu c;
    cpu_init(&c);
    char lerr[128] = "";
    if (!cpu_load_elf(&c, (const uint8_t *)n->data.p, n->data.len,
                      lerr, sizeof lerr)) {
        cpu_free(&c);
        return spawn_fail(console, err, errsz, SPAWN_ENOEXEC, "%s: %s", path, lerr);
    }

    /* Register the process. A pid is handed out even for a program that is
     * about to fail, because "pid 7 exited 1" is information the player wants
     * and a table that only lists successes is a lie. */
    Proc p;
    memset(&p, 0, sizeof p);
    p.m = m;
    p.console = console;
    p.depth = depth;
    snprintf(p.arg, sizeof p.arg, "%s", arg ? arg : "");
    /* Pipeline plumbing, set by kernel_spawn_piped and consumed here. */
    p.stdin_from   = g_next_stdin;
    p.capture_into = g_next_capture;
    p.capture      = (g_next_capture != NULL);

    /* Reap. The table is finite and a real session runs hundreds of commands;
     * when it filled, new processes got no ProcInfo at all and silently lost
     * their chroot and namespace, quietly operating on the wrong filesystem.
     * Exited processes are dropped oldest-first, keeping the session (ppid
     * -1) and anything still running. */
    if (!as && m->nproc >= PROC_MAX) {
        int w = 0;
        for (int i = 0; i < m->nproc; i++) {
            ProcInfo *q = &m->proc[i];
            bool keep = q->alive || q->ppid == -1 || i >= m->nproc - PROC_MAX / 2;
            if (keep) m->proc[w++] = *q;
        }
        m->nproc = w;
    }

    ProcInfo *pi = as;
    if (as) {
        /* Running AS an existing process: this is what a shell session is.
         * cd and bind then change the session's own namespace, which is the
         * only way they can persist between commands. */
        snprintf(pi->name, sizeof pi->name, "%s", path);
        snprintf(pi->arg, sizeof pi->arg, "%s", arg ? arg : "");
        pi->alive = true;
        p.pid = pi->pid;
        p.info = pi;
    } else if (m->nproc < PROC_MAX) {
        pi = &m->proc[m->nproc++];
        memset(pi, 0, sizeof *pi);
        pi->pid  = m->next_pid ? m->next_pid++ : (m->next_pid = 2, 1);
        pi->ppid = parent ? parent->pid : 0;
        pi->alive = true;
        snprintf(pi->name, sizeof pi->name, "%s", path);
        snprintf(pi->arg, sizeof pi->arg, "%s", arg ? arg : "");
        /* A child inherits its parent's view of the world and may then change
         * its own copy. That is the whole of Plan 9 namespace inheritance. */
        if (parent && parent->info) {
            ns_copy(&pi->ns, &parent->info->ns);
            snprintf(pi->cwd, sizeof pi->cwd, "%s", parent->info->cwd);
            /* and the root: a child of a chrooted process is inside the same
             * chroot, which is the entire point of chroot */
            snprintf(pi->root, sizeof pi->root, "%s", parent->info->root);
        } else {
            ns_init(&pi->ns);
            snprintf(pi->cwd, sizeof pi->cwd, "/");
        }
        p.pid = pi->pid;
        p.info = pi;
    }

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
    buf_free(&p.pipe);
    if (pi) {
        pi->alive = (as != NULL);      /* a session outlives its commands */
        pi->exit_code = (t == TRAP_EXIT) ? c.exit_code : rc;
        pi->icount = c.icount;
    }
    cpu_free(&c);
    return rc;
}

int64_t kernel_spawn_p(Machine *m, const char *path, const char *arg,
                       Buf *console, int depth, Proc *parent,
                       char *err, size_t errsz)
{
    return kernel_spawn_as(m, path, arg, console, depth, parent, NULL, err, errsz);
}

int64_t kernel_spawn(Machine *m, const char *path, const char *arg,
                     Buf *console, int depth, char *err, size_t errsz)
{
    return kernel_spawn_as(m, path, arg, console, depth, NULL, NULL, err, errsz);
}

/* The session: one long-lived process that a person is driving. Its namespace
 * and working directory persist between commands, because they belong to it
 * and not to the programs it runs. */
ProcInfo *kernel_session(Machine *m)
{
    for (int i = 0; i < m->nproc; i++)
        if (m->proc[i].ppid == -1) return &m->proc[i];
    if (m->nproc >= PROC_MAX) return NULL;
    ProcInfo *pi = &m->proc[m->nproc++];
    memset(pi, 0, sizeof *pi);
    pi->pid  = m->next_pid ? m->next_pid++ : (m->next_pid = 2, 1);
    pi->ppid = -1;                     /* marks it as the session */
    pi->alive = true;
    ns_init(&pi->ns);
    snprintf(pi->name, sizeof pi->name, "-sh");
    snprintf(pi->cwd, sizeof pi->cwd, "/");
    return pi;
}

int64_t kernel_run(Machine *m, const char *line, Buf *console)
{
    ProcInfo *ses = kernel_session(m);
    char err[NOM_ERR_MAX] = "";
    int64_t rc = kernel_spawn_as(m, "/bin/sh", line, console, 0, NULL, ses,
                                 err, sizeof err);
    /* Time passes while you work. Without this the daemons are frozen between
     * commands, so a signal sent with `kill -HUP` would sit pending forever
     * and a service could never die while you were looking at it. */
    kernel_tick(m, 2, console);
    return rc;
}

/* ------------------------------------------------------------ daemons --
 *
 * A service is not a program that runs and exits. It is a program that keeps
 * running, and everything interesting about services follows from that: it
 * can be running now and dead in ten minutes, it can be restarted, it can
 * respawn too fast, and `ps` can tell you which.
 *
 * There is no scheduler and no preemption. A daemon gets a slice of
 * instructions when the system ticks, and between slices its cpu and its
 * memory sit exactly where it left them. That is cooperative multitasking,
 * which is what this needs and a great deal less than a kernel.
 */

/* How much cpu a daemon gets to start up, and per tick afterwards. Starting
 * is generous because a daemon reads its config; running is not, because a
 * well-behaved one is mostly waiting. */
#define DAEMON_START_BUDGET  2000000ull
#define DAEMON_TICK_BUDGET     50000ull


/* How many times a service may come back before the system decides it is
 * never going to work. Real init systems all have a number like this and
 * they all print something close to the same sentence, because a daemon
 * that dies instantly and is restarted instantly is a machine that does
 * nothing else forever. */
#define RESPAWN_LIMIT 5

#define DAEMON_MAX 16

static struct Daemon *daemons(Machine *m)
{
    if (!m->daemon) {
        m->daemon = nom_alloc(sizeof(struct Daemon) * DAEMON_MAX);
        memset(m->daemon, 0, sizeof(struct Daemon) * DAEMON_MAX);
    }
    return (struct Daemon *)m->daemon;
}

/* Is this machine actually WORKING?
 *
 * Booting to a login prompt is not the same thing. A service that gave up
 * after five restarts leaves the machine up and useless in a specific way,
 * and the boot console scrolled past it twenty lines ago. Widening a ticket
 * from "will not boot" to "is not healthy" is what lets that be a ticket at
 * all -- and it is the commoner kind of call. */
/* The first line of a file that is not blank and not a comment. */
static bool first_real_line(Vfs *fs, const char *path, char *out, size_t outsz)
{
    VNode *n = vfs_resolve(fs, path, NULL);
    if (!n || n->kind != VN_FILE) return false;
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const char *t = p; size_t tl = len;
        while (tl && (*t == ' ' || *t == '\t')) { t++; tl--; }
        while (tl && (t[tl-1] == ' ' || t[tl-1] == '\r')) tl--;
        if (tl && *t != '#') {
            if (tl >= outsz) tl = outsz - 1;
            memcpy(out, t, tl);
            out[tl] = '\0';
            return true;
        }
        p = nl ? nl + 1 : NULL;
    }
    return false;
}

int kernel_health(Machine *m, Buf *out)
{
    if (!m->daemon) return 0;
    struct Daemon *d = daemons(m);
    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    int dead = 0;
    /* A daemon can be running and still wrong: the file on disk says one
     * thing and the process loaded another, because somebody edited it and
     * never reloaded. Nothing is corrupt, `pkg verify` is clean, and the
     * machine does not do what its configuration says it does. */
    for (int i = 0; i < m->ndaemon; i++) {
        if (!d[i].running) continue;
        char statepath[NOM_PATH_MAX];
        snprintf(statepath, sizeof statepath, "/run/%s.state", d[i].name);
        VNode *sn = vfs_resolve(fs, statepath, NULL);
        if (!sn || sn->kind != VN_FILE || !sn->data.len) continue;
        char confpath[NOM_PATH_MAX] = "", loaded[256] = "";
        const char *nl = memchr(sn->data.p, '\n', sn->data.len);
        if (!nl) continue;
        size_t l1 = (size_t)(nl - sn->data.p);
        if (l1 >= sizeof confpath) continue;
        memcpy(confpath, sn->data.p, l1); confpath[l1] = 0;
        const char *v = nl + 1;
        size_t vlen = sn->data.len - l1 - 1;
        while (vlen && (v[vlen-1] == '\n' || v[vlen-1] == '\r')) vlen--;
        if (vlen >= sizeof loaded) continue;
        memcpy(loaded, v, vlen); loaded[vlen] = 0;

        char ondisk[256];
        if (!first_real_line(fs, confpath, ondisk, sizeof ondisk)) continue;
        if (strcmp(ondisk, loaded) == 0) continue;
        if (!dead) buf_puts(out, "services that are not doing what they are configured to do:\n");
        buf_printf(out, "  %-14s running with a stale %s\n", d[i].name, confpath);
        buf_printf(out, "  %-14s   on disk:  %s\n", "", ondisk);
        buf_printf(out, "  %-14s   running: %s\n", "", loaded);
        dead++;
    }

    for (int i = 0; i < m->ndaemon; i++) {
        if (d[i].running) continue;
        if (!dead) buf_puts(out, "services that should be running and are not:\n");
        buf_printf(out, "  %-14s %s\n", d[i].name,
                   d[i].gave_up ? "gave up after repeated failures"
                                : (d[i].died[0] ? d[i].died : "not running"));
        dead++;
    }
    return dead;
}

void kernel_stop_daemons(Machine *m)
{
    if (!m->daemon) return;
    struct Daemon *d = daemons(m);
    for (int i = 0; i < m->ndaemon; i++) if (d[i].running) cpu_free(&d[i].cpu);
    nom_free(m->daemon);
    m->daemon = NULL;
    m->ndaemon = 0;
}

static int64_t daemon_launch(Machine *m, struct Daemon *d, Buf *console);

int64_t kernel_start_daemon(Machine *m, const char *path, const char *arg,
                            const char *name, int restart, Buf *console)
{
    struct Daemon *ds = daemons(m);
    if (m->ndaemon >= DAEMON_MAX) return SPAWN_EDEPTH;
    struct Daemon *d = &ds[m->ndaemon];
    memset(d, 0, sizeof *d);
    snprintf(d->name, sizeof d->name, "%s", name ? name : path);
    snprintf(d->path, sizeof d->path, "%s", path);
    d->restart_policy = restart;
    snprintf(d->proc.arg, sizeof d->proc.arg, "%s", arg ? arg : "");
    m->ndaemon++;
    return daemon_launch(m, d, console);
}

/* Start, or restart, one daemon. Everything about loading and running it
 * lives here so that a restart is genuinely the same operation as a start --
 * a restart that took a different path would eventually diverge from it. */
static int64_t daemon_launch(Machine *m, struct Daemon *d, Buf *console)
{
    const char *path = d->path;

    Vfs *fs = m->on_rescue ? &m->rescue : &m->disk;
    bool dangling = false;
    VNode *n = vfs_resolve(fs, path, &dangling);
    char err[NOM_ERR_MAX] = "";
    if (dangling || !n)
        return spawn_fail(console, err, sizeof err, SPAWN_ENOENT, "%s: not found", path);
    if (n->kind != VN_FILE)
        return spawn_fail(console, err, sizeof err, SPAWN_ENOENT, "%s: not a regular file", path);
    if (!(n->mode & 0111))
        return spawn_fail(console, err, sizeof err, SPAWN_EPERM,
                          "%s: permission denied (mode %04o)", path, n->mode);

    {
        char needs[512];
        if (cpu_elf_needs((const uint8_t *)n->data.p, n->data.len, needs, sizeof needs)) {
            char lderr[NOM_ERR_MAX];
            if (!link_check(m, fs, needs, lderr, sizeof lderr))
                return spawn_fail(console, err, sizeof err, SPAWN_ENOEXEC,
                                  "%s: %s", path, lderr);
        }
    }

    cpu_init(&d->cpu);
    char lerr[128] = "";
    d->died[0] = '\0';
    if (!cpu_load_elf(&d->cpu, (const uint8_t *)n->data.p, n->data.len, lerr, sizeof lerr)) {
        cpu_free(&d->cpu);
        return spawn_fail(console, err, sizeof err, SPAWN_ENOEXEC, "%s: %s", path, lerr);
    }

    /* Register it in the process table like anything else. */
    ProcInfo *pi = NULL;
    if (m->nproc < PROC_MAX) {
        pi = &m->proc[m->nproc++];
        memset(pi, 0, sizeof *pi);
        pi->pid = m->next_pid ? m->next_pid++ : (m->next_pid = 2, 1);
        pi->ppid = 1;
        pi->alive = true;
        snprintf(pi->name, sizeof pi->name, "%s", path);
        snprintf(pi->arg, sizeof pi->arg, "%s", d->proc.arg);
        ns_init(&pi->ns);
        snprintf(pi->cwd, sizeof pi->cwd, "/");
    }

    char savearg[NOM_PATH_MAX];
    snprintf(savearg, sizeof savearg, "%s", d->proc.arg);
    memset(&d->proc, 0, sizeof d->proc);
    d->proc.m = m;
    d->proc.console = console;
    d->proc.info = pi;
    d->proc.pid = pi ? pi->pid : 0;
    snprintf(d->proc.arg, sizeof d->proc.arg, "%s", savearg);
    d->cpu.syscall = kernel_syscall;
    d->cpu.ctx = &d->proc;

    CpuTrap t = cpu_run(&d->cpu, DAEMON_START_BUDGET);
    if (t == TRAP_BUDGET) {
        /* Still going: that is a daemon. */
        d->running = true;
        if (pi) pi->icount = d->cpu.icount;
        return 0;
    }
    /* It finished during startup, which for a service means it fell over. */
    if (pi) { pi->alive = false; pi->exit_code = d->cpu.exit_code; pi->icount = d->cpu.icount; }
    d->exit_code = d->cpu.exit_code;
    if (t == TRAP_EXIT)
        snprintf(d->died, sizeof d->died, "exited immediately with status %lld",
                 (long long)d->cpu.exit_code);
    else
        snprintf(d->died, sizeof d->died, "%s at pc 0x%llx",
                 cpu_trap_name(t), (unsigned long long)d->cpu.pc);
    cpu_free(&d->cpu);

    /* It fell over on startup. Bring it back if it is allowed to come back,
     * and give up out loud once it is obviously never going to work. */
    if (d->restart_policy != 0) {
        if (++d->restarts >= RESPAWN_LIMIT) {
            d->gave_up = true;
            if (console)
                buf_printf(console,
                           "kernel: %s respawning too fast, giving up on it\n",
                           d->name);
            return SPAWN_EFAULT;
        }
        if (console)
            buf_printf(console, "kernel: %s died -- %s, restarting (%d)\n",
                       d->name, d->died, d->restarts);
        return daemon_launch(m, d, console);
    }
    return SPAWN_EFAULT;
}

void kernel_tick(Machine *m, int slices, Buf *console)
{
    if (!m->daemon) return;
    struct Daemon *d = daemons(m);
    for (int s = 0; s < slices; s++) {
        for (int i = 0; i < m->ndaemon; i++) {
            if (!d[i].running) continue;
            d[i].proc.console = console;
            CpuTrap t = cpu_run(&d[i].cpu, DAEMON_TICK_BUDGET);
            if (d[i].proc.info) d[i].proc.info->icount = d[i].cpu.icount;
            if (t == TRAP_BUDGET) continue;      /* still going, as expected */

            d[i].running = false;
            d[i].exit_code = d[i].cpu.exit_code;
            if (t == TRAP_EXIT)
                snprintf(d[i].died, sizeof d[i].died, "exited with status %lld",
                         (long long)d[i].cpu.exit_code);
            else
                snprintf(d[i].died, sizeof d[i].died, "%s at pc 0x%llx",
                         cpu_trap_name(t), (unsigned long long)d[i].cpu.pc);
            if (d[i].proc.info) {
                d[i].proc.info->alive = false;
                d[i].proc.info->exit_code = d[i].cpu.exit_code;
            }
            cpu_free(&d[i].cpu);
            if (d[i].restart_policy != 0 && !d[i].gave_up) {
                if (++d[i].restarts >= RESPAWN_LIMIT) {
                    d[i].gave_up = true;
                    if (console)
                        buf_printf(console, "kernel: %s respawning too fast, "
                                            "giving up on it\n", d[i].name);
                } else {
                    if (console)
                        buf_printf(console, "kernel: %s died -- %s, restarting (%d)\n",
                                   d[i].name, d[i].died, d[i].restarts);
                    daemon_launch(m, &d[i], console);
                }
            } else if (console) {
                buf_printf(console, "kernel: %s died -- %s\n", d[i].name, d[i].died);
            }
        }
    }
}

/* --------------------------------------------------------- the linker -- */

/* The search path, read from /etc/ld.so.conf. A library that is installed but
 * on a path nobody lists is a library that is not found -- which is a real
 * fault and a genuinely annoying one. */
static bool find_lib(Machine *m, Vfs *fs, const char *soname,
                     char *out, size_t outsz)
{
    Buf conf = {0};
    VNode *cn = vfs_resolve(fs, "/etc/ld.so.conf", NULL);
    if (cn && cn->kind == VN_FILE) buf_put(&conf, cn->data.p, cn->data.len);
    else buf_puts(&conf, "/lib\n/usr/lib\n");     /* the built-in default */

    const char *p = conf.p, *end = conf.p + conf.len;
    bool found = false;
    while (p && p < end && !found) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        char dir[NOM_PATH_MAX];
        if (len && len < sizeof dir) {
            memcpy(dir, p, len);
            dir[len] = '\0';
            char cand[NOM_PATH_MAX];
            snprintf(cand, sizeof cand, "%s/%s", dir, soname);
            VNode *ln = vfs_resolve(fs, cand, NULL);
            if (ln && ln->kind == VN_FILE) {
                snprintf(out, outsz, "%s", cand);
                found = true;
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    buf_free(&conf);
    return found;
}

/* A library declares its own version on its first line: "<stub> <name> <ver>".
 * Reading it out of the file is what makes an upgrade real -- replace the file
 * and every consumer sees the new number. */
static bool lib_version(Vfs *fs, const char *path, char *out, size_t outsz)
{
    VNode *n = vfs_resolve(fs, path, NULL);
    if (!n || n->kind != VN_FILE) return false;
    size_t len = n->data.len;
    const char *nl = memchr(n->data.p, '\n', len);
    if (nl) len = (size_t)(nl - n->data.p);
    /* the version is the last whitespace-separated word of the first line */
    size_t e = len;
    while (e && (n->data.p[e-1] == ' ' || n->data.p[e-1] == '\r')) e--;
    size_t b = e;
    while (b && n->data.p[b-1] != ' ') b--;
    if (b >= e) return false;
    size_t nl2 = e - b;
    if (nl2 >= outsz) nl2 = outsz - 1;
    memcpy(out, n->data.p + b, nl2);
    out[nl2] = '\0';
    return true;
}

static bool link_check(Machine *m, Vfs *fs, const char *needs,
                       char *err, size_t errsz)
{
    const char *p = needs;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[160];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = nl ? nl + 1 : p + strlen(p);

        char soname[80] = "", want[40] = "";
        if (sscanf(line, "%79s %39s", soname, want) < 1) continue;
        if (!soname[0]) continue;

        char path[NOM_PATH_MAX];
        if (!find_lib(m, fs, soname, path, sizeof path)) {
            snprintf(err, errsz,
                     "error while loading shared libraries: %s: "
                     "cannot open shared object file", soname);
            return false;
        }
        char have[40] = "";
        if (want[0] && lib_version(fs, path, have, sizeof have) &&
            strcmp(have, want) != 0) {
            snprintf(err, errsz,
                     "error while loading shared libraries: %s: "
                     "version %s not found (installed: %s)",
                     soname, want, have);
            return false;
        }
    }
    return true;
}

/* --------------------------------------------------------------- mount -- */

/* The block devices this machine has. The customer's disk is /dev/sda1 --
 * present as a device whether or not anything on it works, which is exactly
 * why you can rescue it. */
static Vfs *device_fs(Machine *m, const char *dev)
{
    if (strcmp(dev, "/dev/sda1") == 0 || strcmp(dev, "/dev/sda") == 0)
        return &m->disk;
    if (strcmp(dev, "/dev/sr0") == 0)
        return &m->rescue;
    return NULL;
}

bool machine_mount(Machine *m, const char *dev, const char *at, int flags)
{
    if (m->nmount >= MOUNT_MAX) return false;
    /* Mounting at / would shadow the running system with itself and make
     * every subsequent lookup nonsense. Real mount(8) allows it; here it is
     * only ever a mistake, and one that is very hard to see afterwards. */
    if (!at || strcmp(at, "/") == 0) return false;
    if (!dev || !*dev) return false;

    Vfs *fs = NULL;
    char sub[NOM_PATH_MAX] = "";
    if (flags & MNT_BIND) {
        /* A bind mount grafts a subtree of the CURRENT root, which is how
         * `mount /dev /mnt/dev` works before you chroot. */
        fs = m->on_rescue ? &m->rescue : &m->disk;
        snprintf(sub, sizeof sub, "%s", dev);
        if (!vfs_lookup(fs, dev)) return false;
    } else if (strcmp(dev, "none") == 0 || strcmp(dev, "proc") == 0 ||
               strcmp(dev, "tmpfs") == 0 || strcmp(dev, "sysfs") == 0 ||
               strcmp(dev, "devtmpfs") == 0) {
        /* A virtual filesystem has no backing device. It is recorded so that
         * `mount` and `df` show it -- which is the whole reason it is in
         * fstab -- but nothing is layered over the path, because /proc is
         * synthesised by the kernel and /tmp is already a directory. */
        fs = NULL;
    } else {
        fs = device_fs(m, dev);
        if (!fs) return false;
        /* A dirty filesystem will not mount. That is not us being awkward: it
         * is the whole reason fsck exists, and it forces the repair to happen
         * in the right order. */
        if (fs == &m->disk && m->fs_dirty) return false;
    }

    /* The mountpoint has to exist, as on any real system -- and it is looked
     * for through the mounts already in place, so /mnt/dev can be a directory
     * on the customer's disk that we mounted a moment ago. */
    Vfs *host = m->on_rescue ? &m->rescue : &m->disk;
    const char *rest = at;
    for (int i = 0; i < m->nmount; i++) {
        if (!m->mount[i].used || !m->mount[i].fs) continue;
        size_t al = strlen(m->mount[i].at);
        if (strncmp(at, m->mount[i].at, al) != 0) continue;
        if (!(al == 1 || at[al] == 0 || at[al] == '/')) continue;
        host = m->mount[i].fs;
        rest = (al == 1) ? at : at + al;
        if (!*rest) rest = "/";
    }
    VNode *mp = vfs_resolve(host, rest, NULL);
    if (!mp || mp->kind != VN_DIR) return false;

    for (int i = 0; i < m->nmount; i++)
        if (m->mount[i].used && strcmp(m->mount[i].at, at) == 0) return false;

    Mount *mt = &m->mount[m->nmount++];
    memset(mt, 0, sizeof *mt);
    mt->used = true;
    mt->fs = fs;
    snprintf(mt->at, sizeof mt->at, "%s", at);
    snprintf(mt->dev, sizeof mt->dev, "%s", dev);
    snprintf(mt->sub, sizeof mt->sub, "%s", sub);
    return true;
}

/* fsck. A filesystem marked dirty was interrupted mid-write; the metadata can
 * be rebuilt and the contents of whatever was being written cannot. So this
 * clears the flag and tells you what it could not save -- and the files it
 * lost then show up in `pkg verify`, which is the second repair. */
int machine_fsck(Machine *m, const char *dev, Buf *out)
{
    if (strcmp(dev, "/dev/sda1") != 0 && strcmp(dev, "/dev/sda") != 0) {
        buf_printf(out, "fsck: %s: not a filesystem this tool understands\n", dev);
        return -1;
    }
    if (!m->fs_dirty) {
        buf_printf(out, "%s: clean\n", dev);
        return 0;
    }
    buf_printf(out, "%s: recovering journal\n", dev);
    buf_puts(out, "Pass 1: checking inodes, blocks, and sizes\n");
    buf_puts(out, "Pass 2: checking directory structure\n");
    if (m->fs_lost > 0)
        buf_printf(out, "Pass 4: %d inode(s) with bad content, cleared\n",
                   m->fs_lost);
    buf_puts(out, "Pass 5: checking group summary information\n");
    buf_printf(out, "%s: FILE SYSTEM WAS MODIFIED\n", dev);
    if (m->fs_lost > 0)
        buf_puts(out, "\nfsck repaired the filesystem. It could not repair the\n"
                      "CONTENTS of what was being written -- check the packages.\n");
    m->fs_dirty = false;
    return 1;
}

bool machine_umount(Machine *m, const char *at)
{
    for (int i = 0; i < m->nmount; i++) {
        if (!m->mount[i].used || strcmp(m->mount[i].at, at) != 0) continue;
        for (int j = i; j < m->nmount - 1; j++) m->mount[j] = m->mount[j + 1];
        m->nmount--;
        return true;
    }
    return false;
}

/* The configured repository channel, read off the machine's own disk. A
 * machine with no repo configuration falls back to `stable`, which is the
 * safe reading and matches what pkg would do. */
void machine_read_channel(Machine *m)
{
    snprintf(m->channel, sizeof m->channel, "stable");
    Buf names = {0};
    if (vfs_list(&m->disk, "/etc/pkg/repos.d", &names) != IO_OK) {
        buf_free(&names);
        return;
    }
    const char *p = names.p, *end = names.p + names.len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        char path[NOM_PATH_MAX];
        snprintf(path, sizeof path, "/etc/pkg/repos.d/%.*s", (int)len, p);
        VNode *n = vfs_resolve(&m->disk, path, NULL);
        if (n && n->kind == VN_FILE) {
            const char *q = n->data.p, *qe = n->data.p + n->data.len;
            while (q && q < qe) {
                const char *ql = memchr(q, '\n', (size_t)(qe - q));
                size_t l = ql ? (size_t)(ql - q) : (size_t)(qe - q);
                char line[160];
                if (l < sizeof line) {
                    memcpy(line, q, l);
                    line[l] = 0;
                    char key[40] = "", val[40] = "";
                    if (sscanf(line, " %39[^= ] = %39s", key, val) == 2 &&
                        strcmp(key, "channel") == 0)
                        snprintf(m->channel, sizeof m->channel, "%s", val);
                }
                q = ql ? ql + 1 : NULL;
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    buf_free(&names);
}
