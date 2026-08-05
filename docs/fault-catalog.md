# The fault catalogue

Every way a real Linux machine fails to boot, and what it would take to
reproduce it here honestly. This is the work queue. It is deliberately longer
than what is built, and it is ordered by how much depth each item adds.

**The rule for every entry:** the fault must be a real difference in real
system state, the failure must be *derived* by code that was genuinely trying
to work, and the repair must be possible with tools on the machine. No fault
ids, no narrated symptoms, no special-cased error messages.

Legend: **[done]** built · **[next]** in progress or immediately next ·
**[todo]** wanted, not started.

---

## 1. Firmware and the boot sector

- **[done]** boot sector wiped — not a file, no package owns it, `pkg verify`
  is clean. Fix: `zbl-install`.
- **[todo]** partition table damaged; the root partition is there but nothing
  points at it. Fix: a partition tool that rewrites the table.
- **[todo]** two disks and the firmware boots the wrong one. Nothing is broken
  at all; the fix is boot order, and the evidence is that the machine that
  came up is the *other* install.
- **[todo]** ESP unmounted or empty on a UEFI-style install: kernel updates
  have been landing somewhere nothing reads for months.

## 2. Bootloader

- **[done]** config deleted, truncated, or a directive typo'd.
- **[done]** `root=` naming a well-formed UUID this disk does not have.
  Fix: `zbl-mkconfig`, which regenerates from the machine in front of you —
  reinstalling the package does not, because the package ships the config for
  the machine it was built for.
- **[done]** **config points at a kernel that was removed by a cleanup or by an
  autoremove after an upgrade.** `fault_stale_kernel_entry`. The classic:
  `/boot` filled, the upgrade half finished, and the entry now names a kernel
  that is gone. `zbl: /boot/vmnomuz-6.4.9: not found`, and `ls /boot` shows
  what is really there. The symlink is not involved and `stat /boot/vmnomuz`
  is perfectly happy, which is what separates it from the deleted-image
  fault -- the file the LOADER wants is not the file the system installs.
  Fix: `zbl-mkconfig`, which writes a config for the machine in front of you.
- **[todo]** `/boot` is a separate filesystem and is not mounted, so the
  loader's world and the running system's `/boot` are different directories.
  Deeply confusing and very real.
- **[todo]** loader installed to the wrong disk after a clone.

## 3. Kernel and initrd

- **[done]** kernel image deleted — surfaces as a *dangling symlink*, and
  `ls /boot` looks perfectly healthy. `stat` tells the truth.
- **[done]** initrd missing the root-device or filesystem driver: rebuilding
  cannot invent a module that is gone, so it is reinstall-then-`mkinitrd`,
  two repairs in sequence.
- **[done]** kernel/initrd truncated or magic corrupted.
- **[todo]** **kernel and modules out of step.** `/lib/modules/<version>` does
  not match the running kernel, so drivers refuse to load. The commonest real
  upgrade failure there is.
- **[done]** **initrd built on a machine with different hardware** — it has the
  wrong storage driver, not none. `fault_foreign_initrd`. A complete, valid
  image full of drivers, none of which drive this machine's disk, which is
  what a clone from a box with different storage produces.

  It needed the loader to say what the image DOES carry, because "no driver
  for the root device" is the same sentence for an empty initrd and a foreign
  one and they are not the same problem:

  ```
  initrd: modules in this image: ahci, nvme, ext4, dm_mod
  initrd: no driver for the root device (virtio_blk)
  ```

  Every module is present in `/lib/modules`, so this is `mkinitrd` and
  nothing else -- a different repair from a module that was deleted, which is
  reinstall-then-rebuild.
- **[todo]** `/boot` full, so the new initrd was written truncated. The write
  "succeeded" and the file is half a file.

## 4. Root filesystem

- **[done]** **dirty filesystem needing `fsck`.** An unclean shutdown marks the
  filesystem dirty and leaves whatever was mid-write half-written. The initrd
  refuses to mount it; so does `mount` from the rescue medium. `fsck /dev/sda1`
  rebuilds the metadata and says plainly that it could *not* repair the
  contents — so the ticket is TWO repairs in order, and the first has to happen
  before the player can even look at the disk.
- **[todo]** an emergency shell that actually runs *inside* the initrd, with a
  smaller toolbox than the rescue medium. Currently the initrd reports and the
  player goes to the live image; being stranded with fewer tools is a different
  feeling and still worth building.
- **[done]** **filesystem type wrong in fstab.** `fault_fstype`. One word, and
  the mount fails with the most recognisable error in Unix administration.

  It needed the type to become real rather than decorative: `mount` ignored
  the type field entirely, and `blkid` printed a hardcoded `TYPE="ext4"`,
  which is an oracle agreeing with itself. `SYS_fstype` now probes the device
  — including by `UUID=`, which is how fstab names the root and therefore the
  only line that matters — and both `mountall` and `blkid` ask it. So the file
  and the device disagree, both tools tell the same story, and the file is
  visibly the odd one out.
- **[done]** **root mounted read-only and stuck that way.** `fault_root_ro`.
  One word in one line of `/etc/fstab`. Nothing on the disk is wrong -- every
  hash matches -- and every service that keeps state dies the moment it first
  tries to write. This is what a half-finished repair looks like: somebody hit
  a dirty filesystem, mounted it `ro` to be safe while they investigated, and
  never put it back.

  It needed a real remount path, because `/` is deliberately not in the mount
  table: `/sbin/mountall` now performs the root remount from the fstab options,
  which is the moment a real init decides between `ro` and `rw`, and says so on
  the console. The clue is early in the boot and the symptom is late -- a
  respawn loop in whichever daemon writes first -- which is the shape of the
  real thing.
- **[done]** **disk full at boot.** A different mechanism from everything else
  here: nothing is corrupt, every hash matches, and `pkg verify` will tell you
  the machine is perfect. There is simply nowhere to put the next byte.

  ```
  rescue# df
  FILESYSTEM       SIZE     USED    AVAIL  USE%
  /dev/sda1     862K   862K   0K   100%
  ```

  The first thing to fail is syslogd, and the first failure is almost never
  the interesting one — what filled the disk is a log that has been growing
  quietly since March. The fix is not a package: it is `df`, then finding what
  is big, then deleting it.
- **[done]** **inode exhaustion — space free, still cannot create a file.**
  `fault_inodes`. A DIFFERENT DIAGNOSIS from a full disk, which is the whole
  reason it exists: `df` reports plenty of room, every hash matches, verify
  says the machine is perfect, and nothing can create a file. `df -i` is the
  only tool that answers it. The cause is the one it always is in life --
  something that writes a file per run and never tidies up.

  A filesystem now has an inode budget as well as a byte budget, and they run
  out independently.

## 5. fstab and mounts

- **[done]** an entry naming a device that does not exist stops the boot.
- **[done]** a mountpoint that is a file, not a directory.
- **[done]** an entry missing its type field, rejected with a line number.
- **[done]** `noauto` dropped from a removable drive, so the boot waits for a
  disc that is not in it.

  `/etc/fstab` had quietly stopped being read at all: the C parser went when
  the boot moved to real userland, and nothing replaced it. It is now
  `/sbin/mountall`, a real program run by `rc.boot`, and fstab is the single
  source of truth for what gets mounted — `rc.boot` no longer mounts anything
  by hand. Virtual filesystems (`none`, `proc`, `tmpfs`) are understood by the
  kernel, recorded in the mount table so `mount` and `df` show them, and
  nothing is layered over the path.
- **[done]** **something mounted OVER a directory that already had things in
  it.** `fault_mount_shadow`. The other half of the hollow-directory idea and
  the better half: nothing is deleted, nothing is corrupt, every hash matches,
  and the contents of `/var` are not the contents of `/var`, because a
  filesystem is sitting on top of them. The line was written on purpose, for a
  disk that never arrived, and it mounts the root device a second time in a
  second place.

  `mount` and `df` show it plainly; `ls /var` is a bewildering few seconds.
  The file `pkg verify` flags is `/etc/fstab`, which is correct in every
  particular except intent -- which is why this one is not a reinstall
  reflex.
- **[done]** **an entry naming a uuid no disk here carries.**
  `fault_fstab_uuid`. The bootloader found the root and handed it over, so the
  machine is running, and then fstab describes a disk that is not in it.
  `blkid` answers it in one command. Deliberately a different fault from
  zbl.cfg's wrong uuid: that one stops in the initrd before userland exists,
  this one stops in `mountall` with the machine half up, and the file to fix
  is the other one.
- **[done]** **`nofail`, which is the difference between a fault and
  housekeeping.** An fstab entry for a disk that is not in the machine stops
  the boot; the same entry with `nofail` prints a line and carries on.
  `mountall` honoured `noauto` and had never heard of `nofail`, so a perfectly
  ordinary line for a drive in a caddy was fatal. It is now both a real fault
  (without the word) and one of the decoys (with it) -- an alarming line on
  the console of a completely healthy machine.

## 6. Libraries and ABI — the biggest missing layer

- **[done]** **binaries declare NEEDED libraries with versions and the loader
  checks them.** This one addition unlocks the next four.
- **[done]** **a bad libc upgrade.** Everything dynamically linked stops
  working at once, including the tools you would use to fix it. The rescue
  medium is the only way back, which is exactly why it exists.
- **[done]** **a package built for the wrong architecture**: the ELF loads,
  the machine code is not ours, and it faults on execution.
- **[done]** **a library present but the wrong soname version, so only SOME
  binaries break.** `fault_bad_libz`. Until now every guest binary declared
  exactly the same dependency, so any library fault broke the whole machine at
  once and the ticket was over in one step. `libz.so.1` is now needed only by
  the programs that compress what they write — httpd, postfix, auditd, links —
  so an old zlib leaves the web server and the audit trail dead while ssh,
  cron, udev, ntp and the firewall run perfectly.

  That partial pattern is the puzzle. Everything dead points at libc; two
  unrelated services dead and the rest fine asks what those two have in
  common. `libz.so.1` was also, before this, a file nothing on the machine
  ever read.
- **[done]** **`ldd`**, which is what makes the whole of this section
  readable. It resolves through `/etc/ld.so.conf` in order and reads the
  dependency list out of the ELF through the same code the loader uses, so it
  cannot disagree with what happens when you run the program. It ships with
  libc, as it does on a real distribution, and on the rescue medium — which is
  the copy you need when the disk's own libc is too broken to run anything.
- **[done]** `/etc/ld.so.conf` missing a path, so a library that is installed
  is not found.
- **[done]** **a dangling symlink in the library path.** `fault_dangling_lib`.
  `libc.so.6 -> libc-2.38.so` where the target was removed by a failed
  upgrade. `ls /lib` shows the library, in the right place, with the right
  name; `stat` says there is nothing there. The loader says `cannot open
  shared object file`, which is a different sentence from `version not found`
  and means a different thing. libz makes it partial, libc makes it total and
  the rescue medium is the only way back.
- **[done]** **two versions of the same library, and the loader picks the wrong
  one.** `fault_lib_shadow`, and the best of this batch. NOTHING IS MISSING
  AND NOTHING IS CORRUPT: the correct library is exactly where it belongs and
  is exactly right. There is an older one in `/usr/lib`, and a search path
  reordered to look there first -- which is what happens every time a vendor
  tarball is unpacked and somebody makes it work.

  `ldd` is the whole diagnosis, and this is why it prints the path it
  resolved to rather than just a verdict:

  ```
  rescue# ldd /usr/sbin/httpd
      libc.so.6 => /lib/libc.so.6 (2.38)
      libz.so.1 => /usr/lib/libz.so.1 (1.2)  -- TOO OLD, this program needs 1.3
  ```

  `pkg verify` flags `/etc/ld.so.conf`, which reads exactly like a deliberate
  local edit because it is one, and `pkg owns` the stray copy and nothing
  does. The repair is the ORDER, not the file.

## 7. Packaging and repositories

- **[done]** **a misconfigured repository pulling incompatible packages.**
  `/etc/pkg/repos.d/main.repo` names a channel and `pkg upgrade` fetches
  whatever that channel serves. Point it at `testing` and the libc that
  arrives is 12.0's — perfectly valid, correctly signed, and nothing on the
  machine is linked against it.

  The reason this is the best puzzle in the catalogue so far: **`pkg verify`
  reports the file as CHANGED and `pkg reinstall` fetches the same wrong
  version straight back.** "4 files restored" and the machine still will not
  boot. The fault is three lines away in a config nobody thinks to look at,
  and the fix is to correct the *source* and then reinstall.
- **[done]** **an interrupted upgrade.** `fault_half_upgrade`. The power went,
  or the disk filled, or somebody hit ctrl-C. Some of a package's programs are
  the new build and want a libc this machine has not got yet; the rest are the
  old build and run fine.

  The signature is unlike anything else here. A corrupted binary is ONE file.
  A bad library is EVERY binary at once. This is several files of ONE package
  changed together, all consistently, all deliberately -- because they really
  were installed on purpose, just not all of them. The fix is to finish the
  upgrade or roll it back, not to edit anything.
- **[was-todo]** an interrupted upgrade: half the package's files are the new
  version and half are the old. `pkg verify` shows a scatter of CHANGED files
  in one package, which reads very differently from one damaged file.
- **[todo]** a held/pinned package that will not upgrade, so a dependency is
  permanently unsatisfiable.
- **[todo]** two packages owning the same path and fighting over it.

## 8. init, rc and services

- **[done]** stray unit file no package owns — `pkg verify` is *clean* and the
  boot still stops. The fix is realising nothing owns it and deleting it.
- **[done]** required service whose exec is missing or not executable.
- **[done]** dependency that never comes up; dependency cycle.
- **[done]** **broken or misconfigured getty/login: the machine boots
  perfectly and there is no way to log in.** `/sbin/getty` validates the
  account it is about to hand the machine to — the entry has to be in
  `/etc/passwd`, and the login shell has to exist and be executable. `login`
  is now its own boot stage, because a machine that is *running* and cannot be
  logged into is a different problem from one that would not start.
- **[done]** **a service in the wrong runlevel, so it is missing without
  anything reporting an error.** `fault_wrong_runlevel`. Nothing failed and
  nothing was tried: the unit is present, correct, enabled and healthy, and it
  belongs to runlevel 5 on a machine that boots to 3. The trap is that
  `enabled: yes` is right there in the file, which is the line everybody
  reads; the word "runlevel" appears only on the console. The other half of
  the fault is the same mistake from the other end -- `rc.3` entering runlevel
  5, where half the service set does not belong.
- **[done]** **a unit pointing at a path the program has never been at.**
  `fault_exec_path`. The binary is present, correct, executable and exactly
  where its package put it; the unit names the directory the program lives in
  on the distribution the unit was copied from. `pkg verify` flags the unit
  and not the binary, which is the clue: what is wrong is the pointer.
- **[done]** **two services ordered after each other.** `fault_dep_cycle`.
  Neither is broken, neither will ever start, and each unit on its own is
  completely reasonable. Reading one file tells you nothing; reading two tells
  you everything.
- **[done]** **ordered after something that is not installed at all.**
  `fault_after_ghost`. Not disabled and not in another runlevel -- there is no
  such service on this machine, either because the unit came from a box that
  had one or because the unit it waited for was deleted. svcinit says which:
  "waiting for network" and "waiting for network -- and no unit by that name
  is installed" are twenty minutes apart.
- **[done]** **svcinit says WHICH KIND of failure it was.** "failed to start"
  was the same five words for a unit pointing at a path that does not exist, a
  binary a hardening script had disarmed, a library at the wrong version and a
  daemon that read its config and gave up -- four different afternoons behind
  one sentence, and it is the last line the console prints, which is the line
  a player reads first. The kernel already distinguished them and handed back
  the reason; nothing looked at it. Now: `not found`, `present, and not
  executable`, `will not load -- check ldd on it`, `started and would not stay
  up`.
- **[done]** **a unit ordered after something that is disabled**, so it waits
  forever for a thing that is never coming. `fault_dep_disabled`. Nothing is
  corrupt: one file changed `enabled: yes` to `enabled: no`, which reads
  exactly like an administrator switching something off on purpose, and the
  damage is in every unit ordered after it. `svc` shows the dependents DEAD
  with no reason and `pkg verify` points at the wrong service; the boot log is
  the only place the truth appears, which is what the boot log is for.
  svcinit now says WHY the thing being waited on never came -- disabled, wrong
  runlevel, not installed, or failed -- because "waiting for net" on its own
  is the most confusing line an init system can print.
- **[todo]** `/etc/inittab` respawning something that exits immediately —
  the "respawning too fast" loop.
- **[done]** **pid 1 told to run the wrong script.** `fault_inittab_target`.
  Somebody was testing single-user mode, or the runlevel scripts were being
  reorganised. `/etc/inittab` is two lines long and one of them is now a path
  that does not exist, so the machine stops before any of userland has run and
  the console has almost nothing on it -- which is itself the diagnosis: a
  boot that dies this early died in init, and init reads one file.
- **[done]** **an installer patched the boot script.** `fault_rcboot_need`. A
  vendor package dropped a `need` line into `/etc/rc.boot` for an agent that
  was never installed, or that somebody tidied away afterwards. rc stops at
  the first failure, on purpose, so the machine dies at a line that has
  nothing to do with booting, and the fix is to take the line out rather than
  to install anything.
- **[done]** **the wrong file copied over a program.** `fault_wrong_binary`. A
  deployment that pushed the wrong artefact. The binary is a real, valid,
  runnable program -- it is simply a different one, so the service starts,
  does that program's job in half a millisecond, and exits, over and over,
  until the machine gives up on it. The console fills with the output of
  whatever it actually is in the middle of the boot, which is the loudest and
  strangest evidence in the game and points straight at the file.

## 9. Accounts and permissions

- **[done]** wrong shell in `/etc/passwd` — a shell that used to exist, one
  that never did, a rename meant to be temporary, or the field left empty.
- **[done]** the root account missing from `/etc/passwd` entirely.
- **[done]** **`/etc/passwd` and `/etc/shadow` out of step.** `fault_no_shadow`.
  The machine boots perfectly, every service is up, and there is no way in:
  the password lives in the other file and root has no line in it, which is
  what half a user migration leaves behind. Invisible in `/etc/passwd`, where
  everybody looks first, because `/etc/passwd` is perfect.
- **[done]** **one extra colon in a passwd line.** `fault_passwd_fields`. The
  line still parses, with the right name, the right uid and the right home.
  Every field after the typo has shifted one to the left, so the login shell
  is now the home directory, and the machine says -- quite correctly -- that
  root's login shell `/root` is not a program. A sentence that makes no sense
  until you count the colons. getty had to learn that a directory is not a
  shell; before that the account looked fine and the machine was quietly
  unusable.
- **[done]** **the hardening sweep.** `fault_hardening_sweep`. A script from
  somebody's laptop walks a directory and takes the execute bit off anything
  it does not recognise. The bytes are perfect, so `pkg verify` says `mode`
  and NOT `changed` on a scatter of files across several packages -- a verify
  signature unlike anything else here, where one word in the output IS the
  diagnosis and the repair is `chmod`, not a reinstall. Ticket 8841 in the
  previous administrator's notes is exactly this and says so.
- **[done]** **permissions on a DIRECTORY rather than a file, for writing.**
  `fault_ro_dir`. `/run` is not deleted and not unreadable -- everything in it
  lists and reads perfectly. It cannot be written to, so every daemon that
  publishes what it loaded fails at the same moment for the same reason and
  the console reads as though the whole service set has gone mad at once.
  Creating a file is a write to the DIRECTORY, which the kernel now enforces;
  before that a directory's mode meant nothing except traversal.
- **[done]** a directory whose mode stops traversal, so everything under a
  perfectly healthy tree is unreachable. `fault_dir_mode`. This is the answer
  to a playtester's complaint that the game is recipe-following: no manifest
  lists a directory, so `pkg verify` cannot name the culprit. It reports the
  files inside as UNREADABLE rather than CHANGED, which is a genuinely
  different signal -- the content is right, the way in is not -- and joining
  those two facts up is the deduction the fault exists to ask for. The kernel
  now enforces traversal, which it did not before.
- **[todo]** ownership wrong on a spool or state directory, so one daemon —
  and only that one — cannot start.
- **[done]** **a state directory deleted outright.** `fault_missing_dir`.
  Rejected once for a good reason — no package owned `/run` or `/var/log`, so
  `pkg reinstall` could not put back something nothing had shipped, and the
  solver scored 0/10. **Packages now record the directories they own**, the
  way rpm and dpkg both really do: a `dir` line in the manifest, checked by
  `pkg verify` for existence and mode, restored by `pkg reinstall`. syslog
  owns `/var/log`, cron owns `/var/spool/cron`, ntp owns `/var/lib/ntp`, base
  owns `/run`, `/tmp` and `/var/cache`. The solver now scores 10/10 on it.

## 10. Network

- **[done]** name resolution broken while the address still works: `chmod 000
  /etc/hosts` and DNS still answers; break `resolv.conf` too and only IPs work.
- **[todo]** a default route that is wrong, so local names resolve and nothing
  else does.
- **[todo]** an interface renamed by a udev rule, so the config configures a
  device that no longer exists under that name.

## 11. The system itself

- **[done]** **long-lived processes.** A service is no longer a file that gets
  stat-checked: it is a real program that starts, reads its configuration, and
  **keeps running** with its cpu and memory intact for the rest of the boot.
  `ps` is a picture of a live system rather than a history. There is no
  scheduler and no preemption — a daemon gets a slice of instructions when the
  system ticks — which is cooperative multitasking and a great deal less than
  a kernel, and enough.
- **[done]** signals. `kill -HUP` and `-TERM`, delivered by being left pending
  until the daemon next looks — there is no preemption here, so that is the
  honest promise, and it is enough for "re-read your configuration". Time
  passes between shell commands so the signal is actually seen.
- **[done]** `restart: on-failure` honoured, with the respawn loop that
  follows. A daemon that dies is brought back; five failures in a row and the
  system says so and stops trying, which is what every real init does and for
  the same reason.

  ```
  nft: /etc/nftables.conf: no ruleset -- refusing to start
  kernel: nftables died -- exited immediately with status 1, restarting (4)
  kernel: nftables respawning too fast, giving up on it
  ```
- **[done]** a config that EXISTS and does not say the one thing its daemon
  needs. Every daemon validates a required directive, so a half-finished edit
  — the line commented out and never restored — starts the service, fails it,
  and loops.
- **[todo]** **"it boots, the firewall is just not running."** A NON-critical
  daemon in a respawn loop leaves the machine UP, which is a nastier ticket
  than one that will not boot and one the breaker cannot currently express,
  because a ticket here is defined as a machine that fails to boot. Needs the
  ticket contract widening to "the machine is not healthy", with `svc status`
  as the way to see it.
- **[done]** pipes. `a | b | c` runs each stage to completion with its output
  as the next one's input — no concurrency, which is right, because these are
  filters and a filter that has not finished has nothing to say. `pkg verify |
  grep CHANGED` and `ls /etc | wc` both work. Builtins cannot be stages
  (`cd` changes this process and there is nothing to pipe it to), so `echo` is
  now a real program as well as a builtin.
- **[todo]** redirection for child processes — still only `echo` can redirect,
  because handing a child a file descriptor needs a real fork/exec.
- **[todo]** globbing and quoting.
- **[done]** `/var/log/messages` is written by a real syslogd at every boot, so
  `grep` over it is a diagnostic. More of the boot should log to it.

---

## 12. Faults that leave the machine UP

**The ticket contract is now "is it healthy", not "does it boot".** A machine
that reaches a login prompt with a service dead is a ticket, and about one in
six now is. The bench says so where it will be read, after the console has
already scrolled past whatever gave up:

```
node-4823 login:
[UP at target, but 1 service(s) are not running]
services that should be running and are not:
  httpd          gave up after repeated failures
```

`svc` is how you see it, and the solve gate requires health rather than a
boot, so a repair that leaves a service dead is not a repair.

- **[done]** a non-critical daemon in a respawn loop. "It boots, the firewall
  is just not running." The console scrolls past it and nothing complains
  afterwards — except the bench, at the end.
- **[done]** **the document root is not there.** `fault_docroot`. The config is
  valid, the daemon is fine, the machine boots to a login prompt, and the web
  server is dead because the directory its configuration names has been moved
  or deleted. `/srv/www/README` has been telling anyone who read it to check
  exactly this -- and it was a lie for several sessions, because httpd read
  `DocumentRoot` and never looked at it. **Daemons now touch what their
  configuration points at:** httpd stats its document root, auditd opens its
  trail, ntpd writes its drift file. A daemon that does not touch what its
  config names cannot be broken by pointing it somewhere wrong, and two
  entries in this catalogue were describing faults that did nothing at all.
- **[done]** **the disk filled with something that is not a log.**
  `fault_cache_full`. The same 100% and a completely different search: a log
  that ate the disk is one enormous file and `wc` finds it in a second; a
  package cache that ate the disk is four hundred ordinary files, none of them
  remarkable, and the only way to see it is to look at the directory rather
  than at the files. `find /var -type f` is the tool.
- **[todo]** a service that starts and then dies an hour later, so the boot
  console is clean and `ps` is the only evidence.
- **[todo]** two services that both start, where one silently depends on the
  other having finished, and the order is wrong only sometimes.
- **[done]** **a daemon running with a stale config because nothing reloaded
  it.** Nothing is corrupt, `pkg verify` is clean, `svc` says running, and the
  machine does not do what its configuration plainly says it does:

  ```
  [UP at target, but 1 service(s) are not right]
  services that are not doing what they are configured to do:
    httpd          running with a stale /etc/httpd/httpd.conf
                     on disk:  Listen 8080
                     running: Listen 80
  ```

  Every daemon publishes what it actually loaded to `/run/<name>.state`, so
  the gap between intention and behaviour is a thing the machine can notice
  rather than something only a person could spot. **The fix is a signal, not
  a file:** `kill -HUP <pid>`. The fault has to be applied *after* boot by
  construction — reboot and the daemon reads the new file and it evaporates,
  which is exactly why it is so miserable to diagnose in real life.
- **[done]** a log filling the disk. See §4 — it now takes syslogd down with
  it, because a logger that cannot write its log is not running whatever the
  process table says.

## 13. Things that are wrong before you arrive

- **[todo]** a machine that was already broken when it was handed over, and the
  customer's story is about something else entirely — the reported symptom and
  the fault are unrelated. Real support is full of this and nothing here
  currently produces it.
- **[todo]** a "fix" a previous technician applied that is itself the fault: a
  package reinstalled that clobbered a local edit, a permission opened up to
  make something work.
- **[todo]** two faults where repairing the first makes the second *look* like
  a new problem you caused.

## The cost of being sloppy

A playtester's sharpest criticism: *"nothing stops you reinstalling every
flagged package — I did exactly that on four tickets and it worked every time
with zero penalty. There's no cost to being sloppy, so the detective framing
is optional, not enforced."*

Two answers, both of them what a real system does:

1. **`pkg reinstall` keeps modified configuration**, names it, and tells you
   how to look at it. `--force` overwrites. This is dpkg's conffile behaviour
   and it exists for exactly this reason: a package ships a default, an
   administrator makes a decision, and a reinstall that silently reverts the
   decision has destroyed somebody's work to fix a problem that was elsewhere.
2. **The bench reports collateral damage.** A machine that boots is not the
   whole job; if local configuration no longer survives, it says which.

The sloppy path still exists, because it must — sometimes the config really is
the fault. It is now a deliberate act with a name and a receipt.

## 13b. The machine you cannot reach

- **[done]** **an air-gapped machine.** David's idea, and it is the best kind
  of hard: nothing is wrong with the *fault*, everything is wrong with your
  *access*. A secure site, a factory floor, a box that was never on a network.
  `rcon connect` finds no route and says so, and the only terminal you have is
  the person standing in front of it.

  `ask type <command>` really runs that command on their machine and they read
  back what they see -- the last few lines, because that is what is in front
  of them, and they say when something has scrolled off. Every character is a
  character the machine printed: they are a slow, narrow pipe, not an
  unreliable one, so the ticket stays fair.

  It changes what a good question is. On a normal ticket you run six commands
  because they cost nothing. Here each one costs a round trip through somebody
  who does not know what any of it means, so you think first. One ticket in
  five.

  Proven solvable end to end by dictation alone: ask them to insert the rescue
  disc, ask them to power cycle, `ask type mount /dev/sda1 /mnt`, `ask type
  pkg --root /mnt verify` -- and they read back the real verify output.

## 14. What the previous administrator's notes say

`/home/nomowner/notes.txt` is ten numbered lessons and **every one of them
describes a fault the breaker really produces**. That is the rule for the
easter eggs: flavour that is also true. A player who reads the home directory
is straightforwardly better at the job afterwards, which is the only kind of
hidden content worth hiding.

The notes are therefore a checklist for this catalogue in reverse — if a note
describes something the breaker cannot do, either the fault is missing or the
note is a lie. Note 9 (a directory bound over `/etc`) was a lie for several
sessions and a playtester followed it into a dead end. It is a real fault now.

## 15. The decoys, which are half the game

There are now **27** of them (`install_local_edits` in `image.c`,
`./tools/check-decoys.sh` walks every one against a 20-machine health run),
and the count is not decoration: a fault set that doubles while the decoy set
stands still turns `pkg verify` straight back into an oracle, because the one
unfamiliar line in the output is the answer again.

The second batch was chosen so that the FRIGHTENING files are represented,
since those are the ones a player reinstalls on sight:

- `/etc/fstab` with a `nofail` entry for the backup caddy — it prints an
  alarming line on the console of a completely healthy machine, and the single
  word that makes it harmless is in the options column.
- `/etc/rc.boot` with an extra `echo`. rc.boot is where two real faults live,
  which is exactly why a harmless edit to it is worth having.
- `/etc/passwd` with a service account somebody added.
- `/etc/inittab` with a comment explaining a mistake its author made once.
- `/etc/ld.so.conf` with a vendor path **appended** — the difference between
  this and `fault_lib_shadow` is the ORDER of two lines and nothing else.
- `/etc/pkg/repos.d/main.repo` with everything changed except the channel.
- `/etc/nftables.conf`, `/etc/logrotate.conf`, `/etc/nomde/panel.conf`,
  `/etc/services.d/sshd.svc` — ordinary tuning, in files that matter.

One of the original seventeen wrote `/etc/default/postfix`, which no package
installs, so the edit silently did nothing: a decoy of a decoy. A fault that
cannot fire is worth checking for as carefully as a repair that cannot fail;
the same bug had one entry of `fault_wellmeant` doing nothing for months.

## What makes a fault good

1. **The evidence is on the machine.** The console says what it tried and what
   it got; the tools can confirm it. Nothing requires guessing.
2. **The obvious move is not always right.** `pkg reinstall` should sometimes
   be wrong, sometimes impossible, and sometimes destructive.
3. **It is diagnosable from a stage.** Where the boot stopped narrows it to a
   package or a subsystem. `man boot` maps stage to package.
4. **It rewards knowing the system** rather than memorising a table. The best
   faults so far are the ones where `pkg verify` is *clean*: the boot sector,
   the stray unit, a bad namespace bind.
