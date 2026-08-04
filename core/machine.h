/* machine.h — an installed system, its package database, and its boot chain.
 *
 * D17. The rule this file exists to enforce: the boot is SIMULATED, not
 * narrated. Every stage reads real files out of the machine's own Vfs and
 * fails because of what it finds there. There is no fault id anywhere in this
 * header, and there must never be one, because the moment a stage asks "which
 * fault is active?" instead of "what does this file say?", the game collapses
 * back into a symptom table.
 */
#ifndef NOM_MACHINE_H
#define NOM_MACHINE_H

#define PKG_MAX        24
#define PKGFILE_MAX    24
#define UNIT_MAX       16
#define CONSOLE_MAX    120

/* A file as its package shipped it. `hash` is of the pristine content, so a
 * corrupted file is detectable without keeping a second copy of the tree —
 * except that we DO keep the content, because `pkg reinstall` has to put the
 * original back and a hash cannot do that. */
typedef struct {
    const char *path;
    const char *content;   /* NULL for a directory */
    unsigned    mode;
    const char *link;      /* non-NULL: this entry is a symlink to `link` */
} PkgFile;

typedef struct {
    const char *name;
    const char *version;
    const char *desc;
    PkgFile     file[PKGFILE_MAX];
    int         nfiles;
} Package;

/* Where a boot got to. The player reads these names constantly, so they are
 * the vocabulary of the whole game. */
typedef enum {
    BOOT_FIRMWARE,   /* find something to boot                     */
    BOOT_LOADER,     /* read the bootloader config                 */
    BOOT_KERNEL,     /* load the kernel image                      */
    BOOT_INITRD,     /* load initrd, find and mount the root fs    */
    BOOT_INIT,       /* hand over to /sbin/init                    */
    BOOT_SERVICES,   /* bring up units in dependency order         */
    BOOT_TARGET,     /* login prompt: the machine is up            */
    BOOT_STAGE_COUNT
} BootStage;

const char *boot_stage_name(BootStage s);

typedef struct {
    bool       running;      /* did the machine reach BOOT_TARGET */
    BootStage  reached;      /* furthest stage entered */
    BootStage  failed_at;    /* stage that stopped it (== reached on failure) */
    char       reason[160];  /* the machine's own words, never a diagnosis */
    Buf        console;      /* everything the boot printed, in order */
    int        emergency;    /* dropped to an emergency shell in the initrd */
} BootResult;

typedef struct {
    char  id[16];            /* "4823" — the seed, and the machine's name  */
    Vfs   disk;              /* the installed root filesystem              */
    char  root_uuid[40];     /* what the root partition actually IS        */
    bool  bootsector;        /* firmware can find something to chain to    */
    BootResult boot;
    /* The package database is a pointer into static image data plus a
     * per-machine record of what has been reinstalled. Packages never change,
     * so they do not need copying per machine. */
    const Package *pkg[PKG_MAX];
    int   npkg;
} Machine;

/* Build a pristine installation. Deterministic: same seed, same machine. */
void machine_install(Machine *m, uint64_t seed);
void machine_free(Machine *m);

/* Run the boot chain against whatever is on the disk right now. Pure function
 * of disk state — call it as often as you like. */
void machine_boot(Machine *m);

/* --- the package database, which is the fix verb ---------------------- */
const Package *pkg_find(const Machine *m, const char *name);
const Package *pkg_owns(const Machine *m, const char *path);
/* Files that differ from what their package shipped. One `path status` line
 * each: missing | changed | mode. */
void pkg_verify(Machine *m, const char *name_or_null, Buf *out);
/* Put a package's files back exactly as shipped. Returns files restored. */
int  pkg_reinstall(Machine *m, const char *name, Buf *out);

/* --- the breaker, which is the content generator ---------------------- */
/* Damage one random file one random way. Returns false if the mutation was a
 * no-op for that file, which the caller simply retries. */
bool machine_corrupt(Machine *m, Rng *r, char *what, size_t whatsz);

/* Break the machine: corrupt a fresh copy at random until it stops booting.
 * `nfaults` independent corruptions are left on the disk, so >1 gives faults
 * that mask each other. `what` is filled in FOR THE AUTHOR ONLY — it is how
 * the test harness reports what it was solving. The player never sees it, and
 * nothing in the boot chain is told. Returns false if no break was found in
 * the attempt budget, which should not happen. */
bool machine_break(Machine *m, uint64_t seed, int nfaults, char *what, size_t whatsz);

#endif /* NOM_MACHINE_H */
