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
/* One Godot object == one station (a Sim plus a Shell). */
typedef struct {
    Sim   *sim;
    Shell *sh;
    Buf    scratch;
} Station;

static SN sn_class, sn_parent;

static GDExtensionObjectPtr station_create(void *userdata,
                                           GDExtensionBool notify_postinitialize)
{
    (void)userdata; (void)notify_postinitialize;
    GDExtensionObjectPtr obj = classdb_construct(&sn_parent);
    Station *st = nom_alloc(sizeof(Station));
    st->sim = sim_new(1);
    st->sh  = shell_new(st->sim);
    buf_init(&st->scratch);
    object_set_instance(obj, &sn_class, st);
    return obj;
}

static void station_free(void *userdata, GDExtensionClassInstancePtr instance)
{
    (void)userdata;
    Station *st = (Station *)instance;
    if (!st) return;
    shell_free(st->sh);
    sim_free(st->sim);
    buf_free(&st->scratch);
    nom_free(st);
}

/* ------------------------------------------------------------- the methods
 *
 * Everything the desktop needs, and nothing the socket does not already have.
 * `exec` is the important one: it is literally the socket's command parser, so
 * the in-game terminal and telnet cannot diverge. */

typedef void (*Bound)(Station *st, const GDExtensionConstTypePtr *args, void *ret);

/* exec(String cmd) -> String   — run one shell command, return its response */
static void m_exec(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char cmd[2048];
    gdstring_to_c(args[0], cmd, sizeof cmd);
    Buf out;
    buf_init(&out);
    shell_exec(st->sh, cmd, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* read_file(String path) -> String   — raw device/file read, "" if it blocks */
static void m_read_file(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    Buf out;
    buf_init(&out);
    IoStatus s = vfs_read(&st->sim->fs, path, &out);
    if (s != IO_OK) { buf_clear(&out); }
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* status() -> String   — the same block the socket's `stat` prints */
static void m_status(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    Buf out;
    buf_init(&out);
    sim_status(st->sim, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* result_json() -> String */
static void m_result(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    Buf out;
    buf_init(&out);
    sim_result_json(st->sim, &out);
    c_to_gdstring(ret, out.p ? out.p : "");
    buf_free(&out);
}

/* telemetry() -> String — a compact "key value" block for the schematic.
 * Deliberately the same shape as a device file so the UI parses one format. */
static void m_telemetry(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    Sim *s = st->sim;
    Buf b;
    buf_init(&b);
    sim_telemetry(s, &b);
    c_to_gdstring(ret, b.p ? b.p : "");
    buf_free(&b);
}

/* messages() -> String — the pager's contents */
static void m_messages(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    Buf b; buf_init(&b);
    sim_messages(st->sim, &b, false);
    c_to_gdstring(ret, b.p ? b.p : "");
    buf_free(&b);
}

/* events(int from) -> String — log lines since index `from` */
static void m_events(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int from = (int)(*(const int64_t *)args[0]);
    Sim *s = st->sim;
    if (from < 0) from = 0;
    Buf b;
    buf_init(&b);
    for (int i = from; i < s->nevents; i++)
        buf_printf(&b, "%llu %s\n", (unsigned long long)s->event[i].tick, s->event[i].text);
    c_to_gdstring(ret, b.p ? b.p : "");
    buf_free(&b);
}

/* event_count() -> int */
static void m_event_count(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    (void)args;
    *(int64_t *)ret = st->sim->nevents;
}

/* tick(int n) -> int — advance n ticks, return the new tick */
static void m_tick(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    int64_t n = *(const int64_t *)args[0];
    for (int64_t i = 0; i < n && st->sim->run == RUN_ACTIVE; i++) sim_tick(st->sim);
    *(int64_t *)ret = (int64_t)st->sim->tick;
}

/* load_home(String path) -> bool */
static void m_load_home(Station *st, const GDExtensionConstTypePtr *args, void *ret)
{
    char path[NOM_PATH_MAX];
    gdstring_to_c(args[0], path, sizeof path);
    char err[NOM_ERR_MAX];
    *(GDExtensionBool *)ret = sim_load_home(st->sim, path, err, sizeof err) ? 1 : 0;
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
    { "exec",        m_exec,        1, { GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "read_file",   m_read_file,   1, { GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_STRING },
    { "status",      m_status,      0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "result_json", m_result,      0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "telemetry",   m_telemetry,   0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "messages",    m_messages,    0, { 0 },                              GDEXTENSION_VARIANT_TYPE_STRING },
    { "events",      m_events,      1, { GDEXTENSION_VARIANT_TYPE_INT },   GDEXTENSION_VARIANT_TYPE_STRING },
    { "event_count", m_event_count, 0, { 0 },                              GDEXTENSION_VARIANT_TYPE_INT },
    { "tick",        m_tick,        1, { GDEXTENSION_VARIANT_TYPE_INT },   GDEXTENSION_VARIANT_TYPE_INT },
    { "load_home",   m_load_home,   1, { GDEXTENSION_VARIANT_TYPE_STRING }, GDEXTENSION_VARIANT_TYPE_BOOL },
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
