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

#include "kernel.h"

/* The last non-blank line the machine printed, which is what it was
 * complaining about when it stopped. Returned in a static buffer: this is
 * called once, at the end of a boot. */
static const char *last_line(const Buf *b)
{
    static char out[NOM_ERR_MAX];
    if (!b->len) return NULL;
    size_t end = b->len;
    while (end && (b->p[end-1] == '\n' || b->p[end-1] == '\r')) end--;
    if (!end) return NULL;
    size_t start = end;
    while (start && b->p[start-1] != '\n') start--;
    size_t n = end - start;
    if (n >= sizeof out) n = sizeof out - 1;
    memcpy(out, b->p + start, n);
    out[n] = '\0';
    return out;
}

/* Did the console print this yet? Used only to say WHICH stage a failure
 * happened in, which is an observation about the output, not a flag the
 * runtime carries. */
static bool buf_contains(const Buf *b, const char *needle)
{
    size_t nl = strlen(needle);
    if (b->len < nl) return false;
    for (size_t i = 0; i + nl <= b->len; i++)
        if (memcmp(b->p + i, needle, nl) == 0) return true;
    return false;
}

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

/* Anything read off a damaged disk can be arbitrary bytes, and it gets echoed
 * into console messages. Real consoles show you the mess without becoming
 * unreadable, so: printable ASCII passes, everything else becomes a dot, and
 * the whole thing is clipped. The player still sees that a file is garbage —
 * that is evidence — without the output turning into control codes. */
static const char *clean(const char *src, size_t len, char *out, size_t outsz)
{
    size_t j = 0;
    for (size_t i = 0; i < len && j + 4 < outsz; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '\n' || ch == '\t') out[j++] = ' ';
        else if (ch >= 0x20 && ch < 0x7f) out[j++] = (char)ch;
        else out[j++] = '.';
    }
    if (j + 4 >= outsz && len > j) { out[j++] = '.'; out[j++] = '.'; out[j++] = '.'; }
    out[j] = '\0';
    return out;
}

/* Copy one line out of a raw buffer into a NUL-terminated scratch string.
 * Parsing straight out of a Buf with sscanf reads past the end when the file
 * has no trailing newline, which is exactly what a truncating corruption
 * produces. */
static size_t line_at(const char *p, const char *end, char *out, size_t outsz)
{
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
    size_t n = len < outsz - 1 ? len : outsz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return len;
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

    /* Validate the whole file before using any of it, the way a real loader
     * does. Random damage inside a config should say WHICH LINE it choked on;
     * silently losing a key and failing later is a worse game and a worse
     * bootloader. */
    {
        static const char *DIRECTIVE[] = { "default", "timeout", "entry",
                                           "kernel", "initrd", "root", NULL };
        const char *p = f.p, *end = f.p + f.len;
        int lineno = 0;
        while (p && p < end) {
            char raw[256], scrub[256], word[64] = {0};
            size_t len = line_at(p, end, raw, sizeof raw);
            lineno++;
            const char *s2 = raw;
            while (*s2 == ' ' || *s2 == '\t') s2++;
            if (*s2 && *s2 != '#') {
                sscanf(s2, "%63s", word);
                bool known = false;
                for (int i = 0; DIRECTIVE[i]; i++)
                    if (strcmp(word, DIRECTIVE[i]) == 0) known = true;
                if (!known) {
                    fail(m, c, BOOT_LOADER, "zbl: zbl.cfg:%d: unrecognised directive: %s",
                         lineno, clean(word, strlen(word), scrub, sizeof scrub));
                    goto done;
                }
            }
            p = (p + len < end) ? p + len + 1 : NULL;
        }
    }

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
        char scrub[256];
        fail(m, c, BOOT_KERNEL, "zbl: %s: not found",
             clean(kpath, strlen(kpath), scrub, sizeof scrub));
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
        char scrub[256];
        fail(m, c, BOOT_INITRD, "zbl: %s: not found",
             clean(ipath, strlen(ipath), scrub, sizeof scrub));
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
    char scrubu[256];
    if (strncmp(want, "UUID=", 5) == 0) want += 5;
    if (strcmp(want, m->root_uuid) != 0) {
        m->boot.emergency = 1;
        fail(m, c, BOOT_INITRD,
             "initrd: waiting for /dev/disk/by-uuid/%s ... timed out (30s), "
             "entering emergency shell",
             clean(want, strlen(want), scrubu, sizeof scrubu));
        goto done;
    }
    say(c, "initrd: mounted %s on /", rootspec);

    /* ---- PID 1: from here it is real, executed userland ----
     * The kernel does exactly what a kernel does: it finds /sbin/init and
     * runs it. It does not know what init will do, because init is a program
     * on the disk and the machine's behaviour from here is whatever that
     * program says it is. */
    m->boot.reached = BOOT_INIT;
    {
        char uerr[NOM_ERR_MAX] = "";
        int64_t rc = kernel_spawn(m, "/sbin/init", "", &m->boot.console, 0,
                                  uerr, sizeof uerr);
        if (rc != 0) {
            /* When a guest program fails it says why, on the console, in its
             * own words. That line IS the reason -- synthesising "init exited
             * with status 1" over the top would throw away the only evidence
             * the player has. Only fall back if nothing was said. */
            /* Every level already printed its own reason. The last thing
             * said is the reason the machine is down. */
            const char *last = last_line(&m->boot.console);
            bool from_console = (last != NULL);
            if (last) snprintf(uerr, sizeof uerr, "%s", last);
            else snprintf(uerr, sizeof uerr,
                          "init exited with status %lld -- nothing left to run",
                          (long long)rc);
            /* Which stage the machine died in is a fact about how far the
             * console got, not something the runtime was told. */
            BootStage at = BOOT_INIT;
            if (buf_contains(&m->boot.console, "rc.boot:")) at = BOOT_SERVICES;
            if (buf_contains(&m->boot.console, "rc.3:") ||
                buf_contains(&m->boot.console, "entering runlevel")) at = BOOT_SERVICES;
            /* The reason was taken FROM the console, so echoing it back would
             * print it twice. Record it without re-saying it. */
            if (from_console) {
                snprintf(m->boot.reason, sizeof m->boot.reason, "%s", uerr);
                m->boot.failed_at = at;
                m->boot.running = false;
            } else {
                fail(m, c, at, "%s", uerr[0] ? uerr : "init failed");
            }
            goto done;
        }
    }

    m->boot.reached   = BOOT_TARGET;
    m->boot.failed_at = BOOT_TARGET;
    m->boot.running   = true;

done:
    buf_free(&f);
}

/* Boot the rescue medium. There is no bootloader chain to walk: a live image
 * is loaded by the firmware directly, which is precisely why it still works
 * when the installed system's boot chain does not. */
void machine_boot_rescue(Machine *m)
{
    buf_clear(&m->boot.console);
    m->on_rescue = true;
    m->nmount = 0;                 /* a fresh boot has nothing mounted */
    m->boot.running = false;
    m->boot.reason[0] = '\0';
    m->boot.reached = BOOT_FIRMWARE;

    BootCtx cx = { m, &m->boot.console };
    say(&cx, "zbios 1.4  booting from /dev/sr0 (rescue medium)");

    char uerr[NOM_ERR_MAX] = "";
    int64_t rc = kernel_spawn(m, "/sbin/init", "", &m->boot.console, 0,
                              uerr, sizeof uerr);
    if (rc != 0) {
        /* The rescue medium failing is a bug in NOMINAL, not a ticket: it is
         * never corrupted, so if it will not come up something is wrong with
         * the game rather than with the customer's machine. */
        fail(m, &cx, BOOT_INIT, "rescue medium failed to start: %s",
             uerr[0] ? uerr : "unknown");
        return;
    }
    m->boot.reached = BOOT_TARGET;
    m->boot.failed_at = BOOT_TARGET;
    m->boot.running = true;
}
