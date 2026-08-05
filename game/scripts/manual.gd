# manual.gd — the documentation app.
#
# David: "One of the apps on the desktop should be a documentation that goes
# through the boot process, the utilities, giving you in-depth knowledge of
# everything you might need to debug."
#
# Every word here is true of the machine actually being simulated. That is the
# only rule that matters: a manual that describes a system slightly different
# from the one in front of you is worse than no manual, because it teaches you
# to distrust it. Where this text names a file, that file exists; where it
# names a failure, the breaker really produces it.

extends Control

var mono: Font
var scroll := 0
var section := 0

const PAGES := [
"THE BOOT, IN ORDER

Every stage hands off to the next, and a failure is reported as the stage it
died in. Knowing the order IS the diagnosis: it tells you which half of the
system to stop looking at.

  firmware    zbios finds something to boot. If the boot sector is gone
              there is nothing here to fix from inside the system.
  bootloader  zbl reads /boot/zbl/zbl.cfg, and that file has ENTRIES. It
              boots the one `default N` names, counted from ZERO, and says
              which: `zbl: booting entry 0 of 2`. `zbl-mkconfig` rewrites
              the file from this machine; `zbl-install /dev/sda` writes the
              boot sector and the firmware's boot entry together.
  kernel      the chosen entry's `kernel` is loaded. /boot/vmnomuz is a
              SYMLINK to a versioned image (/boot/vmnomuz-6.4.11), and the
              loader prints the version it read out of the IMAGE rather
              than out of the filename: `zbl: loading /boot/vmnomuz
              (6.4.11)`. A dangling symlink here is common and `ls -l
              /boot` shows it as DANGLING.
  initrd      the chosen entry's `initrd` is unpacked. It carries driver
              modules and NOTHING ELSE -- there is no shell in it, so a
              boot that stops here has no prompt on it and the way in is
              the rescue medium. It waits for whatever the `root` line
              says, a UUID or a device node, so a zbl.cfg naming a UUID
              this disk does not have waits forever. `blkid` tells you the
              UUID the disk really has.
  init        /sbin/init, pid 1. It runs /bin/rc /etc/rc.boot.
  rc.boot     brings the filesystems up: /sbin/mountall reads /etc/fstab and
              mounts everything in it, then the default runlevel is entered
              through /etc/rc.d/rc.3.
  services    /sbin/svcinit reads the unit files in /etc/services.d and
              starts them in dependency order.
  login       /sbin/getty offers a login prompt. It checks that the account
              exists in /etc/passwd and that its shell exists and can be
              executed -- so a machine can boot perfectly and still be
              impossible to log in to.
  target      everything that should be running is running.

ZBL.CFG, IN ONE SCREEN

  default 0      which entry, COUNTED FROM ZERO. Absent means 0.
  timeout 5      read, kept and never waited on -- nothing here can press
                 a key. Everything above the first `entry` is global.
  entry \"...\"     opens a block, which runs to the next `entry` or to the
                 end of the file. The label is decoration.
    kernel PATH  all three REQUIRED, and read from the CHOSEN ENTRY ONLY:
    initrd PATH  a `kernel` line up in the global section is read by
    root SPEC    nothing, and the loader says there is no kernel line.

Any other word stops the boot and names the line it was on -- `zbl.cfg:5:
unrecognised directive: timout`. The whole file is checked before any of it
is used, so a typo on the last line kills a boot the rest of the file would
have got right.

THREE THINGS HAVE TO AGREE

The kernel image, /lib/modules/<the version that image says it is>, and the
initrd. Most upgrade tickets are one of those three disagreeing, and the
console names which one, because the repairs are opposite.

  kernel: /lib/modules/6.4.11: no modules for this kernel
                 the modules are the odd one out --
                 `pkg reinstall kernel-default`
  initrd: /boot/initrd was built for 6.3.12, and this kernel is 6.4.11
                 the initrd is the odd one out -- `mkinitrd`, which
                 rebuilds it from /lib/modules and nothing else",

"READING THE BOOT LOG

  dmesg              what this boot said
  dmesg -1           the previous boot
  dmesg -f <text>    only lines containing <text>
  dmesg -r /mnt      the customer's log, from the rescue medium

The log is written to /var/log/boot.log as the machine boots, so it exists
for a boot that FAILED -- which is the only kind worth reading. The previous
boot is kept as /var/log/boot.log.1.

READ IT FIRST. It names the layer that failed, and the layer decides which
package to suspect. A machine that cannot write its log at all is itself a
finding: either the root is mounted read-only, or /var/log is not there.",

"PACKAGES

Everything on the disk belongs to a package, and the package database on the
machine records the mode, a hash and the path of every file -- and, like rpm
and dpkg, the DIRECTORIES a package owns.

  pkg list                    what is installed
  pkg verify [name]           what no longer matches what was shipped
  pkg owns <path>             which package a file belongs to
  pkg diff <path>|<package>   what a file says, against what shipped
  pkg reinstall <name>        put the files back
  pkg reinstall --force <n>   ...including edited config
  pkg --root /mnt <verb>      work on a disk mounted elsewhere

TWO THINGS ABOUT verify. It is not a fault list: every machine has
configuration somebody edited on purpose, and those show as CHANGED because
that is the truth. And it cannot see a fault that is not a file -- a
directory's mode, a missing directory, a bind mount, a full disk.

reinstall LEAVES EDITED CONFIG ALONE unless you add --force, which first
copies what was there to <file>.pkgsave. There is no other undo.

--root is not a convenience. When the disk's own libc is broken, nothing on
that disk will run, so you cannot chroot into it -- and chroot will refuse.
--root never runs anything off the broken disk.",

"LIBRARIES

Binaries declare the libraries and versions they need. The loader resolves
them through /etc/ld.so.conf, in order.

  ldd <program>        what it needs, where each was found, and whether it
                       is new enough
  ldd /mnt/sbin/init   resolved against the root filesystem at /mnt

A NEWER library satisfies an older requirement -- that is what symbol
versioning is for. An OLDER one does not. So the interesting library fault is
a downgrade, not an upgrade.

Not every program needs the same libraries. When some services are dead and
others are fine, run ldd on one of each and see what the dead ones have in
common. A library that is installed but sits in a directory nobody lists in
ld.so.conf reads as `not found`, which is the fault stated plainly.",

"SERVICES

A unit file in /etc/services.d says: name, exec, enabled, runlevel, after,
critical, restart.

  svc                  every unit and its state
  svc status <name>    why THIS one is unhappy: restarts, exit status, what
                       it said as it died, whether the kernel gave up
  svc enable <name>    /  svc disable <name>

DEAD means enabled and not running. `not at rl3` means it belongs to another
runlevel and was never meant to start. `disabled` means somebody turned it
off -- which may be the fault, since anything ordered `after` a disabled
service waits forever for something that is never coming.

A service that keeps failing is restarted a few times and then given up on.
That is in the boot log, and `svc status` counts them.",

"FILESYSTEMS

  df           bytes
  df -i        INODES
  mount        what is mounted where
  blkid        the UUID the disk actually has
  fsck /dev/sda1   check and repair a dirty filesystem

A filesystem runs out of the two independently. A disk with free space and no
free inodes refuses to create anything at all, and nothing but `df -i` will
tell you -- every file is intact and `pkg verify` reports a perfect machine.

/etc/fstab is the single source of truth for what gets mounted. The root
entry's options decide whether the running system can write to its own disk:
`ro` there and every service that keeps state dies the moment it tries.

A dirty filesystem will not mount, by the initrd or by you. `fsck` first.",

"THE RESCUE MEDIUM

/dev/sr0 is a complete, separate system that is never damaged. It is the same
system as the customer's with different contents, so what you have learned
about one applies to the other.

  rescue                             boot it
  mount /dev/sda1 /mnt               the customer's disk
  for i in dev sys proc; do mount /$i /mnt/$i; done
  chroot /mnt                        become that system

chroot will REFUSE if the shell in there cannot run -- which is exactly when
a broken libc has made the disk unusable. Use `pkg --root /mnt` instead;
it takes the same verbs and never executes anything off the broken disk.",

"EDITING, AND THE SHELL

  sed -i s/old/new/ <file>       any delimiter: s|a|b|  s,a,b,
  sed -i /text/d <file>          delete every line containing <text>
  echo \"a line\" >> <file>        append   ( > truncates, -n omits newline )
  cp  mv  rm  touch  chmod
  cat  grep  head  wc  ls  stat

The shell has pipes, quoting, && and ||, and `for i in a b; do ... done`.
Escapes reach the program: sed -i \"s|x|/dev/null\\nnext line|\" works.

  ps        the process table, from /proc
  ns        this process's namespace bindings
  kill -HUP <pid>   make a daemon re-read its config

A daemon publishes what it actually LOADED in /run/<name>.state. If that
disagrees with the file on disk, somebody edited the config and never
reloaded it -- the machine is running on something that is no longer written
down anywhere."
]

const TITLES := ["the boot", "the log", "packages", "libraries",
	"services", "filesystems", "rescue", "editing"]


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()


func take_focus() -> void:
	grab_focus()


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		if e.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll = max(0, scroll - 3); queue_redraw()
		elif e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll += 3; queue_redraw()
		elif e.button_index == MOUSE_BUTTON_LEFT and e.position.y < 26:
			var i := int(e.position.x / 92)
			if i >= 0 and i < TITLES.size():
				section = i; scroll = 0; queue_redraw()
		return
	if e is InputEventKey and e.pressed:
		accept_event()
		if e.keycode == KEY_RIGHT or e.keycode == KEY_TAB:
			section = (section + 1) % PAGES.size(); scroll = 0
		elif e.keycode == KEY_LEFT:
			section = (section + PAGES.size() - 1) % PAGES.size(); scroll = 0
		elif e.keycode == KEY_DOWN:
			scroll += 2
		elif e.keycode == KEY_UP:
			scroll = max(0, scroll - 2)
		queue_redraw()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("#12161e"))

	# tabs
	for i in range(TITLES.size()):
		var x := i * 92.0
		if i == section:
			draw_rect(Rect2(x, 0, 92, 24), Color("#2f6fb5"))
		draw_string(mono, Vector2(x + 8, 16), TITLES[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
			Color("#e6eefb") if i == section else Color("#8b97aa"))
	draw_line(Vector2(0, 25), Vector2(size.x, 25), Color("#2b3444"))

	var body: PackedStringArray = PAGES[section].split("\n")
	var rows := int((size.y - 34) / 14)
	var first: int = clampi(scroll, 0, max(0, body.size() - rows))
	var y := 42.0
	for i in range(first, min(body.size(), first + rows)):
		var line := body[i]
		var col := Color("#c3cddb")
		if i == 0 or (line != "" and line == line.to_upper() and line.length() > 4):
			col = Color("#d3b06a")
		elif line.begins_with("  "):
			col = Color("#8fd6a4")
		draw_string(mono, Vector2(10, y), line,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
		y += 14

	if body.size() > rows:
		draw_string(mono, Vector2(size.x - 150, size.y - 6),
			"%d/%d  scroll, arrows" % [first, body.size()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#5d6878"))
