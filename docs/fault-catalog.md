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
- **[todo]** filesystem type wrong in fstab (`ext4` vs something else), so the
  mount fails with "unknown filesystem type".
- **[todo]** root mounted read-only and stuck that way, so everything that
  wants to write at boot fails in a cascade.
- **[todo]** disk full at boot: services that need to write a pidfile or a log
  fail one after another and the *first* error is not the interesting one.
- **[todo]** inode exhaustion — space free, still cannot create a file.

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
- **[todo]** a library present but the wrong soname version, so only *some*
  binaries break — the ones built against the newer one.
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
- **[todo]** a unit ordered after something that is disabled, so it waits
  forever for a thing that is never coming.
- **[todo]** `/etc/inittab` respawning something that exits immediately —
  the "respawning too fast" loop.

## 9. Accounts and permissions

- **[done]** wrong shell in `/etc/passwd` — a shell that used to exist, one
  that never did, a rename meant to be temporary, or the field left empty.
- **[done]** the root account missing from `/etc/passwd` entirely.
- **[todo]** `/etc/passwd` and `/etc/shadow` out of step.
- **[todo]** a directory whose mode stops traversal, so everything under a
  perfectly healthy tree is unreachable.
- **[todo]** ownership wrong on a spool or state directory, so one daemon —
  and only that one — cannot start.

## 10. Network

- **[done]** name resolution broken while the address still works: `chmod 000
  /etc/hosts` and DNS still answers; break `resolv.conf` too and only IPs work.
- **[todo]** a default route that is wrong, so local names resolve and nothing
  else does.
- **[todo]** an interface renamed by a udev rule, so the config configures a
  device that no longer exists under that name.

## 11. The system itself

- **[todo]** **long-lived processes.** `spawn` runs children to completion, so
  there are no daemons, no signals, no scheduler, and "the service crashed and
  restarted twice" cannot happen. This is the single biggest structural gap.
- **[todo]** pipes and redirection for child processes in the shell.
- **[todo]** globbing and quoting.
- **[todo]** a `logs` surface worth grepping: real `/var/log/messages` written
  during boot, so `grep` is a diagnostic rather than a toy.

---

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
