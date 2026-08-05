#define _GNU_SOURCE 1
/* nominal_gdext.c — the Godot binding.
 *
 * Plain C against gdextension_interface.h, which the engine binary in this
 * repo produced itself (--dump-gdextension-interface). Zero fetched
 * dependencies, and it cannot drift from the engine version. See D2.
 *
 * This file is deliberately thin. It owns no game logic: it exposes the same
 * Sim and the same shell_exec() the socket uses, so the desktop and a telnet
 * session are two front ends onto one implementation and cannot disagree.
 */
#include "gdextension_interface.h"

/* The dumped interface header does not define the export macro that Godot's
 * own build supplies; declare it ourselves. */
#ifndef GDE_EXPORT
#  if defined(_WIN32)
#    define GDE_EXPORT __declspec(dllexport)
#  else
#    define GDE_EXPORT __attribute__((visibility("default")))
#  endif
#endif
#include "nom.h"
#include "cpu.h"
#include "machine.h"
#include "kernel.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static GDExtensionInterfaceGetProcAddress api_get;
static GDExtensionClassLibraryPtr         api_lib;

/* the handful of engine entry points we need */
static GDExtensionInterfaceClassdbRegisterExtensionClass6      classdb_register;
static GDExtensionInterfaceClassdbRegisterExtensionClassMethod classdb_add_method;
static GDExtensionInterfaceStringNewWithUtf8Chars              string_new_utf8;
static GDExtensionInterfaceStringToUtf8Chars                   string_to_utf8;
static GDExtensionInterfaceVariantGetPtrConstructor            variant_get_ctor;
static GDExtensionInterfaceVariantGetPtrDestructor             variant_get_dtor;
static GDExtensionInterfaceMemAlloc                            mem_alloc;
static GDExtensionInterfaceMemFree                             mem_free;
static GDExtensionInterfaceObjectSetInstance                   object_set_instance;
static GDExtensionInterfaceObjectSetInstanceBinding            object_set_binding;
static GDExtensionInterfaceClassdbConstructObject3             classdb_construct;
static GDExtensionInterfaceStringNameNewWithUtf8Chars          stringname_new;
static GDExtensionPtrConstructor string_from_gdstring;
static GDExtensionPtrDestructor  string_destroy;
static GDExtensionPtrDestructor  stringname_destroy;

#define GETPROC(var, name) \
    var = (void *)api_get(name)

/* ------------------------------------------------------------ string glue */
/* A StringName that we own for the lifetime of the library. */
typedef struct { uint8_t opaque[8]; } SN;

static void sn_make(SN *sn, const char *s)
{
    memset(sn, 0, sizeof *sn);
    stringname_new(sn, s);
}

/* Copy a Godot String argument into a C buffer. */
static void gdstring_to_c(const void *gdstr, char *out, size_t outsz)
{
    GDExtensionInt need = string_to_utf8((GDExtensionConstStringPtr)gdstr, NULL, 0);
    if (need < 0) need = 0;
    if ((size_t)need >= outsz) need = (GDExtensionInt)(outsz - 1);
    string_to_utf8((GDExtensionConstStringPtr)gdstr, out, need);
    out[need] = 0;
}

static void c_to_gdstring(void *dest, const char *s)
{
    string_new_utf8((GDExtensionUninitializedStringPtr)dest, s ? s : "");
}

/* ------------------------------------------------------------- the class */
/* THE MODEL, LOADED ONCE FOR THE WHOLE GAME.
 *
 * The weights ship as game data. Loading is best-effort and silent: if the
 * file is missing, or the build has no llama in it, llm_available() stays
 * false and customer.c falls back to the scripted persona -- which is the
 * D20 contract, "the player never sees a hang".
 *
 * The path is relative to where Godot was started, which for a played build
 * is the project directory. NOM_MODEL overrides it. */
#ifdef NOM_LLM
#include <dlfcn.h>
bool llm_load(const char *path);
const char *llm_why(void);

/* FIND THE WEIGHTS RELATIVE TO THIS LIBRARY, not to the working directory.
 *
 * cwd-relative paths worked from a terminal and not from the editor, because
 * Godot's idea of the current directory depends on how it was started -- so
 * the model silently failed to load in the one case that matters, and the
 * customer answered every question with the scripted "I'm not sure what
 * you're asking me". A shipped game finds its data next to itself. */
static void nominal_load_model(void)
{
    static bool tried = false;
    if (tried) return;
    tried = true;

    const char *env = getenv("NOM_MODEL");
    if (env && *env) { llm_load(env); return; }

    static const char *NAMES[] = {
        "Qwen2.5-3B-Instruct-Q4_K_M.gguf",
        "Qwen2.5-1.5B-Instruct-Q4_K_M.gguf",
        "Qwen2.5-0.5B-Instruct-Q4_K_M.gguf",
        NULL
    };

    /* Where is this .so? game/bin/libnominal... -> game/models/... */
    char base[1024] = "";
    Dl_info info;
    if (dladdr((void *)(uintptr_t)&nominal_load_model, &info) && info.dli_fname) {
        snprintf(base, sizeof base, "%s", info.dli_fname);
        char *slash = strrchr(base, '/');
        if (slash) *slash = '\0';            /* .../game/bin */
        slash = strrchr(base, '/');
        if (slash) *slash = '\0';            /* .../game     */
    }

    char path[1200];
    for (int i = 0; NAMES[i]; i++) {
        if (base[0]) {
            snprintf(path, sizeof path, "%s/models/%s", base, NAMES[i]);
            if (llm_load(path)) {
                fprintf(stderr, "nominal: model %s\n", path);
                return;
            }
        }
        snprintf(path, sizeof path, "models/%s", NAMES[i]);
        if (llm_load(path)) { fprintf(stderr, "nominal: model %s\n", path); return; }
        snprintf(path, sizeof path, "game/models/%s", NAMES[i]);
        if (llm_load(path)) { fprintf(stderr, "nominal: model %s\n", path); return; }
    }
    fprintf(stderr, "nominal: NO MODEL (%s) -- scripted persona will answer\n",
            llm_why ? llm_why() : "?");
}

#else
static void nominal_load_model(void) { }
#endif

/* One Godot object == one customer machine: a disk, a package database and a
 * boot chain that really runs the programs on that disk. */
typedef struct {
    Machine m;          /* THE CUSTOMER'S machine: broken, reached by rcon  */
    /* YOUR WORKSTATION. A healthy install of the same system that is never
     * corrupted, so "compare it against mine" is a real move. It is also
     * where rcon runs from, because a service processor is reached over the
     * network from somewhere -- and that somewhere is a computer too. */
    Machine desk;
    bool    installed;
    bool    desk_up;
    Buf     scratch;
} Station;

static SN sn_class, sn_parent;

static GDExtensionObjectPtr station_create(void *userdata,
                                           GDExtensionBool notify_postinitialize)
{
    (void)userdata; (void)notify_postinitialize;
    GDExtensionObjectPtr obj = classdb_construct(&sn_parent);
    Station *st = nom_alloc(sizeof(Station));
    memset(st, 0, sizeof *st);
    machine_install(&st->m, 1);
    st->installed = true;
    buf_init(&st->scratch);
    object_set_instance(obj, &sn_class, st);
    return obj;
}

static void station_free(void *userdata, GDExtensionClassInstancePtr instance)
{
    (void)userdata;
    Station *st = (Station *)instance;
    if (!st) return;
    if (st->installed) machine_free(&st->m);
    buf_free(&st->scratch);
    nom_free(st);
}

/* ------------------------------------------------------------- the methods
 *
 * The whole surface the front end needs, and nothing that bypasses the
 * machine: the desktop cannot learn anything a player at a console could not.
 */

typedef void (*Bound)(Station *st, const GDExtensionConstTypePtr *args, void *ret);

static void ensure_desk(Station *st, uint64_t seed);

static void reset_to(Station *st, uint64_t seed)
{
    if (st->installed) machine_free(&st->m);
    machine_install(&st->m, seed);
    st->installed = true;
    ensure_desk(st, seed);
}

/* The workstation, brought up once and re-pointed at each new ticket. */
static void ensure_desk(Station *st, uint64_t seed)
{
    if (!st->desk_up) {
        machine_install(&st->desk, 1);
        machine_boot(&st->desk);
        st->desk_up = true;
    }
    st->desk.peer = &st->m;
    snprintf(st->desk.peer_addr, sizeof st->desk.peer_addr,
             "10.0.2.%d", 60 + (int)(seed % 40));
}

/* install(int seed) -> String — a healthy machine. Returns its id. */
static void m_install(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    reset_to(st, (uint64_t)(*(const int64_t *)args[0]));
    machine_boot(&st->m);
    c_to_gdstring(ret, st->m.id);
}

/* take_ticket(int seed, int faults) -> String — a BROKEN machine, guaranteed
 * not to boot, generated by corrupting a healthy one at random until it
 * stops. Returns its id. */
static void m_take_ticket(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    uint64_t seed = (uint64_t)(*(const int64_t *)args[0]);
    int faults = (int)(*(const int64_t *)args[1]);
    if (st->installed) machine_free(&st->m);
    machine_install(&st->m, seed);
    st->installed = true;
    char what[512];
    machine_break(&st->m, seed, faults < 1 ? 1 : faults, what, sizeof what);
    /* AND YOUR OWN MACHINE. Without this the workstation was a zeroed struct
     * and the first command typed into your own terminal ran a kernel on it,
     * which crashes Godot outright -- no GDScript error, just a native
     * backtrace, because the fault is three layers below the script. */
    ensure_desk(st, seed);
    c_to_gdstring(ret, st->m.id);
}

/* boot() -> String — run the boot chain and return everything it printed.
 * Pure function of what is on the disk right now, so the front end can call
 * it as often as it likes. */
static void m_boot(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    st->m.on_rescue = false;
    st->m.nmount = 0;
    machine_boot(&st->m);
    c_to_gdstring(ret, st->m.boot.console.p ? st->m.boot.console.p : "");
}

static void m_booted(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    *(GDExtensionBool *)ret = st->m.boot.running ? 1 : 0;
}

static void m_boot_stage(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    c_to_gdstring(ret, boot_stage_name(st->m.boot.failed_at));
}

static void m_boot_reason(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    c_to_gdstring(ret, st->m.boot.reason);
}

/* read_file(String path) -> String */
static void m_read_file(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    Buf out; buf_init(&out);
    bool dangling = false;
    VNode *n = vfs_resolve(&st->m.disk, path, &dangling);
    if (n && n->kind == VN_FILE) buf_put(&out, n->data.p, n->data.len);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* write_file(String path, String data) -> bool — editing a file is the
 * player's most basic repair, so it has to be here. */
static void m_write_file(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    char *data = nom_alloc(1 << 16);
    gdstring_to_c(args[1], data, 1 << 16);
    VNode *n = vfs_lookup(&st->m.disk, path);
    if (!n) n = vfs_mkfile(&st->m.disk, path, "");
    bool ok = (n && n->kind == VN_FILE);
    if (ok) { buf_clear(&n->data); buf_puts(&n->data, data); }
    nom_free(data);
    *(GDExtensionBool *)ret = ok ? 1 : 0;
}

/* list_dir(String path) -> String — one "name kind mode size" line per entry */
static void m_list_dir(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    Buf out; buf_init(&out);
    VNode *d = vfs_resolve(&st->m.disk, path, NULL);
    if (d && d->kind == VN_DIR) {
        for (VNode *k = d->child; k; k = k->next) {
            const char *kind = k->kind == VN_DIR ? "dir"
                             : k->kind == VN_LINK ? "link"
                             : k->kind == VN_DEV ? "dev" : "file";
            buf_printf(&out, "%s %s %04o %d\n", k->name, kind, k->mode,
                       (int)k->data.len);
        }
    }
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* packages() -> String — "name version description" per line */
static void m_packages(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    Buf out; buf_init(&out);
    for (int i = 0; i < st->m.npkg; i++)
        buf_printf(&out, "%s %s %s\n", st->m.pkg[i]->name, st->m.pkg[i]->version,
                   st->m.pkg[i]->desc);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* verify(String pkg_or_empty) -> String */
static void m_verify(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char pkg[64];
    gdstring_to_c(args[0], pkg, sizeof pkg);
    Buf out; buf_init(&out);
    pkg_verify(&st->m, pkg[0] ? pkg : NULL, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* reinstall(String pkg) -> String */
static void m_reinstall(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char pkg[64];
    gdstring_to_c(args[0], pkg, sizeof pkg);
    Buf out; buf_init(&out);
    pkg_reinstall(&st->m, pkg, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* owns(String path) -> String — which package would I be reinstalling */
static void m_owns(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    const Package *p = pkg_owns(&st->m, path);
    c_to_gdstring(ret, p ? p->name : "");
}

/* run(String path, String arg) -> String — execute a program on the machine
 * and return what it printed. This is the rescue shell's whole job. */
static void m_run(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX], arg[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    gdstring_to_c(args[1], arg, sizeof arg);
    Buf out; buf_init(&out);
    char err[NOM_ERR_MAX] = "";
    kernel_spawn(&st->m, path, arg, &out, 0, err, sizeof err);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* boot_rescue() -> String — boot the live medium. It is never corrupted, so
 * this is the button that always works, and the reason a player is never
 * stuck with a machine they cannot get a shell on. */
static void m_boot_rescue(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    machine_boot_rescue(&st->m);
    c_to_gdstring(ret, st->m.boot.console.p ? st->m.boot.console.p : "");
}

/* on_rescue() -> bool — which medium are we on */
static void m_on_rescue(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    *(GDExtensionBool *)ret = st->m.on_rescue ? 1 : 0;
}

/* sh(String line) -> String — run one command line on the machine, through
 * the same kernel_run() the socket uses. The desktop therefore cannot do
 * anything a remote player cannot, which is the point. */
/* sh_on(int which, String line) -> String
 *
 * which 0 = YOUR workstation, 1 = the customer's machine.
 *
 * A terminal window is bound to one machine or the other, which is what makes
 * `rcon connect` able to open a NEW terminal on their box while your own
 * shell stays alive beside it -- the thing a support engineer actually does. */
static void m_sh_on(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int64_t which = *(const int64_t *)args[0];
    char line[2048];
    gdstring_to_c(args[1], line, sizeof line);
    Buf out; buf_init(&out);
    kernel_run(which ? &st->m : &st->desk, line, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* healthy() -> bool
 *
 * The customer's machine is up AND everything that should be running is.
 * The desktop needs this to know when a ticket is actually finished --
 * David: "After you fix the computer it should be FAR more apparent." */
static void m_healthy(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    bool ok = false;
    /* A RESCUE MEDIUM THAT BOOTS IS NOT A REPAIRED MACHINE.
     *
     * This asked two questions -- is it running, and is everything healthy --
     * and the rescue image answers yes to both, because the rescue image is a
     * complete working system that was never broken. So the instant a
     * playtester ran `rcon media insert; rcon boot media; rcon power cycle`,
     * the ticket closed itself and the customer said "it is working again,
     * everything is where it was" -- with the disk still corrupt and not even
     * mounted. Thirty seconds, any seed, no diagnosis. It made the optimal
     * play "skip the game".
     *
     * The job is not "get a login prompt in front of the customer". It is
     * "get THEIR system, on THEIR disk, running again". So the third question
     * is which medium is under it, and it is the one that was missing. */
    if (st->installed && st->m.boot.running && !st->m.on_rescue) {
        Buf sick; buf_init(&sick);
        ok = kernel_health(&st->m, &sick) == 0;
        buf_free(&sick);
    }
    *(GDExtensionBool *)ret = ok;
}


/* de_requests() -> String
 *
 * Everything written to /run/nomde/requests since the last call, and it
 * clears the file. This is the display server's socket being drained by the
 * thing that draws the windows -- which is what makes `open g2048` in a
 * terminal actually open a window, and what makes a broken graphical stack
 * debuggable: no nomde, no /run, or a damaged .desktop, and nothing happens
 * for a reason you can find. */
static void m_de_requests(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    /* The workstation is created with the ticket. Asking before that walks a
     * zeroed Machine and takes Godot down with a native backtrace and no
     * script error at all -- the second time I have made exactly this
     * mistake, so both new methods check. */
    if (!st->desk_up) { c_to_gdstring(ret, ""); return; }
    VNode *n = vfs_lookup(&st->desk.disk, "/run/nomde/requests");
    if (!n || n->kind != VN_FILE || !n->data.len) { c_to_gdstring(ret, ""); return; }
    Buf b; buf_init(&b);
    buf_put(&b, n->data.p, n->data.len);
    buf_clear(&n->data);
    c_to_gdstring(ret, b.p ? b.p : "");
    buf_free(&b);
}

/* de_apps() -> String: the .desktop registry, one "key\tName\tIcon" per line.
 * The desktop does not know what applications exist; it reads this. */
static void m_de_apps(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!st->desk_up) { c_to_gdstring(ret, ""); return; }
    Buf b; buf_init(&b);
    VNode *d = vfs_lookup(&st->desk.disk, "/usr/share/applications");
    for (VNode *k = d ? d->child : NULL; k; k = k->next) {
        if (k->kind != VN_FILE) continue;
        size_t nl = strlen(k->name);
        if (nl < 9 || strcmp(k->name + nl - 8, ".desktop") != 0) continue;
        char key[64];
        size_t kl = nl - 8;
        if (kl >= sizeof key) kl = sizeof key - 1;
        memcpy(key, k->name, kl);
        key[kl] = 0;
        /* Name= and Icon= out of the entry itself. */
        char nm[64] = "", ic[32] = "";
        const char *p = k->data.p ? k->data.p : "";
        for (const char *q = p; q && *q; ) {
            const char *e = strchr(q, '\n');
            size_t len = e ? (size_t)(e - q) : strlen(q);
            if (len > 5 && strncmp(q, "Name=", 5) == 0) {
                size_t c2 = len - 5; if (c2 >= sizeof nm) c2 = sizeof nm - 1;
                memcpy(nm, q + 5, c2); nm[c2] = 0;
            } else if (len > 5 && strncmp(q, "Icon=", 5) == 0) {
                size_t c2 = len - 5; if (c2 >= sizeof ic) c2 = sizeof ic - 1;
                memcpy(ic, q + 5, c2); ic[c2] = 0;
            }
            q = e ? e + 1 : NULL;
        }
        if (!nm[0]) continue;             /* a damaged entry has no name */
        buf_printf(&b, "%s\t%s\t%s\n", key, nm, ic);
    }
    c_to_gdstring(ret, b.p ? b.p : "");
    buf_free(&b);
}

/* peer_addr() -> String: what the customer reads off the sticker. */
static void m_peer_addr(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    c_to_gdstring(ret, st->desk.peer_addr);
}

static void m_sh(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char line[2048];
    gdstring_to_c(args[0], line, sizeof line);
    Buf out; buf_init(&out);
    kernel_run(&st->m, line, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* ask(String question) -> String
 *
 * The customer, on the phone. This was reachable from the TCP bench and not
 * from the desktop at all, so the entire language-model side of the game --
 * the persona, the memory of the call, the fact that they can tell you what
 * is on their screen -- was invisible to anyone playing through the front
 * end. A view that cannot see half the game is not a view of the game. */
static void m_ask(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    /* LAZILY, AND NOT ON THE WAY IN.
     *
     * Loading 1.8 GB of weights at construction blocked the whole desktop
     * before it drew a single frame -- over three minutes on a busy machine,
     * for a screen with nothing on it. Nobody needs the customer until they
     * open the chat, and the chat already calls this from a worker thread, so
     * loading here costs the first message and nothing else. */
    nominal_load_model();
    char q[1024];
    gdstring_to_c(args[0], q, sizeof q);
    Buf out; buf_init(&out);
    customer_ask(&st->m, q, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* colleague(String who, String question) -> String
 * customer_name() -> String
 *
 * The other two people in the chat window, and the name of the first. */
static void m_colleague(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    nominal_load_model();
    char who[32], q[1024];
    gdstring_to_c(args[0], who, sizeof who);
    gdstring_to_c(args[1], q, sizeof q);
    Buf out; buf_init(&out);
    colleague_ask(&st->m, who, q, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

static void m_customer_name(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    c_to_gdstring(ret, customer_name(&st->m));
}

/* chmod(String path, int mode) -> bool */
static void m_chmod(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    int64_t mode = *(const int64_t *)args[1];
    VNode *n = vfs_lookup(&st->m.disk, path);
    if (n) n->mode = (unsigned)(mode & 0777);
    *(GDExtensionBool *)ret = n ? 1 : 0;
}

/* ------------------------------------------------------- method plumbing */
typedef struct {
    const char *name;
    Bound       fn;
    int         nargs;
    GDExtensionVariantType argtype[2];
    GDExtensionVariantType rettype;   /* GDEXTENSION_VARIANT_TYPE_NIL == void */
} MethodDef;

static const MethodDef METHODS[] = {
    { "install",     m_install,     1, { GDEXTENSION_VARIANT_TYPE_INT },       GDEXTENSION_VARIANT_TYPE_STRING },
    { "take_ticket", m_take_ticket, 2, { GDEXTENSION_VARIANT_TYPE_INT, GDEXTENSION_VARIANT_TYPE_INT },   GDEXTENSION_VARIANT_TYPE_STRING },
    { "boot",        m_boot,        0, { 0 },        GDEXTENSION_VARIANT_TYPE_STRING },
    { "booted",      m_booted,      0, { 0 },        GDEXTENSION_VARIANT_TYPE_BOOL },
    { "boot_stage",  m_boot_stage,  0, { 0 },        GDEXTENSION_VARIANT_TYPE_STRING },
    { "boot_reason", m_boot_reason, 0, { 0 },        GDEXTENSION_VARIANT_TYPE_STRING },
    { "read_file",   m_read_file,   1, { GDEXTENSION_VARIANT_TYPE_STRING },       GDEXTENSION_VARIANT_TYPE_STRING },
    { "write_file",  m_write_file,  2, { GDEXTENSION_VARIANT_TYPE_STRING, GDEXTENSION_VARIANT_TYPE_STRING },   GDEXTENSION_VARIANT_TYPE_BOOL },
    { "list_dir",    m_list_dir,    1, { GDEXTENSION_VARIANT_TYPE_STRING },       GDEXTENSION_VARIANT_TYPE_STRING },
    { "packages",    m_packages,    0, { 0 },        GDEXTENSION_VARIANT_TYPE_STRING },
    { "verify",      m_verify,      1, { GDEXTENSION_VARIANT_TYPE_STRING },       GDEXTENSION_VARIANT_TYPE_STRING },
    { "reinstall",   m_reinstall,   1, { GDEXTENSION_VARIANT_TYPE_STRING },       GDEXTENSION_VARIANT_TYPE_STRING },
    { "owns",        m_owns,        1, { GDEXTENSION_VARIANT_TYPE_STRING },       GDEXTENSION_VARIANT_TYPE_STRING },
    { "run",         m_run,         2, { GDEXTENSION_VARIANT_TYPE_STRING, GDEXTENSION_VARIANT_TYPE_STRING },   GDEXTENSION_VARIANT_TYPE_STRING },
    { "chmod",       m_chmod,       2, { GDEXTENSION_VARIANT_TYPE_STRING, GDEXTENSION_VARIANT_TYPE_INT },   GDEXTENSION_VARIANT_TYPE_BOOL },
    { "boot_rescue", m_boot_rescue, 0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "on_rescue",   m_on_rescue,   0, { 0 },                              GDEXTENSION_VARIANT_TYPE_BOOL },
    { "sh",          m_sh,          1, { GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "ask",         m_ask,         1, { GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "sh_on",       m_sh_on,       2, { GDEXTENSION_VARIANT_TYPE_INT, GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "peer_addr",   m_peer_addr,   0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "healthy",     m_healthy,     0, { 0 },                              GDEXTENSION_VARIANT_TYPE_BOOL },
    { "de_requests", m_de_requests, 0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "de_apps",     m_de_apps,     0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "colleague",   m_colleague,   2, { GDEXTENSION_VARIANT_TYPE_STRING, GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "customer_name", m_customer_name, 0, { 0 },            GDEXTENSION_VARIANT_TYPE_STRING },
};
#define NMETHODS ((int)(sizeof METHODS / sizeof METHODS[0]))

static void method_ptrcall(void *method_userdata,
                           GDExtensionClassInstancePtr instance,
                           const GDExtensionConstTypePtr *args,
                           GDExtensionTypePtr ret)
{
    const MethodDef *m = (const MethodDef *)method_userdata;
    m->fn((Station *)instance, args, ret);
}

/* The Variant path. Godot calls this when the method is invoked dynamically
 * (which is what GDScript does unless the call is statically typed), so it has
 * to work, not just the ptrcall fast path. */
static void method_call(void *method_userdata,
                        GDExtensionClassInstancePtr instance,
                        const GDExtensionConstVariantPtr *args,
                        GDExtensionInt argc,
                        GDExtensionVariantPtr ret,
                        GDExtensionCallError *error)
{
    const MethodDef *m = (const MethodDef *)method_userdata;
    if (argc < m->nargs) {
        error->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
        error->argument = (int32_t)m->nargs;
        return;
    }
    error->error = GDEXTENSION_CALL_OK;

    /* unpack Variant args into the raw types ptrcall expects */
    uint8_t raw[2][64];
    const void *argp[2] = { raw[0], raw[1] };
    static GDExtensionInterfaceGetVariantToTypeConstructor to_type;
    if (!to_type) GETPROC(to_type, "get_variant_to_type_constructor");

    for (int i = 0; i < m->nargs; i++) {
        memset(raw[i], 0, sizeof raw[i]);
        GDExtensionTypeFromVariantConstructorFunc conv = to_type(m->argtype[i]);
        conv(raw[i], (GDExtensionVariantPtr)args[i]);
    }

    uint8_t rawret[64];
    memset(rawret, 0, sizeof rawret);
    m->fn((Station *)instance, (const GDExtensionConstTypePtr *)argp, rawret);

    static GDExtensionInterfaceGetVariantFromTypeConstructor from_type;
    if (!from_type) GETPROC(from_type, "get_variant_from_type_constructor");
    GDExtensionVariantFromTypeConstructorFunc back = from_type(m->rettype);
    back(ret, rawret);

    /* release the String we just copied into the Variant */
    if (m->rettype == GDEXTENSION_VARIANT_TYPE_STRING && string_destroy)
        string_destroy(rawret);
    for (int i = 0; i < m->nargs; i++)
        if (m->argtype[i] == GDEXTENSION_VARIANT_TYPE_STRING && string_destroy)
            string_destroy(raw[i]);
}

/* One empty StringName and one empty String, owned for the library's
 * lifetime. Godot reads these while registering and never takes ownership. */
static SN empty_sn;
static struct { uint8_t opaque[8]; } empty_string;

static void register_methods(void)
{
    sn_make(&empty_sn, "");
    c_to_gdstring(&empty_string, "");

    for (int i = 0; i < NMETHODS; i++) {
        const MethodDef *m = &METHODS[i];

        SN mname;
        sn_make(&mname, m->name);

        GDExtensionPropertyInfo ret_info;
        memset(&ret_info, 0, sizeof ret_info);
        ret_info.type = m->rettype;
        ret_info.name = &empty_sn;
        ret_info.class_name = &empty_sn;
        ret_info.hint_string = &empty_string;
        ret_info.usage = 6;   /* PROPERTY_USAGE_DEFAULT */

        GDExtensionPropertyInfo args_info[2];
        GDExtensionClassMethodArgumentMetadata args_meta[2];
        for (int a = 0; a < m->nargs; a++) {
            memset(&args_info[a], 0, sizeof args_info[a]);
            args_info[a].type = m->argtype[a];
            args_info[a].name = &empty_sn;
            args_info[a].class_name = &empty_sn;
            args_info[a].hint_string = &empty_string;
            args_info[a].usage = 6;
            args_meta[a] = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
        }

        GDExtensionClassMethodInfo mi;
        memset(&mi, 0, sizeof mi);
        mi.name = &mname;
        mi.method_userdata = (void *)m;
        mi.call_func = method_call;
        mi.ptrcall_func = method_ptrcall;
        mi.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
        mi.has_return_value = (m->rettype != GDEXTENSION_VARIANT_TYPE_NIL);
        mi.return_value_info = &ret_info;
        mi.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
        mi.argument_count = (uint32_t)m->nargs;
        mi.arguments_info = args_info;
        mi.arguments_metadata = args_meta;

        classdb_add_method(api_lib, &sn_class, &mi);
    }
}

static void initialize(void *userdata, GDExtensionInitializationLevel level)
{
    (void)userdata;
    if (level != GDEXTENSION_INITIALIZATION_SCENE) return;

    sn_make(&sn_class, "NominalStation");
    sn_make(&sn_parent, "RefCounted");

    GDExtensionClassCreationInfo6 info;
    memset(&info, 0, sizeof info);
    info.is_exposed = 1;
    info.create_instance_func = station_create;
    info.free_instance_func = station_free;

    classdb_register(api_lib, &sn_class, &sn_parent, &info);
    register_methods();
}

static void deinitialize(void *userdata, GDExtensionInitializationLevel level)
{
    (void)userdata; (void)level;
}

GDExtensionBool GDE_EXPORT nominal_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization)
{
    api_get = p_get_proc_address;
    api_lib = p_library;

    GETPROC(classdb_register,    "classdb_register_extension_class6");
    GETPROC(classdb_add_method,  "classdb_register_extension_class_method");
    GETPROC(string_new_utf8,     "string_new_with_utf8_chars");
    GETPROC(string_to_utf8,      "string_to_utf8_chars");
    GETPROC(variant_get_ctor,    "variant_get_ptr_constructor");
    GETPROC(variant_get_dtor,    "variant_get_ptr_destructor");
    GETPROC(mem_alloc,           "mem_alloc");
    GETPROC(mem_free,            "mem_free");
    GETPROC(object_set_instance, "object_set_instance");
    GETPROC(object_set_binding,  "object_set_instance_binding");
    GETPROC(classdb_construct,   "classdb_construct_object3");
    GETPROC(stringname_new,      "string_name_new_with_utf8_chars");

    if (!classdb_register || !classdb_add_method || !string_new_utf8 || !classdb_construct) {
        fprintf(stderr, "nominal: GDExtension interface is missing entry points\n");
        return 0;
    }

    string_destroy     = variant_get_dtor(GDEXTENSION_VARIANT_TYPE_STRING);
    stringname_destroy = variant_get_dtor(GDEXTENSION_VARIANT_TYPE_STRING_NAME);
    (void)stringname_destroy;
    (void)string_from_gdstring;
    (void)object_set_binding;
    (void)mem_free;

    r_initialization->initialize = initialize;
    r_initialization->deinitialize = deinitialize;
    r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
    return 1;
}
