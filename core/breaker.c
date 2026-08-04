/* breaker.c — corrupt the customer's machine at random until it won't boot.
 *
 * There is no list of faults here and there must never be one. The previous
 * version of this file had twenty hand-written breaks, which is a fault table
 * wearing a costume: a player who meets it twice has learned a lookup.
 *
 * What happens instead: pick a file at random, damage it at random, boot the
 * machine, and keep going until it stops booting. The engine validates its own
 * content by running it. Every ticket is a fresh failure nobody authored, and
 * solvability is structural — any difference from what the package shipped is
 * visible to `pkg verify`, whatever the difference is.
 *
 * The only power this file has is to edit the disk. It cannot raise a flag or
 * tell the boot chain anything, because there is nothing to tell.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"

#define PATHS_MAX 128

/* Collect every file and symlink on the disk. Directories are excluded: the
 * interesting damage is to contents, and a game that randomly deletes /etc is
 * not a puzzle, it is a coin flip. */
typedef struct {
    char  path[PATHS_MAX][NOM_PATH_MAX];
    int   n;
} PathSet;

static void collect(VNode *n, const char *prefix, PathSet *ps)
{
    for (VNode *k = n->child; k; k = k->next) {
        char p[NOM_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", prefix, k->name);
        if (k->kind == VN_DIR) {
            collect(k, p, ps);
        } else if (ps->n < PATHS_MAX) {
            snprintf(ps->path[ps->n++], NOM_PATH_MAX, "%s", p);
        }
    }
}

/* --- the mutations ----------------------------------------------------
 * Each is a thing that genuinely happens to a file: a disk truncates a
 * write, a bad block flips bytes, an admin fat-fingers a config, a package
 * script deletes the wrong path, a chmod goes wide. None of them knows or
 * cares what the file is for.
 */

static void mut_delete(Vfs *fs, const char *path, Rng *r, char *d, size_t ds)
{
    (void)r;
    vfs_remove(fs, path);
    snprintf(d, ds, "deleted %s", path);
}

static void mut_truncate(Vfs *fs, const char *path, Rng *r, char *d, size_t ds)
{
    VNode *n = vfs_lookup(fs, path);
    if (!n || n->kind != VN_FILE || n->data.len == 0) return;
    size_t keep = (size_t)(rng_next(r) % n->data.len);
    n->data.len = keep;
    snprintf(d, ds, "truncated %s to %d bytes", path, (int)keep);
}

static void mut_flip(Vfs *fs, const char *path, Rng *r, char *d, size_t ds)
{
    VNode *n = vfs_lookup(fs, path);
    if (!n || n->kind != VN_FILE || n->data.len == 0) return;
    int runs = 1 + (int)(rng_next(r) % 4);
    for (int i = 0; i < runs; i++) {
        size_t at = (size_t)(rng_next(r) % n->data.len);
        n->data.p[at] = (char)(0x20 + (rng_next(r) % 0x5f));
    }
    snprintf(d, ds, "corrupted %d bytes of %s", runs, path);
}

static void mut_zero(Vfs *fs, const char *path, Rng *r, char *d, size_t ds)
{
    VNode *n = vfs_lookup(fs, path);
    if (!n || n->kind != VN_FILE) return;
    int len = (int)n->data.len;
    size_t at = n->data.len ? (size_t)(rng_next(r) % n->data.len) : 0;
    for (size_t i = at; i < n->data.len; i++) n->data.p[i] = '\0';
    snprintf(d, ds, "nulled %s from byte %d of %d", path, (int)at, len);
}

/* Line surgery: the shape of damage that config files actually suffer. */
typedef enum { L_DROP, L_DUP, L_SWAP, L_TYPO, L_JUNK } LineOp;

static void mut_line(Vfs *fs, const char *path, Rng *r, char *d, size_t ds)
{
    VNode *n = vfs_lookup(fs, path);
    if (!n || n->kind != VN_FILE || n->data.len == 0) return;

    /* split into lines */
    char line[64][160];
    int nl = 0;
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    while (p < end && nl < 64) {
        const char *e = memchr(p, '\n', (size_t)(end - p));
        size_t len = e ? (size_t)(e - p) : (size_t)(end - p);
        if (len > 159) len = 159;
        memcpy(line[nl], p, len);
        line[nl][len] = '\0';
        nl++;
        p = e ? e + 1 : end;
    }
    if (nl == 0) return;

    LineOp op = (LineOp)(rng_next(r) % 5);
    int i = (int)(rng_next(r) % (uint64_t)nl);
    const char *opname = "?";
    switch (op) {
    case L_DROP:
        for (int k = i; k < nl - 1; k++) memcpy(line[k], line[k+1], 160);
        nl--;
        opname = "deleted line";
        break;
    case L_DUP:
        if (nl >= 64) return;
        for (int k = nl; k > i; k--) memcpy(line[k], line[k-1], 160);
        nl++;
        opname = "duplicated line";
        break;
    case L_SWAP: {
        if (nl < 2) return;
        int j = (int)(rng_next(r) % (uint64_t)nl);
        if (j == i) j = (i + 1) % nl;
        char t[160];
        memcpy(t, line[i], 160);
        memcpy(line[i], line[j], 160);
        memcpy(line[j], t, 160);
        opname = "swapped lines";
        break;
    }
    case L_TYPO: {
        /* One character, in place. The most human failure there is, and the
         * hardest to see, because the file still looks completely normal. */
        size_t len = strlen(line[i]);
        if (len == 0) return;
        size_t at = (size_t)(rng_next(r) % len);
        static const char POOL[] = "abcdefghijklmnopqrstuvwxyz0123456789-_/.";
        line[i][at] = POOL[rng_next(r) % (sizeof POOL - 1)];
        opname = "typo in line";
        break;
    }
    case L_JUNK: {
        if (nl >= 64) return;
        for (int k = nl; k > i; k--) memcpy(line[k], line[k-1], 160);
        int len = 3 + (int)(rng_next(r) % 12);
        for (int k = 0; k < len; k++)
            line[i][k] = (char)(0x21 + (rng_next(r) % 0x5e));
        line[i][len] = '\0';
        nl++;
        opname = "inserted junk line";
        break;
    }
    }

    buf_clear(&n->data);
    for (int k = 0; k < nl; k++) {
        buf_puts(&n->data, line[k]);
        buf_putc(&n->data, '\n');
    }
    snprintf(d, ds, "%s %d of %s", opname, i + 1, path);
}

static void mut_mode(Vfs *fs, const char *path, Rng *r, char *d, size_t ds)
{
    VNode *n = vfs_lookup(fs, path);
    if (!n) return;
    static const unsigned MODES[] = { 0644, 0600, 0000, 0444, 0755 };
    unsigned mode = MODES[rng_next(r) % (sizeof MODES / sizeof MODES[0])];
    if (mode == n->mode) return;
    n->mode = mode;
    snprintf(d, ds, "chmod %04o %s", mode, path);
}

static void mut_relink(Vfs *fs, const char *path, Rng *r, char *d, size_t ds)
{
    VNode *n = vfs_lookup(fs, path);
    if (!n || n->kind != VN_LINK) return;
    char t[NOM_PATH_MAX];
    int room = (int)sizeof t - 8;
    snprintf(t, sizeof t, "%.*s.%llu", room, n->target,
             (unsigned long long)(rng_next(r) % 100));
    snprintf(n->target, sizeof n->target, "%s", t);
    snprintf(d, ds, "repointed %s -> %s", path, t);
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
        const char *s2 = p; size_t sl = len;
        while (sl && (*s2 == ' ' || *s2 == '\t')) { s2++; sl--; }
        if (!done && sl >= plen && strncmp(s2, prefix, plen) == 0) {
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

/* --- faults that are not "a file changed" -------------------------------
 * These exist because the repair has to be different. If every ticket ends
 * in `pkg reinstall`, the player has learned one move and the game is over.
 */

/* The boot sector is not a file. No package owns it, verify cannot see it,
 * and reinstalling everything on the disk will not put it back. The fix is
 * zbl-install. */
static void fault_bootsector(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    if (!m->bootsector) return;
    m->bootsector = false;
    snprintf(d, ds, "wiped the boot sector (not a file: verify cannot see it)");
}

/* A stray unit nobody installed. It is not in any manifest, so `pkg verify`
 * reports a completely clean machine -- and svcinit still refuses to finish
 * because the unit says it is critical. The fix is to work out that no
 * package owns it and delete it. */
static void fault_stray_unit(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *NAMES[] = { "zz-monitoring", "vendor-agent",
                                   "backup-helper", "site-check" };
    const char *nm = NAMES[rng_next(r) % 4];
    char path[NOM_PATH_MAX], body[512];
    snprintf(path, sizeof path, "/etc/services.d/%s.svc", nm);
    if (vfs_lookup(&m->disk, path)) return;
    snprintf(body, sizeof body,
             "# dropped in by the vendor installer, %s\n"
             "name: %s\n"
             "exec: /opt/%s/bin/agent\n"
             "description: %s agent\n"
             "critical: yes\n"
             "enabled: yes\n"
             "runlevel: 3 5\n", nm, nm, nm, nm);
    VNode *n = vfs_mkfile(&m->disk, path, body);
    if (n) n->mode = 0644;
    snprintf(d, ds, "added a stray unit %s owned by no package", path);
}

/* A well-formed uuid that is simply not this disk's. Every file is legal,
 * nothing is corrupt in any way a hash can see -- but zbl.cfg is generated
 * content, so verify DOES catch it. The interesting part is the repair:
 * reinstalling zbl writes the config for the machine the package was built
 * for, and zbl-mkconfig writes one for the machine in front of you. */
static void fault_wrong_uuid(Machine *m, Rng *r, char *d, size_t ds)
{
    char line[96];
    snprintf(line, sizeof line, "  root UUID=%04llx-%04llx-%04llx-%04llx",
             (unsigned long long)(rng_next(r) % 0xffff),
             (unsigned long long)(rng_next(r) % 0xffff),
             (unsigned long long)(rng_next(r) % 0xffff),
             (unsigned long long)(rng_next(r) % 0xffff));
    rewrite_line(m, "/boot/zbl/zbl.cfg", "root", line);
    snprintf(d, ds, "pointed zbl.cfg at a uuid this disk does not have");
}

/* A module removed from /lib/modules. The initrd on disk still has it listed,
 * so the machine boots -- until something rebuilds the initrd. Left here as a
 * SECOND fault it pairs with, never alone. */
static void fault_missing_module(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *MODS[] = { "virtio_blk", "ext4", "dm_mod" };
    const char *mod = MODS[rng_next(r) % 3];
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/lib/modules/6.4.11/%s.ko", mod);
    if (!vfs_lookup(&m->disk, path)) return;
    vfs_remove(&m->disk, path);
    rewrite_line(m, "/boot/initrd-6.4.11", "module", NULL);
    snprintf(d, ds, "removed module %s and dropped a line from the initrd", mod);
}

/* A bad libc upgrade. Everything dynamically linked stops working at once --
 * including every tool on the disk you would use to fix it. There is no way
 * back except the rescue medium, which carries its own libc, and `pkg --root`,
 * which repairs a filesystem without chrooting into it. */
static void fault_bad_libc(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *VERS[] = { "2.41", "2.39", "2.35", "2.28" };
    const char *v = VERS[rng_next(r) % 4];
    VNode *n = vfs_lookup(&m->disk, "/lib/libc.so.6");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_printf(&n->data, "stub libc %s\n", v);
    snprintf(d, ds, "upgraded libc to %s, which nothing on the disk is built for", v);
}

/* A package built for the wrong architecture. The file is a perfectly valid
 * ELF -- it is simply not machine code this cpu can execute. */
static void fault_wrong_arch(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *VICTIMS[] = {
        "/usr/sbin/syslogd", "/usr/sbin/netd", "/usr/sbin/udevd",
        "/sbin/svcinit", "/bin/rc", "/usr/sbin/nft",
    };
    const char *path = VICTIMS[rng_next(r) % 6];
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE || n->data.len < 20) return;
    /* e_machine lives at offset 18. 62 is x86-64, 183 is aarch64. */
    unsigned m2 = (rng_next(r) % 2) ? 62 : 183;
    n->data.p[18] = (char)(m2 & 0xff);
    n->data.p[19] = (char)(m2 >> 8);
    snprintf(d, ds, "replaced %s with a build for the wrong architecture", path);
}

/* A library removed from the search path entirely: installed, but nowhere
 * ld.so.conf looks. */
static void fault_ldsoconf(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/ld.so.conf");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_puts(&n->data, "/usr/lib\n/usr/local/lib\n");
    snprintf(d, ds, "dropped /lib from ld.so.conf");
}

/* The machine comes all the way up, starts every service, and there is no way
 * to log in. "It booted" and "it works" are not the same sentence, and a whole
 * class of real tickets lives in the gap. */
static void fault_bad_shell(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *SHELLS[] = {
        "/bin/hamsh",      /* a shell that used to exist                  */
        "/usr/bin/zsh",    /* one that was never installed                */
        "/bin/sh.old",     /* a rename that was meant to be temporary     */
        "",                /* the field left empty entirely               */
    };
    const char *sh = SHELLS[rng_next(r) % 4];
    VNode *n = vfs_lookup(&m->disk, "/etc/passwd");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 5 && strncmp(p, "root:", 5) == 0) {
            /* rewrite only the last field, so the line still parses */
            size_t last = len;
            while (last && p[last - 1] != ':') last--;
            buf_put(&out, p, last);
            buf_puts(&out, sh);
        } else {
            buf_put(&out, p, len);
        }
        buf_putc(&out, '\n');
        p = nl ? nl + 1 : NULL;
    }
    buf_clear(&n->data);
    buf_put(&n->data, out.p, out.len);
    buf_free(&out);
    snprintf(d, ds, "set root's login shell to %s", sh[0] ? sh : "(nothing)");
}

/* The root account gone from passwd entirely. */
static void fault_no_root(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/passwd");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    bool dropped = false;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 5 && strncmp(p, "root:", 5) == 0) { dropped = true; }
        else { buf_put(&out, p, len); buf_putc(&out, '\n'); }
        p = nl ? nl + 1 : NULL;
    }
    if (dropped) {
        buf_clear(&n->data);
        buf_put(&n->data, out.p, out.len);
        snprintf(d, ds, "removed the root account from /etc/passwd");
    }
    buf_free(&out);
}

/* An unclean shutdown. The filesystem is marked dirty and whatever was being
 * written at the time is damaged. Nothing will mount it until fsck has run --
 * so the repair happens in two stages, in order, and the first one has to
 * happen before the player can even look at the disk. */
static void fault_unclean_shutdown(Machine *m, Rng *r, char *d, size_t ds)
{
    m->fs_dirty = true;

    /* The file that was mid-write when the power went. Config files are the
     * realistic casualty: they are what gets rewritten. */
    static const char *INFLIGHT[] = {
        "/etc/services.d/syslog.svc", "/etc/fstab", "/etc/passwd",
        "/var/lib/pkg/sysinit/files", "/etc/rc.conf", "/etc/ld.so.conf",
    };
    const char *path = INFLIGHT[rng_next(r) % 6];
    VNode *n = vfs_lookup(&m->disk, path);
    if (n && n->kind == VN_FILE && n->data.len > 4) {
        size_t keep = (size_t)(rng_next(r) % (n->data.len / 2));
        n->data.len = keep;              /* a half-written file */
        m->fs_lost = 1;
        snprintf(d, ds, "unclean shutdown: fs marked dirty, %s left half-written",
                 path);
    } else {
        m->fs_lost = 0;
        snprintf(d, ds, "unclean shutdown: filesystem marked dirty");
    }
}

/* Someone pointed the repository at the pre-release channel and ran an
 * upgrade. Every file that arrived is a perfectly valid, correctly signed,
 * up-to-date file -- and nothing installed on this machine is built against
 * it. `pkg verify` reports the library as CHANGED, reinstalling fetches the
 * same wrong version straight back, and the fault is three lines away in a
 * config file nobody thinks to look at. */
static void fault_wrong_channel(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/pkg/repos.d/main.repo");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_puts(&n->data,
        "# the repository this machine is built from.\n"
        "# channels: stable (11.4) | testing (12.0-pre)\n"
        "name = main\n"
        "channel = testing\n"
        "url = https://packages.hamnix.org/12.0-pre\n");
    snprintf(m->channel, sizeof m->channel, "testing");

    /* and the upgrade that was run afterwards */
    VNode *libc = vfs_lookup(&m->disk, "/lib/libc.so.6");
    if (libc && libc->kind == VN_FILE) {
        buf_clear(&libc->data);
        buf_puts(&libc->data, "stub libc 2.41\n");
    }
    snprintf(d, ds, "repo pointed at testing and upgraded: libc is 12.0's");
}

/* fstab faults. The file is read at every boot by /sbin/mountall, so these
 * are not cosmetic: a line that names a device this machine does not have
 * stops the boot dead, and the fix is usually to delete a line rather than to
 * restore a file. */
static void fault_fstab(Machine *m, Rng *r, char *d, size_t ds)
{
    VNode *n = vfs_lookup(&m->disk, "/etc/fstab");
    if (!n || n->kind != VN_FILE) return;

    switch (rng_next(r) % 4) {
    case 0:
        /* A disk somebody added and then removed, left in fstab. The
         * commonest fstab fault there is. */
        buf_puts(&n->data, "/dev/sdb1                       /backup ext4  defaults\n");
        snprintf(d, ds, "fstab entry for /dev/sdb1, a disk that is not there");
        return;
    case 1: {
        /* noauto dropped from the optical drive, so the boot waits for a
         * disc that is not in it. */
        Buf out = {0};
        const char *p = n->data.p, *end = n->data.p + n->data.len;
        while (p && p < end) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
            if (len > 8 && strncmp(p, "/dev/sr0", 8) == 0)
                buf_puts(&out, "/dev/sr0                        /media iso9660 defaults");
            else
                buf_put(&out, p, len);
            buf_putc(&out, '\n');
            p = nl ? nl + 1 : NULL;
        }
        buf_clear(&n->data);
        buf_put(&n->data, out.p, out.len);
        buf_free(&out);
        snprintf(d, ds, "removed noauto from the optical drive's fstab entry");
        return;
    }
    case 2:
        /* A mount point that is a file, not a directory. */
        vfs_remove(&m->disk, "/media");
        {
            VNode *f = vfs_mkfile(&m->disk, "/media", "this was meant to be a directory\n");
            if (f) f->mode = 0644;
        }
        buf_puts(&n->data, "/dev/sr0                        /media iso9660 defaults\n");
        snprintf(d, ds, "/media replaced with a file, and fstab mounts onto it");
        return;
    default:
        /* A line missing its type field, which the parser rejects. */
        buf_puts(&n->data, "/dev/sdb1  /data\n");
        snprintf(d, ds, "fstab line missing its filesystem type");
        return;
    }
}

/* A daemon's configuration file removed. The binary is present, correct and
 * executable -- `pkg verify` says the package is fine except for one config,
 * and the service still will not start, because it reads that config and
 * gives up. This only became possible when services became real programs. */
static void fault_daemon_config(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *CONFS[] = {
        "/etc/net/interfaces", "/etc/udev/rules.d/50-default.rules",
        "/etc/syslog.conf", "/etc/nftables.conf", "/etc/audit/auditd.conf",
        "/etc/ntp.conf", "/etc/httpd/httpd.conf",
    };
    const char *path = CONFS[rng_next(r) % 7];
    if (!vfs_lookup(&m->disk, path)) return;
    vfs_remove(&m->disk, path);
    snprintf(d, ds, "removed %s, so the daemon that reads it will not start", path);
}

typedef void (*StructuralFault)(Machine *, Rng *, char *, size_t);
static const StructuralFault STRUCTURAL[] = {
    fault_bootsector, fault_stray_unit, fault_wrong_uuid, fault_missing_module,
    fault_bad_libc, fault_wrong_arch, fault_ldsoconf,
    fault_bad_shell, fault_no_root, fault_unclean_shutdown,
    fault_wrong_channel, fault_fstab, fault_daemon_config,
};
#define NSTRUCT ((int)(sizeof STRUCTURAL / sizeof STRUCTURAL[0]))

typedef void (*Mutation)(Vfs *, const char *, Rng *, char *, size_t);
static const Mutation MUTATION[] = {
    mut_delete, mut_truncate, mut_flip, mut_zero,
    mut_line, mut_line, mut_line,     /* line surgery is the commonest, so
                                       * weight it: config damage should be
                                       * more likely than a bad block */
    mut_mode, mut_relink,
};
#define NMUT ((int)(sizeof MUTATION / sizeof MUTATION[0]))

/* Damage one random file one random way. Returns false if the mutation was a
 * no-op (wrong kind of file for it, empty file), which the caller retries. */
bool machine_corrupt(Machine *m, Rng *r, char *what, size_t whatsz)
{
    /* Roughly one ticket in four is structural rather than a damaged file, so
     * `pkg reinstall` is not the answer often enough that the player cannot
     * rely on it. */
    if (rng_next(r) % 100 < 15) {
        char d[200] = "";
        STRUCTURAL[rng_next(r) % (uint64_t)NSTRUCT](m, r, d, sizeof d);
        if (d[0]) { snprintf(what, whatsz, "%s", d); return true; }
        return false;
    }

    PathSet ps = { .n = 0 };
    collect(m->disk.root, "", &ps);
    if (ps.n == 0) return false;
    const char *path = ps.path[rng_next(r) % (uint64_t)ps.n];
    Mutation mut = MUTATION[rng_next(r) % (uint64_t)NMUT];
    char d[200] = "";
    mut(&m->disk, path, r, d, sizeof d);
    if (!d[0]) return false;
    snprintf(what, whatsz, "%s", d);
    return true;
}

/* Break the machine for real: keep damaging a fresh copy until it stops
 * booting. This is generate-and-test — the engine proves the ticket is a
 * ticket by trying to boot it — and it is why the corruption can be totally
 * random without producing machines that are fine.
 *
 * `nfaults` is how many independent corruptions to leave on the disk. More
 * than one means faults that mask each other: you fix the boot, and the next
 * one is waiting underneath.
 */
bool machine_break(Machine *m, uint64_t seed, int nfaults, char *what, size_t whatsz)
{
    if (nfaults < 1) nfaults = 1;
    if (what && whatsz) what[0] = '\0';

    for (int attempt = 0; attempt < 400; attempt++) {
        machine_free(m);
        machine_install(m, seed);
        Rng r;
        rng_seed(&r, (seed ^ 0x9e3779b97f4a7c15ULL) + (uint64_t)attempt * 0x2545f491ULL);

        char all[512] = "";
        int applied = 0;
        for (int guard = 0; guard < 64 && applied < nfaults; guard++) {
            char d[200];
            if (!machine_corrupt(m, &r, d, sizeof d)) continue;
            if (applied) strncat(all, "; ", sizeof all - strlen(all) - 1);
            strncat(all, d, sizeof all - strlen(all) - 1);
            applied++;
        }
        if (applied < nfaults) continue;

        machine_boot(m);
        if (!m->boot.running) {
            if (what) snprintf(what, whatsz, "%s", all);
            /* Brief the customer with what actually happened. They will not
             * volunteer it, but they cannot tell you something that is not
             * true either. */
            customer_brief(m, all);
            return true;
        }
        /* It still boots. That is not a ticket — try again. */
    }
    return false;
}
