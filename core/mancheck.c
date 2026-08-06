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
