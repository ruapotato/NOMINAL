# D35. A tenancy occupies the rooms it leases

## The sentence this record exists for

From the day-30 playtest of the living tower:

> "A tenancy's twenty desks are all in one room. Tenant 1 holds eleven rooms
> including a server room; all twenty desks and all twenty people are in
> `#36`, and the other ten rooms are empty. **The building the letting agent
> describes and the building you walk through aren't the same building.**"

They were right, and the cause was four lines in `core/siteday.c`:

    for (int i = 0; i < t->drops; i++) {
        snprintf(nm, sizeof nm, "t%dd%d", t->tenant, i);
        int d = site_install(s, SDEV_DESK, t->room, nm);   /* <- t->room */

`t->room` is the FIRST room the tenancy holds. Every desk went into it. That
was invisible for months and stopped being invisible the moment another agent
seated a person at every desk: a room with twenty computers in it reads as a
data-entry hall, and the ten empty offices beside it read as a bug.

## Why this is not cosmetic

Copper is the metered resource -- D27 measured it at about 30% of spend, and
D32 landed `quote` so a player can price two rooms before paying for either.
With every desk in one room, **every drop in a tenancy is the same run**, and
the only geometric question a floor can ask is which cupboard the switch goes
in. There was no such thing as within-floor distance.

There is now. On seed 7008's floor 1, from that floor's own comms cupboard
`#35`, tenancy 1's twenty desks are:

    t1d14  f1 office #46   20 m   86 for cat5e
    t1d0   f1 office #36   55 m  106 for cat5e

Same tenancy, same drop, same grade, a different bill -- which is what `quote`
was built to let a player see before the money leaves.

## How the desks are placed

**Rooms that take people.** Offices, flats and shops. **A tenant's server
room does not**: it is the one room kind in the heat model built to hold
equipment (`sheds_per_m2` gives it 120 W/m2, six times a comms cupboard), and
nobody sits in it. A tenancy that holds one puts its people in the other
rooms and leaves the server room to be what the player carried a server into
-- which is also the room `web_origin_for` looks in. If a tenancy somehow
holds nothing but a server room, the desks go into `t->room` as before: a
desk in an odd room is better than a desk that does not exist.

**How many in each: the square metres, apportioned.** Hare quota with largest
remainders -- `floor(drops * area / total)` each, leftovers to the largest
remainders, ties to the bigger room and then to the lower room index. A
112 m2 office takes four times what a 28 m2 one does because that is what the
rooms are. Seed 7008, tenancy 1, 20 desks over 819 m2 of offices:

    #36  91 m2 : 2     #41  98 m2 : 2     #45  28 m2 : 1
    #37  28 m2 : 1     #42  98 m2 : 2     #46 105 m2 : 3
    #38  56 m2 : 1     #43  28 m2 : 1     #47 112 m2 : 3
    #40 112 m2 : 3     #44  63 m2 : 1     #39 server room : 0

Desks are installed in room order and numbered as they go, so `t1d0` is in
the tenancy's first room and the numbers walk the floor -- `desks 1` reads
like a walk rather than a list.

**THERE IS NO RNG IN ANY OF IT, and that is the whole determinism argument.**
D30 was bitten by a new draw taken from an existing stream: the trade roll
shifted `wants_server`, which shifted the letting queue, which moved every
tenancy's move-in day in every tower, which announced itself as three
unrelated blackout checks failing in `--eventcheck`. The safest new stream is
no new stream. Every term in the apportionment is this building's own square
metres, in integer arithmetic -- a remainder compared as a double is a
remainder that can compare differently on another compiler.

Verified rather than believed. `demand` over seeds 7000-7040, 1,457
tenancies, this build against a `git archive HEAD` checkout:

    0 differences in 1580 lines of output

Day, floor, tenant, trade, drops, wants and rent are identical to the byte on
every row of all forty-one towers.

## What it does to the money, played

`./build/bf --towersh 7008`, day 1, one `switch24`, `serve 1 sw1 cat5e`, run
identically against HEAD and against this build. The switch in the floor's
comms cupboard `#35`, then the same run with the switch carried into the
tenancy's own big office `#36`:

                      HEAD (all desks in #36)   this build (spread)
    switch in #35            2120                      1902
    switch in #36            1540                      1949

**At HEAD the answer was always the same: put the switch in the room with the
desks in it and pay 1540.** There was nothing to weigh, because the twenty
runs were twenty copies of one run. Now the cupboard is the better spot by 47
on this tenancy, and it is better for a reason a player can see -- the desks
are spread over eleven rooms and the cupboard is central to them while `#36`
is at one end. On a different tenancy, with a different shape of floor, it
will come out the other way. That is the decision this change exists to
create.

It is worth saying plainly that this made `serve` **cheaper** on the gate
seed's usual build, not dearer. Nothing was tuned toward either. `#36`
happened to be the far room.

## The load curve did not move, and here is why

    ./build/bf --loadcheck   35/35, and the output is byte-for-byte identical
                             to HEAD across all eighteen rows of both tables

`core/loadcheck.c` is untouched -- not one assertion weakened, not one demand
number moved. That is not luck and it is not a near miss:

- Both builds in the calibration serve each floor from **that floor's own
  comms cupboard**, so every run is a within-floor run.
- Within-floor tray metres are 20 to 55 m. The netstack's propagation term is
  `1 + metres / 250` microseconds, so the difference between the nearest and
  the farthest desk on a floor is **under one microsecond**, three orders of
  magnitude below the millisecond the world clocks in. Distance is a price in
  this game, not a delay, and the load model never saw this change.

What moved is the bill, which is where it belongs.

## The one new way to build it wrong, named rather than hidden

Copper stops carrying past 100 m. A tenancy's rooms can now be further from
the box than its first room was, so a naive tower that **home-runs desks to
the basement** can produce a desk that does not come up where HEAD's would
have. Measured over the same forty-one seeds:

    worst tenant room to its own floor's comms cupboard:   55 m
    worst tenant room to the MDF:                         116 m
    tenancies with a room over 100 m from the MDF whose
    FIRST room is under it:                     8 of 1457  (0.5%)

So: served the way the game teaches -- a switch in the floor's cupboard --
**nothing anywhere in forty-one towers is out of reach**, with 45 m of
margin. Home-run from the basement, one tenancy in two hundred now has a
desk that reports `PORT_TOOLONG`, which `show` prints in words and which
`--sitecheck`'s copper section has asserted at exactly 116 m since D23. A
build that was already wrong has one more honest symptom.

## The gate

`--sitecheck` gains one section, `check_desk_rooms`, of seventeen
assertions. It asserts the three things the change was FOR and the one thing
it must not break:

    the rooms they lease are the rooms they are in, and every one has desks
    no desk of theirs is in anybody else's room, or in their server room
    the split follows the square metres, and no two rooms are the wrong
      way round -- more area never means fewer desks, over the whole tenancy
    the nearest and farthest desk are a different run, and a different price
    `serve` still cables every one, every link comes up, none is too long,
      every desk is walkable to from the lift lobby
    the bill is every desk's own metres added up, which is not twenty
      copies of the nearest run
    and the same seed puts the same desk in the same room, every time

### Fails without it

The new gate compiles unchanged against HEAD -- it names no new type and no
new function -- so the falsification is literal: `git archive HEAD` into
`d35-desks-in-rooms-head`, this file's `sitecheck.c` copied over it, HEAD's
`siteday.c` left alone.

    HEAD + this gate:   463/471 site checks pass   (8 FAIL)
    this build:         496/496

The eight that fail at HEAD are the eight that are about the change:

    every room the tenancy leases to sit in has desks in it
    a bigger room takes more desks than a smaller one
    and twice the floor area takes at least twice the people
    and no two of their rooms are the wrong way round
    the tenancy's nearest and farthest desk are not the same run
    and the difference is metres a player would notice
    which is a different price for the identical drop
    which is not what twenty copies of the nearest run would cost

The nine that pass at HEAD as well pass on purpose: they are the
non-regression half -- the desks all exist, they all cable, they all come up,
none is too long, they are all walkable to, and the bill is arithmetic either
way.

Two of them were tightened after the first pass, because at HEAD they passed
vacuously on a room with no desks in it: `big_desks > small_desks` and
`big_desks >= 2 * small_desks` are both true of `0` and `0`, so both now
require `small_desks >= 1`. That is the difference between asserting a spread
and asserting an empty room.

## Gates, run on this tree

    --sitecheck   496/496       --loadcheck   35/35, identical to HEAD
    --netcheck    262/262       --eventcheck  83/83
    --health       20/20        --mancheck    57/57
    --building   200/200        --solve 60    60 repaired, 60 handed back
    --askcheck  2844/2844       check-decoys  37/37
    test-cpu     PASS (40 agreed with qemu, 0 diverged)
    asan --sitecheck   clean, no ERROR or SUMMARY lines

`--sitecheck`'s total is a moving target this hour and the reason is worth
recording rather than rounding off: it was 454 at HEAD, 471 with this
section's seventeen, and 496 by the time the last gate ran, because another
agent is landing `deliver` in the same tree. Anything below this section in
the listing is theirs, and one run of `--sitecheck` at 10:56 caught their
`sessioncheck.c` mid-save and reported a failure that does not reproduce on
any build before or since.

## What was NOT done

**`core/loadcheck.c` was not touched.** It was the thing most at risk and it
is byte-for-byte the same file, producing byte-for-byte the same table.

**`desks <tenant>` still heads its listing with the tenancy's FIRST room** --
"tenancy 1, f1 office #36: 20 desks" -- which now under-describes a tenancy
that holds eleven. The room is on every desk's own row, so nothing is hidden,
but the header would read better as the floor and the room count. That line
is in `core/session.c`, which belongs to another agent this hour, and it is
theirs to change.

**Nothing was done about which desk a person sits at.** The desks moved; the
people are still dealt to them in device order, so a tenancy's staff are
spread with the desks and no attempt was made to make that mean anything.

**No trade places its desks differently.** A studio's suites and a call
centre's positions are apportioned by area exactly as an office's are. There
is an argument that an edit suite is a bigger footprint per head than a desk,
but it would be a number typed in this project rather than one the building
generator produced, and D30's record says at length why that trade was
refused before.
