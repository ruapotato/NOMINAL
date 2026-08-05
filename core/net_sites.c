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

{ "wiki.nomnix.org", "10.0.2.20", "/",
"NomnixOS wiki\n"
"===========\n"
"\n"
"  /boot          how this system boots, stage by stage\n"
"  /packages      the package database, verify and reinstall\n"
"  /rescue        booting the live medium and repairing a disk\n"
"  /namespaces    bind, and why nothing being corrupt is still a fault\n"
"  /services      running, dead, and running-but-wrong\n"
"  /disk          space, uuids and fsck\n"
"  /libraries     what a binary needs and where it looks\n"
"  /logs          reading a boot that already failed\n"
"  /network       resolving names, and what is listening\n"
"  /faq           things people ask twice\n"
"\n"
"  links wiki.nomnix.org/boot\n"
},

{ "wiki.nomnix.org", "10.0.2.20", "/boot",
"How NomnixOS boots\n"
"================\n"
"\n"
"Seven stages. Each reads real files, and each fails differently, so the\n"
"stage a machine dies at tells you where to look.\n"
"\n"
"  firmware    finds a boot sector on /dev/sda\n"
"  zbl         reads /boot/zbl/zbl.cfg: kernel, initrd, root UUID\n"
"  kernel      loads /boot/vmnomuz -- a SYMLINK to the versioned image\n"
"  initrd      loads /boot/initrd, needs virtio_blk and ext4 modules,\n"
"              then finds the root filesystem by UUID\n"
"  init        /sbin/init -> /usr/lib/sysinit/init, reads /etc/inittab\n"
"  rc          /bin/rc runs /etc/rc.boot, then /etc/rc.d/rc.3\n"
"  services    /sbin/svcinit reads /etc/services.d/*.svc\n"
"\n"
"The commonest thing people miss: /boot/vmnomuz and /boot/initrd are\n"
"symlinks. If the file they point at is gone, the loader reports the LINK\n"
"and the target, and `ls` shows the link looking perfectly healthy.\n"
"`stat /boot/vmnomuz` will tell you the truth.\n"
},

{ "wiki.nomnix.org", "10.0.2.20", "/packages",
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

{ "wiki.nomnix.org", "10.0.2.20", "/rescue",
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

{ "wiki.nomnix.org", "10.0.2.20", "/namespaces",
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

{ "wiki.nomnix.org", "10.0.2.20", "/services",
"Services\n"
"========\n"
"\n"
"A service is a real process. It can be running, dead, or running and\n"
"WRONG, and those are three different tickets.\n"
"\n"
"  svc                 every unit: running, DEAD, disabled, or not at this\n"
"                      runlevel. Start here on a machine that boots and is\n"
"                      still wrong.\n"
"  ps                  processes, with how much cpu each has used\n"
"  kill -HUP <pid>     tell a daemon to re-read its configuration\n"
"  cat /run/NAME.state what that daemon ACTUALLY loaded: the file it read\n"
"                      and the setting it took from it\n"
"\n"
"THE ONE PEOPLE MISS. A daemon reads its config once, at startup. Edit the\n"
"file afterwards and the process keeps the old one -- so the file on disk\n"
"is a description of what the machine is SUPPOSED to do and /run/*.state\n"
"is what it is actually doing. When those disagree, nothing is corrupt and\n"
"the fix is a signal, not a file.\n"
"\n"
"A unit that dies is restarted if it says `restart: on-failure`. Five\n"
"failures in a row and the system stops trying and says so -- but that\n"
"line scrolled past during boot, and `svc` is where you find it afterwards.\n"
},

{ "wiki.nomnix.org", "10.0.2.20", "/disk",
"Disk\n"
"====\n"
"\n"
"  df                  space, and what is mounted\n"
"  blkid               the uuid the disk ACTUALLY carries\n"
"  fsck /dev/sda1      check and repair after an unclean shutdown\n"
"\n"
"RUN df FIRST. A full disk is not a corruption: every file is exactly what\n"
"it should be, every hash matches, `pkg verify` reports a perfect machine,\n"
"and there is simply nowhere to put the next byte. The first thing that\n"
"fails is whatever writes first, which is almost never the thing that\n"
"filled it -- look for a log that has been growing for months.\n"
"\n"
"RUN blkid BEFORE YOU BELIEVE A CONFIG. /boot/zbl/zbl.cfg and /etc/fstab\n"
"can agree with each other perfectly and both be wrong about which disk\n"
"this is. Two configs agreeing is not evidence.\n"
"\n"
"A filesystem marked dirty will not mount -- not by the initrd and not by\n"
"you. fsck first. It rebuilds the metadata and tells you plainly that it\n"
"cannot repair the CONTENTS of whatever was being written, which is your\n"
"second repair and usually a `pkg verify` away.\n"
},

{ "wiki.nomnix.org", "10.0.2.20", "/faq",
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
"Q. Where did /boot/vmnomuz go?\n"
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
"see also: wiki.nomnix.org\n"
},

{ "bofh.nomnix.org", "10.0.2.44", "/",
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

/* ------------------------------------------------------------------ *
 * The rest of the wiki.
 * ------------------------------------------------------------------ */

{ "wiki.nomnix.org", "10.0.2.20", "/libraries",
"Libraries\n"
"=========\n"
"\n"
"A binary declares the libraries it needs, with versions. The loader\n"
"resolves them through /etc/ld.so.conf, in the order the file lists.\n"
"\n"
"  ldd <program>         what it needs, where each was found, and whether\n"
"                        the one that was found is new enough\n"
"  ldd -r /mnt <program> resolved against a root filesystem at /mnt\n"
"\n"
"A NEWER library satisfies an older requirement. An OLDER one does not.\n"
"So the interesting library fault is a downgrade, not an upgrade -- which\n"
"is why the repository channel in /etc/pkg/repos.d is worth a look before\n"
"you blame the file.\n"
"\n"
"NOT EVERY PROGRAM NEEDS THE SAME LIBRARIES. If everything is dead, suspect\n"
"libc. If two unrelated services are dead and the other five are fine, run\n"
"ldd on one of each and ask what the dead pair have in common. A library\n"
"that is installed but sits in a directory ld.so.conf does not list reads\n"
"as `not found`, which is the fault stated in plain words.\n"
"\n"
"When the disk's own libc is too broken to run anything, you cannot chroot\n"
"into it -- chroot will refuse. `pkg --root /mnt` and `ldd -r /mnt` are the\n"
"pair that still work, because neither executes anything off that disk.\n"
},

{ "wiki.nomnix.org", "10.0.2.20", "/logs",
"Logs\n"
"====\n"
"\n"
"  dmesg               what THIS boot said\n"
"  dmesg -1            the previous boot\n"
"  dmesg -f <text>     only the lines containing <text>\n"
"  dmesg -r /mnt       the customer's log, read from the rescue medium\n"
"\n"
"The boot log is written as the machine boots, so it survives a boot that\n"
"FAILED, which is the only kind anybody wants to read. The previous one is\n"
"kept as /var/log/boot.log.1 and dmesg -1 is the polite way to it.\n"
"\n"
"/var/log/messages is written by a real syslogd while the machine runs, so\n"
"`grep something /var/log/messages` is a diagnostic and not decoration.\n"
"\n"
"A MACHINE THAT CANNOT WRITE ITS LOG IS ITSELF A FINDING. There are only\n"
"three reasons: the root is mounted read-only (look at the options in\n"
"/etc/fstab), /var/log is not there (a directory can be owned by a package\n"
"and can be missing, and `pkg verify` will say so), or the disk is full and\n"
"there is nowhere to put the line. `df` answers the third one in a second.\n"
},

{ "wiki.nomnix.org", "10.0.2.20", "/network",
"Network\n"
"=======\n"
"\n"
"Two separate things have to work and they fail separately.\n"
"\n"
"  NAMES     /etc/hosts is consulted first, then the nameserver named in\n"
"            /etc/resolv.conf. Break either and some names still resolve.\n"
"  LISTENING netstat prints what is actually listening, read out of /proc,\n"
"            so a daemon that died shows nothing at all and a daemon whose\n"
"            config was edited shows the port the config NOW says.\n"
"\n"
"`links 10.0.2.20/boot` reaches the wiki by address with no resolver in the\n"
"way. If that works and `links wiki.nomnix.org/boot` does not, the fault is\n"
"in resolution and nowhere else -- read /etc/hosts, check its MODE with\n"
"stat, and read /etc/resolv.conf for a nameserver line.\n"
"\n"
"netstat showing a port nobody expected, or not showing one everybody does,\n"
"is worth more than any status page. Compare it with `svc` and with\n"
"/run/<name>.state before you believe either.\n"
},

/* ------------------------------------------------------------------ *
 * The company. People use these, which is why they are a bit sad.
 * ------------------------------------------------------------------ */

{ "intranet.internal", "10.0.2.5", "/",
"CORVID LOGISTICS -- staff intranet\n"
"==================================\n"
"\n"
"  helpdesk.internal        the ticket queue\n"
"  status.internal          service status board\n"
"  notices.internal         all-staff notices\n"
"  cafeteria.internal       this week's menu\n"
"  home.internal            staff pages\n"
"  blog.internal            sysadmin notes\n"
"  oldwiki.internal         Project HALYARD (archived)\n"
"  wiki.nomnix.org          NomnixOS documentation\n"
"  support.internal         the bench you are sitting at\n"
"  bofh.nomnix.org          do not forward this one to customers\n"
"\n"
"A NEW INTRANET IS COMING. Preview it at intranet.internal/new.\n"
"\n"
"Page owner: nomowner. Page owner has left the company. If you need this\n"
"page changed, raise a ticket, and see helpdesk.internal/closed for how\n"
"that has gone for everyone else.\n"
},

{ "intranet.internal", "10.0.2.5", "/new",
"[ INTRANET 2.0 -- PREVIEW -- DO NOT LINK EXTERNALLY ]\n"
"\n"
"                    < hero image goes here >\n"
"\n"
"  Corvid Logistics is a leading provider of PLACEHOLDER, delivering\n"
"  PLACEHOLDER to PLACEHOLDER since PLACEHOLDER.\n"
"\n"
"  Our Values\n"
"    - Value one\n"
"    - Value two\n"
"    - (three more, ask Marketing)\n"
"\n"
"TODO nav bar\n"
"TODO search\n"
"TODO everything under Operations\n"
"TODO decide whether the old intranet is switched off or just unlinked\n"
"TODO ask nomowner where this is even served from\n"
"\n"
"last modified: 14 months ago\n"
"go live: two weeks\n"
},

{ "helpdesk.internal", "10.0.2.31", "/",
"HELPDESK -- open queue\n"
"======================\n"
"\n"
"  4471  warehouse    printer again. see notices.internal/printer   3d\n"
"  4470  accounts     Excel is slow. (Excel is not installed here.)  3d\n"
"  4468  night shift  node-4823 boots to a login prompt and then\n"
"                     nothing works. svc says httpd DEAD. we did not\n"
"                     touch anything.                                4d\n"
"  4465  facilities   can the server room be used for storage        6d\n"
"  4462  goods-in     the handheld says the date is 1970. ntpd?      8d\n"
"  4459  nobody       ticket opened by the monitoring agent. It has\n"
"                     opened this ticket every night for five weeks.\n"
"                     Nobody knows what installed the agent.        35d\n"
"  4451  reception    the machine at the front desk makes a noise\n"
"                     like a coin in a tumble dryer. Still works.   41d\n"
"  4402  ops          UPS battery light. Ignoring. -- reassigned to\n"
"                     nomowner. nomowner has left the company.      93d\n"
"  4388  unknown      \"who is authorised to power off the racks\"\n"
"                     No reply. Question stands.                   118d\n"
"\n"
"see also: helpdesk.internal/closed\n"
},

{ "helpdesk.internal", "10.0.2.31", "/closed",
"HELPDESK -- recently closed\n"
"===========================\n"
"\n"
"These are kept because the resolution field is the only training material\n"
"anybody here has ever actually read.\n"
"\n"
"4443  \"kernel is missing, ls says it is right there\"\n"
"      RESOLVED. /boot/vmnomuz is a symlink to a versioned image and the\n"
"      image had been deleted. ls shows the link and it looks healthy;\n"
"      stat follows it and says so. Reinstalled the kernel package.\n"
"\n"
"4437  \"reinstalled the package, still broken, please advise\"\n"
"      RESOLVED. The repository channel in /etc/pkg/repos.d was pointed at\n"
"      testing, so reinstall fetched the same wrong version back and\n"
"      reported success. Fix the channel FIRST, then reinstall.\n"
"\n"
"4431  \"disk not full but nothing can be created\"\n"
"      RESOLVED. Out of inodes, not bytes. df said 60% used and df -i said\n"
"      100%. Something wrote one small file per run since March.\n"
"\n"
"4429  \"machine is up, config is right, machine ignores config\"\n"
"      RESOLVED. The daemon read the file at startup and nobody reloaded\n"
"      it. /run/<name>.state says what it actually loaded. kill -HUP the\n"
"      pid. No file on the disk was wrong. Four hours.\n"
"\n"
"4420  \"everything is dead after an upgrade\"\n"
"      RESOLVED. Booted the rescue medium, mounted /dev/sda1 on /mnt, and\n"
"      used pkg --root /mnt because chroot refuses when the disk's own\n"
"      shell will not run.\n"
"\n"
"4415  \"boots fine, nobody can log in\"\n"
"      RESOLVED. The login shell in /etc/passwd named a program that is\n"
"      not on the disk. getty checks that before it offers a prompt, and\n"
"      it says which account and which shell.\n"
"\n"
"4409  \"pkg verify is completely clean and it still will not boot\"\n"
"      RESOLVED. A .svc file in /etc/services.d that no package owns,\n"
"      marked critical. pkg owns said nothing owns it. Deleted it.\n"
"      Third time this year. Same vendor.\n"
"\n"
"4396  \"can we have the server room for storage\"\n"
"      CLOSED -- WILL NOT FIX.\n"
},

{ "status.internal", "10.0.2.61", "/",
"===============================================================\n"
"                    S E R V I C E   S T A T U S\n"
"===============================================================\n"
"\n"
"  Core network ............................ OPERATIONAL\n"
"  Storage ................................. OPERATIONAL\n"
"  Web ..................................... OPERATIONAL\n"
"  Mail .................................... OPERATIONAL\n"
"  Time ..................................... OPERATIONAL\n"
"  Print ................................... OPERATIONAL\n"
"  Monitoring .............................. OPERATIONAL\n"
"  Backups ................................. OPERATIONAL\n"
"\n"
"                 ALL SYSTEMS OPERATIONAL\n"
"          No incidents reported. Have a great day!\n"
"\n"
"---------------------------------------------------------------\n"
"This board is maintained by hand from a spreadsheet. It has said\n"
"ALL SYSTEMS OPERATIONAL every day since it was created, including\n"
"the two days the building had no power.\n"
"\n"
"What the machine itself will tell you, and this page will not:\n"
"  svc                 which units are running, DEAD, or disabled\n"
"  svc status <name>   why that one is unhappy, and how many times\n"
"  netstat             what is actually listening, from /proc\n"
"  ps                  what is actually running\n"
"\n"
"see also: status.internal/history\n"
},

{ "status.internal", "10.0.2.61", "/history",
"UPTIME REPORT -- rolling twelve months\n"
"======================================\n"
"\n"
"  Jan  100.00%    Jul  100.00%\n"
"  Feb  100.00%    Aug  100.00%\n"
"  Mar  100.00%    Sep  100.00%\n"
"  Apr  100.00%    Oct  100.00%\n"
"  May  100.00%    Nov  100.00%\n"
"  Jun  100.00%    Dec  100.00%\n"
"\n"
"  Twelve month availability: 100.00%\n"
"\n"
"Methodology: an outage is counted when a member of the Availability\n"
"Working Group agrees in writing that an outage occurred. The group has\n"
"not met since the year before last.\n"
"\n"
"Known limitations of this report:\n"
"  - March is the month /var/log/messages filled the disk.\n"
"  - Nothing here is measured. It is typed.\n"
},

{ "notices.internal", "10.0.2.62", "/",
"ALL-STAFF NOTICES\n"
"=================\n"
"\n"
"  /printer      THE PRINTER (updated 11 times)\n"
"\n"
"  * Fire drill Thursday. If you hear the alarm on Wednesday that is a\n"
"    different problem and you should still leave the building.\n"
"\n"
"  * The fridge in the second floor kitchen will be emptied on Friday.\n"
"    Everything in it will be emptied. Everything.\n"
"\n"
"  * Please do not power off equipment in the room marked SERVER ROOM,\n"
"    including the equipment that appears to be doing nothing. Especially\n"
"    that equipment. See ticket 4402.\n"
"\n"
"  * IT would like to remind everyone that IT is one person and that\n"
"    person left in March.\n"
"\n"
"  * Lost: one lanyard, one dignity. Reception.\n"
},

{ "notices.internal", "10.0.2.62", "/printer",
"NOTICE: THE PRINTER\n"
"===================\n"
"\n"
"v1   The printer is out of paper.\n"
"v2   The printer is not out of paper. Please stop adding paper.\n"
"v3   The printer has been reset. Please do not reset the printer.\n"
"v4   Whoever reset the printer: the queue was thirty jobs long and it is\n"
"     now zero jobs long. If your document is missing, it is not missing,\n"
"     it is gone.\n"
"v5   The printer is fine. The printer has always been fine.\n"
"v6   Please do not print the all-staff email to check whether the printer\n"
"     is working. That is what has been happening.\n"
"v7   Do not print this notice.\n"
"v8   Somebody printed this notice 400 times.\n"
"v9   The printer is out of paper.\n"
"v10  The print server is a machine like any other. If it is not answering,\n"
"     `netstat` on it will tell you whether anything is listening at all,\n"
"     and `svc` will tell you whether the daemon is running. Both are more\n"
"     use than hitting it.\n"
"v11  Somebody hit it. It works now. Nobody is happy about this.\n"
},

{ "cafeteria.internal", "10.0.2.66", "/",
"CAFETERIA -- this week\n"
"======================\n"
"\n"
"  MON   Soup of the day (tomato)          Pie, chips, beans\n"
"  TUE   Soup of the day (tomato)          Curry, rice\n"
"  WED   Soup of the day (tomato)          Pie, chips, beans\n"
"  THU   Soup of the day (tomato)          Curry, rice\n"
"  FRI   Soup of the day (tomato)          FISH\n"
"\n"
"  Vegetarian option: beans.\n"
"  Vegan option: ask, and then have the beans.\n"
"\n"
"Hot food 11:45 - 13:30. The till has been cash-only since the card reader\n"
"went, which was a Tuesday, which was a curry day, which is how we all\n"
"remember it.\n"
"\n"
"This menu has been the same for nineteen weeks. The page is generated\n"
"nightly by a script that copies last week's menu, and nobody has ever\n"
"replaced the seed file. It is, in fairness, running perfectly.\n"
},

{ "home.internal", "10.0.2.70", "/",
"STAFF PAGES\n"
"===========\n"
"\n"
"  /~rkeeler     R. Keeler -- Facilities\n"
"  /~nomowner    (redirects to blog.internal)\n"
"\n"
"Staff pages were discontinued. These two survive because the process for\n"
"removing them requires a form that is hosted on the new intranet.\n"
},

{ "home.internal", "10.0.2.70", "/~rkeeler",
"                 Welcome to Rob's Homepage!\n"
"                 ==========================\n"
"\n"
"                       [ under construction ]\n"
"\n"
"Hi! I'm Rob and I work in Facilities. This is my page on the work intranet\n"
"which they let us have now, which I think is pretty cool.\n"
"\n"
"ABOUT ME\n"
"  - Been here since 2004\n"
"  - Coffee (2 sugars)\n"
"  - My son's football team won the league!!\n"
"\n"
"LINKS\n"
"  - The wiki\n"
"  - That page with the excuses on it, very funny\n"
"  - < broken image: webring.gif >\n"
"\n"
"UPDATES\n"
"  14/02/2011 - Added photos of the new loading bay\n"
"  09/03/2011 - Photos coming soon, still waiting on the camera\n"
"  22/06/2011 - Photos coming soon\n"
"\n"
"Last updated: 22 June 2011\n"
"Visitor number: 00000417\n"
"\n"
"This page is best viewed at 800x600.\n"
},

{ "home.internal", "10.0.2.70", "/~nomowner",
"nomowner\n"
"========\n"
"\n"
"Systems. Left in March.\n"
"\n"
"This page used to redirect to blog.internal, back when the intranet could\n"
"do redirects. It cannot any more and nobody knows when that stopped.\n"
"\n"
"  blog.internal          the notes, which are the useful part\n"
"  bofh.nomnix.org        the other notes\n"
"\n"
"On any machine here, /home/nomowner/notes.txt is the same advice in ten\n"
"numbered lines, and it is on the disk whether the network works or not.\n"
},

{ "oldwiki.internal", "10.0.2.71", "/",
"PROJECT HALYARD -- project wiki\n"
"===============================\n"
"\n"
"  *** ARCHIVED. This wiki is read-only. Do not raise tickets against\n"
"      HALYARD. There is nobody to assign them to. ***\n"
"\n"
"  /charter      what HALYARD was going to be\n"
"  /runbook      the runbook (incomplete)\n"
"  /meeting      minutes, 14 of them, the last one is one line\n"
"\n"
"HALYARD was the replacement for the thing HALYARD was written to replace,\n"
"which is still running, and which everything still depends on.\n"
},

{ "oldwiki.internal", "10.0.2.71", "/charter",
"PROJECT HALYARD -- charter\n"
"==========================\n"
"\n"
"PURPOSE. To replace the current arrangement with a modern, unified,\n"
"scalable platform.\n"
"\n"
"SCOPE. All of it.\n"
"\n"
"OUT OF SCOPE. Migrating anything currently running. Documenting anything\n"
"currently running. Speaking to the person who runs it.\n"
"\n"
"SUCCESS CRITERIA. To be agreed at the next steering meeting.\n"
"\n"
"RISKS.\n"
"  - Key person dependency (nomowner). MITIGATION: none identified.\n"
"  - The current arrangement continues to work, reducing urgency.\n"
"    This risk materialised.\n"
"\n"
"STATUS: parked. See /meeting.\n"
},

{ "oldwiki.internal", "10.0.2.71", "/runbook",
"HALYARD RUNBOOK (DRAFT -- DO NOT FOLLOW)\n"
"========================================\n"
"\n"
"1. Confirm the node is actually down. `svc` on the node; if you cannot\n"
"   get to a prompt at all, the console is the evidence, so power cycle\n"
"   it and READ what it says on the way up rather than looking away.\n"
"\n"
"2. TODO: someone who knows what step 2 is\n"
"\n"
"3. If the node boots and HALYARD is not running, `svc status halyard`.\n"
"   (Note from review: there is no halyard unit. There was never a\n"
"   halyard unit. This document describes a machine that was not built.)\n"
"\n"
"4. Escalate to the HALYARD on-call rota.\n"
"   (Note from review: there is no rota.)\n"
"\n"
"5. TODO\n"
"\n"
"REVIEWER'S SUMMARY: the only true sentence in this document is step 1,\n"
"and step 1 is true of every machine we own, which rather suggests where\n"
"the effort should have gone.\n"
},

{ "oldwiki.internal", "10.0.2.71", "/meeting",
"HALYARD -- minutes\n"
"==================\n"
"\n"
"#1   Kickoff. Everyone very positive. Scope: everything.\n"
"#4   Scope reduced to everything except the hard part.\n"
"#7   Discussion of what the hard part is. No conclusion.\n"
"#9   Agreed to write a runbook before writing the system, so that the\n"
"     system can be written to match the runbook.\n"
"#11  The runbook describes a system nobody has agreed to build.\n"
"#12  Two attendees. Neither is on the project.\n"
"#13  Rescheduled.\n"
"#14  \"Parked.\"\n"
},

/* ------------------------------------------------------------------ *
 * The previous administrator. Everything on this host is true, which is
 * the point of it: the flavour and the hints are the same sentences.
 * ------------------------------------------------------------------ */

{ "blog.internal", "10.0.2.72", "/",
"nomowner's notes\n"
"================\n"
"\n"
"Things I got wrong, written down so I get them wrong faster next time.\n"
"\n"
"  /uuid          the disk does not care what your config believes\n"
"  /clean         the cleanup script, and what it took with it\n"
"  /stale         running and wrong is a third state\n"
"  /order         where it stopped tells you what to suspect\n"
"  /goodbye       last post\n"
"\n"
"If you are the person who came after me: sorry about /var/log. Read\n"
"/home/nomowner/notes.txt on the machine itself, it is the short version\n"
"of all of this and it is ten numbered lines.\n"
},

{ "blog.internal", "10.0.2.72", "/uuid",
"the disk does not care what your config believes\n"
"================================================\n"
"\n"
"Spent a whole evening on a machine that hung in the initrd waiting for a\n"
"root filesystem. Checked /boot/zbl/zbl.cfg. Checked /etc/fstab. They\n"
"agreed. I took that as confirmation and went looking somewhere else for\n"
"three hours.\n"
"\n"
"TWO CONFIGS AGREEING IS NOT EVIDENCE. They were both written by the same\n"
"person on the same afternoon, so of course they agree. The only thing in\n"
"the building that knows what UUID that disk carries is the disk:\n"
"\n"
"    blkid\n"
"\n"
"and it took four seconds. If the UUID in zbl.cfg is a well-formed UUID\n"
"that simply is not this disk's, nothing is corrupt, every hash matches,\n"
"and `pkg verify` will hand you a clean bill of health while the machine\n"
"sits there waiting for a disk that does not exist.\n"
"\n"
"Reinstalling the bootloader package does NOT fix this, because the\n"
"package ships the config for the machine it was BUILT for, not the one\n"
"in front of you. `zbl-mkconfig` regenerates it from this machine. That\n"
"is the difference between putting a file back and making it true.\n"
},

{ "blog.internal", "10.0.2.72", "/clean",
"the cleanup script\n"
"==================\n"
"\n"
"I wrote a script to tidy /boot. It removed the versioned kernel image\n"
"because the filename did not match the pattern it expected, and it\n"
"decided that meant stale. It did not remove /boot/vmnomuz, because\n"
"/boot/vmnomuz is a SYMLINK and the symlink was fine.\n"
"\n"
"So `ls /boot` looked perfect. The loader said the kernel was missing.\n"
"I believed ls for an hour.\n"
"\n"
"    ls    shows you the link\n"
"    stat  follows it and tells you the truth\n"
"\n"
"`pkg verify` reports this as MISSING, which is the honest answer, and\n"
"`pkg reinstall` on the kernel package puts the image back. The script is\n"
"still in my home directory and it is still disabled and it is staying\n"
"disabled.\n"
"\n"
"Same class of mistake, different day: a REPOINTED symlink. The link is\n"
"there, the target is there, the target is the wrong file. ls shows you\n"
"nothing wrong at all.\n"
},

{ "blog.internal", "10.0.2.72", "/stale",
"running and wrong\n"
"=================\n"
"\n"
"A service has three states, not two. Running, dead, and running with a\n"
"configuration that stopped being the configuration on disk some time ago.\n"
"The third one has taken more of my life than the other two combined.\n"
"\n"
"A daemon reads its config ONCE, at startup. Edit the file afterwards and\n"
"the process keeps what it already has. So the file is a description of\n"
"what the machine is supposed to do, and the machine is doing something\n"
"else, and nothing anywhere is corrupt.\n"
"\n"
"    cat /run/<name>.state\n"
"\n"
"is what that daemon ACTUALLY loaded -- the file it read and the setting\n"
"it took out of it. When that disagrees with the file, you have found it.\n"
"`netstat` catches the same thing from the other end: it lists the port\n"
"the running process really has, not the one the config now claims.\n"
"\n"
"The fix is a signal, not a file:\n"
"\n"
"    ps                 find the pid\n"
"    kill -HUP <pid>    make it re-read\n"
"\n"
"and the reason this is so miserable to catch is that rebooting makes it\n"
"vanish. The daemon comes up reading the new file and the evidence is\n"
"gone, along with any chance of explaining what happened.\n"
},

{ "blog.internal", "10.0.2.72", "/order",
"where it stopped\n"
"================\n"
"\n"
"I keep this on a card. The stage a machine dies at is half the diagnosis,\n"
"because it tells you which half of the system to stop looking at.\n"
"\n"
"  zbios        finds a boot sector on the disk. Not a file. No package\n"
"               owns it, so pkg verify is clean and always will be.\n"
"  zbl          reads /boot/zbl/zbl.cfg -- kernel, initrd, root UUID.\n"
"  kernel       loads /boot/vmnomuz (a symlink to the versioned image).\n"
"  initrd       loads /boot/initrd, needs the block and filesystem\n"
"               drivers, then finds the root BY UUID.\n"
"  init         /sbin/init, pid 1.\n"
"  rc           /bin/rc runs /etc/rc.boot.\n"
"  mounts       /sbin/mountall reads /etc/fstab and mounts what is in it,\n"
"               including deciding ro or rw for the root itself.\n"
"  runlevel     /etc/rc.d/rc.3.\n"
"  services     /sbin/svcinit reads /etc/services.d and starts the units\n"
"               in dependency order.\n"
"  login        /sbin/getty checks the account is in /etc/passwd and that\n"
"               its shell exists and can be executed, then offers a prompt.\n"
"\n"
"Stopped before init and it is the disk, the loader or the images. Stopped\n"
"after init and it is a config file somebody edited. Booted to a prompt\n"
"and still wrong and it is `svc`, every time.\n"
},

{ "blog.internal", "10.0.2.72", "/goodbye",
"last post\n"
"=========\n"
"\n"
"Leaving on Friday. Handover notes, since there is nobody to hand over to.\n"
"\n"
"1. The bench work is a loop and the loop is: read the console, find out\n"
"   what CHANGED, then change one thing. Not the other order.\n"
"\n"
"2. `pkg verify` is not a fault list. Every machine that a person has\n"
"   administered has files that differ from what shipped, because the\n"
"   person decided so on purpose. Use `pkg diff <path>` and read it. A\n"
"   diff that reads like a decision is not a fault.\n"
"\n"
"3. `pkg reinstall` will not overwrite config you have edited. It names\n"
"   the files and leaves them. --force overwrites and keeps a .pkgsave\n"
"   copy, and that is the only undo there is.\n"
"\n"
"4. Everything above only works if something can still run. When the libc\n"
"   is the casualty, nothing on that disk runs, chroot refuses, and the\n"
"   rescue medium is the whole answer:\n"
"\n"
"       rcon media insert\n"
"       rcon boot media\n"
"       rcon power cycle\n"
"       mount /dev/sda1 /mnt\n"
"       pkg --root /mnt verify\n"
"\n"
"5. `find /var -type f` and `df` between them have solved more of my\n"
"   tickets than any amount of cleverness. Something is always growing.\n"
"\n"
"6. The status board is not a source of truth. Nothing that a human types\n"
"   into a spreadsheet is a source of truth. The machine is.\n"
"\n"
"Good luck. The coffee is bad on purpose.\n"
},

/* ------------------------------------------------------------------ *
 * BOFH, extended. Jokes only -- nothing here is advice.
 * ------------------------------------------------------------------ */

{ "bofh.nomnix.org", "10.0.2.44", "/excuse",
"EXCUSE GENERATOR v0.3\n"
"=====================\n"
"\n"
"  [ generate ]   (button not implemented, here is the whole list)\n"
"\n"
"   1  cosmic rays flipped a bit in your symlink\n"
"   2  the package manager is sulking\n"
"   3  somebody chmod'd it for security reasons\n"
"   4  it worked on the test machine\n"
"   5  that file was never load-bearing until it was\n"
"   6  the UUID moved\n"
"   7  temporary read-only, from March\n"
"   8  the initrd is thinking about it\n"
"   9  the daemon is running, it is simply not listening to you\n"
"  10  it is not down, it is between runlevels\n"
"  11  the log would explain everything but the disk is full of the log\n"
"  12  a vendor agent did it and the vendor no longer exists\n"
"  13  we have always been at 100.00% uptime\n"
"  14  that is not a bug, that is a decision somebody made in 2011\n"
"  15  the fix is a signal, and I am not in the mood\n"
"\n"
"Excuse 11 is real and happens constantly. Excuse 9 is real too. The rest\n"
"of them are excuses, which is a category of statement that is true about\n"
"the speaker rather than the machine.\n"
},

{ "bofh.nomnix.org", "10.0.2.44", "/rules",
"HOUSE RULES, THE HONEST VERSION\n"
"===============================\n"
"\n"
"  1. The machine is not lying to you. It is describing what somebody did.\n"
"  2. Two files agreeing with each other is not evidence. Ask the device.\n"
"  3. If verify is clean and it is still broken, the fault is not a file:\n"
"     the boot sector, a directory's mode, a namespace bind, a full disk,\n"
"     no free inodes, or a process running on a config that no longer\n"
"     exists anywhere on the disk.\n"
"  4. Reinstalling everything that shows up in verify is not a diagnosis,\n"
"     it is a coin toss with a receipt.\n"
"  5. If it boots and it is healthy, stop. Do not tidy. Tidying is how the\n"
"     kernel image went missing in the first place.\n"
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
