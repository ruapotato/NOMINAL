# D45. What the world can actually do to a machine

A blind measurement of the late game, run over 30 seeds x 120 days of played
station (3,600 station-days, plus two counterfactual variants), asked one
question: **on a station a competent player has built and is running well, how
many days pass between events that require them to do something?**

The answer was not the one the design assumed.

## What was measured

| event | per 100 station-days |
|---|---|
| POWERCUT | 4.08 |
| DOWN_DIRTY | 1.67 |
| CREW_DARK | 0.83 |
| DISK_WARN | 0.39 |
| LINK_WARN | 0.33 |
| DISK_FAIL | 0.14 |
| LINK_SLOW | 0.14 |
| DISK_BOOT | 0.03 |
| **HEAT_WARN** | **0.00** |
| **HEAT_TRIP** | **0.00** |

Zero heat events in 7,200 station-days. The hottest room ever observed was at
22% of what it can shed.

The mains is a renewal process: first cut uniform on days 20-30, then gaps
uniform on 17-29 days, flat for ever after — mean 4.67 cuts in 120 days. That
part is working as designed and is a reasonable tempo.

## The three findings, in the order they matter

### 1. There were two watt tables, and the heat model was reading the wrong one

`core/site.c`'s `KIT[]` priced the conduit model. A private switch in
`core/siteday.c` priced the heat model. Nine boxes appeared in both with
different numbers — switch24 90 W against 60, router 120 against 45,
rackserver 700 against 520 — and two were wrong outright: the player's own
workstation was missing from the heat switch entirely and fell through to
zero, and the ISP handoff produced 15 W of heat from a box on somebody else's
meter.

Fixed in the commit *"Two watt tables, and the box that heated its room by
nothing"*, gated by `check_one_nameplate()`, which asks the catalogue, the
conduit tree and the room's temperature the same question and requires the same
answer box by box.

This is the twentieth-odd instance of the defect this project keeps finding:
**one fact computed in two places.** It is worth noting how this one was
found — not by reading the code, but by measuring an outcome (heat is
unreachable) and asking why.

### 2. A lost sector could only land under /etc

This is the big one, and it is why the world could produce so few of the shapes
the break-fix half of this game knows how to diagnose.

`breaker_bad_sector_any()` filtered its candidates with
`strncmp(f->path, "/etc/", 5) != 0`. On a pristine machine that is **twelve
files**. A platter does not know what a directory is, so this was never a
physical rule; it was a stand-in for "somewhere the damage will show".

**The obvious repair is wrong, and it was measured too.** Allowing any
package-owned file takes the set from 12 to 133 — and 88 of the 121 it adds
are manual pages, package documentation, and the previous administrator's home
directory. Shipping that would have been worse than not widening at all:

- A bad sector in a man page makes this project's own documentation lie. Every
  technical claim in this game is meant to be true of this machine, and
  `--mancheck` runs the examples in those pages to prove it.
- A bad sector in `/usr/share/doc` is a `pkg verify` line with no symptom
  behind it. A player who chases two of those learns that verify output is
  noise, which is the opposite of what the tool is for.
- `/home/nomowner` is the diary, the postmortem and the notes. That is the
  story, not the machine.

So the rule is neither a path prefix nor "anything". It is **whether something
on the box reads the file at runtime**: 44 files rather than 12, and what it
adds has consequences you can see without a package tool — the initrd, the
kernel modules, the desktop's launchers, and `/srv/www/index.html`, which is
the page a web-host tenancy is paid for serving.

Widening it moved three files into reach that stand between the player and a
shell, so `boot_critical()` grew to match: the initrd, `/lib/modules/` (which
holds virtio_blk, the driver that finds the disk the machine is on) and
`/usr/lib/sysinit/`. They are not out of reach — they are held for the SECOND
loss, the disk nobody replaced, where a boot that stops at the stage that is
really wrong is the entire point.

`--eventcheck`'s `check_sector_reach()` gates it by measuring the set rather
than trusting the rule that built it: that it is wider than /etc, that no
manual page or README or page of the diary is in it, that the first and second
draws do not overlap, and that nothing the boot chain reads is reachable by the
first one. Falsified both ways — the naive widening fails claim two at 88
prose files, and the old /etc rule fails claim one at 0 elsewhere.

While doing it, `--eventcheck` was found keeping its own hand-written copy of
`boot_critical()`'s list, with a comment admitting as much. The predicate is
exported now; the gate asks the real one and proves the consequence separately
by watching the box fail to reach target.

## What this record does NOT claim to have fixed

**The world still cannot call `machine_break()`.** Grepping every call site, it
is reached only from the break-fix harnesses in `core/bfmain.c` and from
`core/serve.c`'s customer mode. The station's own weather produces five damage
shapes — `pf_deal`'s three powerfail casualties plus a first-sector and a
boot-sector bad block — against the 62 structural faults `./build/faulthist`
proves are reachable from the break-fix side.

Widening the sector draw makes each of those five shapes land in more places,
which is a real improvement in variety and in how often the damage has visible
consequences. It does not change the number of shapes. That is a larger piece
of work and it needs its own record, because the honest version of it is not
"call machine_break from the weather" — that would be a designer hiding a
fault, which is exactly what D23 moved away from. It is: **find the causal
chains the station model already contains that end in a structural fault, and
follow them.** The conduit trip is the most promising, because it is the
player's own doing: an overloaded run trips, everything behind it goes dark,
and `site_mains_sync()` already deals the machines behind it a dirty stop. That
chain works today and nothing measures how often a player meets it.

**And a UPS still buys out the blackout permanently, for £220.** The
measurement is unambiguous: with a battery a mains failure is
`SEV_UPS_HELD` and no work at all. Whether that is a fault or a correct reward
for a good purchase is a design question this record does not settle, but it
should be settled rather than left to be discovered again.
