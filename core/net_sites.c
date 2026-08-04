/* net_sites.c — the virtual network.
 *
 * A handful of hosts with real pages on them. This is not decoration: the
 * wiki is where the boot process is actually documented, so a player who
 * explores is rewarded with knowledge they can use, and a player who does not
 * has to work it out from the machine itself. Both are legitimate.
 *
 * Reaching a site requires the machine's own networking to work:
 * /etc/hosts and /etc/resolv.conf are read by the browser and consulted by
 * the resolver. Damage either and the network breaks in the way networks
 * actually break -- "it works by IP but not by name" is a real diagnosis and
 * now a possible one.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"

typedef struct {
    const char *host;
    const char *ip;
    const char *path;
    const char *body;
} Page;

/* Anything reachable. The addresses matter: they are what /etc/hosts maps to
 * and what the resolver returns, so a corrupted hosts file sends the browser
 * somewhere that is not there. */
static const Page PAGES[] = {

{ "wiki.hamnix.org", "10.0.2.20", "/",
"Hamnix wiki\n"
"===========\n"
"\n"
"  /boot          how this system boots, stage by stage\n"
"  /packages      the package database, verify and reinstall\n"
"  /rescue        booting the live medium and repairing a disk\n"
"  /namespaces    bind, and why nothing being corrupt is still a fault\n"
"  /faq           things people ask twice\n"
"\n"
"  links wiki.hamnix.org/boot\n"
},

{ "wiki.hamnix.org", "10.0.2.20", "/boot",
"How Hamnix boots\n"
"================\n"
"\n"
"Seven stages. Each reads real files, and each fails differently, so the\n"
"stage a machine dies at tells you where to look.\n"
"\n"
"  firmware    finds a boot sector on /dev/sda\n"
"  zbl         reads /boot/zbl/zbl.cfg: kernel, initrd, root UUID\n"
"  kernel      loads /boot/vmlinuz -- a SYMLINK to the versioned image\n"
"  initrd      loads /boot/initrd, needs virtio_blk and ext4 modules,\n"
"              then finds the root filesystem by UUID\n"
"  init        /sbin/init -> /usr/lib/sysinit/init, reads /etc/inittab\n"
"  rc          /bin/rc runs /etc/rc.boot, then /etc/rc.d/rc.3\n"
"  services    /sbin/svcinit reads /etc/services.d/*.svc\n"
"\n"
"The commonest thing people miss: /boot/vmlinuz and /boot/initrd are\n"
"symlinks. If the file they point at is gone, the loader reports the LINK\n"
"and the target, and `ls` shows the link looking perfectly healthy.\n"
"`stat /boot/vmlinuz` will tell you the truth.\n"
},

{ "wiki.hamnix.org", "10.0.2.20", "/packages",
"Packages\n"
"========\n"
"\n"
"Every file that matters is owned by a package. The database lives on the\n"
"machine at /var/lib/pkg/<name>/files -- `mode hash path`, one per line.\n"
"\n"
"  pkg [--root DIR] ...    work on a filesystem mounted elsewhere, without\n"
"                          chrooting into it -- which you cannot do when the\n"
"                          disk's own libc is broken\n"
"  pkg list                what is installed\n"
"  pkg verify              hash everything, report what differs\n"
"  pkg verify <name>       just one package\n"
"  pkg owns <path>         what would I be reinstalling\n"
"  pkg reinstall <name>    fetch pristine copies and put them back. Files\n"
"                          under /etc that have been EDITED are kept, and\n"
"                          named, because a package ships a default and an\n"
"                          administrator makes a decision.\n"
"  pkg reinstall --force   overwrite them anyway. Look with `pkg diff` first.\n"
"\n"
"reinstall pulls from a repository that is NOT on the machine, which is why\n"
"it works on a disk with nothing good left on it.\n"
"\n"
"verify reports:\n"
"  MISSING            the file is gone\n"
"  MISSING (symlink)  a symlink is gone\n"
"  REPOINTED          a symlink points somewhere new\n"
"  CHANGED            contents differ from what was shipped\n"
"  MODE               contents are fine, permissions are not\n"
},

{ "wiki.hamnix.org", "10.0.2.20", "/rescue",
"Rescue\n"
"======\n"
"\n"
"The live medium is /dev/sr0 and is never damaged. Boot it and the\n"
"customer's disk is /dev/sda1, not mounted.\n"
"\n"
"  mount /dev/sda1 /mnt\n"
"  for i in dev sys proc; do mount /$i /mnt/$i; done\n"
"  chroot /mnt\n"
"  pkg verify\n"
"\n"
"The bind mounts matter: after chroot, /proc and /dev have to be the ones\n"
"you already had, because the disk you are repairing cannot provide them.\n"
"\n"
"`mount` with no arguments prints the table. `chroot` with nothing mounted\n"
"at the target will refuse, which is usually the mistake.\n"
},

{ "wiki.hamnix.org", "10.0.2.20", "/namespaces",
"Namespaces\n"
"==========\n"
"\n"
"Every process has its own view of the filesystem, inherited from its\n"
"parent. `bind TARGET AT` makes lookups under AT resolve to TARGET.\n"
"Longest prefix wins.\n"
"\n"
"  bind /etc /mnt        now /mnt/passwd is /etc/passwd\n"
"  ns                    print the current namespace\n"
"  cat /proc/<pid>/ns    print another process's\n"
"\n"
"WHY YOU CARE. A bad bind is a fault where nothing is corrupt. Every file\n"
"passes `pkg verify` and the machine still reads the wrong one. If verify\n"
"is clean and the machine is still wrong, look at the namespace.\n"
},

{ "wiki.hamnix.org", "10.0.2.20", "/faq",
"FAQ\n"
"===\n"
"\n"
"Q. `ls` says the file is there and the loader says it is not.\n"
"A. It is a symlink and its target is gone. `stat` follows links; `ls`\n"
"   shows you the link itself.\n"
"\n"
"Q. pkg verify lists half a dozen files. Which one is the fault?\n"
"A. Probably none of them on their own. This machine has been\n"
"   administered by a person, and their deliberate edits show up as\n"
"   CHANGED because they ARE changed. Work out which package to suspect\n"
"   from where the boot stopped -- `man boot` maps stage to package --\n"
"   verify that one, and use `pkg diff <path>` before you touch anything.\n"
"   A diff that reads like a decision is not a fault.\n"
"\n"
"Q. pkg verify is clean but the machine will not boot.\n"
"A. Three possibilities, in order of likelihood: something not owned by a\n"
"   package (the boot sector), a namespace binding, or a file whose\n"
"   CONTENTS are legal but wrong -- a valid UUID that is not this disk's.\n"
"\n"
"Q. I reinstalled the package and it is still broken.\n"
"A. Reinstall puts back what shipped, and it now REFUSES to overwrite a\n"
"   config you have edited. If the fault is in one of those files it will\n"
"   say so and leave it alone; look at it with `pkg diff` and then either\n"
"   fix the line by hand or use --force. If you forced it and the machine\n"
"   came up, check the bench report -- it lists what you reverted.\n"
"\n"
"Q. The initrd is waiting for a uuid. Which one is wrong?\n"
"A. `blkid` tells you what the disk ACTUALLY carries. Two configs agreeing\n"
"   with each other and not with the disk is exactly the fault, so check\n"
"   both /boot/zbl/zbl.cfg and /etc/fstab against blkid, not against each\n"
"   other.\n"
"\n"
"Q. It boots, and something is still wrong.\n"
"A. `svc` shows services rather than processes: enabled and running,\n"
"   enabled and DEAD, or not meant to run at this runlevel at all. The boot\n"
"   console scrolls past a service that gave up.\n"
"\n"
"Q. Where did /boot/vmlinuz go?\n"
"A. Somebody ran a cleanup script. It happens more than anyone admits.\n"
},

{ "support.internal", "10.0.2.30", "/",
"NOMINAL support desk\n"
"====================\n"
"\n"
"Open tickets are dispatched to your bench automatically.\n"
"\n"
"House rules:\n"
"  1. Boot it before you touch it. The console is evidence.\n"
"  2. Find out what changed before you change anything.\n"
"  3. `pkg reinstall` is a hammer. Look first.\n"
"  4. If it boots, you are done. Do not tidy.\n"
"\n"
"see also: wiki.hamnix.org\n"
},

{ "bofh.hamnix.org", "10.0.2.44", "/",
"THE BASTARD OPERATOR FROM HELL\n"
"==============================\n"
"\n"
"Excuse of the day:\n"
"\n"
"  \"It's not a bug, it's an undocumented feature of the initrd.\"\n"
"\n"
"Previously:\n"
"  - cosmic rays flipped a bit in your symlink\n"
"  - the package manager is sulking\n"
"  - somebody chmod'd it for security reasons\n"
"  - it worked on the test machine\n"
"  - that file was never load-bearing until it was\n"
},

{ "nominal.local", "127.0.0.1", "/",
"this machine\n"
"============\n"
"\n"
"If you are reading this, the loopback address resolves and the browser\n"
"works. That is not nothing -- it means /etc/hosts is intact enough to\n"
"find at least one name.\n"
},
};
#define NPAGES ((int)(sizeof PAGES / sizeof PAGES[0]))

/* Resolve a hostname the way a nameserver would. Returns NULL if the name is
 * not known -- which is a real answer, not an error. */
const char *net_dns(const char *host)
{
    for (int i = 0; i < NPAGES; i++)
        if (strcmp(PAGES[i].host, host) == 0) return PAGES[i].ip;
    return NULL;
}

/* Fetch by ADDRESS, not by name: the browser has already resolved. That split
 * is what makes "it works by IP but not by name" possible, and it is one of
 * the most common real diagnoses there is. */
bool net_fetch(const char *ip, const char *path, Buf *out)
{
    if (!path || !*path) path = "/";
    for (int i = 0; i < NPAGES; i++) {
        if (strcmp(PAGES[i].ip, ip) != 0) continue;
        if (strcmp(PAGES[i].path, path) != 0) continue;
        buf_puts(out, PAGES[i].body);
        return true;
    }
    /* the host is there but the page is not */
    for (int i = 0; i < NPAGES; i++) {
        if (strcmp(PAGES[i].ip, ip) != 0) continue;
        buf_printf(out, "404 no such page: %s\n\nthis host serves:\n", path);
        for (int j = 0; j < NPAGES; j++)
            if (strcmp(PAGES[j].ip, ip) == 0)
                buf_printf(out, "  %s%s\n", PAGES[j].host, PAGES[j].path);
        return true;
    }
    return false;
}
