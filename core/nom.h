/* nom.h — shared declarations for the NOMINAL core.
 *
 * The core is plain C11 with no third-party dependencies and no knowledge of
 * Godot. It owns: the virtual file tree, the NomScript interpreter, the
 * simulation, the shell and the TCP server. See docs/decisions.md D1.
 */
#ifndef NOM_H
#define NOM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------- limits */
/* Fixed limits, in the spirit of HAMSH_SPEC 16b: every one of these raises a
 * named error on overflow. None silently truncates. */
#define NOM_PATH_MAX      256
#define NOM_NAME_MAX      64
/* How long ONE program's argument string may be. It is not NOM_PATH_MAX,
 * because an argument is not a path: a glob over /tmp expands to every name
 * in the directory, and a 256-byte ceiling silently threw away everything
 * past the eighth match -- which on one ticket dropped the faulty file out of
 * a glob of the .conf files in /etc, and left the player looking at a
 * complete-looking answer that was missing the evidence.
 *
 * 4096 was the next ceiling and it was set by counting the fault that exists
 * rather than the one a player would meet: /var/cache fills with 120 files
 * whose paths are 30 bytes under /mnt, which comes to 3720 and fitted with
 * three hundred bytes to spare. Any deeper mount point, any longer name, and
 * a glob of that cache deleted NOTHING and printed a usage line. A limit
 * that a real glob on this machine can reach is a limit in the wrong place.
 * 16384 clears the largest directory the breaker can make by a wide margin.
 * Must match GARG_MAX in guest/gsys.h. */
#define NOM_ARG_MAX       16384
#define NOM_STACK_MAX     256
#define NOM_FRAMES_MAX    32
#define NOM_LOCALS_MAX    64
#define NOM_GLOBALS_MAX   128
#define NOM_CONSTS_MAX    512
#define NOM_CODE_MAX      16384
#define NOM_FUNCS_MAX     32
#define NOM_ARGS_MAX      8
#define NOM_LIST_MAX      1024
#define NOM_SRC_MAX       65536
#define NOM_TOK_MAX       8192
#define NOM_ERR_MAX       256

/* --------------------------------------------------------------- buffers */
typedef struct {
    char   *p;
    size_t  len, cap;
} Buf;

void  buf_init(Buf *b);
void  buf_free(Buf *b);
void  buf_clear(Buf *b);
void  buf_put(Buf *b, const void *data, size_t n);
void  buf_puts(Buf *b, const char *s);
void  buf_putc(Buf *b, char c);
void  buf_printf(Buf *b, const char *fmt, ...);
/* Deterministic double formatting: fixed decimals, no locale, no %g. */
void  buf_putnum(Buf *b, double v, int decimals);

void *nom_alloc(size_t n);
void *nom_realloc(void *p, size_t n);
void  nom_free(void *p);
char *nom_strdup(const char *s);

/* ------------------------------------------------------------------- rng */
/* splitmix64. The only source of randomness in the program. */
typedef struct { uint64_t s; } Rng;
void     rng_seed(Rng *r, uint64_t seed);
uint64_t rng_next(Rng *r);
/* uniform in [0,1) with 53 bits, deterministic across platforms */
double   rng_unit(Rng *r);
int32_t  rng_range(Rng *r, int32_t lo, int32_t hi); /* inclusive */

/* ----------------------------------------------------------------- fmath */
/* Our own math so results don't vary with the host libm. D3. */
double nom_sqrt(double x);
double nom_sin(double x);
double nom_cos(double x);
double nom_atan2(double y, double x);
double nom_fabs(double x);
double nom_floor(double x);
double nom_pow(double b, double e);

/* ----------------------------------------------------------------- value */
typedef struct Obj    Obj;
typedef struct VM     VM;
typedef struct Vfs    Vfs;
typedef struct Sim    Sim;

typedef enum {
    V_NIL, V_BOOL, V_INT, V_NUM, V_OBJ
} VKind;

typedef enum {
    O_STR, O_LIST, O_DICT, O_FUNC, O_NATIVE
} OKind;

typedef struct {
    uint8_t k;
    union { int64_t i; double d; Obj *o; } as;
} Value;

struct Obj {
    uint8_t  kind;
    uint32_t rc;
};

typedef struct { Obj o; uint32_t len; char s[]; } Str;
typedef struct { Obj o; uint32_t len, cap; Value *v; } List;
typedef struct { Obj o; uint32_t len, cap; Value *k; Value *v; } Dict; /* insertion ordered */

#define VAL_NIL      ((Value){ .k = V_NIL,  .as.i = 0 })
#define VAL_BOOL(b)  ((Value){ .k = V_BOOL, .as.i = (b) ? 1 : 0 })
#define VAL_INT(n)   ((Value){ .k = V_INT,  .as.i = (n) })
#define VAL_NUM(x)   ((Value){ .k = V_NUM,  .as.d = (x) })
#define VAL_OBJ(p)   ((Value){ .k = V_OBJ,  .as.o = (Obj *)(p) })

#define IS_STR(v)   ((v).k == V_OBJ && (v).as.o->kind == O_STR)
#define IS_LIST(v)  ((v).k == V_OBJ && (v).as.o->kind == O_LIST)
#define IS_DICT(v)  ((v).k == V_OBJ && (v).as.o->kind == O_DICT)
#define AS_STR(v)   ((Str  *)(v).as.o)
#define AS_LIST(v)  ((List *)(v).as.o)
#define AS_DICT(v)  ((Dict *)(v).as.o)

Value  str_new(const char *s, size_t len);
Value  str_newz(const char *s);
Value  list_new(void);
void   list_push(List *l, Value v);
Value  dict_new(void);
void   dict_set(Dict *d, Value key, Value v);
bool   dict_get(Dict *d, Value key, Value *out);

Value  val_retain(Value v);
void   val_release(Value v);
bool   val_truthy(Value v);
bool   val_equal(Value a, Value b);
void   val_repr(Buf *b, Value v);   /* python-ish repr, strings quoted */
void   val_tostr(Buf *b, Value v);  /* str(), strings bare */
double val_num(Value v);
/* Deterministic decimal parsing, shared by the language, sim and shell. */
bool   nom_parse_number(const char *s, size_t len, Value *out);
int64_t val_int(Value v);

/* ------------------------------------------------------------------- vfs */
/* A device file is a read callback plus a write callback plus a "would this
 * block right now" answer. D5. */
typedef struct VNode VNode;

/* VN_BIND grafts one path onto another (a bind mount). VN_LINK is a real
 * symlink: it derefs the same way, but it is a file you can see, edit and
 * break, and a dangling one is a legitimate state rather than an error in the
 * tree. The boot chain needs both. */
typedef enum { VN_DIR, VN_FILE, VN_DEV, VN_BIND, VN_LINK } VNodeKind;

/* Return values shared by device callbacks. */
typedef enum {
    IO_OK = 0,
    IO_BLOCK,     /* no data available yet; caller should suspend and retry */
    IO_ERR        /* err string set on the Vfs */
} IoStatus;

typedef IoStatus (*DevRead) (VNode *n, Buf *out, void *ctx);
typedef IoStatus (*DevWrite)(VNode *n, const char *data, size_t len, void *ctx);

struct VNode {
    char       name[NOM_NAME_MAX];
    VNodeKind  kind;
    VNode     *parent;
    VNode     *child;    /* first child, ordered by insertion */
    VNode     *next;     /* next sibling */
    /* VN_FILE */
    Buf        data;
    /* VN_DEV */
    DevRead    read;
    DevWrite   write;
    void      *ctx;
    int        id;       /* device-specific discriminator */
    int        src;      /* for a field file: the aggregate it reads out of */
    char       target[NOM_PATH_MAX];  /* VN_BIND/VN_LINK: what this stands for */
    /* Unix mode bits, low 9 only. An init that is not executable is a real
     * fault with a real error message, so this has to be modelled. */
    unsigned   mode;
    
    /* bookkeeping the sim uses to surface honest diagnostics */
    uint64_t   reads, writes, blocks;
    uint64_t   last_read_tick, last_write_tick;
};

struct Vfs {
    VNode *root;
    char   err[NOM_ERR_MAX];
    void  *ctx;          /* Sim *, for device callbacks */
};

void   vfs_init(Vfs *fs);
void   vfs_free(Vfs *fs);
VNode *vfs_mkdir(Vfs *fs, const char *path);
VNode *vfs_mkfile(Vfs *fs, const char *path, const char *contents);
VNode *vfs_mkdev(Vfs *fs, const char *path, DevRead rd, DevWrite wr, int id);
/* A single field of an aggregate status device, as its own file. Plan 9 shape:
 * `read("/dev/reactor/state")` beats `parse(read(".../status"))["state"]`. */
VNode *vfs_mkfield(Vfs *fs, const char *path, DevRead rd, int id, int src);
VNode *vfs_lookup(Vfs *fs, const char *path);
IoStatus vfs_read (Vfs *fs, const char *path, Buf *out);
IoStatus vfs_write(Vfs *fs, const char *path, const char *data, size_t len);
IoStatus vfs_list (Vfs *fs, const char *path, Buf *out);  /* one name per line */
bool   vfs_remove(Vfs *fs, const char *path);
/* Graft `target` at `path`. Reads and writes on `path` are performed on
 * `target`, so a script that only knows /dev/scrubber does not care that it is
 * really /n/wreck/dev/thm-04. This is the whole point of the namespace. */
VNode *vfs_bind(Vfs *fs, const char *target, const char *path);
VNode *vfs_symlink(Vfs *fs, const char *target, const char *path);
/* Resolve through symlinks and binds. Returns NULL for a dangling link, and
 * sets *dangling so the caller can tell "missing" from "points at nothing". */
VNode *vfs_resolve(Vfs *fs, const char *path, bool *dangling);
void   vfs_normalize(const char *cwd, const char *in, char *out, size_t outsz);

/* ------------------------------------------------------------------ lang */
typedef struct Chunk Chunk;
typedef struct Func  Func;
typedef struct Prog  Prog;

Prog *prog_compile(const char *src, const char *name, char *err, size_t errsz);
void  prog_free(Prog *p);

typedef enum {
    VM_OK,        /* program ran to completion */
    VM_YIELD,     /* instruction budget exhausted this tick */
    VM_BLOCKED,   /* suspended on a blocking device read */
    VM_SLEEP,     /* explicitly waiting until vm->wake_tick */
    VM_ERROR      /* runtime fault; vm->err set */
} VmStatus;

VM   *vm_new(Prog *p, Vfs *fs, Sim *sim);
void  vm_free(VM *v);
VmStatus vm_run(VM *v, int budget);
const char *vm_err(VM *v);
uint64_t vm_steps(VM *v);
uint64_t vm_wake_tick(VM *v);
const char *vm_blocked_on(VM *v);
int   vm_line(VM *v);

/* ------------------------------------------------------------------- sim */
#include "parts.h"
#include "sim.h"

/* ----------------------------------------------------------------- shell */
typedef struct Shell Shell;
Shell *shell_new(Sim *sim);
void   shell_free(Shell *sh);
/* Execute one command line; append protocol response to `out`. Returns false
 * if the session should close. */
bool   shell_exec(Shell *sh, const char *line, Buf *out);
const char *shell_cwd(Shell *sh);

/* ------------------------------------------------------------------- net */
int  net_serve(Sim *sim, int port, bool verbose);

#endif /* NOM_H */
