/* abi.h — the syscall ABI of our machine.
 *
 * Shared verbatim by the host (which implements these) and by every guest
 * program (which calls them). One file so the two can never drift apart.
 *
 * This list IS the sandbox. A guest program has no other channel to the
 * outside world: no host filesystem, no clock, no entropy, no network except
 * through what is here. Keeping it small is a security property and a
 * determinism property at the same time.
 *
 * Numbers below 512 follow the Linux rv64 convention, so an off-the-shelf
 * toolchain's idea of write/read/exit lines up. Everything from 1024 is ours,
 * because D18 says we define the platform.
 */
#ifndef NOM_ABI_H
#define NOM_ABI_H

/* Freestanding guests have no <stdint.h>. Both sides need the same widths, so
 * they are stated once here rather than assumed twice. */
#ifdef NOM_GUEST
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long               int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;
#else
#include <stdint.h>
#endif

#define SYS_read       63
#define SYS_write      64
#define SYS_close      57
#define SYS_exit       93

#define SYS_open     1024   /* (path, flags) -> fd                          */
#define SYS_readdir  1025   /* (path, index, buf, len) -> name length or -1 */
#define SYS_stat     1026   /* (path, statbuf) -> 0 or -1                   */
#define SYS_spawn    1027   /* (path, argptr) -> exit code, or negative     */
#define SYS_getarg   1028   /* (buf, len) -> length of our argument         */
#define SYS_getpid   1029   /* () -> pid                                    */
#define SYS_bind     1030   /* (target, at) -> 0 or -1  (plan 9 bind)       */
#define SYS_unbind   1031   /* (at) -> 0 or -1                              */
#define SYS_chdir    1032   /* (path) -> 0 or -1                            */
#define SYS_getcwd   1033   /* (buf, len) -> length                         */
#define SYS_repo     1034   /* (pkg, path, buf, len) -> bytes of the file as
                             * the package SHIPPED it. This is the machine
                             * talking to a package repository, which is off
                             * the machine -- that is why reinstall can fix a
                             * disk that has nothing good left on it. */
#define SYS_chmod    1035   /* (path, mode) -> 0 or -1                      */
#define SYS_mount    1036   /* (dev, at, flags) -> 0 or -1                  */
#define SYS_umount   1037   /* (at) -> 0 or -1                              */
#define SYS_chroot   1038   /* (path) -> 0 or -1                            */
#define SYS_mounts   1039   /* (buf, len) -> bytes: the mount table         */
#define SYS_readlink 1040   /* (path, buf, len) -> length of the target     */

#define MNT_BIND  1         /* `at` is another path, not a device           */

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0100
#define O_TRUNC   01000
#define O_APPEND  02000

/* The layout SYS_stat fills in. Fixed-width and explicitly padded, because
 * host and guest are compiled by different compilers for different machines
 * and a layout disagreement here is a silent memory bug. */
#define NOM_KIND_FILE  1
#define NOM_KIND_DIR   2
#define NOM_KIND_LINK  3
#define NOM_KIND_DEV   4

typedef struct {
    int64_t  size;
    int32_t  mode;      /* low 9 bits, as usual */
    int32_t  kind;      /* NOM_KIND_*           */
} NomStat;

/* Spawn failures are distinguishable, because "the binary is corrupt" and
 * "the binary is missing" are different tickets. */
#define SPAWN_ENOENT   (-1)   /* no such file                        */
#define SPAWN_EPERM    (-2)   /* not executable                      */
#define SPAWN_ENOEXEC  (-3)   /* not a program this machine can run  */
#define SPAWN_EFAULT   (-4)   /* it ran and trapped                  */
#define SPAWN_EDEPTH   (-5)   /* nested too deep                     */

#endif /* NOM_ABI_H */
