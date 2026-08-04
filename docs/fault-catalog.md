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
- **[todo]** config points at a kernel that was removed by a cleanup or by an
  autoremove after an upgrade. The classic: `/boot` filled, the upgrade half
  finished, and the entry now names a kernel that is gone.
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
- **[todo]** initrd built on a machine with different hardware — has the wrong
  storage driver, not none.
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
- **[todo]** `/var` or `/usr` on a separate filesystem that does not mount, so
  the system comes up with a *hollow* directory where a populated one belongs.
  Everything that reads from it fails oddly and nothing is corrupt.

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
- **[todo]** a dangling symlink in the library path: `libc.so.6 -> libc-2.38.so`
  where the target was removed by a failed upgrade.

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
- **[todo]** an interrupted upgrade: half the package's files are the new
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
- **[todo]** a service enabled in the wrong runlevel, so it is missing without
  anything reporting an error.
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

## 9. Accounts and permissions

- **[done]** wrong shell in `/etc/passwd` — a shell that used to exist, one
  that never did, a rename meant to be temporary, or the field left empty.
- **[done]** the root account missing from `/etc/passwd` entirely.
- **[todo]** `/etc/passwd` and `/etc/shadow` out of step.
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
