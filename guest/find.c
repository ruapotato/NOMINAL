/* /usr/bin/find — walk a tree and print what is in it.
 *
 * It exists because the model kept reaching for it and a playtester kept
 * wanting it. Both were right: "where did that file go" is a question every
 * administrator asks, and answering it with `ls` in a loop is not an answer.
 *
 *   find <dir>                  everything under <dir>
 *   find <dir> -name <pattern>  only names matching (* and ? work)
 *   find <dir> -type f|d        only files, or only directories
 *
 * Depth first, so the output reads like a tree rather than a queue.
 */
#include "gsys.h"

static char arg[512];
static const char *pat = 0;
static int want_kind = 0;          /* 0 any, 1 file, 2 dir */
static int hits = 0;

static int match(const char *p, const char *nm)
{
    while (*p && *nm) {
        if (*p == '*') {
            p++;
            if (!*p) return 1;
            for (const char *q = nm; *q; q++) if (match(p, q)) return 1;
            return 0;
        }
        if (*p != '?' && *p != *nm) return 0;
        p++; nm++;
    }
    while (*p == '*') p++;
    return !*p && !*nm;
}

static void walk(const char *dir, int depth)
{
    if (depth > 12 || hits > 4000) return;
    static char nm[160];
    for (int i = 0; i < 4096; i++) {
        if (g_readdir(dir, i, nm) < 0) break;
        static char child[320];
        g_copy(child, dir, sizeof child);
        if (!g_streq(dir, "/")) g_cat(child, "/", sizeof child);
        g_cat(child, nm, sizeof child);

        NomStat st;
        int isdir = (g_stat(child, &st) == 0 && st.kind == NOM_KIND_DIR);
        int kind_ok = (want_kind == 0) || (want_kind == 1 && !isdir)
                                       || (want_kind == 2 && isdir);
        if (kind_ok && (!pat || match(pat, nm))) {
            g_putln(child);
            hits++;
        }
        if (isdir) walk(child, depth + 1);
    }
}

void _start(void)
{
    g_getarg(arg, sizeof arg);
    char *v[GARGS];
    int n = g_argv(arg, v);
    if (n < 1) { g_putln("usage: find <dir> [-name <pattern>] [-type f|d]"); g_exit(1); }

    const char *root = v[0];
    for (int i = 1; i + 1 < n; i++) {
        if (g_streq(v[i], "-name")) pat = v[++i];
        else if (g_streq(v[i], "-type")) {
            i++;
            want_kind = v[i][0] == 'd' ? 2 : 1;
        }
    }
    NomStat st;
    if (g_stat(root, &st) != 0) {
        g_puts("find: "); g_puts(root); g_putln(": no such directory");
        g_exit(1);
    }
    if (st.kind != NOM_KIND_DIR) { g_putln(root); g_exit(0); }
    walk(root, 0);
    if (!hits) g_putln("(nothing matched)");
    g_exit(0);
}
