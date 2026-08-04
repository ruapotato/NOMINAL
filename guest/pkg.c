/* /usr/bin/pkg — the package database, and the repair verb.
 *
 *   pkg list                 what is installed
 *   pkg owns <path>          which package would I be reinstalling
 *   pkg verify [name]        which files differ from what was shipped
 *   pkg reinstall <name>     put them back, from the repository
 *
 * verify works by hashing each installed file and comparing against the
 * manifest in /var/lib/pkg/<name>/files. The manifest is ON THE DISK, so it
 * can itself be damaged -- and when it is, verify says so rather than
 * reporting a clean system, because a check that cannot fail is worthless.
 */
#include "gsys.h"

static char arg[256], manifest[8192], filebuf[65536], path[256];

/* Operate on a filesystem mounted somewhere else, without chrooting into it.
 * This is not a convenience: when the customer's libc is the wrong version,
 * NOTHING on their disk will run -- so you cannot chroot in and use their
 * tools, and repairing from outside is the only way back. rpm and dpkg both
 * have this for exactly the same reason. */
static char root[128];

static void rooted(const char *p2)
{
    g_copy(path, root, sizeof path);
    g_cat(path, p2, sizeof path);
}

static int read_manifest(const char *pkg)
{
    rooted("/var/lib/pkg/");
    g_cat(path, pkg, sizeof path);
    g_cat(path, "/files", sizeof path);
    return g_slurp(path, manifest, sizeof manifest) >= 0;
}

/* one manifest line: "<mode> <hash> <path>" */
static int split3(char *line, char **a, char **b, char **c)
{
    char *v[GARGS];
    if (g_argv(line, v) < 3) return 0;
    *a = v[0]; *b = v[1]; *c = v[2];
    return 1;
}

static unsigned long parse_hex(const char *s)
{
    unsigned long v = 0;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else break;
        v = v * 16 + d;
    }
    return v;
}

static unsigned parse_oct(const char *s)
{
    unsigned v = 0;
    for (; *s >= '0' && *s <= '7'; s++) v = v * 8 + (unsigned)(*s - '0');
    return v;
}

/* Every finding names the package that owns it, because the next thing the
 * player does is reinstall something and the whole point is reinstalling the
 * RIGHT thing. A verify that only prints paths makes you look the owner up
 * by hand, every time. */
static void finding(const char *pkg, const char *path, const char *what)
{
    g_puts(pkg);
    for (u64 k = g_strlen(pkg); k < 16; k++) g_puts(" ");
    g_puts(path);
    for (u64 k = g_strlen(path); k < 34; k++) g_puts(" ");
    g_putln(what);
}

static int verify_one(const char *pkg, int *bad)
{
    if (!read_manifest(pkg)) {
        g_puts("pkg: "); g_puts(pkg);
        g_putln(": no such package (or its manifest is unreadable)");
        return -1;          /* not a finding: a bad question */
    }
    char *p = manifest;
    while (*p) {
        char *nl = p; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        static char line[300];
        g_copy(line, p, sizeof line);
        *nl = save; p = *nl ? nl + 1 : nl;
        char *t = g_trim(line);
        if (!*t || *t == '#') continue;
        char *mode, *hash, *fp;
        if (!split3(t, &mode, &hash, &fp)) continue;

        /* A symlink is checked by its target, not its contents. stat follows
         * links, so a dangling one fails stat -- which is exactly the report
         * we want, but it has to be attributed to the link itself. */
        static char real[300];
        g_copy(real, root, sizeof real);
        g_cat(real, fp, sizeof real);
        if (g_streq(mode, "link")) {
            static char tgt[256];
            i64 tl = g_readlink(real, tgt, sizeof tgt);
            if (tl < 0)      { finding(pkg, fp, "MISSING (symlink)"); (*bad)++; }
            else if (g_hash(tgt, (u64)tl) != parse_hex(hash))
                             { finding(pkg, fp, "REPOINTED"); (*bad)++; }
            continue;
        }

        NomStat st;
        if (g_stat(real, &st) != 0) {
            finding(pkg, fp, "MISSING"); (*bad)++;
            continue;
        }
        i64 n = g_slurp(real, filebuf, sizeof filebuf);
        if (n < 0) { finding(pkg, fp, "UNREADABLE"); (*bad)++; continue; }
        unsigned long h = g_hash(filebuf, (u64)n);
        if (h != parse_hex(hash)) {
            finding(pkg, fp, "CHANGED"); (*bad)++;
        } else if ((unsigned)st.mode != parse_oct(mode)) {
            static char msg[48];
            g_copy(msg, "MODE is ", sizeof msg);
            finding(pkg, fp, "");
            g_puts("                 mode is "); g_putoct((unsigned)st.mode, 4);
            g_puts(", package shipped "); g_putoct(parse_oct(mode), 4); g_puts("\n");
            (*bad)++;
        }
    }
    return 1;
}

static void each_package(void (*fn)(const char *))
{
    static char name[64], dir[160];
    g_copy(dir, root, sizeof dir);
    g_cat(dir, "/var/lib/pkg", sizeof dir);
    for (int i = 0; i < 128; i++) {
        if (g_readdir(dir, i, name) < 0) break;
        fn(name);
    }
}

static int g_bad;
static void verify_cb(const char *n) { verify_one(n, &g_bad); }

static void list_cb(const char *n)
{
    static char p2[192], ver[256];
    g_copy(p2, root, sizeof p2);
    g_cat(p2, "/var/lib/pkg/", sizeof p2);
    g_cat(p2, n, sizeof p2);
    g_cat(p2, "/version", sizeof p2);
    g_puts(n);
    for (u64 k = g_strlen(n); k < 18; k++) g_puts(" ");
    if (g_slurp(p2, ver, sizeof ver) > 0) g_puts(g_trim(ver));
    g_puts("\n");
}

void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int n = g_argv(arg, v);
    root[0] = 0;
    if (n >= 2 && g_streq(v[0], "--root")) {
        g_copy(root, v[1], sizeof root);
        for (int i = 0; i + 2 < n; i++) v[i] = v[i + 2];
        n -= 2;
    }
    if (n < 1) {
        g_putln("usage: pkg [--root DIR] list|owns|verify|diff|reinstall|upgrade");
        g_putln("  --root repairs a filesystem mounted elsewhere, without");
        g_putln("         chrooting into it -- which you cannot do when the");
        g_putln("         disk's own libc is broken");
        g_exit(1);
    }

    if (g_streq(v[0], "list")) { each_package(list_cb); g_exit(0); }

    if (g_streq(v[0], "owns")) {
        if (n < 2) { g_putln("usage: pkg owns <path>"); g_exit(1); }
        static char name[64];
        for (int i = 0; i < 128; i++) {
            rooted("/var/lib/pkg");
            static char pkgdir[160];
            g_copy(pkgdir, path, sizeof pkgdir);
            if (g_readdir(pkgdir, i, name) < 0) break;
            if (!read_manifest(name)) continue;
            char *p = manifest;
            while (*p) {
                char *nl = p; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                static char line[300];
                g_copy(line, p, sizeof line);
                *nl = save; p = *nl ? nl + 1 : nl;
                char *a, *b, *c;
                char *t = g_trim(line);
                if (!*t || !split3(t, &a, &b, &c)) continue;
                if (g_streq(c, v[1])) { g_putln(name); g_exit(0); }
            }
        }
        /* Asking about a directory is a reasonable question, so answer it:
         * which packages own anything underneath. */
        u64 qlen = g_strlen(v[1]);
        int found = 0;
        for (int i = 0; i < 128; i++) {
            if (g_readdir("/var/lib/pkg", i, name) < 0) break;
            if (!read_manifest(name)) continue;
            char *p = manifest;
            int hit = 0;
            while (*p && !hit) {
                char *nl = p; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                static char line[300];
                g_copy(line, p, sizeof line);
                *nl = save; p = *nl ? nl + 1 : nl;
                char *a, *b, *cc;
                char *t = g_trim(line);
                if (!*t || !split3(t, &a, &b, &cc)) continue;
                u64 m2 = 0;
                while (m2 < qlen && cc[m2] == v[1][m2]) m2++;
                if (m2 == qlen && (cc[m2] == '/' || qlen == 1)) hit = 1;
            }
            if (hit) { g_puts("  "); g_putln(name); found++; }
        }
        if (!found) {
            /* A playtester hit an orphan service four times and never worked
             * out that nothing owning it was the CLUE. Say so. */
            g_putln("no package owns that path");
            g_putln("");
            g_putln("nothing installed this file. If the system is trying to");
            g_putln("use it, either it was dropped there by hand or by an");
            g_putln("installer that is not managed here -- and removing it is");
            g_putln("usually safe. `rm <path>` if you are sure.");
        }
        else { g_puts("(packages owning files under "); g_puts(v[1]); g_putln(")"); }
        g_exit(found ? 0 : 1);
    }

    if (g_streq(v[0], "upgrade")) {
        /* Refetch every file from the repository. What arrives depends on the
         * CHANNEL in /etc/pkg/repos.d, so this is exactly as safe or as
         * dangerous as that configuration is. */
        static char rp[192], nm3[64];
        int files = 0, pkgs = 0;
        rooted("/var/lib/pkg");
        static char pdir[192];
        g_copy(pdir, path, sizeof pdir);
        for (int i = 0; i < 128; i++) {
            if (g_readdir(pdir, i, nm3) < 0) break;
            if (!read_manifest(nm3)) continue;
            pkgs++;
            char *p = manifest;
            while (*p) {
                char *nl = p; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                static char line[300];
                g_copy(line, p, sizeof line);
                *nl = save; p = *nl ? nl + 1 : nl;
                char *mode, *hash, *fp;
                char *t = g_trim(line);
                if (!*t || !split3(t, &mode, &hash, &fp)) continue;
                if (g_streq(mode, "link")) continue;
                i64 got = g_repo(nm3, fp, filebuf);
                if (got < 0) continue;
                g_copy(rp, root, sizeof rp);
                g_cat(rp, fp, sizeof rp);
                int fd = g_open(rp, O_WRONLY | O_CREAT | O_TRUNC);
                if (fd < 0) continue;
                sysc(SYS_write, fd, (i64)filebuf, got);
                g_close(fd);
                sysc(SYS_chmod, (i64)rp, (i64)parse_oct(mode), 0);
                files++;
            }
        }
        g_putn(pkgs); g_puts(" packages, "); g_putn(files);
        g_putln(" files fetched from the configured repository");
        g_putln("(what you get depends on the channel in /etc/pkg/repos.d)");
        g_exit(0);
    }

    if (g_streq(v[0], "diff")) {
        /* Show what a CHANGED file actually says, against what the package
         * shipped. This is the tool that makes local edits fair: a diff that
         * reads like an admin's deliberate change ("# hardened after the
         * audit") is not the same as one that reads like damage, and only a
         * person can tell the difference. */
        if (n < 2) { g_putln("usage: pkg diff <path>"); g_exit(1); }
        static char owner[64];
        owner[0] = 0;
        static char nm2[64];
        for (int i = 0; i < 128 && !owner[0]; i++) {
            if (g_readdir("/var/lib/pkg", i, nm2) < 0) break;
            if (!read_manifest(nm2)) continue;
            char *p = manifest;
            while (*p) {
                char *nl = p; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                static char line[300];
                g_copy(line, p, sizeof line);
                *nl = save; p = *nl ? nl + 1 : nl;
                char *a, *b, *cc;
                char *t = g_trim(line);
                if (!*t || !split3(t, &a, &b, &cc)) continue;
                if (g_streq(cc, v[1])) { g_copy(owner, nm2, sizeof owner); break; }
            }
        }
        if (!owner[0]) { g_putln("pkg: no package owns that path"); g_exit(1); }

        i64 want = g_repo(owner, v[1], filebuf);
        if (want < 0) { g_putln("pkg: cannot fetch the shipped copy"); g_exit(1); }
        static char shipped[65536];
        for (i64 k = 0; k < want; k++) shipped[k] = filebuf[k];
        shipped[want] = 0;

        i64 have = g_slurp(v[1], filebuf, sizeof filebuf);
        if (have < 0) { g_puts("pkg: "); g_puts(v[1]); g_putln(": cannot read what is installed"); g_exit(1); }

        g_puts("--- shipped by ");
        g_puts(owner);
        g_puts(" (");
        g_putn(want);
        g_putln(" bytes)");
        g_write(1, shipped, (u64)want);
        g_puts("+++ installed now (");
        g_putn(have);
        g_putln(" bytes)");
        g_write(1, filebuf, (u64)have);
        g_exit(0);
    }

    if (g_streq(v[0], "verify")) {
        g_bad = 0;
        if (n >= 2) {
            if (verify_one(v[1], &g_bad) < 0) g_exit(1);   /* unknown package */
        } else {
            each_package(verify_cb);
        }
        if (!g_bad) g_putln("all files match their packages");
        else { g_puts("\n"); g_putn(g_bad); g_putln(" file(s) differ. `pkg reinstall <package>` puts them back."); }
        g_exit(g_bad ? 1 : 0);
    }

    if (g_streq(v[0], "reinstall")) {
        if (n < 2) { g_putln("usage: pkg reinstall <name>"); g_exit(1); }
        if (!read_manifest(v[1])) {
            g_puts("pkg: "); g_puts(v[1]); g_putln(": no such package");
            g_exit(1);
        }
        int done = 0, failed = 0;
        char *p = manifest;
        while (*p) {
            char *nl = p; while (*nl && *nl != '\n') nl++;
            char save = *nl; *nl = 0;
            static char line[300];
            g_copy(line, p, sizeof line);
            *nl = save; p = *nl ? nl + 1 : nl;
            char *mode, *hash, *fp;
            char *t = g_trim(line);
            if (!*t || !split3(t, &mode, &hash, &fp)) continue;
            /* Pull the pristine bytes from the repository, which is not on
             * this disk -- that is why this works on a wrecked machine. */
            if (g_streq(mode, "link")) {
                /* the repo restores links through the same call; the host
                 * knows it is a link and recreates it */
                if (g_repo(v[1], fp, filebuf) < 0) {
                    g_puts("  cannot restore link "); g_putln(fp); failed++;
                } else done++;
                continue;
            }
            i64 got = g_repo(v[1], fp, filebuf);
            if (got < 0) { g_puts("  cannot fetch "); g_putln(fp); failed++; continue; }
            static char rp[300];
            g_copy(rp, root, sizeof rp);
            g_cat(rp, fp, sizeof rp);
            int fd = g_open(rp, O_WRONLY | O_CREAT | O_TRUNC);
            if (fd < 0) { g_puts("  cannot write "); g_putln(rp); failed++; continue; }
            sysc(SYS_write, fd, (i64)filebuf, got);
            g_close(fd);
            sysc(SYS_chmod, (i64)rp, (i64)parse_oct(mode), 0);
            done++;
        }
        g_puts(v[1]); g_puts(": "); g_putn(done); g_puts(" files restored");
        if (failed) { g_puts(", "); g_putn(failed); g_puts(" failed"); }
        g_puts("\n");
        g_exit(failed ? 1 : 0);
    }

    g_puts("pkg: unknown command: "); g_putln(v[0]);
    g_exit(1);
}
