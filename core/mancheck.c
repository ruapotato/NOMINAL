/* mancheck.c — the gate on the founding rule, run as `bf --mancheck`.
 *
 * David's rule, standing since before the pivot: every technical claim in
 * in-game text must be TRUE of this machine, verified by running it. Man
 * pages are where that rule is easiest to break, because a page describing
 * the Linux tool of the same name reads perfectly and sends a player hunting
 * for a flag that does not exist here.
 *
 * It has nearly been broken twice in one day. A netstat(8) draft listed -a,
 * -n and -p, none of which this netstat has. A dmesg(8) draft quoted a
 * respawn message the kernel does not print. Both were caught by hand, which
 * is not a mechanism.
 *
 * SO THIS RUNS THE PAGES. Every line of every manual that looks like a
 * command example is executed on a real booted machine, and the answer has
 * to be something other than "I do not know what that is". It is deliberately
 * narrow: it proves the command EXISTS and ACCEPTS the invocation the page
 * shows. It does not check that the output means what the prose says -- no
 * gate can -- but "the page names a flag the program refuses" is most of what
 * actually goes wrong, and it is now impossible to ship.
 *
 * WHAT IT WILL NOT RUN. Only read-only diagnostics, listed in SAFE below. A
 * page showing `rm -rf /` is not an invitation, and a gate that reinstalls
 * packages to check a man page is a gate nobody will keep.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "nom.h"
#include "machine.h"
#include "kernel.h"
#include "site.h"

static int passed, total;

static void ck(const char *what, bool ok, const char *detail)
{
    total++;
    if (ok) passed++;
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok && detail && *detail) printf("      %s\n", detail);
}

/* The programs a gate may run. Read-only, no argument of theirs can change
 * the machine, and every one of them is something a player runs while
 * diagnosing rather than while repairing. */
static const char *SAFE[] = {
    "ls", "cat", "grep", "head", "tail", "wc", "stat", "find", "du", "df",
    "blkid", "dmesg", "netstat", "ip", "ss", "arp", "svc", "ps", "ns",
    "uname", "whoami", "ldd", "man", "pwd", "echo", "seq", "rev", "sort",
    /* `voice` reads this machine's own call counters and changes nothing, the
     * same as ss and tcpdump above. Without it here its manual's examples were
     * existence-checked and never run, which is the weaker half of this gate. */
    "voice",
    "fortune", "cowsay", "traceroute", "tcpdump", "ping", NULL
};

static bool safe_cmd(const char *word)
{
    for (int i = 0; SAFE[i]; i++)
        if (strcmp(word, SAFE[i]) == 0) return true;
    /* `pkg` is safe in its reading verbs and emphatically not in its writing
     * ones, so it is decided on the second word rather than the first. */
    return false;
}

static bool safe_pkg(const char *line)
{
    const char *v = line + 3;
    while (*v == ' ') v++;
    return strncmp(v, "list", 4) == 0 || strncmp(v, "verify", 6) == 0 ||
           strncmp(v, "owns", 4) == 0 || strncmp(v, "diff", 4) == 0;
}

/* DOES THIS MACHINE HAVE THAT PROGRAM AT ALL?
 *
 * The safe-list above decides what may be EXECUTED, and on its own it left
 * the worst hole in this gate wide open: a page naming `iptables`, `vi`,
 * `systemctl` or any other tool from a different operating system was
 * skipped as "not read-only" and passed in silence -- when a command that
 * does not exist is the single most likely thing to be wrong in a page
 * somebody wrote from memory. So existence is checked for every example,
 * whether or not it is safe to run, and only the running is gated by the
 * list. */
static const char *BINDIRS[] = { "/bin", "/usr/bin", "/sbin", "/usr/sbin", NULL };

/* Things sh does itself, which are on no disk and are still real. */
static const char *BUILTINS[] = {
    "cd", "pwd", "bind", "unbind", "echo", "help", "for", "export", "exit",
    "set", "unset", "read", "test", NULL
};

static bool program_exists(Machine *m, const char *word)
{
    for (int i = 0; BUILTINS[i]; i++)
        if (strcmp(word, BUILTINS[i]) == 0) return true;
    if (word[0] == '/' || word[0] == '.')
        return vfs_lookup(&m->disk, word) != NULL;
    for (int i = 0; BINDIRS[i]; i++) {
        char p[160];
        snprintf(p, sizeof p, "%s/%s", BINDIRS[i], word);
        if (vfs_lookup(&m->disk, p)) return true;
    }
    return false;
}

/* The answers that mean the machine did not understand the page. Anything
 * else -- an error about a missing file, an empty table, a refusal on
 * grounds of state -- is the program working and is not this gate's
 * business. */
static const char *DUNNO[] = {
    "command not found", "not found\n", "no such option", "no such command",
    "unrecognised", "unrecognized", NULL
};

static const char *misunderstood(const char *out)
{
    for (int i = 0; DUNNO[i]; i++)
        if (strstr(out, DUNNO[i])) return DUNNO[i];
    return NULL;
}

/* A line of a manual that is a command example.
 *
 * The pages in this image put examples in one of two shapes: indented two
 * spaces in a synopsis block, where the command is followed by two or more
 * spaces and then its description; or indented two spaces on its own as a
 * worked example. Both start at column 2 with a lowercase letter, so that is
 * the test, and the command is the text up to a run of two spaces.
 */
static bool example_of(const char *line, char *out, size_t cap)
{
    if (line[0] != ' ' || line[1] != ' ') return false;
    if (line[2] == ' ' || !islower((unsigned char)line[2])) return false;
    const char *p = line + 2;
    const char *gap = strstr(p, "  ");
    size_t n = gap ? (size_t)(gap - p) : strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\r')) n--;
    if (!n || n >= cap) return false;
    /* Prose that happens to be indented is not an example. A command has no
     * sentence punctuation in it and is not a run of ordinary words. */
    for (size_t i = 0; i < n; i++)
        if (p[i] == '.' && (i + 1 == n || p[i + 1] == ' ')) return false;
    memcpy(out, p, n);
    out[n] = 0;
    /* SQUARE BRACKETS ARE OPTIONAL ARGUMENTS, and what is left when you drop
     * them is a command somebody can really type: `ip addr [show]` is `ip
     * addr`, `du [-s] [-h] [dir ...]` is `du`. Dropping them rather than
     * rejecting the line is worth doing -- it is what makes ip, du and ns
     * checkable at all, and `ip addr` is exactly the sort of claim that
     * silently stops being true. */
    {
        char tmp[200];
        size_t w = 0;
        int depth = 0;
        for (size_t i = 0; out[i] && w < sizeof tmp - 1; i++) {
            if (out[i] == '[') { depth++; continue; }
            if (out[i] == ']') { if (depth) depth--; continue; }
            if (!depth) tmp[w++] = out[i];
        }
        tmp[w] = 0;
        /* Collapse the space the brackets left behind, and the trailing one. */
        char *s = tmp;
        while (*s == ' ') s++;
        size_t k = 0;
        for (size_t i = 0; s[i]; i++) {
            if (s[i] == ' ' && k && tmp[k - 1] == ' ') continue;
            tmp[k++] = s[i];
        }
        while (k && tmp[k - 1] == ' ') k--;
        tmp[k] = 0;
        if (!k) return false;
        memcpy(out, tmp, k + 1);
    }
    /* Braces and angles are REQUIRED placeholders -- `fsck <device>`,
     * `mkdir <dir>` -- and there is nothing honest to substitute for them. */
    if (strpbrk(out, "{}<>")) return false;
    /* PROSE IN A CODE BLOCK IS STILL PROSE. bofh.nomnix.org/haiku sets its
     * verse in <pre>, and the line "ls shows you a healthy link / pointing at
     * nothing" begins with a real program, so it came through as a command
     * and failed the gate on a poem. A shell example of more than four words
     * that contains no flag and no path is a sentence; a command that long is
     * quoting or redirecting, and both of those are already skipped as not
     * read-only. */
    {
        int words = 1;
        bool flagish = false;
        for (size_t i = 0; out[i]; i++) {
            if (out[i] == ' ') words++;
            if (out[i] == '/' || (out[i] == '-' && i && out[i - 1] == ' '))
                flagish = true;
        }
        if (words > 4 && !flagish) return false;
    }
    /* A program left with no arguments that would read standard input will
     * sit there until the instruction budget kills it, which costs time and
     * proves nothing. */
    {
        static const char *STDIN_HUNGRY[] = {
            "cat", "tail", "head", "wc", "grep", "rev", "sort", "rot13",
            "cowsay", "sed", NULL
        };
        if (!strchr(out, ' '))
            for (int i = 0; STDIN_HUNGRY[i]; i++)
                if (strcmp(out, STDIN_HUNGRY[i]) == 0) return false;
    }
    /* A bar is ambiguous: `ip addr | link | route` is alternation, and
     * `cat /etc/hostname | rev | rev` is a pipeline somebody is meant to
     * type -- and the pipeline examples are the good ones, because they are
     * the pages claiming this shell really carries bytes. A pipeline names a
     * file; alternation is bare words. So a bar is allowed through only when
     * the line also mentions a path. */
    if (strchr(out, '|') && !strchr(out, '/')) return false;
    return true;
}

/* Run every example in one file of text and say how it went. Shared by the
 * manuals and by the package documentation, because they make the same kind
 * of claim and there is no reason for a README to be held to a lower
 * standard than a man page. */
typedef struct {
    int  tried, bad, skipped;
    char firstbad[220];
} Scan;

static Scan scan_text(Machine *m, const char *body)
{
    Scan r = {0, 0, 0, {0}};
    const char *p = body;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        char line[220], ex[200];
        if (l < sizeof line) {
            memcpy(line, p, l);
            line[l] = 0;
            if (example_of(line, ex, sizeof ex)) {
                char word[32];
                size_t w = 0;
                while (ex[w] && ex[w] != ' ' && w < sizeof word - 1) {
                    word[w] = ex[w]; w++;
                }
                word[w] = 0;
                /* Existence, for anything that is UNMISTAKABLY an invocation.
                 *
                 * The first draft failed twenty-two pages, and every one was
                 * this extractor mistaking something else for a command:
                 * config syntax (`policy drop`, `channel = stable`,
                 * `default N`), console output being quoted back (`zbl:
                 * loading /boot/vmnomuz`), ed's own one-letter commands
                 * (`p`), and prose (`the WRONG mac`). None of those is a
                 * shell line and none should be judged as one.
                 *
                 * So existence is only asserted where the line carries a flag
                 * or a path -- `iptables -L`, `vi /etc/hosts` -- which is
                 * what an invocation of a tool from somebody else's operating
                 * system looks like, and is the case worth catching. A first
                 * token containing a colon or a line containing an equals is
                 * output or configuration, never a command.
                 *
                 * The limitation is real and worth stating: `systemctl
                 * status foo` would slip through, because it has neither. */
                bool looks_invoked = !strchr(word, ':') && !strchr(ex, '=');
                if (looks_invoked) {
                    /* Token by token, because a lone slash is prose: the
                     * nomsh README says "and / or", and a slash with spaces
                     * round it is a conjunction, not a path. */
                    bool has_flag_or_path = false;
                    const char *tk = ex;
                    while (*tk) {
                        while (*tk == ' ') tk++;
                        const char *te = tk;
                        while (*te && *te != ' ') te++;
                        size_t tl = (size_t)(te - tk);
                        if (tl > 1 && tk[0] == '-') has_flag_or_path = true;
                        if (tl > 1 && memchr(tk, '/', tl)) has_flag_or_path = true;
                        tk = te;
                    }
                    looks_invoked = has_flag_or_path;
                }
                if (looks_invoked && !program_exists(m, word)) {
                    r.tried++;
                    r.bad++;
                    if (!r.firstbad[0])
                        snprintf(r.firstbad, sizeof r.firstbad,
                                 "`%.150s` -> this machine has no %.30s",
                                 ex, word);
                    p = e ? e + 1 : p + l;
                    continue;
                }
                bool ok_to_run = safe_cmd(word) ||
                    (strcmp(word, "pkg") == 0 && safe_pkg(ex));
                if (ok_to_run) {
                    Buf out = {0};
                    kernel_run(m, ex, &out);
                    const char *why = out.p ? misunderstood(out.p) : NULL;
                    r.tried++;
                    if (why) {
                        r.bad++;
                        if (!r.firstbad[0])
                            snprintf(r.firstbad, sizeof r.firstbad,
                                     "`%.150s` -> %.40s", ex, why);
                    }
                    buf_free(&out);
                } else {
                    r.skipped++;
                }
            }
        }
        p = e ? e + 1 : p + l;
    }
    return r;
}

/* THE PACKAGE DOCUMENTATION, held to the same standard as the manuals.
 * pkg(1) tells the player to start with `ls /usr/share/doc` and every
 * package with anything to say ships a README, a CHANGELOG and a
 * known-issues there. Those name commands too, and nothing was checking
 * them. */
static void check_docs(Machine *m, int *ran, int *skipped)
{
    printf("\nand every command the package documentation shows\n");
    Buf dirs = {0};
    if (vfs_list(&m->disk, "/usr/share/doc", &dirs) != IO_OK || !dirs.p) {
        ck("the documentation is on the disk", false, "/usr/share/doc did not list");
        buf_free(&dirs);
        return;
    }
    static const char *FILES[] = { "README", "CHANGELOG", "known-issues", NULL };
    const char *d = dirs.p;
    while (*d) {
        const char *nl = strchr(d, '\n');
        size_t len = nl ? (size_t)(nl - d) : strlen(d);
        char name[64];
        if (len && len < sizeof name) {
            memcpy(name, d, len);
            name[len] = 0;
            /* vfs_list marks directories with a trailing slash. */
            if (len && name[len - 1] == '/') name[len - 1] = 0;
            for (int i = 0; FILES[i]; i++) {
                char path[160];
                snprintf(path, sizeof path, "/usr/share/doc/%s/%s", name, FILES[i]);
                Buf body = {0};
                if (vfs_read(&m->disk, path, &body) == IO_OK && body.p) {
                    /* THE MACHINE HAS TO BE ABLE TO READ IT TOO. This gate
                     * reads a doc through the host's vfs, and a file whose
                     * parent directory was never created is readable that way
                     * and not from a shell: `ls` lists it, `stat` prints its
                     * mode and size, and cat, head, man and pkg diff all
                     * answer "cannot read". Both tools you would reach for to
                     * find out whether it is there say it is. Shipped one
                     * this way an hour ago and only noticed by opening it. */
                    Buf o = {0};
                    char cmd[200];
                    snprintf(cmd, sizeof cmd, "head %.150s", path);
                    kernel_run(m, cmd, &o);
                    if (o.p && strstr(o.p, "cannot read")) {
                        char why[220];
                        snprintf(why, sizeof why, "the machine cannot read it -- "
                                 "is %.60s in the DIRS list in core/image.c?", name);
                        ck(path, false, why);
                        buf_free(&o);
                        buf_free(&body);
                        continue;
                    }
                    buf_free(&o);
                    Scan r = scan_text(m, body.p);
                    *ran += r.tried; *skipped += r.skipped;
                    if (r.tried) {
                        char what[128];
                        snprintf(what, sizeof what, "%.30s/%.20s (%d run)",
                                 name, FILES[i], r.tried);
                        ck(what, r.bad == 0, r.firstbad);
                    }
                }
                buf_free(&body);
            }
        }
        d = nl ? nl + 1 : d + len;
    }
    buf_free(&dirs);
}

/* THE IN-GAME INTERNET, held to the same standard as everything else.
 *
 * core/net_sites.c says of itself, at the top of the wiki: "Every word of
 * this is true of the machine." That was kept by hand, and by the time
 * anybody checked, a page still said "nomsh 1.11" after the shell had gone
 * to 1.12. A player reads those pages with `links wiki.nomnix.org` from
 * inside a machine they are trying to repair, which makes a wrong command
 * there more expensive than a wrong command in a man page, not less.
 *
 * The examples live in <pre> blocks at column zero rather than indented two
 * spaces, so each line is re-indented into the shape example_of() already
 * understands and the same filters apply -- which is why `ip addr | link |
 * route` and `ping &lt;host&gt;` are skipped here for exactly the reasons
 * they are skipped in a manual. */
extern int net_site_page(int i, const char **host, const char **ip,
                         const char **path);
extern bool net_fetch(const char *ip, const char *path, Buf *out);

/* &lt; and friends, so that a placeholder in a page reads as a placeholder
 * to the filter rather than as a literal word. */
static void unescape(const char *in, size_t len, Buf *out)
{
    for (size_t i = 0; i < len; ) {
        if (in[i] == '&') {
            if (!strncmp(in + i, "&lt;", 4))       { buf_putc(out, '<'); i += 4; continue; }
            if (!strncmp(in + i, "&gt;", 4))       { buf_putc(out, '>'); i += 4; continue; }
            if (!strncmp(in + i, "&amp;", 5))      { buf_putc(out, '&'); i += 5; continue; }
            if (!strncmp(in + i, "&quot;", 6))     { buf_putc(out, '"'); i += 6; continue; }
        }
        buf_putc(out, in[i++]);
    }
}

static void check_web(Machine *m, int *ran, int *skipped)
{
    printf("\nand every command the in-game internet shows\n");
    const char *host, *ip, *path;
    for (int i = 0; net_site_page(i, &host, &ip, &path); i++) {
        Buf body = {0};
        if (!net_fetch(ip, path, &body)) { buf_free(&body); continue; }
        if (!body.p) { buf_free(&body); continue; }

        /* Every <pre> block on the page, re-indented two spaces a line. */
        Buf code = {0};
        const char *p = body.p;
        while ((p = strstr(p, "<pre>")) != NULL) {
            p += 5;
            const char *end = strstr(p, "</pre>");
            if (!end) break;
            const char *ls = p;
            while (ls < end) {
                const char *le = memchr(ls, '\n', (size_t)(end - ls));
                size_t l = le ? (size_t)(le - ls) : (size_t)(end - ls);
                buf_puts(&code, "  ");
                unescape(ls, l, &code);
                buf_putc(&code, '\n');
                if (!le) break;
                ls = le + 1;
            }
            p = end + 6;
        }
        if (code.p && code.len) {
            Scan r = scan_text(m, code.p);
            *ran += r.tried; *skipped += r.skipped;
            if (r.tried) {
                char what[160];
                snprintf(what, sizeof what, "%.40s%.30s (%d run)",
                         host, path, r.tried);
                ck(what, r.bad == 0, r.firstbad);
            }
        }
        buf_free(&code);
        buf_free(&body);
    }
}

/* ------------------------------------------------- the shop and the catalogue
 *
 * A PRICE LIST ON A WEB PAGE IS THE CATALOGUE WRITTEN DOWN TWICE, and this
 * project has shipped one fact from two places five times in a day. The
 * supplier's page at halbert.co.uk/catalogue is generated from KIT[] in
 * core/site.c so that it cannot drift, and this is the gate that proves the
 * generation is really doing that: every number on the page is READ BACK OFF
 * THE PAGE and compared with what site_kind_*() answers, product by product.
 *
 * It is deliberately a parser and not a second copy of the generator. Asking
 * the generator to build the expected line and comparing it with itself
 * would pass whatever either of them did.
 *
 * What it catches, and what nothing else would: a product added to the
 * catalogue that the shop does not sell, a price that has been retuned in
 * site.c, a port count or a port speed that has moved, and a page that has
 * gone back to being typed by hand. */
extern const char *site_kind_name(int kind);
extern int   site_kind_by_name(const char *name);
extern int   site_kind_ports(int kind);
extern int   site_kind_price(int kind);
extern int   site_kind_port_mb(int kind, int port);

/* The `n`th whole number on a line, or -1. */
static long nth_num(const char *line, int n)
{
    const char *p = line;
    for (int i = 0; ; ) {
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return -1;
        long v = 0;
        while (isdigit((unsigned char)*p)) v = v * 10 + (*p++ - '0');
        if (i++ == n) return v;
    }
}

/* The row of the catalogue table for that product, into `out` -- WITHOUT the
 * product's own name, because `switch8` and `switch24` have digits in them
 * and the numbers this gate reads are the ones the page printed, not the
 * ones in the spelling. The rows are indented two spaces. */
static bool kit_row(const char *page, const char *name, char *out, size_t cap)
{
    char want[64];
    snprintf(want, sizeof want, "\n  %s ", name);
    const char *at = strstr(page, want);
    if (!at) return false;
    at += strlen(want);
    const char *nl = strchr(at, '\n');
    size_t l = nl ? (size_t)(nl - at) : strlen(at);
    if (l >= cap) l = cap - 1;
    memcpy(out, at, l);
    out[l] = 0;
    return true;
}

static void check_shop(void)
{
    printf("\nand the supplier's catalogue against the catalogue itself\n");
    Buf page = {0};
    if (!net_fetch("10.0.2.73", "/catalogue", &page) || !page.p) {
        ck("halbert.co.uk/catalogue is on the network", false,
           "net_fetch returned nothing for 10.0.2.73/catalogue");
        buf_free(&page);
        return;
    }
    ck("halbert.co.uk/catalogue is on the network", true, NULL);

    int sold = 0;
    for (int k = 0; k < SDEV_KIND_COUNT; k++) {
        const char *name = site_kind_name(k);
        int price = site_kind_price(k);
        char row[200], what[160], why[320];
        bool listed = kit_row(page.p, name, row, sizeof row);

        /* Not for sale, and the shop must not be selling it. The handoff and
         * the tenant's own desk cost nothing and are nobody's to buy. */
        if (price <= 0) {
            snprintf(what, sizeof what, "%s is not for sale and is not on the page", name);
            snprintf(why, sizeof why, "the shop lists `%s`, which costs nothing", name);
            ck(what, !listed, why);
            continue;
        }
        sold++;
        snprintf(what, sizeof what, "%s is in the shop at all", name);
        snprintf(why, sizeof why, "the catalogue sells `%s` and the page has no "
                 "row for it -- the page is not being generated", name);
        ck(what, listed, why);
        if (!listed) continue;

        long ports = nth_num(row, 0), mb = nth_num(row, 1), shown = nth_num(row, 2);
        snprintf(what, sizeof what, "%s: %d sockets, %d Mb, %d", name,
                 site_kind_ports(k), site_kind_port_mb(k, 0), price);
        snprintf(why, sizeof why, "the page row says `%s`", row);
        ck(what, ports == site_kind_ports(k) &&
                 mb    == site_kind_port_mb(k, 0) &&
                 shown == price, why);

        /* And the odd ports, where a box has any: the number has to be on
         * the row, because it is the whole reason to buy that box. */
        int top = site_kind_port_mb(k, site_kind_ports(k) - 1);
        if (top != site_kind_port_mb(k, 0)) {
            char fast[32];
            snprintf(fast, sizeof fast, "%d Mb", top);
            snprintf(what, sizeof what, "%s: its fast ports say %s", name, fast);
            ck(what, strstr(row, fast) != NULL, why);
        }
    }
    ck("the shop sells something", sold > 0, "nothing in the catalogue is priced");

    /* AND NOTHING IT SELLS IS INVENTED. Every order link on the page has to
     * name a kind the building will actually accept, or the shop is selling
     * something that cannot be delivered. */
    int links = 0, bad = 0;
    char firstbad[120] = {0};
    for (const char *p = page.p; (p = strstr(p, "\"order:")) != NULL; ) {
        p += 7;
        char kind[40];
        size_t w = 0;
        while (p[w] && p[w] != '"' && w < sizeof kind - 1) { kind[w] = p[w]; w++; }
        kind[w] = 0;
        links++;
        if (site_kind_by_name(kind) < 0) {
            bad++;
            if (!firstbad[0])
                snprintf(firstbad, sizeof firstbad,
                         "`order %s` -- there is no such kit", kind);
        }
    }
    ck("every order link names a real kind", links == sold && bad == 0,
       firstbad[0] ? firstbad :
       "the page offers a different number of order links than it has products");

    buf_free(&page);

    /* THE DISCONTINUED PAGE IS ONLY HONEST WHILE IT IS TRUE. It lists things
     * the building does not sell; the day one of those names is added to the
     * catalogue the page has to stop claiming it. It filters itself, and
     * this is the proof. */
    Buf gone = {0};
    if (!net_fetch("10.0.2.73", "/discontinued", &gone) || !gone.p) {
        ck("halbert.co.uk/discontinued is on the network", false, "net_fetch returned nothing");
        buf_free(&gone);
        return;
    }
    int named = 0, alive = 0;
    char firstalive[120] = {0};
    for (const char *p = gone.p; (p = strstr(p, "<li><b>")) != NULL; ) {
        p += 7;
        char kind[40];
        size_t w = 0;
        while (p[w] && p[w] != '<' && w < sizeof kind - 1) { kind[w] = p[w]; w++; }
        kind[w] = 0;
        named++;
        if (site_kind_by_name(kind) >= 0) {
            alive++;
            if (!firstalive[0])
                snprintf(firstalive, sizeof firstalive,
                         "the page says `%s` cannot be had, and the catalogue sells it",
                         kind);
        }
    }
    ck("nothing on the discontinued page can be ordered", alive == 0 && named > 0,
       firstalive[0] ? firstalive : "the page named nothing at all");
    buf_free(&gone);
}

/* ------------------------------------------------- the tower's own help */
/* THE PAGE IS A SECOND LIST OF VERBS, AND A SECOND LIST OF ANYTHING IS THE
 * defect this project keeps re-finding: one fact with two answers. The
 * tower's commands live in VERB[] in core/site.c, and `help` is a page
 * somebody wrote by hand beside it. Nothing held the two together.
 *
 * WHAT THAT COST, measured rather than imagined. `mains`, `outlet` and
 * `outlets` were verbs the site answered to and no help text named. A player
 * who bought a machine and typed `power box on` was refused, and the verb
 * that unblocked them was in neither the refusal nor the page -- three
 * places to look and the answer in none of them. It went unnoticed because
 * nothing counted.
 *
 * SO THIS COUNTS, IN BOTH DIRECTIONS.
 *
 *   1. The page must not name a command the tower does not answer to. This
 *      is a hard failure and always has been true; it is asserted so it
 *      stays true. A page that documents a verb that was renamed sends a
 *      player to type something that gets "no such command".
 *
 *   2. Every verb the tower answers to must be named on the page. This is a
 *      hard failure too, with an explicit list of exceptions below -- each
 *      one written down with its reason, so the gap is a thing somebody
 *      chose rather than a thing nobody noticed. A NEW verb that nobody
 *      documented fails this gate on the day it is added.
 *
 * WHAT COUNTS AS NAMED, and it is deliberately stricter than "the word
 * appears somewhere". At the time of writing, `mains` DID appear in the page
 * -- inside the prose of the `ups` entry, "it rides a mains failure out" --
 * so a word-search would have called the bug documented. A verb is named
 * only where a verb is presented: first word of a line, or first word of a
 * `|`-separated column in one of the summary rows. That is exactly the
 * shape the page uses for everything it really documents. */
/* The page is the same page in every building -- it is written down rather
 * than generated -- so the seed only has to be one that makes a tower with
 * somewhere for the handoff to land. It is the one the rest of the project
 * measures on, so a failure here can be reproduced with `bf --sitesh 7008`
 * and the same `help`. */
#define MANCHECK_SEED 7008ull

static bool named_in_help(const char *help, const char *verb)
{
    size_t vl = strlen(verb);
    const char *line = help;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        /* Each `|`-separated column of the line is somewhere a verb may be
         * presented: `status | service | load` documents three. */
        const char *seg = line;
        const char *end = line + len;
        while (seg < end) {
            const char *bar = memchr(seg, '|', (size_t)(end - seg));
            const char *stop = bar ? bar : end;
            while (seg < stop && *seg == ' ') seg++;
            if ((size_t)(stop - seg) >= vl && strncmp(seg, verb, vl) == 0 &&
                (seg + vl == stop || seg[vl] == ' ' || seg[vl] == '\t'))
                return true;
            seg = bar ? bar + 1 : stop;
        }
        line = nl ? nl + 1 : line + len;
    }
    return false;
}

/* THE VERBS THE PAGE DELIBERATELY DOES NOT LIST. Every entry is a decision
 * with a reason, and the list is short on purpose: it is the only way a verb
 * may be missing from `help` without failing this gate, so adding to it is
 * an act somebody has to justify in a diff. */
static const struct { const char *verb; const char *why; } HELP_SILENT[] = {
    { "help",   "the page itself; a page that documented itself would be the "
                "only entry a player did not need" },
    { "credit", "a lever for a gate to pull, not a move a player has -- money "
                "the tower did not earn is not in the game" },
    { "buy",    "an alias of `order`, and the page documents it under that "
                "name. Two entries would be two names for one fact" },
    { NULL, NULL }
};

static const char *silent_why(const char *verb)
{
    for (int i = 0; HELP_SILENT[i].verb; i++)
        if (strcmp(HELP_SILENT[i].verb, verb) == 0) return HELP_SILENT[i].why;
    return NULL;
}

static void check_help(void)
{
    printf("\nand the tower's own `help` against the verbs the tower answers to\n");
    Building b;
    if (!bld_generate(&b, MANCHECK_SEED)) {
        ck("a building to ask for help in", false, "bld_generate refused the seed");
        return;
    }
    Site s;
    if (!site_new(&s, &b, MANCHECK_SEED, 60000)) {
        ck("a building to ask for help in", false, "site_new found nowhere for the uplink");
        bld_free(&b);
        return;
    }
    Buf help = {0};
    site_cmd(&s, "help", &help);
    ck("`help` prints a page at all", help.p && help.len > 200,
       "site_cmd answered `help` with nothing worth reading");

    if (help.p) {
        /* 1. NOTHING THE PAGE NAMES IS INVENTED. Every first word of every
         * line has to be a verb, an indented continuation, or blank. */
        int invented = 0;
        char firstbad[160] = {0};
        const char *line = help.p;
        while (*line) {
            const char *nl = strchr(line, '\n');
            size_t len = nl ? (size_t)(nl - line) : strlen(line);
            if (len && islower((unsigned char)line[0])) {
                char word[40];
                size_t w = 0;
                while (w < len && w < sizeof word - 1 &&
                       islower((unsigned char)line[w])) { word[w] = line[w]; w++; }
                word[w] = 0;
                bool real = false;
                for (int i = 0; i < site_verb_count(); i++)
                    if (strcmp(site_verb_name(i), word) == 0) { real = true; break; }
                if (!real) {
                    invented++;
                    if (!firstbad[0])
                        snprintf(firstbad, sizeof firstbad,
                                 "`help` documents `%s` and the tower has no such "
                                 "verb -- a player who types it gets \"no such command\"",
                                 word);
                }
            }
            line = nl ? nl + 1 : line + len;
        }
        ck("`help` names no command the tower does not answer to",
           invented == 0, firstbad);

        /* 2. AND IT LEAVES NOTHING OUT. */
        int missing = 0;
        char firstmissing[240] = {0};
        for (int i = 0; i < site_verb_count(); i++) {
            const char *v = site_verb_name(i);
            if (named_in_help(help.p, v)) continue;
            if (silent_why(v)) continue;
            missing++;
            if (!firstmissing[0])
                snprintf(firstmissing, sizeof firstmissing,
                         "`%s` is a verb the tower answers to and `help` does not "
                         "name it. Document it beside the verbs it belongs with, or "
                         "-- if a player is not meant to have it -- put it in "
                         "HELP_SILENT in core/mancheck.c with the reason", v);
        }
        char what[96];
        snprintf(what, sizeof what, "every one of the tower's %d verbs is on the page",
                 site_verb_count());
        ck(what, missing == 0, firstmissing);
    }

    buf_free(&help);
    site_free(&s);
    bld_free(&b);
}

int man_check(void)
{
    passed = total = 0;
    printf("every command a manual page shows, run on a real machine\n");

    Machine m;
    machine_install(&m, 4242);
    machine_boot(&m);
    if (!m.boot.running) {
        ck("a machine boots to run the examples on", false, "it did not boot");
        machine_free(&m);
        printf("\n%d/%d manual claims hold\n", passed, total);
        return 1;
    }

    Buf names = {0};
    if (vfs_list(&m.disk, "/usr/share/man", &names) != IO_OK || !names.p) {
        ck("the manuals are on the disk", false, "/usr/share/man did not list");
        buf_free(&names);
        machine_free(&m);
        printf("\n%d/%d manual claims hold\n", passed, total);
        return 1;
    }

    int pages = 0, ran = 0, skipped = 0;
    /* A page that offers nothing runnable passes this gate without being
     * checked at all, which looks exactly like a page that passed. Name
     * them, so the coverage gap is a number somebody can see rather than a
     * row of ok. */
    Buf unverified = {0};
    int nunverified = 0;
    char page[64];
    const char *n = names.p;
    while (*n) {
        const char *nl = strchr(n, '\n');
        size_t len = nl ? (size_t)(nl - n) : strlen(n);
        if (len && len < sizeof page) {
            memcpy(page, n, len);
            page[len] = 0;
            pages++;

            char path[128];
            snprintf(path, sizeof path, "/usr/share/man/%s", page);
            Buf body = {0};
            if (vfs_read(&m.disk, path, &body) == IO_OK && body.p) {
                /* THE PAGE MUST BE READABLE AS WELL AS PRESENT. A page
                 * installed at mode 0000 is listed by `man` and refused by
                 * it, which is how one shipped this morning. */
                Buf o = {0};
                char cmd[160];
                snprintf(cmd, sizeof cmd, "man %s", page);
                kernel_run(&m, cmd, &o);
                bool readable = o.p && !strstr(o.p, "no manual entry");
                if (!readable) {
                    char why[128];
                    snprintf(why, sizeof why, "`man %s` cannot read it -- "
                             "check its mode in core/image.c", page);
                    ck(page, false, why);
                    buf_free(&o);
                    buf_free(&body);
                    n = nl ? nl + 1 : n + len;
                    continue;
                }
                buf_free(&o);

                Scan r = scan_text(&m, body.p);
                int tried = r.tried;
                ran += r.tried;
                skipped += r.skipped;
                char what[128];
                snprintf(what, sizeof what, "%.40s (%d example%s run)",
                         page, tried, tried == 1 ? "" : "s");
                ck(what, r.bad == 0, r.firstbad);
                if (tried == 0) {
                    if (nunverified) buf_puts(&unverified, " ");
                    buf_puts(&unverified, page);
                    nunverified++;
                }
            } else {
                ck(page, false, "listed by the directory and unreadable");
            }
            buf_free(&body);
        }
        n = nl ? nl + 1 : n + len;
    }

    buf_free(&names);
    check_docs(&m, &ran, &skipped);
    check_web(&m, &ran, &skipped);
    check_shop();
    check_help();
    machine_free(&m);

    printf("\n%d page(s), %d example(s) run, %d skipped as not read-only\n",
           pages, ran, skipped);
    if (nunverified)
        printf("%d page(s) offered nothing runnable and are therefore NOT\n"
               "verified by this gate -- their examples are placeholders,\n"
               "prose, or writing commands: %s\n",
               nunverified, unverified.p ? unverified.p : "");
    buf_free(&unverified);
    printf("%d/%d manual claims hold\n", passed, total);
    return passed == total ? 0 : 1;
}
