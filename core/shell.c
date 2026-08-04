/* shell.c — the command layer, shared by the socket and the in-game terminal.
 *
 * Protocol: one request per line. Every response is a status line
 * (`+OK ...` or `-ERR ...`), optional body lines, then a lone `.` terminator.
 * Body lines beginning with `.` are dot-stuffed to `..`. That is enough
 * structure to parse reliably and still plain enough to drive by hand from
 * telnet, which is the point — see docs/protocol.md.
 */
#include "nom.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

struct Shell {
    Sim  *sim;
    /* The host this session is logged into. Everything the session does is
     * resolved under `root`, so after `ssh wreck-01` a plain `ls /dev` lists
     * the WRECK's devices, exactly as it would over a real ssh. */
    char  host[64];
    char  root[NOM_PATH_MAX];
    char  cwd[NOM_PATH_MAX];
    /* `put` captures following lines until a lone "." */
    bool  capturing;
    char  capture_path[NOM_PATH_MAX];
    Buf   capture;
};

Shell *shell_new(Sim *sim)
{
    Shell *sh = nom_alloc(sizeof(Shell));
    sh->sim = sim;
    snprintf(sh->host, sizeof sh->host, "station");
    sh->root[0] = 0;
    snprintf(sh->cwd, sizeof sh->cwd, "/home");
    buf_init(&sh->capture);
    return sh;
}

void shell_free(Shell *sh)
{
    if (!sh) return;
    buf_free(&sh->capture);
    nom_free(sh);
}

const char *shell_cwd(Shell *sh) { return sh->cwd; }

/* ------------------------------------------------------------- responses */
static void ok(Buf *out, const char *fmt, ...)
{
    va_list ap;
    char msg[512];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    buf_printf(out, "+OK %s\n", msg);
}

static void err(Buf *out, const char *fmt, ...)
{
    va_list ap;
    char msg[512];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    buf_printf(out, "-ERR %s\n.\n", msg);
}

static void body(Buf *out, const char *text, size_t len)
{
    size_t i = 0;
    bool at_line_start = true;
    while (i < len) {
        if (at_line_start && text[i] == '.') buf_putc(out, '.');   /* dot-stuff */
        buf_putc(out, text[i]);
        at_line_start = (text[i] == '\n');
        i++;
    }
    if (len && text[len - 1] != '\n') buf_putc(out, '\n');
}

static void endbody(Buf *out) { buf_puts(out, ".\n"); }

/* ---------------------------------------------------------------- parsing */
static int split_args(char *line, char *argv[], int maxargs)
{
    int n = 0;
    char *p = line;
    while (*p && n < maxargs) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

static void resolve(Shell *sh, const char *in, char *out, size_t outsz)
{
    char local[NOM_PATH_MAX * 2];
    vfs_normalize(sh->cwd, in, local, sizeof local);
    if (sh->root[0]) snprintf(out, outsz, "%s%s", sh->root, local);
    else             snprintf(out, outsz, "%s", local);
}

/* ------------------------------------------------------------------ help */
static const char *HELP =
"NOMINAL — station mainframe.  Every response ends with a lone '.'\n"
"\n"
"YOU ARE OPS.  Tenants pay while somebody is looking after them. Somebody is\n"
"your scripts. Start here:   station    ls /srv    cat /home/scripts/serve.nom\n"
"\n"
"the station\n"
"  station                 tenants, service, income and the power bill\n"
"  msg                     the pager. tenants report symptoms, never causes\n"
"  slots                   installed hardware\n"
"  trace <device>          what it depends on, and where that chain breaks\n"
"  log [n] | tail [file]   events, /var/log/messages\n"
"\n"
"scripts  (this is the job)\n"
"  ls /srv                 one directory per tenant\n"
"  cat /srv/<seg>/status   what it needs and how fresh its heartbeat is\n"
"  attach <path>           add a script      detach   remove them all\n"
"  launch                  compile and start everything attached\n"
"  ps                      what is running, and what it is blocked on\n"
"  cat /proc/<pid>/status  per-script detail\n"
"  put <path>              write a script: send lines, end with a lone '.'\n"
"\n"
"hardware and money\n"
"  catalog                 what the replicator can print\n"
"  order <part>            print one (costs credits, takes time)\n"
"  install <part> <slot>   fit it from the receiving bay\n"
"  wire <thing> <switch>   measure, replicate and run a cable in one go\n"
"  measure <a> <b>         price a run before committing\n"
"  rewire | unwire | cable | place\n"
"  pull <slot>             remove a card, half the value back\n"
"\n"
"getting about\n"
"  goto <core|plant|seg>   teleport. core has the desktop, plant has the racks\n"
"  ssh <host> | logout     log into another machine; `hosts` lists them\n"
"  sshfs <host> <path>     mount one instead, for when ssh stops scaling\n"
"  bind <a> <b> | mount-all\n"
"\n"
"files\n"
"  ls | cat | put | write | rm | mkdir | cd | pwd\n"
"\n"
"time\n"
"  step [n]                advance n ticks      run [n]   advance a long way\n"
"  reset [seed] | seed <n> | budget <n>\n"
"\n"
"  help | quit\n";

/* ---------------------------------------------------------------- filters
 * `cat /srv/lab-1/status | grep service` reads like a shell because it is one.
 * These are the handful of filters that make the /proc and /srv trees usable
 * without leaving the prompt. */
static void filter_grep(const Buf *in, const char *pat, bool invert, Buf *out)
{
    const char *p = in->p ? in->p : "";
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        char line[1024];
        size_t l = (size_t)(e - p);
        if (l > sizeof line - 1) l = sizeof line - 1;
        memcpy(line, p, l);
        line[l] = 0;
        bool hit = strstr(line, pat) != NULL;
        if (hit != invert) { buf_put(out, line, l); buf_putc(out, '\n'); }
        p = *e ? e + 1 : e;
    }
}

static void filter_head(const Buf *in, int n, bool from_end, Buf *out)
{
    int total = 0;
    for (const char *q = in->p ? in->p : ""; *q; q++) if (*q == '\n') total++;
    int skip = from_end ? (total - n > 0 ? total - n : 0) : 0;
    int seen = 0, kept = 0;
    const char *p = in->p ? in->p : "";
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        if (seen >= skip && (from_end || kept < n)) {
            buf_put(out, p, (size_t)(e - p));
            buf_putc(out, '\n');
            kept++;
        }
        seen++;
        p = *e ? e + 1 : e;
    }
}

static void filter_wc(const Buf *in, Buf *out)
{
    int lines = 0;
    for (const char *q = in->p ? in->p : ""; *q; q++) if (*q == '\n') lines++;
    buf_printf(out, "%d lines, %zu bytes\n", lines, in->len);
}

static void filter_sort(const Buf *in, Buf *out)
{
    char *lines[512];
    int n = 0;
    char *copy = nom_strdup(in->p ? in->p : "");
    char *p = copy;
    while (*p && n < 512) {
        lines[n++] = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
    }
    for (int i = 1; i < n; i++) {
        char *k = lines[i];
        int j = i - 1;
        while (j >= 0 && strcmp(lines[j], k) > 0) { lines[j+1] = lines[j]; j--; }
        lines[j+1] = k;
    }
    for (int i = 0; i < n; i++) { buf_puts(out, lines[i]); buf_putc(out, '\n'); }
    nom_free(copy);
}

/* Apply one `| filter args` stage. */
static void apply_filter(Buf *in, char *argv[], int argc, Buf *out)
{
    if (argc == 0) { buf_put(out, in->p, in->len); return; }
    const char *f = argv[0];
    if (strcmp(f, "grep") == 0 && argc > 1)      filter_grep(in, argv[1], false, out);
    else if (strcmp(f, "grep-v") == 0 && argc > 1) filter_grep(in, argv[1], true, out);
    else if (strcmp(f, "head") == 0)             filter_head(in, argc > 1 ? atoi(argv[1]) : 10, false, out);
    else if (strcmp(f, "tail") == 0)             filter_head(in, argc > 1 ? atoi(argv[1]) : 10, true, out);
    else if (strcmp(f, "wc") == 0)               filter_wc(in, out);
    else if (strcmp(f, "sort") == 0)             filter_sort(in, out);
    else buf_printf(out, "no filter '%s' (grep, grep-v, head, tail, wc, sort)\n", f);
}

/* ------------------------------------------------------------------ exec */
static bool shell_exec1(Shell *sh, const char *line, Buf *out);

/* Split on `|` and run the pipeline, feeding each stage the previous body. */
bool shell_exec(Shell *sh, const char *line, Buf *out)
{
    if (sh->capturing || strchr(line, '|') == NULL)
        return shell_exec1(sh, line, out);

    char work[2048];
    snprintf(work, sizeof work, "%s", line);
    char *stage[8];
    int nstage = 0;
    char *p = work;
    stage[nstage++] = p;
    while (*p && nstage < 8) {
        if (*p == '|') { *p++ = 0; while (*p == ' ') p++; stage[nstage++] = p; }
        else p++;
    }

    Buf first;
    buf_init(&first);
    bool keep = shell_exec1(sh, stage[0], &first);

    /* take the body: everything between the status line and the trailing dot */
    Buf body_in;
    buf_init(&body_in);
    const char *q = first.p ? first.p : "";
    const char *nl = strchr(q, '\n');
    bool errored = (strncmp(q, "-ERR", 4) == 0);
    if (nl) {
        const char *bstart = nl + 1;
        size_t blen = first.len - (size_t)(bstart - q);
        if (blen >= 2 && bstart[blen-2] == '.' && bstart[blen-1] == '\n') blen -= 2;
        buf_put(&body_in, bstart, blen);
    }
    if (errored) { buf_put(out, first.p, first.len); buf_free(&first); buf_free(&body_in); return keep; }

    for (int i = 1; i < nstage; i++) {
        char sbuf[512];
        snprintf(sbuf, sizeof sbuf, "%s", stage[i]);
        char *fargv[8];
        int fargc = split_args(sbuf, fargv, 8);
        Buf next;
        buf_init(&next);
        apply_filter(&body_in, fargv, fargc, &next);
        buf_free(&body_in);
        body_in = next;
    }
    ok(out, "%zu bytes", body_in.len);
    body(out, body_in.p ? body_in.p : "", body_in.len);
    endbody(out);
    buf_free(&first);
    buf_free(&body_in);
    return keep;
}

static bool shell_exec1(Shell *sh, const char *line, Buf *out)
{
    const char *rawline = line;
    Sim *s = sh->sim;

    /* capture mode: everything is data until a lone "." */
    if (sh->capturing) {
        if (strcmp(line, ".") == 0) {
            sh->capturing = false;
            VNode *n = vfs_mkfile(&s->fs, sh->capture_path,
                                  sh->capture.p ? sh->capture.p : "");
            size_t len = sh->capture.len;
            buf_clear(&sh->capture);
            if (!n) { err(out, "%s: cannot create", sh->capture_path); return true; }
            ok(out, "wrote %zu bytes to %s", len, sh->capture_path);
            endbody(out);
            return true;
        }
        /* undo dot-stuffing on the way in */
        const char *p = line;
        if (p[0] == '.' && p[1] == '.') p++;
        buf_puts(&sh->capture, p);
        buf_putc(&sh->capture, '\n');
        return true;
    }

    char buf[2048];
    snprintf(buf, sizeof buf, "%s", line);
    char *argv[16];
    int argc = split_args(buf, argv, 16);
    if (argc == 0) { ok(out, "");  endbody(out); return true; }

    const char *cmd = argv[0];
    char path[NOM_PATH_MAX * 2];
    char path_buf[NOM_PATH_MAX * 2];

    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        ok(out, "goodbye");
        endbody(out);
        return false;
    }

    if (strcmp(cmd, "help") == 0) {
        ok(out, "commands");
        body(out, HELP, strlen(HELP));
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "pwd") == 0) { ok(out, "%s", sh->cwd); endbody(out); return true; }

    if (strcmp(cmd, "hostname") == 0 || strcmp(cmd, "whoami") == 0) {
        ok(out, "root@%s", sh->host);
        endbody(out);
        return true;
    }

    /* ssh <host> — log into another machine. The session's whole view moves,
     * so every command you already know keeps working over there. */
    if (strcmp(cmd, "ssh") == 0) {
        extern bool wreck_mount(Sim *, const char *, const char *, char *, size_t);
        if (argc < 2) { err(out, "usage: ssh <host>   (try 'hosts')"); return true; }
        char mnt[NOM_PATH_MAX];
        snprintf(mnt, sizeof mnt, "/n/%s", argv[1]);
        char e[NOM_ERR_MAX];
        if (!vfs_lookup(&s->fs, mnt) && !wreck_mount(s, argv[1], mnt, e, sizeof e)) {
            err(out, "ssh: %s", e);
            return true;
        }
        snprintf(sh->root, sizeof sh->root, "%s", mnt);
        snprintf(sh->host, sizeof sh->host, "%s", argv[1]);
        snprintf(sh->cwd, sizeof sh->cwd, "/");
        ok(out, "logged into %s", sh->host);
        Buf b; buf_init(&b);
        char ident[NOM_PATH_MAX];
        snprintf(ident, sizeof ident, "%s/ident", mnt);
        if (vfs_read(&s->fs, ident, &b) == IO_OK) body(out, b.p, b.len);
        buf_free(&b);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "logout") == 0 || strcmp(cmd, "disconnect") == 0) {
        if (!sh->root[0]) { err(out, "not logged into anything"); return true; }
        sh->root[0] = 0;
        snprintf(sh->host, sizeof sh->host, "station");
        snprintf(sh->cwd, sizeof sh->cwd, "/home");
        ok(out, "back on station");
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "hosts") == 0) {
        Buf b; buf_init(&b);
        vfs_read(&s->fs, "/etc/hosts", &b);
        ok(out, "/etc/hosts");
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    /* mount -a: bring up everything in fstab, the way a box does at boot. */
    if (strcmp(cmd, "mount-all") == 0) {
        extern bool wreck_mount(Sim *, const char *, const char *, char *, size_t);
        Buf fstab; buf_init(&fstab);
        vfs_read(&s->fs, "/etc/fstab", &fstab);
        Buf rep; buf_init(&rep);
        int n = 0;
        const char *p2 = fstab.p ? fstab.p : "";
        while (*p2) {
            const char *e2 = p2; while (*e2 && *e2 != '\n') e2++;
            if (*p2 != '#' && e2 > p2) {
                char line[256];
                size_t l = (size_t)(e2 - p2); if (l > 255) l = 255;
                memcpy(line, p2, l); line[l] = 0;
                char h[128], mp[128];
                if (sscanf(line, "%127s %127s", h, mp) == 2) {
                    char e3[NOM_ERR_MAX];
                    if (wreck_mount(s, h, mp, e3, sizeof e3)) {
                        buf_printf(&rep, "%-12s -> %s\n", h, mp);
                        n++;
                    } else {
                        buf_printf(&rep, "%-12s -- %s\n", h, e3);
                    }
                }
            }
            p2 = *e2 ? e2 + 1 : e2;
        }
        buf_free(&fstab);
        ok(out, "%d filesystem(s) mounted", n);
        body(out, rep.p ? rep.p : "", rep.len);
        endbody(out);
        buf_free(&rep);
        return true;
    }

    /* sshfs <host> <mountpoint> — the thing you reach for when ssh-ing to
     * everything stops scaling. Same idea a Linux admin already uses. */
    if (strcmp(cmd, "sshfs") == 0) {
        extern bool wreck_mount(Sim *, const char *, const char *, char *, size_t);
        if (argc < 3) { err(out, "usage: sshfs <host> <mountpoint>"); return true; }
        char e[NOM_ERR_MAX];
        resolve(sh, argv[2], path_buf, sizeof path_buf);
        if (!wreck_mount(s, argv[1], path_buf, e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "%s mounted at %s", argv[1], path_buf);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "cd") == 0) {
        resolve(sh, argc > 1 ? argv[1] : "/home", path, sizeof path);
        VNode *n = vfs_lookup(&s->fs, path);
        if (!n) { err(out, "%s: no such directory", path); return true; }
        if (n->kind != VN_DIR) { err(out, "%s: not a directory", path); return true; }
        snprintf(sh->cwd, sizeof sh->cwd, "%s", path);
        ok(out, "%s", sh->cwd);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dev") == 0) {
        if (strcmp(cmd, "dev") == 0) snprintf(path, sizeof path, "/dev");
        else resolve(sh, argc > 1 ? argv[1] : ".", path, sizeof path);
        Buf b;
        buf_init(&b);
        if (vfs_list(&s->fs, path, &b) != IO_OK) { err(out, "%s", s->fs.err); buf_free(&b); return true; }
        ok(out, "%s", path);
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "cat") == 0) {
        if (argc < 2) { err(out, "usage: cat <path>"); return true; }
        resolve(sh, argv[1], path, sizeof path);
        Buf b;
        buf_init(&b);
        IoStatus st = vfs_read(&s->fs, path, &b);
        if (st == IO_BLOCK) {
            buf_free(&b);
            /* A terminal cannot block the simulation; say so honestly rather
             * than pretending the device returned nothing. */
            err(out, "%s: would block (device has no reading yet)", path);
            return true;
        }
        if (st != IO_OK) { buf_free(&b); err(out, "%s", s->fs.err); return true; }
        ok(out, "%s (%zu bytes)", path, b.len);
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "put") == 0) {
        if (argc < 2) { err(out, "usage: put <path>   (then lines, then a lone '.')"); return true; }
        resolve(sh, argv[1], sh->capture_path, sizeof sh->capture_path);
        buf_clear(&sh->capture);
        sh->capturing = true;
        buf_printf(out, "+DATA send lines, end with a lone '.'\n");
        return true;
    }

    if (strcmp(cmd, "write") == 0) {
        if (argc < 2) { err(out, "usage: write <path> <text>"); return true; }
        resolve(sh, argv[1], path, sizeof path);
        /* rejoin argv[2..] with single spaces */
        Buf t;
        buf_init(&t);
        for (int i = 2; i < argc; i++) { if (i > 2) buf_putc(&t, ' '); buf_puts(&t, argv[i]); }
        IoStatus st = vfs_write(&s->fs, path, t.p ? t.p : "", t.len);
        if (st != IO_OK) { err(out, "%s", s->fs.err); buf_free(&t); return true; }
        ok(out, "wrote %zu bytes to %s", t.len, path);
        endbody(out);
        buf_free(&t);
        return true;
    }

    if (strcmp(cmd, "rm") == 0) {
        if (argc < 2) { err(out, "usage: rm <path>"); return true; }
        resolve(sh, argv[1], path, sizeof path);
        if (!vfs_remove(&s->fs, path)) { err(out, "%s: cannot remove", path); return true; }
        ok(out, "removed %s", path);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "mkdir") == 0) {
        if (argc < 2) { err(out, "usage: mkdir <path>"); return true; }
        resolve(sh, argv[1], path, sizeof path);
        if (!vfs_mkdir(&s->fs, path)) { err(out, "%s: cannot create", path); return true; }
        ok(out, "created %s", path);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "attach") == 0) {
        if (argc < 2) { err(out, "usage: attach <path>"); return true; }
        resolve(sh, argv[1], path, sizeof path);
        char e[NOM_ERR_MAX];
        if (!sim_attach(s, path, e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "attached %s (%d script(s))", path, s->nscripts);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "detach") == 0) {
        sim_detach_all(s);
        ok(out, "detached all scripts");
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "seed") == 0) {
        if (argc < 2) { err(out, "usage: seed <n>"); return true; }
        Value v;
        if (!nom_parse_number(argv[1], strlen(argv[1]), &v)) { err(out, "seed must be a number"); return true; }
        sim_reset(s, (uint64_t)val_int(v));
        ok(out, "seed %llu, ship reset to cold", (unsigned long long)s->seed);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "budget") == 0) {
        if (argc < 2) { err(out, "usage: budget <instructions-per-tick>"); return true; }
        Value v;
        if (!nom_parse_number(argv[1], strlen(argv[1]), &v)) { err(out, "budget must be a number"); return true; }
        int b = (int)val_int(v);
        if (b < 1) b = 1;
        s->budget_max = b;
        ok(out, "budget %d instructions per script per tick at full compute power", s->budget_max);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "reset") == 0) {
        uint64_t seed = s->seed;
        if (argc > 1) {
            Value v;
            if (nom_parse_number(argv[1], strlen(argv[1]), &v)) seed = (uint64_t)val_int(v);
        }
        sim_reset(s, seed);
        ok(out, "reset to the cold derelict, seed %llu", (unsigned long long)seed);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "launch") == 0) {
        if (argc > 1) {
            Value v;
            if (nom_parse_number(argv[1], strlen(argv[1]), &v)) sim_reset(s, (uint64_t)val_int(v));
        } else if (s->run != RUN_SETUP) {
            sim_reset(s, s->seed);
        }
        char e[NOM_ERR_MAX];
        if (!sim_launch(s, e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "launched: seed %llu, %d script(s), budget %d",
           (unsigned long long)s->seed, s->nscripts, s->budget_max);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "step") == 0) {
        int n = 1;
        if (argc > 1) {
            Value v;
            if (nom_parse_number(argv[1], strlen(argv[1]), &v)) n = (int)val_int(v);
        }
        if (s->run != RUN_ACTIVE) { err(out, "not running (state is '%s'); launch first",
                                        s->run == RUN_SETUP ? "setup" : "finished"); return true; }
        int done = 0;
        for (int i = 0; i < n && s->run == RUN_ACTIVE; i++) { sim_tick(s); done++; }
        /* a short report; `stat` is there when you want the whole machine */
        Buf b;
        buf_init(&b);
        buf_printf(&b, "credits %.0f (%+.2f/tick)   O2 %.0f%%   bay %.0fC   pool %d",
                   s->credits, s->income_last - s->power_bill_last, s->o2,
                   s->cpu.bay_temp, s->cpu.pool);
        if (s->cpu.throttled) buf_puts(&b, "  THROTTLED");
        if (s->bus.brownout)  buf_puts(&b, "  BROWNOUT");
        buf_putc(&b, '\n');
        int unserved = 0, unread = sim_unread(s);
        for (int i = 0; i < s->nsegs; i++)
            if (s->seg[i].docked && s->seg[i].service < s->seg[i].sla) unserved++;
        if (unserved) buf_printf(&b, "%d of %d tenant(s) below SLA — `station`\n", unserved, s->nsegs);
        if (unread)   buf_printf(&b, "%d unread message(s) — `msg`\n", unread);
        ok(out, "tick %llu (+%d)", (unsigned long long)s->tick, done);
        body(out, b.p, b.len);
        buf_free(&b);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "run") == 0) {
        uint64_t max = s->max_ticks;
        if (argc > 1) {
            Value v;
            if (nom_parse_number(argv[1], strlen(argv[1]), &v)) max = (uint64_t)val_int(v);
        }
        if (s->run != RUN_ACTIVE) { err(out, "not running; launch first"); return true; }
        uint64_t n = sim_run_to_end(s, max);
        ok(out, "ran %llu tick(s), run is '%s'", (unsigned long long)n,
           s->run == RUN_WON ? "won" : s->run == RUN_LOST ? "lost" : "active");
        Buf b;
        buf_init(&b);
        sim_status(s, &b);
        body(out, b.p, b.len);
        buf_free(&b);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "stat") == 0) {
        Buf b;
        buf_init(&b);
        sim_status(s, &b);
        ok(out, "tick %llu", (unsigned long long)s->tick);
        body(out, b.p, b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    /* ---- namespace: the reason any of this is interesting ----------- */
    if (strcmp(cmd, "mount") == 0) {
        extern bool wreck_mount(Sim *, const char *, const char *, char *, size_t);
        if (argc < 3) { err(out, "usage: mount <host> <mountpoint>   e.g. mount wreck-01 /n/wreck"); return true; }
        resolve(sh, argv[2], path_buf, sizeof path_buf);
        char e[NOM_ERR_MAX];
        if (!wreck_mount(s, argv[1], path_buf, e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "mounted %s at %s", argv[1], path_buf);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "bind") == 0 || strcmp(cmd, "mount") == 0) {
        extern bool wreck_mount(Sim *, const char *, const char *, char *, size_t);
        if (argc < 3) {
            err(out, "usage: bind <host|path> <mountpoint>\n"
                     "  bind wreck-01 /my/mount        the whole machine, same view as ssh\n"
                     "  bind /n/w/dev/thm-04 /dev/x    one file, grafted where you want it");
            return true;
        }
        /* A known host binds the whole machine; anything else binds one path. */
        char e[NOM_ERR_MAX];
        resolve(sh, argv[2], path_buf, sizeof path_buf);
        if (!strchr(argv[1], '/')) {
            if (!wreck_mount(s, argv[1], path_buf, e, sizeof e)) { err(out, "%s", e); return true; }
            ok(out, "%s bound at %s (same view as 'ssh %s')", argv[1], path_buf, argv[1]);
            endbody(out);
            return true;
        }
        char tgt[NOM_PATH_MAX * 2];
        resolve(sh, argv[1], tgt, sizeof tgt);
        if (!vfs_bind(&s->fs, tgt, path_buf)) { err(out, "%s", s->fs.err); return true; }
        ok(out, "bound %s -> %s", path_buf, tgt);
        endbody(out);
        return true;
    }

    /* mount with no arguments prints the table, like the real thing. */
    if (strcmp(cmd, "mounts") == 0) {
        Buf b; buf_init(&b);
        vfs_read(&s->fs, "/etc/fstab", &b);
        ok(out, "/etc/fstab");
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    /* ---- the sysadmin verbs ---------------------------------------- */
    if (strcmp(cmd, "slots") == 0 || strcmp(cmd, "lspci") == 0) {
        Buf b; buf_init(&b);
        buf_puts(&b, "slot dev       part           state     hp  duty  draw  got  eff\n");
        for (int i = 0; i < SLOT_COUNT; i++) {
            Buf sb; buf_init(&sb);
            char path[NOM_PATH_MAX];
            snprintf(path, sizeof path, "/sys/slot/%d", i);
            vfs_read(&s->fs, path, &sb);
            if (strstr(sb.p ? sb.p : "", "state empty")) buf_printf(&b, " %-3d (empty)\n", i);
            buf_free(&sb);
        }
        buf_free(&b);
        /* the full table already lives in sim_status; show that instead */
        Buf t; buf_init(&t);
        sim_status(s, &t);
        const char *from = strstr(t.p ? t.p : "", "slot dev");
        ok(out, "hardware");
        body(out, from ? from : (t.p ? t.p : ""), from ? strlen(from) : t.len);
        endbody(out);
        buf_free(&t);
        return true;
    }

    if (strcmp(cmd, "patch") == 0 || strcmp(cmd, "connect") == 0) {
        if (argc < 3) {
            err(out, "usage: patch <device> <rail|spine> [metres]\n"
                     "  the cable tool measures the run, prints a patch cable and fits it.\n"
                     "  cable is %.0f cr/m — where you rack something is a real cost.",
                sim_cable_rate());
            return true;
        }
        double m = 0.0;
        if (argc > 3) {
            Value v;
            if (nom_parse_number(argv[3], strlen(argv[3]), &v)) m = val_num(v);
        }
        char e[NOM_ERR_MAX];
        if (!sim_connect(s, argv[1], argv[2], m, e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "patched %s into %s, %.0f credits left", argv[1], argv[2], s->credits);
        endbody(out);
        return true;
    }

    /* wire — the whole job in one verb: measure, replicate, transport, fit.
     * Dragging in the bay is a convenience; this is the reliable path. */
    if (strcmp(cmd, "wire") == 0) {
        if (argc < 3) {
            err(out, "usage: wire <card|segment> <switch>\n"
                     "  measures the run, replicates the cable and transports it into place.\n"
                     "  e.g. wire hab-1 rail0    wire cpu0 data0");
            return true;
        }
        double m = sim_measure(s, argv[1], argv[2]);
        char e[NOM_ERR_MAX];
        if (!sim_wire(s, argv[1], argv[2], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "measured %.1f m, replicated and fitted %s -> %s.  %.0f cr left",
           m, argv[1], argv[2], s->credits);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "unwire") == 0) {
        if (argc < 3) { err(out, "usage: unwire <card|segment> <switch>"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_unwire(s, argv[1], argv[2], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "unwired %s from %s", argv[1], argv[2]);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "rewire") == 0) {
        if (argc < 4) { err(out, "usage: rewire <card|segment> <old-switch> <new-switch>"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_unwire(s, argv[1], argv[2], e, sizeof e)) { err(out, "%s", e); return true; }
        double m = sim_measure(s, argv[1], argv[3]);
        if (!sim_wire(s, argv[1], argv[3], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "moved %s from %s to %s (%.1f m).  %.0f cr left",
           argv[1], argv[2], argv[3], m, s->credits);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "measure") == 0) {
        if (argc < 3) { err(out, "usage: measure <a> <b>"); return true; }
        double m = sim_measure(s, argv[1], argv[2]);
        if (m <= 0.0) { err(out, "cannot measure %s to %s", argv[1], argv[2]); return true; }
        ok(out, "%.1f m, %.0f cr to replicate", m, m * sim_cable_rate());
        endbody(out);
        return true;
    }

    /* move something in the bay from the command line, so a tidy-up does not
     * require the mouse either */
    if (strcmp(cmd, "place") == 0) {
        if (argc < 4) { err(out, "usage: place <thing> <x> <y>"); return true; }
        Value vx, vy;
        if (!nom_parse_number(argv[2], strlen(argv[2]), &vx) ||
            !nom_parse_number(argv[3], strlen(argv[3]), &vy)) {
            err(out, "x and y must be numbers"); return true;
        }
        char e[NOM_ERR_MAX];
        if (!sim_place(s, argv[1], val_num(vx), val_num(vy), e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "moved %s", argv[1]);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "cable") == 0) {
        ok(out, "cable tool");
        Buf b; buf_init(&b);
        buf_printf(&b, "rate      %.0f cr per metre (measure, replicate, fit)\n", sim_cable_rate());
        buf_printf(&b, "spent     %.0f cr on patch cable so far\n", sim_cable_spent(s));
        buf_puts(&b, "\nrunning cable:\n");
        for (int i = 0; i < s->nsegs; i++) {
            if (!s->seg[i].docked) continue;
            for (int k = 0; k < 2; k++) {
                int l = k == 0 ? s->seg[i].rail : s->seg[i].spine;
                if (l < 0) continue;
                buf_printf(&b, "  %-8s %-5s -> %-8s %5.1f m\n", s->seg[i].name,
                           k == 0 ? "power" : "data", s->slot[l].dev,
                           sim_measure(s, s->seg[i].name, s->slot[l].dev));
            }
        }
        for (int i = 0; i < SLOT_COUNT; i++) {
            if (s->slot[i].part < 0) continue;
            for (int k = 0; k < 2; k++) {
                if (s->slot[i].link[k] < 0) continue;
                buf_printf(&b, "  %-8s %-5s -> %-8s %5.1f m\n", s->slot[i].dev,
                           k == 0 ? "power" : "data", s->slot[s->slot[i].link[k]].dev,
                           s->slot[i].cable_m[k]);
            }
        }
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "unpatch") == 0 || strcmp(cmd, "disconnect") == 0) {
        if (argc < 3) { err(out, "usage: unpatch <device> <rail|spine>"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_disconnect(s, argv[1], argv[2], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "unpatched %s from %s", argv[1], argv[2]);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "trace") == 0) {
        if (argc < 2) { err(out, "usage: trace <device>   e.g. trace sen0"); return true; }
        Buf b; buf_init(&b);
        sim_trace(s, argv[1], &b);
        ok(out, "trace %s", argv[1]);
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "kill") == 0 || strcmp(cmd, "restart") == 0 || strcmp(cmd, "start") == 0) {
        if (argc < 2) { err(out, "usage: %s <pid>   (start takes a path)", cmd); return true; }
        char e[NOM_ERR_MAX];
        if (strcmp(cmd, "start") == 0) {
            resolve(sh, argv[1], path_buf, sizeof path_buf);
            if (!sim_start_script(s, path_buf, e, sizeof e)) { err(out, "%s", e); return true; }
            ok(out, "started %s as pid %d", path_buf, s->nscripts);
            endbody(out);
            return true;
        }
        Value v;
        if (!nom_parse_number(argv[1], strlen(argv[1]), &v)) { err(out, "pid must be a number"); return true; }
        int pid = (int)val_int(v);
        bool okk = (strcmp(cmd, "kill") == 0) ? sim_kill(s, pid, e, sizeof e)
                                              : sim_restart(s, pid, e, sizeof e);
        if (!okk) { err(out, "%s", e); return true; }
        ok(out, "%s pid %d", strcmp(cmd, "kill") == 0 ? "killed" : "restarted", pid);
        endbody(out);
        return true;
    }

    /* edit — line-at-a-time, because `put` rewriting the whole file to change
     * one line is the most annoying thing about working over a socket. */
    if (strcmp(cmd, "edit") == 0) {
        if (argc < 2) {
            err(out, "usage:\n"
                     "  edit <file>              show it, numbered\n"
                     "  edit <file> <n> <text>   replace line n\n"
                     "  edit <file> +<n> <text>  insert after line n (+0 for the top)\n"
                     "  edit <file> -<n>         delete line n");
            return true;
        }
        resolve(sh, argv[1], path_buf, sizeof path_buf);
        VNode *f = vfs_lookup(&s->fs, path_buf);
        if (!f || f->kind != VN_FILE) { err(out, "%s: no such file", path_buf); return true; }

        /* split into lines */
        char *lines[512];
        int nl = 0;
        char *copy = nom_strdup(f->data.p ? f->data.p : "");
        char *p2 = copy;
        while (*p2 && nl < 512) {
            lines[nl++] = p2;
            while (*p2 && *p2 != '\n') p2++;
            if (*p2) *p2++ = 0;
        }
        if (nl && lines[nl-1][0] == 0) nl--;

        if (argc == 2) {                       /* show */
            Buf b; buf_init(&b);
            for (int i = 0; i < nl; i++) buf_printf(&b, "%4d  %s\n", i + 1, lines[i]);
            ok(out, "%s (%d lines)", path_buf, nl);
            body(out, b.p ? b.p : "", b.len);
            endbody(out);
            buf_free(&b);
            nom_free(copy);
            return true;
        }

        const char *spec = argv[2];
        bool insert = spec[0] == '+', del = spec[0] == '-';
        int n = atoi(insert || del ? spec + 1 : spec);
        if (!del && argc < 4 && !insert) { err(out, "nothing to put on line %d", n); nom_free(copy); return true; }
        if (!insert && (n < 1 || n > nl)) { err(out, "no line %d (file has %d)", n, nl); nom_free(copy); return true; }
        if (insert && (n < 0 || n > nl))  { err(out, "cannot insert after line %d", n); nom_free(copy); return true; }

        /* Take the REST OF THE RAW LINE, not the re-joined argv: this language
         * is indentation-sensitive and split_args eats leading spaces. */
        Buf text; buf_init(&text);
        {
            const char *r = rawline;
            int skipped = 0;
            while (*r && skipped < 3) {
                while (*r == ' ' || *r == '\t') r++;
                if (!*r) break;
                while (*r && *r != ' ' && *r != '\t') r++;
                skipped++;
            }
            if (*r == ' ') r++;              /* exactly one separator space */
            buf_puts(&text, r);
            while (text.len && (text.p[text.len-1] == '\r' || text.p[text.len-1] == '\n'))
                text.p[--text.len] = 0;
        }

        Buf outbuf; buf_init(&outbuf);
        for (int i = 0; i < nl; i++) {
            if (insert && i == n) { buf_put(&outbuf, text.p, text.len); buf_putc(&outbuf, '\n'); }
            if (del && i == n - 1) continue;
            if (!del && !insert && i == n - 1) { buf_put(&outbuf, text.p, text.len); buf_putc(&outbuf, '\n'); continue; }
            buf_puts(&outbuf, lines[i]);
            buf_putc(&outbuf, '\n');
        }
        if (insert && n == nl) { buf_put(&outbuf, text.p, text.len); buf_putc(&outbuf, '\n'); }

        buf_clear(&f->data);
        buf_put(&f->data, outbuf.p ? outbuf.p : "", outbuf.len);
        ok(out, "%s: %s line %d.  `restart <pid>` to deploy it.", path_buf,
           del ? "deleted" : insert ? "inserted after" : "replaced", n);
        endbody(out);
        buf_free(&outbuf); buf_free(&text);
        nom_free(copy);
        return true;
    }

    if (strcmp(cmd, "man") == 0) {
        extern const char *nom_man(const char *topic);
        const char *page = nom_man(argc > 1 ? argv[1] : "");
        ok(out, "man %s", argc > 1 ? argv[1] : "index");
        body(out, page, strlen(page));
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "priority") == 0 || strcmp(cmd, "pri") == 0) {
        if (argc < 3) { err(out, "usage: priority <tenant> <position>   (1 is served first)"); return true; }
        Value v;
        if (!nom_parse_number(argv[2], strlen(argv[2]), &v)) { err(out, "position must be a number"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_priority(s, argv[1], (int)val_int(v), e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "%s is now priority %d", argv[1], (int)val_int(v));
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "msg") == 0 || strcmp(cmd, "messages") == 0) {
        Buf b; buf_init(&b);
        int unread = sim_unread(s);
        sim_messages(s, &b, false);
        ok(out, "%d message(s), %d unread", s->nmsgs, unread);
        body(out, b.p ? b.p : "(nothing yet)\n", b.len ? b.len : 14);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "station") == 0 || strcmp(cmd, "st") == 0) {
        Buf b; buf_init(&b);
        sim_station(s, &b);
        ok(out, "station");
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "order") == 0) {
        if (argc < 2) { err(out, "usage: order <part-id>   (see 'catalog')"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_order(s, argv[1], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "replicator queued %s, %d credits left", argv[1], (int)s->credits);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "install") == 0 || strcmp(cmd, "fit") == 0) {
        if (argc < 3) { err(out, "usage: install <part-id> <slot>"); return true; }
        char rp[NOM_PATH_MAX];
        snprintf(rp, sizeof rp, "/mnt/replicator/%s", argv[1]);
        if (!vfs_lookup(&s->fs, rp)) {
            err(out, "%s is not in the receiving bay — 'order' it first", argv[1]);
            return true;
        }
        Value v;
        if (!nom_parse_number(argv[2], strlen(argv[2]), &v)) { err(out, "slot must be a number"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_install(s, (int)val_int(v), argv[1], e, sizeof e)) { err(out, "%s", e); return true; }
        vfs_remove(&s->fs, rp);
        ok(out, "fitted %s in slot %s — now patch it into a rail", argv[1], argv[2]);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "goto") == 0 || strcmp(cmd, "tp") == 0) {
        if (argc < 2) { err(out, "usage: goto <segment|core>"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_teleport(s, argv[1], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "you are in %s", s->here);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "dock") == 0) {
        if (argc < 2) { err(out, "usage: dock <habitat|lab|foundry|dock>"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_dock_segment(s, argv[1], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "%s docked", s->seg[s->nsegs-1].name);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "segpatch") == 0) {
        if (argc < 3) { err(out, "usage: segpatch <segment> <rail|spine>"); return true; }
        char e[NOM_ERR_MAX];
        if (!sim_seg_patch(s, argv[1], argv[2], e, sizeof e)) { err(out, "%s", e); return true; }
        ok(out, "%s patched into %s", argv[1], argv[2]);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "catalog") == 0) {
        Buf b; buf_init(&b);
        vfs_read(&s->fs, "/mnt/catalog/parts", &b);
        ok(out, "%d credits available", (int)s->credits);
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "buy") == 0) {
        if (argc < 3) { err(out, "usage: buy <part-id> <slot>   (see 'catalog')"); return true; }
        Value v;
        if (!nom_parse_number(argv[2], strlen(argv[2]), &v)) { err(out, "slot must be a number"); return true; }
        int slotno = (int)val_int(v);
        int ci = catalog_find(argv[1]);
        if (ci < 0) { err(out, "no part '%s' in the catalog", argv[1]); return true; }
        int nc; const PartSpec *cat = catalog(&nc);
        if ((double)cat[ci].price > s->credits) {
            err(out, "%s costs %d, you have %d", cat[ci].id, cat[ci].price, (int)s->credits);
            return true;
        }
        char e[NOM_ERR_MAX];
        if (!sim_install(s, slotno, argv[1], e, sizeof e)) { err(out, "%s", e); return true; }
        s->credits -= cat[ci].price;
        ok(out, "installed %s in slot %d as %s (-%d cr, %d left)",
           cat[ci].id, slotno, s->slot[slotno].dev, cat[ci].price, (int)s->credits);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "pull") == 0) {
        if (argc < 2) { err(out, "usage: pull <slot>"); return true; }
        Value v;
        if (!nom_parse_number(argv[1], strlen(argv[1]), &v)) { err(out, "slot must be a number"); return true; }
        char e[NOM_ERR_MAX];
        int slotno = (int)val_int(v);
        int ci = s->slot[slotno].part;
        if (!sim_remove(s, slotno, e, sizeof e)) { err(out, "%s", e); return true; }
        if (ci >= 0) {
            int nc; const PartSpec *cat = catalog(&nc);
            int refund = cat[ci].price / 2;
            s->credits += refund;
            ok(out, "pulled slot %d (+%d cr salvage, %d total)", slotno, refund, (int)s->credits);
        } else ok(out, "pulled slot %d", slotno);
        endbody(out);
        return true;
    }

    if (strcmp(cmd, "tail") == 0) {
        const char *path = argc > 1 ? argv[1] : "/var/log/messages";
        resolve(sh, path, path_buf, sizeof path_buf);
        Buf b; buf_init(&b);
        if (vfs_read(&s->fs, path_buf, &b) != IO_OK) { err(out, "%s", s->fs.err); buf_free(&b); return true; }
        /* last 25 lines */
        int want = 25, nl = 0;
        const char *start = b.p ? b.p : "";
        for (const char *p2 = start + b.len; p2 > start; p2--)
            if (*(p2 - 1) == '\n' && ++nl > want) { start = p2; break; }
        ok(out, "%s", path_buf);
        body(out, start, b.len - (size_t)(start - (b.p ? b.p : "")));
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "ps") == 0) {
        Buf b;
        buf_init(&b);
        buf_puts(&b, "pid name             state     steps    wchan\n");
        for (int i = 0; i < s->nscripts; i++) {
            char pp[NOM_PATH_MAX];
            snprintf(pp, sizeof pp, "/proc/%d/status", i + 1);
            Buf ps; buf_init(&ps);
            if (vfs_read(&s->fs, pp, &ps) == IO_OK) {
                char nm[48] = "?", st[24] = "?", sp[24] = "0", wc[96] = "";
                const char *q = ps.p ? ps.p : "";
                while (*q) {
                    const char *e2 = q; while (*e2 && *e2 != '\n') e2++;
                    if      (!strncmp(q, "name ",  5)) snprintf(nm, sizeof nm, "%.*s", (int)(e2-q-5),  q+5);
                    else if (!strncmp(q, "state ", 6)) snprintf(st, sizeof st, "%.*s", (int)(e2-q-6),  q+6);
                    else if (!strncmp(q, "steps ", 6)) snprintf(sp, sizeof sp, "%.*s", (int)(e2-q-6),  q+6);
                    else if (!strncmp(q, "wchan ", 6)) snprintf(wc, sizeof wc, "%.*s", (int)(e2-q-6),  q+6);
                    q = *e2 ? e2 + 1 : e2;
                }
                buf_printf(&b, "%-3d %-16s %-9s %-8s %s\n", i + 1, nm, st, sp, wc);
            }
            buf_free(&ps);
        }
        ok(out, "%d script(s)", s->nscripts);
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "log") == 0) {
        int n = 20;
        if (argc > 1) {
            Value v;
            if (nom_parse_number(argv[1], strlen(argv[1]), &v)) n = (int)val_int(v);
        }
        int start = s->nevents - n;
        if (start < 0) start = 0;
        Buf b;
        buf_init(&b);
        for (int i = start; i < s->nevents; i++)
            buf_printf(&b, "%6llu  %s\n", (unsigned long long)s->event[i].tick, s->event[i].text);
        ok(out, "%d event(s)", s->nevents - start);
        body(out, b.p ? b.p : "", b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "result") == 0) {
        Buf b;
        buf_init(&b);
        sim_result_json(s, &b);
        buf_putc(&b, '\n');
        ok(out, "result");
        body(out, b.p, b.len);
        endbody(out);
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "replay") == 0) {
        Buf b;
        buf_init(&b);
        sim_replay_json(s, &b);
        if (argc > 1) {
            extern bool nom_write_file(const char *path, const char *data, size_t len);
            if (!nom_write_file(argv[1], b.p, b.len)) { err(out, "%s: cannot write", argv[1]); buf_free(&b); return true; }
            ok(out, "wrote %zu bytes to %s (digest %016llx)", b.len, argv[1],
               (unsigned long long)sim_state_digest(s));
            endbody(out);
        } else {
            ok(out, "replay (%zu bytes, digest %016llx)", b.len,
               (unsigned long long)sim_state_digest(s));
            body(out, b.p, b.len);
            endbody(out);
        }
        buf_free(&b);
        return true;
    }

    if (strcmp(cmd, "save") == 0) {
        const char *dest = argc > 1 ? argv[1] : s->home;
        if (!dest || !dest[0]) { err(out, "no home directory known; usage: save <hostpath>"); return true; }
        if (!sim_save_home(s, dest)) { err(out, "%s: cannot write home", dest); return true; }
        ok(out, "saved /home to %s", dest);
        endbody(out);
        return true;
    }

    err(out, "unknown command '%s' (try 'help')", cmd);
    return true;
}
