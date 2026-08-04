# D22. The boot log is the primary instrument, and the machine gets bigger

Two directions from David, in the same session, that turn out to be one
direction.

## What he said

> "I think pkg verify needs to be drowned out more by simply having more
> packages. If you're not sure what is broken, a broad give-me-all-packages-
> with-changes should be kinda slow, and noisy by default. Perhaps you can
> only verify 1 package in 5 seconds, so a wide verify would take a long time,
> and give you plenty of false leads. We may need a script that takes a
> default image and 'lives in it' so that configs drift in a working way, to
> kinda preload the pkg changes, before we corrupt/break the image. It can't
> be the only tool you use. We also really need to make a boot log the user
> can get to and review — that should be the main point of info. pkg verify
> should be something you use once you kinda know what layer is broken."

> "We might look at a minimal Debian install, and try to duplicate what it
> needs to boot. The more complex but familiar this is the better, up to a
> real Linux install. Really I want to clone a Linux distro as best we can, or
> perhaps even Hamnix (not a Linux distro but super cool and my IP)."

## The diagnosis behind it

Three blind playtests in a row have said the same thing in different words:
the loop is `pkg verify` → `pkg diff` → `pkg reinstall` → `boot`, and
`pkg verify` hands you the answer before you have thought about anything. The
last one put it plainly: *"pkg verify is an oracle."*

Every fix so far has attacked the **signal** — local edits so verify reports
innocent changes too, decoys with rotating wording, faults verify is blind to,
and now `fault_wellmeant`, where one deliberate-looking edit is the fault.
Those help and they are not enough, because verify is still the **first** thing
you reach for and it still costs nothing to run.

D22 attacks the **economics** instead. Make the oracle expensive, make its
output genuinely noisy, and give the player a better first instrument.

## 1. The boot log is the primary instrument

**This is the change that matters most.** A real administrator's first move is
not to checksum the filesystem, it is to read what the machine said while it
was failing. We have never had that: the boot output scrolls past on the
console and is gone, and there is no way to look at the boot that failed
*before* you got there.

- `/var/log/boot.log` — what the current boot said, written as it happens.
- `/var/log/boot.log.1` — the **previous** boot. This is the important one:
  the customer rebooted before calling, so the interesting boot is the one
  that already scrolled away.
- `dmesg` reads them, and both are readable from the rescue medium through
  `/mnt`, which is where you will actually be standing.

A log that only exists when the machine boots is no use to a machine that does
not boot, so it is written by the kernel as the boot proceeds, not flushed at
the end.

## 2. Verify becomes expensive and noisy

Verifying one package is cheap and precise. Verifying *everything* is a fishing
expedition, and it should feel like one:

- **A per-package cost.** A whole-system verify walks 28+ packages and takes
  real time. `pkg verify <name>` stays fast, because knowing which package to
  ask about is the skill being rewarded.
- **Noise by default.** With enough packages and enough lived-in drift, a wide
  verify returns a screenful of legitimate differences, most of them
  irrelevant. That is what it looks like on a real machine.

The intended shape: read the boot log, form a hypothesis about the *layer*
(loader, initrd, root fs, libraries, services, login), then verify the two or
three packages that layer implicates.

## 3. A machine that has been LIVED IN

Local edits are currently installed by a table of hand-written variants. That
is a decent imitation of drift, but it is still an imitation, and it is
bounded by how many I bothered to write.

The better version is a **script that boots a clean image and uses it**:
changes a nameserver, adds a host, turns a service off and on, edits a config
and reverts most of it, rotates a log, leaves a half-finished experiment in
`/tmp`. Whatever state that leaves behind is the starting point, and it is
drift no one designed. The breaker then corrupts *that*, so the fault lands on
a machine with a history rather than on a factory image plus one deliberate
edit.

## 4. Debian-shaped, up to a real install

The target is explicit: **duplicate what a minimal Debian install genuinely
needs to boot**, and stop only where the emulator does. Familiarity is a
feature — a Linux administrator should recognise nearly everything, and the
few places where it differs should be interesting rather than arbitrary.

Concretely, what a minimal Debian has that we do not:

- `/etc/init.d` scripts with LSB headers and dependency ordering
- runlevel symlink farms (`/etc/rc3.d/S20foo` → `../init.d/foo`), where a
  service enabled in the wrong runlevel is a real and very confusing fault
- `update-rc.d`-shaped tooling, and a `dpkg`-shaped conffile prompt
- `/etc/default/*` for per-service knobs, which we have started
- proper `/etc/apt`-shaped sources with suites and pinning
- `/etc/modules`, `modprobe.d`, and an initramfs built from a config
- `/etc/nsswitch.conf`, PAM-shaped auth stack, `/etc/securetty`
- `logrotate`, `cron.d`, `cron.daily`
- a package trigger/postinst step that can itself fail — half-configured
  packages are a Debian speciality and a genuinely good ticket

**On Hamnix:** it is David's IP and it is the shape we already borrow from.
NomnixOS stays the in-game system's name; where Hamnix and Debian differ, we
follow Hamnix, because that is the thing worth cloning and the thing he owns.

## Order of work

1. Boot log, `dmesg`, and previous-boot retention. *(the instrument)*
2. Per-package verify cost, so wide verify is a last resort. *(the economics)*
3. More packages, Debian-shaped, each genuinely necessary.
4. The lived-in drift pass replacing the hand-written edit table.
5. `/etc/init.d` + runlevel symlink farm, and the faults that live there.
