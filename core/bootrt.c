/* bootrt.c — running real userland.
 *
 * The line this file draws, and it is the honest one: firmware, the
 * bootloader, the kernel and the initrd are native code, because on a real
 * machine they are. Everything from PID 1 upward is a REAL PROGRAM ON THE
 * DISK, compiled and executed by the same VM the player writes scripts with.
 *
 * So /sbin/init is not a C function that pretends to be init. It is a file.
 * The kernel reads it, compiles it, and runs it. It really opens /etc/fstab,
 * really iterates /etc/services.d, really starts things. Corrupt it and the
 * failure is a genuine parse error at a genuine line, produced by an
 * interpreter that was actually trying to run the damaged code.
 *
 * That is the difference between simulating a boot and having one.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "lang.h"
#include "machine.h"

/* An rc script that never finishes is a real way for a machine to hang, so it
 * gets a real budget rather than an infinite loop in the host. */
#define BOOT_BUDGET 400000

typedef struct {
    Machine *m;
    Buf     *console;
    char     err[NOM_ERR_MAX];
    bool     failed;
    int      budget_left;
} BootRun;

static BootRun *RUN;   /* the VM hook has no user pointer of its own */

static bool run_file(VM *parent, const char *path);

/* mount(what, where). The machine knows what its root partition actually is;
 * an fstab that names something else fails here, exactly as it would on a
 * real box waiting for a device that never appears. */
static bool mount_hook(VM *v, const char *what, const char *where,
                       char *err, size_t errsz)
{
    (void)v;
    Machine *m = RUN->m;
    if (strncmp(what, "UUID=", 5) == 0) {
        if (strcmp(what + 5, m->root_uuid) != 0) {
            snprintf(err, errsz, "%s: no device with that uuid (waiting for %s)",
                     what, where);
            return false;
        }
        return true;
    }
    /* virtual filesystems have no device to find */
    if (strcmp(what, "none") == 0 || strcmp(what, "proc") == 0 ||
        strcmp(what, "tmpfs") == 0)
        return true;
    if (strncmp(what, "/dev/", 5) == 0) {
        snprintf(err, errsz, "%s: no such block device", what);
        return false;
    }
    snprintf(err, errsz, "%s: unrecognised device specification", what);
    return false;
}

/* svc(path) — start a service. The unit's exec has to be a real, executable
 * file, which is why chmod damage takes a machine down. */
static bool svc_hook(VM *v, const char *exec, char *err, size_t errsz)
{
    (void)v;
    Machine *m = RUN->m;
    bool dangling = false;
    VNode *n = vfs_resolve(&m->disk, exec, &dangling);
    if (dangling || !n)      { snprintf(err, errsz, "%s: not found", exec); return false; }
    if (n->kind != VN_FILE)  { snprintf(err, errsz, "%s: not a regular file", exec); return false; }
    if (!(n->mode & 0111))   { snprintf(err, errsz, "%s: permission denied (mode %04o)",
                                        exec, n->mode); return false; }
    return true;
}

/* Compile and execute one script out of the machine's own filesystem. Every
 * failure mode here is one a real system has: the file is gone, it is not
 * executable, it does not compile, it faults at runtime, or it never
 * terminates. */
static bool exec_script(const char *path, int depth)
{
    Machine *m = RUN->m;

    bool dangling = false;
    VNode *n = vfs_resolve(&m->disk, path, &dangling);
    if (dangling) {
        VNode *ln = vfs_lookup(&m->disk, path);
        snprintf(RUN->err, sizeof RUN->err, "%s -> %s: no such file",
                 path, ln ? ln->target : "?");
        return false;
    }
    if (!n) {
        snprintf(RUN->err, sizeof RUN->err, "%s: not found", path);
        return false;
    }
    if (n->kind != VN_FILE) {
        snprintf(RUN->err, sizeof RUN->err, "%s: not a regular file", path);
        return false;
    }
    if (!(n->mode & 0111)) {
        snprintf(RUN->err, sizeof RUN->err, "%s: permission denied (mode %04o)",
                 path, n->mode);
        return false;
    }

    /* The VM wants a NUL-terminated source. A truncated file has no
     * terminator, so copy rather than pointing at the raw buffer. */
    char *src = nom_alloc(n->data.len + 1);
    if (n->data.len) memcpy(src, n->data.p, n->data.len);
    src[n->data.len] = '\0';

    char cerr[NOM_ERR_MAX];
    Prog *prog = prog_compile(src, path, cerr, sizeof cerr);
    nom_free(src);
    if (!prog) {
        /* prog_compile already says "parse error [line N]: ..." */
        snprintf(RUN->err, sizeof RUN->err, "%s: %s", path, cerr);
        return false;
    }

    VM *v = vm_new(prog, &m->disk, NULL);
    v->console    = RUN->console;
    v->run_script = run_file;
    v->mount_hook = mount_hook;
    v->svc_hook   = svc_hook;
    v->depth      = depth;

    VmStatus st = vm_run(v, RUN->budget_left);
    RUN->budget_left -= (int)vm_steps(v);
    bool ok = false;

    if (st == VM_OK) {
        ok = true;
    } else if (st == VM_ERROR) {
        const char *e = vm_err(v);
        /* A nested exec already reported the real cause; do not bury it. */
        if (strncmp(e, "exec: ", 6) != 0)
            snprintf(RUN->err, sizeof RUN->err, "%s:%d: %s", path, vm_line(v), e);
    } else if (st == VM_YIELD || RUN->budget_left <= 0) {
        snprintf(RUN->err, sizeof RUN->err,
                 "%s: still running after %d instructions -- giving up",
                 path, BOOT_BUDGET);
    } else {
        /* blocked on a device that will never produce anything */
        snprintf(RUN->err, sizeof RUN->err, "%s:%d: blocked on %s",
                 path, vm_line(v), v->blocked_on[0] ? v->blocked_on : "a device");
    }

    vm_free(v);
    prog_free(prog);
    return ok;
}

static bool run_file(VM *parent, const char *path)
{
    return exec_script(path, parent->depth + 1);
}

/* Entry point used by machine_boot once the kernel is up. */
bool boot_userland(Machine *m, const char *initpath, Buf *console,
                   char *err, size_t errsz)
{
    BootRun run = { m, console, {0}, false, BOOT_BUDGET };
    RUN = &run;
    bool ok = exec_script(initpath, 0);
    RUN = NULL;
    if (!ok) snprintf(err, errsz, "%s", run.err);
    return ok;
}
