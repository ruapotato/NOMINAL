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
#include <stdlib.h>
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
static void fault_fstab_uuid(Machine *m, Rng *r, char *d, size_t ds);

/* TWO FILES CAN CARRY A UUID AND THIS IS ONE FAULT, NOT TWO.
 *
 * A blind playtester drew "a uuid in a config does not match blkid" three
 * times in eight tickets and had the third one in under a minute: the
 * bootloader's copy and fstab's copy are the same mistake wearing a different
 * filename, and drawing them from two slots made the family twice as likely
 * as anything else in the table. They are still genuinely different tickets --
 * one stops in the initrd before userland exists and the other stops in
 * mountall with the machine half up, and the file to fix is the other one --
 * so both survive, as two arms of one draw. */
static void fault_wrong_uuid(Machine *m, Rng *r, char *d, size_t ds)
{
    if (rng_next(r) % 2) { fault_fstab_uuid(m, r, d, ds); return; }
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
    /* Older than the 2.38 everything on this disk was built against. A
     * DOWNGRADE, not an upgrade: a newer libc satisfies an older requirement,
     * so "upgraded to 2.41 and nothing runs" was a failure mode that does not
     * exist on a real machine. What does happen, constantly, is a package from
     * the wrong release pulling libc backwards. */
    static const char *VERS[] = { "2.36", "2.35", "2.31", "2.28" };
    const char *v = VERS[rng_next(r) % 4];
    VNode *n = vfs_lookup(&m->disk, "/lib/libc.so.6");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_printf(&n->data, "stub libc %s\n", v);
    snprintf(d, ds, "pulled libc backwards to %s, older than the 2.38 the system is built against", v);
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
        "/bin/nomsh",      /* a shell that used to exist                  */
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
        "url = https://packages.nomnix.org/12.0-pre\n");
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

/* A config that EXISTS and does not say the one thing its daemon needs. The
 * file is there, `pkg verify` flags it as changed among the usual config
 * drift, and the service starts, reads it, and gives up -- over and over,
 * until the system stops trying.
 *
 * Aimed at the critical daemons on purpose: a non-critical service dying in
 * a respawn loop leaves the machine UP, which is a real and nastier ticket
 * ("it boots, the firewall is just not running") and one the breaker cannot
 * currently express, because a ticket here means a machine that will not
 * boot. Noted in the catalogue as its own item. */
static void fault_daemon_directive(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *path, *key; } C[] = {
        { "/etc/net/interfaces",                "iface" },
        { "/etc/udev/rules.d/50-default.rules", "SUBSYSTEM" },
        { "/etc/syslog.conf",                   "*.info" },
        { "/etc/nftables.conf",                 "table" },
    };
    int i = (int)(rng_next(r) % 4);
    VNode *n = vfs_lookup(&m->disk, C[i].path);
    if (!n || n->kind != VN_FILE) return;

    /* Comment the line out, which is what a person does and what a
     * half-finished edit leaves behind. */
    Buf out = {0};
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    size_t klen = strlen(C[i].key);
    bool hit = false;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const char *t = p; size_t tl = len;
        while (tl && (*t == ' ' || *t == '\t')) { t++; tl--; }
        if (!hit && tl >= klen && strncmp(t, C[i].key, klen) == 0) {
            buf_puts(&out, "# ");
            hit = true;
        }
        buf_put(&out, p, len);
        buf_putc(&out, '\n');
        p = nl ? nl + 1 : NULL;
    }
    if (hit) {
        buf_clear(&n->data);
        buf_put(&n->data, out.p, out.len);
        snprintf(d, ds, "commented out the %s line in %s", C[i].key, C[i].path);
    }
    buf_free(&out);
}

/* The disk filled up.
 *
 * A different mechanism from everything else here: nothing is corrupt, no
 * file is wrong, every hash matches, and `pkg verify` will tell you the
 * machine is perfect. There is simply nowhere to put the next byte, so the
 * first thing that needs to write fails -- and the first failure is almost
 * never the interesting one, because what actually filled the disk is a log
 * that has been growing quietly since March.
 *
 * The fix is not a package. It is `df`, then finding what is big, then
 * deleting it. */
static void fault_disk_full(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/var/log/messages");
    if (!n) n = vfs_mkfile(&m->disk, "/var/log/messages", "");
    if (!n || n->kind != VN_FILE) return;

    uint64_t used = machine_disk_used(m);
    if (m->fs_capacity <= used) return;
    uint64_t room = m->fs_capacity - used;

    /* Months of a daemon logging every failed attempt at something. Filling
     * it to the brim rather than over, so the disk is full and not corrupt. */
    static const char *LINES[] = {
        "udevd: could not open /dev/input/event3: no such device\n",
        "sshd: refused connect from 10.0.2.88\n",
        "ntpd: no reply from 10.0.2.3, will retry\n",
        "crond: (root) CMD (/usr/sbin/logrotate /etc/logrotate.conf)\n",
    };
    uint64_t before = n->data.len;
    uint64_t k = 0;
    for (; k < room; ) {
        const char *l = LINES[k % 4];
        size_t ll = strlen(l);
        if (k + ll > room) break;
        buf_puts(&n->data, l);
        k += ll;
    }
    /* Fill the last few bytes too. Leaving even forty spare is enough for a
     * daemon's one-line banner to fit, and then the disk is full in a way
     * nothing notices -- which is a fault that does not exist. */
    while (k < room) { buf_putc(&n->data, '.'); k++; }
    snprintf(d, ds, "filled the disk: /var/log/messages grew from %llu to %llu bytes",
             (unsigned long long)before, (unsigned long long)n->data.len);
}

/* Somebody took a backup of /etc before making a change, and then bound it
 * over the top "just while I test something".
 *
 * NOTHING IS CORRUPT. Every file passes pkg verify -- including the ones the
 * machine is now not reading. The boot reads a month-old copy of the config
 * and behaves accordingly, and the only way to see it is to look at what the
 * namespace actually says: `cat /proc/<pid>/ns`, or the line in rc.boot that
 * put it there. This is the fault the wiki has been promising and could not
 * deliver. */
/* A recursive chmod that caught a directory. Every file underneath is
 * byte-for-byte what the package shipped -- `pkg verify` reports the machine
 * as clean -- and nothing can read any of them. The evidence is in the error
 * ("cannot read"), in `ls -l` on the parent, and nowhere else.
 *
 * This is the answer to the playtest note that the game is recipe-following.
 * There IS no manifest line for a directory, so verify cannot hand this one
 * over, and the player has to notice that a file which reads as present is
 * still unreachable. */
/* The root filesystem left read-only. One word in one line of fstab, and
 * nothing on the disk is wrong: every hash matches, `pkg verify` reports a
 * perfect machine, and every service that keeps state dies the moment it
 * tries to write.
 *
 * This is what a half-finished repair looks like. Somebody hit a dirty
 * filesystem, mounted it ro to be safe, edited fstab so it would stay that
 * way while they investigated, and never put it back. The failure is a
 * cascade of unrelated-looking errors from whichever daemon writes first,
 * which is exactly how it reads on a real machine. */
/* Replace ONE whitespace-separated field of a line, in place, touching no
 * other byte. Rewriting the whole line with printf padding was a fairness
 * bug: a player who corrected the wrong word with `sed` fixed the machine and
 * `pkg verify` still reported the file as CHANGED, because the injected line
 * had different column spacing from the one the package ships. The machine
 * booted and the tools said it was still broken. A playtester hit exactly
 * that and was right to call it out.
 *
 * Field indices are 0-based over whitespace-separated tokens. Returns false
 * if the line does not have that many. */
static bool line_set_field(const char *line, int idx, const char *val,
                           char *out, size_t outsz)
{
    const char *p = line;
    int f = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (f == idx) {
            size_t pre = (size_t)(start - line);
            size_t taillen = strlen(p);
            if (pre + strlen(val) + taillen + 1 > outsz) return false;
            memcpy(out, line, pre);
            memcpy(out + pre, val, strlen(val));
            memcpy(out + pre + strlen(val), p, taillen + 1);
            return true;
        }
        f++;
    }
    return false;
}

static void fault_root_ro(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/fstab");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    bool hit = false;
    const char *p = n->data.p ? n->data.p : "";
    size_t len = n->data.len;
    size_t i = 0;
    while (i < len) {
        size_t e = i; while (e < len && p[e] != '\n') e++;
        char line[512];
        size_t ll = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, p + i, ll); line[ll] = 0;
        /* The root entry: second field is exactly "/". */
        char a[128] = "", b[128] = "", c[64] = "", o[64] = "";
        int got = sscanf(line, "%127s %127s %63s %63s", a, b, c, o);
        if (!hit && got >= 3 && line[0] != '#' && strcmp(b, "/") == 0) {
            /* Field 3 is the options; a line with only three fields grows
             * one, spaced the way the shipped file spaces them. */
            char edited[512];
            if (got >= 4 && line_set_field(line, 3, "ro", edited, sizeof edited))
                buf_printf(&out, "%s\n", edited);
            else
                buf_printf(&out, "%s ro\n", line);
            hit = true;
        } else {
            buf_put(&out, line, strlen(line));
            buf_puts(&out, "\n");
        }
        i = e < len ? e + 1 : len;
    }
    if (hit) { buf_clear(&n->data); buf_put(&n->data, out.p, out.len); }
    buf_free(&out);
    if (hit) snprintf(d, ds, "left the root filesystem mounted read-only in "
                             "/etc/fstab");
}

/* A support library at the wrong version. Unlike libc, only SOME programs
 * need libz -- the ones that compress what they write -- so this breaks the
 * web server, the mailer and the logger and leaves ssh, cron, udev and the
 * firewall running perfectly.
 *
 * That partial pattern is the puzzle. A machine where everything is dead
 * points straight at libc; a machine where three unrelated services are dead
 * and the rest are fine asks what those three have in common, and the answer
 * is in `ldd`-shaped output, not in any config file any of them reads. */
static void fault_bad_libz(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *VERS[] = { "1.2", "1.1", "0.9", "1.29" };
    const char *v = VERS[rng_next(r) % 4];
    VNode *n = vfs_lookup(&m->disk, "/lib/libz.so.1");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_printf(&n->data, "\x7fELF (stub) zlib %s\n", v);
    snprintf(d, ds, "installed zlib %s, older than the 1.3 some programs are "
                    "built against", v);
}

/* fstab claiming a filesystem type the device does not have. A single word,
 * and the mount fails with the most recognisable error in the whole of Unix
 * system administration: wrong fs type.
 *
 * It is a real mistake -- somebody rebuilt a disk, or copied a line from
 * another machine's fstab, or guessed. Nothing is corrupt and `blkid` will
 * tell the truth to anyone who asks it, which is the point: the file and the
 * device disagree, and only one of them can be edited. */
static void fault_fstype(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *WRONG[] = { "xfs", "btrfs", "ext3", "reiserfs", "vfat" };
    const char *bad = WRONG[rng_next(r) % 5];
    VNode *n = vfs_lookup(&m->disk, "/etc/fstab");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    bool hit = false;
    const char *p = n->data.p ? n->data.p : "";
    size_t len = n->data.len, i = 0;
    while (i < len) {
        size_t e = i; while (e < len && p[e] != '\n') e++;
        char line[512];
        size_t ll = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, p + i, ll); line[ll] = 0;
        char a[128] = "", b[128] = "", c[64] = "", o[64] = "defaults";
        int got = sscanf(line, "%127s %127s %63s %63s", a, b, c, o);
        /* A real device only: a wrong type on `none` means nothing. */
        if (!hit && got >= 3 && line[0] != '#' &&
            (a[0] == '/' || strncmp(a, "UUID=", 5) == 0) &&
            strcmp(c, "iso9660") != 0) {
            char edited[512];
            if (!line_set_field(line, 2, bad, edited, sizeof edited)) {
                buf_put(&out, line, strlen(line)); buf_puts(&out, "\n");
                i = e < len ? e + 1 : len; continue;
            }
            buf_printf(&out, "%s\n", edited);
            hit = true;
        } else {
            buf_put(&out, line, strlen(line));
            buf_puts(&out, "\n");
        }
        i = e < len ? e + 1 : len;
    }
    if (hit) { buf_clear(&n->data); buf_put(&n->data, out.p, out.len); }
    buf_free(&out);
    if (hit) snprintf(d, ds, "changed a filesystem type in /etc/fstab to %s", bad);
}

/* A directory a daemon needs, deleted. This was tried once and withdrawn,
 * because no package owned /run or /var/log and so `pkg reinstall` could not
 * put back something no package had ever shipped -- the auto-solver scored
 * 0/10 and a fault the solver cannot repair is one a player cannot repair.
 *
 * Packages now record the directories they own, the way rpm and dpkg both
 * really do, so verify reports the directory MISSING by name and reinstall
 * recreates it. The fault is back, and it is the same fault a careless
 * cleanup script produces on a real machine. */
/* THE PREVIOUS TECHNICIAN'S FIX, WHICH IS THE FAULT.
 *
 * Every other fault here looks like damage. This one looks like WORK: a dated
 * comment, a reason, a name, in exactly the voice of the legitimate local
 * edits on every machine -- because that is what it is. Somebody solved a
 * real problem on a Tuesday and broke the machine doing it.
 *
 * It is the answer to the sharpest thing a playtester has said about this
 * game: that `pkg verify` is an oracle, because the two or three CHANGED
 * files are always the same familiar decoys and exactly one unfamiliar line,
 * which is the answer. Now one of the deliberate-looking edits IS the answer,
 * and telling them apart means reading what each one actually does instead of
 * pattern-matching on which file it is.
 *
 * `pkg reinstall` will not overwrite it -- it looks locally modified, because
 * it is -- so the repair is `pkg diff`, then judgement, then `--force` or an
 * edit. Which is the whole loop this game is about. */
static void fault_wellmeant(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *path, *content, *what; } FIX[] = {
      { "/etc/syslog.conf",
        "# moved the log off the root fs 14 May, it filled up again\n"
        "*.info /var/log/archive/messages\n",
        "pointed syslog at /var/log/archive, a directory that is not there" },
      { "/etc/ntp.conf",
        "# commented out while we were being rate-limited -- put back!\n"
        "# server 10.0.2.4\n",
        "commented out the only time server in /etc/ntp.conf" },
      { "/etc/net/interfaces",
        "# renamed to match the new cabling standard, 2 April\n"
        "iface eth1\n"
        "  address 10.0.2.15\n"
        "  gateway 10.0.2.2\n",
        "renamed the interface in /etc/net/interfaces to one that does not exist" },
      /* This used to write /etc/default/postfix, which no package installs
       * and which therefore was not there -- so one fault in six did nothing
       * at all and the breaker quietly rolled again. A repair that cannot
       * fail is not the only thing worth checking; so is a fault that cannot
       * fire. */
      { "/etc/httpd/httpd.conf",
        "# site moved to the new volume 5 May, load balancer updated\n"
        "Listen 80\n"
        "DocumentRoot /srv/www-new\n"
        "ServerName nominal.local\n",
        "pointed the web server's document root at /srv/www-new, which was "
        "never created" },
      { "/etc/ntp.conf",
        "# drift moved off the root filesystem when it filled up, 3 Feb\n"
        "server 10.0.2.3 iburst\n"
        "driftfile /var/db/ntp/drift\n",
        "pointed ntpd's drift file at /var/db, a directory that is not there" },
      { "/etc/audit/auditd.conf",
        "# audit trail moved to its own volume, 30 Jan\n"
        "log_file = /var/audit/trail\n",
        "pointed the audit log at /var/audit, which was never created" },
      { "/etc/crontab",
        "# tidy the logs nightly -- added after the March incident\n"
        "# 0 3 * * *  root  rm /var/log/messages\n",
        "commented out every line of /etc/crontab" },
    };
    int i = (int)(rng_next(r) % (sizeof FIX / sizeof FIX[0]));
    VNode *n = vfs_lookup(&m->disk, FIX[i].path);
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_puts(&n->data, FIX[i].content);
    snprintf(d, ds, "%s", FIX[i].what);
}

/* A service ordered after one that is switched off.
 *
 * Nothing is corrupt, nothing is missing, and `pkg verify` names one file
 * whose only change is `enabled: yes` becoming `enabled: no` -- which reads
 * exactly like an administrator turning something off on purpose, because
 * that is what it looks like on a real machine too. The damage is somewhere
 * else entirely: every unit ordered AFTER it now waits for something that is
 * never coming.
 *
 * This is the fault the boot log was built for. The dependents announce
 * themselves in the log and nowhere else -- `svc` shows them as DEAD with no
 * reason, and verify points at the wrong service. */
static void fault_dep_disabled(Machine *m, Rng *r, char *d, size_t ds)
{
    /* Only services other units are ordered after are worth switching off. */
    static const char *HUBS[] = { "net", "udev", "syslog" };
    const char *hub = HUBS[rng_next(r) % 3];
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/etc/services.d/%s.svc", hub);
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE) return;

    Buf out = {0};
    bool hit = false;
    const char *p = n->data.p ? n->data.p : "";
    size_t len = n->data.len, i = 0;
    while (i < len) {
        size_t e = i; while (e < len && p[e] != '\n') e++;
        char line[512];
        size_t ll = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, p + i, ll); line[ll] = 0;
        if (!hit && strncmp(line, "enabled:", 8) == 0) {
            buf_puts(&out, "enabled: no\n");
            hit = true;
        } else {
            buf_put(&out, line, strlen(line));
            buf_puts(&out, "\n");
        }
        i = e < len ? e + 1 : len;
    }
    if (hit) { buf_clear(&n->data); buf_put(&n->data, out.p, out.len); }
    buf_free(&out);
    if (hit) snprintf(d, ds, "switched off the %s service that other units are "
                             "ordered after", hub);
}

/* Inodes exhausted, with space to spare.
 *
 * A DIFFERENT DIAGNOSIS FROM A FULL DISK, and that is the whole reason it is
 * here. `df` reports plenty of room, every hash matches, `pkg verify` says
 * the machine is perfect -- and nothing can create a file. The only tool that
 * answers it is `df -i`, and a player who has only ever seen a full disk will
 * go round the houses first.
 *
 * The cause is the one it always is in real life: something that makes a file
 * per run and never cleans up. A per-minute cron job that has been running
 * since March leaves a quarter of a million of them, and the directory it
 * filled is the evidence. */
static void fault_inodes(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *WHERE[] = {
        "/var/spool/cron", "/var/cache", "/tmp",
    };
    const char *dir = WHERE[rng_next(r) % 3];
    VNode *dn = vfs_lookup(&m->disk, dir);
    if (!dn || dn->kind != VN_DIR) return;

    /* Empty files: this must exhaust INODES without touching the byte
     * budget, or it is just a full disk wearing a hat. */
    uint64_t room = m->fs_inodes_max > machine_inodes_used(m)
                  ? m->fs_inodes_max - machine_inodes_used(m) : 0;
    if (!room) return;
    for (uint64_t i = 0; i < room; i++) {
        char p2[NOM_PATH_MAX];
        snprintf(p2, sizeof p2, "%s/job.%llu.tmp", dir, (unsigned long long)i);
        VNode *n = vfs_mkfile(&m->disk, p2, "");
        if (n) n->mode = 0644;
    }
    snprintf(d, ds, "left %llu stale files in %s: the filesystem is out of "
                    "inodes with space to spare",
             (unsigned long long)room, dir);
}

/* udev renamed the network interface.
 *
 * BOTH FILES ARE VALID AND BOTH ARE WHAT SOMEBODY INTENDED. The udev rule
 * names the device; the network config configures a device by name; a rename
 * in one of them and they no longer agree. Nothing is corrupt, no version is
 * wrong, and `pkg verify` flags the rules file as CHANGED -- which looks
 * exactly like the legitimate local edits on every machine.
 *
 * This is the "predictable interface names" migration that broke half the
 * world's servers, and the debugging is genuinely its own shape: the answer
 * is in a file nobody thinks about until they have to. */
static void fault_iface_rename(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *NAMES[] = { "enp0s3", "eno1", "ens18", "eth1" };
    const char *nm = NAMES[rng_next(r) % 4];
    VNode *n = vfs_lookup(&m->disk, "/etc/udev/rules.d/50-default.rules");
    if (!n || n->kind != VN_FILE) return;

    Buf out = {0};
    bool hit = false;
    const char *p = n->data.p ? n->data.p : "";
    size_t len = n->data.len, i = 0;
    while (i < len) {
        size_t e = i; while (e < len && p[e] != '\n') e++;
        char line[512];
        size_t ll = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, p + i, ll); line[ll] = 0;
        if (!hit && strstr(line, "NAME=") && strstr(line, "net")) {
            buf_printf(&out, "# renamed to the predictable scheme, 3 June\n");
            buf_printf(&out, "SUBSYSTEM==\"net\", NAME=\"%s\"\n", nm);
            hit = true;
        } else {
            buf_put(&out, line, strlen(line));
            buf_puts(&out, "\n");
        }
        i = e < len ? e + 1 : len;
    }
    if (hit) { buf_clear(&n->data); buf_put(&n->data, out.p, out.len); }
    buf_free(&out);
    if (hit) snprintf(d, ds, "a udev rule renames the network device to %s, "
                             "which /etc/net/interfaces does not configure", nm);
}

/* AN UPGRADE THAT STOPPED HALFWAY.
 *
 * The power went, or the disk filled, or somebody hit ctrl-C. Some of the
 * package's files are the new version and some are still the old one, and the
 * package is now internally inconsistent in a way nothing on the machine
 * agrees about: `pkg list` shows one version, the files disagree with each
 * other, and only SOME of the programs it ships will run.
 *
 * The signature is unlike anything else here. A corrupted binary is one file;
 * a bad library is every binary at once. This is several files of ONE package
 * changed together, all consistently, all deliberately -- because they really
 * were installed on purpose, just not all of them.
 *
 * Done by patching the dependency each binary declares: the new build wants a
 * libc this machine does not have yet, because the libc half of the upgrade
 * never happened. That is exactly what a half-finished dist-upgrade feels
 * like, and the fix is to finish it or roll it back, not to edit anything. */
static void fault_half_upgrade(Machine *m, Rng *r, char *d, size_t ds)
{
    /* Programs that ship together and are upgraded together. */
    static const char *SETS[][4] = {
        { "/usr/sbin/syslogd", "/usr/sbin/crond",  NULL, NULL },
        { "/usr/sbin/netd",    "/usr/sbin/sshd",   NULL, NULL },
        { "/usr/sbin/httpd",   "/usr/sbin/nft",    NULL, NULL },
    };
    int set = (int)(rng_next(r) % 3);
    int hit = 0;

    for (int i = 0; i < 4 && SETS[set][i]; i++) {
        VNode *n = vfs_lookup(&m->disk, SETS[set][i]);
        if (!n || n->kind != VN_FILE || n->data.len < 32) continue;
        /* The .nomneed section carries "libc.so.6 2.38". The new build of
         * this program was compiled against 2.41. */
        static const char want[] = "libc.so.6 2.38";
        for (size_t k = 0; k + sizeof want - 1 < n->data.len; k++) {
            if (memcmp(n->data.p + k, want, sizeof want - 1) != 0) continue;
            n->data.p[k + sizeof want - 2] = '1';   /* 2.38 -> 2.41 */
            n->data.p[k + sizeof want - 3] = '4';
            hit++;
            break;
        }
    }
    if (!hit) return;
    snprintf(d, ds, "an upgrade stopped halfway: %d program(s) are the new "
                    "build and want a libc this machine has not got yet", hit);
}

static void fault_missing_dir(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *VICTIMS[] = {
        "/run", "/var/log", "/tmp", "/var/spool/cron", "/var/lib/ntp",
        "/run/nomde", "/var/cache", "/srv/www", "/usr/share/applications",
    };
    const char *path = VICTIMS[rng_next(r) % (sizeof VICTIMS / sizeof VICTIMS[0])];
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_DIR) return;
    if (!vfs_remove(&m->disk, path)) return;
    snprintf(d, ds, "deleted the directory %s and everything in it", path);
}

static void fault_dir_mode(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *VICTIMS[] = {
        "/etc/rc.d", "/etc/svc", "/lib/modules/6.4.11", "/etc/pkg", "/boot/zbl",
        "/etc/net", "/etc/udev/rules.d", "/etc/services.d", "/etc/httpd",
        "/etc/audit", "/srv/www",
    };
    const char *path = VICTIMS[rng_next(r) % (sizeof VICTIMS / sizeof VICTIMS[0])];
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_DIR) return;
    n->mode = 0644;                  /* readable, NOT traversable */
    snprintf(d, ds, "took the execute bit off the directory %s", path);
}

static void fault_bad_bind(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    if (vfs_lookup(&m->disk, "/etc.bak")) return;
    vfs_mkdir(&m->disk, "/etc.bak");

    /* A copy of everything the boot needs, as it was BEFORE the last change:
     * an old runlevel and a services.d without the newer units. */
    vfs_mkdir(&m->disk, "/etc.bak/rc.d");
    vfs_mkdir(&m->disk, "/etc.bak/services.d");
    static const char *COPY[] = { "/etc/inittab", "/etc/rc.boot", "/etc/fstab",
                                  "/etc/hostname", "/etc/issue", NULL };
    for (int i = 0; COPY[i]; i++) {
        VNode *src = vfs_lookup(&m->disk, COPY[i]);
        if (!src || src->kind != VN_FILE) continue;
        char dst[NOM_PATH_MAX];
        snprintf(dst, sizeof dst, "/etc.bak%s", COPY[i] + 4);
        VNode *n = vfs_mkfile(&m->disk, dst, "");
        if (n) { buf_put(&n->data, src->data.p, src->data.len); n->mode = src->mode; }
    }
    /* the old runlevel file names a runlevel the backup has no script for */
    VNode *rc = vfs_mkfile(&m->disk, "/etc.bak/rc.conf", "5\n");
    if (rc) rc->mode = 0644;

    /* and the line that does it, left in rc.boot */
    VNode *b = vfs_lookup(&m->disk, "/etc/rc.boot");
    if (!b || b->kind != VN_FILE) return;
    Buf out = {0};
    buf_puts(&out, "# bind the backup while I test something -- REMOVE THIS\n");
    buf_puts(&out, "bind /etc.bak /etc\n");
    buf_put(&out, b->data.p, b->data.len);
    buf_clear(&b->data);
    buf_put(&b->data, out.p, out.len);
    buf_free(&out);
    snprintf(d, ds, "left `bind /etc.bak /etc` in rc.boot: the machine reads a "
                    "month-old copy of its configuration");
}

/* =====================================================================
 * A SECOND GENERATION OF STRUCTURAL FAULTS.
 *
 * The bar for anything below: a player who knows Linux must be able to reach
 * it from the console, `svc`, `mount`, `ldd`, `df` or `pkg verify`, and the
 * repair must be a DIFFERENT SEQUENCE from the ones above. A new symptom on
 * the same fix is not a new fault, it is a repaint.
 * ===================================================================== */

/* Append a line to a file that is already there. */
static void append_line(Machine *m, const char *path, const char *line)
{
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE) return;
    if (n->data.len && n->data.p[n->data.len - 1] != '\n') buf_putc(&n->data, '\n');
    buf_puts(&n->data, line);
}

/* Rewrite one `key: value` line of a service unit. */
static bool svc_set(Machine *m, const char *unit, const char *key,
                    const char *newline)
{
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/etc/services.d/%s.svc", unit);
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE) return false;
    size_t before = n->data.len;
    rewrite_line(m, path, key, newline);
    return n->data.len != before || true;
}

/* SOMETHING MOUNTED OVER A DIRECTORY THAT ALREADY HAD THINGS IN IT.
 *
 * Nothing is deleted, nothing is corrupt, every hash matches -- and the
 * contents of /var are not the contents of /var any more, because a
 * filesystem is sitting on top of them. The line is one somebody wrote on
 * purpose, for a disk that never arrived, and it mounts the root device a
 * second time in a second place.
 *
 * `mount` and `df` both show it plainly and `ls /var` is a bewildering few
 * seconds: the directory is full of /bin, /etc and /home. It is the fault
 * `pkg verify` is least use for, because the file it flags (/etc/fstab) is
 * correct in every particular except intent. */
static void fault_mount_shadow(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/fstab");
    if (!n || n->kind != VN_FILE) return;
    for (size_t i = 0; i + 4 < n->data.len; i++)
        if (memcmp(n->data.p + i, "/var", 4) == 0) return;   /* already there */
    append_line(m, "/etc/fstab",
        "# /var onto its own filesystem -- 8 Feb. new disk still not here,\n"
        "# pointed it at sda1 for now so the entry is ready. -- J.\n"
        "/dev/sda1                       /var   ext4  defaults\n");
    snprintf(d, ds, "fstab mounts the root disk a second time over /var, "
                    "hiding everything in it");
}

/* fstab NAMING A DISK THIS MACHINE HAS NOT GOT, by uuid.
 *
 * The bootloader found the root filesystem and handed it over, so the machine
 * is running -- and then /etc/fstab says the root is a uuid nothing here
 * carries. A disk was replaced, or the line was copied from another machine.
 * `blkid` answers it in one command, which is the whole point: two files
 * claim to describe this disk and only one of them agrees with it.
 *
 * A DIFFERENT FAULT FROM zbl.cfg's wrong uuid, and deliberately so: that one
 * stops in the initrd before userland exists, this one stops in mountall with
 * the machine half up, and the file to fix is the other one. */
static void fault_fstab_uuid(Machine *m, Rng *r, char *d, size_t ds)
{
    char bad[64];
    snprintf(bad, sizeof bad, "UUID=%04llx-%04llx-%04llx-%04llx",
             (unsigned long long)(rng_next(r) % 0xffff),
             (unsigned long long)(rng_next(r) % 0xffff),
             (unsigned long long)(rng_next(r) % 0xffff),
             (unsigned long long)(rng_next(r) % 0xffff));
    VNode *n = vfs_lookup(&m->disk, "/etc/fstab");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    bool hit = false;
    const char *p = n->data.p ? n->data.p : "";
    size_t len = n->data.len, i = 0;
    while (i < len) {
        size_t e = i; while (e < len && p[e] != '\n') e++;
        char line[512];
        size_t ll = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, p + i, ll); line[ll] = 0;
        char a[128] = "", b[128] = "";
        int got = sscanf(line, "%127s %127s", a, b);
        char edited[512];
        if (!hit && got >= 2 && line[0] != '#' && strncmp(a, "UUID=", 5) == 0 &&
            line_set_field(line, 0, bad, edited, sizeof edited)) {
            buf_printf(&out, "%s\n", edited);
            hit = true;
        } else {
            buf_put(&out, line, strlen(line));
            buf_puts(&out, "\n");
        }
        i = e < len ? e + 1 : len;
    }
    if (hit) { buf_clear(&n->data); buf_put(&n->data, out.p, out.len); }
    buf_free(&out);
    if (hit) snprintf(d, ds, "fstab names a root uuid this disk does not have");
}

/* A LIBRARY THAT IS NOW A SYMLINK TO NOTHING.
 *
 * An upgrade that got as far as replacing the file with a link to its
 * versioned name and no further. `ls /lib` shows the library, in the right
 * place, with the right name; `stat` says there is nothing there, and the
 * loader agrees -- "cannot open shared object file", which is a different
 * sentence from "version not found" and means a different thing.
 *
 * Choosing libz makes it partial (the web server and the audit trail, and
 * nothing else); choosing libc makes it total, and then the only way in is
 * the rescue medium. */
static void fault_dangling_lib(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *path, *target; } L[] = {
        { "/lib/libz.so.1", "/lib/libz.so.1.3.0" },
        { "/lib/libz.so.1", "/usr/lib/libz-1.3.so" },
        { "/lib/libc.so.6", "/lib/libc-2.38.so" },
    };
    int i = (int)(rng_next(r) % 3);
    VNode *n = vfs_lookup(&m->disk, L[i].path);
    if (!n || n->kind != VN_FILE) return;
    vfs_remove(&m->disk, L[i].path);
    vfs_symlink(&m->disk, L[i].target, L[i].path);
    snprintf(d, ds, "%s is now a symlink to %s, which is not there",
             L[i].path, L[i].target);
}

/* TWO COPIES OF ONE LIBRARY, AND THE LOADER PICKS THE WRONG ONE.
 *
 * Nothing is missing and nothing is corrupt: the correct library is exactly
 * where it belongs and is exactly right. There is simply an older one
 * somewhere else, and a search path that was reordered to put that somewhere
 * else first -- which is what happens every time a vendor tarball is
 * unpacked into /usr/lib and somebody makes it work.
 *
 * `ldd` is the whole diagnosis and the reason it prints the PATH it resolved
 * to: the version is wrong AND the file it came from is not the one you were
 * looking at. `pkg verify` flags /etc/ld.so.conf, which reads like a
 * deliberate local edit, because it is one. `pkg owns` the stray copy and
 * nothing does. */
static void fault_lib_shadow(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *so, *stub, *ver; } L[] = {
        { "libz.so.1", "\x7f" "ELF (stub) zlib %s\n", "1.2" },
        { "libc.so.6", "stub libc %s\n",           "2.31" },
    };
    int i = (int)(rng_next(r) % 2);
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/usr/lib/%s", L[i].so);
    if (vfs_lookup(&m->disk, path)) return;
    VNode *n = vfs_mkfile(&m->disk, path, "");
    if (!n) return;
    buf_printf(&n->data, L[i].stub, L[i].ver);
    n->mode = 0755;

    VNode *cf = vfs_lookup(&m->disk, "/etc/ld.so.conf");
    if (!cf || cf->kind != VN_FILE) return;
    buf_clear(&cf->data);
    buf_puts(&cf->data,
        "# the vendor tools ship their own libraries and will not start\n"
        "# unless theirs are found first. 12 May -- R.\n"
        "/usr/lib\n"
        "/lib\n");
    snprintf(d, ds, "an older %s in /usr/lib, and ld.so.conf searches there "
                    "first", L[i].so);
}

/* A SERVICE MOVED TO A RUNLEVEL THIS MACHINE DOES NOT BOOT INTO.
 *
 * Nothing failed. Nothing was tried. The unit is present, correct, enabled
 * and perfectly healthy -- and it belongs to runlevel 5 on a machine that
 * boots to 3, so it is simply not there, and everything ordered after it
 * waits for a service that was never going to be started.
 *
 * The trap is that `enabled: yes` is right there in the file, which is the
 * line everybody reads. The console is the only place the word "runlevel"
 * appears. */
static void fault_wrong_runlevel(Machine *m, Rng *r, char *d, size_t ds)
{
    if (rng_next(r) % 2) {
        static const char *HUBS[] = { "udev", "syslog", "net" };
        const char *hub = HUBS[rng_next(r) % 3];
        char path[NOM_PATH_MAX];
        snprintf(path, sizeof path, "/etc/services.d/%s.svc", hub);
        VNode *n = vfs_lookup(&m->disk, path);
        if (!n || n->kind != VN_FILE) return;
        rewrite_line(m, path, "runlevel", "runlevel: 5");
        snprintf(d, ds, "moved the %s service to runlevel 5 on a machine that "
                        "boots to 3", hub);
        return;
    }
    /* Or the other way round: the machine was pointed at the graphical
     * runlevel, and half the service set does not belong to it. */
    VNode *n = vfs_lookup(&m->disk, "/etc/rc.d/rc.3");
    if (!n || n->kind != VN_FILE) return;
    rewrite_line(m, "/etc/rc.d/rc.3", "exec /sbin/svcinit",
                 "exec /sbin/svcinit 5");
    snprintf(d, ds, "rc.3 now enters runlevel 5, where half the services are "
                    "not wanted");
}

/* TWO SERVICES ORDERED AFTER EACH OTHER.
 *
 * Neither is broken. Neither will ever start. Somebody added an ordering to
 * fix a race and closed a loop doing it, which is the single easiest mistake
 * to make in any init system and the hardest to see, because each unit on its
 * own is completely reasonable. Reading one file tells you nothing; reading
 * two tells you everything. */
static void fault_dep_cycle(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *unit, *on; } C[] = {
        { "syslog", "cron"  },      /* cron is already after syslog  */
        { "udev",   "syslog" },     /* syslog is already after udev  */
        { "net",    "httpd"  },     /* httpd is already after net    */
    };
    int i = (int)(rng_next(r) % 3);
    char line[64];
    snprintf(line, sizeof line, "after: %s", C[i].on);
    if (!svc_set(m, C[i].unit, "after", line)) return;
    snprintf(d, ds, "%s is now ordered after %s, which is ordered after %s",
             C[i].unit, C[i].on, C[i].unit);
}

/* ORDERED AFTER SOMETHING THAT IS NOT INSTALLED AT ALL.
 *
 * The dependency is not disabled and not in another runlevel: there is no
 * such service on this machine. Either the unit came from a box that had one,
 * or the unit it was waiting for was deleted. svcinit says which of the two
 * it is, because "waiting for network" and "waiting for network -- and no
 * unit by that name is installed" are twenty minutes apart. */
static void fault_after_ghost(Machine *m, Rng *r, char *d, size_t ds)
{
    if (rng_next(r) % 2) {
        static const char *GHOSTS[] = { "network", "rsyslog", "systemd-udevd",
                                        "dbus" };
        static const char *UNITS[]  = { "net", "syslog", "httpd", "cron" };
        const char *g = GHOSTS[rng_next(r) % 4];
        const char *u = UNITS[rng_next(r) % 4];
        char line[64];
        snprintf(line, sizeof line, "after: %s", g);
        if (!svc_set(m, u, "after", line)) return;
        snprintf(d, ds, "%s is ordered after %s, which this machine has never "
                        "had", u, g);
        return;
    }
    /* The other way it happens: the unit everything is ordered after was
     * deleted, and every dependent inherits the silence. */
    static const char *HUBS[] = { "syslog", "net", "udev" };
    const char *hub = HUBS[rng_next(r) % 3];
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/etc/services.d/%s.svc", hub);
    if (!vfs_lookup(&m->disk, path)) return;
    vfs_remove(&m->disk, path);
    snprintf(d, ds, "deleted the %s unit that other services are ordered "
                    "after", hub);
}

/* THE HARDENING SCRIPT. It walks a directory and takes the execute bit off
 * anything it does not recognise, and it is run from somebody's laptop by
 * somebody who does not work here.
 *
 * The bytes are perfect. `pkg verify` says `mode` and NOT `changed` on a
 * scatter of files across several packages, which is a verify signature
 * unlike anything else in the game -- one word in the output IS the whole
 * diagnosis, and the repair is `chmod`, not a reinstall. Ticket 8841 in the
 * previous administrator's notes is this, and it says so. */
static void fault_hardening_sweep(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *DIRS[] = { "/usr/sbin", "/sbin" };
    const char *dir = DIRS[rng_next(r) % 2];
    VNode *dn = vfs_lookup(&m->disk, dir);
    if (!dn || dn->kind != VN_DIR) return;

    /* One file it DOES recognise survives, because a sweep that took every
     * bit off would read as a deleted directory rather than as a script with
     * an allow-list. */
    int n = 0;
    for (VNode *k = dn->child; k; k = k->next) n++;
    if (n < 2) return;
    int spare = (int)(rng_next(r) % (uint64_t)n), i = 0, hit = 0;
    char first[NOM_PATH_MAX] = "";
    for (VNode *k = dn->child; k; k = k->next, i++) {
        if (i == spare || k->kind != VN_FILE) continue;
        if (!(k->mode & 0111)) continue;
        k->mode = 0644;
        if (!hit) snprintf(first, sizeof first, "%s/%s", dir, k->name);
        hit++;
    }
    if (!hit) return;
    snprintf(d, ds, "a hardening sweep took the execute bit off %d file(s) in "
                    "%s, starting with %s", hit, dir, first);
}

/* THE WRONG FILE COPIED OVER A PROGRAM.
 *
 * A deployment that pushed the wrong artefact. The binary is a real, valid,
 * runnable program -- it is simply a different one, so the service starts,
 * does that program's job in half a millisecond, and exits, over and over,
 * until the machine gives up on it. The console fills with the output of
 * whatever it actually is, in the middle of the boot, which is the loudest
 * and strangest evidence in the game and points straight at the file. */
static void fault_wrong_binary(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *VICTIMS[] = {
        "/usr/sbin/syslogd", "/usr/sbin/udevd", "/usr/sbin/nft",
        "/usr/sbin/httpd",   "/usr/sbin/sshd",  "/usr/sbin/ntpd",
    };
    static const char *SOURCES[] = {
        "/bin/ls", "/bin/whoami", "/bin/uname", "/bin/df",
    };
    const char *vp = VICTIMS[rng_next(r) % 6];
    const char *sp = SOURCES[rng_next(r) % 4];
    VNode *v = vfs_lookup(&m->disk, vp);
    VNode *s = vfs_lookup(&m->disk, sp);
    if (!v || v->kind != VN_FILE || !s || s->kind != VN_FILE) return;
    buf_clear(&v->data);
    buf_put(&v->data, s->data.p, s->data.len);
    snprintf(d, ds, "%s is a copy of %s: it runs, does that job, and exits",
             vp, sp);
}

/* THE BOOTLOADER STILL NAMES THE KERNEL THE UPGRADE REMOVED.
 *
 * The classic: /boot filled, the new kernel went in, the old one was
 * autoremoved, and the entry that survived names the one that is gone. The
 * symlink is not involved and `stat /boot/vmnomuz` is perfectly happy, which
 * is what makes it different from the deleted-image fault -- the file the
 * loader wants is not the file the system installs.
 *
 * `zbl-mkconfig` writes a configuration for the machine in front of you,
 * which is the repair; reading zbl.cfg against `ls /boot` is the diagnosis. */
static void fault_stale_kernel_entry(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *OLD[] = { "6.4.9", "6.4.7", "6.3.12" };
    const char *v = OLD[rng_next(r) % 3];
    char line[128];
    snprintf(line, sizeof line, "  kernel /boot/vmnomuz-%s", v);
    VNode *n = vfs_lookup(&m->disk, "/boot/zbl/zbl.cfg");
    if (!n || n->kind != VN_FILE) return;
    rewrite_line(m, "/boot/zbl/zbl.cfg", "kernel", line);
    snprintf(d, ds, "zbl.cfg boots /boot/vmnomuz-%s, which an upgrade removed", v);
}

/* AN INITRD BUILT FOR SOMEBODY ELSE'S HARDWARE.
 *
 * Not an empty image and not a corrupt one: a complete, valid initrd with a
 * full set of drivers, none of which drive this machine's disk. It is what
 * you get when an image is cloned from a box with different storage, and it
 * is a different repair from a module that was deleted -- the modules are all
 * present in /lib/modules, so `mkinitrd` alone puts it right.
 *
 * The loader now prints what the image DOES carry, because "no driver for the
 * root device" is the same sentence for an empty initrd and a foreign one,
 * and they are not the same problem. */
static void fault_foreign_initrd(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *SETS[] = {
        "module ahci\nmodule nvme\nmodule ext4\nmodule dm_mod\n",
        "module megaraid_sas\nmodule ext4\nmodule dm_mod\n",
        "module xen_blkfront\nmodule ext4\n",
    };
    const char *set = SETS[rng_next(r) % 3];
    VNode *n = vfs_lookup(&m->disk, "/boot/initrd-6.4.11");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_puts(&n->data, "\x7fINITRD 6.4.11\n");
    buf_puts(&n->data, set);
    snprintf(d, ds, "the initrd carries drivers for another machine's disk");
}

/* AN ACCOUNT IN passwd AND NOT IN shadow.
 *
 * The machine boots perfectly. Every service is up. There is no way in,
 * because the password lives in the other file and root has no line in it --
 * which is exactly what half a user migration leaves behind, and it is
 * invisible in /etc/passwd, where everybody looks first. */
static void fault_no_shadow(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/shadow");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    bool dropped = false;
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 5 && strncmp(p, "root:", 5) == 0) dropped = true;
        else { buf_put(&out, p, len); buf_putc(&out, '\n'); }
        p = nl ? nl + 1 : NULL;
    }
    if (dropped) {
        buf_clear(&n->data);
        buf_put(&n->data, out.p, out.len);
        snprintf(d, ds, "removed root's line from /etc/shadow: the account "
                        "exists and cannot be authenticated");
    }
    buf_free(&out);
}

/* ONE EXTRA COLON IN /etc/passwd.
 *
 * The line still parses. It has the right name, the right uid, the right
 * home. Every field after the typo has shifted one to the left, so the login
 * shell is now the home directory -- and the machine says, quite correctly,
 * that root's login shell /root does not exist, which is a sentence that
 * makes no sense until you count the colons. */
static void fault_passwd_fields(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/passwd");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    bool hit = false;
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (!hit && len > 5 && strncmp(p, "root:", 5) == 0) {
            /* after the gecos field, which is the fifth */
            int colons = 0;
            size_t k = 0;
            for (; k < len && colons < 5; k++) if (p[k] == ':') colons++;
            buf_put(&out, p, k);
            buf_putc(&out, ':');
            buf_put(&out, p + k, len - k);
            hit = true;
        } else {
            buf_put(&out, p, len);
        }
        buf_putc(&out, '\n');
        p = nl ? nl + 1 : NULL;
    }
    if (hit) {
        buf_clear(&n->data);
        buf_put(&n->data, out.p, out.len);
        snprintf(d, ds, "an extra colon in root's passwd line shifts every "
                        "field after it");
    }
    buf_free(&out);
}

/* THE DOCUMENT ROOT IS NOT THERE.
 *
 * The config is valid, the daemon is fine, the machine boots to a login
 * prompt -- and the web server is dead, because the directory its
 * configuration names has been moved or deleted. /srv/www/README has been
 * telling anyone who read it to check exactly this. */
static void fault_docroot(Machine *m, Rng *r, char *d, size_t ds)
{
    if (rng_next(r) % 2) {
        static const char *MOVED[] = { "/srv/www-old", "/var/www", "/srv/http" };
        const char *to = MOVED[rng_next(r) % 3];
        VNode *n = vfs_lookup(&m->disk, "/etc/httpd/httpd.conf");
        if (!n || n->kind != VN_FILE) return;
        char line[128];
        snprintf(line, sizeof line, "DocumentRoot %s", to);
        rewrite_line(m, "/etc/httpd/httpd.conf", "DocumentRoot", line);
        snprintf(d, ds, "httpd.conf points the document root at %s, which does "
                        "not exist", to);
        return;
    }
    if (!vfs_lookup(&m->disk, "/srv/www")) return;
    if (!vfs_remove(&m->disk, "/srv/www")) return;
    snprintf(d, ds, "deleted /srv/www, the directory httpd.conf names as its "
                    "document root");
}

/* A UNIT POINTING AT A PATH THE PROGRAM HAS NEVER BEEN AT.
 *
 * The binary is present, correct, executable and exactly where its package
 * put it. The unit names a different directory -- the one the program lived
 * in on the distribution somebody copied the unit from. `pkg verify` flags
 * the unit and not the binary, which is the whole clue: what is wrong is the
 * pointer, not the thing pointed at. */
static void fault_exec_path(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *unit, *was, *now; } E[] = {
        { "syslog", "/usr/sbin/syslogd", "/usr/bin/syslogd" },
        { "net",    "/usr/sbin/netd",    "/sbin/netd"       },
        { "udev",   "/usr/sbin/udevd",   "/lib/udev/udevd"  },
        { "httpd",  "/usr/sbin/httpd",   "/usr/local/sbin/httpd" },
    };
    int i = (int)(rng_next(r) % 4);
    char line[160];
    snprintf(line, sizeof line, "exec: %s", E[i].now);
    if (!svc_set(m, E[i].unit, "exec", line)) return;
    snprintf(d, ds, "the %s unit execs %s; the program is at %s",
             E[i].unit, E[i].now, E[i].was);
}

/* PID 1 TOLD TO RUN THE WRONG SCRIPT.
 *
 * Somebody was testing single-user mode, or the runlevel scripts were being
 * reorganised. /etc/inittab is two lines long and one of them is now a path
 * that does not exist, so the machine stops before any of userland has run
 * and the console has almost nothing on it -- which is itself the diagnosis:
 * a boot that dies this early died in init, and init reads one file. */
static void fault_inittab_target(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *T[] = {
        "/bin/rc /etc/rc.d/rc.1",       /* single user, which does not exist */
        "/bin/rc /etc/rc.sysinit",      /* another distribution's name       */
        "/sbin/init.new /etc/rc.boot",  /* a replacement that never landed   */
    };
    const char *t = T[rng_next(r) % 3];
    VNode *n = vfs_lookup(&m->disk, "/etc/inittab");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_printf(&n->data,
        "# /etc/inittab -- the last non-comment line is run by /sbin/init.\n"
        "%s\n", t);
    snprintf(d, ds, "inittab runs %s", t);
}

/* AN INSTALLER PATCHED THE BOOT SCRIPT.
 *
 * A vendor package dropped a `need` line into /etc/rc.boot for an agent that
 * was never installed, or was removed afterwards by somebody tidying up. rc
 * stops at the first failure, on purpose, so the machine dies at a line that
 * has nothing to do with booting -- and the fix is to take the line out, not
 * to install anything. */
static void fault_rcboot_need(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *P[] = {
        "/opt/vendor/bin/agent", "/usr/local/sbin/site-init",
        "/opt/monitoring/bin/probe",
    };
    const char *path = P[rng_next(r) % 3];
    VNode *n = vfs_lookup(&m->disk, "/etc/rc.boot");
    if (!n || n->kind != VN_FILE) return;
    if (vfs_lookup(&m->disk, path)) return;

    Buf out = {0};
    bool put = false;
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        buf_put(&out, p, len);
        buf_putc(&out, '\n');
        if (!put && len > 4 && strncmp(p, "echo", 4) == 0) {
            buf_puts(&out, "# added by the vendor agent installer -- do not remove\n");
            buf_printf(&out, "need %s\n", path);
            put = true;
        }
        p = nl ? nl + 1 : NULL;
    }
    if (put) { buf_clear(&n->data); buf_put(&n->data, out.p, out.len); }
    buf_free(&out);
    if (put) snprintf(d, ds, "rc.boot needs %s, which no package installed", path);
}

/* A STATE DIRECTORY MADE READ-ONLY.
 *
 * Not deleted, not unreadable -- every file in it lists and reads perfectly.
 * It cannot be written to, so every daemon that publishes what it loaded
 * fails at the same moment for the same reason, and the console reads as
 * though the whole service set has gone mad at once. The mode is on the
 * DIRECTORY, which is the one place nobody looks, and `pkg verify` says so in
 * one word because the package that owns the directory records its mode. */
static void fault_ro_dir(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/run");
    if (!n || n->kind != VN_DIR) return;
    n->mode = 0555;
    snprintf(d, ds, "took the write bit off /run, where every daemon publishes "
                    "its state");
}

/* THE DISK FILLED WITH SOMETHING THAT IS NOT A LOG.
 *
 * The same 100% and a completely different search. A log that ate the disk is
 * one enormous file and `wc` finds it in a second; a package cache that ate
 * the disk is four hundred ordinary ones, none of them remarkable, and the
 * only way to see it is to look at the directory rather than at the files.
 * `find /var -type f` is the tool, and what it finds is a cache nobody has
 * ever cleaned because nothing on this machine cleans it. */
static void fault_cache_full(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *dn = vfs_lookup(&m->disk, "/var/cache");
    if (!dn || dn->kind != VN_DIR) return;
    uint64_t used = machine_disk_used(m);
    if (m->fs_capacity <= used) return;
    uint64_t room = m->fs_capacity - used;

    /* Enough files that the directory is the story, few enough that this is a
     * full disk and not an inode exhaustion wearing its coat. */
    uint64_t files = 120;
    uint64_t each = room / files;
    if (each < 64) return;
    uint64_t wrote = 0;
    for (uint64_t i = 0; i < files; i++) {
        char p2[NOM_PATH_MAX];
        snprintf(p2, sizeof p2, "/var/cache/nomnix-%llu.pkg",
                 (unsigned long long)(1400 + i));
        VNode *n = vfs_mkfile(&m->disk, p2, "");
        if (!n) break;
        n->mode = 0644;
        uint64_t want = (i == files - 1) ? room - wrote : each;
        for (uint64_t k = 0; k < want; k++) buf_putc(&n->data, 'p');
        wrote += want;
    }
    snprintf(d, ds, "filled the disk with %llu uncleaned package downloads in "
                    "/var/cache", (unsigned long long)files);
}

/* =====================================================================
 * A THIRD GENERATION, weighted at the stages the survey said were thin.
 *
 * Two thirds of every ticket landed in `services` or `target`, because that
 * is where the machine has the most files -- and the first four stages of the
 * boot, which are the ones a player has to reason about rather than grep,
 * were producing about one ticket in twenty between them. Everything below
 * fails in the firmware, the loader, the kernel, the initrd, init or login,
 * and each one is a different question from the ones already here.
 * ===================================================================== */

/* THE FIRMWARE IS STILL SET TO BOOT THE INSTALLER.
 *
 * Not a file. No package owns it, `pkg verify` is perfect, the disk is
 * perfect, and there is nothing to boot -- because somebody put the install
 * medium in, moved the optical drive to the top of the boot order, finished
 * the job, took the disc out and never put the order back. The machine has
 * been up for two hundred days on the strength of nobody rebooting it.
 *
 * The console prints the boot order before it prints anything else, which is
 * the whole diagnosis; `rcon status` says the same thing from the service
 * processor. Two repairs, both real: put the order back (`rcon boot disk`),
 * or run `zbl-install /dev/sda`, which writes the firmware's boot entry the
 * way grub-install does. */
static void fault_boot_order(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    if (m->sp_bootdev == 1) return;
    m->sp_bootdev = 1;
    m->sp_media   = false;
    snprintf(d, ds, "the firmware boot order names the optical drive, and "
                    "there is no disc in it");
}

/* THE DEFAULT ENTRY IS NOT THERE.
 *
 * Somebody added a menu entry to test something, booted it, deleted the entry
 * and left `default` pointing past the end of the list. The loader has a
 * perfectly good entry sitting right there and refuses to guess, which is
 * what a bootloader should do. One number in one line, and `zbl-mkconfig`
 * writes a configuration for the machine in front of you. */
static void fault_zbl_default(Machine *m, Rng *r, char *d, size_t ds)
{
    int n = 1 + (int)(rng_next(r) % 3);
    char line[64];
    snprintf(line, sizeof line, "default %d", n);
    VNode *f = vfs_lookup(&m->disk, "/boot/zbl/zbl.cfg");
    if (!f || f->kind != VN_FILE) return;
    rewrite_line(m, "/boot/zbl/zbl.cfg", "default", line);
    snprintf(d, ds, "zbl.cfg boots entry %d and the file has one entry", n);
}

/* THE UPGRADE ADDED AN ENTRY AND LEFT THE DEFAULT WHERE IT WAS.
 *
 * Two entries, both well-formed, and the one at the top is last release's --
 * whose kernel was autoremoved when /boot filled. The new entry is right
 * there, one line further down, and the machine has never booted it.
 *
 * Deliberately not the same fault as a single entry naming a kernel that is
 * gone: here the configuration contains the right answer and picks the wrong
 * one, so the repair is the `default` line or `zbl-mkconfig`, and reading the
 * file tells you everything. */
static void fault_zbl_dup_entry(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *OLD[] = { "6.4.9", "6.4.7", "6.3.12" };
    const char *v = OLD[rng_next(r) % 3];
    VNode *f = vfs_lookup(&m->disk, "/boot/zbl/zbl.cfg");
    if (!f || f->kind != VN_FILE) return;
    Buf out = {0};
    buf_printf(&out,
        "default 0\ntimeout 5\n\n"
        "entry \"NomnixOS 11.4 (%s)\"\n"
        "  kernel /boot/vmnomuz-%s\n"
        "  initrd /boot/initrd-%s\n"
        "  root UUID=%s\n"
        "\n"
        "entry \"NomnixOS 11.4\"\n"
        "  kernel /boot/vmnomuz\n"
        "  initrd /boot/initrd\n"
        "  root UUID=%s\n",
        v, v, v, m->root_uuid, m->root_uuid);
    buf_clear(&f->data);
    buf_put(&f->data, out.p, out.len);
    buf_free(&out);
    snprintf(d, ds, "zbl.cfg has two entries and the default is the %s one, "
                    "which is gone", v);
}

/* THE ROOT NAMED BY DEVICE, AND THE NUMBERING MOVED.
 *
 * Somebody wrote the root by hand, the way it was written for twenty years,
 * and then a disk was added and the partition it names is not the partition
 * it was. This is the reason installers write uuids, and the console says
 * plainly that it waited for a device that never appeared -- which is a
 * different sentence, and a different mental model, from a uuid nothing
 * carries. `zbl-mkconfig` writes what this machine actually has. */
static void fault_zbl_rootdev(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *DEV[] = { "/dev/sda2", "/dev/sdb1", "/dev/vda1",
                                 "/dev/nvme0n1p2" };
    const char *dev = DEV[rng_next(r) % 4];
    VNode *f = vfs_lookup(&m->disk, "/boot/zbl/zbl.cfg");
    if (!f || f->kind != VN_FILE) return;
    char line[96];
    snprintf(line, sizeof line, "  root %s", dev);
    rewrite_line(m, "/boot/zbl/zbl.cfg", "root", line);
    snprintf(d, ds, "zbl.cfg names the root as %s, a device this machine has "
                    "not got", dev);
}

/* THE MODULES ARE FOR THE KERNEL BEFORE LAST.
 *
 * The commonest real upgrade failure there is: the new kernel went in, the
 * modules did not, or a cleanup took the wrong directory. /lib/modules holds
 * one directory per kernel and the one this kernel needs is not among them,
 * so nothing can be loaded and the root device has no driver.
 *
 * `ls /lib/modules` against `uname` is the whole diagnosis and the console
 * prints both halves of it. `pkg reinstall kernel-default` puts the modules
 * back. */
static void fault_module_mismatch(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *OLD[] = { "6.4.9", "6.4.7", "6.3.12" };
    const char *v = OLD[rng_next(r) % 3];
    VNode *src = vfs_lookup(&m->disk, "/lib/modules/6.4.11");
    if (!src || src->kind != VN_DIR) return;
    char dst[NOM_PATH_MAX];
    snprintf(dst, sizeof dst, "/lib/modules/%s", v);
    if (vfs_lookup(&m->disk, dst)) return;
    vfs_mkdir(&m->disk, dst);
    for (VNode *k = src->child; k; k = k->next) {
        if (k->kind != VN_FILE) continue;
        char p2[NOM_PATH_MAX];
        snprintf(p2, sizeof p2, "%s/%s", dst, k->name);
        VNode *n = vfs_mkfile(&m->disk, p2, "");
        if (!n) continue;
        buf_clear(&n->data);
        buf_put(&n->data, k->data.p, k->data.len);
        n->mode = k->mode;
    }
    vfs_remove(&m->disk, "/lib/modules/6.4.11");
    snprintf(d, ds, "the only modules installed are %s's; the kernel is "
                    "6.4.11", v);
}

/* A VALID KERNEL IMAGE OF THE WRONG VERSION.
 *
 * A restore from a backup, or an image copied over the top of another one.
 * The file is where it belongs, it has the right name, it loads, and it is
 * last release's kernel -- so the modules on this disk were never built for
 * it. The FILENAME says 6.4.11 and the IMAGE says otherwise, which is the
 * one thing a filename cannot be trusted about, and the loader prints the
 * version it actually read. `pkg reinstall kernel-default`. */
static void fault_kernel_version(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *OLD[] = { "6.4.9", "6.4.7", "6.3.12" };
    const char *v = OLD[rng_next(r) % 3];
    VNode *n = vfs_lookup(&m->disk, "/boot/vmnomuz-6.4.11");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_printf(&n->data, "\x7fKRNL %s rv64\n", v);
    snprintf(d, ds, "/boot/vmnomuz-6.4.11 is really a %s image", v);
}

/* THE TWO SYMLINKS IN /boot, WRITTEN THE WRONG WAY ROUND.
 *
 * A rebuild script that took its arguments in the other order. Both files are
 * present and perfect, both links resolve to something real, `ls /boot` looks
 * completely healthy -- and the loader is handed an initrd where it expects a
 * kernel and says so, because a kernel image has a magic number and this is
 * not it. `stat` on the two links is four seconds and the whole answer. */
static void fault_boot_symlink_swap(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *k = vfs_lookup(&m->disk, "/boot/vmnomuz");
    VNode *i = vfs_lookup(&m->disk, "/boot/initrd");
    if (!k || k->kind != VN_LINK || !i || i->kind != VN_LINK) return;
    vfs_remove(&m->disk, "/boot/vmnomuz");
    vfs_remove(&m->disk, "/boot/initrd");
    vfs_symlink(&m->disk, "/boot/initrd-6.4.11", "/boot/vmnomuz");
    vfs_symlink(&m->disk, "/boot/vmnomuz-6.4.11", "/boot/initrd");
    snprintf(d, ds, "/boot/vmnomuz and /boot/initrd point at each other's "
                    "images");
}

/* THE FIRST HINT IN THE PREVIOUS ADMINISTRATOR'S NOTES, AND THE GAME HAD NO
 * FAULT THAT PRODUCED IT.
 *
 * "1. /boot/vmnomuz is a SYMLINK. When somebody deletes the versioned image,
 * `ls /boot` looks completely fine. stat it." There is a haiku about it on
 * the wiki as well. Three playtesters have quoted that note back; none of
 * them ever drew it, because the only way to get there was for the random
 * mutation to happen to delete one particular file out of a hundred and
 * twenty-eight.
 *
 * `ls /boot` shows the name. `stat` shows there is nothing at the end of it,
 * and the loader says which path it could not follow. The versioned image is
 * package content, so `pkg verify kernel-default` names it and a reinstall
 * puts it back -- the symlink was never wrong. */
static void fault_dangling_kernel(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *l = vfs_lookup(&m->disk, "/boot/vmnomuz");
    if (!l || l->kind != VN_LINK) return;
    if (!vfs_lookup(&m->disk, "/boot/vmnomuz-6.4.11")) return;
    vfs_remove(&m->disk, "/boot/vmnomuz-6.4.11");
    snprintf(d, ds, "deleted /boot/vmnomuz-6.4.11: the symlink is still there "
                    "and points at nothing");
}

/* AN INITRD BUILT FOR ANOTHER KERNEL.
 *
 * Not empty, not corrupt, not foreign hardware: a complete image full of the
 * right modules for the kernel that was running when somebody rebuilt it,
 * which is not the kernel installed now. `mkinitrd` builds one for the kernel
 * that is here, and that is the entire repair -- no package is damaged and
 * reinstalling anything achieves nothing. */
static void fault_initrd_version(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *OLD[] = { "6.4.9", "6.4.7", "6.3.12" };
    const char *v = OLD[rng_next(r) % 3];
    VNode *n = vfs_lookup(&m->disk, "/boot/initrd-6.4.11");
    if (!n || n->kind != VN_FILE) return;
    Buf out = {0};
    buf_printf(&out, "\x7fINITRD %s\n", v);
    /* keep the module list: the image is complete, it is simply not ours */
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    const char *nl = p ? memchr(p, '\n', (size_t)(end - p)) : NULL;
    if (nl) buf_put(&out, nl + 1, (size_t)(end - nl - 1));
    buf_clear(&n->data);
    buf_put(&n->data, out.p, out.len);
    buf_free(&out);
    snprintf(d, ds, "the initrd on this disk was built for %s", v);
}

/* TWO COMMANDS IN /etc/inittab, AND INIT RUNS THE LAST ONE.
 *
 * Somebody was testing single-user mode and added a line rather than editing
 * the one that was there. The correct line is still in the file, three
 * characters above the wrong one, which is what makes this different from an
 * inittab that simply names the wrong script: the machine is not misconfigured
 * so much as ambiguous, and init resolves the ambiguity the way init always
 * has. The comment in the shipped file warns about exactly this. */
static void fault_inittab_second(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *T[] = {
        "/bin/rc /etc/rc.d/rc.single",
        "/bin/rc /etc/rc.boot.new",
        "/sbin/init.debug /etc/rc.boot",
    };
    const char *t = T[rng_next(r) % 3];
    VNode *n = vfs_lookup(&m->disk, "/etc/inittab");
    if (!n || n->kind != VN_FILE) return;
    if (n->data.len && n->data.p[n->data.len - 1] != '\n') buf_putc(&n->data, '\n');
    buf_puts(&n->data, "# temporary, while I test the new bootstrap. -- R.\n");
    buf_puts(&n->data, t);
    buf_putc(&n->data, '\n');
    snprintf(d, ds, "a second command in /etc/inittab (%s), which is the one "
                    "init runs", t);
}

/* THE SHELL IS NOT ON THE LIST.
 *
 * A hardening pass pruned /etc/shells to the shells it approved of, and
 * /bin/sh was not one of them because whoever wrote the list was working from
 * another distribution's. The account is fine, the shell is present and
 * executable, every service is running, and there is no way in. The machine
 * says which file it consulted, which is the only reason this is findable at
 * all -- nobody looks at /etc/shells twice a decade. */
static void fault_shells(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *LISTS[] = {
        "# approved interactive shells -- security review, 14 May\n"
        "/bin/bash\n/bin/dash\n",
        "# pruned to what we actually support. -- R.\n"
        "/bin/nomsh\n/bin/false\n",
        "/bin/false\n",
    };
    const char *l = LISTS[rng_next(r) % 3];
    VNode *n = vfs_lookup(&m->disk, "/etc/shells");
    if (!n || n->kind != VN_FILE) return;
    buf_clear(&n->data);
    buf_puts(&n->data, l);
    snprintf(d, ds, "/etc/shells no longer lists /bin/sh, which is root's "
                    "login shell");
}

/* THE CONSOLE IS HANDED TO AN ACCOUNT THAT IS NOT THERE.
 *
 * The runlevel script names who gets the terminal, and somebody changed it to
 * the operations account during a migration that never finished. The machine
 * boots perfectly, every service is up, and getty has nobody to hand it to.
 * `/etc/passwd` is completely correct, which is the trap: the wrong file is
 * the one nobody thinks of as an account file at all. */
static void fault_getty_user(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *WHO[] = { "ops", "console", "admin", "operator" };
    const char *who = WHO[rng_next(r) % 4];
    VNode *n = vfs_lookup(&m->disk, "/etc/rc.d/rc.3");
    if (!n || n->kind != VN_FILE) return;
    char line[96];
    snprintf(line, sizeof line, "exec /sbin/getty %s", who);
    rewrite_line(m, "/etc/rc.d/rc.3", "exec /sbin/getty", line);
    snprintf(d, ds, "rc.3 gives the console to %s, who has no account", who);
}

/* A UNIT THAT LOST ITS NAME.
 *
 * The service starts. It runs. It is healthy. And everything ordered after it
 * waits forever, because a unit with no `name:` is known by its filename, and
 * `after: syslog` does not match `syslog.svc`.
 *
 * This is the one dependency fault where the thing being waited for is
 * PRESENT AND WELL, so every reflex -- is it enabled, is it in this runlevel,
 * is it installed -- comes back yes. The console has both halves one line
 * apart: `started syslog.svc` and `waiting for syslog`. */
static void fault_unit_no_name(Machine *m, Rng *r, char *d, size_t ds)
{
    static const char *HUBS[] = { "syslog", "net", "udev" };
    const char *hub = HUBS[rng_next(r) % 3];
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/etc/services.d/%s.svc", hub);
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE) return;
    size_t before = n->data.len;
    rewrite_line(m, path, "name:", NULL);
    if (n->data.len == before) return;
    snprintf(d, ds, "the %s unit has no name line, so it starts as %s.svc and "
                    "nothing ordered after it ever runs", hub, hub);
}

/* THE CONFIGURATION AGENT'S OWN COPY, BOUND OVER THE REAL ONE.
 *
 * `pkg verify` is COMPLETELY CLEAN. Nothing is corrupt, nothing is missing,
 * nothing has the wrong mode. The file you `cat` is the right file, and the
 * daemon read a different one, because a unit nobody installed binds a
 * directory over the top of /etc/httpd before any service starts -- which is
 * exactly what an estate-management agent does, and exactly what note 9 in
 * the previous administrator's notes is about.
 *
 * The evidence is in three places and nowhere else: the boot log says it
 * bound something, `ns <pid>` on the dead daemon shows the binding, and
 * `pkg owns /etc/services.d/site-config.svc` answers nothing. The repair is
 * to delete a file no package owns. */
static void fault_ns_bind_unit(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct {
        const char *dir, *file, *body, *over, *unit, *why;
    } B[] = {
      { "/opt/sitecfg/httpd", "/opt/sitecfg/httpd/httpd.conf",
        "# site policy, managed centrally. Do not edit on the host.\n"
        "Listen 80\n"
        "DocumentRoot /srv/sites/default\n"
        "ServerName nominal.local\n",
        "/etc/httpd", "site-config",
        "a site-config unit binds /opt/sitecfg/httpd over /etc/httpd, and its "
        "document root does not exist here" },
      /* Over /etc/audit, not over /etc. Binding the whole of /etc takes every
       * daemon on the machine down at once and reads as a catastrophe rather
       * than as a puzzle -- and it is the fault rc.boot's leftover bind
       * already is. One directory, one daemon, and everything else perfect is
       * the harder and better ticket. */
      { "/opt/sitecfg/audit", "/opt/sitecfg/audit/auditd.conf",
        "# site policy, managed centrally. Do not edit on the host.\n"
        "log_file = /var/audit/trail\n"
        "max_log_file = 32\n",
        "/etc/audit", "site-config",
        "a site-config unit binds /opt/sitecfg/audit over /etc/audit, and the "
        "trail it names is not there" },
    };
    int i = (int)(rng_next(r) % 2);
    char unit[NOM_PATH_MAX];
    snprintf(unit, sizeof unit, "/etc/services.d/%s.svc", B[i].unit);
    if (vfs_lookup(&m->disk, unit)) return;
    if (vfs_lookup(&m->disk, B[i].dir)) return;

    vfs_mkdir(&m->disk, "/opt");
    vfs_mkdir(&m->disk, "/opt/sitecfg");
    vfs_mkdir(&m->disk, B[i].dir);
    VNode *f = vfs_mkfile(&m->disk, B[i].file, B[i].body);
    if (!f) return;
    f->mode = 0644;

    char body[512];
    snprintf(body, sizeof body,
             "# dropped in by the estate agent installer -- managed centrally\n"
             "name: %s\n"
             "description: site configuration overlay\n"
             "bind: %s %s\n"
             "enabled: yes\n"
             "runlevel: 3 5\n", B[i].unit, B[i].dir, B[i].over);
    VNode *n = vfs_mkfile(&m->disk, unit, body);
    if (n) n->mode = 0644;
    snprintf(d, ds, "%s", B[i].why);
}

/* THE WRITE BIT OFF ONE DAEMON'S STATE DIRECTORY.
 *
 * Not /run, which takes the whole machine down at once and reads as madness.
 * One directory, belonging to one service, so exactly one thing on the
 * machine stops and everything else is perfect -- which is a much harder
 * ticket, because there is no pattern to notice. Every file in it lists and
 * reads; the daemon cannot create the one file it keeps there.
 *
 * `pkg verify` says `mode` on a DIRECTORY, which is a line most people have
 * never seen, and `chmod 755` is the repair. */
static void fault_ro_spool(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *path, *who; } S[] = {
        { "/var/lib/ntp", "ntpd, which keeps its drift file there" },
        { "/run/nomde",   "the display server, which keeps its socket there" },
    };
    int i = (int)(rng_next(r) % 2);
    VNode *n = vfs_lookup(&m->disk, S[i].path);
    if (!n || n->kind != VN_DIR) return;
    n->mode = 0555;
    snprintf(d, ds, "%s is read-only: %s cannot write", S[i].path, S[i].who);
}

/* /etc/shadow, TIGHTENED UNTIL NOTHING CAN READ IT.
 *
 * The same hand that prunes /etc/shells. The file is byte-for-byte correct --
 * `pkg verify` says `mode` and not `changed` -- and login is impossible,
 * because the thing that authenticates cannot open the file that
 * authenticates. The machine boots, every service runs, and it is unusable.
 * `ls -l /etc/shadow`, and `chmod 600`. */
static void fault_shadow_mode(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/etc/shadow");
    if (!n || n->kind != VN_FILE) return;
    if (n->mode == 0000) return;
    n->mode = 0000;
    snprintf(d, ds, "/etc/shadow is mode 0000: nothing can read the passwords");
}

/* A CONFIG THAT STOPS IN THE MIDDLE AND STILL PARSES.
 *
 * The disk filled, or the editor was killed, or the copy was interrupted --
 * and what is left is a perfectly valid file that is missing everything after
 * a certain line. Nothing is malformed. There is no error to find. The daemon
 * reads it, cannot see the one directive that was in the part that never got
 * written, and refuses to start.
 *
 * `pkg diff` is what shows it, because a file that simply ENDS reads very
 * differently from a file with a line changed -- and it is the only fault
 * here whose evidence is what is absent from the bottom of a file. */
static void fault_conf_truncated(Machine *m, Rng *r, char *d, size_t ds)
{
    static const struct { const char *path; int keep; const char *what; } C[] = {
        { "/etc/httpd/httpd.conf",  1, "the document root" },
        { "/etc/crontab",           4, "every job" },
        { "/etc/nftables.conf",     0, "the whole ruleset" },
    };
    int i = (int)(rng_next(r) % 3);
    VNode *n = vfs_lookup(&m->disk, C[i].path);
    if (!n || n->kind != VN_FILE || !n->data.len) return;

    size_t at = 0;
    int lines = 0;
    while (at < n->data.len && lines < C[i].keep) {
        while (at < n->data.len && n->data.p[at] != '\n') at++;
        if (at < n->data.len) at++;
        lines++;
    }
    if (at == n->data.len) return;
    n->data.len = at;
    snprintf(d, ds, "%s stops after %d line(s): %s never got written",
             C[i].path, C[i].keep, C[i].what);
}

/* THE DOCUMENT ROOT IS A FILE.
 *
 * An archive unpacked one level too high, or a `cp` where a `cp -r` was
 * meant: /srv/www is not missing, it is a file called /srv/www with a web
 * page in it. `ls /srv` lists it, `cat` reads it, the config names it, and
 * the only tool that tells you what it IS rather than that it is there is
 * `stat`. A different sentence out of httpd and a different half-second of
 * confusion from a directory that was deleted. */
static void fault_docroot_file(Machine *m, Rng *r, char *d, size_t ds)
{
    (void)r;
    VNode *n = vfs_lookup(&m->disk, "/srv/www");
    if (!n || n->kind != VN_DIR) return;
    if (!vfs_remove(&m->disk, "/srv/www")) return;
    VNode *f = vfs_mkfile(&m->disk, "/srv/www",
        "<html><body>this machine</body></html>\n");
    if (f) f->mode = 0644;
    snprintf(d, ds, "/srv/www is a file, not a directory: the document root "
                    "was unpacked one level too high");
}

/* THE TABLE IS NAMED NOW, AND THAT IS A MEASUREMENT TOOL, NOT DECORATION.
 *
 * A blind playtester sampled fifty-five ticket openings and met about a fifth
 * of the fault classes the in-game documentation promises. Nobody could argue
 * with that because nobody could count: the generator drew a function pointer
 * out of an anonymous array and threw away which one it was, so "is this
 * fault reachable?" had no answer short of reading the diff of every ticket.
 * The names are what `make faults` counts, and NOM_FORCE_FAULT takes one
 * instead of an index nobody can read. */
typedef void (*StructuralFault)(Machine *, Rng *, char *, size_t);
static const struct { const char *name; StructuralFault fn; } STRUCTURAL[] = {
    { "bootsector", fault_bootsector }, { "stray_unit", fault_stray_unit },
    { "wrong_uuid", fault_wrong_uuid }, { "missing_module", fault_missing_module },
    { "bad_libc", fault_bad_libc }, { "wrong_arch", fault_wrong_arch },
    { "ldsoconf", fault_ldsoconf },
    { "bad_shell", fault_bad_shell }, { "no_root", fault_no_root },
    { "unclean_shutdown", fault_unclean_shutdown },
    { "wrong_channel", fault_wrong_channel }, { "fstab", fault_fstab },
    { "daemon_config", fault_daemon_config },
    { "daemon_directive", fault_daemon_directive },
    { "disk_full", fault_disk_full }, { "bad_bind", fault_bad_bind },
    { "dir_mode", fault_dir_mode }, { "root_ro", fault_root_ro },
    { "bad_libz", fault_bad_libz }, { "fstype", fault_fstype },
    { "missing_dir", fault_missing_dir }, { "wellmeant", fault_wellmeant },
    { "dep_disabled", fault_dep_disabled },
    { "inodes", fault_inodes }, { "iface_rename", fault_iface_rename },
    { "half_upgrade", fault_half_upgrade },
    /* the second generation */
    { "mount_shadow", fault_mount_shadow }, { "dangling_lib", fault_dangling_lib },
    { "lib_shadow", fault_lib_shadow }, { "wrong_runlevel", fault_wrong_runlevel },
    { "dep_cycle", fault_dep_cycle },
    { "after_ghost", fault_after_ghost },
    { "hardening_sweep", fault_hardening_sweep },
    { "wrong_binary", fault_wrong_binary },
    { "stale_kernel_entry", fault_stale_kernel_entry },
    { "foreign_initrd", fault_foreign_initrd }, { "no_shadow", fault_no_shadow },
    { "passwd_fields", fault_passwd_fields }, { "docroot", fault_docroot },
    { "exec_path", fault_exec_path },
    { "inittab_target", fault_inittab_target },
    { "rcboot_need", fault_rcboot_need }, { "ro_dir", fault_ro_dir },
    { "cache_full", fault_cache_full },
    /* the third generation, weighted at the stages the survey said were thin */
    { "boot_order", fault_boot_order }, { "zbl_default", fault_zbl_default },
    { "zbl_dup_entry", fault_zbl_dup_entry },
    { "zbl_rootdev", fault_zbl_rootdev },
    { "module_mismatch", fault_module_mismatch },
    { "kernel_version", fault_kernel_version },
    { "boot_symlink_swap", fault_boot_symlink_swap },
    { "dangling_kernel", fault_dangling_kernel },
    { "initrd_version", fault_initrd_version },
    { "inittab_second", fault_inittab_second },
    { "shells", fault_shells }, { "getty_user", fault_getty_user },
    { "unit_no_name", fault_unit_no_name },
    { "ns_bind_unit", fault_ns_bind_unit }, { "ro_spool", fault_ro_spool },
    { "shadow_mode", fault_shadow_mode },
    { "conf_truncated", fault_conf_truncated },
    { "docroot_file", fault_docroot_file },
};
#define NSTRUCT ((int)(sizeof STRUCTURAL / sizeof STRUCTURAL[0]))

int breaker_fault_count(void) { return NSTRUCT; }
const char *breaker_fault_name(int i)
{
    return (i >= 0 && i < NSTRUCT) ? STRUCTURAL[i].name : "?";
}

/* WHAT THE TICKET IS MADE OF, recorded as it is applied.
 *
 * Only the accepted attempt survives: every retry resets it, so what is left
 * at the end is what the player was actually handed. */
static char g_dealt[512];
static void dealt_add(const char *tag)
{
    size_t l = strlen(g_dealt);
    if (l && l + 1 < sizeof g_dealt) g_dealt[l++] = ' ';
    snprintf(g_dealt + l, sizeof g_dealt - l, "%s", tag);
}
const char *breaker_dealt(void) { return g_dealt; }

typedef void (*Mutation)(Vfs *, const char *, Rng *, char *, size_t);
static const Mutation MUTATION[] = {
    mut_delete, mut_truncate, mut_flip, mut_zero,
    mut_line, mut_line, mut_line,     /* line surgery is the commonest, so
                                       * weight it: config damage should be
                                       * more likely than a bad block */
    mut_mode, mut_relink,
};
static const char *const MUTNAME[] = {
    "-delete", "-truncate", "-flip", "-zero",
    "-line", "-line", "-line", "-mode", "-relink",
};
#define NMUT ((int)(sizeof MUTATION / sizeof MUTATION[0]))

/* A configuration edited AFTER the machine came up, and never reloaded.
 *
 * This one has to happen post-boot by construction: reboot the machine and the
 * daemon reads the new file and the fault evaporates, which is exactly why it
 * is such a miserable thing to diagnose in real life. Nothing is corrupt,
 * `pkg verify` is clean, `svc` says running, and the machine does not do what
 * its configuration plainly says it does. The fix is a signal, not a file.
 */
static const struct { const char *path, *from, *to; } STALE_EDITS[] = {
    { "/etc/nftables.conf",   "inet",       "ip"        },
    { "/etc/ntp.conf",        "10.0.2.3",   "10.0.2.7"  },
    { "/etc/httpd/httpd.conf","Listen 80",  "Listen 8080" },
    { "/etc/net/interfaces",  "eth0",       "eth1"      },
    { "/etc/nomde/nomde.conf","/run/nomde/requests", "/run/nomde/socket" },
    { "/etc/audit/auditd.conf","/var/log/audit.log",  "/var/log/audit/trail" },
};
#define NSTALE ((int)(sizeof STALE_EDITS / sizeof STALE_EDITS[0]))

/* Replace the first occurrence of `from` with `to`. Returns false if the file
 * does not contain it, which is how a machine whose local edits have already
 * moved that line declines the fault rather than pretending. */
static bool text_sub(Machine *m, const char *path, const char *from,
                     const char *to)
{
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE) return false;
    Buf out = {0};
    const char *p = n->data.p, *end = n->data.p + n->data.len;
    size_t fl = strlen(from);
    bool hit = false;
    while (p < end) {
        if (!hit && (size_t)(end - p) >= fl && memcmp(p, from, fl) == 0) {
            buf_puts(&out, to);
            p += fl;
            hit = true;
            continue;
        }
        buf_putc(&out, *p++);
    }
    if (hit) { buf_clear(&n->data); buf_put(&n->data, out.p, out.len); }
    buf_free(&out);
    return hit;
}

/* THE OTHER HALF OF THE SAME LESSON, AND THE HARDER ONE: THE FILE IS RIGHT.
 *
 * Somebody already found this fault and fixed it. They edited the config back
 * to what it should say, wrote the file, and did not restart the daemon --
 * and then went home, or handed the ticket on, or simply forgot. So `pkg
 * verify` is CLEAN, `cat` shows exactly the right thing, `svc` says running,
 * and the machine is still doing the wrong thing, because the process has
 * been holding the old file in memory since it started.
 *
 * The pair is applied around the boot: the wrong value is on disk while the
 * daemon reads it, and the right value is put back afterwards. That is the
 * only honest way to build it -- reboot the machine and it evaporates, which
 * is exactly why it is so miserable in life -- and it is what note 8 in the
 * previous administrator's notes has been promising: /run/*.state says what
 * each daemon really loaded, and `kill -HUP` is the repair.
 */
typedef struct { char path[NOM_PATH_MAX]; Buf orig; bool on; } StaleFix;

/* A NARROWER SET THAN THE POST-BOOT ONE, and the difference matters: this
 * value is on the disk WHILE THE DAEMON STARTS, so it has to be one the
 * daemon will accept. A wrong interface name or an audit path that is not
 * there stops the service dead, and then the file is corrected behind it and
 * the player is handed a dead daemon with a perfect config and no evidence at
 * all -- which is not a puzzle, it is a trap. */
static const int STALE_PRE[] = { 0, 1, 2, 4 };

static bool stale_pre(Machine *m, Rng *r, StaleFix *s)
{
    int i = STALE_PRE[rng_next(r) % (sizeof STALE_PRE / sizeof STALE_PRE[0])];
    VNode *n = vfs_lookup(&m->disk, STALE_EDITS[i].path);
    if (!n || n->kind != VN_FILE) return false;
    buf_clear(&s->orig);
    buf_put(&s->orig, n->data.p, n->data.len);
    /* The machine boots with the value somebody has SINCE corrected. */
    if (!text_sub(m, STALE_EDITS[i].path, STALE_EDITS[i].from, STALE_EDITS[i].to)) {
        buf_free(&s->orig);
        s->orig = (Buf){0};
        return false;
    }
    snprintf(s->path, sizeof s->path, "%s", STALE_EDITS[i].path);
    s->on = true;
    return true;
}

static void stale_post(Machine *m, StaleFix *s)
{
    VNode *n = vfs_lookup(&m->disk, s->path);
    if (n && n->kind == VN_FILE) {
        buf_clear(&n->data);
        buf_put(&n->data, s->orig.p, s->orig.len);
    }
    buf_free(&s->orig);
    s->orig = (Buf){0};
    s->on = false;
}

static bool fault_stale_config(Machine *m, Rng *r)
{
    int i = (int)(rng_next(r) % (uint64_t)NSTALE);
    /* The file now says what somebody has just decided it should say, and the
     * running process has not been told. */
    return text_sub(m, STALE_EDITS[i].path, STALE_EDITS[i].from,
                    STALE_EDITS[i].to);
}

/* How many draws in a hundred come from the designed table rather than from
 * random damage. Set once per ticket; see machine_break. */
static int g_struct_share = 25;

/* Where in the rotation this ticket starts, and how far it has walked. See
 * the long note in machine_corrupt: the fault is dealt, not rolled. */
static uint64_t g_pick_base = 0;
static int      g_pick_n = 0;

/* Damage one random file one random way. Returns false if the mutation was a
 * no-op (wrong kind of file for it, empty file), which the caller retries. */
bool machine_corrupt(Machine *m, Rng *r, char *what, size_t whatsz)
{
    /* Roughly one ticket in four is structural rather than a damaged file, so
     * `pkg reinstall` is not the answer often enough that the player cannot
     * rely on it.
     *
     * The share went from 15% to 25% when the structural set roughly doubled.
     * It has to: forty-odd designed faults drawn at 15% means most of them are
     * never met, and the whole point of them is that each one asks a different
     * question. The random mutations are still the majority, which is right --
     * they are the part nobody authored. */
    /* NOM_FORCE_FAULT=<n> pins the structural fault, so a new one can be
     * exercised without waiting for it to come up at 15%/17. Survey only
     * prints a sample of seeds, so "I did not see it" proves nothing. */
    const char *forced = getenv("NOM_FORCE_FAULT");
    if (forced) {
        char d[200] = "";
        int fi = -1;
        if (forced[0] >= '0' && forced[0] <= '9') fi = atoi(forced) % NSTRUCT;
        else for (int i = 0; i < NSTRUCT; i++)
            if (strcmp(STRUCTURAL[i].name, forced) == 0) fi = i;
        if (fi < 0) return false;
        STRUCTURAL[fi].fn(m, r, d, sizeof d);
        if (d[0]) { dealt_add(STRUCTURAL[fi].name);
                    snprintf(what, whatsz, "%s", d); return true; }
        return false;
    }
    if (rng_next(r) % 100 < g_struct_share) {
        /* NOT A COIN. A HAND OF CARDS, DEALT WITHOUT REPLACEMENT.
         *
         * Drawing the fault uniformly at random is not the same thing as a
         * player meeting the faults, and the difference is the whole of the
         * complaint. Sixty-one faults drawn independently means each one
         * comes up in about one ticket in eighty-two, so a dozen-ticket
         * session meets a dozen of them AT BEST and meets several of them
         * twice -- and a measured survey of four hundred tickets showed
         * exactly that: a flat histogram where every single fault sat near
         * one percent, which reads to a player as "the same handful over and
         * over" because the ones they draw twice are the ones they notice.
         *
         * So the table is a rotation instead. Both strides are coprime with
         * its length, so a run of NSTRUCT consecutive seeds deals every fault
         * in it exactly once, adjacent tickets are never handed the same
         * fault, and every fault is reachable inside a session rather than
         * eventually. `g_pick_n` advances only when a structural
         * fault is actually drawn, so a fault that cannot break THIS machine
         * hands its place to the next one instead of costing a slot. */
        int fi = (int)((g_pick_base + (uint64_t)g_pick_n++ * 7) % NSTRUCT);
        char d[200] = "";
        STRUCTURAL[fi].fn(m, r, d, sizeof d);
        if (d[0]) { dealt_add(STRUCTURAL[fi].name);
                    snprintf(what, whatsz, "%s", d); return true; }
        return false;
    }

    PathSet ps = { .n = 0 };
    collect(m->disk.root, "", &ps);
    if (ps.n == 0) return false;
    const char *path = ps.path[rng_next(r) % (uint64_t)ps.n];
    int mi = (int)(rng_next(r) % (uint64_t)NMUT);
    char d[200] = "";
    MUTATION[mi](&m->disk, path, r, d, sizeof d);
    if (!d[0]) return false;
    dealt_add(MUTNAME[mi]);
    snprintf(what, whatsz, "%s", d);
    return true;
}

/* IS THIS TICKET AIR-GAPPED? A property of the seed, so every front end
 * agrees without having to pass it around.
 *
 * It used to be ((seed / 7) % 5) == 0, and dividing by seven made SEVEN
 * ADJACENT SEEDS IDENTICAL. A player does not sample seeds at random, they
 * take tickets in order, so what that arithmetic produced was runs: a
 * playtester drew seeds 2206-2210 and got five air-gapped calls in a row, and
 * an air-gapped call is a whole different set of tools. One in five is the
 * right RATE and it was never the problem; the correlation between
 * neighbours was. Hashing the seed keeps the rate and kills the run.
 */
bool machine_airgapped(uint64_t seed)
{
    Rng r;
    rng_seed(&r, seed ^ 0xa17c9a99e5b1d3c7ULL);
    return rng_next(&r) % 5 == 0;
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

    /* WHAT KIND OF TICKET THIS IS GOING TO BE.
     *
     * A blind playtester played sixteen boots and never once got a machine
     * that was UP and wrong: "it would double the FEEL of variety more than
     * another twenty boot-time faults would, because it changes the whole
     * shape of the session -- no rescue medium, no /mnt, different tools,
     * different customer conversation."
     *
     * They were unlucky rather than blocked -- the class was drawing at about
     * one ticket in four -- but one in four is a coin that lands the wrong way
     * three times in a row often enough to matter, and every fault added to
     * the boot chain makes it rarer. So the MIX is decided first and the
     * generator keeps drawing until it has that kind of ticket. Nothing about
     * the fault itself is chosen or faked: the machine is still broken at
     * random and still has to prove it by failing, and a run of attempts that
     * cannot produce the wanted shape falls back to taking what it has, so a
     * ticket is always produced.
     *
     * Three shapes, one third each: a machine that will not boot, a machine
     * that is up with something dead on it, and a machine that is up and
     * running a configuration nobody reloaded. */
    enum { WANT_ANY, WANT_UP, WANT_STALE };
    int want = WANT_ANY;
    {
        /* AND THE SHAPE IS DEALT ROUND THE TABLE TOO, FOR THE SAME REASON.
         *
         * Rolling it independently per seed gives the right rate and the
         * wrong experience: a playtester drew eleven consecutive networked
         * won't-boots, which is a run an independent coin produces regularly
         * and which reads as "this game only has one kind of ticket". A
         * player takes tickets in sequence; what they feel is the sequence,
         * not the histogram. So the eight shapes are laid out in an order
         * that has no long run in it and consecutive seeds read consecutive
         * entries -- at the rates the last round of playtesting settled on,
         * which were never the problem. The PHASE of the cycle is hashed per
         * block of eight so the pattern is not literally "every fourth call
         * is the one that is up and wrong". */
        static const int CYCLE[8] = {
            WANT_ANY, WANT_UP, WANT_ANY, WANT_ANY,
            WANT_STALE, WANT_ANY, WANT_ANY, WANT_ANY,
        };
        Rng pr;
        rng_seed(&pr, (seed / 8) ^ 0x5bf03635e9a1c4d3ULL);
        want = CYCLE[(seed + rng_next(&pr)) % 8];

        /* And where in the fault table this ticket's hand starts. */
        rng_seed(&pr, (seed / (uint64_t)NSTRUCT) ^ 0x3c79ac492ba7b653ULL);
        g_pick_base = (seed * 23 + rng_next(&pr)) % (uint64_t)NSTRUCT;
        g_pick_n = 0;
    }
    {
        const char *f = getenv("NOM_FORCE_STALE");
        if (f) want = WANT_STALE;
        if (getenv("NOM_FORCE_UP")) want = WANT_UP;
    }
    /* After this many tries the wanted shape is not happening on this seed --
     * usually because the one structural fault it keeps drawing takes the
     * boot down -- and a ticket that exists beats a ticket of the right
     * flavour that does not. */
    const int GIVE_UP = 150;

    for (int attempt = 0; attempt < 400; attempt++) {
        machine_free(m);
        machine_install(m, seed);
        g_dealt[0] = '\0';
        Rng r;
        rng_seed(&r, (seed ^ 0x9e3779b97f4a7c15ULL) + (uint64_t)attempt * 0x2545f491ULL);

        /* Half of the WANT_STALE draws are the harder shape: the wrong value
         * has to be on the disk while the daemon reads it, so the edit goes
         * in before the boot and is corrected after it. */
        StaleFix sf = {0};
        if (want == WANT_STALE && attempt < GIVE_UP && (attempt & 1) == 0)
            stale_pre(m, &r, &sf);

        char all[512] = "";
        int applied = 0;
        /* A STALE TICKET IS THE WHOLE TICKET. The edit around the boot is the
         * fault, so nothing else is broken -- which is the entire point of
         * the class: `pkg verify` comes back clean and the player has to
         * stop trusting it. Corrupting something as well left a stray
         * unrelated file in `pkg verify` on every one of these, which is the
         * one thing that would have given the answer away. */
        bool pure_stale = (want == WANT_STALE && nfaults == 1 &&
                           attempt < GIVE_UP);
        if (!pure_stale) {
            for (int guard = 0; guard < 64 && applied < nfaults; guard++) {
                char d[200];
                if (!machine_corrupt(m, &r, d, sizeof d)) continue;
                if (applied) strncat(all, "; ", sizeof all - strlen(all) - 1);
                strncat(all, d, sizeof all - strlen(all) - 1);
                applied++;
            }
            if (applied < nfaults) { if (sf.on) stale_post(m, &sf); continue; }
        }

        machine_boot(m);

        /* THE TWO WAYS A RUNNING DAEMON ENDS UP OUT OF STEP WITH ITS FILE,
         * both of which have to happen around the boot rather than before it.
         *
         * One: the file was edited after the machine came up and nothing was
         * reloaded, so the disk says the new thing and the process is doing
         * the old one. Two: somebody already FOUND that and corrected the
         * file, and still did not reload it -- so `pkg verify` is clean, the
         * config reads perfectly, and the machine is doing the wrong thing
         * anyway. The second is note 8 in the previous administrator's notes,
         * and until now the game had never once produced it. */
        if (sf.on) {
            stale_post(m, &sf);
            if (m->boot.running) {
                Buf sick2 = {0};
                int d2 = kernel_health(m, &sick2);
                buf_free(&sick2);
                if (d2 > 0) {
                    dealt_add("stale-corrected");
                    machine_rebaseline_local(m);
                    if (what) snprintf(what, whatsz,
                        "a config was corrected and the daemon was never "
                        "reloaded: the file is right and the process is not");
                    customer_brief(m, "somebody had a go at it before you and "
                                      "did not restart anything");
                    return true;
                }
            }
        } else if (m->boot.running && want == WANT_STALE) {
            if (fault_stale_config(m, &r)) {
                Buf sick2 = {0};
                int d2 = kernel_health(m, &sick2);
                buf_free(&sick2);
                if (d2 > 0) {
                    dealt_add("stale-edited");
                    if (what) snprintf(what, whatsz,
                        "a config was edited after boot and never reloaded");
                    customer_brief(m, "changed a setting and did not restart it");
                    return true;
                }
            }
        }

        /* A ticket is a machine that is NOT HEALTHY, which is a wider and
         * truer thing than a machine that will not boot. "It comes up and the
         * firewall is not running" is a real call, and a nastier one, because
         * nothing announces it. */
        Buf sick = {0};
        int dead = kernel_health(m, &sick);
        buf_free(&sick);
        /* Not the shape this ticket is supposed to be: put it back and draw
         * again. Every attempt is an independently broken machine, so this
         * costs nothing but time and biases nothing except the mix. */
        if (attempt < GIVE_UP && want != WANT_ANY &&
            (!m->boot.running || dead == 0))
            continue;
        if (!m->boot.running || dead > 0) {
            /* The ticket is settled. Whatever the breaker did to a file that
             * also carries a local edit is now part of what the player was
             * handed, not something they destroyed. */
            machine_rebaseline_local(m);
            if (what) {
                if (m->boot.running && dead > 0) {
                    char tmp[512];
                    snprintf(tmp, sizeof tmp, "%s (boots, %d service(s) dead)",
                             all, dead);
                    snprintf(what, whatsz, "%s", tmp);
                } else {
                    snprintf(what, whatsz, "%s", all);
                }
            }
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

/* ==================================================== THE WORLD'S OWN DAMAGE
 *
 * D23 said it in one line -- *the world supplies the cause* -- and until now
 * nothing in the running tower could supply one. Faults arrived because a
 * ticket was generated; a machine you installed, cabled and ran for forty
 * days never broke.
 *
 * These three entry points are how the building breaks a box. The rule they
 * exist to keep is that there is NO SECOND KIND OF BROKEN: a blackout
 * truncates real bytes in the real Vfs, a bad sector nulls real bytes in a
 * real file, and `pkg verify` finds them because they are genuinely different
 * from what shipped -- not because anything anywhere holds a flag saying so.
 * Everything below is machinery this file already had, with the world holding
 * the other end of it.
 */

/* ONE LINE INTO THE MACHINE'S OWN SYSLOG, which is where a real daemon writes
 * and where the player greps. This is the whole of "the cause must be
 * findable": a UPS that carried a box through a mains failure says so here,
 * and a disk that is reallocating sectors complains here for days before it
 * finally loses one. Nothing is invented -- if the disk is full the line does
 * not fit, which is exactly what a full disk does to a log. */
void breaker_syslog(Machine *m, const char *line)
{
    VNode *n = vfs_lookup(&m->disk, "/var/log/messages");
    if (!n) n = vfs_mkfile(&m->disk, "/var/log/messages", "");
    if (!n || n->kind != VN_FILE) return;
    size_t need = strlen(line) + 1;
    if (m->fs_capacity && machine_disk_used(m) + need > m->fs_capacity) return;
    buf_puts(&n->data, line);
    buf_putc(&n->data, '\n');
}

/* THE LIGHTS WENT OUT WHILE IT WAS RUNNING.
 *
 * `writing` is not a die roll: the caller passes whether this box actually
 * moved frames in the busy period that had just finished, so the machine that
 * was serving a floor's files loses what it had in flight and the one nobody
 * had touched since it was installed comes back dirty and complete. That is
 * the same fault_unclean_shutdown the ticket generator has always used, so
 * the console text, the fsck, the `pkg verify` afterwards and the `pkg diff`
 * that says SHORT rather than edited are all the ones already proven by
 * --solve. */
void breaker_powerfail(Machine *m, Rng *r, bool writing, char *d, size_t ds)
{
    if (writing) {
        fault_unclean_shutdown(m, r, d, ds);
        if (d[0]) return;
    }
    m->fs_dirty = true;
    m->fs_lost = 0;
    snprintf(d, ds, "unclean shutdown: filesystem marked dirty");
}

/* A SECTOR THAT WILL NOT READ BACK.
 *
 * A disk that has been spinning for months reallocates sectors until it runs
 * out of spares, and then a block of a file is gone: not truncated, not
 * edited -- five hundred and twelve bytes of nothing in the middle of it,
 * which is what a read error handed back as zeroes looks like on the day
 * somebody finally notices. `pkg verify` reports the file CHANGED and `pkg
 * diff` names the byte, which is a different sentence from the truncation a
 * blackout leaves, and telling those two apart is the whole point of having
 * both tools.
 *
 * WHICH FILE, AND THE JUDGEMENT IN IT. Package-owned configuration under
 * /etc, because that is what `pkg reinstall` can put back and what the player
 * can see the consequence of. The files the boot chain itself reads are
 * excluded, so the box comes up and can be worked on from its own shell. That
 * is a fairness decision rather than a physical one -- a real bad sector does
 * not care -- and it is written down here and in the fault catalogue rather
 * than hidden, because the blackout above already covers the machine that
 * will not boot at all. */
static bool boot_critical(const char *p)
{
    static const char *NO[] = {
        "/etc/fstab", "/etc/passwd", "/etc/shadow", "/etc/group",
        "/etc/inittab", "/etc/ld.so.conf", "/etc/shells", "/etc/rc.",
        "/etc/services.d/", "/etc/zbl", "/etc/net/interfaces", NULL
    };
    for (int i = 0; NO[i]; i++)
        if (strncmp(p, NO[i], strlen(NO[i])) == 0) return true;
    return false;
}

bool breaker_bad_sector(Machine *m, Rng *r, char *d, size_t ds)
{
    const char *cand[128];
    int nc = 0;
    for (int i = 0; i < m->npkg && nc < 128; i++) {
        const Package *p = m->pkg[i];
        if (!p) continue;
        for (int j = 0; j < p->nfiles && nc < 128; j++) {
            const PkgFile *f = &p->file[j];
            if (f->isdir || f->link || !f->content) continue;
            if (strncmp(f->path, "/etc/", 5) != 0) continue;
            if (boot_critical(f->path)) continue;
            if (strlen(f->content) < 48) continue;
            cand[nc++] = f->path;
        }
    }
    if (!nc) return false;
    const char *path = cand[rng_next(r) % (uint64_t)nc];
    VNode *n = vfs_lookup(&m->disk, path);
    if (!n || n->kind != VN_FILE || n->data.len < 48) return false;
    size_t sectors = (n->data.len + 511) / 512;
    size_t at = (size_t)(rng_next(r) % sectors) * 512;
    size_t end = at + 512;
    if (end > n->data.len) end = n->data.len;
    if (end <= at) return false;
    for (size_t i = at; i < end; i++) n->data.p[i] = '\0';
    snprintf(d, ds, "bad sector: %d bytes of %s at offset %d read back as zeroes",
             (int)(end - at), path, (int)at);
    return true;
}
