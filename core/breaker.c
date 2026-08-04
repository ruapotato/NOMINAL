/* breaker.c — the script that breaks the installation.
 *
 * This is the whole content pipeline, and it has exactly one power: it edits
 * the disk. It cannot set a flag, raise a fault, or tell the boot chain
 * anything. If a break is not visible as a difference in a file, it does not
 * exist. That constraint is what stops this game from being a symptom table,
 * so it is enforced by the signature: machine_break() takes a Machine and a
 * seed and touches nothing but m->disk (and m->bootsector, which is media).
 *
 * The `what` string it fills in is FOR THE AUTHOR. It exists so the test
 * harness can report which break it was solving. The player never sees it.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"

/* Corrupt a file in place: keep it present and plausible, change what it says.
 * Truncation, a mangled magic number and a garbled line are the three ways a
 * real file goes wrong, and they fail at visibly different places. */
static void truncate_file(Machine *m, const char *path, size_t keep)
{
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE) return;
    if (n->data.len > keep) n->data.len = keep;
}

static void smash_byte(Machine *m, const char *path, size_t at, char to)
{
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE || n->data.len <= at) return;
    n->data.p[at] = to;
}

/* Replace the first line starting with `prefix` (after indentation). Passing
 * NULL for `with` deletes the line. This is how a config gets a wrong value or
 * loses one, which is what an admin's bad afternoon actually looks like. */
static void rewrite_line(Machine *m, const char *path, const char *prefix,
                         const char *with)
{
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    size_t plen = strlen(prefix);
    bool done = false;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const char *s = p; size_t sl = len;
        while (sl && (*s == ' ' || *s == '\t')) { s++; sl--; }
        if (!done && sl >= plen && strncmp(s, prefix, plen) == 0) {
            done = true;
            if (with) { buf_puts(&out, with); buf_putc(&out, '\n'); }
        } else {
            buf_put(&out, p, len);
            buf_putc(&out, '\n');
        }
        p = nl ? nl + 1 : NULL;
    }
    buf_clear(&n->data);
    buf_put(&n->data, out.p, out.len);
    buf_free(&out);
}

static void set_mode(Machine *m, const char *path, unsigned mode)
{
    VNode *n = vfs_lookup(&m->disk, path);
    if (n) n->mode = mode;
}

/* Every break is a real edit to a real file. Each one names the stage it will
 * surface at, but only as a comment: the boot chain is never told. */
typedef void (*BreakFn)(Machine *m, Rng *r);

/* -- bootloader ------------------------------------------------------- */
static void br_cfg_gone(Machine *m, Rng *r)
{ (void)r; vfs_remove(&m->disk, "/boot/zbl/zbl.cfg"); }

static void br_cfg_no_kernel(Machine *m, Rng *r)
{ (void)r; rewrite_line(m, "/boot/zbl/zbl.cfg", "kernel", NULL); }

static void br_cfg_wrong_uuid(Machine *m, Rng *r)
{
    char line[96];
    snprintf(line, sizeof line, "  root UUID=%04llx-%04llx-a19d-5be3",
             (unsigned long long)(rng_next(r) % 0xffff),
             (unsigned long long)(rng_next(r) % 0xffff));
    rewrite_line(m, "/boot/zbl/zbl.cfg", "root", line);
}

static void br_cfg_wrong_kernel(Machine *m, Rng *r)
{ (void)r; rewrite_line(m, "/boot/zbl/zbl.cfg", "kernel", "  kernel /boot/vmlinuz-6.4.9"); }

/* -- kernel ------------------------------------------------------------ */
static void br_kernel_gone(Machine *m, Rng *r)
{ (void)r; vfs_remove(&m->disk, "/boot/vmlinuz-6.4.11"); }

static void br_kernel_truncated(Machine *m, Rng *r)
{ (void)r; truncate_file(m, "/boot/vmlinuz-6.4.11", 2); }

static void br_kernel_smashed(Machine *m, Rng *r)
{ smash_byte(m, "/boot/vmlinuz-6.4.11", 1 + (size_t)(rng_next(r) % 3), 'x'); }

/* -- initrd ------------------------------------------------------------ */
static void br_initrd_no_driver(Machine *m, Rng *r)
{ (void)r; rewrite_line(m, "/boot/initrd-6.4.11", "module virtio_blk", NULL); }

static void br_initrd_no_fs(Machine *m, Rng *r)
{ (void)r; rewrite_line(m, "/boot/initrd-6.4.11", "module ext4", NULL); }

static void br_initrd_gone(Machine *m, Rng *r)
{ (void)r; vfs_remove(&m->disk, "/boot/initrd-6.4.11"); }

static void br_initrd_truncated(Machine *m, Rng *r)
{ (void)r; truncate_file(m, "/boot/initrd-6.4.11", 3); }

/* -- init -------------------------------------------------------------- */
static void br_init_gone(Machine *m, Rng *r)
{ (void)r; vfs_remove(&m->disk, "/usr/lib/sysinit/sysinit"); }

static void br_init_not_exec(Machine *m, Rng *r)
{ (void)r; set_mode(m, "/usr/lib/sysinit/sysinit", 0644); }

static void br_fstab_bad_uuid(Machine *m, Rng *r)
{
    char line[96];
    snprintf(line, sizeof line, "UUID=%04llx-2c07-a19d-5be3  /var   ext4  defaults",
             (unsigned long long)(rng_next(r) % 0xffff));
    rewrite_line(m, "/etc/fstab", "/dev/sda2", line);
}

static void br_fstab_gone(Machine *m, Rng *r)
{ (void)r; vfs_remove(&m->disk, "/etc/fstab"); }

/* -- services ---------------------------------------------------------- */
static void br_svc_exec_gone(Machine *m, Rng *r)
{ (void)r; vfs_remove(&m->disk, "/usr/sbin/syslogd"); }

static void br_svc_not_exec(Machine *m, Rng *r)
{ (void)r; set_mode(m, "/usr/sbin/netd", 0644); }

static void br_svc_dangling_dep(Machine *m, Rng *r)
{ (void)r; rewrite_line(m, "/etc/init/network.service", "after", "after=sysloggd"); }

static void br_svc_cycle(Machine *m, Rng *r)
{ (void)r; rewrite_line(m, "/etc/init/mount-local.service", "exec",
                        "exec=/usr/sbin/mount-all\nafter=network"); }

/* -- media ------------------------------------------------------------- */
static void br_no_bootsector(Machine *m, Rng *r)
{ (void)r; m->bootsector = false; }

static const struct { BreakFn fn; const char *desc; } BREAKS[] = {
    { br_cfg_gone,          "bootloader config deleted" },
    { br_cfg_no_kernel,     "bootloader config lost its kernel line" },
    { br_cfg_wrong_uuid,    "bootloader points at a root uuid that does not exist" },
    { br_cfg_wrong_kernel,  "bootloader points at a kernel version not installed" },
    { br_kernel_gone,       "kernel image deleted" },
    { br_kernel_truncated,  "kernel image truncated" },
    { br_kernel_smashed,    "kernel image magic corrupted" },
    { br_initrd_no_driver,  "initrd rebuilt without the root device driver" },
    { br_initrd_no_fs,      "initrd rebuilt without the ext4 module" },
    { br_initrd_gone,       "initrd deleted" },
    { br_initrd_truncated,  "initrd truncated" },
    { br_init_gone,         "sysinit binary deleted" },
    { br_init_not_exec,     "sysinit binary lost its execute bit" },
    { br_fstab_bad_uuid,    "fstab names a uuid that does not exist" },
    { br_fstab_gone,        "fstab deleted" },
    { br_svc_exec_gone,     "syslogd binary deleted" },
    { br_svc_not_exec,      "netd lost its execute bit" },
    { br_svc_dangling_dep,  "network.service depends on a unit that does not exist" },
    { br_svc_cycle,         "mount-local and network depend on each other" },
    { br_no_bootsector,     "boot sector wiped" },
};
#define NBREAKS ((int)(sizeof BREAKS / sizeof BREAKS[0]))

bool machine_break(Machine *m, uint64_t seed, char *what, size_t whatsz)
{
    Rng r; rng_seed(&r, seed ^ 0x9e3779b97f4a7c15ULL);
    int pick = (int)(rng_next(&r) % (uint64_t)NBREAKS);
    BREAKS[pick].fn(m, &r);
    if (what) snprintf(what, whatsz, "%s", BREAKS[pick].desc);
    return true;
}

int machine_break_count(void) { return NBREAKS; }
const char *machine_break_desc(int i)
{ return (i >= 0 && i < NBREAKS) ? BREAKS[i].desc : "?"; }
