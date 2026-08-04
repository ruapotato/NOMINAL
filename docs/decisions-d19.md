# D19. Processes, namespaces, and the tools to debug them

Plan 9 where it earns its keep, Linux where familiarity matters. A Linux
sysadmin should sit down and be productive in a minute; the Plan 9 parts
should be the reason the system is *deeper* than Linux, not the reason it is
strange.

## The layers, bottom to top

Each is a real thing that can fail on its own, and each has a different
failure signature. That is what "enough layers to be a legitimate operating
system" means in practice: not decoration, but independent failure domains.

| layer | what it is | how it fails |
|---|---|---|
| cpu | rv64im, deterministic (D18) | illegal instruction, fault at a pc |
| kernel | syscalls over the machine's disk | ENOENT, EPERM, ENOEXEC |
| firmware / zbl / kernel / initrd | native, as on real hardware | bad magic, no such uuid |
| `/sbin/init` | rv64 binary: reads `/etc/inittab` | nothing to run |
| `/bin/rc` | rv64 binary: interprets rc scripts | unrecognised command, line N |
| `/etc/rc.boot`, `/etc/rc.d/rc.N` | text the above interprets | a damaged line |
| `/sbin/svcinit` | rv64 binary: starts `.svc` units | critical unit down, dependency never came up |
| services | `enabled`, `runlevel`, `after`, `critical` | degraded, or fatal if critical |
| `/sbin/login` | rv64 binary | — |

## Processes

Real ones, with pids, a parent, an exit status and an instruction count, in a
table that lives in the Machine. `/proc` is **synthesised from that table and
never read off the disk**, exactly as on a real system.

That last point is a deliberate property: corrupting the customer's filesystem
cannot forge a process, so `/proc` stays trustworthy when nothing else is. In a
game about deciding what to believe, having one surface that cannot lie is
worth more than one more thing that can.

```
  PID  PPID STATE     EXIT  INSTRUCTIONS  COMMAND
    1     0 exited       0          1512  /sbin/init
    2     1 exited       0          5766  /bin/rc
    3     2 exited       0          2993  /bin/rc
    4     3 exited       0         22204  /sbin/svcinit
    5     3 exited       0           360  /sbin/login
```

## Namespaces

Per-process, inherited at spawn, resolved by **longest prefix match** — which
is what NomnixOS's own `rc.boot` relies on when it binds `#c` at `/dev` and `#b`
at `/dev/blk`.

```
rescue# bind /etc /mnt
rescue# ns
/mnt /etc
rescue# cd /mnt
rescue# cat inittab          <- reads /etc/inittab
```

**Why this matters for the game.** A wrong bind is a fault where *nothing is
corrupt*. Every file passes `pkg verify`, and the machine still reads the wrong
`/etc`. The only way to see it is `ns` or `/proc/<pid>/ns`. That is the class
of problem that separates someone who understands the system from someone who
has memorised a checklist — and no other mechanic here produces it.

## The package database is on the disk

`/var/lib/pkg/<name>/{version,files}`, where `files` is `mode hash path` per
line. `/usr/bin/pkg` is a real program that reads it, hashes what is installed,
and compares.

Consequences, all of them wanted:

- the manifest can itself be damaged, and `verify` says so rather than
  reporting a clean system. A check that cannot fail is worthless.
- every finding names the package that owns it, because the next thing the
  player does is reinstall something and the point is reinstalling the *right*
  thing.
- **the repository is off the machine.** `pkg reinstall` pulls pristine bytes
  through a syscall, which is why it can repair a disk with nothing good left
  on it — and it is what a package manager actually does.

```
rescue# pkg verify
openssh         /usr/sbin/sshd
                 mode is 0000, package shipped 0755
1 file(s) differ. `pkg reinstall <package>` puts them back.
```

## Critical versus degraded

A `.svc` unit may declare `critical: yes`. `udev`, `syslog` and `net` are;
`sshd` and `nomde` are not. A non-critical service that will not start is
reported and stepped over.

This was a bug worth fixing rather than a preference: chmod on `sshd` was
taking the entire boot down, and any Linux admin would have called that wrong.
"The box is up but ssh is broken" and "the box will not boot" are different
problems and the system must not conflate them.

## Remote play is the primary interface

`build/bf --serve 7777` gives every connection its own machine and a rescue
shell. The socket and the desktop both call `kernel_run()`, which spawns
`/bin/sh` **on the machine** — so there is exactly one implementation of what a
command does, and the GUI cannot drift from the socket or acquire powers a
remote player lacks. The GUI is a nicety; this is the game.

## The rescue medium

A second, complete, package-backed system on its own medium (`/dev/sr0`). The
breaker only ever touches the customer's disk, so booting the rescue **always
works** -- which is the entire reason a live image exists, and the reason a
player is never stuck with a machine they cannot get a shell on.

The customer's disk is `/dev/sda1`, present as a block device whether or not
anything on it functions. So the real workflow is the real workflow:

```
rescue# mount /dev/sda1 /mnt
rescue# for i in dev sys proc; do mount /$i /mnt/$i; done
rescue# chroot /mnt
rescue# pkg verify
kernel-default  /boot/initrd    MISSING (symlink)
rescue# pkg reinstall kernel-default
kernel-default: 10 files restored
rescue# boot
node-5107 login:
```

Four transforms turn a path into a filesystem and an offset, in the order a
real kernel applies them: cwd, then chroot, then namespace bindings, then the
mount table. Getting that order right is what makes `chroot /mnt` mean the
customer's `/etc` rather than the rescue's.

### Bugs this shook out, all of them real

- **The mountpoint was being resolved through the mount table**, so once `/mnt`
  was mounted, `/mnt/dev` collapsed back to `/dev` and the bind landed on the
  wrong filesystem. A mountpoint is a name in the process's *view*, not a path
  on the underlying disk.
- **Children did not inherit the chroot root**, only cwd and namespace. So
  `chroot /mnt` worked and then every command ran outside it.
- **`$i` was expanded before the `for` parser ran**, so the loop body was built
  with the variable still unset. `mount /$i /mnt/$i` became `mount / /mnt/`,
  which mounted the root over everything and made every later lookup nonsense.
  `mount` now refuses to mount at `/`.
- **`pkg verify` could not see symlink damage.** The manifest skipped symlinks,
  so a deleted `/boot/initrd` reported a completely clean system. Symlinks are
  now recorded by the hash of their target, and `readlink` exists so the guest
  can check them.
- The rescue medium was installed with the *customer's* `rc.boot`, because
  `image_generated()` asks the machine which medium it is describing and had
  not been told yet. The one thing guaranteed to work was broken by
  construction.

## What is deliberately still missing

- no fork/exec split, no signals, no scheduler: `spawn` runs a child to
  completion. Enough for a boot chain, not enough for a daemon that stays up.
- no union directories (Plan 9's `bind -a`/`-b`). Longest-prefix binding only.
- no pipes or redirection in the shell.
- `/dev` is a directory, not a device filesystem.
