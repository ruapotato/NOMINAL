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
 * the bootstrap rc, /etc/rc.d/rc.N are the runlevels, /etc/services.d/*.svc
 * are the services.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"

#define ROOT_UUID "8f41-2c07-a19d-5be3"

/* ------------------------------------------------------------ userland --
 * These are programs, not data. They run.
 */

/* PID 1. Exactly the Hamnix shape: a shim that reads inittab and execs the
 * last non-comment line. */
static const char *SRC_INIT =
"# /sbin/init -- pid 1.\n"
"# Reads /etc/inittab and execs the last non-comment line, which is what\n"
"# Hamnix's init2 does. Everything the machine becomes follows from here.\n"
"print(\"init: pid 1 starting\")\n"
"cmd = \"\"\n"
"for line in split(read(\"/etc/inittab\"), \"\\n\"):\n"
"    line = strip(line)\n"
"    if len(line) == 0:\n"
"        continue\n"
"    if startswith(line, \"#\"):\n"
"        continue\n"
"    cmd = line\n"
"if len(cmd) == 0:\n"
"    panic(\"init: /etc/inittab: nothing to run\")\n"
"exec(cmd)\n";

/* The bootstrap rc. Mounts what fstab asks for, then hands off to the
 * runlevel named in /etc/rc.conf. */
static const char *SRC_RCBOOT =
"# /etc/rc.boot -- the bootstrap rc, interpreted by pid 1.\n"
"# Brings the filesystems online and hands off to the runlevel rc.\n"
"print(\"rc.boot: bootstrap rc starting\")\n"
"\n"
"for line in split(read(\"/etc/fstab\"), \"\\n\"):\n"
"    line = strip(line)\n"
"    if len(line) == 0:\n"
"        continue\n"
"    if startswith(line, \"#\"):\n"
"        continue\n"
"    f = split(line)\n"
"    if len(f) < 3:\n"
"        panic(\"rc.boot: /etc/fstab: bad entry: \" + line)\n"
"    mount(f[0], f[1])\n"
"    print(\"rc.boot: mounted \" + f[0] + \" on \" + f[1])\n"
"\n"
"level = strip(read(\"/etc/rc.conf\"))\n"
"target = \"/etc/rc.d/rc.\" + level\n"
"if exists(target) == false:\n"
"    panic(\"rc.boot: \" + target + \": no such runlevel\")\n"
"print(\"rc.boot: entering runlevel \" + level)\n"
"exec(target)\n";

/* Runlevel 3: multi-user. Starts every unit in /etc/services.d in dependency
 * order, which it works out itself from the unit files. */
static const char *SRC_RC3 =
"# /etc/rc.d/rc.3 -- multi-user runlevel.\n"
"# Reads every unit in /etc/services.d, filters by `enabled` and `runlevel`,\n"
"# and starts what is left in dependency order.\n"
"\n"
"level = \"3\"\n"
"units = {}\n"
"for name in ls(\"/etc/services.d\"):\n"
"    if endswith(name, \".svc\") == false:\n"
"        continue\n"
"    u = parse(read(\"/etc/services.d/\" + name))\n"
"    if lookup(u, \"enabled\", \"yes\") != \"yes\":\n"
"        print(\"rc.3: \" + name + \": disabled\")\n"
"        continue\n"
"    on = str(lookup(u, \"runlevel\", \"3\"))\n"
"    want = false\n"
"    for r in split(on):\n"
"        if r == level:\n"
"            want = true\n"
"    if want == false:\n"
"        continue\n"
"    units[name] = u\n"
"\n"
"started = {}\n"
"for round in range(20):\n"
"    moved = 0\n"
"    for name in keys(units):\n"
"        if has(started, name):\n"
"            continue\n"
"        u = units[name]\n"
"        dep = lookup(u, \"after\", \"\")\n"
"        if len(dep) > 0:\n"
"            if has(started, dep + \".svc\") == false:\n"
"                continue\n"
"        run = lookup(u, \"exec\", \"\")\n"
"        if len(run) == 0:\n"
"            panic(\"rc.3: \" + name + \": no exec line\")\n"
"        svc(run)\n"
"        print(\"rc.3: started \" + lookup(u, \"name\", name) +\n"
"              \" -- \" + lookup(u, \"description\", \"\"))\n"
"        started[name] = \"up\"\n"
"        moved = 1\n"
"    if moved == 0:\n"
"        break\n"
"\n"
"for name in keys(units):\n"
"    if has(started, name):\n"
"        continue\n"
"    u = units[name]\n"
"    print(\"rc.3: \" + name + \": waiting for \" + lookup(u, \"after\", \"?\"))\n"
"    panic(\"rc.3: \" + name + \" never started\")\n"
"\n"
"print(\"\")\n"
"print(strip(read(\"/etc/issue\")))\n"
"print(strip(read(\"/etc/hostname\")) + \" login:\")\n";

static const char *SRC_RC0 =
"# /etc/rc.d/rc.0 -- halt.\n"
"print(\"rc.0: stopping services\")\n"
"print(\"rc.0: system halted\")\n";

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
      { "/usr/lib/sysinit/init", NULL, 0755, NULL },   /* SRC_INIT */
      { "/sbin/init", NULL, 0777, "/usr/lib/sysinit/init" },
      { "/etc/inittab",
        "# /etc/inittab -- the last non-comment line is exec'd by /sbin/init.\n"
        "/etc/rc.boot\n", 0644, NULL },
      { "/etc/rc.boot",   NULL, 0755, NULL },          /* SRC_RCBOOT */
      { "/etc/rc.d/rc.3", NULL, 0755, NULL },          /* SRC_RC3 */
      { "/etc/rc.d/rc.0", NULL, 0755, NULL },          /* SRC_RC0 */
      { "/etc/rc.conf", "3\n", 0644, NULL },
      { "/usr/bin/svc", "#!svc\n", 0755, NULL },
      { "/usr/sbin/mount-all", "#!mount-all\n", 0755, NULL },
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
      { "/bin/hamsh", "#!hamsh\n", 0755, NULL },
      { "/bin/ls",    "#!ls\n",    0755, NULL },
      { "/bin/cat",   "#!cat\n",   0755, NULL },
      { "/bin/echo",  "#!echo\n",  0755, NULL },
      { "/bin/mount", "#!mount\n", 0755, NULL },
      { "/bin/false", "#!false\n", 0755, NULL },
      { "/bin/true",  "#!true\n",  0755, NULL },
    }, 7
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
    if (strcmp(path, "/usr/lib/sysinit/init") == 0)   buf_puts(out, SRC_INIT);
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

static void install_file(Machine *m, const PkgFile *f)
{
    if (f->link) { vfs_symlink(&m->disk, f->link, f->path); return; }
    if (f->content) {
        VNode *n = vfs_mkfile(&m->disk, f->path, f->content);
        if (n) n->mode = f->mode;
        return;
    }
    Buf b = {0};
    image_generated(m, f->path, &b);
    buf_putc(&b, '\0');
    VNode *n = vfs_mkfile(&m->disk, f->path, b.p ? b.p : "");
    if (n) n->mode = f->mode;
    buf_free(&b);
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
