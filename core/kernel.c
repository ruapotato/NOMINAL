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
    int      pid;            /* index into m->proc                       */
    ProcInfo *info;          /* our own row: cwd and namespace live here */
};

/* Turn whatever a program said into an absolute path in ITS namespace.
 * Relative paths are resolved against the process's cwd first, because a
 * process that has chdir'd somewhere expects "ls" to mean where it is. */
static void resolve(Proc *p, const char *in, char *out, size_t outsz)
{
    char abs[NOM_PATH_MAX * 2];
    vfs_normalize(p->info ? p->info->cwd : "/", in, abs, sizeof abs);
    if (p->info) ns_resolve(&p->info->ns, abs, out, outsz);
    else         snprintf(out, outsz, "%s", abs);
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
    resolve(p, raw, path, sizeof path);

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
    char raw[NOM_PATH_MAX], path[NOM_PATH_MAX], name[NOM_NAME_MAX];
    if (!guest_str(c, pathp, raw, sizeof raw)) return -1;
    resolve(p, raw, path, sizeof path);

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
    char raw[NOM_PATH_MAX], path[NOM_PATH_MAX];
    if (!guest_str(c, pathp, raw, sizeof raw)) return -1;
    resolve(p, raw, path, sizeof path);

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
        resolve(p, raw, path, sizeof path);
        int pid; char leaf[64];
        bool isproc = proc_split(path, &pid, leaf, sizeof leaf, p);
        VNode *d = isproc ? NULL : vfs_resolve(&p->m->disk, path, NULL);
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
        resolve(p, raw, path, sizeof path);
        VNode *n = vfs_lookup(&p->m->disk, path);
        if (!n) return -1;
        n->mode = (unsigned)(a1 & 0777);
        return 0;
    }
    case SYS_repo: {
        char pkg[64], path[NOM_PATH_MAX];
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
    if (parent) resolve(parent, path, rpath, sizeof rpath);
    else        snprintf(rpath, sizeof rpath, "%s", path);
    path = rpath;

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

    /* Register the process. A pid is handed out even for a program that is
     * about to fail, because "pid 7 exited 1" is information the player wants
     * and a table that only lists successes is a lie. */
    Proc p;
    memset(&p, 0, sizeof p);
    p.m = m;
    p.console = console;
    p.depth = depth;
    snprintf(p.arg, sizeof p.arg, "%s", arg ? arg : "");

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
    return kernel_spawn_as(m, "/bin/sh", line, console, 0, NULL, ses,
                           err, sizeof err);
}
