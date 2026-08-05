/* image.c — the installed system, modelled on NomnixOS.
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
 * Layout follows NomnixOS: /etc/inittab names what PID 1 runs, /etc/rc.boot is
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
      { "/boot/vmnomuz-6.4.11", "\x7fKRNL 6.4.11 rv64\n", 0644, NULL },
      { "/boot/vmnomuz", NULL, 0777, "/boot/vmnomuz-6.4.11" },
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
        "NAME=\"NomnixOS\"\nVERSION=\"11.4\"\nID=nomnix\n"
        "PRETTY_NAME=\"NomnixOS 11.4\"\n", 0644, NULL },
      { "/etc/lsb-release",
        "DISTRIB_ID=NomnixOS\nDISTRIB_RELEASE=11.4\n", 0644, NULL },
      { "/etc/issue", "NomnixOS 11.4\n", 0644, NULL },
      { "/etc/motd",  "Welcome to NomnixOS.\n", 0644, NULL },
      { "/etc/shells", "/bin/nomsh\n", 0644, NULL },
      { "/etc/profile", "# login shell profile\nPATH=/bin:/usr/bin:/sbin\n", 0644, NULL },
          { "/run", NULL, 0755, NULL, true },
      { "/tmp", NULL, 0777, NULL, true },
      { "/var/cache", NULL, 0755, NULL, true },
    }, 11
};

static const Package PKG_USERS = {
    "shadow", "4.13", "accounts",
    {
      { "/etc/passwd",
        "root:x:0:0:root:/root:/bin/sh\n"
        "daemon:x:1:1:daemon:/:/bin/false\n"
        "nomowner:x:1000:1000:host owner:/home/nomowner:/bin/sh\n", 0644, NULL },
      { "/etc/group", "root:x:0:\ndaemon:x:1:\nnomowner:x:1000:\n", 0644, NULL },
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
        "10.0.2.20       wiki.nomnix.org wiki\n"
        "10.0.2.30       support.internal support\n"
        "10.0.2.44       bofh.nomnix.org bofh\n", 0644, NULL },
      { "/etc/resolv.conf", "nameserver 10.0.2.3\nsearch nomnix.org\n", 0644, NULL },
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
          { "/var/log", NULL, 0755, NULL, true },
    }, 4
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
      /* The rule that NAMES the network device. udev is what decides an
       * interface is called eth0, and netd configures whatever udev named --
       * so these two files have to agree, and a rename in one of them is a
       * real and thoroughly confusing fault. Before this the rules file was
       * read by udevd and consulted by nothing, which is exactly the
       * "file nothing reads" this project is not supposed to have. */
      { "/etc/udev/rules.d/50-default.rules",
        "SUBSYSTEM==\"block\", MODE=\"0660\"\n"
        "SUBSYSTEM==\"net\", NAME=\"eth0\"\n", 0644, NULL },
    }, 3
};

/* The previous administrator. Everything here is flavour EXCEPT that some of
 * it is true and useful -- the notes describe faults this machine really has
 * had, in the voice of someone who was tired. A player who reads them is
 * better at the job than one who does not, which is the only kind of easter
 * egg worth hiding. */
static const Package PKG_HOME = {
    "nomowner-home", "1.0", "the previous admin's home directory",
    {
      { "/home/nomowner/TODO",
        "- rotate the logs, /var/log is getting silly. SERIOUSLY this time,\n"
        "  it filled up in March and syslogd would not start\n"
        "- ask R. why sshd keeps coming back chmod 000. THIRD TIME.\n"
        "- the cleanup script in ~/bin is too enthusiastic, fix or delete\n"
        "- document the initrd thing before I forget it again\n"
        "- holiday\n", 0644, NULL },

      { "/home/nomowner/notes.txt",
        "Things this box has done to me, so the next person does not have to\n"
        "learn them the hard way. Every one of these actually happened.\n"
        "\n"
        "1. /boot/vmnomuz is a SYMLINK. When somebody deletes the versioned\n"
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

      { "/home/nomowner/.plan",
        "gone fishing. if the machine is on fire, boot the rescue medium and\n"
        "read wiki.nomnix.org/rescue. if the wiki is also on fire, I am sorry.\n",
        0644, NULL },

      { "/home/nomowner/fortunes",
        "It is not a bug, it is an undocumented feature of the initrd.\n"
        "Any sufficiently advanced cleanup script is indistinguishable from\n"
        "  an attacker.\n"
        "The machine is always right. The machine is describing what you did.\n"
        "There is no cloud. There is only somebody else's /dev/sda1.\n"
        "Backups are a theory. Restores are a fact.\n", 0644, NULL },

      { "/home/nomowner/bin/cleanup",
        "# the enthusiastic cleanup script. DO NOT RUN. See TODO.\n"
        "# It removed /boot/vmnomuz-6.4.11 in March because the name did not\n"
        "# match the pattern it expected and it decided that meant stale.\n"
        "echo this script is disabled and is staying disabled\n", 0644, NULL },

      { "/root/.plan",
        "root is not for reading mail.\n", 0644, NULL },
      /* The dust a real person leaves. Every one of these is TRUE of a fault
       * the breaker actually produces, which is the rule for easter eggs
       * here: funny, and load-bearing if you read it carefully. */
      { "/home/nomowner/Desktop/read-me-first.txt",
        "If you are reading this I have gone.\n"
        "\n"
        "Three things nobody told me and I had to find out:\n"
        "  1. /boot/vmnomuz is a SYMLINK. Deleting \"the old kernel\" breaks it.\n"
        "  2. pkg reinstall will NOT overwrite a config you edited. That is a\n"
        "     feature. --force writes a .pkgsave first. Read it before you\n"
        "     assume the file was rubbish.\n"
        "  3. df says there is room. df -i is a different question.\n", 0644, NULL },
      { "/home/nomowner/Documents/handover.txt",
        "HANDOVER NOTES\n"
        "\n"
        "The web server listens on 8080, not 80. That was deliberate; the load\n"
        "balancer terminates. Do not \"fix\" it.\n"
        "\n"
        "The audit trail is compressed, so auditd needs libz. If you ever see\n"
        "httpd and audit down together and everything else up, that is the\n"
        "same library and not a coincidence.\n"
        "\n"
        "Marcus in accounts will tell you his machine has never been touched.\n"
        "Marcus's machine has been touched.\n", 0644, NULL },
      { "/home/nomowner/Downloads/rescue-3.2.iso.txt",
        "(this is where the rescue image lives when someone downloads it\n"
        " instead of using the one in the drawer. It is the same image.)\n", 0644, NULL },
      { "/home/nomowner/Documents/passwords.txt",
        "I am not writing passwords in a file.\n"
        "\n"
        "-- but the root password is on a sticker under the desk, which is\n"
        "   worse, and I am sorry.\n", 0644, NULL },

      /* THE POSTMORTEM. Every command in it is a command this machine has,
       * and the sequence is the actual repair for the disk-full fault --
       * which the breaker really produces. A player who reads this has been
       * handed the March outage as a worked example. */
      { "/home/nomowner/Documents/postmortem-march.txt",
        "POSTMORTEM -- 14 March -- \"the machine is dead\" (it was not)\n"
        "\n"
        "SYMPTOM. Boot reached the services and syslog would not start. Two\n"
        "hours were spent on syslogd, which was innocent, because the first\n"
        "thing to fail is never the thing that is wrong -- it is just the\n"
        "first thing that tried to write.\n"
        "\n"
        "CAUSE. /var/log/messages had been growing since January. The disk\n"
        "was full. Nothing was corrupt. `pkg verify` said the machine was\n"
        "perfect and it was telling the truth.\n"
        "\n"
        "WHAT WOULD HAVE FOUND IT IN THIRTY SECONDS:\n"
        "  df\n"
        "  find /var -type f\n"
        "  wc /var/log/messages\n"
        "  rm /var/log/messages\n"
        "  svc\n"
        "\n"
        "NOTE FOR NEXT TIME. `df` and `df -i` are two different questions.\n"
        "Space and inodes run out independently, and when the inodes go, df\n"
        "with no arguments swears blind there is plenty of room.\n"
        "\n"
        "ACTION ITEMS. Rotate the logs. (Not done.) Alert on disk usage.\n"
        "(Not done.) Write this postmortem. (Done, obviously, it is the only\n"
        "one that costs nothing.)\n", 0644, NULL },

      /* The chmod 000 saga from the TODO, closed out. It names the exact
       * command that repairs a mode, and the exact one that finds it. */
      { "/home/nomowner/Documents/ticket-8841.txt",
        "TICKET 8841 -- sshd keeps coming back mode 000\n"
        "\n"
        "It was R. It was always going to be R. He runs a \"hardening\"\n"
        "script from a laptop and it walks /usr/sbin taking the execute bit\n"
        "off anything it does not recognise.\n"
        "\n"
        "Symptom: svc says sshd is not running, the binary is present, and\n"
        "`pkg verify openssh` reports it as `mode` and NOT as `changed` --\n"
        "the bytes are perfect, the permission is not. That one word in the\n"
        "verify output is the whole diagnosis.\n"
        "\n"
        "  ls /usr/sbin            the mode is in the first column\n"
        "  chmod 755 /usr/sbin/sshd\n"
        "  svc status sshd\n"
        "\n"
        "Resolution: told R. Reopened twice. I have stopped counting.\n", 0644, NULL },

      /* THE QUIETLY OMINOUS ONE. It is also entirely true: a directory the
       * display server needs, that no other package can put back. */
      { "/home/nomowner/Desktop/DO-NOT-DELETE.txt",
        "Please do not tidy /run.\n"
        "\n"
        "I know it looks like rubbish. It is where every daemon writes what it\n"
        "actually loaded -- /run/crond.state, /run/ntpd.state and the rest --\n"
        "and it is the only place on this machine that knows the difference\n"
        "between a service that is running and a service that is running the\n"
        "config you can see in the file.\n"
        "\n"
        "The desktop keeps its own directory in there, /run/nomde. If that\n"
        "goes, the display server does not come back, and nothing tells you\n"
        "why. I have written this three times. The third time was after I\n"
        "did it myself.\n", 0644, NULL },

      /* Saved off the network, back when someone still read it. */
      { "/home/nomowner/Downloads/bofh-excuses.txt",
        "(saved from bofh.nomnix.org/excuse -- `links bofh.nomnix.org/excuse`\n"
        " if the network is up and you want the current list)\n"
        "\n"
        "  it is a layer 8 problem\n"
        "  the cleaning contractor unplugged something to charge their phone\n"
        "  the previous administrator left and took the knowledge with them\n"
        "  we upgraded from the testing channel by accident\n"
        "  it is DNS\n"
        "  it is not DNS\n"
        "  it was DNS\n"
        "\n"
        "The last three are a joke everywhere except here, where /etc/hosts\n"
        "is consulted before dns -- /etc/nsswitch.conf says so, in that\n"
        "order -- so it is usually a line somebody typed by hand. Which is\n"
        "worse.\n", 0644, NULL },

      /* A README in an odd place. There is no image viewer and no images. */
      { "/home/nomowner/Pictures/README",
        "There are no pictures on this machine and there never were.\n"
        "\n"
        "The directory came with the account and I have left it alone,\n"
        "because deleting an empty directory is exactly the kind of tidying\n"
        "that ends up in a postmortem.\n"
        "\n"
        "If you want art: links asciiart.nomnix.org\n"
        "If you want a cow: cowsay -f tux hello\n", 0644, NULL },

      /* THE ABANDONED SCRIPT, which explains its own abandonment: /bin/rc is
       * the only interpreter here, rc has five verbs, and rc stops at the
       * first failure -- all three true, all three checkable in rc.c. What is
       * left is a genuinely good checklist in the right order, which is what
       * half-finished automation always turns out to have been. */
      { "/home/nomowner/bin/checkboot",
        "# checkboot -- half a script, and it is staying that way.\n"
        "#\n"
        "# /bin/rc is the only thing here that runs a script file, and rc knows\n"
        "# five verbs: echo, mount, run, exec, need. Everything below would\n"
        "# need `exec` in front of it, and rc stops at the FIRST failure --\n"
        "# which is precisely wrong for a checklist, where the whole point is\n"
        "# to keep going and see which line answers oddly. So it is all\n"
        "# comments. Read it and type them.\n"
        "#\n"
        "# The order matters. It goes down the boot chain, and the first line\n"
        "# that answers wrong is the stage that broke.\n"
        "#\n"
        "#   dmesg                     what the last boot actually said\n"
        "#   svc                       what should be up and is not\n"
        "#   df                        space, before anything clever\n"
        "#   df -i                     inodes, which is a different question\n"
        "#   blkid                     what the disk really is\n"
        "#   cat /etc/fstab            what we told it the disk was\n"
        "#   cat /boot/zbl/zbl.cfg     what the bootloader was told\n"
        "#   stat /boot/vmnomuz        the symlink, not the listing\n"
        "#   ldd /usr/sbin/httpd       one dead service, one live one\n"
        "#   pkg verify <the suspect>  not all of it. you will drown.\n"
        "#   ns                        in case none of the above was real\n"
        "#\n"
        "# TODO: turn this into something that runs. (It is fine. Leave it.)\n",
        0644, NULL },

      { "/home/nomowner/bin/README",
        "Nothing in here is executable and that is not an accident.\n"
        "\n"
        "The one script that WAS executable removed a kernel in March. See\n"
        "~/TODO, and see /etc/crontab, where its job is commented out and is\n"
        "staying commented out.\n", 0644, NULL },

      /* Funny, and every question is answerable on the machine in front of
       * you -- which makes it a tutorial wearing a joke's clothing. */
      { "/home/nomowner/Documents/interview-questions.txt",
        "Questions I ask, and what the answer tells me\n"
        "\n"
        "1. `ls /boot` looks perfect and the machine will not boot. Next?\n"
        "   (`stat` it. ls shows a symlink; stat follows it.)\n"
        "\n"
        "2. `pkg verify` says every file matches and it still will not boot.\n"
        "   (Then it is something no package owns: the boot sector, a bind,\n"
        "    a full disk, or the repository it all came from.)\n"
        "\n"
        "3. Two services dead, eleven fine. Where do you look?\n"
        "   (`ldd` on one of each, and ask what the dead two have in common.)\n"
        "\n"
        "4. The config plainly says port 80 and the daemon is on 8080.\n"
        "   (Nobody reloaded it. The file is not the process. kill -HUP.)\n"
        "\n"
        "5. You have fixed it. How do you know?\n"
        "   (This is the only question. `svc`, and then `pkg verify` on what\n"
        "    you touched, and then say what you changed and why.)\n"
        "\n"
        "Nobody has ever got 5 first. I did not either.\n", 0644, NULL },

      { "/root/Desktop/if-you-are-here.txt",
        "You are logged in as root on a machine you did not build.\n"
        "\n"
        "Before you change anything: `pkg diff` the file. Somebody chose what\n"
        "is in it, and `pkg reinstall --force` will take their afternoon away\n"
        "without asking. Plain `pkg reinstall` leaves edited config alone,\n"
        "which is the whole reason the flag exists.\n"
        "\n"
        "-- nomowner, who did not do that, once, for about ten minutes\n", 0644, NULL },
    }, 19
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
    "nomde", "3.1", "the desktop",
    {
      { "/usr/bin/nomde", NULL, 0755, NULL },
      { "/etc/services.d/nomde.svc",
        "# /etc/services.d/nomde.svc\n"
        "name: nomde\n"
        "exec: /usr/bin/nomde\n"
        "description: the display server\n"
        /* RUNLEVEL 3 AND 5, so the graphical stack exists on every machine
         * and can therefore BREAK on every machine. A desktop that only runs
         * on a box you never see is not debuggable, and David's whole point
         * was being able to debug a broken graphical session the way you
         * would a broken X11 one. */
        "after: net\n"
        "restart: on-failure\n"
        "enabled: yes\n"
        "runlevel: 3 5\n", 0644, NULL },
      { "/etc/nomde/panel.conf", "position=bottom\nheight=28\n", 0644, NULL },
      { "/etc/nomde/desktop.icons", "Terminal\nFiles\n", 0644, NULL },
      /* THE APP REGISTRY, and it is FILES.
       *
       * David: "tie the desktop into the OS at a fairly deep level, so you
       * could start any of the graphical applications from the command line
       * ... similar to debugging a broken X11 session."
       *
       * So the desktop does not know what applications exist. It reads these,
       * the way every real desktop reads .desktop files. Delete one and the
       * icon goes; corrupt one and it goes; and both are diagnosable with
       * `ls /usr/share/applications` and `cat` like anything else. The
       * graphical stack becomes a thing you can break and repair rather than
       * a painted-on menu. */
      { "/usr/share/applications/terminal.desktop",
        "[Desktop Entry]\n"
        "Name=Terminal\n"
        "Exec=terminal\n"
        "Icon=term\n"
        "Comment=A shell on this machine\n", 0644, NULL },
      { "/usr/share/applications/chat.desktop",
        "[Desktop Entry]\n"
        "Name=Chat\n"
        "Exec=chat\n"
        "Icon=chat\n"
        "Comment=The customer, and two colleagues\n", 0644, NULL },
      { "/usr/share/applications/files.desktop",
        "[Desktop Entry]\n"
        "Name=Files\n"
        "Exec=files\n"
        "Icon=files\n"
        "Comment=Browse this machine\n", 0644, NULL },
      { "/usr/share/applications/notes.desktop",
        "[Desktop Entry]\n"
        "Name=Notes\n"
        "Exec=notes\n"
        "Icon=notes\n"
        "Comment=/root/notes.txt, in a window\n", 0644, NULL },
      { "/usr/share/applications/logview.desktop",
        "[Desktop Entry]\n"
        "Name=Log Viewer\n"
        "Exec=logview\n"
        "Icon=log\n"
        "Comment=What the machine said while booting\n", 0644, NULL },
      { "/usr/share/applications/manual.desktop",
        "[Desktop Entry]\n"
        "Name=Manual\n"
        "Exec=manual\n"
        "Icon=manual\n"
        "Comment=How this machine works\n", 0644, NULL },
      { "/usr/share/applications/browser.desktop",
        "[Desktop Entry]\n"
        "Name=Browser\n"
        "Exec=browser\n"
        "Icon=browser\n"
        "Comment=The intranet\n", 0644, NULL },
      { "/usr/share/applications/g2048.desktop",
        "[Desktop Entry]\n"
        "Name=2048\n"
        "Exec=g2048\n"
        "Icon=game\n"
        "Comment=Slide the tiles\n", 0644, NULL },
      { "/usr/share/applications/gflappy.desktop",
        "[Desktop Entry]\n"
        "Name=Flappy\n"
        "Exec=gflappy\n"
        "Icon=game\n"
        "Comment=Do not hit the pipes\n", 0644, NULL },
      { "/usr/share/applications/gworms.desktop",
        "[Desktop Entry]\n"
        "Name=Worms\n"
        "Exec=gworms\n"
        "Icon=game\n"
        "Comment=Two worms, one hill\n", 0644, NULL },
      { "/usr/share/applications/gsnake.desktop",
        "[Desktop Entry]\n"
        "Name=Snake\n"
        "Exec=gsnake\n"
        "Icon=snake\n"
        "Comment=Do not eat yourself\n", 0644, NULL },
      { "/usr/share/applications/gmines.desktop",
        "[Desktop Entry]\n"
        "Name=Minesweeper\n"
        "Exec=gmines\n"
        "Icon=mines\n"
        "Comment=The first click is always safe\n", 0644, NULL },
      { "/usr/share/applications/gblocks.desktop",
        "[Desktop Entry]\n"
        "Name=Blocks\n"
        "Exec=gblocks\n"
        "Icon=blocks\n"
        "Comment=Seven shapes, one well\n", 0644, NULL },
      { "/usr/share/applications/gsolitaire.desktop",
        "[Desktop Entry]\n"
        "Name=Solitaire\n"
        "Exec=gsolitaire\n"
        "Icon=cards\n"
        "Comment=Klondike, draw one or three\n", 0644, NULL },
      { "/usr/share/applications/gliquid.desktop",
        "[Desktop Entry]\n"
        "Name=Liquid War\n"
        "Exec=gliquid\n"
        "Icon=liquid\n"
        "Comment=Three hundred fighters follow your cursor\n", 0644, NULL },
      { "/usr/share/applications/calc.desktop",
        "[Desktop Entry]\n"
        "Name=Calculator\n"
        "Exec=calc\n"
        "Icon=calc\n"
        "Comment=Arithmetic, with precedence\n", 0644, NULL },
      /* THE THREE THAT LOOK AT A MACHINE.
       *
       * These are graphical front ends to ps, svc, df, and pkg -- they run
       * the same commands you would type and show you what came back. That
       * is deliberate: nothing here can tell you something the shell would
       * not, so a player who prefers the terminal loses no information, and
       * a player who prefers the window is never shown a comforting lie. */
      { "/usr/share/applications/sysmon.desktop",
        "[Desktop Entry]\n"
        "Name=System Monitor\n"
        "Exec=sysmon\n"
        "Icon=sysmon\n"
        "Comment=Processes, services and storage\n", 0644, NULL },
      { "/usr/share/applications/pkgman.desktop",
        "[Desktop Entry]\n"
        "Name=Package Manager\n"
        "Exec=pkgman\n"
        "Icon=pkg\n"
        "Comment=What is installed, and what has changed\n", 0644, NULL },
      { "/usr/share/applications/svcman.desktop",
        "[Desktop Entry]\n"
        "Name=Service Manager\n"
        "Exec=svcman\n"
        "Icon=svc\n"
        "Comment=Start, stop and read what died\n", 0644, NULL },
      /* The music player reads /usr/share/sounds the way this desktop reads
       * /usr/share/applications: the playlist is a directory listing, so
       * `rm /usr/share/sounds/hamnix-demo.wav` empties the playlist and
       * `pkg reinstall nomnix-sounds` fills it again. A player with its
       * tracks compiled into the window would be a second source of truth
       * about a filesystem the player can already read with `ls`. */
      { "/usr/share/applications/music.desktop",
        "[Desktop Entry]\n"
        "Name=Music\n"
        "Exec=music\n"
        "Icon=music\n"
        "Comment=Play what is in /usr/share/sounds\n", 0644, NULL },
      { "/usr/share/applications/gsand.desktop",
        "[Desktop Entry]\n"
        "Name=Falling Sand\n"
        "Exec=gsand\n"
        "Icon=sand\n"
        "Comment=Twelve materials and one density table\n", 0644, NULL },
      { "/usr/share/applications/gsetris.desktop",
        "[Desktop Entry]\n"
        "Name=Sand Tetris\n"
        "Exec=gsetris\n"
        "Icon=sandtris\n"
        "Comment=Pieces dissolve; bridge a colour wall to wall\n", 0644, NULL },
      { "/usr/share/applications/clock.desktop",
        "[Desktop Entry]\n"
        "Name=Clock\n"
        "Exec=clock\n"
        "Icon=clock\n"
        "Comment=Time, calendar, timer and stopwatch\n", 0644, NULL },
      { "/usr/share/applications/imgview.desktop",
        "[Desktop Entry]\n"
        "Name=Image Viewer\n"
        "Exec=imgview\n"
        "Icon=imgview\n"
        "Comment=Look at what is picture-shaped\n", 0644, NULL },
      { "/usr/share/applications/archman.desktop",
        "[Desktop Entry]\n"
        "Name=Archive Manager\n"
        "Exec=archman\n"
        "Icon=archman\n"
        "Comment=Browse a package as a tree of files\n", 0644, NULL },
      { "/usr/share/applications/duview.desktop",
        "[Desktop Entry]\n"
        "Name=Disk Usage\n"
        "Exec=duview\n"
        "Icon=duview\n"
        "Comment=Where the disk went\n", 0644, NULL },
      { "/usr/share/applications/charmap.desktop",
        "[Desktop Entry]\n"
        "Name=Character Map\n"
        "Exec=charmap\n"
        "Icon=charmap\n"
        "Comment=Every character the font can draw\n", 0644, NULL },
      { "/usr/share/applications/search.desktop",
        "[Desktop Entry]\n"
        "Name=Search\n"
        "Exec=search\n"
        "Icon=search\n"
        "Comment=A front end to find\n", 0644, NULL },
      /* Where the display server takes requests. A file, because a file can
       * be looked at: `cat /run/nomde/requests` shows what was asked for,
       * which is the debuggability the socket version would not have. */
      { "/etc/nomde/nomde.conf",
        "# the display server\n"
        "socket = /run/nomde/requests\n"
        "applications = /usr/share/applications\n", 0644, NULL },
      { "/usr/bin/open", NULL, 0755, NULL },
      /* THE DISPLAY SERVER OWNS ITS OWN DIRECTORIES.
       *
       * /run/nomde was created at install and owned by nobody, so when the
       * deleted-directory fault took /run out, `pkg reinstall` could put back
       * /run -- which base owns -- and not /run/nomde, and nomde never came
       * up again. One seed of sixty went unfixable that way. A package owns
       * the directories it needs, or it cannot be repaired. */
      { "/run/nomde", NULL, 0755, NULL, true },
      { "/usr/share/applications", NULL, 0755, NULL, true },
    }, 36
};

/* THE SOUNDS ARE FILES, AND A PACKAGE OWNS THEM.
 *
 * The desktop plays the real samples out of its own resources -- a WAV is a
 * megabyte of PCM and the guest disk is modelled byte for byte, so putting
 * the audio itself on it would cost the player's RAM for nothing anybody can
 * hear. What lives here is the ENTRY: a name, a mode, an owner and a size,
 * which is everything `ls`, `pkg verify` and `pkg reinstall` need. The music
 * player lists this directory and shows what it finds, so deleting a track
 * removes it from the playlist and reinstalling this package brings it back.
 * That is the same bargain /usr/share/applications already makes with the
 * launcher, and it is what stops the playlist being a hardcoded array that
 * disagrees with the disk. */
static const Package PKG_SOUNDS = {
    "nomnix-sounds", "1.2", "the system sounds",
    {
      { "/usr/share/sounds", NULL, 0755, NULL, true },
      { "/usr/share/sounds/boot-jingle.wav",
        "RIFF WAVE 44100Hz 16bit stereo -- the startup jingle\n", 0644, NULL },
      { "/usr/share/sounds/hamnix-demo.wav",
        "RIFF WAVE 44100Hz 16bit stereo -- Hamnix music demo\n", 0644, NULL },
    }, 3
};

static const Package PKG_SHELL = {
    "nomsh", "1.9", "the shell and the base tools",
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
      /* Essential here specifically: when the disk's own libc is wrong,
       * nothing on that disk runs, so the only ldd you can use is this one --
       * pointed at the broken binary through /mnt. */
      { "/usr/bin/ldd", NULL, 0755, NULL },
      /* Essential on the live medium: the whole point is reading the log of a
       * boot that failed, from a system that did not. */
      { "/bin/dmesg", NULL, 0755, NULL },
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
      /* The remote console. It lives on the technician's workstation, and
       * on the customer's machine too -- every NomnixOS install has it,
       * because every one of them might be the machine you are calling from
       * when the next ticket comes in. */
      { "/usr/bin/rcon", NULL, 0755, NULL },
      /* Both of these exist because the model kept reaching for them and a
       * playtester kept wanting them. When the thing everyone expects is
       * missing, the answer is to build it, not to explain its absence. */
      { "/usr/bin/find", NULL, 0755, NULL },
      { "/bin/netstat",  NULL, 0755, NULL },
      /* `init 6` worked and `reboot` did not. Nobody types `init 6` first. */
      { "/sbin/reboot",   NULL, 0755, NULL },
      { "/sbin/halt",     NULL, 0755, NULL },
      { "/sbin/poweroff", NULL, 0755, NULL },
      /* `init 0` rebooted the machine because nothing implemented it. On a
       * real system /sbin/init IS pid 1 and ALSO acts as telinit when a user
       * runs it with a runlevel, so that is what init.c does now. This is
       * only the second name for it. */
      { "/sbin/telinit", NULL, 0755, NULL },
    }, 40
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
        "url = https://packages.nomnix.org/11.4\n", 0644, NULL },
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
      /* ldd ships with the C library on a real distribution, and for a real
       * reason: it has to agree with that library's loader about how a
       * dependency is resolved. Ours reads the same ELF section through the
       * same code the loader uses. */
      { "/usr/bin/ldd",    NULL, 0755, NULL },
    }, 5
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
        "#\n"
        "# Rules of this file, learned expensively:\n"
        "#   1. a job that has no log has never run\n"
        "#   2. a job you cannot run by hand is not a job, it is a rumour\n"
        "#   3. the line below with DISABLED on it is disabled. Read ~/TODO\n"
        "#      before you decide it looks harmless. -- nomowner\n"
        "17 *  * * *  /usr/sbin/logrotate /etc/logrotate.conf\n"
        "0  4  * * *  /home/nomowner/bin/cleanup   # DISABLED, see TODO\n", 0644, NULL },
      { "/var/spool/cron/root", "# no personal jobs\n", 0600, NULL },
          { "/var/spool/cron", NULL, 0755, NULL, true },
    }, 5
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
          { "/var/lib/ntp", NULL, 0755, NULL, true },
    }, 4
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
      /* A README in the document root, which is where every real web server
       * has one. It is also the only place a note about httpd is certain to
       * be found by somebody who is already looking at httpd. */
      { "/srv/www/README",
        "This is DocumentRoot. /etc/httpd/httpd.conf says so, and httpd checks\n"
        "that this directory exists before it will start -- so if the web\n"
        "server is refusing to come up and the config looks perfect, ask\n"
        "whether the directory the config NAMES is still here.\n"
        "\n"
        "  grep DocumentRoot /etc/httpd/httpd.conf\n"
        "  ls /srv/www\n"
        "  svc status httpd\n"
        "\n"
        "Do not put anything secret in here. That is the entire point of the\n"
        "directory and people forget it about twice a year.\n"
        "\n"
        "-- nomowner. If the Listen line surprises you, read\n"
        "   /home/nomowner/Documents/handover.txt before you \"fix\" it.\n", 0644, NULL },
      { "/etc/services.d/httpd.svc",
        "# /etc/services.d/httpd.svc\n"
        "name: httpd\nexec: /usr/sbin/httpd\n"
        "description: web server\nafter: net\n"
        "restart: on-failure\nenabled: yes\nrunlevel: 3 5\n", 0644, NULL },
    }, 5
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
        "  pkg --root DIR <verb>    work on a filesystem mounted somewhere\n"
        "                           else, WITHOUT chrooting into it. The only\n"
        "                           way in when the disk's own libc is broken\n"
        "                           and nothing on it will run.\n"
        "  pkg reinstall <name>     put a package's files back. Config files\n"
        "                           that were edited on this machine are LEFT\n"
        "                           ALONE -- somebody chose those settings.\n"
        "  pkg reinstall --force <name>\n"
        "                           overwrite them too. There is no undo, so\n"
        "                           `pkg diff` them first.\n"
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
        "  kernel, initrd   /boot/vmnomuz, /boot/initrd (SYMLINKS)\n"
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
      { "/usr/share/man/ldd",
        "ldd(1)\n\n"
        "  ldd <program>      the libraries a program needs, where each one\n"
        "                     was found, and whether it is new enough\n"
        "\n"
        "Resolution follows /etc/ld.so.conf in order, exactly as the loader\n"
        "does, so ldd cannot disagree with what happens when you run the\n"
        "program. A library that is installed but sits in a directory nobody\n"
        "lists reads as `not found`, which is the fault, stated plainly.\n"
        "\n"
        "Not every program needs the same libraries. When some services are\n"
        "dead and others are fine, ldd on one of each is the fastest way to\n"
        "see what the dead ones have in common.\n"
        "\n"
        "From the rescue medium, name the broken binary through the mount:\n"
        "  ldd /mnt/usr/sbin/httpd\n"
        "That works even when the disk's own libc is too broken to run\n"
        "anything at all, which is when you need it most.\n", 0644, NULL },
    }, 6
};

/* THE JOKE PACKAGE, built exactly like the serious ones.
 *
 * It is here for two reasons beyond being funny. The first is that a machine
 * with nothing pointless on it does not feel like a machine anyone used; every
 * box that has ever had an administrator has a cow on it somewhere. The second
 * is that these are ORDINARY packages and ORDINARY binaries -- `pkg owns
 * /usr/bin/sl` answers, `ldd /usr/bin/cowsay` lists libc, a bad libc kills the
 * train along with everything else -- so poking at the toys teaches exactly
 * the same tools as poking at sshd, with none of the fear.
 *
 * The fortunes are DATA, in a file, not strings inside the binary. That is the
 * difference between something you can `cat`, `grep`, `wc`, damage, verify and
 * repair, and something you can only run. */
static const Package PKG_FUN = {
    "nomfun", "1.4", "the fortune cookie, the cow and the train",
    {
      { "/usr/bin/fortune", NULL, 0755, NULL },
      { "/usr/bin/cowsay",  NULL, 0755, NULL },
      { "/usr/bin/sl",      NULL, 0755, NULL },
      /* One per line, because `fortune` reads a line and because that makes
       * the file greppable. Blank lines and #comments are skipped, and an
       * indented line continues the one above it -- which is the same shape
       * ~nomowner's own fortunes file already had. */
      { "/usr/share/fortunes",
        "# /usr/share/fortunes -- read by /usr/bin/fortune, one per line.\n"
        "# An indented line continues the one above it. Comments and blank\n"
        "# lines are skipped. Add your own; nothing here is compiled in.\n"
        "Backups are a theory. Restores are a fact.\n"
        "There is no cloud. There is only somebody else's /dev/sda1.\n"
        "ls sees the arrow. stat follows it. Only one of them is your friend.\n"
        "The machine is always right. The machine is describing what you did.\n"
        "Any sufficiently enthusiastic cleanup script is an attacker with a\n"
        "  crontab entry.\n"
        "It is not a bug, it is an undocumented feature of the initrd.\n"
        "df first. It is free, it takes two seconds, and it has been the\n"
        "  answer twice.\n"
        "A service can be running and still wrong. Ask /run for what it loaded.\n"
        "\"Nothing changed\" means nothing changed that they wish to discuss.\n"
        "The uptime record and the patch level are the same conversation.\n"
        "Nobody wants a backup. Everybody wants a restore.\n"
        "pkg verify is clean and it still will not boot: the boot sector is\n"
        "  not a file.\n"
        "Every well-formed UUID belongs to some disk. Not necessarily this one.\n"
        "To err is human. To blame the previous administrator is systems\n"
        "  administration.\n"
        "The fastest way to learn who owns a file is pkg owns. The second\n"
        "  fastest is to delete it.\n"
        "Reinstalling a package you have not diffed is how you get two problems.\n"
        "Read the console before the wiki. The console was there.\n"
        "A reboot is a diagnostic, not a repair. It only destroys the evidence\n"
        "  faster.\n"
        "Log rotation is a chore right up until the morning it is an outage.\n"
        "The correct number of critical services a monitoring agent may add to\n"
        "  your boot is zero.\n"
        "Everything broken means libc. Two things broken means asking what\n"
        "  those two have in common.\n"
        "ldd never disagrees with the loader. People do.\n"
        "Documentation is a love letter you write to yourself at four in the\n"
        "  morning.\n"
        "The severity of an outage is proportional to how recently somebody\n"
        "  said \"it's fine\".\n"
        "Never trust a config you have not cat'ed today.\n"
        "There are two hard problems in this job: naming things, cache\n"
        "  invalidation, and off-by-one errors.\n"
        "A mode of 000 is not a mystery. It is a person, and they will do it\n"
        "  again.\n"
        "The disk is never full of anything interesting.\n"
        "df -i exists for the one day a year when it is the only answer.\n"
        "Do not rm in anger. rm does not care and cannot be argued with.\n"
        "A migration is complete when the old machine is switched off, and not\n"
        "  one day before.\n"
        "Every temporary mount outlives the person who made it.\n"
        "If you did not write it down it did not happen, and if you wrote it in\n"
        "  /tmp, ask yourself who cleans /tmp.\n"
        "Nine tickets in ten are one word in one line of one file.\n"
        "It is always DNS, except here, where it is /etc/hosts and you typed it\n"
        "  yourself.\n"
        "A machine that boots is not the same thing as a machine that is well.\n"
        "The bootloader has one job and reads one file. Read the same file.\n"
        "Somebody's local edit is somebody's afternoon. Do not --force without\n"
        "  looking.\n"
        "Yes, it worked in testing. Testing is where the wrong libc lives.\n"
        "Two administrators, one /etc, no agreement. This is why ns exists.\n"
        "The train has cost me more hours than any outage and I regret nothing.\n"
        "Root is not a skill level.\n"
        "The last person who touched it is not the person who broke it, but\n"
        "  they are the person you can still telephone.\n", 0644, NULL },
      { "/usr/share/man/fortune",
        "fortune(6)\n\n"
        "  fortune            one line from /usr/share/fortunes\n"
        "  fortune FILE       one line from FILE\n"
        "\n"
        "One fortune per line; an indented line continues the one above, and\n"
        "#comments and blank lines are skipped.\n"
        "\n"
        "The quotes are a file, not part of the program: cat, grep and wc all\n"
        "work on /usr/share/fortunes, and pkg verify nomfun notices when it has\n"
        "been damaged. Try `fortune /home/nomowner/fortunes` for the previous\n"
        "administrator's own list.\n"
        "\n"
        "There is no clock on this machine, so the choice is made from the\n"
        "process id. Two runs in a row give different lines; the same pid would\n"
        "give the same line, which nothing here can arrange.\n", 0644, NULL },
      { "/usr/share/man/cowsay",
        "cowsay(6)\n\n"
        "  cowsay <words>     a cow says it\n"
        "  cowsay -f <face>   cow (default), tux, dragon, daemon\n"
        "  ... | cowsay       with no words it reads stdin, which is the point\n"
        "\n"
        "  fortune | cowsay -f tux\n"
        "\n"
        "The balloon is measured: the text wraps at 40 columns and the box is\n"
        "as wide as the longest line that came out of the wrap.\n", 0644, NULL },
      { "/usr/share/man/sl",
        "sl(6)\n\n"
        "  sl                 a steam locomotive, because you meant ls\n"
        "\n"
        "It does not animate. Nothing on this machine redraws the screen, and a\n"
        "program that pretended to would be the only dishonest thing in\n"
        "/usr/bin, so the train is drawn all at once with its smoke behind it.\n", 0644, NULL },
    }, 7
};

static const Package PKG_MAIL = {
    "postfix", "3.8", "mail transport",
    {
      { "/usr/sbin/postfix", NULL, 0755, NULL },
      { "/etc/postfix/main.cf",
        "myhostname = nominal.local\nrelayhost = 10.0.2.30\n", 0644, NULL },
      { "/etc/aliases", "root: nomowner\npostmaster: root\n", 0644, NULL },
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
    &PKG_SOUNDS,
    &PKG_HOME, &PKG_PKGCONF, &PKG_LIBC, &PKG_ZLIB, &PKG_CRON, &PKG_LOGROTATE, &PKG_NTP,
    &PKG_HTTPD, &PKG_FIREWALL, &PKG_MAN, &PKG_MAIL, &PKG_ACCT, &PKG_TZ,
    &PKG_TERMINFO, &PKG_AUDIT, &PKG_FUN,
};
#define IMAGE_N ((int)(sizeof IMAGE / sizeof IMAGE[0]))

void image_generated(const Machine *m, const char *path, Buf *out);
static void install_local_edits(Machine *m, uint64_t seed);
static void install_history(Machine *m);

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
        "echo   links wiki.nomnix.org/rescue   for the full procedure\n"
        "echo\n", 0755, NULL },
      { "/etc/hostname", "rescue\n", 0644, NULL },
      { "/etc/issue",    "NomnixOS rescue 3.2 -- live medium\n", 0644, NULL },
      { "/etc/os-release",
        "NAME=\"NomnixOS Rescue\"\nVERSION=\"3.2\"\nID=nomnix-rescue\n", 0644, NULL },
      { "/etc/fstab", "# nothing is mounted automatically on the rescue medium\n",
        0644, NULL },
      { "/etc/hosts",
        "127.0.0.1       localhost\n"
        "10.0.2.20       wiki.nomnix.org wiki\n"
        "10.0.2.30       support.internal support\n"
        "10.0.2.44       bofh.nomnix.org bofh\n", 0644, NULL },
      { "/etc/resolv.conf", "nameserver 10.0.2.3\n", 0644, NULL },
      /* The live medium has its OWN libc. That is the whole point of it: when
       * the customer's libc is wrong, nothing on their disk runs, including
       * the tools you would fix it with. */
      { "/lib/libc.so.6",  "stub libc 2.38\n", 0755, NULL },
      { "/lib/libm.so.6",  "stub libm 2.38\n", 0755, NULL },
      { "/etc/ld.so.conf", "/lib\n/usr/lib\n", 0644, NULL },
      { "/etc/motd",
        "NomnixOS rescue medium.\n"
        "  the customer disk is /dev/sda1 and is not mounted\n"
        "  links wiki.nomnix.org/rescue    for the procedure\n", 0644, NULL },
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
      /* Essential here specifically: when the disk's own libc is wrong,
       * nothing on that disk runs, so the only ldd you can use is this one --
       * pointed at the broken binary through /mnt. */
      { "/usr/bin/ldd", NULL, 0755, NULL },
      /* Essential on the live medium: the whole point is reading the log of a
       * boot that failed, from a system that did not. */
      { "/bin/dmesg", NULL, 0755, NULL },
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
    }, 35
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
    else if (strcmp(path, "/usr/bin/ldd") == 0)
        buf_put(out, (const char *)GUEST_LDD, GUEST_LDD_LEN);
    else if (strcmp(path, "/bin/dmesg") == 0)
        buf_put(out, (const char *)GUEST_DMESG, GUEST_DMESG_LEN);
    else if (strcmp(path, "/usr/bin/rcon") == 0)
        buf_put(out, (const char *)GUEST_RCON, GUEST_RCON_LEN);
    else if (strcmp(path, "/usr/bin/find") == 0)
        buf_put(out, (const char *)GUEST_FIND, GUEST_FIND_LEN);
    else if (strcmp(path, "/bin/netstat") == 0)
        buf_put(out, (const char *)GUEST_NETSTAT, GUEST_NETSTAT_LEN);
    else if (strcmp(path, "/usr/bin/nomde") == 0)
        buf_put(out, (const char *)GUEST_NOMDE, GUEST_NOMDE_LEN);
    else if (strcmp(path, "/usr/bin/open") == 0)
        buf_put(out, (const char *)GUEST_OPEN, GUEST_OPEN_LEN);
    else if (strcmp(path, "/usr/bin/fortune") == 0)
        buf_put(out, (const char *)GUEST_FORTUNE, GUEST_FORTUNE_LEN);
    else if (strcmp(path, "/usr/bin/cowsay") == 0)
        buf_put(out, (const char *)GUEST_COWSAY, GUEST_COWSAY_LEN);
    else if (strcmp(path, "/usr/bin/sl") == 0)
        buf_put(out, (const char *)GUEST_SL, GUEST_SL_LEN);
    else if (strcmp(path, "/sbin/telinit") == 0)
        buf_put(out, (const char *)GUEST_INIT, GUEST_INIT_LEN);
    else if (strcmp(path, "/sbin/reboot") == 0 ||
             strcmp(path, "/sbin/halt") == 0 ||
             strcmp(path, "/sbin/poweroff") == 0)
        buf_put(out, (const char *)GUEST_REBOOT, GUEST_REBOOT_LEN);
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
        buf_puts(out, "entry \"NomnixOS 11.4\"\n");
        buf_puts(out, "  kernel /boot/vmnomuz\n");
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
    if (f->isdir) {
        VNode *n = vfs_mkdir(&m->disk, f->path);
        if (n) n->mode = f->mode;
        return;
    }
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
            if (f->isdir) {
                /* Three fields like every other line; the mode goes where a
                 * file's hash would, because a directory has no contents to
                 * hash and its mode is the thing worth checking. */
                buf_printf(&man, "dir %04o %s\n", f->mode, f->path);
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
        "/bin", "/boot", "/boot/zbl", "/dev", "/etc", "/etc/nomde",
        "/usr/share/applications", "/run/nomde",
        "/etc/net", "/etc/rc.d", "/etc/services.d", "/etc/ssh", "/etc/udev",
        "/etc/udev/rules.d", "/home", "/home/nomowner", "/home/nomowner/bin",
        /* A HOME DIRECTORY LOOKS LIKE SOMEBODY LIVES IN IT. Everything was
         * dumped straight in /home/nomowner, which is not what anyone's home
         * looks like -- there is always a Desktop, always a Documents, always
         * a Downloads full of things they meant to sort out. */
        "/home/nomowner/Desktop", "/home/nomowner/Documents",
        "/home/nomowner/Downloads", "/home/nomowner/Pictures",
        "/root/Desktop", "/root/Documents", "/root/Downloads",
        "/lib", "/lib/modules",
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
    install_history(m);
    install_local_edits(m, seed);
    m->fs_capacity = machine_disk_used(m) + 512u * 1024u;
    /* Headroom in inodes as well as bytes, so a healthy machine can create
     * files freely and a fault has to work to exhaust them. */
    m->fs_inodes_max = machine_inodes_used(m) + 400u;
    install_rescue(m);
    m->next_pid = 1;
}

/* THINGS NO PACKAGE OWNS, because in life no package owns them: logs.
 *
 * A machine that has been running since January has a log with January in it.
 * These are written straight onto the disk rather than shipped by a package,
 * which is not a shortcut -- it is the truth about what a log is, and `pkg
 * owns /var/log/messages` answering "nothing" is a fact worth being able to
 * discover. It also keeps them out of `pkg verify`: a log that matched a hash
 * would be a log nothing had written to.
 *
 * syslogd appends its own banner to /var/log/messages at every boot, so this
 * is history and the newest line is always the machine's own.
 */
static void install_history(Machine *m)
{
    VNode *n = vfs_mkfile(&m->disk, "/var/log/messages",
        "syslogd: started, logging to /var/log/messages\n"
        "netd: eth0 configured\n"
        "sshd: refused connect from 10.0.2.88\n"
        "sshd: refused connect from 10.0.2.88\n"
        "sshd: refused connect from 10.0.2.88 (this went on for a week)\n"
        "ntpd: no reply from 10.0.2.3, will retry\n"
        "crond: (root) CMD (/usr/sbin/logrotate /etc/logrotate.conf)\n"
        "udevd: could not open /dev/input/event3: no such device\n"
        "httpd: document root /srv/www ok\n"
        "auditd: log opened\n"
        "nomde: display server ready\n");
    if (n) n->mode = 0644;

    /* The rotated one. logrotate.conf says weekly, rotate 8; this is what
     * came before, and it is the March outage as the machine saw it. */
    n = vfs_mkfile(&m->disk, "/var/log/messages.1",
        "syslogd: started, logging to /var/log/messages\n"
        "crond: (root) CMD (/usr/sbin/logrotate /etc/logrotate.conf)\n"
        "crond: (root) CMD (/home/nomowner/bin/cleanup)\n"
        "cleanup: removing stale kernel images\n"
        "cleanup: /boot/vmnomuz-6.4.11 removed\n"
        "cleanup: done, 1 file removed, 0 errors\n"
        "-- machine did not come back. 6 hours. See ~/TODO, item 3.\n"
        "syslogd: started, logging to /var/log/messages\n"
        "ntpd: no reply from 10.0.2.3, will retry\n"
        "ntpd: no reply from 10.0.2.3, will retry\n"
        "sshd: refused connect from 10.0.2.88\n"
        "udevd: could not open /dev/input/event3: no such device\n"
        "syslogd: /var/log/messages: cannot write -- is the disk full?\n"
        "-- it was. df said 100%, every hash matched, nothing was corrupt.\n"
        "-- growing since January. ~/Documents/postmortem-march.txt.\n"
        "syslogd: started, logging to /var/log/messages\n");
    if (n) n->mode = 0644;
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
/* How many legitimate local edits exist, and a way to install exactly one of
 * them. `--health` walks all of them, because a decoy that breaks the machine
 * is a fairness bug of the worst kind: the player is told a deliberate-looking
 * edit is innocent by every signal the game gives, and it is the fault.
 *
 * One shipped. /etc/httpd/httpd.conf said `listen`/`root` where httpd wants
 * `Listen`/`DocumentRoot`, so that decoy silently killed the web server. It
 * survived a 20-machine health run because 17 decoys drawn 2-5 at a time do
 * not cover themselves in twenty tries. Now they are covered on purpose. */
int local_edit_count(void);

static void install_local_edits(Machine *m, uint64_t seed)
{
    Rng r;
    rng_seed(&r, seed ^ 0xc0ffee1234ULL);

    /* A wide pool, and SEVERAL WORDINGS EACH. A playtester reported that by
     * the fourth machine they filtered the decoys on sight without reading
     * them -- which is exactly right, because there were six files with one
     * fixed text apiece, so `/etc/ssh/sshd_config` always said "hardened
     * after the audit". A decoy you recognise is not a decoy, it is a
     * landmark. Rotating the wording is what makes you read the file. */
    struct { const char *path; const char *content; } EDITS[] = {
      { "/etc/resolv.conf",
        "# changed 12 March -- the .3 resolver was timing out at peak\n"
        "nameserver 10.0.2.9\n"
        "search nomnix.org\n" },
      { "/etc/resolv.conf",
        "nameserver 10.0.2.3\n"
        "# second one added after the outage in Feb, do not remove\n"
        "nameserver 10.0.2.9\n" },
      { "/etc/hosts",
        "127.0.0.1       localhost nominal.local\n"
        "10.0.2.20       wiki.nomnix.org wiki\n"
        "10.0.2.30       support.internal support\n"
        "10.0.2.44       bofh.nomnix.org bofh\n"
        "# added for the migration, remove when dock-2 is retired\n"
        "10.0.2.61       oldbilling.internal oldbilling\n" },
      { "/etc/hosts",
        "127.0.0.1       localhost\n"
        "10.0.2.20       wiki.nomnix.org wiki\n"
        "10.0.2.30       support.internal support\n"
        "10.0.2.44       bofh.nomnix.org bofh\n"
        "# pinning this until DNS is fixed -- J.\n"
        "10.0.2.31       licences.internal licences\n" },
      { "/etc/ssh/sshd_config",
        "# hardened after the audit, do not revert\n"
        "Port 2222\n"
        "PermitRootLogin no\n"
        "MaxAuthTries 3\n" },
      { "/etc/ssh/sshd_config",
        "Port 22\n"
        "# left root login on for the console cart -- ops asked, ticket 8841\n"
        "PermitRootLogin yes\n" },
      { "/etc/syslog.conf",
        "# quieten the udev chatter, it was filling the disk\n"
        "*.info /var/log/messages\n"
        "udev.* /dev/null\n" },
      { "/etc/syslog.conf",
        "*.info /var/log/messages\n"
        "# cron was noisy every minute, dropped it 4 Jan\n"
        "cron.* /dev/null\n" },
      { "/etc/net/interfaces",
        "# static since the dhcp lease kept moving us\n"
        "iface eth0\n"
        "  address 10.0.2.15\n"
        "  gateway 10.0.2.2\n" },
      { "/etc/net/interfaces",
        "iface eth0\n"
        "  address 10.0.2.15\n"
        "  gateway 10.0.2.2\n"
        "# mtu lowered for the tunnel, see the runbook\n"
        "  mtu 1400\n" },
      { "/etc/profile",
        "# login shell profile\n"
        "PATH=/bin:/usr/bin:/sbin\n"
        "# added by nomowner: I got tired of typing it\n"
        "alias v=pkg verify\n" },
      { "/etc/profile",
        "# login shell profile\n"
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin\n"
        "# sbin on the path so I stop getting command not found -- nomowner\n" },
      { "/etc/crontab",
        "# nightly log trim, added after we filled the disk in March\n"
        "0 3 * * *  root  rm /var/log/messages\n" },
      { "/etc/ntp.conf",
        "server 10.0.2.4\n"
        "# second source added after the drift complaint\n"
        "server 10.0.2.5\n" },
      { "/etc/httpd/httpd.conf",
        "# port moved off 80, the load balancer terminates now\n"
        "Listen 8080\nDocumentRoot /srv/www\nServerName nominal.local\n" },
      { "/etc/motd",
        "Welcome to NomnixOS.\n"
        "\n"
        "*** dock-2 is scheduled for migration. Do NOT reboot without\n"
        "*** telling ops first. -- J.\n" },
      { "/etc/default/postfix",
        "myhostname = node.nomnix.org\n"
        "# relay added when we lost direct outbound, 9 Feb\n"
        "relayhost = 10.0.2.7\n" },
    };
    const int NEDITS = (int)(sizeof EDITS / sizeof EDITS[0]);

    /* NOM_FORCE_EDIT=<n>: install exactly decoy n and nothing else. */
    const char *fe = getenv("NOM_FORCE_EDIT");
    if (fe) {
        int i = atoi(fe) % NEDITS;
        VNode *n = vfs_lookup(&m->disk, EDITS[i].path);
        if (n && n->kind == VN_FILE) {
            buf_clear(&n->data);
            buf_puts(&n->data, EDITS[i].content);
            buf_puts(&m->local_orig[m->nlocal], EDITS[i].content);
            snprintf(m->local[m->nlocal], NOM_PATH_MAX, "%s", EDITS[i].path);
            m->nlocal++;
        }
        return;
    }

    /* Two to five of them, chosen by the seed. More than before, because the
     * point is that `pkg verify` output has to be READ rather than skimmed
     * for the one familiar line. Duplicates by path are skipped, so a machine
     * never gets two versions of the same file. */
    int want = 2 + (int)(rng_next(&r) % 4);
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
        if (!lost) buf_puts(out,
            "\nyou overwrote local configuration:\n");
        buf_printf(out, "  %s\n", m->local[i]);
        lost++;
    }
    if (lost)
        buf_puts(out,
            "  this machine's own settings are gone and there is no undo.\n"
            "  somebody chose them on purpose; `pkg diff` shows what a file\n"
            "  says against what the package ships, and plain `pkg reinstall`\n"
            "  (without --force) leaves edited config alone.\n");
    return lost;
}

/* Re-baseline the local edits against the disk AS THE PLAYER RECEIVES IT.
 *
 * The collateral report asks one question: did YOU destroy something that was
 * there when you arrived. It compared against the edits as INSTALLED, which is
 * a different question -- if the breaker then corrupted one of those files,
 * the report fired before the player had typed a single command, blaming them
 * for damage the ticket shipped with. A playtester spent twenty minutes trying
 * to work out what it meant and concluded, reasonably, that the same message
 * means both "you broke this" and "the customer broke this".
 *
 * Called once the machine is broken and before anyone touches it. */
void machine_rebaseline_local(Machine *m)
{
    for (int i = 0; i < m->nlocal; i++) {
        VNode *n = vfs_lookup(&m->disk, m->local[i]);
        buf_clear(&m->local_orig[i]);
        if (n && n->kind == VN_FILE && n->data.len)
            buf_put(&m->local_orig[i], n->data.p, n->data.len);
    }
}

/* DAMAGE THAT IS STILL THERE, whether or not the machine boots.
 *
 * A playtester took a three-fault ticket, repaired two, booted, and was told
 * [UP at target] with no complaint -- while /etc/udev/rules.d/50-default.rules
 * still read SUBSYSTEM=="bhock". The health check only ever asked whether the
 * services were running, so a fault that has not broken anything YET signs off
 * as fixed. That undercuts the whole premise: the ticket is "prove it is
 * healthy", not "prove it starts today".
 *
 * A package file that no longer matches what the package ships, and that is
 * not one of this machine's own local edits, is outstanding damage. That
 * definition needs no cooperation from the faults and no list of what was
 * injected -- it is just the truth about the disk.
 */
int machine_outstanding(Machine *m, Buf *out)
{
    int bad = 0;
    for (int i = 0; i < m->npkg; i++) {
        const Package *p = m->pkg[i];
        for (int j = 0; j < p->nfiles; j++) {
            const PkgFile *f = &p->file[j];
            if (f->isdir || f->link) continue;

            bool is_local = false;
            for (int k = 0; k < m->nlocal; k++)
                if (strcmp(m->local[k], f->path) == 0) is_local = true;
            if (is_local) continue;

            VNode *n = vfs_lookup(&m->disk, f->path);
            Buf want = {0};
            pristine(m, f, &want);
            bool differs = !n || n->kind != VN_FILE ||
                           n->data.len != want.len ||
                           (want.len && memcmp(n->data.p, want.p, want.len) != 0);
            buf_free(&want);
            if (!differs) continue;

            if (!bad) buf_puts(out,
                "\nthe machine is up, but this is still not as its package "
                "shipped it:\n");
            if (bad < 6) buf_printf(out, "  %s  (%s)\n", f->path, p->name);
            bad++;
        }
    }
    if (bad) buf_puts(out,
        "  a fault that has not broken anything yet is still a fault --\n"
        "  `pkg diff <path>` to see it.\n");
    return bad;
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
        if (f->isdir) {
            if (n->kind != VN_DIR)       { buf_printf(out, "%s changed\n", f->path); (*bad)++; }
            else if (n->mode != f->mode) { buf_printf(out, "%s mode\n",    f->path); (*bad)++; }
            continue;
        }
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
        if (p->file[j].isdir || p->file[j].link) {
            /* A directory and a symlink both have no bytes to hand back. They
             * are RESTORED by pkg_restore_path, never here.
             *
             * This function used to do the restoring itself, which made it a
             * read that wrote: `pkg diff /boot/vmnomuz` on a dangling symlink
             * silently repaired the symlink and solved the machine for the
             * player. A blind playtester hit exactly that, watched `ls` show
             * a healthy link one command after `stat` said the path did not
             * exist, and reasonably called it the worst bug in the game. */
            return true;
        }
        pristine(m, &p->file[j], out);
        return true;
    }
    return false;
}

/* Put one path back the way the package ships it. The MUTATING counterpart of
 * pkg_file_content, kept separate so that fetching a file can never change the
 * machine -- `pkg diff` reads, `pkg reinstall` writes, and the two must not
 * share a code path that does both. */
bool pkg_restore_path(Machine *m, const char *pkgname, const char *path)
{
    const Package *p = pkg_find(m, pkgname);
    if (!p) return false;
    for (int j = 0; j < p->nfiles; j++) {
        if (strcmp(p->file[j].path, path) != 0) continue;
        if (p->file[j].isdir) {
            VNode *d = vfs_mkdir(&m->disk, path);
            if (d) d->mode = p->file[j].mode;
            return true;
        }
        if (p->file[j].link) {
            vfs_remove(&m->disk, path);
            vfs_symlink(&m->disk, p->file[j].link, path);
            return true;
        }
        vfs_remove(&m->disk, path);
        install_file(m, &p->file[j]);
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
