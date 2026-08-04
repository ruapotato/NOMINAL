/* boot.c — the boot chain.
 *
 * THE RULE (D17): every stage reads real files and fails because of what it
 * finds. Nothing in this file may ask "which fault was injected?" — there is
 * no such question to ask, because the breaker only ever edits the disk.
 *
 * The console output is the player's primary evidence, so it obeys one rule of
 * its own: a stage says what it TRIED and what it GOT. It never says what is
 * wrong, because the machine does not know, and a machine that diagnoses
 * itself is a machine that plays the game for you.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "nom.h"
#include "machine.h"

const char *boot_stage_name(BootStage s)
{
    switch (s) {
    case BOOT_FIRMWARE: return "firmware";
    case BOOT_LOADER:   return "bootloader";
    case BOOT_KERNEL:   return "kernel";
    case BOOT_INITRD:   return "initrd";
    case BOOT_INIT:     return "init";
    case BOOT_SERVICES: return "services";
    case BOOT_TARGET:   return "target";
    default:            return "?";
    }
}

typedef struct {
    Machine *m;
    Buf     *con;
} BootCtx;

static void say(BootCtx *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char line[256];
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    buf_puts(c->con, line);
    buf_putc(c->con, '\n');
}

static bool fail(Machine *m, BootCtx *c, BootStage at, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->boot.reason, sizeof m->boot.reason, fmt, ap);
    va_end(ap);
    m->boot.failed_at = at;
    m->boot.running   = false;
    say(c, "%s", m->boot.reason);
    return false;
}

/* Read a whole file, following symlinks. Distinguishes the three states that
 * matter to a boot: present, absent, and present-but-pointing-at-nothing. */
typedef enum { F_OK, F_MISSING, F_DANGLING, F_NOTFILE } FileState;

static FileState slurp(Machine *m, const char *path, Buf *out, unsigned *mode,
                       char *linktarget, size_t ltsz)
{
    VNode *ln = vfs_lookup(&m->disk, path);
    if (!ln) return F_MISSING;
    if (ln->kind == VN_LINK && linktarget)
        snprintf(linktarget, ltsz, "%s", ln->target);
    bool dangling = false;
    VNode *n = vfs_resolve(&m->disk, path, &dangling);
    if (dangling) return F_DANGLING;
    if (!n) return F_MISSING;
    if (n->kind != VN_FILE) return F_NOTFILE;
    if (mode) *mode = n->mode;
    if (out) buf_put(out, n->data.p, n->data.len);
    return F_OK;
}

/* Pull `key` out of an indented config block. Returns NULL if absent. The
 * parser is deliberately unforgiving about nothing: a config with a typo'd
 * key simply does not have the key, which is how real config breaks. */
static bool cfg_get(const Buf *b, const char *key, char *out, size_t outsz)
{
    size_t klen = strlen(key);
    const char *p = b->p, *end = b->p + b->len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const char *s = p;
        while (len && (*s == ' ' || *s == '\t')) { s++; len--; }
        if (len > klen && strncmp(s, key, klen) == 0 &&
            (s[klen] == ' ' || s[klen] == '\t' || s[klen] == '=')) {
            const char *v = s + klen;
            size_t vl = len - klen;
            while (vl && (*v == ' ' || *v == '\t' || *v == '=')) { v++; vl--; }
            while (vl && (v[vl-1] == ' ' || v[vl-1] == '\r')) vl--;
            if (vl >= outsz) vl = outsz - 1;
            memcpy(out, v, vl);
            out[vl] = '\0';
            return true;
        }
        p = nl ? nl + 1 : NULL;
    }
    return false;
}

/* Does this initrd carry a module by this name? */
static bool initrd_has_module(const Buf *b, const char *name)
{
    const char *p = b->p, *end = b->p + b->len;
    while (p && p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 7 && strncmp(p, "module ", 7) == 0) {
            const char *v = p + 7; size_t vl = len - 7;
            while (vl && (v[vl-1] == ' ' || v[vl-1] == '\r')) vl--;
            if (vl == strlen(name) && strncmp(v, name, vl) == 0) return true;
        }
        p = nl ? nl + 1 : NULL;
    }
    return false;
}

/* --- services ---------------------------------------------------------- */

typedef struct {
    char name[40];
    char after[40];
    char exec[NOM_PATH_MAX];
    bool required;
    bool started;
} Unit;

static int load_units(Machine *m, Unit *u, int max)
{
    Buf names = {0};
    int n = 0;
    if (vfs_list(&m->disk, "/etc/init", &names) == IO_OK) {
        const char *p = names.p, *end = names.p + names.len;
        while (p && p < end && n < max) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
            if (len > 8 && strncmp(p + len - 8, ".service", 8) == 0) {
                char path[NOM_PATH_MAX];
                snprintf(path, sizeof path, "/etc/init/%.*s", (int)len, p);
                Buf body = {0};
                if (slurp(m, path, &body, NULL, NULL, 0) == F_OK) {
                    Unit *x = &u[n];
                    memset(x, 0, sizeof *x);
                    snprintf(x->name, sizeof x->name, "%.*s", (int)(len - 8), p);
                    cfg_get(&body, "after", x->after, sizeof x->after);
                    cfg_get(&body, "exec",  x->exec,  sizeof x->exec);
                    char req[8] = "1";
                    cfg_get(&body, "required", req, sizeof req);
                    x->required = (req[0] == '1');
                    n++;
                }
                buf_free(&body);
            }
            p = nl ? nl + 1 : NULL;
        }
    }
    buf_free(&names);
    return n;
}

/* --- the chain --------------------------------------------------------- */

void machine_boot(Machine *m)
{
    buf_clear(&m->boot.console);
    m->boot.running   = false;
    m->boot.emergency = 0;
    m->boot.reason[0] = '\0';
    m->boot.reached   = BOOT_FIRMWARE;
    m->boot.failed_at = BOOT_FIRMWARE;

    BootCtx cx = { m, &m->boot.console };
    BootCtx *c = &cx;
    Buf f = {0};
    char link[NOM_PATH_MAX];
    unsigned mode = 0;

    /* ---- firmware ---- */
    say(c, "zbios 1.4  node-%s", m->id);
    if (!m->bootsector) {
        fail(m, c, BOOT_FIRMWARE, "no bootable device -- insert boot media");
        goto done;
    }

    /* ---- bootloader ---- */
    m->boot.reached = BOOT_LOADER;
    switch (slurp(m, "/boot/zbl/zbl.cfg", &f, NULL, NULL, 0)) {
    case F_MISSING:
    case F_DANGLING:
        fail(m, c, BOOT_LOADER, "zbl: /boot/zbl/zbl.cfg: not found");
        goto done;
    case F_NOTFILE:
        fail(m, c, BOOT_LOADER, "zbl: /boot/zbl/zbl.cfg: not a file");
        goto done;
    default: break;
    }
    say(c, "zbl 2.06  loading configuration");

    char kpath[NOM_PATH_MAX], ipath[NOM_PATH_MAX], rootspec[64];
    if (!cfg_get(&f, "kernel", kpath, sizeof kpath)) {
        fail(m, c, BOOT_LOADER, "zbl: no kernel line in configuration");
        goto done;
    }
    if (!cfg_get(&f, "initrd", ipath, sizeof ipath)) {
        fail(m, c, BOOT_LOADER, "zbl: no initrd line in configuration");
        goto done;
    }
    if (!cfg_get(&f, "root", rootspec, sizeof rootspec)) {
        fail(m, c, BOOT_LOADER, "zbl: no root line in configuration");
        goto done;
    }

    /* ---- kernel ---- */
    m->boot.reached = BOOT_KERNEL;
    buf_clear(&f);
    link[0] = '\0';
    FileState st = slurp(m, kpath, &f, &mode, link, sizeof link);
    if (st == F_DANGLING) {
        fail(m, c, BOOT_KERNEL, "zbl: %s -> %s: no such file", kpath, link);
        goto done;
    }
    if (st != F_OK) {
        fail(m, c, BOOT_KERNEL, "zbl: %s: not found", kpath);
        goto done;
    }
    if (f.len < 5 || memcmp(f.p, "\x7fKRNL", 5) != 0) {
        fail(m, c, BOOT_KERNEL, "zbl: %s: bad magic -- not a kernel image", kpath);
        goto done;
    }
    say(c, "zbl: loading %s", kpath);

    /* ---- initrd: find and mount the root filesystem ---- */
    m->boot.reached = BOOT_INITRD;
    buf_clear(&f);
    link[0] = '\0';
    st = slurp(m, ipath, &f, &mode, link, sizeof link);
    if (st == F_DANGLING) {
        fail(m, c, BOOT_INITRD, "zbl: %s -> %s: no such file", ipath, link);
        goto done;
    }
    if (st != F_OK) {
        fail(m, c, BOOT_INITRD, "zbl: %s: not found", ipath);
        goto done;
    }
    if (f.len < 7 || memcmp(f.p, "\x7fINITRD", 7) != 0) {
        fail(m, c, BOOT_INITRD, "zbl: %s: bad magic -- not an initrd image", ipath);
        goto done;
    }
    say(c, "kernel 6.4.11 booting");

    /* The initrd must carry the driver for the root device and the filesystem
     * it is formatted with. This is the classic one: regenerate the initrd
     * without a module and the machine cannot reach its own root. */
    if (!initrd_has_module(&f, "virtio_blk")) {
        say(c, "initrd: no driver for the root device");
        m->boot.emergency = 1;
        fail(m, c, BOOT_INITRD,
             "initrd: waiting for %s ... timed out (30s), entering emergency shell",
             rootspec);
        goto done;
    }
    if (!initrd_has_module(&f, "ext4")) {
        say(c, "initrd: no filesystem driver for ext4");
        m->boot.emergency = 1;
        fail(m, c, BOOT_INITRD,
             "initrd: mount %s: unknown filesystem type, entering emergency shell",
             rootspec);
        goto done;
    }

    /* The root the bootloader named has to be the root that exists. */
    const char *want = rootspec;
    if (strncmp(want, "UUID=", 5) == 0) want += 5;
    if (strcmp(want, m->root_uuid) != 0) {
        m->boot.emergency = 1;
        fail(m, c, BOOT_INITRD,
             "initrd: waiting for /dev/disk/by-uuid/%s ... timed out (30s), "
             "entering emergency shell", want);
        goto done;
    }
    say(c, "initrd: mounted %s on /", rootspec);

    /* ---- init ---- */
    m->boot.reached = BOOT_INIT;
    buf_clear(&f);
    link[0] = '\0';
    mode = 0;
    st = slurp(m, "/sbin/init", &f, &mode, link, sizeof link);
    if (st == F_DANGLING) {
        fail(m, c, BOOT_INIT, "kernel: /sbin/init -> %s: no such file -- "
             "kernel panic: no init found", link);
        goto done;
    }
    if (st != F_OK) {
        fail(m, c, BOOT_INIT, "kernel panic: no init found -- /sbin/init: not found");
        goto done;
    }
    if (!(mode & 0111)) {
        fail(m, c, BOOT_INIT, "kernel panic: /sbin/init: permission denied "
             "(mode %04o)", mode);
        goto done;
    }
    say(c, "sysinit 254 running as pid 1");

    /* fstab is read by init, and an entry naming a device that is not there
     * stops a boot dead. Everyone who has typed a UUID wrong knows this one. */
    buf_clear(&f);
    if (slurp(m, "/etc/fstab", &f, NULL, NULL, 0) != F_OK) {
        fail(m, c, BOOT_INIT, "sysinit: /etc/fstab: not found");
        goto done;
    }
    {
        const char *p = f.p, *end = f.p + f.len;
        while (p && p < end) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
            if (len && *p != '#') {
                char dev[64] = {0}, mnt[64] = {0};
                if (sscanf(p, "%63s %63s", dev, mnt) == 2) {
                    if (strncmp(dev, "UUID=", 5) == 0 &&
                        strcmp(dev + 5, m->root_uuid) != 0) {
                        fail(m, c, BOOT_INIT,
                             "sysinit: %s: no device with that uuid -- "
                             "dependency failed for %s", dev, mnt);
                        goto done;
                    }
                }
            }
            p = nl ? nl + 1 : NULL;
        }
    }
    say(c, "sysinit: local filesystems mounted");

    /* ---- services ---- */
    m->boot.reached = BOOT_SERVICES;
    Unit u[UNIT_MAX];
    int nu = load_units(m, u, UNIT_MAX);
    /* Start in dependency order. A unit whose `after` never becomes startable
     * is left unstarted, which is how a cycle or a missing dependency shows
     * up: not as an error about cycles, but as services that never come up. */
    for (int pass = 0; pass < UNIT_MAX; pass++) {
        int progress = 0;
        for (int i = 0; i < nu; i++) {
            if (u[i].started) continue;
            if (u[i].after[0]) {
                bool ready = false;
                for (int j = 0; j < nu; j++)
                    if (strcmp(u[j].name, u[i].after) == 0 && u[j].started) ready = true;
                if (!ready) continue;
            }
            unsigned xm = 0;
            FileState xs = slurp(m, u[i].exec, NULL, &xm, NULL, 0);
            if (xs != F_OK) {
                say(c, "sysinit: %s: %s: not found", u[i].name, u[i].exec);
                if (u[i].required) {
                    fail(m, c, BOOT_SERVICES,
                         "sysinit: failed to start %s -- required, giving up",
                         u[i].name);
                    goto done;
                }
                u[i].started = true;   /* optional: note it and carry on */
                progress++;
                continue;
            }
            if (!(xm & 0111)) {
                say(c, "sysinit: %s: %s: permission denied", u[i].name, u[i].exec);
                if (u[i].required) {
                    fail(m, c, BOOT_SERVICES,
                         "sysinit: failed to start %s -- required, giving up",
                         u[i].name);
                    goto done;
                }
                u[i].started = true;
                progress++;
                continue;
            }
            say(c, "sysinit: started %s", u[i].name);
            u[i].started = true;
            progress++;
        }
        if (!progress) break;
    }
    for (int i = 0; i < nu; i++) {
        if (u[i].started) continue;
        say(c, "sysinit: %s: waiting for %s", u[i].name, u[i].after);
        if (u[i].required) {
            fail(m, c, BOOT_SERVICES,
                 "sysinit: %s never started -- dependency %s did not come up",
                 u[i].name, u[i].after);
            goto done;
        }
    }

    /* ---- up ---- */
    m->boot.reached   = BOOT_TARGET;
    m->boot.failed_at = BOOT_TARGET;
    m->boot.running   = true;
    {
        Buf h = {0};
        char host[64] = "localhost";
        if (slurp(m, "/etc/hostname", &h, NULL, NULL, 0) == F_OK && h.len) {
            size_t n = h.len;
            while (n && (h.p[n-1] == '\n' || h.p[n-1] == '\r')) n--;
            if (n >= sizeof host) n = sizeof host - 1;
            memcpy(host, h.p, n);
            host[n] = '\0';
        }
        buf_free(&h);
        say(c, "");
        say(c, "Nominal Linux 11.4  %s", host);
        say(c, "%s login:", host);
    }

done:
    buf_free(&f);
}
