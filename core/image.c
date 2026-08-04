/* image.c — the installed system, modelled on Hamnix.
 *
 * Two rules govern this table.
 *
 * 1. WIDE. Diagnosis is only a skill if there is somewhere to look. A system
 *    with six files means the player checks all six; a system with sixty means
 *    they have to reason about which ones matter. The width is the game.
 *
 * 2. REAL. Everything from /sbin/init upward is an actual program in the
 *    system's own language, executed by the VM. /etc/rc.boot is not a
 *    description of what booting does — it is what booting does. Corrupt it
 *    and the interpreter fails on the damaged line.
 *
 * Layout follows Hamnix: /etc/inittab names what PID 1 runs, /etc/rc.boot is
 * the bootstrap rc, /etc/rc.d/rc.N are the runlevels, and the .svc files
 * under /etc/services.d are the services.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"
#include "guestbin.h"

#define ROOT_UUID "8f41-2c07-a19d-5be3"

/* ------------------------------------------------------------ userland --
 * The scripts below are interpreted by /bin/rc, which is a COMPILED PROGRAM
 * running on our cpu. The binaries themselves live in guestbin.h. Both are
 * real files on the disk: a corrupted script fails in rc's parser, a
 * corrupted binary fails in the ELF loader or traps mid-execution.
 */

static const char *SRC_RCBOOT =
"# /etc/rc.boot -- the bootstrap rc, run by pid 1.\n"
"# Brings the filesystems online and enters the default runlevel.\n"
"echo rc.boot: bootstrap rc starting\n"
"need /sbin/svcinit\n"
"mount none /proc\n"
"mount none /tmp\n"
"run /etc/rc.d/rc.3\n";

static const char *SRC_RC3 =
"# /etc/rc.d/rc.3 -- multi-user runlevel.\n"
"echo rc.3: entering multi-user\n"
"exec /sbin/svcinit 3\n"
"exec /sbin/login\n";

static const char *SRC_RC0 =
"# /etc/rc.d/rc.0 -- halt.\n"
"echo rc.0: system halted\n";

/* ------------------------------------------------------------- packages -- */

static const Package PKG_BOOTLOADER = {
    "zbl", "2.06", "the bootloader",
    {
      { "/boot/zbl/zbl.cfg", NULL, 0644, NULL },
      { "/usr/sbin/zbl-install",  "#!zbl-install\n",  0755, NULL },
      { "/usr/sbin/zbl-mkconfig", "#!zbl-mkconfig\n", 0755, NULL },
    }, 3
};

static const Package PKG_KERNEL = {
    "kernel-default", "6.4.11", "the kernel and its initrd",
    {
      { "/boot/vmlinuz-6.4.11", "\x7fKRNL 6.4.11 x86_64\n", 0644, NULL },
      { "/boot/vmlinuz", NULL, 0777, "/boot/vmlinuz-6.4.11" },
      { "/boot/initrd-6.4.11",
        "\x7fINITRD 6.4.11\n"
        "module virtio_blk\n"
        "module ext4\n"
        "module dm_mod\n", 0644, NULL },
      { "/boot/initrd", NULL, 0777, "/boot/initrd-6.4.11" },
      { "/usr/bin/mkinitrd", "#!mkinitrd\n", 0755, NULL },
      { "/lib/modules/6.4.11/virtio_blk.ko", "\x7fMOD virtio_blk\n", 0644, NULL },
      { "/lib/modules/6.4.11/ext4.ko",       "\x7fMOD ext4\n",       0644, NULL },
      { "/lib/modules/6.4.11/dm_mod.ko",     "\x7fMOD dm_mod\n",     0644, NULL },
      { "/lib/modules/6.4.11/e1000.ko",      "\x7fMOD e1000\n",      0644, NULL },
      { "/lib/modules/6.4.11/loop.ko",       "\x7fMOD loop\n",       0644, NULL },
    }, 10
};

/* The userland that actually runs. */
static const Package PKG_SYSINIT = {
    "sysinit", "254", "pid 1, the rc scripts and the runlevels",
    {
      { "/usr/lib/sysinit/init", NULL, 0755, NULL },   /* GUEST_INIT */
      { "/sbin/init", NULL, 0777, "/usr/lib/sysinit/init" },
      { "/sbin/svcinit", NULL, 0755, NULL },           /* GUEST_SVCINIT */
      { "/sbin/login",   NULL, 0755, NULL },           /* GUEST_LOGIN   */
      { "/etc/inittab",
        "# /etc/inittab -- the last non-comment line is run by /sbin/init.\n"
        "/bin/rc /etc/rc.boot\n", 0644, NULL },
      { "/etc/rc.boot",   NULL, 0755, NULL },          /* SRC_RCBOOT */
      { "/etc/rc.d/rc.3", NULL, 0755, NULL },          /* SRC_RC3 */
      { "/etc/rc.d/rc.0", NULL, 0755, NULL },          /* SRC_RC0 */
      { "/etc/rc.conf", "3\n", 0644, NULL },
    }, 9
};

static const Package PKG_BASE = {
    "filesystem", "84.87", "the base layout and system identity",
    {
      { "/etc/fstab",    NULL, 0644, NULL },
      { "/etc/hostname", NULL, 0644, NULL },
      { "/etc/os-release",
        "NAME=\"Hamnix\"\nVERSION=\"11.4\"\nID=hamnix\n"
        "PRETTY_NAME=\"Hamnix 11.4\"\n", 0644, NULL },
      { "/etc/lsb-release",
        "DISTRIB_ID=Hamnix\nDISTRIB_RELEASE=11.4\n", 0644, NULL },
      { "/etc/issue", "Hamnix 11.4\n", 0644, NULL },
      { "/etc/motd",  "Welcome to Hamnix.\n", 0644, NULL },
      { "/etc/shells", "/bin/hamsh\n", 0644, NULL },
      { "/etc/profile", "# login shell profile\nPATH=/bin:/usr/bin:/sbin\n", 0644, NULL },
    }, 8
};

static const Package PKG_USERS = {
    "shadow", "4.13", "accounts",
    {
      { "/etc/passwd",
        "root:x:0:0:root:/root:/bin/hamsh\n"
        "daemon:x:1:1:daemon:/:/bin/false\n"
        "hamowner:x:1000:1000:host owner:/home/hamowner:/bin/hamsh\n", 0644, NULL },
      { "/etc/group", "root:x:0:\ndaemon:x:1:\nhamowner:x:1000:\n", 0644, NULL },
      { "/etc/shadow", "root:!:19000:0:99999:7:::\n", 0600, NULL },
      { "/etc/login.defs", "UID_MIN 1000\nUID_MAX 60000\n", 0644, NULL },
    }, 4
};

static const Package PKG_NET = {
    "netcfg", "11.6", "network configuration and daemon",
    {
      { "/usr/sbin/netd", "#!netd\n", 0755, NULL },
      { "/etc/services.d/net.svc",
        "# /etc/services.d/net.svc\n"
        "name: net\n"
        "critical: yes\n"
        "exec: /usr/sbin/netd\n"
        "description: network interfaces\n"
        "after: syslog\n"
        "restart: on-failure\n"
        "enabled: yes\n"
        "runlevel: 3\n", 0644, NULL },
      { "/etc/net/interfaces", "iface eth0\n  address dhcp\n", 0644, NULL },
      { "/etc/hosts", "127.0.0.1 localhost\n", 0644, NULL },
      { "/etc/resolv.conf", "nameserver 10.0.2.3\n", 0644, NULL },
      { "/etc/host.conf", "order hosts,bind\n", 0644, NULL },
      { "/etc/networks", "default 0.0.0.0\n", 0644, NULL },
      { "/etc/protocols", "ip 0 IP\ntcp 6 TCP\nudp 17 UDP\n", 0644, NULL },
      { "/etc/services", "ssh 22/tcp\nhttp 80/tcp\n", 0644, NULL },
    }, 9
};

static const Package PKG_SYSLOG = {
    "syslog", "2.4", "system logging",
    {
      { "/usr/sbin/syslogd", "#!syslogd\n", 0755, NULL },
      { "/etc/services.d/syslog.svc",
        "# /etc/services.d/syslog.svc\n"
        "name: syslog\n"
        "critical: yes\n"
        "exec: /usr/sbin/syslogd\n"
        "description: system logging\n"
        "after: udev\n"
        "restart: on-failure\n"
        "enabled: yes\n"
        "runlevel: 3\n", 0644, NULL },
      { "/etc/syslog.conf", "*.info /var/log/messages\n", 0644, NULL },
    }, 3
};

static const Package PKG_UDEV = {
    "udev", "254", "device node management",
    {
      { "/usr/sbin/udevd", "#!udevd\n", 0755, NULL },
      { "/etc/services.d/udev.svc",
        "# /etc/services.d/udev.svc\n"
        "name: udev\n"
        "critical: yes\n"
        "exec: /usr/sbin/udevd\n"
        "description: device manager\n"
        "restart: on-failure\n"
        "enabled: yes\n"
        "runlevel: 3\n", 0644, NULL },
      { "/etc/udev/rules.d/50-default.rules", "SUBSYSTEM==\"block\", MODE=\"0660\"\n", 0644, NULL },
    }, 3
};

static const Package PKG_SSH = {
    "openssh", "9.4", "remote login",
    {
      { "/usr/sbin/sshd", "#!sshd\n", 0755, NULL },
      { "/etc/services.d/sshd.svc",
        "# /etc/services.d/sshd.svc\n"
        "name: sshd\n"
        "exec: /usr/sbin/sshd\n"
        "description: remote login\n"
        "after: net\n"
        "restart: on-failure\n"
        "enabled: yes\n"
        "runlevel: 3\n", 0644, NULL },
      { "/etc/ssh/sshd_config", "Port 22\nPermitRootLogin no\n", 0644, NULL },
    }, 3
};

static const Package PKG_HAMDE = {
    "hamde", "3.1", "the desktop",
    {
      { "/usr/bin/hamde", "#!hamde\n", 0755, NULL },
      { "/etc/services.d/hamde.svc",
        "# /etc/services.d/hamde.svc\n"
        "name: hamde\n"
        "exec: /usr/bin/hamde\n"
        "description: desktop panel\n"
        "after: net\n"
        "restart: on-failure\n"
        "enabled: yes\n"
        "runlevel: 5\n", 0644, NULL },
      { "/etc/hamde/panel.conf", "position=bottom\nheight=28\n", 0644, NULL },
      { "/etc/hamde/desktop.icons", "Terminal\nFiles\n", 0644, NULL },
    }, 4
};

static const Package PKG_SHELL = {
    "hamsh", "1.9", "the shell and the base tools",
    {
      { "/bin/rc",    NULL, 0755, NULL },
      { "/bin/sh",    NULL, 0755, NULL },
      { "/bin/ls",    NULL, 0755, NULL },
      { "/bin/cat",   NULL, 0755, NULL },
      { "/bin/ps",    NULL, 0755, NULL },
      { "/bin/ns",    NULL, 0755, NULL },
      { "/bin/stat",  NULL, 0755, NULL },
      { "/bin/chmod", NULL, 0755, NULL },
      { "/usr/bin/pkg", NULL, 0755, NULL },
      { "/bin/false", "#!false\n", 0755, NULL },
      { "/bin/true",  "#!true\n",  0755, NULL },
    }, 11
};

static const Package *IMAGE[] = {
    &PKG_BASE, &PKG_USERS, &PKG_BOOTLOADER, &PKG_KERNEL, &PKG_SYSINIT,
    &PKG_SHELL, &PKG_UDEV, &PKG_SYSLOG, &PKG_NET, &PKG_SSH, &PKG_HAMDE,
};
#define IMAGE_N ((int)(sizeof IMAGE / sizeof IMAGE[0]))

/* Content that belongs to a package but names THIS installation, plus the
 * userland sources, which live in C string literals but are files on the disk
 * in every sense that matters: they are read, compiled and executed from
 * there, and `pkg reinstall` restores them from here. */
void image_generated(const Machine *m, const char *path, Buf *out)
{
    if (strcmp(path, "/usr/lib/sysinit/init") == 0)
        buf_put(out, (const char *)GUEST_INIT, GUEST_INIT_LEN);
    else if (strcmp(path, "/bin/rc") == 0)
        buf_put(out, (const char *)GUEST_RC, GUEST_RC_LEN);
    else if (strcmp(path, "/bin/sh") == 0)
        buf_put(out, (const char *)GUEST_SH, GUEST_SH_LEN);
    else if (strcmp(path, "/bin/ls") == 0)
        buf_put(out, (const char *)GUEST_LS, GUEST_LS_LEN);
    else if (strcmp(path, "/bin/cat") == 0)
        buf_put(out, (const char *)GUEST_CAT, GUEST_CAT_LEN);
    else if (strcmp(path, "/bin/ps") == 0)
        buf_put(out, (const char *)GUEST_PS, GUEST_PS_LEN);
    else if (strcmp(path, "/bin/ns") == 0)
        buf_put(out, (const char *)GUEST_NS, GUEST_NS_LEN);
    else if (strcmp(path, "/bin/stat") == 0)
        buf_put(out, (const char *)GUEST_STAT, GUEST_STAT_LEN);
    else if (strcmp(path, "/bin/chmod") == 0)
        buf_put(out, (const char *)GUEST_CHMOD, GUEST_CHMOD_LEN);
    else if (strcmp(path, "/usr/bin/pkg") == 0)
        buf_put(out, (const char *)GUEST_PKG, GUEST_PKG_LEN);
    else if (strcmp(path, "/sbin/svcinit") == 0)
        buf_put(out, (const char *)GUEST_SVCINIT, GUEST_SVCINIT_LEN);
    else if (strcmp(path, "/sbin/login") == 0)
        buf_put(out, (const char *)GUEST_LOGIN, GUEST_LOGIN_LEN);
    else if (strcmp(path, "/etc/rc.boot") == 0)       buf_puts(out, SRC_RCBOOT);
    else if (strcmp(path, "/etc/rc.d/rc.3") == 0)     buf_puts(out, SRC_RC3);
    else if (strcmp(path, "/etc/rc.d/rc.0") == 0)     buf_puts(out, SRC_RC0);
    else if (strcmp(path, "/boot/zbl/zbl.cfg") == 0) {
        buf_puts(out, "default 0\ntimeout 5\n\n");
        buf_puts(out, "entry \"Hamnix 11.4\"\n");
        buf_puts(out, "  kernel /boot/vmlinuz\n");
        buf_puts(out, "  initrd /boot/initrd\n");
        buf_puts(out, "  root UUID=");
        buf_puts(out, m->root_uuid);
        buf_puts(out, "\n");
    } else if (strcmp(path, "/etc/fstab") == 0) {
        buf_puts(out, "# device                        mount  type  options\n");
        buf_puts(out, "UUID=");
        buf_puts(out, m->root_uuid);
        buf_puts(out, "  /      ext4  defaults\n");
        buf_puts(out, "none                            /proc  proc  defaults\n");
        buf_puts(out, "none                            /tmp   tmpfs defaults\n");
    } else if (strcmp(path, "/etc/hostname") == 0) {
        buf_puts(out, "node-");
        buf_puts(out, m->id);
        buf_puts(out, "\n");
    }
}

static void pristine(const Machine *m, const PkgFile *f, Buf *out);

static void install_file(Machine *m, const PkgFile *f)
{
    if (f->link) { vfs_symlink(&m->disk, f->link, f->path); return; }
    if (f->content) {
        VNode *n = vfs_mkfile(&m->disk, f->path, f->content);
        if (n) n->mode = f->mode;
        return;
    }
    /* Generated content may be a BINARY (the guest programs are ELF images
     * with embedded NULs), so it is written by length, never as a C string.
     * Going through vfs_mkfile's char* would truncate every binary at its
     * first zero byte. */
    Buf b = {0};
    image_generated(m, f->path, &b);
    VNode *n = vfs_mkfile(&m->disk, f->path, "");
    if (n) {
        buf_clear(&n->data);
        buf_put(&n->data, b.p, b.len);
        n->mode = f->mode;
    }
    buf_free(&b);
}

/* FNV-1a, the same hash /usr/bin/pkg computes on the guest side. If these two
 * ever disagree, verify reports a clean machine as broken -- so they are the
 * same three lines, deliberately trivial. */
static uint64_t fnv1a(const char *p, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211ULL; }
    return h;
}

/* The package database, written onto the machine's own disk at
 * /var/lib/pkg/<name>/{version,files}. It is real data that a real program
 * reads -- which also means it can be damaged, and `pkg verify` says so
 * rather than reporting a clean system. */
static void install_pkgdb(Machine *m)
{
    for (int i = 0; i < m->npkg; i++) {
        const Package *p = m->pkg[i];
        char dir[NOM_PATH_MAX], fp[NOM_PATH_MAX];
        snprintf(dir, sizeof dir, "/var/lib/pkg/%s", p->name);
        vfs_mkdir(&m->disk, dir);

        snprintf(fp, sizeof fp, "%s/version", dir);
        char ver[128];
        snprintf(ver, sizeof ver, "%s  %s\n", p->version, p->desc);
        VNode *vn = vfs_mkfile(&m->disk, fp, ver);
        if (vn) vn->mode = 0644;

        Buf man = {0};
        for (int j = 0; j < p->nfiles; j++) {
            const PkgFile *f = &p->file[j];
            if (f->link) continue;      /* a symlink has no content to hash */
            Buf c = {0};
            pristine(m, f, &c);
            buf_printf(&man, "%04o %016llx %s\n", f->mode,
                       (unsigned long long)fnv1a(c.p, c.len), f->path);
            buf_free(&c);
        }
        snprintf(fp, sizeof fp, "%s/files", dir);
        VNode *fn = vfs_mkfile(&m->disk, fp, "");
        if (fn) {
            buf_clear(&fn->data);
            buf_put(&fn->data, man.p, man.len);
            fn->mode = 0644;
        }
        buf_free(&man);
    }
}

void machine_install(Machine *m, uint64_t seed)
{
    memset(m, 0, sizeof *m);
    vfs_init(&m->disk);
    snprintf(m->id, sizeof m->id, "%llu", (unsigned long long)(seed % 10000));
    snprintf(m->root_uuid, sizeof m->root_uuid, "%s", ROOT_UUID);
    m->bootsector = true;

    static const char *DIRS[] = {
        "/bin", "/boot", "/boot/zbl", "/dev", "/etc", "/etc/hamde",
        "/etc/net", "/etc/rc.d", "/etc/services.d", "/etc/ssh", "/etc/udev",
        "/etc/udev/rules.d", "/home", "/home/hamowner", "/lib", "/lib/modules",
        "/lib/modules/6.4.11", "/proc", "/root", "/sbin", "/tmp", "/usr",
        "/usr/bin", "/usr/lib", "/usr/lib/sysinit", "/usr/sbin", "/usr/share",
        "/var", "/var/log", NULL
    };
    for (int i = 0; DIRS[i]; i++) vfs_mkdir(&m->disk, DIRS[i]);

    for (int i = 0; i < IMAGE_N && i < PKG_MAX; i++) {
        m->pkg[m->npkg++] = IMAGE[i];
        for (int j = 0; j < IMAGE[i]->nfiles; j++)
            install_file(m, &IMAGE[i]->file[j]);
    }
    install_pkgdb(m);
    m->next_pid = 1;
}

void machine_free(Machine *m)
{
    vfs_free(&m->disk);
    buf_free(&m->boot.console);
}

/* --- the package database -------------------------------------------- */

const Package *pkg_find(const Machine *m, const char *name)
{
    for (int i = 0; i < m->npkg; i++)
        if (strcmp(m->pkg[i]->name, name) == 0) return m->pkg[i];
    return NULL;
}

const Package *pkg_owns(const Machine *m, const char *path)
{
    for (int i = 0; i < m->npkg; i++)
        for (int j = 0; j < m->pkg[i]->nfiles; j++)
            if (strcmp(m->pkg[i]->file[j].path, path) == 0) return m->pkg[i];
    return NULL;
}

static void pristine(const Machine *m, const PkgFile *f, Buf *out)
{
    if (f->content) buf_puts(out, f->content);
    else            image_generated(m, f->path, out);
}

static void verify_pkg(Machine *m, const Package *p, Buf *out, int *bad)
{
    for (int j = 0; j < p->nfiles; j++) {
        const PkgFile *f = &p->file[j];
        VNode *n = vfs_lookup(&m->disk, f->path);
        if (!n) { buf_printf(out, "%s missing\n", f->path); (*bad)++; continue; }
        if (f->link) {
            if (n->kind != VN_LINK || strcmp(n->target, f->link) != 0) {
                buf_printf(out, "%s changed\n", f->path); (*bad)++;
            }
            continue;
        }
        if (n->kind != VN_FILE) { buf_printf(out, "%s changed\n", f->path); (*bad)++; continue; }
        Buf want = {0};
        pristine(m, f, &want);
        bool differs = (want.len != n->data.len) ||
                       (want.len && memcmp(want.p, n->data.p, want.len) != 0);
        buf_free(&want);
        if (differs)                 { buf_printf(out, "%s changed\n", f->path); (*bad)++; }
        else if (n->mode != f->mode) { buf_printf(out, "%s mode\n",    f->path); (*bad)++; }
    }
}

void pkg_verify(Machine *m, const char *name, Buf *out)
{
    int bad = 0;
    if (name) {
        const Package *p = pkg_find(m, name);
        if (!p) { buf_printf(out, "no such package: %s\n", name); return; }
        verify_pkg(m, p, out, &bad);
    } else {
        for (int i = 0; i < m->npkg; i++) verify_pkg(m, m->pkg[i], out, &bad);
    }
    if (bad == 0) buf_puts(out, "all files match their packages\n");
}

bool pkg_file_content(const Machine *m, const char *pkgname, const char *path,
                      Buf *out)
{
    const Package *p = pkg_find(m, pkgname);
    if (!p) return false;
    for (int j = 0; j < p->nfiles; j++) {
        if (strcmp(p->file[j].path, path) != 0) continue;
        if (p->file[j].link) return false;      /* a link is not content */
        pristine(m, &p->file[j], out);
        return true;
    }
    return false;
}

int pkg_reinstall(Machine *m, const char *name, Buf *out)
{
    const Package *p = pkg_find(m, name);
    if (!p) { buf_printf(out, "no such package: %s\n", name); return 0; }
    int n = 0;
    for (int j = 0; j < p->nfiles; j++) {
        vfs_remove(&m->disk, p->file[j].path);
        install_file(m, &p->file[j]);
        n++;
    }
    buf_printf(out, "%s-%s: %d files restored\n", p->name, p->version, n);
    return n;
}
