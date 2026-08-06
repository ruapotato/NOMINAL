/* net_sites.c — the virtual network.
 *
 * A web with real pages on it. This is not decoration: the wiki is where the
 * boot process is actually documented, so a player who explores is rewarded
 * with knowledge they can use, and a player who does not has to work it out
 * from the machine itself. Both are legitimate.
 *
 * Reaching a site requires the machine's own networking to work:
 * /etc/hosts and /etc/resolv.conf are read by the browser and consulted by
 * the resolver. Damage either and the network breaks in the way networks
 * actually break -- "it works by IP but not by name" is a real diagnosis and
 * now a possible one.
 *
 * PAGES ARE MARKUP. The subset is deliberately tiny, because it is parsed
 * twice -- once by /usr/bin/links inside the machine and once by the desktop
 * browser -- and a subset that cannot be parsed honestly in both places would
 * become two subsets and then two webs.
 *
 *   <h1> <h2>                 headings
 *   <p>                       a paragraph, wrapped by whoever renders it
 *   <ul> <li>                 a bulleted list
 *   <pre>                     verbatim: commands, logs, ASCII art
 *   <hr>                      a rule
 *   <b> <i>                   emphasis
 *   <a href="host/path">      a link; "/path" means this host
 *   <img src=".." alt="..">   an image, drawn as a coloured box with its alt
 *   &lt; &gt; &amp; &quot;    the four entities that matter
 *
 * Inside <pre> a bare '<' is just a character, which is why every command
 * example lives in one. Outside it, write &lt;.
 *
 * TRUTH RULE. Most of this web is a joke, and the jokes are allowed to be
 * about anything except how this operating system works. Any page that gives
 * technical advice about THIS machine gives advice that is true of it, and
 * names only commands that exist. A wiki that invents a command teaches the
 * player to distrust the wiki, and then the whole web is scenery.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"
#include "site.h"

/* A PAGE THAT IS COMPUTED RATHER THAN TYPED.
 *
 * Every page below is a string literal, and that is right for a page that is
 * somebody's opinion. It is wrong for a page that is a fact the game already
 * holds somewhere else -- a price list is the catalogue in core/site.c, and
 * this project has shipped the same fact from two places five times in one
 * day (a footer dividing by the wrong port count, a boot menu counting
 * differently from its loader, prose quoting a constant it sat beside). A
 * supplier's price list typed in here is that bug with a delivery date on it:
 * it would be correct this afternoon and wrong the moment somebody adds a
 * product.
 *
 * So a page may have NO BODY. A NULL body means the page is COMPUTED, and
 * gen_page() below says by what. The Page struct is untouched, which is not
 * squeamishness: a fifth member would have to be initialised in all four
 * hundred entries or -Wextra would warn about every one of them, and four
 * hundred `, NULL`s to serve two pages is a worse trade than one branch in
 * net_fetch.
 *
 * The list of pages stays in one place either way -- a generated page is a
 * PAGES entry like any other, so it is in the zone, in the 404 index, and in
 * the enumeration `--mancheck` walks. A computed page is held to exactly the
 * same standard as a written one, which is the entire reason it is in the
 * same table. */
typedef struct {
    const char *host;
    const char *ip;
    const char *path;
    const char *body;      /* NULL: computed -- see gen_page() */
} Page;

static void shop_catalogue(Buf *out);
static void shop_discontinued(Buf *out);

/* Anything reachable. The addresses matter: they are what /etc/hosts maps to
 * and what the resolver returns, so a corrupted hosts file sends the browser
 * somewhere that is not there. */
static const Page PAGES[] = {

/* ------------------------------------------------------------------ *
 * The wiki. Every word of this is true of the machine.
 * ------------------------------------------------------------------ */

{ "wiki.nomnix.org", "10.0.2.20", "/",
"<h1>NomnixOS wiki</h1>"
"<ul>"
"<li><a href=\"/boot\">/boot</a> -- how this system boots, stage by stage</li>"
"<li><a href=\"/packages\">/packages</a> -- the package database, verify and reinstall</li>"
"<li><a href=\"/rescue\">/rescue</a> -- booting the live medium and repairing a disk</li>"
"<li><a href=\"/namespaces\">/namespaces</a> -- bind, and why nothing being corrupt is still a fault</li>"
"<li><a href=\"/services\">/services</a> -- running, dead, and running-but-wrong</li>"
"<li><a href=\"/disk\">/disk</a> -- space, uuids and fsck</li>"
"<li><a href=\"/libraries\">/libraries</a> -- what a binary needs and where it looks</li>"
"<li><a href=\"/logs\">/logs</a> -- reading a boot that already failed</li>"
"<li><a href=\"/network\">/network</a> -- resolving names, and what is listening</li>"
"<li><a href=\"/faq\">/faq</a> -- things people ask twice</li>"
"</ul>"
"<p>Everything here has been checked against a machine. Pages elsewhere on\n"
"this network have not.</p>"
"<p>The people who actually run these machines are on the mailing list:\n"
"<a href=\"lists.nomnix.org\">lists.nomnix.org</a>. The threads are long and\n"
"the answer is usually a long way down, which is what a mailing list is.</p>"
"<pre>links wiki.nomnix.org/boot</pre>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/boot",
"<h1>How NomnixOS boots</h1>"
"<p>Seven stages. Each reads real files, and each fails differently, so the\n"
"stage a machine dies at tells you where to look.</p>"
"<pre>"
"firmware    finds a boot sector on /dev/sda\n"
"zbl         reads /boot/zbl/zbl.cfg: kernel, initrd, root UUID\n"
"kernel      loads /boot/vmnomuz -- a SYMLINK to the versioned image\n"
"initrd      loads /boot/initrd, needs virtio_blk and ext4 modules,\n"
"            then finds the root filesystem by UUID\n"
"init        /sbin/init -> /usr/lib/sysinit/init, reads /etc/inittab\n"
"rc          /bin/rc runs /etc/rc.boot, then /etc/rc.d/rc.3\n"
"services    /sbin/svcinit reads /etc/services.d/*.svc"
"</pre>"
"<p>The commonest thing people miss: /boot/vmnomuz and /boot/initrd are\n"
"symlinks. If the file they point at is gone, the loader reports the LINK\n"
"and the target, and <b>ls</b> shows the link looking perfectly healthy.\n"
"<b>stat /boot/vmnomuz</b> will tell you the truth.</p>"
"<p>See also <a href=\"blog.internal/order\">blog.internal/order</a>, which is the\n"
"same list written by somebody who had been up all night.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/packages",
"<h1>Packages</h1>"
"<p>Every file that matters is owned by a package. The database lives on the\n"
"machine at /var/lib/pkg/&lt;name&gt;/files -- <i>mode hash path</i>, one per line.</p>"
"<pre>"
"pkg [--root DIR] ...    work on a filesystem mounted elsewhere, without\n"
"                        chrooting into it -- which you cannot do when the\n"
"                        disk's own libc is broken\n"
"pkg list                what is installed\n"
"pkg verify              hash everything, report what differs\n"
"pkg verify <name>       just one package\n"
"pkg owns <path>         what would I be reinstalling\n"
"pkg diff <path>         what this file says against what shipped\n"
"pkg reinstall <name>    fetch pristine copies and put them back\n"
"pkg reinstall --force   ...including config you edited"
"</pre>"
"<p>Files under /etc that have been EDITED are kept by a reinstall, and named,\n"
"because a package ships a default and an administrator makes a decision.\n"
"--force overwrites them and leaves a .pkgsave copy, which is the only undo\n"
"there is. Look with <b>pkg diff</b> first.</p>"
"<p>reinstall pulls from a repository that is NOT on the machine, which is why\n"
"it works on a disk with nothing good left on it.</p>"
"<h2>What a package says about itself</h2>"
"<p>Anything with something worth saying ships it at /usr/share/doc/&lt;name&gt;\n"
"on the machine: a README, a CHANGELOG whose top entry is the version pkg\n"
"reports, and a known-issues file listing the ways that package really goes\n"
"wrong, what each looks like in verify, and what repairs it. It is shipped BY\n"
"the package, so it is covered by verify and cannot quietly drift away from\n"
"the software the way this wiki can.</p>"
"<pre>"
"ls /usr/share/doc\n"
"cat /usr/share/doc/README\n"
"cat /usr/share/doc/libc/known-issues"
"</pre>"
"<h2>What verify reports</h2>"
"<ul>"
"<li>MISSING -- the file is gone</li>"
"<li>MISSING (symlink) -- a symlink is gone</li>"
"<li>REPOINTED -- a symlink points somewhere new</li>"
"<li>CHANGED -- contents differ from what was shipped</li>"
"<li>MODE -- contents are fine, permissions are not</li>"
"</ul>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/rescue",
"<h1>Rescue</h1>"
"<p>The live medium is /dev/sr0 and is never damaged. Boot it and the\n"
"customer's disk is /dev/sda1, not mounted.</p>"
"<pre>"
"mount /dev/sda1 /mnt\n"
"for i in dev sys proc; do mount /$i /mnt/$i; done\n"
"chroot /mnt\n"
"pkg verify"
"</pre>"
"<p>The bind mounts matter: after chroot, /proc and /dev have to be the ones\n"
"you already had, because the disk you are repairing cannot provide them.</p>"
"<p><b>mount</b> with no arguments prints the table. <b>chroot</b> with nothing\n"
"mounted at the target will refuse, which is usually the mistake.</p>"
"<p>The console itself is reached with <b>rcon</b>. Attach first --\n"
"<i>rcon connect ADDRESS</i> -- and nothing you type reaches the customer's\n"
"machine until you have.</p>"
"<pre>"
"rcon connect 10.0.2.NN   attach to the service processor\n"
"rcon media insert        the live medium into the virtual drive\n"
"rcon boot media          what it boots from NEXT time\n"
"rcon power cycle         and now it boots it"
"</pre>"
"<p><b>GETTING BACK OUT.</b> The same three, backwards. The medium boots for\n"
"as long as it is the boot device, so a repaired disk will not start until you\n"
"have told the service processor to boot it -- and that is a separate step from\n"
"emptying the drive.</p>"
"<pre>"
"rcon media eject\n"
"rcon boot disk\n"
"rcon power cycle         now it boots what you repaired"
"</pre>"
"<p><i>rcon status</i> prints power, media and boot device. It is the machine's\n"
"own answer, so when it disagrees with what you think you did, believe it.</p>"
"<p>Once attached, <b>rescue</b> and <b>boot</b> are shorthand for those two\n"
"sequences. They go through the same service processor and leave it in the same\n"
"state, so <i>rcon status</i> agrees with them.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/namespaces",
"<h1>Namespaces</h1>"
"<p>Every process has its own view of the filesystem, inherited from its\n"
"parent. <b>bind TARGET AT</b> makes lookups under AT resolve to TARGET.\n"
"Longest prefix wins.</p>"
"<pre>"
"bind /etc /mnt        now /mnt/passwd is /etc/passwd\n"
"ns                    print the current namespace\n"
"cat /proc/<pid>/ns    print another process's"
"</pre>"
"<p><b>WHY YOU CARE.</b> A bad bind is a fault where nothing is corrupt. Every\n"
"file passes <i>pkg verify</i> and the machine still reads the wrong one. If\n"
"verify is clean and the machine is still wrong, look at the namespace.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/services",
"<h1>Services</h1>"
"<p>A service is a real process. It can be running, dead, or running and\n"
"WRONG, and those are three different tickets.</p>"
"<pre>"
"svc                 every unit: running, DEAD, disabled, or not at this\n"
"                    runlevel. Start here on a machine that boots and is\n"
"                    still wrong.\n"
"svc status <name>   why that one is unhappy, and how many times\n"
"svc start|stop|restart|reload <name>\n"
"                    act on the process that is running NOW\n"
"svc enable|disable <name>\n"
"                    decide only what happens at the NEXT boot\n"
"ps                  processes, with how much cpu each has used\n"
"kill -HUP <pid>     tell a daemon to re-read its configuration; `svc\n"
"                    reload <name>` is the same signal by name\n"
"cat /run/NAME.state what that daemon ACTUALLY loaded"
"</pre>"
"<p><b>THE ONE PEOPLE MISS.</b> A daemon reads its config once, at startup. Edit\n"
"the file afterwards and the process keeps the old one -- so the file on disk\n"
"is a description of what the machine is SUPPOSED to do and /run/*.state is\n"
"what it is actually doing. When those disagree, nothing is corrupt and the\n"
"fix is a signal, not a file: <b>svc reload &lt;name&gt;</b>. Not a reboot --\n"
"a reboot also fixes it, and takes with it every trace of what was wrong.</p>"
"<p>A daemon that keeps nothing in memory has nothing to re-read, and says so\n"
"rather than pretending. For those, <b>svc restart &lt;name&gt;</b>: it reads\n"
"the unit file and the config again from disk, which is also what you want\n"
"when it is the UNIT that somebody edited.</p>"
"<p>A unit that dies is restarted if it says <i>restart: on-failure</i>. Five\n"
"failures in a row and the system stops trying and says so -- but that line\n"
"scrolled past during boot, and <b>svc</b> is where you find it afterwards.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/disk",
"<h1>Disk</h1>"
"<pre>"
"df                  space, and what is mounted\n"
"df -i               INODES, which run out separately\n"
"blkid               the uuid the disk ACTUALLY carries\n"
"fsck /dev/sda1      check and repair after an unclean shutdown"
"</pre>"
"<p><b>RUN df FIRST.</b> A full disk is not a corruption: every file is exactly\n"
"what it should be, every hash matches, <i>pkg verify</i> reports a perfect\n"
"machine, and there is simply nowhere to put the next byte. The first thing\n"
"that fails is whatever writes first, which is almost never the thing that\n"
"filled it -- look for a log that has been growing for months.</p>"
"<p><b>RUN blkid BEFORE YOU BELIEVE A CONFIG.</b> /boot/zbl/zbl.cfg and\n"
"/etc/fstab can agree with each other perfectly and both be wrong about which\n"
"disk this is. Two configs agreeing is not evidence.</p>"
"<p>A filesystem marked dirty will not mount -- not by the initrd and not by\n"
"you. fsck first. It rebuilds the metadata and tells you plainly that it\n"
"cannot repair the CONTENTS of whatever was being written, which is your\n"
"second repair and usually a <i>pkg verify</i> away.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/libraries",
"<h1>Libraries</h1>"
"<p>A binary declares the libraries it needs, with versions. The loader\n"
"resolves them through /etc/ld.so.conf, in the order the file lists.</p>"
"<pre>"
"ldd <program>         what it needs, where each was found, and whether\n"
"                      the one that was found is new enough\n"
"ldd -r /mnt <program> resolved against a root filesystem at /mnt"
"</pre>"
"<p>A NEWER library satisfies an older requirement. An OLDER one does not. So\n"
"the interesting library fault is a downgrade, not an upgrade -- which is why\n"
"the repository channel in /etc/pkg/repos.d is worth a look before you blame\n"
"the file.</p>"
"<p><b>NOT EVERY PROGRAM NEEDS THE SAME LIBRARIES.</b> If everything is dead,\n"
"suspect libc. If two unrelated services are dead and the other five are\n"
"fine, run ldd on one of each and ask what the dead pair have in common. A\n"
"library that is installed but sits in a directory ld.so.conf does not list\n"
"reads as <i>not found</i>, which is the fault stated in plain words.</p>"
"<p>When the disk's own libc is too broken to run anything, you cannot chroot\n"
"into it -- chroot will refuse. <b>pkg --root /mnt</b> and <b>ldd -r /mnt</b>\n"
"are the pair that still work, because neither executes anything off that\n"
"disk.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/logs",
"<h1>Logs</h1>"
"<pre>"
"dmesg               what THIS boot said\n"
"dmesg -1            the previous boot\n"
"dmesg -f <text>     only the lines containing <text>\n"
"dmesg -r /mnt       the customer's log, read from the rescue medium"
"</pre>"
"<p>The boot log is written as the machine boots, so it survives a boot that\n"
"FAILED, which is the only kind anybody wants to read. The previous one is\n"
"kept as /var/log/boot.log.1 and <b>dmesg -1</b> is the polite way to it.</p>"
"<p>/var/log/messages is written by a real syslogd while the machine runs, so\n"
"<i>grep something /var/log/messages</i> is a diagnostic and not decoration.</p>"
"<p><b>A MACHINE THAT CANNOT WRITE ITS LOG IS ITSELF A FINDING.</b> There are\n"
"only three reasons: the root is mounted read-only (look at the options in\n"
"/etc/fstab), /var/log is not there (a directory can be owned by a package\n"
"and can be missing, and <i>pkg verify</i> will say so), or the disk is full\n"
"and there is nowhere to put the line. <b>df</b> answers the third one in a\n"
"second.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/network",
"<h1>Network</h1>"
"<p>Two separate things have to work and they fail separately.</p>"
"<ul>"
"<li>NAMES -- /etc/hosts is consulted first, then the nameserver named in\n"
"/etc/resolv.conf. Break either and some names still resolve.</li>"
"<li>LISTENING -- netstat prints the sockets the kernel really has open, so a\n"
"daemon that died shows nothing at all, and a daemon that is running with a\n"
"configuration somebody edited and never reloaded shows the port it ACTUALLY\n"
"LOADED, not the one the file now says. When netstat and the config file\n"
"disagree, that gap IS the fault, and the fix is a reload rather than an\n"
"edit.</li>"
"</ul>"
"<p><b>links 10.0.2.20/boot</b> reaches this wiki by address with no resolver\n"
"in the way. If that works and <i>links wiki.nomnix.org/boot</i> does not, the\n"
"fault is in resolution and nowhere else -- read /etc/hosts, check its MODE\n"
"with stat, and read /etc/resolv.conf for a nameserver line. A nameserver\n"
"line pointing at an address with nothing on it does not say so: the query\n"
"goes out, nothing comes back, and you wait. That pause is the symptom.</p>"
"<h2>Looking below the names</h2>"
"<p>netstat takes one flag at a time, and each shows a different layer:</p>"
"<pre>netstat        sockets: listening, connected, and in what TCP state\n"
"netstat -i     the interface: address, mask, carrier, packet counts\n"
"netstat -r     the routing table, connected routes included\n"
"netstat -A     the arp cache -- who on this wire has answered\n"
"netstat -P     the physical port: link, speed, duplex, errors\n"
"netstat -W     start capturing packets\n"
"netstat -w     print what has been captured</pre>"
"<p>Work down them. <i>-i</i> with no address means nothing configured it --\n"
"look at the <b>net</b> service, because netd is what applies\n"
"/etc/net/interfaces and a netd that refused to start leaves the interface\n"
"exactly as blank as a missing config would. <i>-P</i> saying no link is a\n"
"cable, not a setting. <i>-A</i> empty after you have tried to reach\n"
"something on your own subnet means nothing answered, which is a machine that\n"
"is not there or an address that is not on this wire.</p>"
"<p><i>-W</i> then <i>-w</i> is the last resort and the most honest one: it\n"
"prints the frames. An arp who-has with no reply, a tcp [S] with no [S.] back,\n"
"an icmp unreachable from a router you did not expect to hear from -- each of\n"
"those names the layer that is broken without anybody having to guess.</p>"
"<p>netstat showing a port nobody expected, or not showing one everybody does,\n"
"is worth more than any status page. Compare it with <b>svc</b> and with\n"
"/run/&lt;name&gt;.state before you believe either.</p>"
"<h2>The rest of the toolbox</h2>"
"<p>nomsh ships the names people actually type, and all of them read the\n"
"same running stack netstat does -- not the files that configure it.</p>"
"<pre>ip addr | link | route | neigh   the shapes iproute2 prints\n"
"arp                              the cache, and the card each entry\n"
"                                 was learned on. arp -d forgets one\n"
"ping &lt;host&gt;                      the one that tries, and times it\n"
"traceroute &lt;host&gt;                how far it got, counted by ttl\n"
"ss -ltn                          the sockets, in ss columns\n"
"tcpdump                          the frames at this card</pre>"
"<p><b>tcpdump</b> is the one worth learning here. Turn it on, make the\n"
"traffic, read it back:</p>"
"<pre>tcpdump --capture on\n"
"ping 10.0.2.2\n"
"tcpdump icmp</pre>"
"<p>It is not live -- nothing runs while your shell waits -- and it sees this\n"
"machine only: what its card sent and what its card accepted. That is enough\n"
"to settle the argument no other tool can. A pristine box ships <i>policy\n"
"drop</i>, so ping reports <i>no answer</i> while the capture shows the echo\n"
"reply arriving: the packet came back and this machine's own filter ate it.\n"
"Without the frames, that is indistinguishable from a dead router.</p>"
"<p>Each one documents its own subset in <b>man</b>, and each refuses what it\n"
"cannot do rather than ignoring it: there is no <i>ip addr add</i>, no\n"
"traceroute times, no <i>ss -p</i>, and a tcpdump filter it cannot apply is an\n"
"error rather than a silently empty result.</p>"
},

{ "wiki.nomnix.org", "10.0.2.20", "/faq",
"<h1>FAQ</h1>"
"<h2>ls says the file is there and the loader says it is not</h2>"
"<p>It is a symlink and its target is gone. <b>stat</b> follows links; <b>ls</b>\n"
"shows you the link itself.</p>"
"<h2>pkg verify lists half a dozen files. Which one is the fault?</h2>"
"<p>Probably none of them on their own. This machine has been administered by\n"
"a person, and their deliberate edits show up as CHANGED because they ARE\n"
"changed. Work out which package to suspect from where the boot stopped --\n"
"<i>man boot</i> maps stage to package -- verify that one, and use\n"
"<b>pkg diff</b> before you touch anything. A diff that reads like a decision\n"
"is not a fault.</p>"
"<h2>pkg verify is clean but the machine will not boot</h2>"
"<p>Three possibilities, in order of likelihood: something not owned by a\n"
"package (the boot sector), a namespace binding, or a file whose CONTENTS are\n"
"legal but wrong -- a valid UUID that is not this disk's.</p>"
"<h2>I reinstalled the package and it is still broken</h2>"
"<p>Reinstall puts back what shipped, and it REFUSES to overwrite a config you\n"
"have edited. If the fault is in one of those files it will say so and leave\n"
"it alone; look at it with <b>pkg diff</b> and then either fix the line by\n"
"hand or use --force. If you forced it and the machine came up, check the\n"
"bench report -- it lists what you reverted.</p>"
"<h2>The initrd is waiting for a uuid. Which one is wrong?</h2>"
"<p><b>blkid</b> tells you what the disk ACTUALLY carries. Two configs agreeing\n"
"with each other and not with the disk is exactly the fault, so check both\n"
"/boot/zbl/zbl.cfg and /etc/fstab against blkid, not against each other.</p>"
"<h2>It boots, and something is still wrong</h2>"
"<p><b>svc</b> shows services rather than processes: enabled and running,\n"
"enabled and DEAD, or not meant to run at this runlevel at all. The boot\n"
"console scrolls past a service that gave up.</p>"
"<h2>Where did /boot/vmnomuz go?</h2>"
"<p>Somebody ran a cleanup script. It happens more than anyone admits. See\n"
"<a href=\"blog.internal/clean\">blog.internal/clean</a>.</p>"
},

{ "support.internal", "10.0.2.30", "/",
"<h1>NOMINAL support desk</h1>"
"<p>Open tickets are dispatched to your bench automatically.</p>"
"<h2>House rules</h2>"
"<ul>"
"<li>Boot it before you touch it. The console is evidence.</li>"
"<li>Find out what changed before you change anything.</li>"
"<li><b>pkg reinstall</b> is a hammer. Look first.</li>"
"<li>If it boots, you are done. Do not tidy.</li>"
"</ul>"
"<p>See also <a href=\"wiki.nomnix.org\">wiki.nomnix.org</a>, and\n"
"<a href=\"blog.internal\">blog.internal</a> for the same rules learned the\n"
"expensive way.</p>"
},

{ "nominal.local", "127.0.0.1", "/",
"<h1>this machine</h1>"
"<p>If you are reading this, the loopback address resolves and the browser\n"
"works. That is not nothing -- it means /etc/hosts is intact enough to find at\n"
"least one name.</p>"
"<p>Things this machine will tell you about itself, which no web page can:</p>"
"<pre>"
"svc          what should be running, and is not\n"
"netstat      what is actually listening\n"
"df -i        space and inodes\n"
"dmesg        what the last boot said\n"
"ns           this process's view of the filesystem"
"</pre>"
},

/* ------------------------------------------------------------------ *
 * The company. People use these, which is why they are a bit sad.
 * ------------------------------------------------------------------ */

{ "intranet.internal", "10.0.2.5", "/",
"<h1>CORVID LOGISTICS -- staff intranet</h1>"
"<img src=\"corvid_logo_final_FINAL_v3.gif\" alt=\"CORVID LOGISTICS -- Moving Things, Mostly\">"
"<h2>Inside</h2>"
"<ul>"
"<li><a href=\"helpdesk.internal\">helpdesk.internal</a> -- the ticket queue</li>"
"<li><a href=\"status.internal\">status.internal</a> -- service status board</li>"
"<li><a href=\"notices.internal\">notices.internal</a> -- all-staff notices</li>"
"<li><a href=\"cafeteria.internal\">cafeteria.internal</a> -- this week's menu</li>"
"<li><a href=\"home.internal\">home.internal</a> -- staff pages</li>"
"<li><a href=\"blog.internal\">blog.internal</a> -- sysadmin notes</li>"
"<li><a href=\"oldwiki.internal\">oldwiki.internal</a> -- Project HALYARD (archived)</li>"
"<li><a href=\"printer.internal\">printer.internal</a> -- the printer's own web page</li>"
"<li><a href=\"webmail.internal\">webmail.internal</a> -- webmail</li>"
"<li><a href=\"y2k.internal\">y2k.internal</a> -- Year 2000 readiness programme</li>"
"<li><a href=\"coffee.internal\">coffee.internal</a> -- second floor kitchen camera</li>"
"<li><a href=\"/policy\">/policy</a> -- IT acceptable use policy</li>"
"</ul>"
"<h2>Outside</h2>"
"<ul>"
"<li><a href=\"wiki.nomnix.org\">wiki.nomnix.org</a> -- NomnixOS documentation</li>"
"<li><a href=\"support.internal\">support.internal</a> -- the bench you are sitting at</li>"
"<li><a href=\"bofh.nomnix.org\">bofh.nomnix.org</a> -- do not forward this one to customers</li>"
"<li><a href=\"halbert.co.uk\">halbert.co.uk</a> -- hardware. We have an account "
"with them and everything on the floor came off their van</li>"
"</ul>"
"<p><b>A NEW INTRANET IS COMING.</b> Preview it at\n"
"<a href=\"/new\">intranet.internal/new</a>.</p>"
"<hr>"
"<p>Page owner: nomowner. Page owner has left the company. If you need this\n"
"page changed, raise a ticket, and see\n"
"<a href=\"helpdesk.internal/closed\">helpdesk.internal/closed</a> for how that\n"
"has gone for everyone else.</p>"
},

{ "intranet.internal", "10.0.2.5", "/new",
"<h1>INTRANET 2.0 -- PREVIEW -- DO NOT LINK EXTERNALLY</h1>"
"<img src=\"hero.psd\" alt=\"hero image goes here\">"
"<p>Corvid Logistics is a leading provider of PLACEHOLDER, delivering\n"
"PLACEHOLDER to PLACEHOLDER since PLACEHOLDER.</p>"
"<h2>Our Values</h2>"
"<ul>"
"<li>Value one</li>"
"<li>Value two</li>"
"<li>(three more, ask Marketing)</li>"
"</ul>"
"<hr>"
"<pre>"
"TODO nav bar\n"
"TODO search\n"
"TODO everything under Operations\n"
"TODO decide whether the old intranet is switched off or just unlinked\n"
"TODO ask nomowner where this is even served from"
"</pre>"
"<p>last modified: 14 months ago. go live: two weeks.</p>"
},

{ "intranet.internal", "10.0.2.5", "/policy",
"<h1>IT ACCEPTABLE USE POLICY (rev 9)</h1>"
"<ul>"
"<li>Passwords must be at least twelve characters, contain upper and lower\n"
"case, a digit, and a symbol, and must be changed every thirty days.</li>"
"<li>Passwords must not be written down.</li>"
"<li>Passwords must not be reused.</li>"
"<li>Passwords must not resemble any previous password.</li>"
"<li>The password reset form is on the new intranet.</li>"
"</ul>"
"<p>Approved by a committee that has never had to log in to anything.</p>"
"<h2>Section 4: unauthorised software</h2>"
"<p>Staff may not install software. Staff may not remove software. Staff may\n"
"not update software. Software is installed by IT. See the notice about IT on\n"
"<a href=\"notices.internal\">notices.internal</a>.</p>"
"<h2>Section 9: the server room</h2>"
"<p>The server room is not storage. The server room has never been storage.\n"
"Whoever put the Christmas decorations in front of the air intake: the room\n"
"was 41 degrees on Monday and we know it was you because the box has your\n"
"name on it.</p>"
},

{ "helpdesk.internal", "10.0.2.31", "/",
"<h1>HELPDESK -- open queue</h1>"
"<pre>"
"4471  warehouse    printer again. see notices.internal/printer   3d\n"
"4470  accounts     Excel is slow. (Excel is not installed here.)  3d\n"
"4468  night shift  node-4823 boots to a login prompt and then\n"
"                   nothing works. svc says httpd DEAD. we did not\n"
"                   touch anything.                                4d\n"
"4465  facilities   can the server room be used for storage        6d\n"
"4462  goods-in     the handheld says the date is 1970. ntpd?      8d\n"
"4459  nobody       ticket opened by the monitoring agent. It has\n"
"                   opened this ticket every night for five weeks.\n"
"                   Nobody knows what installed the agent.        35d\n"
"4451  reception    the machine at the front desk makes a noise\n"
"                   like a coin in a tumble dryer. Still works.   41d\n"
"4402  ops          UPS battery light. Ignoring. -- reassigned to\n"
"                   nomowner. nomowner has left the company.      93d\n"
"4388  unknown      \"who is authorised to power off the racks\"\n"
"                   No reply. Question stands.                   118d"
"</pre>"
"<p>See also <a href=\"/closed\">helpdesk.internal/closed</a>.</p>"
},

{ "helpdesk.internal", "10.0.2.31", "/closed",
"<h1>HELPDESK -- recently closed</h1>"
"<p>These are kept because the resolution field is the only training material\n"
"anybody here has ever actually read.</p>"
"<h2>4443 -- kernel is missing, ls says it is right there</h2>"
"<p>RESOLVED. /boot/vmnomuz is a symlink to a versioned image and the image\n"
"had been deleted. ls shows the link and it looks healthy; stat follows it and\n"
"says so. Reinstalled the kernel package.</p>"
"<h2>4437 -- reinstalled the package, still broken, please advise</h2>"
"<p>RESOLVED. The repository channel in /etc/pkg/repos.d was pointed at\n"
"testing, so reinstall fetched the same wrong version back and reported\n"
"success. Fix the channel FIRST, then reinstall.</p>"
"<h2>4431 -- disk not full but nothing can be created</h2>"
"<p>RESOLVED. Out of inodes, not bytes. df said 60% used and df -i said 100%.\n"
"Something wrote one small file per run since March.</p>"
"<h2>4429 -- machine is up, config is right, machine ignores config</h2>"
"<p>RESOLVED. The daemon read the file at startup and nobody reloaded it.\n"
"/run/&lt;name&gt;.state says what it actually loaded. kill -HUP the pid, or\n"
"svc reload the name, which is the same thing without the pid hunt. No file\n"
"on the disk was wrong. Four hours.</p>"
"<h2>4420 -- everything is dead after an upgrade</h2>"
"<p>RESOLVED. Booted the rescue medium, mounted /dev/sda1 on /mnt, and used\n"
"pkg --root /mnt because chroot refuses when the disk's own shell will not\n"
"run.</p>"
"<h2>4415 -- boots fine, nobody can log in</h2>"
"<p>RESOLVED. The login shell in /etc/passwd named a program that is not on\n"
"the disk. getty checks that before it offers a prompt, and it says which\n"
"account and which shell.</p>"
"<h2>4409 -- pkg verify is completely clean and it still will not boot</h2>"
"<p>RESOLVED. A .svc file in /etc/services.d that no package owns, marked\n"
"critical. pkg owns said nothing owns it. Deleted it. Third time this year.\n"
"Same vendor. See <a href=\"support.zephyrsys.com\">support.zephyrsys.com</a>,\n"
"which will tell you to reinstall the operating system.</p>"
"<h2>4396 -- can we have the server room for storage</h2>"
"<p>CLOSED -- WILL NOT FIX.</p>"
},

{ "status.internal", "10.0.2.61", "/",
"<h1>SERVICE STATUS</h1>"
"<pre>"
"Core network ............................ OPERATIONAL\n"
"Storage ................................. OPERATIONAL\n"
"Web ..................................... OPERATIONAL\n"
"Mail .................................... OPERATIONAL\n"
"Time .................................... OPERATIONAL\n"
"Print ................................... OPERATIONAL\n"
"Monitoring .............................. OPERATIONAL\n"
"Backups ................................. OPERATIONAL\n"
"\n"
"             ALL SYSTEMS OPERATIONAL\n"
"      No incidents reported. Have a great day!"
"</pre>"
"<hr>"
"<p>This board is maintained by hand from a spreadsheet. It has said ALL\n"
"SYSTEMS OPERATIONAL every day since it was created, including the two days\n"
"the building had no power.</p>"
"<p>What the machine itself will tell you, and this page will not:</p>"
"<pre>"
"svc                 which units are running, DEAD, or disabled\n"
"svc status <name>   why that one is unhappy, and how many times\n"
"netstat             what is actually listening, from /proc\n"
"ps                  what is actually running"
"</pre>"
"<p>See also <a href=\"/history\">status.internal/history</a>.</p>"
},

{ "status.internal", "10.0.2.61", "/history",
"<h1>UPTIME REPORT -- rolling twelve months</h1>"
"<pre>"
"Jan  100.00%    Jul  100.00%\n"
"Feb  100.00%    Aug  100.00%\n"
"Mar  100.00%    Sep  100.00%\n"
"Apr  100.00%    Oct  100.00%\n"
"May  100.00%    Nov  100.00%\n"
"Jun  100.00%    Dec  100.00%\n"
"\n"
"Twelve month availability: 100.00%"
"</pre>"
"<p>Methodology: an outage is counted when a member of the Availability\n"
"Working Group agrees in writing that an outage occurred. The group has not\n"
"met since the year before last.</p>"
"<h2>Known limitations of this report</h2>"
"<ul>"
"<li>March is the month /var/log/messages filled the disk.</li>"
"<li>Nothing here is measured. It is typed.</li>"
"</ul>"
},

{ "notices.internal", "10.0.2.62", "/",
"<h1>ALL-STAFF NOTICES</h1>"
"<ul>"
"<li><a href=\"/printer\">/printer</a> -- THE PRINTER (updated 11 times)</li>"
"</ul>"
"<ul>"
"<li>Fire drill Thursday. If you hear the alarm on Wednesday that is a\n"
"different problem and you should still leave the building.</li>"
"<li>The fridge in the second floor kitchen will be emptied on Friday.\n"
"Everything in it will be emptied. Everything.</li>"
"<li>Please do not power off equipment in the room marked SERVER ROOM,\n"
"including the equipment that appears to be doing nothing. Especially that\n"
"equipment. See ticket 4402.</li>"
"<li>IT would like to remind everyone that IT is one person and that person\n"
"left in March.</li>"
"<li>Lost: one lanyard, one dignity. Reception.</li>"
"</ul>"
},

{ "notices.internal", "10.0.2.62", "/printer",
"<h1>NOTICE: THE PRINTER</h1>"
"<pre>"
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
"v11  Somebody hit it. It works now. Nobody is happy about this."
"</pre>"
"<p>v10 -- The print server is a machine like any other. If it is not\n"
"answering, <b>netstat</b> on it will tell you whether anything is listening\n"
"at all, and <b>svc</b> will tell you whether the daemon is running. Both are\n"
"more use than hitting it.</p>"
"<p>The printer has its own web page at\n"
"<a href=\"printer.internal\">printer.internal</a>. It is not reassuring.</p>"
},

{ "cafeteria.internal", "10.0.2.66", "/",
"<h1>CAFETERIA -- this week</h1>"
"<pre>"
"MON   Soup of the day (tomato)          Pie, chips, beans\n"
"TUE   Soup of the day (tomato)          Curry, rice\n"
"WED   Soup of the day (tomato)          Pie, chips, beans\n"
"THU   Soup of the day (tomato)          Curry, rice\n"
"FRI   Soup of the day (tomato)          FISH"
"</pre>"
"<p>Vegetarian option: beans. Vegan option: ask, and then have the beans.</p>"
"<p>Hot food 11:45 - 13:30. The till has been cash-only since the card reader\n"
"went, which was a Tuesday, which was a curry day, which is how we all\n"
"remember it.</p>"
"<p>This menu has been the same for nineteen weeks. The page is generated\n"
"nightly by a script that copies last week's menu, and nobody has ever\n"
"replaced the seed file. It is, in fairness, running perfectly.</p>"
},

{ "home.internal", "10.0.2.70", "/",
"<h1>STAFF PAGES</h1>"
"<ul>"
"<li><a href=\"/~rkeeler\">/~rkeeler</a> -- R. Keeler, Facilities</li>"
"<li><a href=\"/~nomowner\">/~nomowner</a> -- (redirects to blog.internal)</li>"
"</ul>"
"<p>Staff pages were discontinued. These two survive because the process for\n"
"removing them requires a form that is hosted on the new intranet.</p>"
},

{ "home.internal", "10.0.2.70", "/~rkeeler",
"<h1>Welcome to Rob's Homepage!</h1>"
"<img src=\"construction.gif\" alt=\"UNDER CONSTRUCTION -- animated, spinning\">"
"<p>Hi! I'm Rob and I work in Facilities. This is my page on the work intranet\n"
"which they let us have now, which I think is pretty cool.</p>"
"<h2>ABOUT ME</h2>"
"<ul>"
"<li>Been here since 2004</li>"
"<li>Coffee (2 sugars)</li>"
"<li>My son's football team won the league!!</li>"
"</ul>"
"<h2>LINKS</h2>"
"<ul>"
"<li><a href=\"wiki.nomnix.org\">The wiki</a></li>"
"<li><a href=\"bofh.nomnix.org\">That page with the excuses on it</a>, very funny</li>"
"<li><a href=\"www.tripodal.net/~zorak\">My friend Zorak's page</a>, he does the\n"
"computers at his work too</li>"
"</ul>"
"<img src=\"webring.gif\" alt=\"broken image\">"
"<h2>UPDATES</h2>"
"<pre>"
"14/02/2011 - Added photos of the new loading bay\n"
"09/03/2011 - Photos coming soon, still waiting on the camera\n"
"22/06/2011 - Photos coming soon"
"</pre>"
"<hr>"
"<p>Last updated: 22 June 2011. Visitor number: 00000417.\n"
"This page is best viewed at 800x600.</p>"
},

{ "home.internal", "10.0.2.70", "/~nomowner",
"<h1>nomowner</h1>"
"<p>Systems. Left in March.</p>"
"<p>This page used to redirect to blog.internal, back when the intranet could\n"
"do redirects. It cannot any more and nobody knows when that stopped.</p>"
"<ul>"
"<li><a href=\"blog.internal\">blog.internal</a> -- the notes, which are the\n"
"useful part</li>"
"<li><a href=\"bofh.nomnix.org\">bofh.nomnix.org</a> -- the other notes</li>"
"</ul>"
"<p>On any machine here, /home/nomowner/notes.txt is the same advice in ten\n"
"numbered lines, and it is on the disk whether the network works or not.</p>"
},

{ "oldwiki.internal", "10.0.2.71", "/",
"<h1>PROJECT HALYARD -- project wiki</h1>"
"<p><b>ARCHIVED.</b> This wiki is read-only. Do not raise tickets against\n"
"HALYARD. There is nobody to assign them to.</p>"
"<ul>"
"<li><a href=\"/charter\">/charter</a> -- what HALYARD was going to be</li>"
"<li><a href=\"/runbook\">/runbook</a> -- the runbook (incomplete)</li>"
"<li><a href=\"/meeting\">/meeting</a> -- minutes, 14 of them, the last one is\n"
"one line</li>"
"<li><a href=\"/boot-guide\">/boot-guide</a> -- a departmental guide to\n"
"booting, whose OUT OF DATE banner is itself out of date</li>"
"</ul>"
"<p>HALYARD was the replacement for the thing HALYARD was written to replace,\n"
"which is still running, and which everything still depends on.</p>"
},

{ "oldwiki.internal", "10.0.2.71", "/charter",
"<h1>PROJECT HALYARD -- charter</h1>"
"<p><b>PURPOSE.</b> To replace the current arrangement with a modern, unified,\n"
"scalable platform.</p>"
"<p><b>SCOPE.</b> All of it.</p>"
"<p><b>OUT OF SCOPE.</b> Migrating anything currently running. Documenting\n"
"anything currently running. Speaking to the person who runs it.</p>"
"<p><b>SUCCESS CRITERIA.</b> To be agreed at the next steering meeting.</p>"
"<h2>RISKS</h2>"
"<ul>"
"<li>Key person dependency (nomowner). MITIGATION: none identified.</li>"
"<li>The current arrangement continues to work, reducing urgency. This risk\n"
"materialised.</li>"
"</ul>"
"<p>STATUS: parked. See <a href=\"/meeting\">/meeting</a>.</p>"
},

{ "oldwiki.internal", "10.0.2.71", "/runbook",
"<h1>HALYARD RUNBOOK (DRAFT -- DO NOT FOLLOW)</h1>"
"<ul>"
"<li>Confirm the node is actually down. <b>svc</b> on the node; if you cannot\n"
"get to a prompt at all, the console is the evidence, so power cycle it and\n"
"READ what it says on the way up rather than looking away.</li>"
"<li>TODO: someone who knows what step 2 is</li>"
"<li>If the node boots and HALYARD is not running, <i>svc status halyard</i>.\n"
"(Note from review: there is no halyard unit. There was never a halyard unit.\n"
"This document describes a machine that was not built.)</li>"
"<li>Escalate to the HALYARD on-call rota. (Note from review: there is no\n"
"rota.)</li>"
"<li>TODO</li>"
"</ul>"
"<p><b>REVIEWER'S SUMMARY:</b> the only true sentence in this document is step\n"
"1, and step 1 is true of every machine we own, which rather suggests where\n"
"the effort should have gone.</p>"
},

{ "oldwiki.internal", "10.0.2.71", "/meeting",
"<h1>HALYARD -- minutes</h1>"
"<pre>"
"#1   Kickoff. Everyone very positive. Scope: everything.\n"
"#4   Scope reduced to everything except the hard part.\n"
"#7   Discussion of what the hard part is. No conclusion.\n"
"#9   Agreed to write a runbook before writing the system, so that the\n"
"     system can be written to match the runbook.\n"
"#11  The runbook describes a system nobody has agreed to build.\n"
"#12  Two attendees. Neither is on the project.\n"
"#13  Rescheduled.\n"
"#14  \"Parked.\""
"</pre>"
},

/* ------------------------------------------------------------------ *
 * The previous administrator. Everything on this host is true, which is
 * the point of it: the flavour and the hints are the same sentences.
 * ------------------------------------------------------------------ */

{ "blog.internal", "10.0.2.72", "/",
"<h1>nomowner's notes</h1>"
"<p>Things I got wrong, written down so I get them wrong faster next time.</p>"
"<ul>"
"<li><a href=\"/uuid\">/uuid</a> -- the disk does not care what your config\n"
"believes</li>"
"<li><a href=\"/clean\">/clean</a> -- the cleanup script, and what it took with\n"
"it</li>"
"<li><a href=\"/stale\">/stale</a> -- running and wrong is a third state</li>"
"<li><a href=\"/order\">/order</a> -- where it stopped tells you what to\n"
"suspect</li>"
"<li><a href=\"/goodbye\">/goodbye</a> -- last post</li>"
"</ul>"
"<p>If you are the person who came after me: sorry about /var/log. Read\n"
"/home/nomowner/notes.txt on the machine itself, it is the short version of\n"
"all of this and it is ten numbered lines.</p>"
},

{ "blog.internal", "10.0.2.72", "/uuid",
"<h1>the disk does not care what your config believes</h1>"
"<p>Spent a whole evening on a machine that hung in the initrd waiting for a\n"
"root filesystem. Checked /boot/zbl/zbl.cfg. Checked /etc/fstab. They agreed.\n"
"I took that as confirmation and went looking somewhere else for three\n"
"hours.</p>"
"<p><b>TWO CONFIGS AGREEING IS NOT EVIDENCE.</b> They were both written by the\n"
"same person on the same afternoon, so of course they agree. The only thing in\n"
"the building that knows what UUID that disk carries is the disk:</p>"
"<pre>blkid</pre>"
"<p>and it took four seconds. If the UUID in zbl.cfg is a well-formed UUID that\n"
"simply is not this disk's, nothing is corrupt, every hash matches, and\n"
"<i>pkg verify</i> will hand you a clean bill of health while the machine sits\n"
"there waiting for a disk that does not exist.</p>"
"<p>Reinstalling the bootloader package does NOT fix this, because the package\n"
"ships the config for the machine it was BUILT for, not the one in front of\n"
"you. <b>zbl-mkconfig</b> regenerates it from this machine. That is the\n"
"difference between putting a file back and making it true.</p>"
},

{ "blog.internal", "10.0.2.72", "/clean",
"<h1>the cleanup script</h1>"
"<p>I wrote a script to tidy /boot. It removed the versioned kernel image\n"
"because the filename did not match the pattern it expected, and it decided\n"
"that meant stale. It did not remove /boot/vmnomuz, because /boot/vmnomuz is a\n"
"SYMLINK and the symlink was fine.</p>"
"<p>So <i>ls /boot</i> looked perfect. The loader said the kernel was missing.\n"
"I believed ls for an hour.</p>"
"<pre>"
"ls    shows you the link\n"
"stat  follows it and tells you the truth"
"</pre>"
"<p><i>pkg verify</i> reports this as MISSING, which is the honest answer, and\n"
"<i>pkg reinstall</i> on the kernel package puts the image back. The script is\n"
"still in my home directory and it is still disabled and it is staying\n"
"disabled.</p>"
"<p>Same class of mistake, different day: a REPOINTED symlink. The link is\n"
"there, the target is there, the target is the wrong file. ls shows you\n"
"nothing wrong at all.</p>"
},

{ "blog.internal", "10.0.2.72", "/stale",
"<h1>running and wrong</h1>"
"<p>A service has three states, not two. Running, dead, and running with a\n"
"configuration that stopped being the configuration on disk some time ago. The\n"
"third one has taken more of my life than the other two combined.</p>"
"<p>A daemon reads its config ONCE, at startup. Edit the file afterwards and\n"
"the process keeps what it already has. So the file is a description of what\n"
"the machine is supposed to do, and the machine is doing something else, and\n"
"nothing anywhere is corrupt.</p>"
"<pre>cat /run/<name>.state</pre>"
"<p>is what that daemon ACTUALLY loaded -- the file it read and the setting it\n"
"took out of it. When that disagrees with the file, you have found it.\n"
"<b>netstat</b> catches the same thing from the other end: it lists the port\n"
"the running process really has, not the one the config now claims.</p>"
"<p>The fix is a signal, not a file:</p>"
"<pre>"
"ps                 find the pid\n"
"kill -HUP <pid>    make it re-read"
"</pre>"
"<p>and the reason this is so miserable to catch is that rebooting makes it\n"
"vanish. The daemon comes up reading the new file and the evidence is gone,\n"
"along with any chance of explaining what happened.</p>"
},

{ "blog.internal", "10.0.2.72", "/order",
"<h1>where it stopped</h1>"
"<p>I keep this on a card. The stage a machine dies at is half the diagnosis,\n"
"because it tells you which half of the system to stop looking at.</p>"
"<pre>"
"zbios        finds a boot sector on the disk. Not a file. No package\n"
"             owns it, so pkg verify is clean and always will be.\n"
"zbl          reads /boot/zbl/zbl.cfg -- kernel, initrd, root UUID.\n"
"kernel       loads /boot/vmnomuz (a symlink to the versioned image).\n"
"initrd       loads /boot/initrd, needs the block and filesystem\n"
"             drivers, then finds the root BY UUID.\n"
"init         /sbin/init, pid 1.\n"
"rc           /bin/rc runs /etc/rc.boot.\n"
"mounts       /sbin/mountall reads /etc/fstab and mounts what is in it,\n"
"             including deciding ro or rw for the root itself.\n"
"runlevel     /etc/rc.d/rc.3.\n"
"services     /sbin/svcinit reads /etc/services.d and starts the units\n"
"             in dependency order.\n"
"login        /sbin/getty checks the account is in /etc/passwd and that\n"
"             its shell exists and can be executed, then offers a prompt."
"</pre>"
"<p>Stopped before init and it is the disk, the loader or the images. Stopped\n"
"after init and it is a config file somebody edited. Booted to a prompt and\n"
"still wrong and it is <b>svc</b>, every time.</p>"
},

{ "blog.internal", "10.0.2.72", "/goodbye",
"<h1>last post</h1>"
"<p>Leaving on Friday. Handover notes, since there is nobody to hand over\n"
"to.</p>"
"<ul>"
"<li>The bench work is a loop and the loop is: read the console, find out what\n"
"CHANGED, then change one thing. Not the other order.</li>"
"<li><i>pkg verify</i> is not a fault list. Every machine that a person has\n"
"administered has files that differ from what shipped, because the person\n"
"decided so on purpose. Use <b>pkg diff</b> and read it. A diff that reads like\n"
"a decision is not a fault.</li>"
"<li><i>pkg reinstall</i> will not overwrite config you have edited. It names\n"
"the files and leaves them. --force overwrites and keeps a .pkgsave copy, and\n"
"that is the only undo there is.</li>"
"<li>Everything above only works if something can still run. When the libc is\n"
"the casualty, nothing on that disk runs, chroot refuses, and the rescue medium\n"
"is the whole answer.</li>"
"<li><b>find /var -type f</b> and <b>df</b> between them have solved more of my\n"
"tickets than any amount of cleverness. Something is always growing.</li>"
"<li>The status board is not a source of truth. Nothing that a human types into\n"
"a spreadsheet is a source of truth. The machine is.</li>"
"</ul>"
"<pre>"
"rcon media insert\n"
"rcon boot media\n"
"rcon power cycle\n"
"mount /dev/sda1 /mnt\n"
"pkg --root /mnt verify"
"</pre>"
"<p>Good luck. The coffee is bad on purpose.</p>"
},

/* ------------------------------------------------------------------ *
 * BOFH. Jokes only -- where a line happens to be true it is marked as
 * true, because an excuse that is real is funnier and also useful.
 * ------------------------------------------------------------------ */

{ "bofh.nomnix.org", "10.0.2.44", "/",
"<h1>THE BASTARD OPERATOR FROM HELL</h1>"
"<p><b>Excuse of the day:</b> it's not a bug, it's an undocumented feature of\n"
"the initrd.</p>"
"<h2>Previously</h2>"
"<ul>"
"<li>cosmic rays flipped a bit in your symlink</li>"
"<li>the package manager is sulking</li>"
"<li>somebody chmod'd it for security reasons</li>"
"<li>it worked on the test machine</li>"
"<li>that file was never load-bearing until it was</li>"
"</ul>"
"<ul>"
"<li><a href=\"/excuse\">/excuse</a> -- the full list</li>"
"<li><a href=\"/rules\">/rules</a> -- house rules, the honest version</li>"
"<li><a href=\"/jokes\">/jokes</a> -- the ones that are actually funny</li>"
"<li><a href=\"/haiku\">/haiku</a> -- error messages, improved</li>"
"</ul>"
},

{ "bofh.nomnix.org", "10.0.2.44", "/excuse",
"<h1>EXCUSE GENERATOR v0.3</h1>"
"<p>[ generate ] -- button not implemented, here is the whole list.</p>"
"<pre>"
" 1  cosmic rays flipped a bit in your symlink\n"
" 2  the package manager is sulking\n"
" 3  somebody chmod'd it for security reasons\n"
" 4  it worked on the test machine\n"
" 5  that file was never load-bearing until it was\n"
" 6  the UUID moved\n"
" 7  temporary read-only, from March\n"
" 8  the initrd is thinking about it\n"
" 9  the daemon is running, it is simply not listening to you\n"
"10  it is not down, it is between runlevels\n"
"11  the log would explain everything but the disk is full of the log\n"
"12  a vendor agent did it and the vendor no longer exists\n"
"13  we have always been at 100.00% uptime\n"
"14  that is not a bug, that is a decision somebody made in 2011\n"
"15  the fix is a signal, and I am not in the mood\n"
"16  the symlink is fine. The symlink has never been the problem.\n"
"17  it is in the queue behind the printer\n"
"18  we support that configuration, we just do not support it working"
"</pre>"
"<p>Excuse 11 is real and happens constantly. Excuse 9 is real too. Excuse 6\n"
"is real if you read it as \"somebody cloned the disk\". The rest of them are\n"
"excuses, which is a category of statement that is true about the speaker\n"
"rather than the machine.</p>"
},

{ "bofh.nomnix.org", "10.0.2.44", "/rules",
"<h1>HOUSE RULES, THE HONEST VERSION</h1>"
"<ul>"
"<li>The machine is not lying to you. It is describing what somebody did.</li>"
"<li>Two files agreeing with each other is not evidence. Ask the device.</li>"
"<li>If verify is clean and it is still broken, the fault is not a file: the\n"
"boot sector, a directory's mode, a namespace bind, a full disk, no free\n"
"inodes, or a process running on a config that no longer exists anywhere on\n"
"the disk.</li>"
"<li>Reinstalling everything that shows up in verify is not a diagnosis, it is\n"
"a coin toss with a receipt.</li>"
"<li>If it boots and it is healthy, stop. Do not tidy. Tidying is how the\n"
"kernel image went missing in the first place.</li>"
"</ul>"
},

{ "bofh.nomnix.org", "10.0.2.44", "/jokes",
"<h1>JOKES, COLLECTED</h1>"
"<p>Curated. The bad ones are on the old page and the old page is gone,\n"
"because I tidied, which is rule five and I broke it.</p>"
"<ul>"
"<li>There are two hard problems in systems administration: knowing who\n"
"changed it, off-by-one errors, and knowing who changed it.</li>"
"<li>A user calls to say the machine is slow. It has been up for four hundred\n"
"days. Those are the same sentence.</li>"
"<li>Backups are a religion. Restores are a science. We are very devout.</li>"
"<li>The documentation is accurate as of the moment it was written, which was\n"
"before the change that made it necessary.</li>"
"<li>Every monitoring system eventually monitors itself, notices it is down,\n"
"and cannot tell you.</li>"
"<li>\"Have you tried turning it off and on again\" is not a joke, it is a\n"
"diagnostic that discards the evidence, which is why it works and why you\n"
"should read the console first.</li>"
"<li>A junior asks why we cannot just reinstall. A senior asks what changed.\n"
"A principal asks who benefits.</li>"
"<li>The most dangerous string in any language is \"temporarily\".</li>"
"<li>Nothing on a computer is haunted. Everything on a computer was done by a\n"
"person, on purpose, at 4pm on a Friday, and they are not answering their\n"
"phone.</li>"
"<li>The disk was 100% full and 100% of it was the log explaining that the\n"
"disk was full.</li>"
"</ul>"
"<h2>Overheard in the server room</h2>"
"<ul>"
"<li>\"It's not production.\" It was production.</li>"
"<li>\"Nobody uses that.\" Everybody used that.</li>"
"<li>\"I'll document it after.\" There is no after.</li>"
"<li>\"That's a one-line change.\" It was a one-line change.</li>"
"</ul>"
},

{ "bofh.nomnix.org", "10.0.2.44", "/haiku",
"<h1>ERROR MESSAGES, IMPROVED</h1>"
"<pre>"
"The kernel is gone.\n"
"ls shows you a healthy link\n"
"pointing at nothing.\n"
"\n"
"Config file, edited.\n"
"The daemon read it last week.\n"
"Send it a signal.\n"
"\n"
"Free space: forty gigs.\n"
"Free inodes: not even one.\n"
"Nothing can be born.\n"
"\n"
"The status page says\n"
"all systems operational.\n"
"The building is dark.\n"
"\n"
"Two configs agree.\n"
"Neither has ever once asked\n"
"the disk what it is.\n"
"\n"
"A vendor agent\n"
"drops a unit file at night.\n"
"The vendor is dead."
"</pre>"
"<p>Written during an outage. The outage was excuse 11.</p>"
},

/* ------------------------------------------------------------------ *
 * The wider internet. Jokes, mostly -- but the technical claims are
 * still checked, because a player cannot tell which host they are on
 * when they are three links deep and tired.
 * ------------------------------------------------------------------ */

{ "nomnix.org", "10.0.2.21", "/",
"<h1>NomnixOS</h1>"
"<img src=\"nomnix_banner.gif\" alt=\"NomnixOS -- An Operating System For People Who Have Been Paged\">"
"<p>NomnixOS is a general purpose operating system. It boots in seven stages,\n"
"owns its files in packages, and tells you the truth when you ask it a\n"
"question, which is more than can be said for the status board.</p>"
"<ul>"
"<li><a href=\"/download\">/download</a> -- get the ISO</li>"
"<li><a href=\"/press\">/press</a> -- what people are saying</li>"
"<li><a href=\"wiki.nomnix.org\">wiki.nomnix.org</a> -- the documentation</li>"
"<li><a href=\"bugs.nomnix.org\">bugs.nomnix.org</a> -- the bug tracker</li>"
"<li><a href=\"forums.nomnix.org\">forums.nomnix.org</a> -- the forums</li>"
"<li><a href=\"rfc.nomnix.org\">rfc.nomnix.org</a> -- the standards, such as\n"
"they are</li>"
"</ul>"
"<hr>"
"<p><i>Best viewed in any browser. Genuinely. That was the whole point.</i></p>"
},

{ "nomnix.org", "10.0.2.21", "/download",
"<h1>Download NomnixOS 6.4</h1>"
"<pre>"
"nomnix-6.4-x86_64.iso       681 MB   [ mirror list ]\n"
"nomnix-6.4-rescue.iso        44 MB   [ mirror list ]\n"
"nomnix-6.4-source.tar.gz    212 MB   [ mirror list ]\n"
"SHA256SUMS                    1 KB   [ mirror list ]\n"
"SHA256SUMS.asc                1 KB   [ mirror list ]"
"</pre>"
"<p>All mirrors are currently maintained by one volunteer, who is on holiday.\n"
"The mirror list is a text file that has not been edited since two of the\n"
"three mirrors were switched off.</p>"
"<p>You do not need this. The rescue system is already in the machine's own\n"
"drive: <b>rcon media insert</b>, then <b>rcon boot media</b>. It is never\n"
"damaged, which is the only reason it is trustworthy.</p>"
"<p><b>Verify what you download.</b> Then verify what is installed:\n"
"<i>pkg verify</i> hashes every file a package owns against what shipped, and\n"
"names the ones that differ.</p>"
},

{ "nomnix.org", "10.0.2.21", "/press",
"<h1>What people are saying</h1>"
"<ul>"
"<li>\"It boots.\" -- a system administrator</li>"
"<li>\"Fewer moving parts than I expected, which turns out to matter at\n"
"three in the morning.\" -- Linux Bimonthly</li>"
"<li>\"I asked it what was wrong and it told me. I did not know that was an\n"
"option.\" -- a customer</li>"
"<li>\"No opinion. We reinstall everything anyway.\" -- an enterprise</li>"
"<li>\"Zero stars. The package manager refused to overwrite my config file and\n"
"I had to read what it said.\" -- a review</li>"
"</ul>"
"<p>The last one is our favourite and it is on a poster in the office.</p>"
},

{ "forums.nomnix.org", "10.0.2.22", "/",
"<h1>NomnixOS forums</h1>"
"<pre>"
"BOARD                              TOPICS   LAST POST\n"
"Installation                         4102   14 minutes ago\n"
"Boot problems                       19883   2 minutes ago\n"
"Packages and repositories            6621   an hour ago\n"
"Networking                           3390   yesterday\n"
"Off topic                           41205   9 seconds ago"
"</pre>"
"<h2>Hot topics</h2>"
"<ul>"
"<li><a href=\"/t8891\">machine hangs at initrd, waiting for root -- 7 pages</a></li>"
"<li>Anyone else's fans loud? -- 212 replies, none about fans</li>"
"<li>[SOLVED] -- thread deleted by author</li>"
"<li>Sticky: read this before posting (nobody has read this)</li>"
"<li><a href=\"lists.nomnix.org/inodes\">crossposted from the mailing list:\n"
"df says 48% and I cannot create a file</a></li>"
"</ul>"
"<p>Registration requires an email address, a forum name, and a reason for\n"
"joining, which is reviewed by a moderator who was last seen in 2014.</p>"
},

{ "forums.nomnix.org", "10.0.2.22", "/t8891",
"<h1>machine hangs at initrd, waiting for root (page 1 of 7)</h1>"
"<h2>#1 -- brakeman88</h2>"
"<p>Hi all. Machine got power cycled by the cleaners and now it stops at the\n"
"initrd saying it is waiting for the root filesystem. It sits there. I did not\n"
"change anything. Please help, this is urgent, it is a production box.</p>"
"<h2>#2 -- lurker_9</h2>"
"<p>have you tried reinstalling</p>"
"<h2>#3 -- brakeman88</h2>"
"<p>Reinstalling what</p>"
"<h2>#4 -- lurker_9</h2>"
"<p>everything</p>"
"<h2>#5 -- kaz</h2>"
"<p>Post the last twenty lines of the boot log. <i>dmesg</i> reads this boot\n"
"and <i>dmesg -1</i> reads the previous one, so you have the failed boot even\n"
"though it failed. Nobody can help you without it.</p>"
"<h2>#6 -- brakeman88</h2>"
"<p>I can't get to a prompt, it's hung</p>"
"<h2>#7 -- kaz</h2>"
"<p>Then boot the rescue medium, mount the disk at /mnt, and read the\n"
"customer's log from there: <i>dmesg -r /mnt</i>. It is the same log. It was\n"
"written while the boot was failing.</p>"
"<h2>#8 -- MODERATOR</h2>"
"<p>Moved to Boot problems. Please use tags.</p>"
"<p><a href=\"/t8891p4\">page 4</a> -- <a href=\"/t8891p7\">page 7</a></p>"
},

{ "forums.nomnix.org", "10.0.2.22", "/t8891p4",
"<h1>machine hangs at initrd, waiting for root (page 4 of 7)</h1>"
"<h2>#61 -- pyxis</h2>"
"<p>Just run <b>nomctl repair --boot</b>, it fixes this in one step.</p>"
"<h2>#62 -- kaz</h2>"
"<p>There is no nomctl. You are thinking of a different operating system. The\n"
"commands here are pkg, svc, blkid, fsck, mount, chroot, ldd, dmesg and\n"
"friends, and <b>man</b> lists them. Please stop telling people to type things\n"
"that do not exist; the last person who did that had someone spend an evening\n"
"looking for a package that was never in the repository.</p>"
"<h2>#63 -- pyxis</h2>"
"<p>works on my machine</p>"
"<h2>#64 -- brakeman88</h2>"
"<p>I booted the rescue medium like kaz said. The log says it is waiting for\n"
"a root filesystem with a UUID. The UUID in /boot/zbl/zbl.cfg is exactly the\n"
"same as the one in /etc/fstab so surely that is fine?</p>"
"<h2>#65 -- kaz</h2>"
"<p>Those two files were written by the same person on the same afternoon.\n"
"They agree with each other. That is not evidence about the disk. Run\n"
"<b>blkid</b> and compare both of them against what the device actually\n"
"carries.</p>"
"<h2>#66 -- lurker_9</h2>"
"<p>or just reinstall</p>"
"<h2>#67 -- helpful_hank</h2>"
"<p>This happened to me and it was the SATA cable. Different problem, but\n"
"worth checking. Also my brother had a similar issue and it was RAM.</p>"
"<p><a href=\"/t8891\">page 1</a> -- <a href=\"/t8891p7\">page 7</a></p>"
},

{ "forums.nomnix.org", "10.0.2.22", "/t8891p7",
"<h1>machine hangs at initrd, waiting for root (page 7 of 7)</h1>"
"<h2>#118 -- kaz</h2>"
"<p>For anyone finding this later, here is the whole thing in order. Longer\n"
"version with the reasoning on my own page,\n"
"<a href=\"kaznotes.net\">kaznotes.net</a>.</p>"
"<pre>"
"rcon media insert           the rescue medium is never damaged\n"
"rcon boot media\n"
"rcon power cycle\n"
"mount /dev/sda1 /mnt        the customer's disk\n"
"dmesg -r /mnt               the failed boot, read from outside it\n"
"blkid                       what the disk ACTUALLY carries\n"
"cat /mnt/boot/zbl/zbl.cfg   what the loader was told\n"
"cat /mnt/etc/fstab          what the system was told"
"</pre>"
"<p>If blkid disagrees with either file, fix the file -- one <b>sed -i</b> is\n"
"enough -- and nothing on that disk was ever corrupt. If blkid agrees with\n"
"both, the fault is elsewhere and you have ruled out a whole layer in about\n"
"ninety seconds.</p>"
"<h2>#119 -- brakeman88</h2>"
"<p>nevermind, fixed it</p>"
"<h2>#120 -- kaz</h2>"
"<p>How?</p>"
"<h2>#121 -- MODERATOR</h2>"
"<p>Marking [SOLVED]. Thread locked to keep the board tidy.</p>"
"<h2>#122 -- (deleted)</h2>"
"<h2>#123 -- someone, four years later</h2>"
"<p>I have this exact problem and I have read all seven pages.</p>"
},

{ "rfc.nomnix.org", "10.0.2.23", "/",
"<h1>Requests For Comments</h1>"
"<p>The standards process, such as it is. Comments were requested. Comments\n"
"were received. The comments are why some of these exist.</p>"
"<ul>"
"<li><a href=\"/rfc0031\">RFC 0031</a> -- IP over Sneakernet</li>"
"<li><a href=\"/rfc0064\">RFC 0064</a> -- The Blame Transfer Protocol (BTP)</li>"
"<li><a href=\"/rfc0100\">RFC 0100</a> -- Reboot Considered Harmful</li>"
"<li>RFC 0002 -- On The Numbering Of RFCs (withdrawn, numbering dispute)</li>"
"<li>RFC 0088 -- A Standard For Standards (never ratified, cited constantly)</li>"
"</ul>"
},

{ "rfc.nomnix.org", "10.0.2.23", "/rfc0031",
"<h1>RFC 0031: IP over Sneakernet</h1>"
"<p><b>Status of this Memo.</b> This memo describes a transport that has never\n"
"failed a delivery in the history of this building, which is more than the\n"
"network can say.</p>"
"<h2>1. Introduction</h2>"
"<p>The datagram is written to removable media. The media is carried by an\n"
"operator to the destination host. Latency is high. Bandwidth is a function of\n"
"how much the operator can hold, and is therefore enormous.</p>"
"<h2>2. Addressing</h2>"
"<p>The address is written on the media in marker pen. Address resolution is\n"
"performed by asking someone. Where the marker pen has rubbed off, the\n"
"datagram is delivered to whoever seems most likely, which is the same failure\n"
"mode as a hosts file nobody maintains.</p>"
"<h2>3. Congestion control</h2>"
"<p>The operator gets tired.</p>"
"<h2>4. Security considerations</h2>"
"<p>The transport is trivially subject to interception at the coffee machine,\n"
"which is where all the important routing decisions in this company are made\n"
"anyway.</p>"
"<h2>5. Reliability</h2>"
"<p>Retransmission is implemented by shouting.</p>"
},

{ "rfc.nomnix.org", "10.0.2.23", "/rfc0064",
"<h1>RFC 0064: The Blame Transfer Protocol</h1>"
"<h2>1. Overview</h2>"
"<p>BTP is a connectionless protocol for moving responsibility between parties\n"
"without moving any information. It is already deployed everywhere and this\n"
"memo merely documents existing practice.</p>"
"<h2>2. Message types</h2>"
"<ul>"
"<li>WORKSFORME -- the sender declines the transfer</li>"
"<li>CANNOTREPRODUCE -- the sender declines the transfer, politely</li>"
"<li>NEEDINFO -- the sender delays the transfer indefinitely</li>"
"<li>ESCALATE -- the transfer is forwarded to a party that no longer exists</li>"
"<li>NOTOURPROBLEM -- broadcast; every recipient forwards it</li>"
"</ul>"
"<h2>3. Loop prevention</h2>"
"<p>There is none. A BTP loop is stable, self-sustaining, and can persist for\n"
"years. Implementations SHOULD NOT attempt to break the loop, as the loop is\n"
"load-bearing.</p>"
"<h2>4. Interoperability</h2>"
"<p>BTP interoperates perfectly with every ticketing system ever written and\n"
"with no operating system at all. A machine cannot speak BTP: ask it what is\n"
"wrong and it simply tells you, which is why engineers find machines restful\n"
"and meetings tiring.</p>"
},

{ "rfc.nomnix.org", "10.0.2.23", "/rfc0100",
"<h1>RFC 0100: Reboot Considered Harmful</h1>"
"<h2>1. Abstract</h2>"
"<p>Power cycling a machine is an effective repair and a catastrophic\n"
"diagnostic. This memo argues that the two facts are the same fact.</p>"
"<h2>2. The mechanism</h2>"
"<p>A running process holds the configuration it read when it started. If the\n"
"file on disk was edited afterwards, the process and the file disagree, and\n"
"the disagreement is the fault. Rebooting resolves the disagreement by\n"
"destroying the process, and with it the evidence that anybody ever edited\n"
"anything.</p>"
"<h2>3. Recommended practice</h2>"
"<p>Before power cycling, capture what the running system knows:</p>"
"<pre>"
"svc                  what is running now\n"
"ps                   the process table\n"
"netstat              the ports actually held\n"
"cat /run/<name>.state what a daemon really loaded"
"</pre>"
"<p>Where the disagreement is confirmed, the repair is <b>kill -HUP</b> on the\n"
"pid -- or <b>svc reload &lt;name&gt;</b>, which is the same signal without\n"
"having to find the pid first -- and not a reboot. Where the daemon does not\n"
"take signals, <b>svc restart &lt;name&gt;</b>, which is still not a reboot.\n"
"This preserves the evidence AND fixes the machine, which\n"
"the working group notes is unusual and should be enjoyed.</p>"
"<h2>4. Dissenting opinion</h2>"
"<p>It was 3am and it worked. -- one member, minuted at their own request</p>"
},

{ "asciiart.nomnix.org", "10.0.2.24", "/",
"<h1>THE ASCII ART ARCHIVE</h1>"
"<p>Established 1997. Curated by hand. No images on this site, on principle,\n"
"and also because the image directory filled the disk in 2003.</p>"
"<pre>"
"        .-\"\"\"\"\"-.\n"
"      .'         '.\n"
"     /   O     O   \\\n"
"    :                :\n"
"    |                |\n"
"    :    \\      /    :\n"
"     \\    '.__.'    /\n"
"      '.          .'\n"
"        '-......-'\n"
"\n"
"   the face of a machine that has\n"
"   been up for four hundred days"
"</pre>"
"<ul>"
"<li><a href=\"/hall\">/hall</a> -- the hall of fame</li>"
"<li>Submissions: send them to an address that has not existed since 2009</li>"
"</ul>"
"<p>This page is 100% text, which means it renders identically in a graphical\n"
"browser and at a terminal prompt. We were right about this and we will not be\n"
"letting it go.</p>"
},

{ "asciiart.nomnix.org", "10.0.2.24", "/hall",
"<h1>HALL OF FAME</h1>"
"<h2>1. The Server (submitted by kaz)</h2>"
"<pre>"
"   +----------------------+\n"
"   | [ ]  [ ]  [ ]  [ ]   |\n"
"   |  o    o    o    o    |\n"
"   +----------------------+\n"
"   | [ ]  [ ]  [ ]  [ ]   |\n"
"   |  o    o    o    .    |  <- this one\n"
"   +----------------------+\n"
"\n"
"   the light that has been off for two years\n"
"   and which nobody will admit to knowing about"
"</pre>"
"<h2>2. The Backup (submitted anonymously)</h2>"
"<pre>"
"    ___________\n"
"   |           |\n"
"   |           |\n"
"   |           |\n"
"   |___________|\n"
"\n"
"   (empty)"
"</pre>"
"<h2>3. The Dangling Symlink (submitted by nomowner)</h2>"
"<pre>"
"   /boot/vmnomuz ----------> ?\n"
"\n"
"   ls sees the arrow.\n"
"   stat follows the arrow.\n"
"   Only one of them is your friend."
"</pre>"
"<h2>4. Untitled (rejected, kept anyway)</h2>"
"<pre>"
"   :(\n"
"\n"
"   submitted at 04:12 with the message\n"
"   \"i'll do a better one tomorrow\""
"</pre>"
},

{ "bugs.nomnix.org", "10.0.2.25", "/",
"<h1>NomnixOS bug tracker</h1>"
"<pre>"
"ID     STATUS      SUMMARY\n"
"4821   WONTFIX     pkg reinstall refuses to overwrite my config\n"
"4788   NEEDINFO    \"it doesn't work\" (reporter last seen 2019)\n"
"4702   DUPLICATE   duplicate of 4701\n"
"4701   DUPLICATE   duplicate of 4702\n"
"4655   FIXED       typo in the manual page for fsck\n"
"4610   WORKSFORME  boot hangs (reporter fixed it, did not say how)\n"
"4590   OPEN        the boot log is too useful, users read it and then\n"
"                   contact us with facts\n"
"4501   WONTFIX     please make ls follow symlinks by default"
"</pre>"
"<ul>"
"<li><a href=\"/4821\">4821</a> -- the famous one</li>"
"</ul>"
},

{ "bugs.nomnix.org", "10.0.2.25", "/4821",
"<h1>Bug 4821 -- pkg reinstall refuses to overwrite my config</h1>"
"<p><b>Status:</b> RESOLVED WONTFIX. <b>Severity:</b> reporter says critical.</p>"
"<h2>Reported</h2>"
"<p>I ran <i>pkg reinstall netcfg</i> to fix my machine and it printed a list\n"
"of config files it was NOT replacing because I had edited them. I want it to\n"
"replace them. That is what reinstall means.</p>"
"<h2>Comment 1 -- maintainer</h2>"
"<p>It means that when you say so: <i>pkg reinstall --force netcfg</i> replaces\n"
"them and leaves a .pkgsave copy of what was there, which is the only undo\n"
"there is. The default is to keep your edits because a package ships a default\n"
"and an administrator makes a decision, and we cannot tell which of your edits\n"
"was the decision that keeps the company running.</p>"
"<h2>Comment 2 -- reporter</h2>"
"<p>Then it should ask.</p>"
"<h2>Comment 3 -- maintainer</h2>"
"<p>It does. It prints the exact list of files it left alone, which is the\n"
"question. <i>pkg diff</i> on any of them shows what your file says against\n"
"what shipped. If the diff reads like a decision, it is not the fault.</p>"
"<h2>Comment 4 -- reporter</h2>"
"<p>I forced it and now the machine is worse.</p>"
"<h2>Comment 5 -- maintainer</h2>"
"<p>The .pkgsave copies are next to the originals. Marking WONTFIX, with\n"
"sympathy.</p>"
},

{ "usenet.nomnix.org", "10.0.2.26", "/",
"<h1>comp.os.nomnix -- archive</h1>"
"<p>Read-only. The server that fed this has been decommissioned twice and is\n"
"still up.</p>"
"<ul>"
"<li><a href=\"/editors\">the editor thread</a> -- 1,204 articles, ongoing</li>"
"<li>ANNOUNCE: NomnixOS 2.0 released (1998)</li>"
"<li>Re: Re: Re: Re: [OT] Re: was: (no subject)</li>"
"<li>Anyone got a spare SCSI terminator</li>"
"</ul>"
},

{ "usenet.nomnix.org", "10.0.2.26", "/editors",
"<h1>comp.os.nomnix -- \"which editor\"</h1>"
"<pre>"
"From: rj@corvid\n"
"Subject: which editor\n"
"\n"
"New to this system. Which editor should I learn?\n"
"\n"
"---\n"
"\n"
"From: kaz\n"
"Subject: Re: which editor\n"
"\n"
"There isn't one. You have `sed -i` and `echo >>`, and for anything\n"
"structural, `pkg diff` to see what a file should say and `pkg\n"
"reinstall` to put it back. It sounds worse than it is: you cannot\n"
"lose a file in a modal editor you did not understand.\n"
"\n"
"---\n"
"\n"
"From: (long signature, 40 lines)\n"
"Subject: Re: which editor\n"
"\n"
"This is why nobody uses this system.\n"
"\n"
"---\n"
"\n"
"From: kaz\n"
"Subject: Re: which editor\n"
"\n"
"Four thousand of them use it. They just aren't posting, because the\n"
"machines are up.\n"
"\n"
"---\n"
"\n"
"From: rj@corvid\n"
"Subject: Re: which editor\n"
"\n"
"sed -i \"s|enabled: yes|enabled: no|\" /etc/services.d/httpd.svc\n"
"\n"
"worked first time. I retract the question.\n"
"\n"
"[1,199 more articles in this thread]"
"</pre>"
},

/* ------------------------------------------------------------------ *
 * The old web: personal pages, a webring, a guestbook nobody moderates,
 * and the download site where nothing downloads.
 * ------------------------------------------------------------------ */

{ "www.tripodal.net", "10.0.3.10", "/",
"<h1>TRIPODAL -- free homepages for everyone!</h1>"
"<img src=\"tripodal_logo.gif\" alt=\"TRIPODAL! 5 MEGABYTES FREE!\">"
"<p>Build your own page today! 5 MB of space! Free counter! Free guestbook!\n"
"Your own address at www.tripodal.net/~yourname!</p>"
"<ul>"
"<li><a href=\"/~zorak\">/~zorak</a> -- ZORAK'S COMPUTER REALM</li>"
"<li><a href=\"/guestbook.cgi\">/guestbook.cgi</a> -- sign the global guestbook</li>"
"<li>Member directory (down)</li>"
"<li>Search member pages (down since the search box was removed)</li>"
"</ul>"
"<hr>"
"<p>New signups are closed. Existing pages are preserved as a service to the\n"
"community and because the person who knows how to switch the server off has\n"
"not worked here for some time.</p>"
},

{ "www.tripodal.net", "10.0.3.10", "/~zorak",
"<h1>*** ZORAK'S COMPUTER REALM ***</h1>"
"<img src=\"construction_barrier.gif\" alt=\"UNDER CONSTRUCTION! CHECK BACK SOON!\">"
"<p><b>WELCOME</b> to my page!!! I am Zorak (not my real name) and I do the\n"
"computers at my work. This page is about COMPUTERS and my SETUP and things I\n"
"have learned.</p>"
"<h2>MY SETUP</h2>"
"<pre>"
"Tower       beige, screwdriver required, one screw missing\n"
"RAM         enough (upgraded twice, second one did nothing)\n"
"Disk        two, one is a mystery, I don't unplug it\n"
"Monitor     17\" (SEVENTEEN)\n"
"OS          NomnixOS, boots in seven stages, I can name all of them\n"
"Cooling     the window"
"</pre>"
"<h2>THINGS I HAVE LEARNED</h2>"
"<ul>"
"<li>If ls says a file is there and the program says it is not, it is a\n"
"symlink and the thing it points at is gone. <b>stat</b> tells you. This took\n"
"me a WHOLE WEEKEND to learn so I am putting it on my page.</li>"
"<li>The log is written while the machine boots, so a boot that FAILED still\n"
"has a log. <b>dmesg</b>.</li>"
"<li>Do not tidy /boot.</li>"
"<li>Do not tidy anything.</li>"
"</ul>"
"<h2>LINKS</h2>"
"<ul>"
"<li><a href=\"/~zorak/links\">MY LINKS PAGE</a> (80+ links!!)</li>"
"<li><a href=\"ring.webring.org\">THE SYSADMIN WEBRING</a> [prev] [next] [random]</li>"
"<li><a href=\"/guestbook.cgi\">SIGN MY GUESTBOOK</a></li>"
"</ul>"
"<hr>"
"<pre>"
"    +-----------------------------+\n"
"    |  YOU ARE VISITOR NUMBER     |\n"
"    |   0 0 0 1 2 4 7 3           |\n"
"    +-----------------------------+"
"</pre>"
"<p>The counter is a CGI script. It has counted my own visits since 1998 and I\n"
"visit every day to check the counter.</p>"
"<p><i>Last updated: 11 Nov 2001. Best viewed at 1024x768. Made on a\n"
"computer.</i></p>"
},

{ "www.tripodal.net", "10.0.3.10", "/~zorak/links",
"<h1>ZORAK'S LINKS PAGE</h1>"
"<p>Hand checked! (last checked 2002)</p>"
"<h2>COMPUTERS</h2>"
"<ul>"
"<li><a href=\"wiki.nomnix.org\">The NomnixOS wiki</a> -- THE GOOD ONE, read\n"
"the boot page first</li>"
"<li><a href=\"kaznotes.net\">KAZNOTES</a> -- this guy knows what he is talking\n"
"about, I have read the one about the two libraries FOUR TIMES</li>"
"<li><a href=\"bofh.nomnix.org/excuse\">The excuse list</a> -- HILARIOUS</li>"
"<li><a href=\"asciiart.nomnix.org\">ASCII art archive</a> -- art without\n"
"downloading!</li>"
"<li><a href=\"rfc.nomnix.org\">RFCs</a> -- the standards, some are jokes,\n"
"good luck</li>"
"</ul>"
"<h2>DOWNLOADS</h2>"
"<ul>"
"<li><a href=\"shareware.zipdrive.net\">ZIPDRIVE SHAREWARE ARCHIVE</a> -- 40,000\n"
"files</li>"
"</ul>"
"<h2>HELP AND SUPPORT</h2>"
"<ul>"
"<li><a href=\"forums.nomnix.org/t8891\">That thread about the initrd</a> --\n"
"read to page 7, the answer is on page 7</li>"
"<li><a href=\"stackunderflow.com\">StackUnderflow</a> -- your question is a\n"
"duplicate</li>"
"<li><a href=\"support.zephyrsys.com\">Zephyr Systems support</a> -- they will\n"
"tell you to reinstall</li>"
"</ul>"
"<h2>NOT COMPUTERS</h2>"
"<ul>"
"<li>My friend's band (link broken)</li>"
"<li>Cool site of the day (site of the day is from 1999)</li>"
"<li>A page about trains (this is the best link on this page)</li>"
"</ul>"
},

{ "www.tripodal.net", "10.0.3.10", "/guestbook.cgi",
"<h1>*** SIGN MY GUESTBOOK ***</h1>"
"<p>[ name ] [ email ] [ message ] [ SUBMIT ] -- the form posts to a script\n"
"that was disabled in 2004, so nothing you type here goes anywhere. The\n"
"entries below arrived before that.</p>"
"<hr>"
"<h2>1998-03-02 -- Dave</h2>"
"<p>Cool page man. First!</p>"
"<h2>1998-03-04 -- kaz</h2>"
"<p>Good page. The bit about symlinks is correct and most people take a lot\n"
"longer than a weekend.</p>"
"<h2>1999-07-19 -- Zorak</h2>"
"<p>testing the guestbook</p>"
"<h2>1999-07-19 -- Zorak</h2>"
"<p>testing again</p>"
"<h2>2001-01-04 -- a visitor</h2>"
"<p>please remove my email address from this page</p>"
"<h2>2002-11-30 -- CHEAP MEDS ONLINE!!!</h2>"
"<p>CHEAP M3DS N0 PRESCRIPTI0N CLICK HERE C.L.I.C.K H.E.R.E</p>"
"<h2>2003-01-02 -- WIN A FREE LAPTOP</h2>"
"<p>YOU HAVE BEEN SELECTED CLICK NOW</p>"
"<h2>2003-01-02 -- WIN A FREE LAPTOP</h2>"
"<p>YOU HAVE BEEN SELECTED CLICK NOW</p>"
"<h2>2003-01-02 -- WIN A FREE LAPTOP</h2>"
"<p>YOU HAVE BEEN SELECTED CLICK NOW</p>"
"<h2>2003-06-11 -- Zorak</h2>"
"<p>I have removed the spam. If it comes back I will remove it again.</p>"
"<h2>2003-06-12 -- CHEAP MEDS ONLINE!!!</h2>"
"<p>CHEAP M3DS N0 PRESCRIPTI0N</p>"
"<h2>2004-02-08 -- Zorak</h2>"
"<p>Guestbook closed. It was nice while it lasted. Nine of the last four\n"
"hundred entries were from people.</p>"
},

{ "ring.webring.org", "10.0.3.11", "/",
"<h1>THE SYSADMIN WEBRING</h1>"
"<img src=\"ring_banner.gif\" alt=\"[ prev ] [ next ] [ random ] [ list all ] -- a ring of 40 sites\">"
"<p>A webring is a circle of pages that link to each other, so that a visitor\n"
"can go round and round forever. This ring has 40 members. Six of them still\n"
"resolve. Following [next] from member 7 has landed on a domain-parking page\n"
"since 2006, which breaks the circle, which means the ring is now a line.</p>"
"<h2>Members</h2>"
"<ul>"
"<li>1. <a href=\"www.tripodal.net/~zorak\">ZORAK'S COMPUTER REALM</a> -- active</li>"
"<li>2. \"The Cable Management Page\" -- gone</li>"
"<li>3. <a href=\"asciiart.nomnix.org\">The ASCII Art Archive</a> -- active</li>"
"<li>4. \"Tape Rotation For The Small Office\" -- gone</li>"
"<li>5. <a href=\"bofh.nomnix.org\">The BOFH page</a> -- active, unrepentant</li>"
"<li>6. \"My Server Room Photos\" -- gone (the photos were the whole site)</li>"
"<li>7. \"UPS Battery Diary\" -- <b>parked domain, ring broken here</b></li>"
"<li>8-39. no response</li>"
"<li>40. <a href=\"home.internal/~rkeeler\">Rob's Homepage</a> -- Rob is in\n"
"Facilities and joined by mistake, and is the most reliable member</li>"
"</ul>"
"<hr>"
"<p>Ringmaster: unreachable. To leave the ring, email the ringmaster.</p>"
},

{ "shareware.zipdrive.net", "10.0.3.12", "/",
"<h1>ZIPDRIVE SHAREWARE ARCHIVE</h1>"
"<p>Over 40,000 files! All scanned! All free to try!</p>"
"<pre>"
"CATEGORY              FILES    LAST ADDED\n"
"Utilities              8,102   1999\n"
"Screen savers          6,551   1999\n"
"Games (demo)           4,220   1998\n"
"Sound & MIDI           3,891   1998\n"
"Networking             1,004   2000\n"
"Business                 612   1997\n"
"Anti-virus                44   1997 (do not use these)"
"</pre>"
"<ul>"
"<li><a href=\"/d/nomzip\">NOMZIP 3.1 -- the compression tool everyone had</a></li>"
"</ul>"
"<p>Every download link on this archive points at an FTP server that was\n"
"switched off before most of the files were uploaded. The catalogue is\n"
"complete, searchable, well organised, and entirely theoretical.</p>"
},

{ "shareware.zipdrive.net", "10.0.3.12", "/d/nomzip",
"<h1>NOMZIP 3.1</h1>"
"<pre>"
"File:      nomzip31.zip\n"
"Size:      412 KB\n"
"Uploaded:  14 Aug 1998\n"
"Downloads: 1,204,552\n"
"Rating:    ****- (4.2)"
"</pre>"
"<p>The classic archiver. Unregistered copies add a five second delay and a\n"
"reminder. Register for $29 and receive a code by post.</p>"
"<h2>Download</h2>"
"<p><b>Your download will begin shortly.</b></p>"
"<p>If your download does not begin, try mirror 2. If mirror 2 does not\n"
"respond, try mirror 3. If mirror 3 asks for a password, that is mirror 3's\n"
"way of saying it is mirror 3.</p>"
"<p><i>Your download will begin shortly.</i></p>"
"<h2>Reviews</h2>"
"<ul>"
"<li>\"does what it says\" -- 5 stars</li>"
"<li>\"the nag screen is fine, 5 seconds is 5 seconds\" -- 4 stars</li>"
"<li>\"could not download\" -- 1 star</li>"
"<li>\"could not download\" -- 1 star</li>"
"<li>\"could not download\" -- 1 star</li>"
"<li>\"registered by post in 1998, still waiting for the code\" -- 3 stars,\n"
"which is generous</li>"
"</ul>"
"<p>On this machine you do not need it: the system compresses what it writes\n"
"by itself, which is why some programs need libz and others do not, and why\n"
"<b>ldd</b> on two different programs shows two different lists.</p>"
},

{ "support.zephyrsys.com", "10.0.3.13", "/",
"<h1>Zephyr Systems -- Customer Support Portal</h1>"
"<p>Welcome to the Zephyr Systems Support Portal. Your satisfaction is our\n"
"priority. Please select your product from a list that does not include your\n"
"product.</p>"
"<ul>"
"<li><a href=\"/kb1041\">KB1041 -- Troubleshooting: general</a></li>"
"<li><a href=\"/kb2207\">KB2207 -- Improving system startup time</a></li>"
"<li><a href=\"/kb0088\">KB0088 -- Understanding disk identifiers</a></li>"
"<li><a href=\"/downloads\">Drivers and downloads</a></li>"
"<li>Open a case (requires a support contract)</li>"
"<li>Check contract status (requires a case)</li>"
"</ul>"
"<hr>"
"<p><i>Zephyr Systems is a wholly owned subsidiary of a company that was\n"
"dissolved. This portal is maintained under the terms of an agreement nobody\n"
"has been able to produce.</i></p>"
"<p>If a Zephyr agent is installed on your machine, it will have dropped a\n"
"unit file into /etc/services.d. <b>pkg owns</b> that file and it will tell you\n"
"that nothing owns it, which is the shortest true sentence about this\n"
"company.</p>"
},

{ "support.zephyrsys.com", "10.0.3.13", "/kb1041",
"<h1>KB1041 -- Troubleshooting: general</h1>"
"<p><b>Applies to:</b> all products. <b>Last reviewed:</b> never.</p>"
"<h2>Symptom</h2>"
"<p>The product does not work.</p>"
"<h2>Resolution</h2>"
"<ul>"
"<li>Step 1. Reinstall the product.</li>"
"<li>Step 2. Reinstall the operating system.</li>"
"<li>Step 3. Reinstall the product.</li>"
"<li>Step 4. If the issue persists, contact your account manager. Zephyr\n"
"Systems no longer has account managers.</li>"
"</ul>"
"<h2>Additional information</h2>"
"<p>Do not send us logs. Logs cannot be accepted by the portal.</p>"
"<hr>"
"<p><i>Was this article helpful?</i> [ yes ] [ no ] -- 4% of 61,220 readers\n"
"found this article helpful.</p>"
"<h2>Note added by somebody else</h2>"
"<p>Whoever you are: on this operating system you almost never need step 2.\n"
"<b>pkg verify</b> names the files that no longer match what shipped, and\n"
"<b>pkg reinstall &lt;name&gt;</b> puts back one package, keeping the config you\n"
"edited. Reinstalling the whole machine destroys the evidence and the fault\n"
"comes back, because the fault was a decision somebody made and it is written\n"
"down in a file you just overwrote.</p>"
},

{ "support.zephyrsys.com", "10.0.3.13", "/downloads",
"<h1>Drivers and downloads</h1>"
"<pre>"
"zephyr-agent-2.1.exe        12 MB   for an operating system you do not run\n"
"zephyr-agent-2.0.exe        12 MB   superseded\n"
"zephyr-diag-tool.exe         4 MB   requires the agent\n"
"zephyr-agent-uninstall       --     not available\n"
"release-notes.txt            2 KB   \"see release notes\""
"</pre>"
"<p>Every file above is a Windows executable. This machine runs NomnixOS on a\n"
"64-bit RISC processor and could not execute one of them if it wanted to: the\n"
"loader reads the header, sees something that is not a program for this\n"
"machine, and refuses. That refusal is the same one you get from a package\n"
"built for the wrong architecture, and it is worth recognising.</p>"
"<p>The agent everybody actually has was not downloaded from here. It arrived\n"
"with something else, it is not owned by any package, and it writes a ticket\n"
"every night. See <a href=\"helpdesk.internal\">ticket 4459</a>.</p>"
},

{ "stackunderflow.com", "10.0.3.14", "/",
"<h1>StackUnderflow -- where good questions go</h1>"
"<pre>"
"HOT QUESTIONS\n"
"[closed]     How do I read a boot log?\n"
"[duplicate]  How do I read a boot log? (2)\n"
"[on hold]    How do I read a boot log? (3)\n"
"[closed]     Why does my question keep getting closed?\n"
"[answered]   How do I read a boot log? -- 1 upvote, accepted, 2011"
"</pre>"
"<ul>"
"<li><a href=\"/q213377\">Q213377 -- compiler says \"undefined reference\", what\n"
"does that mean</a></li>"
"</ul>"
"<p>Ask a question. Someone will tell you it has been asked. It has been\n"
"asked. The answer to the other one is wrong.</p>"
},

{ "stackunderflow.com", "10.0.3.14", "/q213377",
"<h1>Q213377: compiler says \"undefined reference\", what does that mean</h1>"
"<p><b>closed as duplicate</b> of a question with no answers.</p>"
"<h2>Question -- asked 9 years ago, viewed 2.1M times</h2>"
"<p>I get this when I build:</p>"
"<pre>"
"ld: undefined reference to `zthing_open'\n"
"collect2: error: ld returned 1 exit status"
"</pre>"
"<p>What is wrong with my code?</p>"
"<h2>Comment</h2>"
"<p>What have you tried?</p>"
"<h2>Comment</h2>"
"<p>This is not a real question.</p>"
"<h2>Comment</h2>"
"<p>Please post a minimal reproducible example, the exact command line, your\n"
"operating system, your compiler version, your birth certificate.</p>"
"<h2>Answer -- 412 upvotes, not accepted</h2>"
"<p>Nothing is wrong with your code. The linker cannot find the library that\n"
"defines the symbol. It is a different problem from a compile error and it\n"
"happens after your code is already correct.</p>"
"<p>The same distinction exists at RUN time, and that one bites people on\n"
"working machines: a program that built fine will not start if the library it\n"
"declared is missing, too old, or sitting in a directory the loader was never\n"
"told about. On NomnixOS, <b>ldd</b> on the program shows what it needs, where\n"
"each one was found, and whether the one that was found is new enough. A\n"
"library that is present but unlisted in /etc/ld.so.conf shows as <i>not\n"
"found</i>, which is the fault stated in plain words.</p>"
"<h2>Answer -- 0 upvotes, accepted</h2>"
"<p>try turning it off and on again</p>"
},

{ "you-have-won.example.biz", "10.0.3.15", "/",
"<h1>!!! WARNING !!! YOUR COMPUTER MAY BE INFECTED !!!</h1>"
"<img src=\"scan.gif\" alt=\"ANIMATED SCANNING BAR -- 98% -- 4 VIRUSES FOUND\">"
"<p><b>CRITICAL ALERT:</b> Your system has been scanned remotely by a page that\n"
"cannot scan anything and has found <b>4 VIRUSES</b>, <b>1,204 ERRORS</b> and\n"
"<b>3 CORRUPT REGISTRY ENTRIES</b> on an operating system that does not have a\n"
"registry.</p>"
"<p>CLICK HERE to download NomShield Pro. CLICK HERE. CLICK ANYWHERE. THE\n"
"WHOLE PAGE IS A BUTTON.</p>"
"<hr>"
"<h2>Also you are the 1,000,000th visitor</h2>"
"<p>You have won a prize. To claim your prize, enter your details, your\n"
"colleague's details, and the root password of a machine you administer.</p>"
"<hr>"
"<h2>What is actually true here</h2>"
"<p>Nothing. A web page cannot see your disk. What CAN tell you whether\n"
"anything on this machine has been tampered with is the machine itself:\n"
"<b>pkg verify</b> hashes every file a package owns against what shipped and\n"
"names anything that differs, and <b>pkg owns</b> on a suspicious file tells\n"
"you whether any package claims it at all. A file nothing owns is not proof of\n"
"a virus -- it is usually a vendor agent, which is worse, because the vendor\n"
"answers the phone even less often than a virus does.</p>"
},

{ "altavistula.com", "10.0.3.16", "/",
"<h1>AltaVistula -- Search The Entire Web</h1>"
"<p>[ search the web ] [ search this site ] [ I'm Feeling Underpowered ]</p>"
"<p>The search box on this page is an image of a search box. The real one was\n"
"removed during a redesign and the redesign was cancelled.</p>"
"<h2>Directory</h2>"
"<ul>"
"<li>Computers &gt; Operating Systems &gt; NomnixOS (3 sites)</li>"
"<li>Computers &gt; Humour &gt; Sysadmin (1 site, it is the BOFH page)</li>"
"<li>Computers &gt; Personal Pages &gt; Under Construction (11,402 sites)</li>"
"<li>Business &gt; Logistics &gt; Corvid Logistics (1 site, out of date)</li>"
"</ul>"
"<ul>"
"<li><a href=\"/results\">Today's popular search: \"machine wont boot\"</a></li>"
"</ul>"
},

{ "altavistula.com", "10.0.3.16", "/results",
"<h1>Results 1-8 of about 4,120,000 for: machine wont boot</h1>"
"<ul>"
"<li><a href=\"support.zephyrsys.com/kb1041\">Reinstall the operating\n"
"system</a> -- sponsored result</li>"
"<li><a href=\"support.zephyrsys.com/kb1041\">Reinstall the operating\n"
"system</a> -- sponsored result</li>"
"<li>MACHINE WONT BOOT?? CLICK HERE -- sponsored result</li>"
"<li><a href=\"forums.nomnix.org/t8891\">machine hangs at initrd, waiting for\n"
"root -- 7 pages</a></li>"
"<li>machine wont boot - Yahoo Answers style site (best answer: \"it's\n"
"broken\")</li>"
"<li><a href=\"wiki.nomnix.org/boot\">How NomnixOS boots</a> -- result 6, which\n"
"is where the answer actually is</li>"
"<li>Buy machine wont boot at auction -- 0 items</li>"
"<li><a href=\"you-have-won.example.biz\">YOUR COMPUTER MAY BE INFECTED</a> --\n"
"do not</li>"
"</ul>"
"<p>Nobody clicks result 6.</p>"
},

/* ------------------------------------------------------------------ *
 * More of the company. Machines with web pages of their own, which is
 * the 1990s corporate intranet in one sentence.
 * ------------------------------------------------------------------ */

{ "printer.internal", "10.0.2.63", "/",
"<h1>CorvidJet 4200 -- Embedded Web Server</h1>"
"<pre>"
"STATUS:      READY\n"
"TRAY 1:      LETTER  (empty)\n"
"TRAY 2:      A4      (empty, sensor faulty, reports empty always)\n"
"TRAY 3:      A4      (full, disabled by a previous administrator)\n"
"TONER:       ####------  38%\n"
"DRUM:        REPLACE SOON (since 2019)\n"
"QUEUE:       0 jobs\n"
"UPTIME:      1,402 days\n"
"FIRMWARE:    1.02 (latest is 3.40, upgrade requires a floppy)"
"</pre>"
"<h2>Message</h2>"
"<p><b>PC LOAD LETTER</b></p>"
"<h2>Configuration</h2>"
"<ul>"
"<li>Change settings -- requires the administrator password</li>"
"<li>The administrator password was set in 2011</li>"
"<li>Password reset -- requires a paperclip and physical access</li>"
"<li>The paperclip hole is behind the printer and the printer is against the\n"
"wall</li>"
"</ul>"
"<p>This page is served by the printer itself, which has been up longer than\n"
"anything else in the building. Whatever is wrong with printing, it is not\n"
"this. See <a href=\"notices.internal/printer\">notices.internal/printer</a>,\n"
"all eleven versions.</p>"
},

{ "webmail.internal", "10.0.2.64", "/",
"<h1>Corvid Webmail</h1>"
"<p><b>This application requires a browser with JavaScript, frames, and a\n"
"plugin that was discontinued.</b></p>"
"<p>You are using a text browser. That is not a supported configuration. It is,\n"
"however, the configuration that can read this sentence, which the supported\n"
"one cannot, because the supported one shows a spinner.</p>"
"<h2>Alternative access</h2>"
"<p>Mail is delivered by a real mail transport on the machine. If webmail is\n"
"unhappy, the useful questions are whether the daemon is running and whether\n"
"anything is listening:</p>"
"<pre>"
"svc                  is the mail unit running or DEAD\n"
"svc status postfix   why it is unhappy, and how many times\n"
"netstat              what is actually listening, read from /proc"
"</pre>"
"<p>A mail daemon that is running but not listening on the port everybody\n"
"expects has usually been given a config nobody reloaded. /run/&lt;name&gt;.state\n"
"is what it really loaded, and <b>svc reload postfix</b> is what makes it\n"
"read the file again without dropping the queue on the floor.</p>"
},

{ "y2k.internal", "10.0.2.65", "/",
"<h1>YEAR 2000 READINESS PROGRAMME</h1>"
"<p><b>Status: ON TRACK. Days remaining: -9,700.</b></p>"
"<p>This page was last edited on 30 December 1999 and has been served\n"
"continuously ever since by a machine nobody can find. It is not in the server\n"
"room. It answers on 10.0.2.65 and it has answered every single day for a\n"
"quarter of a century.</p>"
"<h2>Remediation checklist</h2>"
"<ul>"
"<li>Inventory all systems -- COMPLETE</li>"
"<li>Assess date handling -- COMPLETE</li>"
"<li>Remediate -- COMPLETE</li>"
"<li>Test -- SCHEDULED FOR JANUARY</li>"
"<li>Decommission this page -- ASSIGNED TO NOMOWNER</li>"
"</ul>"
"<h2>Guidance to staff</h2>"
"<p>On 1 January, do not switch anything on until IT has confirmed it is\n"
"safe. If a machine shows an incorrect date, do not panic; a clock that is\n"
"wrong is a service problem and not a data problem. The time daemon exists for\n"
"this and <b>svc</b> will tell you whether it is running.</p>"
"<p>See ticket 4462: the handheld in goods-in says 1970. It is 25 years later\n"
"and this page is still the most relevant document in the building.</p>"
},

{ "coffee.internal", "10.0.2.67", "/",
"<h1>SECOND FLOOR KITCHEN -- LIVE CAMERA</h1>"
"<img src=\"pot.jpg\" alt=\"greyscale image of a coffee pot, 128x128, refreshes every 20 seconds\">"
"<pre>"
"POT:        present\n"
"LEVEL:      approximately one cup\n"
"AGE:        4 hours 20 minutes\n"
"HOTPLATE:   on\n"
"VERDICT:    technically coffee"
"</pre>"
"<p>The camera is a real camera pointed at a real pot and it is the single\n"
"most trusted monitoring system in this company. When the status board says\n"
"ALL SYSTEMS OPERATIONAL, people check this page instead, because this page\n"
"has never once been wrong about anything.</p>"
"<p>Requests to brew coffee are refused by this server, which is a teapot in\n"
"every sense that matters.</p>"
"<hr>"
"<p>Camera installed by nomowner \"as a test of the video service\". The video\n"
"service was decommissioned. The camera was not. It has outlived the project,\n"
"the department, and the person.</p>"
},

/* ------------------------------------------------------------------ *
 * THE MAILING LIST. A thread that runs for seventeen messages and is
 * solved on the fourteenth, which is what mailing lists are like. Every
 * command in it is a command this machine has, and the answer -- `df -i`
 * -- is a real fault in the catalogue that `df` alone cannot see.
 * ------------------------------------------------------------------ */

{ "lists.nomnix.org", "10.0.2.28", "/",
"<h1>nomnix-users -- archive</h1>"
"<p>The list has been running since before the wiki and it is where the people\n"
"who actually run these machines are. The signal is high and the threads are\n"
"long, and those two facts are the same fact.</p>"
"<pre>"
"LIST            POSTS/MONTH   NOTE\n"
"nomnix-users            210   this one\n"
"nomnix-devel             40   patches, mostly to the loader\n"
"nomnix-announce           1   releases. One post a year. It is enough.\n"
"halyard-project           0   see oldwiki.internal"
"</pre>"
"<h2>Recent threads</h2>"
"<ul>"
"<li><a href=\"/inodes\">df says 48% and I cannot create a file</a> -- 17\n"
"messages, and it is solved on message 14</li>"
"<li>[ANNOUNCE] 11.4 -- 1 message, no replies, correct</li>"
"<li>Re: Re: Re: Re: unsubscribe -- 61 messages</li>"
"<li>Why does my crontab not run (it runs)</li>"
"<li>OT: what chair -- 340 messages, locked</li>"
"</ul>"
"<p>To unsubscribe, follow the instructions in the footer of any message. The\n"
"footer was removed in 2011.</p>"
},

{ "lists.nomnix.org", "10.0.2.28", "/inodes",
"<h1>[nomnix-users] df says 48% and I cannot create a file (1-9 of 17)</h1>"
"<h2>1. mfaraday</h2>"
"<p>Box has been up for months. This morning nothing can write. Every daemon\n"
"that keeps state is dead, the shell cannot make a file in /tmp, and:</p>"
"<pre>"
"# df\n"
"FILESYSTEM       SIZE     USED    AVAIL  USE%\n"
"/dev/sda1     1085K   520K   565K   48%"
"</pre>"
"<p>Half the disk is free. What am I looking at?</p>"
"<h2>2. lurker_9</h2>"
"<p>df is wrong, reboot it</p>"
"<h2>3. mfaraday</h2>"
"<p>I would rather understand it than reboot it, and if I reboot it I will\n"
"never find out.</p>"
"<h2>4. tolliver</h2>"
"<p>Is the root mounted read-only? That looks exactly like this. <b>mount</b>\n"
"with no arguments, and look at the options on /.</p>"
"<h2>5. mfaraday</h2>"
"<p>Good thought. It is rw. Every existing file is writable -- I can append to\n"
"/etc/hosts perfectly happily with echo &gt;&gt;. I just cannot CREATE one.</p>"
"<h2>6. tolliver</h2>"
"<p>Then it is the directory, not the filesystem. Creating a file is a write\n"
"to the DIRECTORY. <i>ls -ld /tmp</i>.</p>"
"<h2>7. mfaraday</h2>"
"<p>0777, same as it shipped. And it is not just /tmp -- it is everywhere. I\n"
"cannot create a file in /root either and that is 0755 and I am root.</p>"
"<h2>8. lurker_9</h2>"
"<p>reinstall</p>"
"<h2>9. brakeman88</h2>"
"<p>I had something like this and it was the SATA cable</p>"
"<p><a href=\"/inodes2\">messages 10-17</a></p>"
},

{ "lists.nomnix.org", "10.0.2.28", "/inodes2",
"<h1>[nomnix-users] df says 48% and I cannot create a file (10-17 of 17)</h1>"
"<h2>10. mfaraday</h2>"
"<p>pkg verify over the whole machine. Three CHANGED files, all three are\n"
"local edits from the change log, all three are deliberate. The machine is, as\n"
"far as the package database is concerned, perfect.</p>"
"<h2>11. tolliver</h2>"
"<p>Which is worth saying out loud: a clean verify does not mean a healthy\n"
"machine. It means nothing has been DAMAGED. Full disks, bad mounts, wrong\n"
"permissions on directories nobody owns, the boot sector -- none of that is a\n"
"file, so none of it is in a manifest.</p>"
"<h2>12. pyxis</h2>"
"<p>Run <b>nomctl fsck --repair</b>.</p>"
"<h2>13. kaz</h2>"
"<p>There is no nomctl and there never has been. Please stop.</p>"
"<h2>14. kaz</h2>"
"<p>mfaraday: run this and paste it.</p>"
"<pre>df -i</pre>"
"<p>A filesystem has two budgets and they run out independently. <b>df</b>\n"
"counts BYTES. <b>df -i</b> counts INODES -- one per file, one per directory,\n"
"and an empty file costs an inode and no bytes worth mentioning. You can have\n"
"half a megabyte free and no way to record that another file exists, and\n"
"plain df will swear blind there is plenty of room for ever, because from\n"
"where df is standing there is.</p>"
"<p>It is always the same cause: something that writes a file per run and\n"
"never tidies up. Find the directory rather than the file, because no\n"
"individual file will look wrong:</p>"
"<pre>"
"df -i\n"
"find /var -type f\n"
"du -s /var /usr /tmp /home"
"</pre>"
"<h2>15. mfaraday</h2>"
"<pre>"
"# df -i\n"
"FILESYSTEM      INODES     IUSED     IFREE  IUSE%\n"
"/dev/sda1     761      761      0      100%"
"</pre>"
"<p>Four hundred and something files in a cache directory, one per run,\n"
"going back to the summer. Deleted them, everything came back, nothing had\n"
"ever been broken. Thank you.</p>"
"<h2>16. kaz</h2>"
"<p>For the archive, because this thread will be found by somebody at three in\n"
"the morning: <b>df</b> and <b>df -i</b> are two different questions and the\n"
"second one is free. Ask it first, not fourteenth.</p>"
"<h2>17. lurker_9</h2>"
"<p>still would have rebooted</p>"
"<p><a href=\"/inodes\">messages 1-9</a></p>"
},

/* ------------------------------------------------------------------ *
 * kaz, who has answered every question on the forums for a decade and
 * has a page of his own. Everything here is true of this machine and
 * was checked against one; the point of the page is that it is the same
 * advice as the wiki in the voice of somebody who has been burned.
 * ------------------------------------------------------------------ */

{ "kaznotes.net", "10.0.3.17", "/",
"<h1>kaznotes -- notes from someone who runs one of these</h1>"
"<p>I am the person who answers on the forums. This is where the long ones go,\n"
"because a forum post scrolls away and a page does not.</p>"
"<p>One NomnixOS box, in a cupboard, since 11.0. It has never been reinstalled\n"
"and I would like that to remain true.</p>"
"<ul>"
"<li><a href=\"/two-libraries\">/two-libraries</a> -- nothing missing, nothing\n"
"corrupt, and the web server will not start</li>"
"<li><a href=\"/mode\">/mode</a> -- the one word in pkg verify that IS the\n"
"diagnosis</li>"
"<li><a href=\"/juniors\">/juniors</a> -- the order I make people work in, and\n"
"why it is that order</li>"
"<li><a href=\"/vendors\">/vendors</a> -- reading a support article without\n"
"believing it</li>"
"</ul>"
"<p>No comments. No guestbook. There was a guestbook.</p>"
},

{ "kaznotes.net", "10.0.3.17", "/two-libraries",
"<h1>nothing is missing and nothing is corrupt</h1>"
"<p>Best afternoon I have lost in years, so it goes on the record.</p>"
"<p>httpd would not start. auditd would not start. Everything else on the box\n"
"was up and cheerful: ssh, cron, udev, ntp, the firewall. pkg verify on httpd:\n"
"clean. On audit: clean. Binaries present, executable, right hash, right mode.\n"
"Config files exactly as shipped.</p>"
"<p>Two things dead and eleven fine is a QUESTION, not a coincidence. What do\n"
"those two have in common that the eleven do not?</p>"
"<pre>"
"ldd /usr/sbin/httpd\n"
"ldd /usr/sbin/sshd"
"</pre>"
"<p>httpd lists libz. sshd does not. On this system libz is needed only by the\n"
"programs that compress what they write -- httpd, auditd, postfix and links,\n"
"four binaries on the whole disk -- which is exactly why a bad libz kills two\n"
"services and leaves the rest alone, and exactly why it is so confusing when\n"
"you are staring at the two.</p>"
"<h2>and then the actual fault</h2>"
"<p>ldd does not print a verdict. It prints THE PATH IT RESOLVED TO, and this\n"
"is the whole reason:</p>"
"<pre>"
"libc.so.6 => /lib/libc.so.6 (2.38)\n"
"libz.so.1 => /usr/lib/libz.so.1 (1.2)  -- TOO OLD"
"</pre>"
"<p>/lib/libz.so.1 was there. It was 1.3. It was perfect. The loader never got\n"
"to it, because /etc/ld.so.conf listed /usr/lib first, because eighteen months\n"
"ago somebody unpacked a vendor tarball and made it work.</p>"
"<p><b>Nothing was missing and nothing was corrupt.</b> The repair was the\n"
"ORDER of two lines in one file. pkg verify flagged /etc/ld.so.conf, which\n"
"reads exactly like a deliberate local edit, because it is one -- and a vendor\n"
"path added to the END of that file is harmless and probably wanted. Same\n"
"line, same file, different position, opposite verdict.</p>"
"<p>Read the path column. It is the column people skip.</p>"
},

{ "kaznotes.net", "10.0.3.17", "/mode",
"<h1>MODE is not CHANGED</h1>"
"<p>pkg verify has a vocabulary and it is small enough to learn in a minute,\n"
"and knowing it is worth more than any amount of cleverness afterwards.</p>"
"<pre>"
"MISSING            the file is gone\n"
"MISSING (symlink)  a symlink is gone\n"
"REPOINTED          a symlink points somewhere new\n"
"CHANGED            the contents differ from what shipped\n"
"MODE               the contents are FINE and the permissions are not"
"</pre>"
"<p>MODE is the interesting one. It means nothing has been damaged. Not one\n"
"byte. Somebody -- a person, or a script written by a person -- has changed a\n"
"permission, and the repair is <b>chmod</b> and takes one second, and\n"
"reinstalling the package is an enormous overreaction that also destroys any\n"
"local config in that package on the way past.</p>"
"<h2>the shape it comes in</h2>"
"<p>A hardening script from somebody's laptop walks a directory and takes the\n"
"execute bit off everything it does not recognise. So you get MODE, and not\n"
"CHANGED, on a SCATTER of files across several packages at once. Nothing else\n"
"produces that pattern. One damaged file is one package; a bad library is\n"
"every binary at once; this is a handful of unrelated packages all saying the\n"
"same word.</p>"
"<pre>"
"pkg verify\n"
"ls /usr/sbin        the mode is the first column\n"
"chmod 755 /usr/sbin/sshd\n"
"svc start sshd      it has been dead since the boot and will stay dead\n"
"svc status sshd"
"</pre>"
"<h2>the blind spot, and it is a big one</h2>"
"<p>A package records the directories it OWNS, and verify checks those for\n"
"existence and for mode like any other line. Most directories are owned by\n"
"nobody. Change the mode of one of THOSE and verify is clean and stays clean,\n"
"while every file underneath it is perfectly intact and completely\n"
"unreachable -- because traversing a directory needs permission on the\n"
"directory, not on the files.</p>"
"<p>When a whole healthy tree has gone unreadable at once, what is wrong is\n"
"the way IN. <b>ls -ld</b> on the directory is the only thing that will say\n"
"so.</p>"
},

{ "kaznotes.net", "10.0.3.17", "/juniors",
"<h1>the order, and why it is that order</h1>"
"<p>Everybody arrives wanting to fix something. The order below is designed to\n"
"stop that for about four minutes, which is all it takes.</p>"
"<pre>"
"1  read the console      it says what it tried and what it got, and it\n"
"                         said it before you arrived\n"
"2  dmesg / dmesg -1      the same thing for a boot that already failed\n"
"3  svc                   what should be running and is not\n"
"4  df ; df -i            two questions, both free, both have been the\n"
"                         answer more than once\n"
"5  pkg verify <suspect>  ONE package, chosen from where it stopped\n"
"6  pkg diff <file>       before you touch anything\n"
"7  change one thing      and say what you changed and why"
"</pre>"
"<h2>the three rules underneath it</h2>"
"<ul>"
"<li><b>pkg verify is not a fault list.</b> Every machine a person has\n"
"administered has files that differ from what shipped, because the person\n"
"decided so. A diff that reads like a decision is not a fault. Reinstalling\n"
"everything it names is how you turn one ticket into two.</li>"
"<li><b>A clean verify does not mean a healthy machine.</b> The boot sector is\n"
"not a file. A full disk is not a file. A wrong mount, a bad bind, a\n"
"directory nobody owns with the wrong mode, a unit file no package installed\n"
"-- none of those can be in a manifest, so none of them can be reported.</li>"
"<li><b>A reboot is a diagnostic, not a repair.</b> It destroys the evidence\n"
"faster than anything else available to you, and the class of fault where a\n"
"daemon is running with a config nobody reloaded EVAPORATES on reboot, along\n"
"with any chance of explaining what happened.</li>"
"</ul>"
"<p>Question 5 is the only one I actually care about: you have fixed it, how\n"
"do you know? <b>svc</b>, then <b>pkg verify</b> on what you touched, then say\n"
"out loud what you changed. Nobody gets that one first.</p>"
},

{ "kaznotes.net", "10.0.3.17", "/vendors",
"<h1>reading a support article without believing it</h1>"
"<p>Somebody asked why I am rude about <a\n"
"href=\"support.zephyrsys.com\">support.zephyrsys.com</a>. I am not rude about\n"
"it. I am rude about reading it uncritically, which is a different thing and\n"
"is a skill worth having, because most of what is written about computers was\n"
"written for a machine that is not yours.</p>"
"<p>Their KB1041 says reinstall the product, then reinstall the operating\n"
"system, then reinstall the product. On this system you almost never need step\n"
"two: <b>pkg verify</b> names the files that no longer match, and <b>pkg\n"
"reinstall</b> puts back ONE package and keeps the config you edited.\n"
"Reinstalling the machine destroys the evidence and the fault comes back,\n"
"because the fault was a decision somebody made and it was written down in a\n"
"file you have just overwritten.</p>"
"<p>Their KB2207 is worse, because it is nearly right, and I have written a\n"
"correction on the article itself. Two things in it will take a healthy\n"
"machine down:</p>"
"<ul>"
"<li>Disabling units you \"do not use\" to speed the boot. On this init a unit\n"
"ordered AFTER a disabled one waits for it for ever, and svc shows the\n"
"dependents dead with no reason given. Read the console; svcinit says which\n"
"kind of failure it was and it says the word <i>disabled</i>.</li>"
"<li>Deleting /run to free space. Every daemon writes what it actually loaded\n"
"into /run/&lt;name&gt;.state, and several refuse to start at all if they\n"
"cannot -- correctly, because a daemon nobody can question is not running in\n"
"any useful sense. It also takes the display server's directory with it.</li>"
"</ul>"
"<p>And their KB0088, on UUIDs, contains the single most expensive sentence in\n"
"this business: that if the loader config and fstab agree, the disk is\n"
"correctly identified. They agree because the same person wrote them on the\n"
"same afternoon. <b>blkid</b> asks the disk. Nothing else does.</p>"
"<p>None of this makes them villains. Real support articles are written by\n"
"someone who has never seen your machine, for a product that shipped three\n"
"versions ago, and they are still often the only thing written down. Read\n"
"them. Check every command in them against <b>man</b> before you type it. A\n"
"command that does not exist on your system is the cheapest possible\n"
"warning.</p>"
},

/* ------------------------------------------------------------------ *
 * The vendor KB, wrong in the way real ones are wrong: nearly right,
 * for a slightly different machine, never reviewed. Both articles are
 * corrected at the foot of the article itself and again on kaznotes.
 * ------------------------------------------------------------------ */

{ "support.zephyrsys.com", "10.0.3.13", "/kb2207",
"<h1>KB2207 -- Improving system startup time</h1>"
"<p><b>Applies to:</b> Linux-based operating systems. <b>Last reviewed:</b>\n"
"never. <b>Author:</b> no longer with the company.</p>"
"<h2>Recommendations</h2>"
"<ul>"
"<li><b>1.</b> Review the services on the system and set <i>enabled: no</i> on\n"
"any unit you do not use. Each disabled unit saves startup time.</li>"
"<li><b>2.</b> Free space in temporary locations. The directories /run and\n"
"/var/log can be removed entirely and will be recreated.</li>"
"<li><b>3.</b> Run a full integrity check and reinstall every package it\n"
"reports, to ensure a known-good baseline.</li>"
"<li><b>4.</b> Where a unit fails to start, remove the unit file. A service\n"
"that does not start provides no value.</li>"
"<li><b>5.</b> Reboot to confirm the improvement.</li>"
"</ul>"
"<hr>"
"<p><i>Was this article helpful?</i> [ yes ] [ no ] -- 11% of 3,904 readers\n"
"found this article helpful.</p>"
"<h2>Correction, added by a reader</h2>"
"<p>Do not do 1, 2, 3 or 4 on NomnixOS. I have taken a healthy machine down\n"
"with each of them so that you do not have to.</p>"
"<ul>"
"<li><b>1 will hang your boot.</b> Units declare <i>after:</i> another unit. A\n"
"unit ordered after one you have just disabled waits for it for ever. <b>svc</b>\n"
"shows the dependents DEAD with no reason at all and pkg verify points at the\n"
"wrong service; the boot console is the only place the truth appears, and\n"
"svcinit does say the word <i>disabled</i> there. Read it.</li>"
"<li><b>2 will stop most of your daemons.</b> Every daemon publishes what it\n"
"actually loaded to /run/&lt;name&gt;.state and several refuse to start\n"
"without it, which is right: a running service nobody can question is worse\n"
"than a stopped one. /run also holds the display server's directory. And\n"
"/var/log is owned by the syslog package, so removing it is visible to <b>pkg\n"
"verify syslog</b> and repairable by <b>pkg reinstall syslog</b> -- which is\n"
"lucky, and is not true of every directory.</li>"
"<li><b>3 destroys people's work.</b> <b>pkg verify</b> is not a fault list.\n"
"It reports every deliberate local edit as CHANGED, because they are changed.\n"
"<b>pkg diff</b> first, and read it. A diff that reads like a decision is a\n"
"decision.</li>"
"<li><b>4 is backwards.</b> A unit that fails to start is telling you\n"
"something, and svcinit says WHICH KIND: <i>not found</i>, <i>present, and not\n"
"executable</i>, <i>will not load -- check ldd on it</i>, and <i>started and\n"
"would not stay up</i> are four different afternoons. Deleting the file throws\n"
"away the message.</li>"
"<li><b>5 is the only harmless step</b>, and even then a reboot is a\n"
"diagnostic and not a repair -- it destroys the evidence, and one whole class\n"
"of fault, where a daemon runs with a config nobody reloaded, vanishes on\n"
"reboot and comes back next week.</li>"
"</ul>"
"<p>The genuinely useful version of this article is one line: a unit in the\n"
"wrong runlevel is present, correct, enabled, healthy and never started, and\n"
"nothing reports an error because nothing was tried. That is the only startup\n"
"problem on this system that is actually about startup. -- kaz</p>"
},

{ "support.zephyrsys.com", "10.0.3.13", "/kb0088",
"<h1>KB0088 -- Understanding disk identifiers (UUIDs)</h1>"
"<p><b>Applies to:</b> all supported platforms. <b>Last reviewed:</b> never.</p>"
"<h2>Overview</h2>"
"<p>Modern systems identify disks by UUID rather than by device name, because\n"
"device names may change between boots. The UUID appears in the boot loader\n"
"configuration and in the filesystem table.</p>"
"<h2>Verification procedure</h2>"
"<p>To confirm that a disk is correctly identified, compare the UUID in the\n"
"boot loader configuration with the UUID in the filesystem table. <b>If the\n"
"two values match, the disk is correctly identified</b> and no further action\n"
"is required.</p>"
"<h2>If the values do not match</h2>"
"<p>Edit one of the files so that it matches the other.</p>"
"<hr>"
"<h2>Correction, added by a reader</h2>"
"<p>The sentence in bold above is the most expensive sentence I have ever read\n"
"in a support article, and I have followed it, and it cost me three hours.</p>"
"<p><b>TWO CONFIGS AGREEING IS NOT EVIDENCE ABOUT A DISK.</b> They agree\n"
"because the same person wrote them on the same afternoon, from the same\n"
"belief, which may always have been wrong. Neither file has ever asked the\n"
"hardware anything. The only thing in the building that knows what UUID that\n"
"disk carries is the disk:</p>"
"<pre>blkid</pre>"
"<p>Four seconds. Compare BOTH files against that, not against each other. A\n"
"UUID can be perfectly well formed, correctly spelled, and simply belong to\n"
"some other disk -- at which point nothing is corrupt, every hash matches, and\n"
"<b>pkg verify</b> will hand you a clean bill of health while the machine sits\n"
"in the initrd waiting for a filesystem that is not in the room.</p>"
"<p>And on the last section: reinstalling the boot loader package does NOT fix\n"
"this, because the package ships the config for the machine it was BUILT for.\n"
"<b>zbl-mkconfig</b> regenerates it from the machine in front of you. Putting\n"
"a file back and making a file true are different acts.</p>"
"<p>The two files fail at different stages, too, which is worth knowing before\n"
"you start editing: the loader config strands you in the initrd before\n"
"userland exists, and /etc/fstab strands you in <b>mountall</b> with the\n"
"machine half up. Where it stopped tells you which file to open. -- kaz</p>"
},

/* ------------------------------------------------------------------ *
 * THE PAGE WHOSE OUT-OF-DATE BANNER IS ITSELF OUT OF DATE, which is the
 * single most common thing on any real intranet.
 * ------------------------------------------------------------------ */

{ "oldwiki.internal", "10.0.2.71", "/boot-guide",
"<h1>Booting a NomnixOS machine -- departmental guide</h1>"
"<p><b>*** THIS PAGE IS OUT OF DATE. It describes NomnixOS 9. Please use the\n"
"new wiki at newwiki.internal/boot. ***</b></p>"
"<h2>The boot sequence</h2>"
"<pre>"
"firmware -> zbl -> kernel -> initrd -> init -> rc -> services"
"</pre>"
"<h2>Notes</h2>"
"<ul>"
"<li>The kernel is at /boot/vmnomuz. This is a SYMLINK to the versioned\n"
"image; ls shows the link and stat follows it.</li>"
"<li>The bootloader reads /boot/zbl/zbl.cfg.</li>"
"<li>pid 1 reads /etc/inittab.</li>"
"<li>Services are started by /etc/rc.d/rc.N calling the start scripts in\n"
"/etc/init.d one at a time, in filename order.</li>"
"<li>To restart a service, run its script: /etc/init.d/&lt;name&gt; restart.</li>"
"<li>The service log is /var/log/rc.log.</li>"
"</ul>"
"<hr>"
"<h2>Note from the archivist, added when this wiki was frozen</h2>"
"<p>The banner at the top of this page is itself out of date, which is\n"
"funnier than it is useful. THERE IS NO newwiki.internal. It existed for about\n"
"five months and was folded into wiki.nomnix.org at the 11.0 release. Try\n"
"<b>links newwiki.internal</b> and the resolver will tell you there is no such\n"
"host, which is a real answer and not an error.</p>"
"<p>So this page has been warning people away to a place that does not exist\n"
"for longer than it was ever correct. I have left it exactly as it is, and\n"
"marked up the body instead, because that is more honest than deleting it:</p>"
"<ul>"
"<li><b>Still true:</b> the seven stages, in that order.</li>"
"<li><b>Still true:</b> /boot/vmnomuz is a symlink, and stat is how you find\n"
"out. This is the single most useful sentence anybody wrote in NomnixOS 9 and\n"
"it has survived three releases.</li>"
"<li><b>Still true:</b> zbl reads /boot/zbl/zbl.cfg, and pid 1 reads\n"
"/etc/inittab.</li>"
"<li><b>WRONG since 10.0:</b> there is no /etc/init.d and there are no start\n"
"scripts. Services are UNIT FILES at /etc/services.d/&lt;name&gt;.svc, started\n"
"by /sbin/svcinit in DEPENDENCY order and not in filename order -- each unit\n"
"declares <i>after:</i>, and that is what decides. Read one and it is\n"
"obvious.</li>"
"<li><b>WRONG since 10.0:</b> there are no start scripts to run. The verb is\n"
"<b>svc</b>, and it has two halves that people run together in their heads\n"
"and should not. <b>svc start|stop|restart|reload &lt;name&gt;</b> act on\n"
"the process that is running RIGHT NOW; <b>svc enable</b> and <b>svc\n"
"disable</b> decide only whether it starts at the NEXT boot and touch\n"
"nothing today. A repair is usually one of each, and the half people forget\n"
"is whichever one they did not need to see the machine come right.</li>"
"<li><b>WRONG since 10.4</b>, and this is my correction, not theirs: I wrote\n"
"here for two years that there was no restart and that starting a service\n"
"over meant taking the machine down. There is now, and it matters more than\n"
"it sounds. <b>svc reload</b> is <b>kill -HUP</b> with the name spelled out\n"
"-- the process stays up and /run/&lt;name&gt;.state changes in front of you\n"
"-- and <b>svc restart</b> re-reads the unit file as well, which is the one\n"
"you want when it is the unit that changed. Neither takes the machine down,\n"
"which is the entire point: the class of fault where a daemon is out of step\n"
"with a file cannot survive a reboot, so a reboot is the one repair that\n"
"guarantees you never find out what it was. A daemon that does not read\n"
"signals says so when you reload it, and then restart is your answer.</li>"
"<li><b>WRONG since 10.0:</b> there is no /var/log/rc.log. The boot log is\n"
"<b>dmesg</b>, the previous boot is <b>dmesg -1</b>, and the system log is\n"
"/var/log/messages.</li>"
"</ul>"
"<p>Three of six lines still correct after two major releases is, in my\n"
"experience of this wiki, an excellent score. The correct page is\n"
"<a href=\"wiki.nomnix.org/boot\">wiki.nomnix.org/boot</a>, which is checked\n"
"against a machine.</p>"
},

/* ------------------------------------------------------------------ *
 * The supplier. Where the kit comes from, and where the decision about
 * WHICH kit gets made.
 *
 * Two of these four pages are generated (see shop_catalogue and
 * shop_discontinued at the foot of this file). The prose here contains no
 * price, no port count and no speed -- not one number about a product
 * anywhere on this host that was not read out of the catalogue at the moment
 * the page was fetched. That is a rule, not a habit: the moment a sentence
 * here says "four hundred" it is a second copy of site.c's KIT table and it
 * will be wrong within the week.
 * ------------------------------------------------------------------ */

{ "halbert.co.uk", "10.0.2.73", "/",
"<h1>HALBERT &amp; VANCE LTD</h1>"
"<img src=\"hv_van.jpg\" alt=\"A white van with HALBERT AND VANCE -- NETWORK "
"HARDWARE -- TRADE ONLY down the side of it\">"
"<p>Network hardware, trade only, since 1994. We are two units off the ring "
"road and one van, and we would rather sell you the right thing once than the "
"wrong thing twice.</p>"
"<ul>"
"<li><a href=\"/catalogue\">/catalogue</a> -- everything we stock, with prices</li>"
"<li><a href=\"/delivery\">/delivery</a> -- how it gets to you, and what happens "
"if you change your mind</li>"
"<li><a href=\"/discontinued\">/discontinued</a> -- what we used to sell and "
"will not be selling again</li>"
"</ul>"
"<h2>Ordering</h2>"
"<p>Off this website, and there is no other way in. We closed the counter to "
"walk-ins in the spring and the phone is an answering machine that says to use "
"the website, which is a decision our sales manager is very pleased with and "
"nobody else is.</p>"
"<p>There is a link beside every product on the "
"<a href=\"/catalogue\">catalogue</a>. A browser that can place orders -- the "
"one on a workstation can -- will ask you to confirm before it spends "
"anything.</p>"
"<p><b>Which does mean that if you cannot reach this page, you cannot buy "
"anything.</b> We are aware. Several of our customers have pointed out, at "
"length, that the one thing you might urgently need a switch for is a network "
"that is too broken to order a switch over. Our position is that we are a "
"hardware supplier and not a telephone exchange.</p>"
"<h2>If this page will not load</h2>"
"<p>It is almost never us -- see the ping at the bottom -- and the machine you "
"are sitting at will tell you which of the four usual things it is, in this "
"order, cheapest first:</p>"
"<pre>"
"cat /etc/resolv.conf\n"
"ip addr\n"
"ip route\n"
"traceroute 10.0.2.73"
"</pre>"
"<p>A name that will not resolve when the address works is your resolver. An "
"address you have not got is your interface, or the daemon that configures it. "
"A route you have not got is your gateway. And a traceroute that stops "
"somewhere out in the middle is the bit neither of us owns.</p>"
"<h2>Where it turns up</h2>"
"<p>Goods in, on the ground floor, on a pallet, in the box. Not in your hands "
"and not in the room you are standing in. Our driver is not carrying it up "
"five flights and neither, in his opinion, should you, but that is between you "
"and your knees.</p>"
"<hr>"
"<p>Trade counter open, and the kettle is on. If this page is loading, our web "
"server is up: it is on 10.0.2.73 and it answers a ping.</p>"
"<pre>"
"ping -c 1 10.0.2.73"
"</pre>"
},

{ "halbert.co.uk", "10.0.2.73", "/delivery",
"<h1>Delivery, and returns</h1>"
"<h2>Delivery</h2>"
"<p>Everything on the <a href=\"/catalogue\">catalogue</a> is stock. That is "
"what the catalogue IS -- if we have not got it, it is not on the page, and if "
"it is on the page the van has it on board. Order it and it is in your goods "
"in, in its box, unplugged and switched off, by the time you have walked down "
"there.</p>"
"<p>Switched off matters and people forget it. A switch or a router wakes up "
"when it has a socket; a computer has a button on the front of it and somebody "
"has to press it. If you cannot find a live socket in the room you have carried "
"it to, the button will not do anything, and that is the socket's fault rather "
"than ours.</p>"
"<h2>Returns</h2>"
"<p>There are none. We are not being difficult: money that leaves does not come "
"back, there is nobody here to take a pallet in off you, and a box you have "
"ordered twice is a box you have paid for twice. <b>Look in goods in before you "
"order.</b> Half the calls we get are somebody buying a second switch that was "
"already sitting under the roller door in the dark.</p>"
"<hr>"
"<p><a href=\"/\">halbert.co.uk</a></p>"
},

/* Both of these are printed off core/site.c's catalogue when you ask for
 * them. NULL body, see gen_page(). */
{ "halbert.co.uk", "10.0.2.73", "/catalogue", NULL },
{ "halbert.co.uk", "10.0.2.73", "/discontinued", NULL },
};
#define NPAGES ((int)(sizeof PAGES / sizeof PAGES[0]))

/* ======================================================================= *
 * THE SHOP, PRINTED OFF THE CATALOGUE
 *
 * The rule for these two functions: every FACT about a product is read from
 * core/site.c through site_kind_*() at the moment the page is fetched, and
 * every OPINION about it is typed below. A product appears in this shop
 * because it exists, not because somebody remembered to add it -- the loop
 * is over SDEV_KIND_COUNT and it does not know what is in the catalogue.
 *
 * WHAT IS FOR SALE is the one thing that had to be inferred, because nothing
 * in site.h says so. It is `price > 0`: the ISP's handoff is the landlord's
 * and the tenant's desk is the tenant's, both cost the player nothing, and
 * neither is the landlord's to buy. A shop sells what it can charge for. If
 * a priced product is ever added it turns up on this page, in the table and
 * in the order links, with no shop copy and an honest line saying so -- see
 * the fallback in HAVE below.
 * ======================================================================= */

/* What the trade counter thinks of each product. NO NUMBERS: the moment a
 * sentence here says "four hundred" or "eight ports" it is a second copy of
 * KIT[] and it will be wrong the week somebody retunes it. Keyed by the
 * catalogue's own name so that renumbering the enum cannot silently move a
 * paragraph onto a different box. */
static const struct { const char *kind; const char *says; } HAVE[] = {
{ "switch8",
"<p>Our biggest seller, and the one we would rather you thought about for a "
"minute. All the sockets are the same and none of them is an uplink, so "
"everything on it -- including the lead going back to the riser -- is fighting "
"over the same kind of hole.</p>"
"<p>It is the cheapest way to get a floor on the network and the cheapest way "
"to regret it. When the floor fills up you will hang a second one off the "
"first, and then everything on both of them is queueing through the one lead "
"between them. Right in a cupboard that will never hold more than a handful of "
"things. Wrong as the box a whole floor's traffic leaves the floor through.</p>"
},
{ "switch24",
"<p>The one we sell to people who have already owned the cheap one. Enough "
"sockets that you will not be back this year, and -- the actual reason to buy "
"it -- the top ones are faster than the rest. The table says which.</p>"
"<p>Those holes are what you are paying for. Land the riser on one of them and "
"the floor stops queueing behind its own way out. Land the riser on any of the "
"others and you have bought a big switch for the size of it, which is money "
"spent on ports you are not using yet.</p>"
},
{ "router",
"<p>Every socket fast, and as many vlans on them as you have the patience to "
"configure. This is what you buy when \"everything on one flat network\" has "
"stopped being a simplification and started being the fault -- when one "
"tenant's broadcast is in another tenant's day.</p>"
"<p>It will also happily be the most expensive way to join two things "
"together. If nothing needs separating yet, nothing needs this yet.</p>"
},
{ "pc",
"<p>An ordinary desktop, and an ordinary card in it. People buy them to have "
"something on a floor that can hold a shell, a browser and a set of eyes -- "
"and to find out whether a port and a run of copper really work, without "
"standing a server on the floor to do it.</p>"
},
{ "server",
"<p>A rack machine. What you are really buying is the card in it, and there is "
"one card.</p>"
"<p>Everybody's files behind that one card is a ceiling, and no amount of "
"copper will lift it, because the copper is not the thing that is full. People "
"find that out on their third floor at about nine in the morning. If a floor's "
"people spend the day pulling files, the cheap answer is not a fatter riser, "
"it is a box on that floor so their files never come down it.</p>"
},
};
#define NHAVE ((int)(sizeof HAVE / sizeof HAVE[0]))

/* Is this something the shop sells? See the note above: price is the test,
 * and it is the catalogue's own number rather than a list kept here. */
static bool for_sale(int kind)
{
    return site_kind_price(kind) > 0;
}

/* The ports that are not like the others, read off the catalogue. Returns
 * the speed of the odd ones and fills lo/hi, or 0 if every socket on the box
 * clocks the same. */
static int odd_ports(int kind, int *lo, int *hi)
{
    int n = site_kind_ports(kind), base = site_kind_port_mb(kind, 0), odd = 0;
    *lo = *hi = -1;
    for (int p = 1; p < n; p++) {
        int mb = site_kind_port_mb(kind, p);
        if (mb == base) continue;
        if (*lo < 0) *lo = p;
        *hi = p;
        odd = mb;
    }
    return odd;
}

static void shop_catalogue(Buf *out)
{
    buf_puts(out,
"<h1>Catalogue</h1>"
"<p>Everything on this page is stock. That is what being on this page MEANS: "
"if the van has not got it we take it off, which is why there is no column "
"here for lead time and no such thing as a back order. Ordered is delivered.</p>"
"<p>This table is printed out of the stock system when you ask for the page, "
"so it cannot disagree with what we charge you at the counter. Prices in "
"pounds. \"Each socket\" is what one hole on the back of it will clock, which "
"is not the same as what your cable will carry -- that is your problem and "
"there is a note about it at the bottom.</p>"
"<pre>\n");
    buf_printf(out, "  %-10s %8s %13s %8s\n",
               "what", "sockets", "each socket", "price");
    for (int k = 0; k < SDEV_KIND_COUNT; k++) {
        if (!for_sale(k)) continue;
        int lo, hi, odd = odd_ports(k, &lo, &hi);
        buf_printf(out, "  %-10s %8d %10d Mb %8d",
                   site_kind_name(k), site_kind_ports(k),
                   site_kind_port_mb(k, 0), site_kind_price(k));
        if (odd) {
            if (lo == hi) buf_printf(out, "   port %d at %d Mb", lo, odd);
            else          buf_printf(out, "   ports %d-%d at %d Mb", lo, hi, odd);
        }
        buf_putc(out, '\n');
    }
    buf_puts(out, "</pre>");

    for (int k = 0; k < SDEV_KIND_COUNT; k++) {
        if (!for_sale(k)) continue;
        const char *name = site_kind_name(k);
        buf_printf(out, "<h2>%s</h2>", name);
        const char *says = NULL;
        for (int i = 0; i < NHAVE; i++)
            if (strcmp(HAVE[i].kind, name) == 0) { says = HAVE[i].says; break; }
        /* A NEW LINE THE COUNTER HAS NOT WRITTEN UP. It is still for sale,
         * it is still in the table with its real numbers, and this says so
         * rather than pretending we have an opinion about it. */
        if (says) buf_puts(out, says);
        else buf_puts(out,
"<p>New line, and we have not had one long enough to have an opinion about it. "
"The numbers in the table are the manufacturer's and they are what you will "
"be charged.</p>");
        /* And the facts, generated: how many holes, how fast, and whether
         * anybody has to press anything. */
        int lo, hi, odd = odd_ports(k, &lo, &hi);
        buf_printf(out, "<p>%d socket%s, ", site_kind_ports(k),
                   site_kind_ports(k) == 1 ? "" : "s");
        if (odd) {
            buf_printf(out, "%d Mb each except ", site_kind_port_mb(k, 0));
            if (lo == hi) buf_printf(out, "port %d, which is %d Mb. ", lo, odd);
            else          buf_printf(out, "ports %d to %d, which are %d Mb. ",
                                     lo, hi, odd);
        } else {
            buf_printf(out, "%d Mb%s. ", site_kind_port_mb(k, 0),
                       site_kind_ports(k) == 1 ? "" : " each");
        }
        if (site_kind_has_os(k))
            buf_puts(out, "It is a computer: it comes switched off, it needs a "
                          "socket on the wall of whatever room you carry it to, "
                          "and somebody has to press the button on the front.</p>");
        else
            buf_puts(out, "It is an appliance. There is no shell on it and no "
                          "button: give it a socket and it comes up, and you "
                          "talk to it over its management line rather than by "
                          "logging in.</p>");
        buf_printf(out, "<p><a href=\"order:%s\">order a %s</a> "
                        "-- to your goods in, in the box.</p>", name, name);
    }

    buf_puts(out,
"<hr>"
"<h2>What we would tell you if you rang up and asked</h2>"
"<p>The cheap box and the dear box do the same job on the day you plug them "
"in. They stop doing the same job the day the floor above yours signs a lease. "
"Everything on this page is a bet on how big you are going to be, and the only "
"one of those bets you can unmake is the one you have not placed yet -- see "
"<a href=\"/delivery\">/delivery</a> on returns, of which there are none.</p>"
"<p>Nobody has ever rung this counter to say the switch they bought was too "
"big. We get the other call most weeks.</p>"
"<p>And the speed on a socket is the top of what that hole will do, not a "
"promise. Put a long enough run of the wrong copper on it and the two ends "
"will settle on something slower between themselves, and neither we nor the "
"box will apologise. Ask the building what a run would come up at before you "
"pay for it.</p>"
"<hr>"
"<p><a href=\"/\">halbert.co.uk</a> -- "
"<a href=\"/discontinued\">what we no longer sell</a></p>");
}

/* WHAT WE NO LONGER SELL, and the filter that keeps it honest.
 *
 * A page listing products that do not exist is a page that becomes a lie the
 * day one of those names is added to the catalogue. So every name below is
 * checked against site_kind_by_name() as the page is printed, and a name the
 * building now understands is dropped from the list rather than advertised
 * as unavailable. */
static const struct { const char *name; const char *why; } GONE[] = {
{ "hub",
"Everything plugged into one of these shared a single conversation and talked "
"over the top of each other. They were cheap and they were a false economy the "
"day the second person on the floor started work." },
{ "switch16",
"Sat between the small one and the big one and did neither job. No fast ports "
"on it, and if you had outgrown the small one you had outgrown this too. The "
"manufacturer stopped making them and we did not chase anybody about it." },
{ "printserver",
"A box whose entire purpose was to put a printer on the network. Printers "
"grew their own cards and this became a thing you found behind a filing "
"cabinet with a light on." },
};
#define NGONE ((int)(sizeof GONE / sizeof GONE[0]))

/* WHICH FUNCTION PRINTS A PAGE THAT HAS NO BODY. Host and path both, because
 * "/catalogue" is not a name any one host owns. */
static void gen_page(const Page *p, Buf *out)
{
    if (strcmp(p->host, "halbert.co.uk") == 0) {
        if (strcmp(p->path, "/catalogue") == 0)    { shop_catalogue(out); return; }
        if (strcmp(p->path, "/discontinued") == 0) { shop_discontinued(out); return; }
    }
    /* Unreachable, and it says so rather than serving nothing: a page in the
     * table with no body and no generator is a bug in this file, and an
     * empty page is the hardest kind of bug to see. */
    buf_printf(out, "<h1>%s%s</h1><p>This page is generated and its generator "
                    "is missing. That is a fault in net_sites.c.</p>",
               p->host, p->path);
}

static void shop_discontinued(Buf *out)
{
    buf_puts(out,
"<h1>Discontinued</h1>"
"<p>We do not stock these, we cannot get them, and we would not sell you one "
"if we could. They are here because people still ring up and ask, and because "
"one of them was on the wall of this building when we took the account "
"over.</p>"
"<p>The building will tell you the same thing we would: ask for one of these "
"by name and there is no such kit.</p>"
"<ul>");
    int listed = 0;
    for (int i = 0; i < NGONE; i++) {
        if (site_kind_by_name(GONE[i].name) >= 0) continue;   /* it is back */
        buf_printf(out, "<li><b>%s</b> -- %s</li>", GONE[i].name, GONE[i].why);
        listed++;
    }
    if (!listed)
        buf_puts(out, "<li>Nothing, as it happens. Everything we ever sold, we "
                      "are selling again. It happens about once a decade.</li>");
    buf_puts(out,
"</ul>"
"<p>What we do have is on the <a href=\"/catalogue\">catalogue</a>, and it is "
"there because it is on the van.</p>");
}

/* Resolve a hostname the way a nameserver would. Returns NULL if the name is
 * not known -- which is a real answer, not an error.
 *
 * This is now the ZONE FILE, not the resolver. Nothing in the machine calls
 * it any more: netsite.c loads these pairs into a real nameserver at
 * 10.0.2.3, and a program inside the machine gets them by sending a query
 * over UDP and waiting for a packet back. Which is why pointing resolv.conf
 * at an address with nothing on it now fails the way it should. */
const char *net_dns(const char *host)
{
    for (int i = 0; i < NPAGES; i++)
        if (strcmp(PAGES[i].host, host) == 0) return PAGES[i].ip;
    return NULL;
}

/* Walk the hosts on this web, one entry per DISTINCT name, so the site can
 * be loaded into a nameserver and onto a web server at start of day. The
 * pages themselves are not enumerated: what a server serves is still
 * net_fetch's business, and it is still keyed by address. */
int net_site_hosts(int i, const char **host, const char **ip)
{
    int seen = 0;
    for (int k = 0; k < NPAGES; k++) {
        bool dup = false;
        for (int j = 0; j < k; j++)
            if (strcmp(PAGES[j].host, PAGES[k].host) == 0) { dup = true; break; }
        if (dup) continue;
        if (seen == i) { *host = PAGES[k].host; *ip = PAGES[k].ip; return 1; }
        seen++;
    }
    return 0;
}

/* AND EVERY PAGE, one at a time, which net_site_hosts deliberately does not
 * do. The header of this file says "every word of this is true of the
 * machine", and that was a promise kept by hand: a wiki page still claimed
 * "nomsh 1.11" after the shell went to 1.12. `bf --mancheck` now runs the
 * command examples out of these pages on a real booted machine, and it needs
 * to walk them. Nothing else does, and nothing else should -- serving a page
 * is still net_fetch's business, keyed by address. */
int net_site_page(int i, const char **host, const char **ip, const char **path)
{
    if (i < 0 || i >= NPAGES) return 0;
    *host = PAGES[i].host;
    *ip   = PAGES[i].ip;
    *path = PAGES[i].path;
    return 1;
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
        if (PAGES[i].body) buf_puts(out, PAGES[i].body);
        else               gen_page(&PAGES[i], out);
        return true;
    }
    /* The host is there but the page is not. Generated in the same markup as
     * everything else, so a 404 is a page you can click your way out of
     * rather than a wall of text both renderers have to special-case. */
    for (int i = 0; i < NPAGES; i++) {
        if (strcmp(PAGES[i].ip, ip) != 0) continue;
        buf_printf(out, "<h1>404 -- no such page</h1><p>There is nothing at "
                        "<b>%s</b> on this host.</p><h2>What it does serve</h2>"
                        "<ul>", path);
        for (int j = 0; j < NPAGES; j++)
            if (strcmp(PAGES[j].ip, ip) == 0)
                buf_printf(out, "<li><a href=\"%s%s\">%s%s</a></li>",
                           PAGES[j].host, PAGES[j].path,
                           PAGES[j].host, PAGES[j].path);
        buf_puts(out, "</ul>");
        return true;
    }
    return false;
}
