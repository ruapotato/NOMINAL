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
#include "building.h"
#include "site.h"
#include "session.h"
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
    /* THE TOWER. Generated on demand and read out as text, exactly like every
     * other method here: the extension owns the geometry and the scene is a
     * view of it. Nothing in Godot may compute a room, a wall or a distance
     * for itself, because then there would be two buildings and only one of
     * them would be the one the game charges for. */
    Building bld;
    bool     bld_ok;
    /* THE SITE: what the player has bought, cabled and configured in that
     * tower. There is exactly one inventory in this project and it is this
     * one -- a second list of devices living in GDScript would be a second
     * truth, and the two would disagree the first time a cable was refused. */
    Site     site;
    bool     site_ok;
    /* THE SESSION: the same struct core/session.c runs for a socket client.
     *
     * The owner, on how an agent should play this: *"Perhaps the commands
     * it's using should tie to 3D space... Claude playing a video game in 3D
     * space by taking screenshots of the actual user interface is not a
     * fantastic way for it to iterate. It should operate with commands over
     * the port. A 3D interface should keep up with what's happening. So if
     * you cable from one place to another place, a physical cord gets
     * rendered out as if the player had cabled that."*
     *
     * So the window does not own a building and a site of its own any more.
     * When a session is up it IS the building and the site -- BLD() and SITE()
     * below hand out the session's -- and every verb the 3D performs goes
     * through session_line(), which is the same function, the same refusals
     * and the same charges the socket gets. The view is a view. */
    Session  ses;
    bool     ses_ok;
} Station;

/* Where the geometry and the inventory actually live. One or the other: the
 * session's when there is one, and the standalone pair for `--desk` and for
 * the gates that generate a building without playing in it. */
static Building *BLD(Station *st) { return st->ses_ok ? &st->ses.b : &st->bld; }
static Site     *SITE(Station *st) { return st->ses_ok ? &st->ses.s : &st->site; }

/* AND WHERE THE DESKTOP RUNS, which is the same question with the same
 * answer. `st->desk` is the break-fix bench's workstation: a machine on
 * nobody's network, which is right for `--desk` and for a run with no tower
 * in it. In a SESSION the player's workstation is a box standing in the MDF
 * with one gigabit socket, a plug in the wall and a lead in the handoff --
 * `site_workstation()` -- and the desktop, the browser, the files app and
 * the terminal have to be running on THAT, or the shop cannot be taken away
 * by anything the player does to their own building. D40 named this as the
 * gap; D41 is it being closed.
 *
 * The peer is set here for the same reason ensure_desk() sets it: `rcon` is
 * how the break-fix half reaches the customer's machine, and it is reached
 * FROM a computer. */
static Machine *DESK(Station *st)
{
    Machine *m = st->ses_ok ? session_ws_machine(&st->ses) : NULL;
    if (!m) return &st->desk;
    m->peer = &st->m;
    if (!m->peer_addr[0])
        snprintf(m->peer_addr, sizeof m->peer_addr, "%s", st->desk.peer_addr);
    return m;
}

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
    if (st->ses_ok) session_end(&st->ses);
    if (st->site_ok) site_free(&st->site);
    if (st->bld_ok) bld_free(&st->bld);
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
    /* The machine's own argument limit. 2048 silently cut a long command
     * line in the desktop terminal, which is the one front end where the
     * player cannot see it happen. */
    char line[NOM_ARG_MAX];
    gdstring_to_c(args[1], line, sizeof line);
    Buf out; buf_init(&out);
    /* A CONSOLE ON A DEAD MACHINE HAS NO SHELL -- and it has to SAY so.
     * The desktop knew this and answered with an empty string, which is the
     * one thing worse than a fake prompt: `stat /mnt/etc/fstab` came back
     * blank and blank reads as "the file is fine". Same words as the socket,
     * from the same function, so the two consoles cannot disagree. */
    if (which && kernel_console_dead(&st->m, line, &out)) {
        c_to_gdstring(ret, out.p ? out.p : "");
        buf_free(&out);
        return;
    }
    kernel_run(which ? &st->m : DESK(st), line, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* handback() -> String — hand the machine back, and check the claim.
 *
 * The same machine_handback() the socket console and --desk call, so the
 * three front ends cannot disagree about whether a job is finished. */
static void m_handback(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    Buf out = {0};
    if (st->installed) machine_handback(&st->m, &out);
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
    VNode *n = vfs_lookup(&DESK(st)->disk, "/run/nomde/requests");
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
    VNode *d = vfs_lookup(&DESK(st)->disk, "/usr/share/applications");
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
    c_to_gdstring(ret, DESK(st)->peer_addr);
}

static void m_sh(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    /* The machine's own argument limit. 2048 silently cut a long command
     * line in the desktop terminal, which is the one front end where the
     * player cannot see it happen. */
    char line[NOM_ARG_MAX];
    gdstring_to_c(args[0], line, sizeof line);
    Buf out; buf_init(&out);
    kernel_run(&st->m, line, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* customer_options() -> String
 * customer_choose(int idx, String arg) -> String
 *
 * THE PERSON IN FRONT OF THE MACHINE, as a menu.
 *
 * She used to be a language model and answered free text; the desktop chat
 * called `ask` with whatever was typed and waited a minute or more for a
 * reply. She is deterministic now, so what she can be asked is a list that
 * depends on the state of the call, and saying one of them is instant. The
 * numbers in the list are stable ids -- pass the id, not the position.
 *
 * `arg` carries the command for the dictate option and is ignored by every
 * other one. */
static void m_customer_options(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    Buf out; buf_init(&out);
    customer_options(&st->m, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

static void m_customer_choose(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int64_t idx = *(const int64_t *)args[0];
    char arg[NOM_ARG_MAX];
    gdstring_to_c(args[1], arg, sizeof arg);
    Buf out; buf_init(&out);
    customer_choose(&st->m, (int)idx, arg, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* customer_name() -> String — the name of the person on the phone. */
static void m_customer_name(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    c_to_gdstring(ret, customer_name(&st->m));
}

/* complaint() -> String
 *
 * WHAT THE CUSTOMER SAYS IS WRONG, and it has to be true.
 *
 * The desktop opened every ticket with "my computer will not start", hard
 * coded, on every machine -- and about one ticket in four is a machine that
 * IS started and has something dead on it. A blind playtester played sixteen
 * boots and reported that they had never once been given an up-but-sick
 * machine; they had been given three, and the first sentence of the call told
 * them otherwise, so they spent those tickets looking for a boot failure that
 * was not there. A lie in the opening line is worse than no line at all,
 * because it is the one thing a player has no way to check.
 *
 * It stays at the customer's level of knowledge: they can see a screen, they
 * cannot see a service. Naming what is actually wrong is still the job. */
static void m_complaint(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!st->installed) { c_to_gdstring(ret, "my computer will not start."); return; }
    Buf sick; buf_init(&sick);
    int dead = kernel_health(&st->m, &sick);
    buf_free(&sick);
    Buf left; buf_init(&left);
    int rest = machine_outstanding(&st->m, &left) ? 1 : 0;
    buf_free(&left);
    const char *say;
    if (!st->m.boot.running)
        say = "my computer will not start.";
    else if (dead || rest)
        say = "my computer comes on, and something on it is not working.";
    else
        say = "it seems all right now, and I would like somebody to be sure.";
    c_to_gdstring(ret, say);
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

/* ------------------------------------------------------------- the tower
 *
 * The building is space, in metres, and the renderer is not allowed to invent
 * any of it. Everything below is a read: generate once, then ask for the
 * floors, the rooms, the doors, the cells and the two distances. The text
 * shapes are the same line-per-record shape list_dir() and packages() use, so
 * there is one parsing idiom in the front end and no binary layout to drift.
 *
 * The two distances stay separate here for the same reason they are separate
 * in building.h: a person cannot walk up a riser and a cable does not go down
 * the stairs, and the price of a run depends on which of the two you meant. */

static bool bld_ready(Station *st)
{
    if (st->ses_ok || st->bld_ok) return true;
    /* A view that asks before generating gets an empty answer, not a walk
     * through a zeroed Building -- the same mistake that took Godot down with
     * a native backtrace twice already. */
    return false;
}

/* bld_generate(int seed) -> String — the tower's dimensions, key per line.
 * Regenerating is cheap and idempotent, so the scene may call it on load. */
static void m_bld_generate(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    uint64_t seed = (uint64_t)(*(const int64_t *)args[0]);
    /* The site BORROWS the building. Regenerating under a live site would
     * leave every device pointing into freed rooms, so the site goes first. */
    if (st->site_ok) { site_free(&st->site); st->site_ok = false; }
    /* A SESSION ALREADY HAS ONE. Generating a second building from the same
     * seed would give the same rooms and a different object, and then half the
     * view would be reading one and half the other. */
    if (st->ses_ok && BLD(st)->seed == seed) { /* nothing to do */ }
    else if (st->bld_ok) { bld_free(&st->bld); st->bld_ok = false; }
    if (!(st->ses_ok && BLD(st)->seed == seed)) {
        if (!bld_generate(&st->bld, seed)) { c_to_gdstring(ret, ""); return; }
        st->bld_ok = true;
    }
    const Building *b = BLD(st);
    Buf o; buf_init(&o);
    buf_printf(&o, "seed %llu\n", (unsigned long long)b->seed);
    buf_printf(&o, "floors %d\n", b->floors);
    buf_printf(&o, "plate %d %d\n", b->w, b->h);
    buf_printf(&o, "floor_height %.4f\n", b->floor_height);
    buf_printf(&o, "tenants %d\n", b->ntenants);
    buf_printf(&o, "rooms %d\n", b->nrooms);
    buf_printf(&o, "doors %d\n", b->ndoors);
    buf_printf(&o, "core %d %d %d %d\n", b->core_x0, b->core_y0, b->core_x1, b->core_y1);
    buf_printf(&o, "ring %d %d %d %d\n", b->ring_x0, b->ring_y0, b->ring_x1, b->ring_y1);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* bld_floors() -> String — "floor kind fx0 fy0 fx1 fy1 kindname" per line. */
static void m_bld_floors(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!bld_ready(st)) { c_to_gdstring(ret, ""); return; }
    const Building *b = BLD(st);
    Buf o; buf_init(&o);
    for (int f = 0; f < b->floors; f++)
        buf_printf(&o, "%d %d %d %d %d %d %s\n", f, b->fkind[f],
                   b->fx0[f], b->fy0[f], b->fx1[f], b->fy1[f],
                   bld_floor_kind_name(b->fkind[f]));
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* bld_rooms() -> String — "i floor kind tenant x0 y0 x1 y1 kindname" per line.
 * The index is the room id every other call speaks in. */
static void m_bld_rooms(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!bld_ready(st)) { c_to_gdstring(ret, ""); return; }
    const Building *b = BLD(st);
    Buf o; buf_init(&o);
    for (int i = 0; i < b->nrooms; i++) {
        const Room *r = &b->rooms[i];
        buf_printf(&o, "%d %d %d %d %d %d %d %d %s\n", i, r->floor, r->kind,
                   r->tenant, r->x0, r->y0, r->x1, r->y1,
                   bld_kind_name(r->kind));
    }
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* bld_doors() -> String — "a b floor x y dir" per line. A door is an EDGE:
 * dir 0 is the wall between (x,y) and (x+1,y), dir 1 between (x,y) and
 * (x,y+1). The renderer leaves a gap there instead of building a wall, which
 * is why a door cannot end up opening into brickwork. */
static void m_bld_doors(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!bld_ready(st)) { c_to_gdstring(ret, ""); return; }
    const Building *b = BLD(st);
    Buf o; buf_init(&o);
    for (int i = 0; i < b->ndoors; i++) {
        const Door *d = &b->doors[i];
        buf_printf(&o, "%d %d %d %d %d %d\n", d->a, d->b, d->floor, d->x, d->y, d->dir);
    }
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* bld_cells(int floor) -> String — h lines of w room indices, 65535 outside
 * the plate. The renderer walks these to find every wall, so a wall exists
 * exactly where two neighbouring square metres belong to different rooms. */
static void m_bld_cells(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int f = (int)(*(const int64_t *)args[0]);
    if (!bld_ready(st) || f < 0 || f >= BLD(st)->floors) { c_to_gdstring(ret, ""); return; }
    const Building *b = BLD(st);
    Buf o; buf_init(&o);
    for (int y = 0; y < b->h; y++) {
        for (int x = 0; x < b->w; x++)
            buf_printf(&o, x ? " %u" : "%u",
                       (unsigned)b->cell[(size_t)f * b->h * b->w + (size_t)y * b->w + x]);
        buf_printf(&o, "\n");
    }
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* bld_find(int floor, int kind) -> int — the first room of a kind on a floor,
 * -1 when there is none. Where the player spawns, and where the test looks
 * for a comms cupboard to walk to. */
static void m_bld_find(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int f = (int)(*(const int64_t *)args[0]);
    int k = (int)(*(const int64_t *)args[1]);
    *(int64_t *)ret = bld_ready(st) ? bld_find(BLD(st), f, k) : -1;
}

/* bld_room_at(int floor, int x*1000+y) is unusable as two ints, so this takes
 * the metre coordinate packed the way the caller has it: bld_room_at_xy is
 * spelled with two ints because that is what the method table allows. */
static void m_bld_room_at(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    /* x and y packed into one int: x * 4096 + y. The method table carries two
     * arguments, and the floor needs one of them. */
    int f  = (int)(*(const int64_t *)args[0]);
    int64_t xy = *(const int64_t *)args[1];
    *(int64_t *)ret = bld_ready(st)
        ? bld_room_at(BLD(st), f, (int)(xy / 4096), (int)(xy % 4096)) : -1;
}

static void bld_dist_out(Station *st, int src, bool cable, void *ret)
{
    if (!bld_ready(st) || src < 0 || src >= BLD(st)->nrooms) { c_to_gdstring(ret, ""); return; }
    double *d = nom_alloc(sizeof(double) * (size_t)BLD(st)->nrooms);
    bool ok = cable ? bld_cable_all(BLD(st), src, d) : bld_walk_all(BLD(st), src, d);
    Buf o; buf_init(&o);
    if (ok)
        for (int i = 0; i < BLD(st)->nrooms; i++)
            /* -1 for unreachable: a route that does not exist has no price,
             * and a huge number would be quietly spent. */
            buf_printf(&o, i ? " %.3f" : "%.3f", d[i] >= BLD_INF / 2 ? -1.0 : d[i]);
    nom_free(d);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* bld_walk(int room) -> String  — metres a PERSON walks to every room. */
static void m_bld_walk(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    bld_dist_out(st, (int)(*(const int64_t *)args[0]), false, ret);
}

/* bld_cable(int room) -> String — metres of CABLE to every room. Not the
 * same number, deliberately: the tray and the riser are not the stairs. */
static void m_bld_cable(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    bld_dist_out(st, (int)(*(const int64_t *)args[0]), true, ret);
}

/* bld_floorplan(int floor) -> String — the ASCII plan, so a blind test and a
 * bug report can both see the floor the renderer was handed. */
static void m_bld_floorplan(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int f = (int)(*(const int64_t *)args[0]);
    if (!bld_ready(st) || f < 0 || f >= BLD(st)->floors) { c_to_gdstring(ret, ""); return; }
    Buf o; buf_init(&o);
    bld_floorplan(BLD(st), f, &o);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* --------------------------------------------------------------- the site
 *
 * The player's own network, in the tower the generator made. core/site.c owns
 * all of it: what is installed, what is cabled, what it cost and whether the
 * link came up. Nothing below computes anything -- site_cmd() is the same
 * one-line-one-operation shell a blind playtester drives over a pipe, and the
 * two dumps are reads in the same line-per-record shape as bld_rooms().
 *
 * This is why there is no inventory in GDScript. The 3D view asks where the
 * boxes are and draws them; when it wants one moved it says so in a line of
 * text, and the refusal it gets back is the real refusal. */

static bool site_ready(Station *st)
{
    return st->ses_ok || (st->site_ok && st->bld_ok);
}

/* site_start(int budget) -> String — day one on the current building: the
 * ISP handoff in the MDF and nothing else. Idempotent; call it after
 * bld_generate(). Empty string when there is no building to put it in. */
static void m_site_start(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    long budget = (long)(*(const int64_t *)args[0]);
    if (!bld_ready(st)) { c_to_gdstring(ret, ""); return; }
    if (st->ses_ok) {          /* a session already owns one; do not make a second */
        Buf o; buf_init(&o);
        site_dump(SITE(st), &o);
        c_to_gdstring(ret, o.p ? o.p : "");
        buf_free(&o);
        return;
    }
    if (st->site_ok) { site_free(&st->site); st->site_ok = false; }
    if (!site_new(&st->site, BLD(st), BLD(st)->seed, budget)) {
        c_to_gdstring(ret, "");
        return;
    }
    st->site_ok = true;
    Buf o; buf_init(&o);
    site_dump(SITE(st), &o);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* site_cmd(String line) -> String — one line, one operation. The whole game
 * over a pipe, and the exact call the 3D view makes when a lead goes in. */
static void m_site_cmd(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char line[512];
    gdstring_to_c(args[0], line, sizeof line);
    if (!site_ready(st)) { c_to_gdstring(ret, "there is no site yet\n"); return; }
    Buf o; buf_init(&o);
    site_cmd(SITE(st), line, &o);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* site_devs() -> String — "i kind room floor tenant nports kindname name" per
 * line. Where the boxes are, so the view can draw them in the racks. */
static void m_site_devs(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!site_ready(st)) { c_to_gdstring(ret, ""); return; }
    const Site *s = SITE(st);
    Buf o; buf_init(&o);
    for (int i = 0; i < s->ndev; i++) {
        const SiteDev *d = &s->dev[i];
        /* AND WHETHER IT IS ON, AND WHETHER IT IS IN THE WALL. Appended, so
         * every existing reader of this dump is unaffected. The window needs
         * both for the player's own workstation: a monitor attached to a box
         * with no power in it shows nothing, and that nothing is the same
         * diagnosis a serial lead into it gives. */
        buf_printf(&o, "%d %d %d %d %d %d %s %s %d %d\n", i, d->kind,
                   (int)(int16_t)d->room, d->floor, d->tenant, d->nports,
                   site_kind_name(d->kind), d->name, d->powered ? 1 : 0,
                   d->mains ? 1 : 0);
    }
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* site_links() -> String — "i a aport b bport room_a room_b metres cost kind
 * state" per line. The cables that really exist, so the tray above the
 * corridor carries the runs the site model was charged for and no others. */
static void m_site_links(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!site_ready(st)) { c_to_gdstring(ret, ""); return; }
    const Site *s = SITE(st);
    Buf o; buf_init(&o);
    for (int i = 0; i < s->nlink; i++) {
        const SiteLink *l = &s->link[i];
        buf_printf(&o, "%d %d %d %d %d %d %d %d %d %d %d\n", i, l->a, l->aport,
                   l->b, l->bport, (int)(int16_t)l->room_a, (int)(int16_t)l->room_b,
                   l->metres, l->cost, l->kind,
                   l->cable < 0 ? -1 : (int)site_link_state(s, i));
    }
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* site_room_of(int dev) -> int — the room a device sits in, BLD_NOROOM for the
 * handoff, which is on the far side of a wall socket. */
static void m_site_room_of(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int d = (int)(*(const int64_t *)args[0]);
    if (!site_ready(st) || d < 0 || d >= SITE(st)->ndev) { *(int64_t *)ret = -1; return; }
    *(int64_t *)ret = (int64_t)SITE(st)->dev[d].room;
}

/* ------------------------------------------------------- method plumbing */
typedef struct {
    const char *name;
    Bound       fn;
    int         nargs;
    GDExtensionVariantType argtype[2];
    GDExtensionVariantType rettype;   /* GDEXTENSION_VARIANT_TYPE_NIL == void */
} MethodDef;


/* ======================================================= THE SESSION
 *
 * ONE PLAYER, ONE BUILDING, ONE SET OF VERBS. Everything the 3D shell does --
 * walking, carrying, taking a drum of cable off the shelf, putting an end in a
 * port, plugging a lead into a console -- goes through session_line() here,
 * which is byte for byte the function `./build/bf --serve` runs for a socket
 * client. There is no second implementation for the window to drift away from,
 * and a refusal a player reads in the 3D is the refusal core wrote.
 *
 * It also runs the other way: a command typed over the socket changes the
 * session, and the view reconciles itself off ses_state() and site_links().
 * That is what makes an agent able to PLAY this in 3D with text -- which is the
 * only thing that has ever moved this project's quality. */

/* ses_start(int seed, int budget) -> String — the tower, the site, and you
 * standing in the MDF of it. Replaces bld_generate + site_start in one call,
 * because a Session owns both and two of either would be two games. */
static void m_ses_start(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    uint64_t seed = (uint64_t)(*(const int64_t *)args[0]);
    long budget = (long)(*(const int64_t *)args[1]);
    if (st->ses_ok) { session_end(&st->ses); st->ses_ok = false; }
    if (!session_start(&st->ses, seed, budget)) { c_to_gdstring(ret, ""); return; }
    st->ses_ok = true;
    Buf o; buf_init(&o);
    site_dump(&st->ses.s, &o);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* ses_cmd(String) -> String — one line, one thing, and the answer the socket
 * would have got. An unrecognised line says so rather than silently doing
 * nothing, because a silence is indistinguishable from a bug. */
static void m_ses_cmd(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char line[512];
    gdstring_to_c(args[0], line, sizeof line);
    if (!st->ses_ok) { c_to_gdstring(ret, "there is no session yet\n"); return; }
    Buf o; buf_init(&o);
    if (!session_line(&st->ses, line, &o))
        buf_printf(&o, "I do not know how to `%s`. `help` lists what there is.\n", line);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* ses_state() -> String — everything the view needs to draw the player: where
 * they are, what is in their hands, what a lead is in. Key per line, in the
 * same shape as every other dump here. The view reads this; it never keeps its
 * own copy of any of it. */
static void m_ses_state(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!st->ses_ok) { c_to_gdstring(ret, ""); return; }
    const Session *e = &st->ses;
    Buf o; buf_init(&o);
    buf_printf(&o, "where %d\nroom %d\nfloors %d\nwalked %ld\n",
               e->where, e->room, e->floors, e->walked);
    buf_printf(&o, "carrying %d\nspool %d %d\ncab %d %d\nplugged %d %d\n",
               e->carrying, e->spool_kind, e->spool_left,
               e->cab_dev, e->cab_port, e->plugged, e->hdmi ? 1 : 0);
    buf_printf(&o, "money %ld\nday %d\n", e->s.money, e->s.day);
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

/* ses_prompt() -> String — what to print in front of the cursor, and it is
 * session_prompt(), not a second opinion about it.
 *
 * A socket client has no screen. The wire's prompt was derived from the ROOM
 * alone, so a client that had plugged a serial lead into a server and was
 * typing `ls /` and `dmesg` at that machine's real shell saw `f0 MDF> ` --
 * the same three characters as standing in the room doing nothing. A blind
 * playtester called that the single most dangerous piece of missing state in
 * the game, and they are right: every word you type is going somewhere else
 * and nothing on the line says so.
 *
 * session_prompt() has always known: `root@files# ` for a shell, `mgmt@core# `
 * for a management line, `f0 MDF> ` for the body. Reimplementing that in
 * GDScript would be a second place for it to be wrong, which is the mistake
 * this whole extension exists to avoid, so it is exported instead. */
static void m_ses_prompt(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    if (!st->ses_ok) { c_to_gdstring(ret, "> "); return; }
    char p[96];
    session_prompt(&st->ses, p, sizeof p);
    c_to_gdstring(ret, p);
}

/* ses_here(int room) -> String — the body moved, in the 3D, on its own legs.
 *
 * The session's `go` verb walks you somewhere and charges the metres. A person
 * at the keyboard walks with W, and those metres are just as real, so the
 * session is told where the body now is and the walk is added to the count.
 * This is the ONE place the view tells the model something rather than reading
 * it, and it is the one thing the view genuinely knows first: where the legs
 * went. */
static void m_ses_here(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int room = (int)(*(const int64_t *)args[0]);
    if (!st->ses_ok) { c_to_gdstring(ret, ""); return; }
    Session *e = &st->ses;
    if (room < 0 || room >= e->b.nrooms || room == e->room) { c_to_gdstring(ret, ""); return; }
    double *d = nom_alloc(sizeof(double) * (size_t)e->b.nrooms);
    if (bld_walk_all(&e->b, e->room, d) && d[room] < BLD_INF)
        e->walked += (long)(d[room] + 0.5);
    nom_free(d);
    e->room = room;
    /* A BOX IN YOUR ARMS IS IN THE ROOM YOU ARE IN, at every step of the walk
     * -- not when you put it down. site_move is what says so, and it is the
     * one that refuses to move a box with a cable in it. */
    if (e->carrying >= 0) site_move(&e->s, e->carrying, room);
    Buf o; buf_init(&o);
    buf_printf(&o, "%s\n", bld_kind_name(e->b.rooms[room].kind));
    c_to_gdstring(ret, o.p ? o.p : "");
    buf_free(&o);
}

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
    { "customer_options", m_customer_options, 0, { 0 },          GDEXTENSION_VARIANT_TYPE_STRING },
    { "customer_choose",  m_customer_choose,  2, { GDEXTENSION_VARIANT_TYPE_INT, GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "sh_on",       m_sh_on,       2, { GDEXTENSION_VARIANT_TYPE_INT, GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "peer_addr",   m_peer_addr,   0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "handback",    m_handback,    0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "healthy",     m_healthy,     0, { 0 },                              GDEXTENSION_VARIANT_TYPE_BOOL },
    { "de_requests", m_de_requests, 0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "de_apps",     m_de_apps,     0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "customer_name", m_customer_name, 0, { 0 },            GDEXTENSION_VARIANT_TYPE_STRING },
    { "complaint",   m_complaint,   0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_generate",  m_bld_generate,  1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_floors",    m_bld_floors,    0, { 0 },                            GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_rooms",     m_bld_rooms,     0, { 0 },                            GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_doors",     m_bld_doors,     0, { 0 },                            GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_cells",     m_bld_cells,     1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_find",      m_bld_find,      2, { GDEXTENSION_VARIANT_TYPE_INT, GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_INT },
    { "bld_room_at",   m_bld_room_at,   2, { GDEXTENSION_VARIANT_TYPE_INT, GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_INT },
    { "bld_walk",      m_bld_walk,      1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_cable",     m_bld_cable,     1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "bld_floorplan", m_bld_floorplan, 1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "site_start",    m_site_start,    1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "site_cmd",      m_site_cmd,      1, { GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "site_devs",     m_site_devs,     0, { 0 },                            GDEXTENSION_VARIANT_TYPE_STRING },
    { "site_links",    m_site_links,    0, { 0 },                            GDEXTENSION_VARIANT_TYPE_STRING },
    { "site_room_of",  m_site_room_of,  1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_INT },
    { "ses_start",     m_ses_start,     2, { GDEXTENSION_VARIANT_TYPE_INT, GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "ses_cmd",       m_ses_cmd,       1, { GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "ses_state",     m_ses_state,     0, { 0 },                            GDEXTENSION_VARIANT_TYPE_STRING },
    { "ses_here",      m_ses_here,      1, { GDEXTENSION_VARIANT_TYPE_INT }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "ses_prompt",    m_ses_prompt,    0, { 0 },                            GDEXTENSION_VARIANT_TYPE_STRING },
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
