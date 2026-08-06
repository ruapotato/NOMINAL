# D37. Power is a wall, and a serial lead stops pretending

## What was asked for

The owner walked into his own tower for the first time in a while and hit
both halves of this within minutes.

On the building:

> "The server in the default rack isn't booting, but it's also not plugged
> into any power. Also the default desk station isn't plugged into power or
> an Ethernet that goes anywhere. **Each room should have at least one power
> outlet.** ... We also need power logic, so you plug in servers into the
> actual wall. Potentially have a way to view the mini map for the entire
> area and request/order additional power for a fee. That then installs the
> power outlet into that room."

And on the handset:

> "When you connect a debugger to a system, it should be pretty much a blank
> prompt. Like as if you connected a serial connection to any random piece of
> hardware, it doesn't show you a past history. And **if the thing's not
> powered on, it shouldn't offer a prompt at all. If it's not booting, it
> shouldn't offer a prompt at all.** Potentially maybe a no-connection prompt
> that gives you the option to attempt to power cycle whatever you're
> attached to. That way you can watch boot up messages."

These are one job. A box with no power gives you nothing down a serial lead,
and that nothing is the diagnosis.

## What was wrong, precisely

`SiteDev.powered` was a flag a box carried around with it. `site_power()`
checked that the thing had a button and then set the flag, wherever the box
was standing and whatever was or was not behind it. So:

- a server standing in an empty cupboard with no lead in the back of it
  booted when you pressed the button;
- `site_room_watts()` counted a switch at sixty watts whether or not
  anything was feeding it, because nothing was;
- and therefore **a box that would not start had exactly one possible
  cause**, which meant a serial lead into a dead box could never be a
  diagnosis of anything. There was nothing for it to diagnose.

The console half was worse than that, and in the opposite direction. `plug
<box>` on a machine that was powered but had **failed to boot** handed over
`root@srv7#` and passed every line to `kernel_run()`. The gate asserted the
lie: `and the box is really down, not down only in the boot log` was checked
by looking for the string `DOWN at services` in `plug`'s output -- a status
line printed immediately above a root prompt on a machine with no init on it.
That assertion is now strictly stronger and is quoted in full below.

## The model

**A room has sockets and they run out.** Not a resource bar: a count of
holes in a wall, `site_room_outlets_built()`, decided by the kind of space
the building generator made and its area. The shape of the table is the whole
argument:

| room | outlets | why |
|---|---|---|
| MDF | 8 | the building's frame room, wired for a frame |
| tenant's server room | 6 + area/10 | the one space built to hold equipment |
| **comms cupboard** | **4** | a twin socket and a spare off a spur |
| plant | 4 | |
| goods in | 2 | |
| riser | 1 | a shaft with one maintenance socket |
| toilet | 1 | the shaver socket |
| office / residence / retail | 2 + area/8 | wired for PEOPLE, so plenty |
| corridor, stair, lift lobby, lobby | 1 | the cleaner's socket |
| lift shaft | 0 | not a room anybody walks into |

The owner asked that every room have at least one, and every room a person
can walk into has one. The asymmetry is the design: **the rooms built to hold
equipment have a handful of sockets on a spur and the rooms built to hold
people have them everywhere**, because that is how buildings are wired -- and
it is why the decision this makes lands in a comms cupboard and never on a
floor of desks. On the gate seed the floor-1 cupboard and a floor-1 office
are both about 35 m² and get 4 and 6 respectively; the kind is doing the
work, not the size, and `--sitecheck` asserts that comparison rather than the
numbers.

Four in a cupboard is the owner's own sentence sitting exactly on the limit:
*"a cupboard with three switches and a server in it is a decision and not an
assumption."* Three switches and a server is four. The fifth box is the
decision.

**A box is plugged in or it is not**, `SiteDev.mains`, and a box that is not
cannot be switched on at all: `site_power()` refuses with `SITE_ENOMAINS`.
An appliance has no button, so for a switch and a router **the plug is the
button** -- which is what `site.h` has said about a switch since the pivot
and which nothing could act on until there was a plug. An unplugged switch
has its ports administratively down, so the box at the far end of every one
of them sees the link drop, which is what makes an unplugged switch
diagnosable from somewhere other than the cupboard it is in.

**More power is buyable, and it is money rather than days.** `outlet <room>`
puts another socket in for `200 + 8/metre` of tray run back to the riser the
power comes up -- geometry `bld_cable_all()` already knows, so a cupboard
against the shaft is 264 on the gate seed and a far corner of a let floor is
more. Charged now, in for good, never refunded, the same as a jack.

**And the circuit is finite.** A room takes as many again as it was built
with and then `SITE_ECIRCUIT`: it is on one final circuit and there is no
more power to bring into it. That is the limit money cannot move, and it is
what stops the floor plan being scenery -- some rooms are places you put
equipment and some are not.

## The console

`plug <box>` now asks one question -- **is there a login on the other end of
this wire** -- and there are three answers.

**No power.** No prompt. The lead goes in, nothing comes back, and the
session enters `SES_NOCON`, whose prompt is `srv3 (no console)> ` with no `#`
and no `$` in it, because both of those are a claim that something is reading
what you type. Nothing is. What the state takes is the four things a person
standing at that rack can actually do -- the button, the plug, the live
medium on the cart, and putting the lead back -- plus `look`, `where` and
`outlet`/`outlets`. Everything else gets

    (nothing. srv3 is not running anything that could read that.)

which is not "no such command": the command may well exist, on a machine that
is not running it.

**Powered, and it did not boot.** Also no prompt, and this is the one the old
code got most wrong. There is no login because nothing got far enough to
start one. **And the boot log is not replayed**, because the owner's words
were *"it doesn't show you a past history"* and he is right: that log went up
this wire before the lead was in it, and a wire has no memory. So the way to
see the boot messages is to make some. `power off` then `power on` from the
no-console prompt boots it and the messages come up the line as they happen,
which is exactly the "attempt to power cycle whatever you're attached to,
that way you can watch boot up messages" that was asked for. `rescue <box>`
is the other door, and the initrd's own last line already tells you to use
it.

### The line this turns on: WHERE the boot stopped, not whether it finished

The first cut of this said "no `boot.running`, no prompt", and
`--eventcheck` caught it immediately: it dropped from 83/83 to 77/83.

The reason is worth the whole record. `boot.running` means the machine
reached `BOOT_TARGET`. A machine whose `netd` would not stay up stops at
`BOOT_SERVICES` -- and that machine **has a login on it**. Its root
filesystem is mounted, `init` came up, `getty` is on the console, and a
service is dead. That is an ordinary Linux box with a broken unit, and
refusing a console on it would have taken away the console the entire
break-fix half of this game is played through -- `pkg verify`, `pkg diff`,
`pkg reinstall --force` -- and sent the player to the rescue medium for a
machine sitting at a login prompt.

So the rule is the stage, and the line is **whether a root filesystem was
ever mounted**:

| stopped at | is there a login? | why |
|---|---|---|
| firmware, bootloader, kernel, initrd | **no** | no root filesystem was ever mounted, so there is no userspace to have a getty in |
| init | **no** | `/sbin/init` did not start |
| services | **yes** | multi-user was reached and a unit died. This is a real box with a broken service |
| login | **yes** — see below | userspace is up; the getty could not hand the terminal to an account |
| target | yes | it is up |

**And `login` is where this model is deliberately more generous than a bare
serial tail, which is worth saying out loud rather than hiding.** A box whose
`/etc/passwd` lost root's shell field prints `getty: root: no login shell in
/etc/passwd` and really cannot hand a terminal to anybody; a person with
nothing but a null-modem lead would get that error and no further. The line
is drawn at the root filesystem instead, for three reasons: the machine has
*booted* -- which is the thing the owner's rule is about; the cart is not
nothing but a lead, it is what `rescue` and `eject` drive with the reset
button under the technician's thumb; and every case he actually met falls on
the other side of that line anyway. The cost is one obscure fault class where
the console is kinder than the hardware. It is priced here rather than
smuggled.

It also means **the tower and the break-fix bench now disagree**, and that is
recorded rather than left to be found: `kernel_console_dead()` in
`core/kernel.c` draws the same line at TARGET and refuses a shell on anything
short of it, which is right for a customer's machine reached over a network
from another site and stricter than a technician standing at the rack. If
that disagreement is ever resolved it should be resolved in one function
rather than two, and it should be resolved with `--eventcheck`'s three
blackout casualties in front of whoever does it, because two of them do not
reach target and their repair is `pkg reinstall` typed at a console.

Both halves say which they are, out loud. A box down at services prints

    [DOWN at services]
    there IS a login on this line: the root filesystem mounted and init came
      up. What did not is a service, which is why you can log in and repair it.

and `power <box> on` on one says *"it came up and a service did not"* rather
than the old, and for that case wrong, *"no kernel got far enough"*.

Getting this wrong in either direction is expensive, and `--eventcheck` --
which was out of scope and not edited -- is what proved it, twice, with
numbers:

    plug gives a shell iff boot.running          --eventcheck  77/83
    ... iff it also reached SERVICES             --eventcheck  79/83
    ... iff a root filesystem was mounted        --eventcheck  83/83

`bb_recover` runs `pkg verify` over a `plug` on each blackout casualty, and
on the gate seed two of those three do not reach target: `srv1` loses
`/etc/udev/rules.d/50-default.rules` and stops at **services** because netd
will not start without it, and `srv2` has `/etc/passwd` cut short and stops
at **login** because getty cannot find root's shell. Those two numbers are
the two boundaries above, measured rather than argued.

**Running.** A shell, unchanged, because there really is a login on the other
end. When a power-cycle from the no-console prompt succeeds the state
promotes itself and says so -- *"a login prompt comes up the line"* -- which
is what a serial console does when the machine on the far end starts
printing.

Note what was NOT touched: `plug` on a healthy box still prints
`[UP at target]`, because `--eventcheck` reads that string to prove the live
medium booted, and because it is true at the moment it is printed.

## The first five minutes, played

`./build/bf --towersh 7008`, from the MDF, no editing. The build is a router
and a core switch in the MDF and a floor's kit in the floor-1 cupboard; the
fifth box into that cupboard is where it lands.

    f1 comms cupboard> look
    f1 comms cupboard #35, 35 m2
      kit in this room:
        sw1          switch24  0/24 ports used   next free port sw1:0
        srv1         server    no address, 2 ports   next free port srv1:0
        srv2         server    no address, 2 ports   next free port srv2:0
        sw1b         switch24  0/24 ports used   next free port sw1b:0
        srv3         server    no address, 2 ports  [NOT PLUGGED IN -- no lead to the wall]
      ways out: #25 corridor, #34 riser
      power: 4 outlets on the wall, 0 free -- nothing else in here will run

    f1 comms cupboard> power srv3 on
    you press the button on srv3 and nothing happens. No fans, no lights.
      IT IS NOT PLUGGED INTO ANYTHING. There is no lead from srv3 to a wall
      socket, so its power button does nothing at all.
      f1 comms cupboard #35 has 4 outlets and 0 free.
      from here: `outlet` puts another socket in this room for 264, then
      `mains srv3 on`. `outlets` is every room in the building and what is free.

    f1 comms cupboard> plug srv3
    the lead goes into the console port on srv3 and nothing comes back.
      A serial line carries what the far end sends. srv3 is sending nothing.
      IT IS NOT PLUGGED INTO ANYTHING. ...
      `unplug` puts the lead back on the cart.

    srv3 (no console)> outlets
      power, one floor
      room                     built  added  in use  free   another
      f1 comms cupboard #35        4      0       4     0   264
          srv3 is NOT plugged in -- `outlet` here, or carry it somewhere with a socket free

    srv3 (no console)> outlet
    a sparky runs a spur off the board: #35 has 5 outlets now, 1 free.
      264 paid, 53682 left. It does not come out again and nothing is refunded.

    srv3 (no console)> mains srv3 on
    srv3 is in a wall socket. 0 of #35's 5 left.
      it is still switched off. `power srv3 on`.

    srv3 (no console)> power on
    you press the button on srv3.
    zbios 1.4  --  node-4015
    ...
    NomnixOS 11.4
    srv3 login:

    [UP at target]

    a login prompt comes up the line. You are root on srv3. `unplug` to leave.
    root@srv3# cat /etc/hostname
    srv3

Arriving, a server that will not start, why, and the fix -- and the last two
lines are the thing the owner said already worked and had to keep working:
the serial lead is a real shell on a real machine reading its own disk.

## The judgement calls

### Is existing kit retro-plugged, or does the player arrive to a maze?

**Putting a box down in a room plugs it in, if there is a socket free.** This
is the call the brief flagged and it is the one that decides whether the
feature is a mechanic or a tax. Three reasons:

1. **It is what a person does.** Nobody sets a switch down in a cupboard and
   then forgets the lead. The act being modelled is *carrying a box into a
   room*, and plugging it in is part of that act, not a second decision.
2. **The failure is the feature, and the success is not.** What the game has
   to be honest about is the room with no hole left. Making the player type
   `mains` four times a floor to reach that moment would be four keystrokes
   of nothing per interesting one.
3. **It is what keeps `--loadcheck` honest.** The difficulty curve is played
   through a real `Session`, and a change that required a new verb in every
   build would have meant editing the scenarios -- which is precisely how a
   calibration gets quietly weakened. Both scenarios are **untouched** and
   the table is unchanged; see the numbers below.

Ordered kit is the exception and it is not an exception to the rule, it is
the rule applied: `site_order()` leaves a delivery in goods in **unplugged**,
because a pallet under a roller door is not a rack and the thing is still in
its box. That is also, exactly, the state the owner found: a machine that has
never been plugged in anywhere.

`mains <box> on|off` exists for the case that is left -- shuffling boxes in a
full room, and the window's faceplate -- and it is the verb, not the default.

### What does a machine losing power mid-day do?

**Unplugging a running machine is the same event as the blackout**, and it is
not softened. `site_unclean_stop()` lives in `core/siteday.c` beside
`the_mains_fails()` and uses the same `pf_deal()` and the same
`breaker_powerfail_as()`; `core/breaker.c` was not touched. The machine on the
end of it cannot tell the difference, and this project's whole claim is that
it never has to.

**And a battery is what a battery is for.** In a blackout `nomups` sees the
utility come back in nineteen minutes and the machine never notices. Here it
does not come back, so the battery does its other job: it holds the load long
enough to shut the machine down in an orderly way. Clean stop, nothing to
check in the morning, and **the player chose the moment** -- which is the
first time in this game the two hundred and twenty pounds pays for something
the world did not do to them.

The one place this is refused rather than performed is `carry`. Picking a
machine up starts with pulling its plug out, so `carry` on a *running* server
is refused and points at `power <box> off`, which is the shutdown and costs
nothing. A player should have to say they meant a blackout, in the verb that
means it. An appliance has no button, so picking a switch up switches it off,
which is what everybody already knows about a switch.

### Per-room or per-wall?

**Per room**, and the reason is D23's rule rather than simplicity. The
`Building` gives a room a kind and an area and no wall geometry any of this
code can address, so a socket with a position would be a coordinate the
**model** had to invent and the **window** had to be the authority on -- the
exact inversion this project exists to avoid. It is also not a decision: a
lead reaches any wall of one room, so which wall it is on changes nothing a
player chooses. The count is the decision. Where the faceplate is drawn is
the window's business and it may put it anywhere.

### Why an outlet is money and not days

A jack already owns "a trade has to come and that is the clock", and D23's
own record says why a second copy would be worth nothing: *"with nothing else
different, waiting is free once the player learns to order early, and the
mechanic evaporates."* Power is also the one thing that must never be a maze
in the first five minutes -- a player who has just carried a server up eight
floors into a full cupboard needs a way out of it today. So the socket is
money, now, and the limit that cannot be bought out of is the circuit cap.

## The gates

    rm -f build/*.o build/bf && make -s bf

| gate | at HEAD | now |
|---|---|---|
| `--sitecheck` | 496/496 | **550/550** |
| `--loadcheck` | 35/35 | 35/35 |
| `--netcheck` | 262/262 | 262/262 |
| `--eventcheck` | 83/83 | 83/83 |
| `--health` | 20/20 | 20/20 |
| `--mancheck` | 57/57 | 57/57 |
| `--building 200` | 200/200 | 200/200 |
| `--solve 60` | 60 / 60 | 60 repaired, 60 handed back |
| `--askcheck` | 2844/2844 | 2844/2844 |
| `tools/check-decoys.sh` | 37/37 | 37/37 |
| `make test-cpu` | pass | pass |

**`--loadcheck`'s two scenarios were not edited and the curve did not move.**
Both builds still deliver into comms cupboards and the MDF and every box in
them still plugs itself in, because there is a socket free -- which is the
point of the auto-plug decision above. The naive build is comfortable on its
first floor, visibly working hard at four tenancies and fallen over at seven;
the planned build carries all nine. Identical to HEAD.

**Shown failing.** A clean `git archive HEAD` checkout in
`.../nominal-power-agent-d37-baseline`, with only `core/sessioncheck.c`
copied in:

    clean HEAD + the new session gate     495/519   (24 failing)
    with the feature                      550/550

`core/sitecheck.c`'s half cannot be built against HEAD at all, and that is
the strongest statement available about it -- there is no model there to
check:

    core/sitecheck.c:476: implicit declaration of 'site_room_outlets_built'
    core/sitecheck.c:500: 'SiteDev' has no member named 'mains'
    core/sitecheck.c:511: 'SITE_ENOMAINS' undeclared
    core/sitecheck.c:515: implicit declaration of 'site_mains'

The session half was written to build against a tree with no power model in
it, deliberately, the same way D31's seat gate asks the PROMPT rather than an
enum member: it fills the cupboard by carrying switches in **until `look`
says the room has none left**, which is the only way a player can know
either. At HEAD that loop never terminates early and every assertion after it
fails, which is the honest reading.

### The assertions that were added rather than weakened

`--sessioncheck`'s *"and the box is really down, not down only in the boot
log"* asserted that `plug` on a box down at services printed
`DOWN at services`. **That assertion is unchanged and still passes**, because
under the stage rule that box really does have a login. What was added beside
it is the sentence that stops the prompt being misread:

    and the lead says why there is a prompt on a box that is down

And the case the assertion never covered -- a box that stopped before there
was a root filesystem -- is now played end to end in the new section, by
deleting the kernel from the box's own shell and cycling it:

    a box whose kernel is gone comes up and stops, with the reason on the line
    and a lead into it offers no login, because there is nowhere for one to be
    and it does not replay the boot log, which the wire has no memory of
    and it points at the two things that are left: boot it again, or the medium

`check_shell`'s nineteen-line script had `power pc1 on` **above** `move pc1
f2.office`, switching a machine on while it was still on a pallet under the
roller door. The two lines are swapped and the reason is in a comment. No
assertion changed.

## What the window will need

Everything is `session_line()` and `site_cmd()`, so D23's rule holds: the
model is what says where an outlet is and what is in it.

- `site_room_outlets_built/`/`site_room_outlets()`/`site_room_outlets_max()`
  /`site_room_outlets_used()`/`site_room_outlets_free()` -- how many
  faceplates to draw in a room and how many have leads in them.
- `site_room_outlet_dev(room, nth)` -- **which box is in the nth used socket**,
  so a lead can be drawn from a faceplate to a machine. Which socket is which
  is not modelled and deliberately is not: the window may assign them however
  it likes as long as it is stable.
- `site_dev_mains` is `SiteDev.mains`, for the "not plugged in" badge.
- `site_outlet_price(room)` for the price on a button, and `site_outlet(room)`
  behind it. `site_dump_outlets(floor, out)` is the whole-area power map the
  request asked for, per floor or `-1` for the building.
- Verbs, all three in `site_verb_name()` and in the tower help, both gated:
  `mains <box> on|off`, `outlet [<room>]`, `outlets [<floor>|all]`.
- **The window's own start pre-orders kit into goods in**, and that kit is
  now unplugged and switched off, which is the honest state of a pallet.
  Anything the window pre-places into a *rack* must be `move`d there and will
  plug itself in; anything it leaves in goods in will not start, and the
  serial lead into it will say so in words.

## What was NOT done

- **A UPS is still not a device in a room.** It is a flag on a box and it
  does not occupy an outlet, which means a box on a battery draws one socket
  where a real one draws two. Fixing that is a catalogue change and it would
  move the cupboard arithmetic that this record just set, so it is named here
  rather than smuggled in.
- **Nothing draws amps.** An outlet is a hole, not a load: four servers in a
  cupboard is four sockets whatever the servers are. The room's real limit on
  what four servers do to it is already the heat model, which is a different
  and better answer, and adding a second capacity number in watts would be
  two models of one thing.
- **A tenant's desk is not on the landlord's outlets** -- it is their machine
  in their room on their own socket, which is the same sentence the heat
  model has always made about a desk. So the owner's *"the default desk
  station isn't plugged into power"* is answered for the player's own kit and
  is still, for a tenant's desk, a question for the window rather than the
  model.
- **No `--loadcheck` scenario ever runs out of sockets**, so nothing yet
  measures a sixty-day tower where the cupboards fill up. That is a
  playthrough rather than a gate, and it is the same gap D23's addendum names
  for the jack.
- **There is no way to take an outlet out**, deliberately, and therefore no
  refund. Same shape as the jack, same reason.
