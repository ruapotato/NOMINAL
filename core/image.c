/* image.c — the base installation, defined as packages.
 *
 * This is the machine the player inherits. Every file here is one a boot stage
 * actually reads, or a tool that actually runs. Nothing is set dressing: if a
 * file is in this table and no code reads it, delete it, because a file that
 * does nothing teaches the player that files do nothing.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"

/* The root filesystem's UUID. The bootloader names it, fstab names it, and the
 * initrd has to find it. Three places that must agree is three places a break
 * can hide, which is exactly why real systems break here. */
#define ROOT_UUID "8f41-2c07-a19d-5be3"

static const Package PKG_BOOTLOADER = {
    "zbl", "2.06", "the bootloader",
    {
      { "/boot/zbl/zbl.cfg", NULL, 0644, NULL },   /* content filled at install */
      { "/usr/sbin/zbl-install", "#!zbl-install\n", 0755, NULL },
      { "/usr/sbin/zbl-mkconfig", "#!zbl-mkconfig\n", 0755, NULL },
    }, 3
};

static const Package PKG_KERNEL = {
    "kernel-default", "6.4.11", "the kernel and its initrd",
    {
      { "/boot/vmlinuz-6.4.11",
        "\x7fKRNL 6.4.11 x86_64\n", 0644, NULL },
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
    }, 8
};

static const Package PKG_INIT = {
    "sysinit", "254", "pid 1 and the service manager",
    {
      { "/usr/lib/sysinit/sysinit", "#!sysinit\n", 0755, NULL },
      { "/sbin/init", NULL, 0777, "/usr/lib/sysinit/sysinit" },
      { "/usr/bin/svc", "#!svc\n", 0755, NULL },
      { "/etc/init/mount-local.service",
        "description=mount local filesystems\n"
        "exec=/usr/sbin/mount-all\n"
        "required=1\n", 0644, NULL },
      { "/etc/init/syslog.service",
        "description=system logging\n"
        "after=mount-local\n"
        "exec=/usr/sbin/syslogd\n"
        "required=1\n", 0644, NULL },
      { "/etc/init/network.service",
        "description=network interfaces\n"
        "after=syslog\n"
        "exec=/usr/sbin/netd\n"
        "required=1\n", 0644, NULL },
      { "/etc/init/sshd.service",
        "description=remote login\n"
        "after=network\n"
        "exec=/usr/sbin/sshd\n"
        "required=0\n", 0644, NULL },
      { "/usr/sbin/mount-all", "#!mount-all\n", 0755, NULL },
      { "/usr/sbin/syslogd",   "#!syslogd\n",   0755, NULL },
    }, 9
};

static const Package PKG_BASE = {
    "filesystem", "84.87", "the directory layout and fstab",
    {
      { "/etc/fstab", NULL, 0644, NULL },     /* content filled at install */
      { "/etc/hostname", NULL, 0644, NULL },  /* content filled at install */
      { "/etc/os-release",
        "NAME=\"Nominal Linux\"\nVERSION=\"11.4\"\nID=nominal\n", 0644, NULL },
    }, 3
};

static const Package PKG_NET = {
    "netcfg", "11.6", "network daemon and its configuration",
    {
      { "/usr/sbin/netd", "#!netd\n", 0755, NULL },
      { "/etc/net/interfaces",
        "iface eth0\n  address dhcp\n", 0644, NULL },
    }, 2
};

static const Package PKG_SSH = {
    "openssh", "9.4", "remote login",
    {
      { "/usr/sbin/sshd", "#!sshd\n", 0755, NULL },
      { "/etc/ssh/sshd_config", "Port 22\nPermitRootLogin no\n", 0644, NULL },
    }, 2
};

static const Package *IMAGE[] = {
    &PKG_BASE, &PKG_BOOTLOADER, &PKG_KERNEL, &PKG_INIT, &PKG_NET, &PKG_SSH,
};
#define IMAGE_N ((int)(sizeof IMAGE / sizeof IMAGE[0]))

/* Content that depends on the machine rather than the package: the bootloader
 * config, fstab and hostname all name THIS installation. Kept out of the
 * static table so the package database still knows they belong to a package —
 * which matters, because `pkg reinstall` has to regenerate them. */
void image_generated(const Machine *m, const char *path, Buf *out)
{
    if (strcmp(path, "/boot/zbl/zbl.cfg") == 0) {
        buf_puts(out, "default 0\ntimeout 5\n\n");
        buf_puts(out, "entry \"Nominal Linux 11.4\"\n");
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
        buf_puts(out, "/dev/sda2                       /var   ext4  defaults\n");
    } else if (strcmp(path, "/etc/hostname") == 0) {
        buf_puts(out, "node-");
        buf_puts(out, m->id);
        buf_puts(out, "\n");
    }
}

static void install_file(Machine *m, const PkgFile *f)
{
    if (f->link) {
        vfs_symlink(&m->disk, f->link, f->path);
        return;
    }
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

    Rng r; rng_seed(&r, seed);
    /* The id is the seed. A machine IS its seed: that is what makes a broken
     * one shareable. */
    snprintf(m->id, sizeof m->id, "%llu", (unsigned long long)(seed % 10000));
    snprintf(m->root_uuid, sizeof m->root_uuid, "%s", ROOT_UUID);
    m->bootsector = true;

    /* The directories a running system has whether or not a package owns
     * them. Owning every directory would make `pkg verify` noise. */
    static const char *DIRS[] = {
        "/boot", "/boot/zbl", "/etc", "/etc/init", "/etc/net", "/etc/ssh",
        "/usr", "/usr/bin", "/usr/sbin", "/usr/lib", "/usr/lib/sysinit",
        "/lib", "/lib/modules", "/lib/modules/6.4.11",
        "/sbin", "/var", "/var/log", "/tmp", "/root", "/dev", NULL
    };
    for (int i = 0; DIRS[i]; i++) vfs_mkdir(&m->disk, DIRS[i]);

    for (int i = 0; i < IMAGE_N && i < PKG_MAX; i++) {
        m->pkg[m->npkg++] = IMAGE[i];
        for (int j = 0; j < IMAGE[i]->nfiles; j++)
            install_file(m, &IMAGE[i]->file[j]);
    }
    (void)r;
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

/* What this file should contain, generated or static. */
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
        if (!n) {
            buf_printf(out, "%s missing\n", f->path);
            (*bad)++;
            continue;
        }
        if (f->link) {
            if (n->kind != VN_LINK || strcmp(n->target, f->link) != 0) {
                buf_printf(out, "%s changed\n", f->path);
                (*bad)++;
            }
            continue;
        }
        if (n->kind != VN_FILE) {
            buf_printf(out, "%s changed\n", f->path);
            (*bad)++;
            continue;
        }
        Buf want = {0};
        pristine(m, f, &want);
        bool differs = (want.len != n->data.len) ||
                       (want.len && memcmp(want.p, n->data.p, want.len) != 0);
        buf_free(&want);
        if (differs)            { buf_printf(out, "%s changed\n", f->path); (*bad)++; }
        else if (n->mode != f->mode) { buf_printf(out, "%s mode\n", f->path);  (*bad)++; }
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
        const PkgFile *f = &p->file[j];
        vfs_remove(&m->disk, f->path);
        install_file(m, f);
        n++;
    }
    buf_printf(out, "%s-%s: %d files restored\n", p->name, p->version, n);
    return n;
}
