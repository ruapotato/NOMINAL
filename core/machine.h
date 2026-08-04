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

#include "ns.h"

#define PKG_MAX        64
#define PKGFILE_MAX    48
#define UNIT_MAX       32
#define CONSOLE_MAX    120
#define PROC_MAX        32

/* A running program. The table lives in the Machine because the machine IS
 * the computer: /proc is a view of this, not of anything on the disk, which
 * is why corrupting the disk cannot fabricate a process. */
typedef struct {
    int      pid, ppid;
    char     name[64];
    char     arg[128];
    char     cwd[NOM_PATH_MAX];
    char     root[NOM_PATH_MAX];   /* chroot: what "/" means to this process */
    Ns       ns;
    bool     alive;
    int64_t  exit_code;
    uint64_t icount;
} ProcInfo;

/* A file as its package shipped it. `hash` is of the pristine content, so a
 * corrupted file is detectable without keeping a second copy of the tree —
 * except that we DO keep the content, because `pkg reinstall` has to put the
 * original back and a hash cannot do that. */
typedef struct {
    const char *path;
    const char *content;   /* NULL for a directory */
    unsigned    mode;
    const char *link;      /* non-NULL: this entry is a symlink to `link` */
    /* A DIRECTORY owned by the package. rpm and dpkg both record these, and
     * for the reason we found the hard way: a directory that goes missing, or
     * loses its execute bit, is a real fault with no file to blame, and a
     * package manager that cannot see it also cannot put it back. Appended
     * last so every existing positional initialiser keeps working. */
    bool        isdir;
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
    BOOT_LOGIN,      /* getty: is there an account to hand it to?  */
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

#define MOUNT_MAX 12

/* A mounted filesystem. `dev` is what you named when you mounted it, so
 * `mount` can print the table the way mount(8) does. */
typedef struct {
    char  at[NOM_PATH_MAX];
    char  dev[40];
    Vfs  *fs;
    char  sub[NOM_PATH_MAX];   /* a bind mount names a subtree, not a device */
    bool  used;
} Mount;

typedef struct {
    char  id[16];            /* "4823" — the seed, and the machine's name  */
    Vfs   disk;              /* the customer's installed system, /dev/sda1  */
    /* The rescue medium: a complete, separate, working system that is never
     * corrupted. Booting it is how you get a shell on a machine whose own
     * disk cannot produce one -- which is the whole point of a live image. */
    Vfs   rescue;
    bool  on_rescue;         /* which medium did we boot                    */
    /* An unclean shutdown leaves the filesystem marked dirty. Nothing will
     * mount it until fsck has been run, which is the point: the repair has to
     * happen BEFORE you can even look at the disk. */
    /* The disk has a size. Everything up to now could write forever, so a log
     * that grows was not a fault and could not become one -- and "the disk
     * filled up" is one of the commonest real causes there is. */
    uint64_t fs_capacity;
    bool  fs_dirty;
    int   fs_lost;           /* files fsck could not save                   */
    /* Which repository channel `pkg` pulls from. Read off the disk at
     * the .repo files under /etc/pkg/repos.d, so pointing it at the wrong
     * one is a configuration fault and the packages that arrive are
     * genuinely different. */
    char  channel[24];
    Mount mount[MOUNT_MAX];
    int   nmount;
    /* The root filesystem is not in the mount table -- it is the thing the
     * table is relative to -- so "mounted read-only" has to live here. Set by
     * /sbin/mountall when the fstab entry for / carries the ro option, which
     * is exactly the moment a real init decides whether to remount rw. */
    bool  root_ro;
    char  root_uuid[40];     /* what the root partition actually IS        */
    bool  bootsector;        /* firmware can find something to chain to    */
    BootResult boot;
    /* The package database is a pointer into static image data plus a
     * per-machine record of what has been reinstalled. Packages never change,
     * so they do not need copying per machine. */
    const Package *pkg[PKG_MAX];
    int   npkg;

    /* Legitimate local edits this machine's admin made. They show up in
     * `pkg verify` as CHANGED and they are NOT the fault -- reinstalling the
     * package destroys real work and usually creates a second problem. This
     * is what stops verify from being an oracle. */
    char  local[8][NOM_PATH_MAX];
    int   nlocal;
    /* What each local edit looked like when the machine arrived, so the bench
     * can tell the player afterwards which of the administrator's decisions
     * they reverted. Fixing the machine and quietly undoing somebody's work
     * is not the same as fixing the machine. */
    Buf   local_orig[8];

    /* The person whose machine it is. Briefed with ground truth from the
     * breaker, and unwilling to volunteer it. See customer.c. */
    struct {
        char truth[256];      /* exactly what the breaker did              */
        int  cause;           /* their version of it                       */
        int  mood;
        int  asked;
        char told[16];        /* topics already covered                    */
        bool deflected;       /* denied it once, as people do              */
        bool confessed;
        bool gave_password;
        /* The customer is also the pair of hands in the room. The technician
         * cannot press the power button; they have to ask. */
        bool at_machine;      /* are they sitting in front of it right now  */
        bool disc_inserted;   /* have they put the rescue medium in         */
        int  power_cycles;
    } cust;

    /* Daemons. A service that starts does not run to completion: it runs
     * until it blocks or its startup budget is spent, and then it STAYS
     * RUNNING, with its cpu and memory intact, for the rest of the boot.
     * That is what makes `ps` a picture of a live system rather than a
     * history, and it is what lets a service crash at 11am. */
    struct Daemon *daemon;
    int    ndaemon;

    ProcInfo proc[PROC_MAX];
    int      nproc;        /* high-water mark, so exited pids stay visible */
    int      next_pid;
} Machine;

/* Build a pristine installation. Deterministic: same seed, same machine. */
void machine_install(Machine *m, uint64_t seed);
void machine_free(Machine *m);

/* Run the boot chain against whatever is on the disk right now. Pure function
 * of disk state — call it as often as you like. */
void machine_boot(Machine *m);

/* Boot the rescue medium instead of the customer's disk. The rescue system is
 * a complete, separate installation that is never corrupted, so this always
 * gets you a shell -- which is the entire reason a live image exists. */
void machine_boot_rescue(Machine *m);

/* The customer. customer_brief is given the breaker's own description of what
 * it did -- that string is the ground truth the persona is working from, and
 * is exactly the brief an LLM backend would receive. */
void customer_brief(Machine *m, const char *what);
void customer_ask(Machine *m, const char *question, Buf *out);
void customer_intro(Machine *m, Buf *out);
/* Which local configuration decisions no longer survive. Returns how many. */
int machine_collateral(Machine *m, Buf *out);
/* Ask the customer to DO something. Returns false if the request was not
 * understood as an action, in which case it was a question. */
bool customer_do(Machine *m, const char *request, Buf *out);

bool machine_mount(Machine *m, const char *dev, const char *at, int flags);
/* Check and repair the filesystem. Clears the dirty flag; reports what it
 * could not save. Metadata is repairable, contents are not -- which is why a
 * dirty filesystem is usually two repairs, not one. */
int  machine_fsck(Machine *m, const char *dev, Buf *out);
void machine_read_channel(Machine *m);
/* Bytes in use on the customer's disk, counted from the tree. */
uint64_t machine_disk_used(const Machine *m);

/* Start a program as a long-lived service. Returns 0 if it is now running,
 * or a negative SPAWN_* if it could not be started at all. */
int64_t kernel_start_daemon(Machine *m, const char *path, const char *arg,
                            const char *name, int restart, Buf *console);
/* Let every running daemon have another slice of cpu. A daemon that exits or
 * faults during its slice has crashed, and says so. */
void kernel_tick(Machine *m, int slices, Buf *console);
void kernel_stop_daemons(Machine *m);
/* How many services that should be running are not, and which. A machine can
 * boot perfectly and still be broken; this is the difference. */
int  kernel_health(Machine *m, Buf *out);
bool machine_umount(Machine *m, const char *at);

/* --- the package database, which is the fix verb ---------------------- */
const Package *pkg_find(const Machine *m, const char *name);
const Package *pkg_owns(const Machine *m, const char *path);
/* Files that differ from what their package shipped. One `path status` line
 * each: missing | changed | mode. */
void pkg_verify(Machine *m, const char *name_or_null, Buf *out);
/* Put a package's files back exactly as shipped. Returns files restored. */
int  pkg_reinstall(Machine *m, const char *name, Buf *out);
/* Put ONE path back. Mutating; kept apart from pkg_file_content so that a
 * fetch can never change the machine. */
bool pkg_restore_path(Machine *m, const char *pkgname, const char *path);
/* The pristine bytes of one file, as its package shipped it. This is the
 * repository, and it deliberately lives OFF the machine: it is how a disk
 * with nothing good left on it can still be repaired. */
bool pkg_file_content(const Machine *m, const char *pkg, const char *path, Buf *out);

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
