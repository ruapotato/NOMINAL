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
#include <stdlib.h>
#include "nom.h"
#include "machine.h"
#include "kernel.h"
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
"# /etc/fstab is the single source of truth for what gets mounted.\n"
"exec /sbin/mountall\n"
"run /etc/rc.d/rc.3\n";

static const char *SRC_RC3 =
"# /etc/rc.d/rc.3 -- multi-user runlevel.\n"
"echo rc.3: entering multi-user\n"
"exec /sbin/svcinit 3\n"
"exec /sbin/getty root\n";

static const char *SRC_RC0 =
"# /etc/rc.d/rc.0 -- halt.\n"
"echo rc.0: system halted\n";

/* ------------------------------------------------------------- packages -- */

static const Package PKG_BOOTLOADER = {
    "zbl", "2.06", "the bootloader",
    {
      { "/boot/zbl/zbl.cfg", NULL, 0644, NULL },
      { "/usr/sbin/zbl-install",  NULL, 0755, NULL },
      { "/usr/sbin/zbl-mkconfig", NULL, 0755, NULL },
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
      { "/usr/bin/mkinitrd", NULL, 0755, NULL },
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
      { "/sbin/getty",   NULL, 0755, NULL },           /* GUEST_GETTY   */
      { "/sbin/mountall", NULL, 0755, NULL },          /* GUEST_MOUNTALL */
      { "/etc/inittab",
        "# /etc/inittab -- the last non-comment line is run by /sbin/init.\n"
        "/bin/rc /etc/rc.boot\n", 0644, NULL },
      { "/etc/rc.boot",   NULL, 0755, NULL },          /* SRC_RCBOOT */
      { "/etc/rc.d/rc.3", NULL, 0755, NULL },          /* SRC_RC3 */
      { "/etc/rc.d/rc.0", NULL, 0755, NULL },          /* SRC_RC0 */
      { "/etc/rc.conf", "3\n", 0644, NULL },
    }, 11
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
        "root:x:0:0:root:/root:/bin/sh\n"
        "daemon:x:1:1:daemon:/:/bin/false\n"
        "hamowner:x:1000:1000:host owner:/home/hamowner:/bin/sh\n", 0644, NULL },
      { "/etc/group", "root:x:0:\ndaemon:x:1:\nhamowner:x:1000:\n", 0644, NULL },
      { "/etc/shadow", "root:!:19000:0:99999:7:::\n", 0600, NULL },
      { "/etc/login.defs", "UID_MIN 1000\nUID_MAX 60000\n", 0644, NULL },
    }, 4
};

static const Package PKG_NET = {
    "netcfg", "11.6", "network configuration and daemon",
    {
      { "/usr/sbin/netd", NULL, 0755, NULL },
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
      { "/etc/hosts",
        "127.0.0.1       localhost nominal.local\n"
        "10.0.2.20       wiki.hamnix.org wiki\n"
        "10.0.2.30       support.internal support\n"
        "10.0.2.44       bofh.hamnix.org bofh\n", 0644, NULL },
      { "/etc/resolv.conf", "nameserver 10.0.2.3\nsearch hamnix.org\n", 0644, NULL },
      { "/etc/host.conf", "order hosts,bind\n", 0644, NULL },
      { "/etc/networks", "default 0.0.0.0\n", 0644, NULL },
      { "/etc/protocols", "ip 0 IP\ntcp 6 TCP\nudp 17 UDP\n", 0644, NULL },
      { "/etc/services", "ssh 22/tcp\nhttp 80/tcp\n", 0644, NULL },
    }, 9
};

static const Package PKG_SYSLOG = {
    "syslog", "2.4", "system logging",
    {
      { "/usr/sbin/syslogd", NULL, 0755, NULL },
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
      { "/usr/sbin/udevd", NULL, 0755, NULL },
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

/* The previous administrator. Everything here is flavour EXCEPT that some of
 * it is true and useful -- the notes describe faults this machine really has
 * had, in the voice of someone who was tired. A player who reads them is
 * better at the job than one who does not, which is the only kind of easter
 * egg worth hiding. */
static const Package PKG_HOME = {
    "hamowner-home", "1.0", "the previous admin's home directory",
    {
      { "/home/hamowner/TODO",
        "- rotate the logs, /var/log is getting silly. SERIOUSLY this time,\n"
        "  it filled up in March and syslogd would not start\n"
        "- ask R. why sshd keeps coming back chmod 000. THIRD TIME.\n"
        "- the cleanup script in ~/bin is too enthusiastic, fix or delete\n"
        "- document the initrd thing before I forget it again\n"
        "- holiday\n", 0644, NULL },

      { "/home/hamowner/notes.txt",
        "Things this box has done to me, so the next person does not have to\n"
        "learn them the hard way. Every one of these actually happened.\n"
        "\n"
        "1. /boot/vmlinuz is a SYMLINK. When somebody deletes the versioned\n"
        "   image, `ls /boot` looks completely fine. stat it.\n"
        "\n"
        "2. If pkg verify is clean and it still will not boot, check the\n"
        "   things no package owns. The boot sector is not a file.\n"
        "\n"
        "3. A UUID can be perfectly well-formed and still be the wrong disk.\n"
        "   zbl.cfg and /etc/fstab have to agree with REALITY, not just with\n"
        "   each other. blkid tells you what the disk actually carries. I\n"
        "   spent a morning comparing two configs that agreed beautifully.\n"
        "\n"
        "4. Do not reinstall a package to fix a file you have not looked at.\n"
        "   You will lose whatever was legitimately edited in it and now you\n"
        "   have two problems. It refuses to touch edited config now unless\n"
        "   you --force it, which it did not used to, which is why I know.\n"
        "\n"
        "5. The updater will happily install a libc nothing here is built\n"
        "   against. When that happens NOTHING on the disk runs, including\n"
        "   every tool you would use to fix it. Boot the rescue medium and\n"
        "   use `pkg --root /mnt`. Do not waste twenty minutes on chroot\n"
        "   like I did.\n"
        "\n"
        "6. Check /etc/pkg/repos.d before you blame a package. If the channel\n"
        "   says testing, reinstalling fetches the SAME wrong version back and\n"
        "   tells you it restored everything. It is not lying. It is doing\n"
        "   exactly what you asked.\n"
        "\n"
        "7. `df` first, always. Twice now the answer has been that\n"
        "   /var/log/messages ate the disk and syslogd could not write its\n"
        "   own startup line. Nothing is corrupt. There is just no room.\n"
        "\n"
        "8. A service can be running and still wrong. If you edit a config\n"
        "   and do not reload it, the process keeps the old one and the file\n"
        "   on disk is a lie about what the machine is doing. /run/*.state\n"
        "   says what each daemon really loaded. kill -HUP it.\n"
        "\n"
        "9. Somebody bound a directory over /etc once. Every file passed\n"
        "   verify and the machine still read the wrong one. `ns` would have\n"
        "   shown me in four seconds.\n"
        "\n"
        "10. The vendor agent. Twice. They drop a .svc in /etc/services.d\n"
        "    that no package owns and it is marked critical, so the boot\n"
        "    stops for a monitoring tool nobody asked for. `pkg owns` it,\n"
        "    see that nothing does, delete it.\n", 0644, NULL },

      { "/home/hamowner/.plan",
        "gone fishing. if the machine is on fire, boot the rescue medium and\n"
        "read wiki.hamnix.org/rescue. if the wiki is also on fire, I am sorry.\n",
        0644, NULL },

      { "/home/hamowner/fortunes",
        "It is not a bug, it is an undocumented feature of the initrd.\n"
        "Any sufficiently advanced cleanup script is indistinguishable from\n"
        "  an attacker.\n"
        "The machine is always right. The machine is describing what you did.\n"
        "There is no cloud. There is only somebody else's /dev/sda1.\n"
        "Backups are a theory. Restores are a fact.\n", 0644, NULL },

      { "/home/hamowner/bin/cleanup",
        "# the enthusiastic cleanup script. DO NOT RUN. See TODO.\n"
        "# It removed /boot/vmlinuz-6.4.11 in March because the name did not\n"
        "# match the pattern it expected and it decided that meant stale.\n"
        "echo this script is disabled and is staying disabled\n", 0644, NULL },

      { "/root/.plan",
        "root is not for reading mail.\n", 0644, NULL },
    }, 6
};

static const Package PKG_SSH = {
    "openssh", "9.4", "remote login",
    {
      { "/usr/sbin/sshd", NULL, 0755, NULL },
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
      { "/bin/mount", NULL, 0755, NULL },
      { "/bin/umount", NULL, 0755, NULL },
      { "/bin/chroot", NULL, 0755, NULL },
      { "/sbin/fsck", NULL, 0755, NULL },
      { "/sbin/blkid", NULL, 0755, NULL },
      { "/bin/kill", NULL, 0755, NULL },
      { "/usr/bin/svc", NULL, 0755, NULL },
      { "/usr/bin/pkg", NULL, 0755, NULL },
      { "/usr/bin/links", NULL, 0755, NULL },
      { "/bin/cp", NULL, 0755, NULL },
      { "/bin/mv", NULL, 0755, NULL },
      { "/bin/rm", NULL, 0755, NULL },
      { "/bin/touch", NULL, 0755, NULL },
      { "/bin/grep", NULL, 0755, NULL },
      { "/bin/sed", NULL, 0755, NULL },
      { "/bin/echo", NULL, 0755, NULL },
      { "/bin/wc", NULL, 0755, NULL },
      { "/bin/head", NULL, 0755, NULL },
      { "/bin/uname", NULL, 0755, NULL },
      { "/bin/whoami", NULL, 0755, NULL },
      { "/bin/df", NULL, 0755, NULL },
      { "/bin/false", "#!false\n", 0755, NULL },
      { "/bin/true",  "#!true\n",  0755, NULL },
    }, 31
};


/* ---------------------------------------------------------------------
 * A distribution is WIDE. `pkg verify` with no arguments over thirty-odd
 * packages is a wall of text with real local edits mixed into it -- so the
 * skill is knowing which package to suspect from where the boot died, and
 * verifying THAT. Dumping everything is a last resort, exactly as it is on a
 * real machine.
 * ------------------------------------------------------------------ */

/* The repository. `stable` is what this machine is built from; `testing`
 * carries a newer libc that nothing installed here is linked against. Point
 * the config at it, run `pkg upgrade`, and the machine breaks in a way that
 * is entirely the administrator's own doing -- which is what makes it fair. */
static const Package PKG_PKGCONF = {
    "pkg-config-data", "1.4", "the package manager's repositories",
    {
      { "/etc/pkg/repos.d/main.repo",
        "# the repository this machine is built from.\n"
        "# channels: stable (11.4) | testing (12.0-pre)\n"
        "name = main\n"
        "channel = stable\n"
        "url = https://packages.hamnix.org/11.4\n", 0644, NULL },
      { "/etc/pkg/pkg.conf",
        "# how aggressive upgrades are allowed to be\n"
        "allow_downgrade = no\n"
        "check_signatures = yes\n", 0644, NULL },
    }, 2
};

static const Package PKG_LIBC = {
    "libc", "2.38", "the C library",
    {
      { "/lib/libc.so.6",  "stub libc 2.38\n", 0755, NULL },
      { "/lib/libm.so.6",  "stub libm 2.38\n", 0755, NULL },
      { "/etc/ld.so.conf", "/lib\n/usr/lib\n", 0644, NULL },
      { "/etc/nsswitch.conf",
        "passwd: files\ngroup: files\nhosts: files dns\n", 0644, NULL },
    }, 4
};

static const Package PKG_ZLIB = {
    "zlib", "1.3", "compression library",
    { { "/lib/libz.so.1", "\x7fELF (stub) zlib 1.3\n", 0755, NULL } }, 1
};

static const Package PKG_CRON = {
    "cron", "3.0", "scheduled jobs",
    {
      { "/usr/sbin/crond", NULL, 0755, NULL },
      { "/etc/services.d/cron.svc",
        "# /etc/services.d/cron.svc\n"
        "name: cron\nexec: /usr/sbin/crond\n"
        "description: scheduled jobs\nafter: syslog\n"
        "restart: on-failure\nenabled: yes\nrunlevel: 3 5\n", 0644, NULL },
      { "/etc/crontab",
        "# m h dom mon dow  command\n"
        "17 *  * * *  /usr/sbin/logrotate /etc/logrotate.conf\n"
        "0  4  * * *  /home/hamowner/bin/cleanup   # DISABLED, see TODO\n", 0644, NULL },
      { "/var/spool/cron/root", "# no personal jobs\n", 0600, NULL },
    }, 4
};

static const Package PKG_LOGROTATE = {
    "logrotate", "3.21", "log rotation",
    {
      { "/usr/sbin/logrotate", "#!logrotate\n", 0755, NULL },
      { "/etc/logrotate.conf",
        "weekly\nrotate 4\ncompress\ninclude /etc/logrotate.d\n", 0644, NULL },
      { "/etc/logrotate.d/syslog",
        "/var/log/messages {\n  weekly\n  rotate 8\n}\n", 0644, NULL },
    }, 3
};

static const Package PKG_NTP = {
    "ntp", "4.2", "time synchronisation",
    {
      { "/usr/sbin/ntpd", NULL, 0755, NULL },
      { "/etc/ntp.conf",
        "server 10.0.2.3 iburst\ndriftfile /var/lib/ntp/drift\n", 0644, NULL },
      { "/etc/services.d/ntp.svc",
        "# /etc/services.d/ntp.svc\n"
        "name: ntp\nexec: /usr/sbin/ntpd\n"
        "description: time synchronisation\nafter: net\n"
        "restart: on-failure\nenabled: yes\nrunlevel: 3 5\n", 0644, NULL },
    }, 3
};

static const Package PKG_HTTPD = {
    "httpd", "2.4", "the web server",
    {
      { "/usr/sbin/httpd", NULL, 0755, NULL },
      { "/etc/httpd/httpd.conf",
        "Listen 80\nDocumentRoot /srv/www\nServerName nominal.local\n", 0644, NULL },
      { "/srv/www/index.html",
        "this machine\n============\n\n"
        "if you are reading this over the network, httpd is up and the\n"
        "document root is intact.\n", 0644, NULL },
      { "/etc/services.d/httpd.svc",
        "# /etc/services.d/httpd.svc\n"
        "name: httpd\nexec: /usr/sbin/httpd\n"
        "description: web server\nafter: net\n"
        "restart: on-failure\nenabled: yes\nrunlevel: 3 5\n", 0644, NULL },
    }, 4
};

static const Package PKG_FIREWALL = {
    "nftables", "1.0", "packet filter",
    {
      { "/usr/sbin/nft", NULL, 0755, NULL },
      { "/etc/nftables.conf",
        "table inet filter {\n"
        "  chain input {\n"
        "    type filter hook input priority 0; policy drop;\n"
        "    tcp dport { 22, 80 } accept\n"
        "  }\n}\n", 0644, NULL },
      { "/etc/services.d/nftables.svc",
        "# /etc/services.d/nftables.svc\n"
        "name: nftables\nexec: /usr/sbin/nft\n"
        "description: packet filter\ncritical: yes\nafter: udev\n"
        "restart: on-failure\nenabled: yes\nrunlevel: 3 5\n", 0644, NULL },
    }, 3
};

static const Package PKG_MAN = {
    "man-db", "2.11", "the manual",
    {
      { "/usr/bin/man", NULL, 0755, NULL },
      { "/usr/share/man/pkg",
        "pkg(1)\n\n"
        "  pkg list                 every installed package\n"
        "  pkg verify [name]        compare installed files against the\n"
        "                           manifest in /var/lib/pkg/<name>/files\n"
        "  pkg diff <path>          what a CHANGED file says, against what\n"
        "                           the package shipped\n"
        "  pkg owns <path>          which package owns it\n"
        "  pkg reinstall <name>     refetch from the repository\n"
        "\n"
        "NOTE. `pkg verify` with no arguments checks EVERY package and will\n"
        "report local configuration changes as CHANGED, because they are.\n"
        "That is not a fault list. Work out which package to suspect from\n"
        "where the boot stopped, verify that one, and use `pkg diff` before\n"
        "you reinstall anything.\n", 0644, NULL },
      { "/usr/share/man/boot",
        "boot(7)\n\n"
        "  firmware -> zbl -> kernel -> initrd -> init -> rc -> services\n"
        "\n"
        "  zbl              /boot/zbl/zbl.cfg        pkg zbl\n"
        "  kernel, initrd   /boot/vmlinuz, /boot/initrd (SYMLINKS)\n"
        "                                            pkg kernel-default\n"
        "  init             /sbin/init, /etc/inittab pkg sysinit\n"
        "  rc               /etc/rc.boot, /etc/rc.d  pkg sysinit\n"
        "  services         /etc/services.d/*.svc    the owning package\n"
        "\n"
        "The stage the console stops at tells you which package to verify.\n", 0644, NULL },
      { "/usr/share/man/rescue",
        "rescue(7)\n\n"
        "  mount /dev/sda1 /mnt\n"
        "  for i in dev sys proc; do mount /$i /mnt/$i; done\n"
        "  chroot /mnt\n"
        "\n"
        "The customer disk is /dev/sda1. The live medium is /dev/sr0 and is\n"
        "never damaged. `exit` leaves the chroot; `quit` hangs up.\n", 0644, NULL },
      { "/usr/share/man/ns",
        "ns(1)\n\n"
        "  bind TARGET AT     lookups under AT resolve to TARGET\n"
        "  ns [pid]           print a namespace\n"
        "\n"
        "A bad bind is a fault where nothing is corrupt: every file passes\n"
        "pkg verify and the machine still reads the wrong one.\n", 0644, NULL },
    }, 5
};

static const Package PKG_MAIL = {
    "postfix", "3.8", "mail transport",
    {
      { "/usr/sbin/postfix", NULL, 0755, NULL },
      { "/etc/postfix/main.cf",
        "myhostname = nominal.local\nrelayhost = 10.0.2.30\n", 0644, NULL },
      { "/etc/aliases", "root: hamowner\npostmaster: root\n", 0644, NULL },
      { "/etc/services.d/postfix.svc",
        "# /etc/services.d/postfix.svc\n"
        "name: postfix\nexec: /usr/sbin/postfix\n"
        "description: mail transport\nafter: net\n"
        "restart: on-failure\nenabled: no\nrunlevel: 3\n", 0644, NULL },
    }, 4
};

static const Package PKG_ACCT = {
    "acct", "6.6", "process accounting",
    {
      { "/usr/sbin/accton", "#!accton\n", 0755, NULL },
      { "/etc/default/acct", "ACCT_ENABLE=no\n", 0644, NULL },
    }, 2
};

static const Package PKG_TZ = {
    "tzdata", "2024a", "time zones",
    {
      { "/etc/timezone", "UTC\n", 0644, NULL },
      { "/usr/share/zoneinfo/UTC", "UTC0\n", 0644, NULL },
    }, 2
};

static const Package PKG_TERMINFO = {
    "ncurses", "6.4", "terminal handling",
    {
      { "/lib/libncurses.so.6", "\x7fELF (stub) ncurses 6.4\n", 0755, NULL },
      { "/usr/share/terminfo/vt100", "vt100|dec vt100\n", 0644, NULL },
      { "/usr/share/terminfo/linux", "linux|linux console\n", 0644, NULL },
    }, 3
};

static const Package PKG_AUDIT = {
    "audit", "3.1", "the audit trail",
    {
      { "/usr/sbin/auditd", NULL, 0755, NULL },
      { "/etc/audit/auditd.conf", "log_file = /var/log/audit.log\nmax_log_file = 8\n", 0644, NULL },
      { "/etc/services.d/audit.svc",
        "# /etc/services.d/audit.svc\n"
        "name: audit\nexec: /usr/sbin/auditd\n"
        "description: audit trail\nafter: syslog\n"
        "restart: on-failure\nenabled: yes\nrunlevel: 3 5\n", 0644, NULL },
    }, 3
};

static const Package *IMAGE[] = {
    &PKG_BASE, &PKG_USERS, &PKG_BOOTLOADER, &PKG_KERNEL, &PKG_SYSINIT,
    &PKG_SHELL, &PKG_UDEV, &PKG_SYSLOG, &PKG_NET, &PKG_SSH, &PKG_HAMDE,
    &PKG_HOME, &PKG_PKGCONF, &PKG_LIBC, &PKG_ZLIB, &PKG_CRON, &PKG_LOGROTATE, &PKG_NTP,
    &PKG_HTTPD, &PKG_FIREWALL, &PKG_MAN, &PKG_MAIL, &PKG_ACCT, &PKG_TZ,
    &PKG_TERMINFO, &PKG_AUDIT,
};
#define IMAGE_N ((int)(sizeof IMAGE / sizeof IMAGE[0]))

void image_generated(const Machine *m, const char *path, Buf *out);
static void install_local_edits(Machine *m, uint64_t seed);

/* ---------------------------------------------------------- the rescue --
 * A complete, separate system on its own medium. It is never corrupted: the
 * breaker only ever touches m->disk. That is what makes it a live image and
 * not just another thing that can go wrong.
 *
 * It is package-backed like everything else, so `pkg verify` works inside it
 * too -- and so a player who has learned the rescue system has learned the
 * customer's, because they are the same system with different contents.
 */
static const Package PKG_RESCUE_BASE = {
    "rescue-base", "3.2", "the live rescue system",
    {
      { "/etc/inittab",
        "# rescue medium: straight to a shell.\n"
        "/bin/rc /etc/rc.boot\n", 0644, NULL },
      /* Literal, not generated. This used to be NULL and image_generated()
       * decided what to put here by looking at m->on_rescue -- so
       * `pkg reinstall sysinit` while booted from the rescue medium wrote the
       * RESCUE's rc.boot onto the CUSTOMER's disk, every single time. Generated
       * content must never depend on mutable machine state. */
      { "/etc/rc.boot",
        "# /etc/rc.boot on the rescue medium.\n"
        "echo rescue: live system, read-only medium\n"
        "echo rescue: the customer disk is /dev/sda1 and is NOT mounted\n"
        "echo\n"
        "echo   mount /dev/sda1 /mnt\n"
        "echo   for i in dev sys proc; do mount /$i /mnt/$i; done\n"
        "echo   chroot /mnt\n"
        "echo\n"
        "echo   links wiki.hamnix.org/rescue   for the full procedure\n"
        "echo\n", 0755, NULL },
      { "/etc/hostname", "rescue\n", 0644, NULL },
      { "/etc/issue",    "Hamnix rescue 3.2 -- live medium\n", 0644, NULL },
      { "/etc/os-release",
        "NAME=\"Hamnix Rescue\"\nVERSION=\"3.2\"\nID=hamnix-rescue\n", 0644, NULL },
      { "/etc/fstab", "# nothing is mounted automatically on the rescue medium\n",
        0644, NULL },
      { "/etc/hosts",
        "127.0.0.1       localhost\n"
        "10.0.2.20       wiki.hamnix.org wiki\n"
        "10.0.2.30       support.internal support\n"
        "10.0.2.44       bofh.hamnix.org bofh\n", 0644, NULL },
      { "/etc/resolv.conf", "nameserver 10.0.2.3\n", 0644, NULL },
      /* The live medium has its OWN libc. That is the whole point of it: when
       * the customer's libc is wrong, nothing on their disk runs, including
       * the tools you would fix it with. */
      { "/lib/libc.so.6",  "stub libc 2.38\n", 0755, NULL },
      { "/lib/libm.so.6",  "stub libm 2.38\n", 0755, NULL },
      { "/etc/ld.so.conf", "/lib\n/usr/lib\n", 0644, NULL },
      { "/etc/motd",
        "Hamnix rescue medium.\n"
        "  the customer disk is /dev/sda1 and is not mounted\n"
        "  links wiki.hamnix.org/rescue    for the procedure\n", 0644, NULL },
      { "/usr/lib/sysinit/init", NULL, 0755, NULL },
      { "/sbin/init", NULL, 0777, "/usr/lib/sysinit/init" },
    }, 14
};

static const Package PKG_RESCUE_TOOLS = {
    "rescue-tools", "3.2", "the tools on the live medium",
    {
      /* The live medium carries EVERY repair tool. It had been quietly
       * missing most of them -- no grep, no sed, no cp, no mkinitrd --
       * because three separate edits that meant to add them did not
       * match, and nothing checked. A rescue disc without the tools is
       * not a rescue disc. */
      { "/bin/rc", NULL, 0755, NULL },
      { "/bin/sh", NULL, 0755, NULL },
      { "/bin/ls", NULL, 0755, NULL },
      { "/bin/cat", NULL, 0755, NULL },
      { "/bin/ps", NULL, 0755, NULL },
      { "/bin/ns", NULL, 0755, NULL },
      { "/bin/stat", NULL, 0755, NULL },
      { "/bin/chmod", NULL, 0755, NULL },
      { "/bin/mount", NULL, 0755, NULL },
      { "/bin/umount", NULL, 0755, NULL },
      { "/bin/chroot", NULL, 0755, NULL },
      { "/bin/cp", NULL, 0755, NULL },
      { "/bin/mv", NULL, 0755, NULL },
      { "/bin/rm", NULL, 0755, NULL },
      { "/bin/touch", NULL, 0755, NULL },
      { "/bin/grep", NULL, 0755, NULL },
      { "/bin/sed", NULL, 0755, NULL },
      { "/bin/echo", NULL, 0755, NULL },
      { "/bin/wc", NULL, 0755, NULL },
      { "/bin/head", NULL, 0755, NULL },
      { "/bin/uname", NULL, 0755, NULL },
      { "/bin/whoami", NULL, 0755, NULL },
      { "/bin/df", NULL, 0755, NULL },
      { "/sbin/fsck", NULL, 0755, NULL },
      { "/sbin/blkid", NULL, 0755, NULL },
      { "/bin/kill", NULL, 0755, NULL },
      { "/usr/bin/svc", NULL, 0755, NULL },
      { "/usr/bin/pkg", NULL, 0755, NULL },
      { "/usr/bin/links", NULL, 0755, NULL },
      { "/usr/bin/man", NULL, 0755, NULL },
      { "/usr/sbin/zbl-install", NULL, 0755, NULL },
      { "/usr/sbin/zbl-mkconfig", NULL, 0755, NULL },
      { "/usr/bin/mkinitrd", NULL, 0755, NULL },
    }, 33
};

static const Package *RESCUE_IMAGE[] = { &PKG_RESCUE_BASE, &PKG_RESCUE_TOOLS };

static void install_rescue(Machine *m)
{
    /* image_generated() asks the machine which medium it is describing, so it
     * has to be told before the rescue root is written -- otherwise the live
     * medium is installed with the customer's rc.boot on it, and the one
     * thing guaranteed to work is broken by construction. */
    bool was = m->on_rescue;
    m->on_rescue = true;
    vfs_init(&m->rescue);
    static const char *DIRS[] = {
        "/bin", "/dev", "/etc", "/mnt", "/proc", "/root", "/sbin", "/sys",
        "/tmp", "/usr", "/usr/bin", "/usr/lib", "/usr/lib/sysinit", "/usr/sbin",
        "/var", "/var/lib", "/var/lib/pkg", NULL
    };
    for (int i = 0; DIRS[i]; i++) vfs_mkdir(&m->rescue, DIRS[i]);

    /* Device nodes, so `ls /dev` on the rescue medium shows you what there is
     * to mount. The customer disk is present whether or not it works. */
    for (const char **d = (const char *[]){ "sda", "sda1", "sr0", NULL }; *d; d++) {
        char p2[NOM_PATH_MAX];
        snprintf(p2, sizeof p2, "/dev/%s", *d);
        VNode *n = vfs_mkfile(&m->rescue, p2, "");
        if (n) { n->kind = VN_DEV; n->mode = 0660; }
    }

    Vfs *save = NULL; (void)save;
    for (int i = 0; i < 2; i++) {
        const Package *p = RESCUE_IMAGE[i];
        for (int j = 0; j < p->nfiles; j++) {
            const PkgFile *f = &p->file[j];
            if (f->link) { vfs_symlink(&m->rescue, f->link, f->path); continue; }
            Buf b = {0};
            if (f->content) buf_puts(&b, f->content);
            else            image_generated(m, f->path, &b);
            VNode *n = vfs_mkfile(&m->rescue, f->path, "");
            if (n) {
                buf_clear(&n->data);
                buf_put(&n->data, b.p, b.len);
                n->mode = f->mode;
            }
            buf_free(&b);
        }
    }
    m->on_rescue = was;
}

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
    else if (strcmp(path, "/bin/mount") == 0)
        buf_put(out, (const char *)GUEST_MOUNT, GUEST_MOUNT_LEN);
    else if (strcmp(path, "/bin/umount") == 0)
        buf_put(out, (const char *)GUEST_UMOUNT, GUEST_UMOUNT_LEN);
    else if (strcmp(path, "/bin/chroot") == 0)
        buf_put(out, (const char *)GUEST_CHROOT, GUEST_CHROOT_LEN);
    else if (strcmp(path, "/bin/cp") == 0)
        buf_put(out, (const char *)GUEST_CP, GUEST_CP_LEN);
    else if (strcmp(path, "/bin/mv") == 0)
        buf_put(out, (const char *)GUEST_MV, GUEST_MV_LEN);
    else if (strcmp(path, "/bin/rm") == 0)
        buf_put(out, (const char *)GUEST_RM, GUEST_RM_LEN);
    else if (strcmp(path, "/bin/touch") == 0)
        buf_put(out, (const char *)GUEST_TOUCH, GUEST_TOUCH_LEN);
    else if (strcmp(path, "/bin/grep") == 0)
        buf_put(out, (const char *)GUEST_GREP, GUEST_GREP_LEN);
    else if (strcmp(path, "/bin/sed") == 0)
        buf_put(out, (const char *)GUEST_SED, GUEST_SED_LEN);
    else if (strcmp(path, "/bin/echo") == 0)
        buf_put(out, (const char *)GUEST_ECHO, GUEST_ECHO_LEN);
    else if (strcmp(path, "/bin/wc") == 0)
        buf_put(out, (const char *)GUEST_WC, GUEST_WC_LEN);
    else if (strcmp(path, "/bin/head") == 0)
        buf_put(out, (const char *)GUEST_HEAD, GUEST_HEAD_LEN);
    else if (strcmp(path, "/bin/uname") == 0)
        buf_put(out, (const char *)GUEST_UNAME, GUEST_UNAME_LEN);
    else if (strcmp(path, "/bin/whoami") == 0)
        buf_put(out, (const char *)GUEST_WHOAMI, GUEST_WHOAMI_LEN);
    else if (strcmp(path, "/bin/df") == 0)
        buf_put(out, (const char *)GUEST_DF, GUEST_DF_LEN);
    else if (strcmp(path, "/usr/sbin/syslogd") == 0)
        buf_put(out, (const char *)GUEST_SYSLOGD, GUEST_SYSLOGD_LEN);
    else if (strcmp(path, "/usr/sbin/netd") == 0)
        buf_put(out, (const char *)GUEST_NETD, GUEST_NETD_LEN);
    else if (strcmp(path, "/usr/sbin/udevd") == 0)
        buf_put(out, (const char *)GUEST_UDEVD, GUEST_UDEVD_LEN);
    else if (strcmp(path, "/usr/sbin/crond") == 0)
        buf_put(out, (const char *)GUEST_CROND, GUEST_CROND_LEN);
    else if (strcmp(path, "/usr/sbin/ntpd") == 0)
        buf_put(out, (const char *)GUEST_NTPD, GUEST_NTPD_LEN);
    else if (strcmp(path, "/usr/sbin/httpd") == 0)
        buf_put(out, (const char *)GUEST_HTTPD, GUEST_HTTPD_LEN);
    else if (strcmp(path, "/usr/sbin/nft") == 0)
        buf_put(out, (const char *)GUEST_NFT, GUEST_NFT_LEN);
    else if (strcmp(path, "/usr/sbin/auditd") == 0)
        buf_put(out, (const char *)GUEST_AUDITD, GUEST_AUDITD_LEN);
    else if (strcmp(path, "/usr/sbin/sshd") == 0)
        buf_put(out, (const char *)GUEST_SSHD, GUEST_SSHD_LEN);
    else if (strcmp(path, "/usr/sbin/postfix") == 0)
        buf_put(out, (const char *)GUEST_POSTFIX, GUEST_POSTFIX_LEN);
    else if (strcmp(path, "/usr/bin/pkg") == 0)
        buf_put(out, (const char *)GUEST_PKG, GUEST_PKG_LEN);
    else if (strcmp(path, "/usr/bin/links") == 0)
        buf_put(out, (const char *)GUEST_LINKS, GUEST_LINKS_LEN);
    else if (strcmp(path, "/usr/bin/man") == 0)
        buf_put(out, (const char *)GUEST_MAN, GUEST_MAN_LEN);
    else if (strcmp(path, "/usr/sbin/zbl-install") == 0)
        buf_put(out, (const char *)GUEST_ZBL_INSTALL, GUEST_ZBL_INSTALL_LEN);
    else if (strcmp(path, "/usr/sbin/zbl-mkconfig") == 0)
        buf_put(out, (const char *)GUEST_ZBL_MKCONFIG, GUEST_ZBL_MKCONFIG_LEN);
    else if (strcmp(path, "/usr/bin/mkinitrd") == 0)
        buf_put(out, (const char *)GUEST_MKINITRD, GUEST_MKINITRD_LEN);
    else if (strcmp(path, "/sbin/svcinit") == 0)
        buf_put(out, (const char *)GUEST_SVCINIT, GUEST_SVCINIT_LEN);
    else if (strcmp(path, "/sbin/login") == 0)
        buf_put(out, (const char *)GUEST_LOGIN, GUEST_LOGIN_LEN);
    else if (strcmp(path, "/sbin/getty") == 0)
        buf_put(out, (const char *)GUEST_GETTY, GUEST_GETTY_LEN);
    else if (strcmp(path, "/sbin/fsck") == 0)
        buf_put(out, (const char *)GUEST_FSCK, GUEST_FSCK_LEN);
    else if (strcmp(path, "/sbin/blkid") == 0)
        buf_put(out, (const char *)GUEST_BLKID, GUEST_BLKID_LEN);
    else if (strcmp(path, "/bin/kill") == 0)
        buf_put(out, (const char *)GUEST_KILL, GUEST_KILL_LEN);
    else if (strcmp(path, "/usr/bin/svc") == 0)
        buf_put(out, (const char *)GUEST_SVC, GUEST_SVC_LEN);
    else if (strcmp(path, "/sbin/mountall") == 0)
        buf_put(out, (const char *)GUEST_MOUNTALL, GUEST_MOUNTALL_LEN);
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
        buf_puts(out, "/dev/sr0                        /media iso9660 noauto\n");
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
            if (f->link) {
                /* A symlink has no content, but it very much has a value, and
                 * a deleted or repointed one is one of the commonest ways a
                 * machine stops booting. Recording the hash of its TARGET is
                 * what lets verify see that. */
                buf_printf(&man, "link %016llx %s\n",
                           (unsigned long long)fnv1a(f->link, strlen(f->link)),
                           f->path);
                continue;
            }
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
        "/etc/udev/rules.d", "/home", "/home/hamowner", "/home/hamowner/bin", "/lib", "/lib/modules",
        "/lib/modules/6.4.11", "/proc", "/root", "/sbin", "/sys", "/tmp",
        "/mnt", "/media", "/usr", "/usr/bin", "/usr/lib", "/usr/lib/sysinit",
        "/usr/sbin", "/usr/share", "/usr/share/man", "/usr/share/zoneinfo",
        "/usr/share/terminfo", "/var", "/var/log", "/var/lib", "/var/lib/ntp",
        "/var/lib/pkg", "/var/cache", "/var/spool", "/var/spool/cron",
        "/etc/audit", "/etc/default", "/etc/httpd", "/etc/logrotate.d",
        "/etc/postfix", "/srv", "/srv/www", "/etc/pkg", "/etc/pkg/repos.d",
        "/run", NULL
    };
    for (int i = 0; DIRS[i]; i++) vfs_mkdir(&m->disk, DIRS[i]);

    for (int i = 0; i < IMAGE_N && i < PKG_MAX; i++) {
        /* A package whose nfiles does not match its initialiser list either
         * silently drops files (invisible to pkg verify, unrepairable by
         * reinstall) or reads past the end of the array. Both have happened.
         * Neither is worth debugging twice. */
        for (int j = 0; j < IMAGE[i]->nfiles; j++) {
            if (IMAGE[i]->file[j].path) continue;
            fprintf(stderr, "image: package %s declares %d files but entry %d "
                            "is empty -- fix its count\n",
                    IMAGE[i]->name, IMAGE[i]->nfiles, j);
            abort();
        }
        m->pkg[m->npkg++] = IMAGE[i];
        for (int j = 0; j < IMAGE[i]->nfiles; j++)
            install_file(m, &IMAGE[i]->file[j]);
    }
    /* Sized from what the installation actually takes, with room for a
     * working machine and not much more -- which is what a real disk feels
     * like and is what makes filling it possible. */
    m->fs_capacity = 0;
    install_pkgdb(m);
    install_local_edits(m, seed);
    m->fs_capacity = machine_disk_used(m) + 512u * 1024u;
    install_rescue(m);
    m->next_pid = 1;
}

/* Every real machine has been touched by a human. These are the edits that
 * admin made on purpose: a nameserver they changed, a service they turned
 * off, a host they added. They are legitimate, they are NOT the fault, and
 * `pkg verify` reports them as CHANGED because that is the truth.
 *
 * This is the single biggest thing standing between this game and a lookup
 * table. Before it, verify named exactly one file and that file was always
 * the answer. Now the player has to decide which difference MATTERS -- and
 * `pkg reinstall` on the wrong package silently destroys somebody's work.
 */
static void install_local_edits(Machine *m, uint64_t seed)
{
    Rng r;
    rng_seed(&r, seed ^ 0xc0ffee1234ULL);

    struct { const char *path; const char *content; } EDITS[] = {
      { "/etc/resolv.conf",
        "# changed 12 March -- the .3 resolver was timing out at peak\n"
        "nameserver 10.0.2.9\n"
        "search hamnix.org\n" },
      { "/etc/hosts",
        "127.0.0.1       localhost nominal.local\n"
        "10.0.2.20       wiki.hamnix.org wiki\n"
        "10.0.2.30       support.internal support\n"
        "10.0.2.44       bofh.hamnix.org bofh\n"
        "# added for the migration, remove when dock-2 is retired\n"
        "10.0.2.61       oldbilling.internal oldbilling\n" },
      { "/etc/ssh/sshd_config",
        "# hardened after the audit, do not revert\n"
        "Port 2222\n"
        "PermitRootLogin no\n"
        "MaxAuthTries 3\n" },
      { "/etc/syslog.conf",
        "# quieten the udev chatter, it was filling the disk\n"
        "*.info /var/log/messages\n"
        "udev.* /dev/null\n" },
      { "/etc/net/interfaces",
        "# static since the dhcp lease kept moving us\n"
        "iface eth0\n"
        "  address 10.0.2.15\n"
        "  gateway 10.0.2.2\n" },
      { "/etc/profile",
        "# login shell profile\n"
        "PATH=/bin:/usr/bin:/sbin\n"
        "# added by hamowner: I got tired of typing it\n"
        "alias v=pkg verify\n" },
    };
    const int NEDITS = (int)(sizeof EDITS / sizeof EDITS[0]);

    /* One to three of them, chosen by the seed, so two machines are not the
     * same machine. */
    int want = 1 + (int)(rng_next(&r) % 3);
    for (int k = 0; k < want && m->nlocal < 8; k++) {
        int i = (int)(rng_next(&r) % (uint64_t)NEDITS);
        bool dup = false;
        for (int j = 0; j < m->nlocal; j++)
            if (strcmp(m->local[j], EDITS[i].path) == 0) dup = true;
        if (dup) continue;
        VNode *n = vfs_lookup(&m->disk, EDITS[i].path);
        if (!n || n->kind != VN_FILE) continue;
        buf_clear(&n->data);
        buf_puts(&n->data, EDITS[i].content);
        buf_puts(&m->local_orig[m->nlocal], EDITS[i].content);
        snprintf(m->local[m->nlocal], NOM_PATH_MAX, "%s", EDITS[i].path);
        m->nlocal++;
    }
}

/* Did the repair survive the administrator's decisions?
 *
 * A playtester's sharpest criticism was that nothing stopped them reinstalling
 * every flagged package, and there was no cost to being sloppy. There is one
 * now: `pkg reinstall` keeps modified config unless forced, and this reports
 * what was reverted anyway. Fixing the machine while quietly undoing
 * somebody's work is not the same as fixing the machine. */
int machine_collateral(Machine *m, Buf *out)
{
    int lost = 0;
    for (int i = 0; i < m->nlocal; i++) {
        VNode *n = vfs_lookup(&m->disk, m->local[i]);
        bool gone = !n || n->kind != VN_FILE ||
                    n->data.len != m->local_orig[i].len ||
                    (n->data.len &&
                     memcmp(n->data.p, m->local_orig[i].p, n->data.len) != 0);
        if (!gone) continue;
        if (!lost) buf_puts(out, "\nlocal configuration that no longer survives:\n");
        buf_printf(out, "  %s\n", m->local[i]);
        lost++;
    }
    if (lost)
        buf_puts(out, "  someone chose those settings deliberately. `pkg diff`\n"
                      "  before reinstalling would have shown you which.\n");
    return lost;
}

void machine_free(Machine *m)
{
    for (int i = 0; i < 8; i++) buf_free(&m->local_orig[i]);
    kernel_stop_daemons(m);
    vfs_free(&m->disk);
    vfs_free(&m->rescue);
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

    /* THE CHANNEL DECIDES WHAT THE REPOSITORY SERVES. On `testing` the libc
     * is 12.0's, which nothing on this machine is linked against -- so an
     * upgrade from the wrong channel installs a perfectly valid library that
     * every binary refuses to run with. The fault is the config, not the
     * file, and `pkg verify` will happily report the file as wrong when the
     * real problem is where it came from. */
    if (m->channel[0] && strcmp(m->channel, "stable") != 0) {
        if (strcmp(path, "/lib/libc.so.6") == 0) {
            buf_puts(out, "stub libc 2.41\n");
            return true;
        }
        if (strcmp(path, "/lib/libm.so.6") == 0) {
            buf_puts(out, "stub libm 2.41\n");
            return true;
        }
    }
    for (int j = 0; j < p->nfiles; j++) {
        if (strcmp(p->file[j].path, path) != 0) continue;
        if (p->file[j].link) {
            /* Restoring a link is not a copy of bytes -- recreate it here and
             * report success with no content, which is what the guest expects
             * for a `link` manifest entry. */
            Machine *mm = (Machine *)(void *)(uintptr_t)m;
            vfs_remove(&mm->disk, path);
            vfs_symlink(&mm->disk, p->file[j].link, path);
            return true;
        }
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
