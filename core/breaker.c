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
            return true;
        }
        /* It still boots. That is not a ticket — try again. */
    }
    return false;
}
