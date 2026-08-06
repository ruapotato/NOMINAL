# D28. One cause, several diagnoses

## The sentence this record exists for

A playtester reached day 34, was asked whether the game is fun, and said:
*"Yes -- from about day 26. Before that it is admin."* What turned it up was
a mains failure on day 30 that took three servers down and left them all on
two strikes, one day from a run-ending sweep. Their verdict on that hour:
*"That single event was worth the preceding twenty-five days."*

And then:

> **"Three servers down from one cause was three instances of the same
> puzzle. Three servers down where one is a heat trip, one is a worn disk and
> one is a truncated fstab would have been the game this engine is obviously
> capable of."**

They were right, and the engine was already capable of it. `core/breaker.c`
holds sixty-two fault types and `--solve 60` proves every one findable and
repairable. `the_weather()` in `core/siteday.c` could reach three of them, and
a blackout dealt the same one -- `fault_unclean_shutdown` -- to every box it
took down. So the second and third repair of a bad morning were the first one
typed again with a different hostname.

## What changed

### 1. A blackout deals a different casualty to each box

`breaker_powerfail_as()` replaces the old boolean `writing`. Four things an
unclean stop can honestly leave, all of them machinery this file already had:

    PF_CLEAN   the journal replays and nothing is missing
    PF_TRUNC   the packaged file it was writing is half there  (the old one)
    PF_CONF    a daemon's own configuration stops mid-file
    PF_SVC     a file created seconds before the stop is gone entirely

All four mark the filesystem dirty, because all four are the same machine
stopping dead. **So the first move is the same on every box -- `fsck` -- and
the second move is different on each.** That is the difference between three
puzzles and one puzzle three times, and it is why the clean case matters: the
box that was idle is the control that proves the player's job is to READ the
tools rather than reinstall everything on everything.

**Why round-robin and not a die per box.** A die is what produced the
complaint. Three independent draws out of three casualties deal all three the
same about one morning in nine and two the same about half the time. What is
physically true is not that each machine rolls -- it is that different
machines were doing different things at 04:12 -- so `pf_deal()` guarantees the
difference and the SEED decides where the deal starts and which file inside
each casualty goes. A blackout across three working servers is three different
mornings, always. Which server gets which is written down nowhere the player
can read.

A box that moved no frames yesterday is not dealt one: it had nothing in
flight, so it is dirty and complete. That rule is unchanged, and it is what
the `idle` control in the gate is built on.

The heat trip deals from the same hand, because a thermal shutdown is the same
event: two boxes tripping in the same cupboard on the same afternoon used to
come back identical too.

### 2. A disk nobody replaces stops being handled gently

`breaker_bad_sector()` keeps the boot chain's own files out of reach so the box
still comes up and can be worked on. That is right the FIRST time and wrong the
second. By the time a disk loses a second sector it has logged SMART warnings
for a fortnight, taken a file, been named in `events`, and `disk <box>` has
been a hundred and forty pounds away the whole while -- fifteen days of notice
before the first loss and fifteen more before the second.

So `breaker_bad_sector_any(m, r, boot_too)` lets the second one take
`/etc/fstab` or a service unit, the boot chain stops at the stage that is
really wrong, and the repair is the one the boot log names. That is D23's
*a disk nobody replaced -> the truncated file the boot log names*, thirty days
of warning in front of it. `disk <box>` resets `lost` as well as `wear`.

### 3. Heat ages the disk

A box in a room over what it can shed wears one extra point a day, and another
for every forty per cent over. Every field study of the things agrees heat is
what kills them.

This is here for a second reason, and it is the other half of the playtester's
verdict. **Days 0-25 were "admin" because the world's only early lever was the
heat warning, which said a thing, waited three days, and then usually did
nothing at all** -- a room at 110% warns for ever and never trips. It now costs
something from the first day it is over: not a machine down, but a disk that
reaches its warning weeks early and starts naming itself in `events`. A small
consequence, soon, out of a decision the player made when they chose the room.

Measured in the gate: identical servers, same day, no work either of them, one
in a 145% cupboard and one in the MDF -- three days of disk against one, after
one day.

### 4. A run with no margin left in it

D23 named it and nothing ever did it: *a cable run past interference -> errors
that only appear under traffic.*

Copper of every grade here carries a hundred metres and the netstack stops
carrying anything past it. The last ten of those metres are what the standard
spends on margin. A run over ninety metres takes CRC errors under load,
complains in `events` and in the syslog of any box at either end of it for
days, and then retrains itself to a hundred megabits -- which `load` and
`show` print because `net_port_rate` really caps the port.

Both terms of the rate matter and the numbers say why:

    a 95 m drop to ONE desk        ~2% busy   x6  =   12/day: 50 days to a
                                                       word, 150 to a retrain
    a 95 m uplink under a FLOOR    ~30% busy  x6  =  180/day: a word on the
                                                       fourth day, a hundred
                                                       megabits on the tenth

The first one is honest and is meant to be almost never: a desk pulls ten
megabits and a long run to one is a bad idea that takes half a year to become a
problem. The second is the shape of a real mistake -- **the floor's switch put
in the office with the desks rather than in the comms cupboard, home-run to the
core in copper** -- and in this seed's building the tray measures that run at
95 m. Nobody chose ninety-five. It is `bld_cable_all()` measuring f0 MDF to f3
office.

Played in the gate: named in `events` on day 25, retrained on day 34, the floor
behind it stopped getting its work done on day 35, and `uncable` plus a fibre
run put the port back at a gigabit. **The port rates for every live run are
reapplied from the catalogue every day**, so nothing is remembered on a port;
the memory is in the run, and a fresh run comes up at what the kit can do.

## The bad morning, played

`./build/bf --towersh 7008`, from 60,000, a planned tower: a vlan per floor on
subinterfaces of the router, a switch and a file server in each of three
floors' comms cupboards, fibre up the risers, cat5e to 118 desks. Six
tenancies in and all six served on day 29. Then:

    day 30: 6 in, 6 served, 118/118 desks addressed, 469/472 finished
            ** the building lost mains power at 04:12 and had it back by 04:31.
            ** srv1 went down with the power and has not been switched back on.
            ** srv2 went down with the power and has not been switched back on.
            ** srv3 went down with the power and has not been switched back on.

    events
      day 24   srv2 is logging reallocated sectors. Its disk is going.
      day 29   the disk in srv2 lost a sector after 29 days. It had been warning.
      day 30   the building lost mains power at 04:12 ...

All three stop in the initrd on the filesystem and all three take the same
first move. After `rescue`, `fsck /dev/sda1`, `eject`:

    srv1   udevd: /etc/udev/rules.d/50-default.rules: cannot read
           kernel: udev respawning too fast, giving up on it
           [DOWN at services]
           pkg verify:  udev  /etc/udev/rules.d/50-default.rules  MISSING
           -> pkg reinstall udev

    srv2   getty: root: no login shell in /etc/passwd
           [DOWN at login]
           pkg verify:  shadow  /etc/passwd  TRUNCATED
                        98 byte(s) of 118 are gone from the END
           -> pkg reinstall --force shadow
           and, separately, logrotate /etc/logrotate.conf CHANGED -- the sector
           the disk lost the day before, which is a different sentence out of
           the same tool

    srv3   crond: /etc/crontab: no jobs -- refusing to start
           [UP at target], with cron in a respawn loop
           pkg verify:  cron  /etc/crontab  TRUNCATED
                        304 byte(s) of 419 are gone from the END
           -> pkg reinstall --force cron

Three boxes, one cause, three boot stages, three different second moves -- and
the console named the damaged file itself on all three, which is the thing that
keeps it diagnosis rather than guessing. Repaired inside the day: day 31 was
six tenancies in, six served, no strikes.

## The judgement calls, made rather than asked

**How many things may go wrong at once.** Up to three casualties from one
blackout, plus whatever the disks and the heat were already doing, and no
cap beyond that. The reasoning: the complaint threshold is a third of the let
tenancies rounded up (never fewer than three, D27), the player has three days
before a strike matures into a complaint, and **every one of these is repaired
in the same morning by somebody who reads the tools**. The played transcript
above is the evidence -- three servers, three different faults, all six
tenancies served again the next day. What ends a run is not the number of
faults; it is not reading `events`. The cap that matters already exists and is
the one the player can see: `service` prints how many complaints end the run.

**Whether the early days should be quieter.** No -- they should be *smaller*,
and they are now, which is the opposite fix from the one that suggests itself.
The first blackout is still deliberately late (day 20-30) for the reason D25
gave: a mains failure in the first fortnight lands on a building with one
switch in it. What was wrong was that nothing else could happen early either.
Heat now costs something from the first day a cupboard is over, and a marginal
run starts logging errors within a week of carrying a floor. Both are local,
both are cheap to put right, and both are consequences of a decision the player
made rather than weather. That is what "the world does more, sooner, in smaller
ways" means here.

**Every new cause is avoidable or survivable by a decision the player was
given**, which is rule 4 of the five in `siteday.c` and the line between this
and a tax:

    a blackout        `ups <box>`, 220, and the box is up in the morning with
                      the receipt in its own syslog
    a second sector   `disk <box>`, 140, any day in the thirty days of SMART
                      warnings and one `events` line that came before it
    heat              carry a box somewhere with air in it. The cupboard says
                      what it can shed and what is in it, in watts
    marginal copper   do not run ninety metres of it under a floor. The metres
                      and the price are printed at the moment the money leaves,
                      and `uncable` plus fibre is the fix afterwards

## The numbers

`--eventcheck` went from 58 assertions to 80. The new gate builds the tower
above -- three floors, three tenancies, a server on each floor plus a box on a
battery and a box with no work to do -- and runs it to the blackout, then walks
`rescue`/`fsck`/`eject`/`pkg verify`/`pkg diff`/`pkg reinstall` on every one of
the five boxes in the words a player types.

**Against a clean `git archive HEAD` checkout with only the new
`core/eventcheck.c` copied in: 65/80.** With the change: 80/80. The fifteen
that fail at HEAD are the fifteen this record is about:

    `pkg verify` names a file on each of them that was fine the night before
    three different files, and more than one KIND of damage between them
    one of them lost a file outright and another had one cut short
    the repair `pkg verify` named puts every one of the three files back
    and all three boot to target again afterwards
    fifteen days later, unreplaced, it loses another one
    and the day says this one was under something the boot reads
    and `pkg verify` names a file the boot chain itself reads
    `events` says it is taking errors under load, days before anything else
    and then the run retrains itself down to a hundred megabits
    which is what the port really clocks now, not a note in a log
    `load` prints the new speed against the port's own name
    and `events` names both ends of the run and how long it is
    the floor behind it stops getting its work done
    and a disk in the hot room is already ageing faster than one that is not

The gate is written to COMPILE against HEAD -- it reads the far end's port
speed rather than the site's new book on the run -- so the two numbers are the
same file measured twice.

Everything else, measured after:

    --eventcheck  80/80        --solve 60   60 repaired, 60 handed back
    --loadcheck   35/35        --netcheck   196/196
    --sitecheck  295/295       --health      20/20
    --mancheck    56/56        --building   200/200
    --askcheck   2850/2850     check-decoys  37/37
    test-cpu      pass         asan --eventcheck  clean

`--loadcheck` is untouched and every assertion in it still holds: the naive
tower's runs are all short and its risers are all under ninety metres, so
nothing in the difficulty curve moved.

`--sitecheck` reads 295 rather than the 293 this branch started from. Two of
those are another agent's work in `core/sessioncheck.c`, arriving in the same
tree; nothing here added or removed a site assertion.

## What was deliberately NOT done

**No new fault was written.** Every casualty a blackout can now deal is a
`fault_*` that has been in `core/breaker.c` since the break-fix game and has
been proven by `--solve` the whole time. The work was entirely in giving the
world a way to reach them, which is what D23 said the world was for. The one
piece of genuinely new damage is the marginal run, and that is not a file on a
disk at all -- it is a port rate, and its evidence is the port counters.

**The blackout schedule was not touched.** It was tempting: an earlier first
mains failure is the cheapest way to make day 10 eventful. It is also the
cheapest way to make day 10 a tax, because the decision that survives it -- a
battery -- costs two hundred and twenty pounds out of a sixty thousand pound
float that is mostly spoken for in the first month (D27). The early-game fix is
heat and copper, which are consequences rather than weather.

**Nothing was added to `SiteDev` that a player cannot see.** `lost` is the only
new field on a box and it is the escalation counter; there is no hidden
per-box difficulty state anywhere, and there must never be.

**The marginal-run rule has a shape this building barely reaches.** Ninety
metres of copper under real load exists here in exactly one form -- a floor
switch put in an office and home-run to the MDF -- because the risers in this
generator are thirty-five to forty-two metres. A taller building or a wider
floor plate will meet it more often. It is honest at its numbers and it is
rarely met at this size, and that is written down rather than tuned, because
tuning it would mean punishing a cable length that is genuinely fine.

**`show <box>` still prints "the circuit" where it means "the port"** on a port
whose kit is slower than its cable. D27 named that wart; `core/netstack.c` was
out of scope again. A retrained run now makes it more visible, not less.

**No second blackout was played.** The transcript stops after the repair on day
31. What day 52's mains failure does to a tower whose disks are a fortnight
older has not been measured, and the interesting question there -- whether a
worn disk and a blackout on the same morning compound into something unfair --
is the next thing to play rather than the next thing to assert.
