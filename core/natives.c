/* natives.c — the entire outside world available to a NomScript program.
 *
 * This list IS the sandbox. There is no module system, no host escape and no
 * second channel to the ship: if it isn't here, a script cannot do it. Note
 * that nothing here reads a clock, opens a host file, or allocates a random
 * number outside the seeded Rng.
 */
#include "lang.h"
#include <string.h>
#include <stdio.h>

/* Deterministic decimal parsing. strtod's rounding is implementation-defined
 * at the edges; this is exact for the ranges the game produces. */
static const double POW10[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18
};

bool nom_parse_number(const char *s, size_t len, Value *out)
{
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    bool neg = false;
    if (i < len && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    if (i >= len) return false;

    int64_t whole = 0;
    int wdigits = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') { whole = whole * 10 + (s[i] - '0'); i++; wdigits++; }

    bool isfloat = false;
    double val = (double)whole;
    if (i < len && s[i] == '.') {
        isfloat = true;
        i++;
        int64_t frac = 0;
        int fdigits = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9' && fdigits < 18) {
            frac = frac * 10 + (s[i] - '0'); i++; fdigits++;
        }
        while (i < len && s[i] >= '0' && s[i] <= '9') i++;   /* excess digits ignored */
        val = (double)whole + (double)frac / POW10[fdigits];
        if (!wdigits && !fdigits) return false;
    }
    if (!wdigits && !isfloat) return false;

    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        bool eneg = false;
        if (i < len && (s[i] == '+' || s[i] == '-')) { eneg = s[i] == '-'; i++; }
        int e = 0;
        int ed = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') { e = e * 10 + (s[i] - '0'); i++; ed++; }
        if (!ed) return false;
        if (e > 18) e = 18;
        val = eneg ? val / POW10[e] : val * POW10[e];
        isfloat = true;
    }
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i != len) return false;

    if (isfloat) *out = VAL_NUM(neg ? -val : val);
    else         *out = VAL_INT(neg ? -whole : whole);
    return true;
}

/* --------------------------------------------------------------- helpers */
static bool want_str(VM *v, Value a, const char *fn, const char **s, size_t *len)
{
    if (!IS_STR(a)) { vm_runtime_error(v, "%s() expects a string", fn); return false; }
    *s = AS_STR(a)->s;
    *len = AS_STR(a)->len;
    return true;
}

static void path_of(Value a, char *out, size_t outsz)
{
    if (IS_STR(a)) snprintf(out, outsz, "%.*s", (int)AS_STR(a)->len, AS_STR(a)->s);
    else out[0] = 0;
}

/* --------------------------------------------------------------- file io */
static VmStatus n_read(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    if (!path[0]) { vm_runtime_error(v, "read() expects a path string"); return VM_ERROR; }

    Buf b;
    buf_init(&b);
    IoStatus st = vfs_read(v->fs, path, &b);
    if (st == IO_BLOCK) {
        buf_free(&b);
        snprintf(v->blocked_on, sizeof v->blocked_on, "%s", path);
        return VM_BLOCKED;
    }
    if (st == IO_ERR) {
        buf_free(&b);
        vm_runtime_error(v, "read: %s", v->fs->err);
        return VM_ERROR;
    }
    v->blocked_on[0] = 0;
    *out = str_new(b.p ? b.p : "", b.len);
    buf_free(&b);
    return VM_OK;
}

static VmStatus n_write(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    if (!path[0]) { vm_runtime_error(v, "write() expects a path string"); return VM_ERROR; }

    Buf b;
    buf_init(&b);
    val_tostr(&b, a[1]);
    IoStatus st = vfs_write(v->fs, path, b.p ? b.p : "", b.len);
    buf_free(&b);
    if (st == IO_BLOCK) return VM_BLOCKED;
    if (st == IO_ERR) { vm_runtime_error(v, "write: %s", v->fs->err); return VM_ERROR; }
    *out = VAL_NIL;
    return VM_OK;
}

static VmStatus n_ls(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    Buf b;
    buf_init(&b);
    if (vfs_list(v->fs, path, &b) != IO_OK) {
        buf_free(&b);
        vm_runtime_error(v, "ls: %s", v->fs->err);
        return VM_ERROR;
    }
    Value l = list_new();
    const char *p = b.p ? b.p : "";
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        size_t len = (size_t)(e - p);
        while (len && (p[len - 1] == '/' || p[len - 1] == '*')) len--;
        if (len) list_push(AS_LIST(l), str_new(p, len));
        p = *e ? e + 1 : e;
    }
    buf_free(&b);
    *out = l;
    return VM_OK;
}

static VmStatus n_exists(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    *out = VAL_BOOL(vfs_lookup(v->fs, path) != NULL);
    return VM_OK;
}

/* parse("key value\n...") -> dict, converting numeric values.
 * Every device status file in the game is in this shape, so a script's first
 * line is usually `st = parse(read("/dev/reactor/status"))`. */
static VmStatus n_parse(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    const char *s;
    size_t len;
    if (!want_str(v, a[0], "parse", &s, &len)) return VM_ERROR;

    Value d = dict_new();
    size_t i = 0;
    while (i < len) {
        size_t ls = i;
        while (i < len && s[i] != '\n') i++;
        size_t le = i;
        if (i < len) i++;
        while (le > ls && (s[le - 1] == '\r' || s[le - 1] == ' ')) le--;
        while (ls < le && (s[ls] == ' ' || s[ls] == '\t')) ls++;
        if (le == ls) continue;
        if (s[ls] == '#') continue;         /* a comment is not a setting */
        size_t k = ls;
        while (k < le && s[k] != ' ' && s[k] != '\t' &&
               s[k] != ':' && s[k] != '=') k++;
        size_t ke = k;                       /* key ends before the separator */
        while (k < le && (s[k] == ':' || s[k] == '=')) k++;
        size_t vs = k;
        while (vs < le && (s[vs] == ' ' || s[vs] == '\t')) vs++;
        Value key = str_new(s + ls, ke - ls);
        Value val;
        if (!nom_parse_number(s + vs, le - vs, &val)) val = str_new(s + vs, le - vs);
        dict_set(AS_DICT(d), key, val);
    }
    *out = d;
    return VM_OK;
}

/* get(path) -> a typed value: a number if it reads as one, else the string.
 * With field files this is the whole accessor:
 *     get("/dev/reactor/state")   ->  "idle"
 *     get("/dev/cpu/budget")      ->  2000
 * which replaces parse(read(".../status"))["budget"]. */
static VmStatus n_get(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    if (!path[0]) { vm_runtime_error(v, "get() expects a path string"); return VM_ERROR; }

    Buf b;
    buf_init(&b);
    IoStatus st = vfs_read(v->fs, path, &b);
    if (st == IO_BLOCK) {
        buf_free(&b);
        snprintf(v->blocked_on, sizeof v->blocked_on, "%s", path);
        return VM_BLOCKED;
    }
    if (st == IO_ERR) {
        buf_free(&b);
        vm_runtime_error(v, "get: %s", v->fs->err);
        return VM_ERROR;
    }
    v->blocked_on[0] = 0;

    /* trim, then take the typed form */
    const char *p = b.p ? b.p : "";
    size_t len = b.len;
    while (len && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' ')) len--;
    Value r;
    if (!nom_parse_number(p, len, &r)) r = str_new(p, len);
    *out = r;
    buf_free(&b);
    return VM_OK;
}

/* waitfor(path, value) — suspend until the file reads as `value`.
 *
 * This is the cheap way AND the readable way, which is the point: a polling
 * loop burns instructions every tick and heats the bay it runs in, while a
 * suspended script costs one instruction per tick. Good code runs cooler. */
static VmStatus n_waitfor(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    if (!path[0]) { vm_runtime_error(v, "waitfor() expects a path string"); return VM_ERROR; }

    Buf b;
    buf_init(&b);
    IoStatus st = vfs_read(v->fs, path, &b);
    if (st == IO_BLOCK) {
        buf_free(&b);
        snprintf(v->blocked_on, sizeof v->blocked_on, "%s", path);
        return VM_BLOCKED;
    }
    if (st == IO_ERR) {
        buf_free(&b);
        vm_runtime_error(v, "waitfor: %s", v->fs->err);
        return VM_ERROR;
    }

    const char *p = b.p ? b.p : "";
    size_t len = b.len;
    while (len && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' ')) len--;
    Value got;
    if (!nom_parse_number(p, len, &got)) got = str_new(p, len);
    buf_free(&b);

    if (!val_equal(got, a[1])) {
        val_release(got);
        snprintf(v->blocked_on, sizeof v->blocked_on, "%s", path);
        return VM_BLOCKED;          /* retried next tick, one instruction */
    }
    v->blocked_on[0] = 0;
    *out = got;
    return VM_OK;
}

/* watch(path) — suspend until the file's value CHANGES, then return the new
 * one. The fourth and last trigger primitive; together with read() (blocks
 * until there is an event), waitfor() (blocks until a state) and sleep()
 * (blocks until a time), it covers every way a script can be woken. There is
 * no callback system and no event syntax: a trigger is a blocking read. */
static VmStatus n_watch(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    if (!path[0]) { vm_runtime_error(v, "watch() expects a path string"); return VM_ERROR; }

    Buf b;
    buf_init(&b);
    IoStatus st = vfs_read(v->fs, path, &b);
    if (st == IO_BLOCK) {
        buf_free(&b);
        snprintf(v->blocked_on, sizeof v->blocked_on, "%s", path);
        return VM_BLOCKED;
    }
    if (st == IO_ERR) {
        buf_free(&b);
        vm_runtime_error(v, "watch: %s", v->fs->err);
        return VM_ERROR;
    }
    const char *p = b.p ? b.p : "";
    size_t len = b.len;
    while (len && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' ')) len--;

    if (!v->watching || strcmp(v->watch_path, path) != 0) {
        /* first look at this path: remember it and suspend */
        v->watching = true;
        snprintf(v->watch_path, sizeof v->watch_path, "%s", path);
        size_t keep = len < sizeof v->watch_last - 1 ? len : sizeof v->watch_last - 1;
        memcpy(v->watch_last, p, keep);
        v->watch_last[keep] = 0;
        buf_free(&b);
        snprintf(v->blocked_on, sizeof v->blocked_on, "%s", path);
        return VM_BLOCKED;
    }
    if (len == strlen(v->watch_last) && memcmp(p, v->watch_last, len) == 0) {
        buf_free(&b);
        snprintf(v->blocked_on, sizeof v->blocked_on, "%s", path);
        return VM_BLOCKED;          /* unchanged */
    }

    size_t keep = len < sizeof v->watch_last - 1 ? len : sizeof v->watch_last - 1;
    memcpy(v->watch_last, p, keep);
    v->watch_last[keep] = 0;
    v->watching = false;            /* re-arm on the next call */
    v->blocked_on[0] = 0;

    Value r;
    if (!nom_parse_number(p, len, &r)) r = str_new(p, len);
    *out = r;
    buf_free(&b);
    return VM_OK;
}

/* bind(target, path) — graft a file into your own namespace. A script that
 * only knows /dev/scrubber never needs to learn that it is really an alien
 * unit on a wreck: the namespace does the lying for you. */
static VmStatus n_bind(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    char tgt[NOM_PATH_MAX], path[NOM_PATH_MAX];
    path_of(a[0], tgt, sizeof tgt);
    path_of(a[1], path, sizeof path);
    if (!tgt[0] || !path[0]) { vm_runtime_error(v, "bind() expects two paths"); return VM_ERROR; }
    if (!vfs_bind(v->fs, tgt, path)) { vm_runtime_error(v, "bind: %s", v->fs->err); return VM_ERROR; }
    *out = VAL_NIL;
    return VM_OK;
}

/* mount(host, at) — bring someone else's file server into your namespace. */
static VmStatus n_mount(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    extern bool wreck_mount(Sim *, const char *, const char *, char *, size_t);
    char host[NOM_PATH_MAX], at[NOM_PATH_MAX];
    path_of(a[0], host, sizeof host);
    path_of(a[1], at, sizeof at);
    char e[NOM_ERR_MAX];
    if (v->mount_hook) {
        if (!v->mount_hook(v, host, at, e, sizeof e)) {
            vm_runtime_error(v, "mount: %s", e);
            return VM_ERROR;
        }
        *out = VAL_NIL;
        return VM_OK;
    }
    if (!wreck_mount(v->sim, host, at, e, sizeof e)) { vm_runtime_error(v, "mount: %s", e); return VM_ERROR; }
    *out = VAL_NIL;
    return VM_OK;
}

/* ------------------------------------------------------------- scheduling */
static VmStatus n_sleep(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (v->sleeping) {          /* resumed: the wait is over */
        v->sleeping = false;
        *out = VAL_NIL;
        return VM_OK;
    }
    int64_t ticks = val_int(a[0]);
    if (ticks < 1) ticks = 1;
    v->sleeping = true;
    v->wake_tick = v->sim->tick + (uint64_t)ticks;
    return VM_SLEEP;
}

static VmStatus n_tick(VM *v, Value *a, int n, Value *out)
{
    (void)a; (void)n;
    *out = VAL_INT((int64_t)v->sim->tick);
    return VM_OK;
}

static VmStatus n_print(VM *v, Value *a, int n, Value *out)
{
    Buf b;
    buf_init(&b);
    for (int i = 0; i < n; i++) {
        if (i) buf_putc(&b, ' ');
        val_tostr(&b, a[i]);
    }
    if (v->console) {
        buf_puts(v->console, b.p ? b.p : "");
        buf_putc(v->console, '\n');
    } else {
        sim_log(v->sim, "%s", b.p ? b.p : "");
    }
    buf_free(&b);
    *out = VAL_NIL;
    return VM_OK;
}

/* Start a service. Only a booting machine can do this. */
static VmStatus n_svc(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    *out = VAL_NIL;
    if (!v->svc_hook)      { vm_runtime_error(v, "svc: not available here"); return VM_ERROR; }
    const char *sp; size_t sl;
    if (!want_str(v, a[0], "svc", &sp, &sl)) return VM_ERROR;
    char path[NOM_PATH_MAX];
    path_of(a[0], path, sizeof path);
    char e[NOM_ERR_MAX];
    if (!v->svc_hook(v, path, e, sizeof e)) {
        vm_runtime_error(v, "svc: %s", e);
        return VM_ERROR;
    }
    return VM_OK;
}

/* String helpers the rc scripts genuinely need. Config parsing is most of
 * what a boot does, so these are not conveniences, they are the job. */
static VmStatus n_startswith(VM *v, Value *a, int n, Value *out)
{
    (void)v; (void)n;
    if (!IS_STR(a[0]) || !IS_STR(a[1])) { *out = VAL_BOOL(false); return VM_OK; }
    size_t pl = AS_STR(a[1])->len;
    *out = VAL_BOOL(AS_STR(a[0])->len >= pl &&
                    memcmp(AS_STR(a[0])->s, AS_STR(a[1])->s, pl) == 0);
    return VM_OK;
}

static VmStatus n_endswith(VM *v, Value *a, int n, Value *out)
{
    (void)v; (void)n;
    if (!IS_STR(a[0]) || !IS_STR(a[1])) { *out = VAL_BOOL(false); return VM_OK; }
    size_t sl = AS_STR(a[0])->len, pl = AS_STR(a[1])->len;
    *out = VAL_BOOL(sl >= pl &&
                    memcmp(AS_STR(a[0])->s + sl - pl, AS_STR(a[1])->s, pl) == 0);
    return VM_OK;
}

/* Run another script file. A real init hands off to a real rc script; this is
 * the native that makes that literally true rather than a description of it. */
static VmStatus n_exec(VM *v, Value *a, int n, Value *out)
{
    *out = VAL_NIL;
    if (!v->run_script) {
        vm_runtime_error(v, "exec: not available here");
        return VM_ERROR;
    }
    if (!IS_STR(a[0])) {
        vm_runtime_error(v, "exec: expected a path");
        return VM_ERROR;
    }
    if (v->depth > 6) {
        vm_runtime_error(v, "exec: too many nested execs");
        return VM_ERROR;
    }
    char xpath[NOM_PATH_MAX];
    path_of(a[0], xpath, sizeof xpath);
    if (!v->run_script(v, xpath)) {
        /* the child already reported itself on the console; stop the parent */
        vm_runtime_error(v, "exec: %s failed", xpath);
        return VM_ERROR;
    }
    return VM_OK;
}

/* Stop the machine, the way a real init does when it cannot continue. */
static VmStatus n_panic(VM *v, Value *a, int n, Value *out)
{
    *out = VAL_NIL;
    Buf b; buf_init(&b);
    for (int i = 0; i < n; i++) { if (i) buf_putc(&b, ' '); val_tostr(&b, a[i]); }
    vm_runtime_error(v, "%s", b.p ? b.p : "panic");
    buf_free(&b);
    return VM_ERROR;
}

/* ------------------------------------------------------------- conversion */
static VmStatus n_str(VM *v, Value *a, int n, Value *out)
{
    (void)v; (void)n;
    Buf b;
    buf_init(&b);
    val_tostr(&b, a[0]);
    *out = str_new(b.p ? b.p : "", b.len);
    buf_free(&b);
    return VM_OK;
}

static VmStatus n_int(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (IS_STR(a[0])) {
        Value r;
        if (!nom_parse_number(AS_STR(a[0])->s, AS_STR(a[0])->len, &r)) {
            vm_runtime_error(v, "int(): '%.*s' is not a number",
                             (int)AS_STR(a[0])->len, AS_STR(a[0])->s);
            return VM_ERROR;
        }
        *out = VAL_INT(val_int(r));
        return VM_OK;
    }
    *out = VAL_INT(val_int(a[0]));
    return VM_OK;
}

static VmStatus n_num(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (IS_STR(a[0])) {
        Value r;
        if (!nom_parse_number(AS_STR(a[0])->s, AS_STR(a[0])->len, &r)) {
            vm_runtime_error(v, "num(): '%.*s' is not a number",
                             (int)AS_STR(a[0])->len, AS_STR(a[0])->s);
            return VM_ERROR;
        }
        *out = VAL_NUM(val_num(r));
        return VM_OK;
    }
    *out = VAL_NUM(val_num(a[0]));
    return VM_OK;
}

static VmStatus n_len(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (IS_STR(a[0]))  { *out = VAL_INT(AS_STR(a[0])->len);  return VM_OK; }
    if (IS_LIST(a[0])) { *out = VAL_INT(AS_LIST(a[0])->len); return VM_OK; }
    if (IS_DICT(a[0])) { *out = VAL_INT(AS_DICT(a[0])->len); return VM_OK; }
    vm_runtime_error(v, "len() needs a string, list or dict");
    return VM_ERROR;
}

/* ------------------------------------------------------------------ math */
static VmStatus n_abs(VM *v, Value *a, int n, Value *out)
{
    (void)v; (void)n;
    if (a[0].k == V_INT) { int64_t x = a[0].as.i; *out = VAL_INT(x < 0 ? -x : x); }
    else                 *out = VAL_NUM(nom_fabs(val_num(a[0])));
    return VM_OK;
}

static VmStatus n_min(VM *v, Value *a, int n, Value *out)
{
    (void)v;
    Value best = a[0];
    for (int i = 1; i < n; i++) if (val_num(a[i]) < val_num(best)) best = a[i];
    *out = val_retain(best);
    return VM_OK;
}

static VmStatus n_max(VM *v, Value *a, int n, Value *out)
{
    (void)v;
    Value best = a[0];
    for (int i = 1; i < n; i++) if (val_num(a[i]) > val_num(best)) best = a[i];
    *out = val_retain(best);
    return VM_OK;
}

static VmStatus n_round(VM *v, Value *a, int n, Value *out)
{
    (void)v; (void)n;
    double x = val_num(a[0]);
    *out = VAL_INT((int64_t)(x < 0 ? -nom_floor(-x + 0.5) : nom_floor(x + 0.5)));
    return VM_OK;
}

static VmStatus n_sqrt(VM *v, Value *a, int n, Value *out)  { (void)v; (void)n; *out = VAL_NUM(nom_sqrt(val_num(a[0]))); return VM_OK; }
static VmStatus n_sin(VM *v, Value *a, int n, Value *out)   { (void)v; (void)n; *out = VAL_NUM(nom_sin(val_num(a[0])));  return VM_OK; }
static VmStatus n_cos(VM *v, Value *a, int n, Value *out)   { (void)v; (void)n; *out = VAL_NUM(nom_cos(val_num(a[0])));  return VM_OK; }
static VmStatus n_atan2(VM *v, Value *a, int n, Value *out) { (void)v; (void)n; *out = VAL_NUM(nom_atan2(val_num(a[0]), val_num(a[1]))); return VM_OK; }

static VmStatus n_range(VM *v, Value *a, int n, Value *out)
{
    int64_t lo = 0, hi, step = 1;
    if (n == 1) hi = val_int(a[0]);
    else { lo = val_int(a[0]); hi = val_int(a[1]); if (n == 3) step = val_int(a[2]); }
    if (step == 0) { vm_runtime_error(v, "range() step must not be zero"); return VM_ERROR; }
    Value l = list_new();
    int count = 0;
    for (int64_t i = lo; step > 0 ? i < hi : i > hi; i += step) {
        if (++count > NOM_LIST_MAX) { vm_runtime_error(v, "range() longer than LIST_MAX=%d", NOM_LIST_MAX); val_release(l); return VM_ERROR; }
        list_push(AS_LIST(l), VAL_INT(i));
    }
    *out = l;
    return VM_OK;
}

/* --------------------------------------------------------------- strings */
static VmStatus n_split(VM *v, Value *a, int n, Value *out)
{
    const char *s;
    size_t len;
    if (!want_str(v, a[0], "split", &s, &len)) return VM_ERROR;
    const char *sep = " ";
    size_t seplen = 1;
    bool whitespace = (n < 2);
    if (n >= 2) { if (!want_str(v, a[1], "split", &sep, &seplen)) return VM_ERROR; }
    if (seplen == 0) { vm_runtime_error(v, "split() separator must not be empty"); return VM_ERROR; }

    Value l = list_new();
    size_t i = 0, start = 0;
    if (whitespace) {
        while (i <= len) {
            bool ws = (i == len) || s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r';
            if (ws) {
                if (i > start) list_push(AS_LIST(l), str_new(s + start, i - start));
                start = i + 1;
            }
            i++;
        }
    } else {
        while (i + seplen <= len) {
            if (memcmp(s + i, sep, seplen) == 0) {
                list_push(AS_LIST(l), str_new(s + start, i - start));
                i += seplen;
                start = i;
            } else i++;
        }
        list_push(AS_LIST(l), str_new(s + start, len - start));
    }
    *out = l;
    return VM_OK;
}

static VmStatus n_strip(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    const char *s;
    size_t len;
    if (!want_str(v, a[0], "strip", &s, &len)) return VM_ERROR;
    size_t b = 0, e = len;
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) b++;
    while (e > b && (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\n' || s[e-1] == '\r')) e--;
    *out = str_new(s + b, e - b);
    return VM_OK;
}

static VmStatus n_join(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (!IS_LIST(a[0])) { vm_runtime_error(v, "join() expects a list"); return VM_ERROR; }
    const char *sep = "";
    size_t seplen = 0;
    if (n >= 2 && !want_str(v, a[1], "join", &sep, &seplen)) return VM_ERROR;
    List *l = AS_LIST(a[0]);
    Buf b;
    buf_init(&b);
    for (uint32_t i = 0; i < l->len; i++) {
        if (i) buf_put(&b, sep, seplen);
        val_tostr(&b, l->v[i]);
    }
    *out = str_new(b.p ? b.p : "", b.len);
    buf_free(&b);
    return VM_OK;
}

/* ------------------------------------------------------ list / dict verbs */
static VmStatus n_append(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (!IS_LIST(a[0])) { vm_runtime_error(v, "append() expects a list"); return VM_ERROR; }
    if (AS_LIST(a[0])->len >= NOM_LIST_MAX) { vm_runtime_error(v, "list longer than LIST_MAX=%d", NOM_LIST_MAX); return VM_ERROR; }
    list_push(AS_LIST(a[0]), val_retain(a[1]));
    *out = VAL_NIL;
    return VM_OK;
}

static VmStatus n_keys(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (!IS_DICT(a[0])) { vm_runtime_error(v, "keys() expects a dict"); return VM_ERROR; }
    Dict *d = AS_DICT(a[0]);
    Value l = list_new();
    for (uint32_t i = 0; i < d->len; i++) list_push(AS_LIST(l), val_retain(d->k[i]));
    *out = l;
    return VM_OK;
}

static VmStatus n_lookup(VM *v, Value *a, int n, Value *out)
{
    if (!IS_DICT(a[0])) { vm_runtime_error(v, "lookup() expects a dict"); return VM_ERROR; }
    Value got;
    if (dict_get(AS_DICT(a[0]), a[1], &got)) *out = val_retain(got);
    else *out = (n >= 3) ? val_retain(a[2]) : VAL_NIL;
    return VM_OK;
}

static VmStatus n_has(VM *v, Value *a, int n, Value *out)
{
    (void)n;
    if (!IS_DICT(a[0])) { vm_runtime_error(v, "has() expects a dict"); return VM_ERROR; }
    Value dummy;
    *out = VAL_BOOL(dict_get(AS_DICT(a[0]), a[1], &dummy));
    return VM_OK;
}

/* ----------------------------------------------------------------- table */
static const Native NATIVES[] = {
    { "read",   1, 1, n_read   },
    { "get",    1, 1, n_get    },
    { "waitfor",2, 2, n_waitfor},
    { "watch",  1, 1, n_watch  },
    { "bind",   2, 2, n_bind   },
    { "mount",  2, 2, n_mount  },
    { "write",  2, 2, n_write  },
    { "ls",     1, 1, n_ls     },
    { "exists", 1, 1, n_exists },
    { "parse",  1, 1, n_parse  },
    { "print",  0, NOM_ARGS_MAX, n_print },
    { "sleep",  1, 1, n_sleep  },
    { "tick",   0, 0, n_tick   },
    { "str",    1, 1, n_str    },
    { "int",    1, 1, n_int    },
    { "num",    1, 1, n_num    },
    { "len",    1, 1, n_len    },
    { "abs",    1, 1, n_abs    },
    { "min",    1, NOM_ARGS_MAX, n_min },
    { "max",    1, NOM_ARGS_MAX, n_max },
    { "round",  1, 1, n_round  },
    { "sqrt",   1, 1, n_sqrt   },
    { "sin",    1, 1, n_sin    },
    { "cos",    1, 1, n_cos    },
    { "atan2",  2, 2, n_atan2  },
    { "range",  1, 3, n_range  },
    { "split",  1, 2, n_split  },
    { "strip",  1, 1, n_strip  },
    { "join",   1, 2, n_join   },
    { "append", 2, 2, n_append },
    { "keys",   1, 1, n_keys   },
    { "lookup", 2, 3, n_lookup },
    { "has",    2, 2, n_has    },
    { "exec",   1, 1, n_exec   },
    { "svc",    1, 1, n_svc    },
    { "startswith", 2, 2, n_startswith },
    { "endswith",   2, 2, n_endswith   },
    { "panic",  1, 1, n_panic  },
};

const Native *native_table(int *count)
{
    *count = (int)(sizeof NATIVES / sizeof NATIVES[0]);
    return NATIVES;
}

int native_find(const char *name, int len)
{
    int count = (int)(sizeof NATIVES / sizeof NATIVES[0]);
    for (int i = 0; i < count; i++)
        if ((int)strlen(NATIVES[i].name) == len && memcmp(NATIVES[i].name, name, (size_t)len) == 0)
            return i;
    return -1;
}
