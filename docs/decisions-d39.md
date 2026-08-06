# D39. The reports stop contradicting each other

## The defect this record is about

It is one defect, four times over, and it is this project's recurring one:
**a fact computed in two places, and the wrong one shipped.** Today alone it
had already produced a `demand` footer dividing by the wrong port count, a
boot menu drawn beside a loader that counted entries differently, prose
quoting a constant it was sitting next to, and `load`'s queue column printing
the instantaneous queue under a heading that said "peak".

A playtest that reached day 18 found four more, all in the pages the game
prints about itself. Every one of them has the same shape: two true numbers
that look like they should agree, and nothing anywhere saying which is which.

The rule this record applies: **when a number appears twice, one of the two
is eventually wrong; the fix is to make there be one.**

## A. `service` accused the player of not serving DHCP

Reproduced on the gate's own tower, seed 7008, over `--towersh`, with the
pool up on `eth1.11 (vlan 11)`, the trunk across, and every desk patched into
vlan 11:

    f1 comms cupboard> serve 1 sw1 11
    tenancy 1: 20 of 20 desks have a port (20 new). 56368 left.
    f1 comms cupboard> service
      floor tenant  trade      desks   up  addr   done  worst   strikes  rent/day  files
          1      1  office       20   20     0      -     0ms        0        740  none
              20 desks with link and no address: nothing is serving dhcp on their segment.

One `day` later all twenty held leases from that exact pool. The sentence was
generated from `addressed == 0` and never asked what had happened -- so the
player who had done it *right* was sent off to re-check a working pool. Their
words: *"leases are only handed out when the day runs, and the diagnostic
needs to say that instead of accusing the player."*

**The fix is not a better guess, it is a measurement.** `SiteTenant` gains
`leases_asked`, incremented in `core/siteday.c` at the line that really calls
`net_dhcp_client` for a desk. A desk asks when the busy period runs and not
before, so the diagnostic now has the fact it needed:

    20 desks with link and no address YET: their machines ask for a lease
    when the day runs. `day`.

and, when they have asked and nothing answered -- which is the fault the old
sentence alleged unconditionally -- it still says so, now earned:

    20 desks with link asked for a lease and got nothing: no dhcp pool
    answered on their segment. `dhcpd <box>` says what a box serves.

Both branches are gated, both reproduced in `check_dhcp_diagnosis`.

## B. The headline transfer count could not be reconciled with the rows

    day 8: 2 in, 1 served, 38/38 desks addressed, 96/134 transfers finished
      floor tenant  trade      desks   up  addr   done
          1      1  office       20   20    20  60/80
          2      2  voice        18   18    18  18/18

80 + 18 = 98, not 134. The gap is exactly twice the voice tenancy's calls,
because **a call centre agent's machine also does two CRM transfers**, which
the tower carries and the tenancy is not judged on. Both numbers were true.
Both used the word "transfers". The player has a 36-unit hole and no
explanation.

`site_tenant_kind_unit()` exists precisely so a trade's unit is named in one
place, and `service`'s legend already had to be taught this once ("done counts
transfers for an office, CALLS for a voice"). The headline never got the
message, because it was reading a different pair of integers.

**The headline is now literally the sum of the rows.** `site_day_work()` in
`core/site.c` walks the same per-tenancy `tried`/`finished` that
`site_dump_service` prints and adds them up; `status` and the `day` line both
call it and neither counts anything itself. `SiteDay.sessions` keeps its old
meaning -- every unit of work the *tower* carried -- and `core/site.h` now
says so beside the field, because `--loadcheck` measures the tower with it and
that is a different and legitimate question.

The unit word comes from `site_tenant_kind_unit` when every tenancy that did
work is in one trade, and is "jobs" when the building holds a mix, so the
headline can never call a call a transfer again:

    day 8: 2 in, 1 served, 38/38 desks addressed, 78/98 jobs done, 309 taken
    78 of 98 jobs finished inside the busy period, summed over the `service`
    rows; 117 MB moved in all.

The gate asserts the *arithmetic* rather than the wording -- headline ==
sum(rows) -- on a building where the two totals genuinely differ (rows 78/80
judged, tower 116/120 carried), so reverting the fix fails it rather than
passing by luck.

## B2. `worst` was a wall time printed next to a delay

Found by the agent on `core/session.c` in the same hour and handed over,
because the column lives in this file. A call centre showed `worst 780ms`
beside `demand`'s "a call dies past 150 ms one way", and the only way the
playtester could rule out the contradiction was to sit at a desk and run
`voice`, which said 3.0 ms.

`worst_ms` is `ended - began` on a finished **transfer**; the call loop never
touches it. So a voice tenancy's `worst` is measured on its agents' file and
page traffic and is not comparable with a one-way delay in any direction. The
gate reproduces the exact shape -- `worst 2057 ms, one-way delay 4 ms, 20/20
calls` -- and asserts the footer says which is which and sends the player to
the evidence that does answer the question:

    worst is WALL TIME and not delay: the longest one transfer took from
    start to finish, for any desk in that tenancy, all day. It never comes
    off a call ... `sit` at one of their desks and run `voice` for the port
    that threw the audio away.

## C. `outlets` hid exactly the rooms that are scarce

Standing in an empty f1 comms cupboard whose own `look` says `power: 4 outlets
on the wall, 4 free`:

    f1 comms cupboard> outlets
      power, one floor
      room                     built  added  in use  free   another
      nothing on it draws power yet.

`outlets all` printed one row: the MDF. The filter was "something is in it or
a socket is in use", which on a let floor means *the tenant's desks* -- so the
page showed eleven offices with thirteen to sixteen free sockets each and
hid the cupboard, the riser, the plant room and the server room. **The rooms
it hid are the scarce ones** (cupboard 4, plant 4, riser 1) and the rooms it
showed are the ones that will never matter. The one question a power map
exists to answer -- *how many sockets has the empty cupboard I am about to
fill?* -- was the one it refused.

Every room built to hold equipment is now on the map, empty or not
(`room_holds_kit()`: MDF, server room, comms, plant, riser, goods in), and
everywhere else appears once there is something in it. The corridors stay
off, because a nine-floor tower is four hundred rooms and the cleaner's socket
is not a decision. The gate walks the building and asserts that no equipment
room anywhere in the tower is missing (16 of 16 present) and that an empty
corridor is still absent. The help text and the README line were rewritten to
what the page now does, rather than the reverse.

## The first design question: what discharges `+server`

They put one server in a comms cupboard on three VLAN subinterfaces, and
`service`'s `files` column then named it for all three tenancies and all three
paid -- after they had spent 1,350 on the assumption that it would not count.
`demand` printed `+server` as a requirement and never said what satisfies it,
while it *did* print the web host's opposite rule, which made the ordinary
case read as an unstated exception to a rule that is itself the exception.

**Decision: a shared floor server discharges it, and `demand` says so, in the
words of the rule the day really applies.** Nothing in the model changed. The
preference order in `file_server_for()` -- their own machine, else one on
their floor, else anything powered and addressed in the building -- is already
the right behaviour and is already what `service` explains after the fact.
What was missing was saying it *before the lease is signed*, where the money
is spent. `demand` now ends with it, and the gate asserts both halves: that
`demand` says it, and that one server in a floor's cupboard really is the file
server of every tenancy on that floor (3 of 3 on seed 22).

The alternative -- making `+server` a per-tenancy requirement that a shared
box does not satisfy -- was rejected because it would make the cheapest
correct build in the game (one server per floor, on subinterfaces, which is
what D27's planned tower is) wrong, to serve a want that is advisory in the
first place: `wants_server` is a column in `demand` and appears nowhere in the
day's arithmetic.

## The second design question: a warning before `serve`, not after

`serve 1 sw1` with no VLAN told them, *after* the copper was billed, that this
tenancy had asked for a segment of its own -- and putting it right cost 21
hand-typed `vlan` lines, because `serve <t> <sw> <vlan>` skips a desk that is
already patched.

**Decision: not a refusal. The warning moves above the bill, and the remedy
becomes one line.**

A refusal-with-confirmation in the house style `carry` uses was the obvious
candidate and is the wrong answer here, for a measured reason: `serve <t>
<sw>` with no vlan is what **every scripted build in this tree** types --
`core/eventcheck.c`, `core/sessioncheck.c`, and `--loadcheck`'s naive tower,
whose entire job is to be the flat build a player really gets on their first
afternoon. A verb that refused it would quietly unbuild the calibration the
game measures itself against, in files this change does not own. That is not a
reason to ship a bad verb, but it is a reason to look harder at what the
player actually paid: not the patching, which is recoverable, but the
twenty-one lines.

So:

- the NOTE is printed **before** the price, naming the vlan `serve` would use
  and saying what the line without it will do;
- `site_serve_vlan()` no longer skips a desk that is already patched **into
  this box** -- it sets that port's vlan too. Setting a port that is already
  patched is a config change on a switch: no copper is laid and no metre is
  charged. Measured in the gate: 20 desks re-vlanned for 0, and all 20 then
  hold a lease off a pool that answers on that vlan and nowhere else, which is
  proof the ports really moved rather than a claim that they did.

The whole remedy for the whole mistake is now the same line with the vlan on
the end.

## Evidence

Every fix has a check that fails without it. Built from `git archive HEAD`
into a path of its own, with only the new checks (plus `site_day_work` itself,
which the B check calls) added:

    HEAD, clean                                 550/550 site checks pass
    HEAD + the new checks                       564/580 -- 16 FAIL
    HEAD + the new checks + this change         580/580

The sixteen that fail without the change are the three DHCP sentences, the
three headline/`day`-line assertions, the three `worst` sentences, the three
power-map assertions, the two `serve` assertions and the two `demand`
sentences.

Gates, measured in a tree containing this change and nothing else (another
agent held `core/session.c` and `core/net_sites.c` this hour):

    --sitecheck   580/580      (550 at HEAD + 30 new)
    --loadcheck    35/35       the curve, unweakened: SiteDay.sessions kept
                               its meaning precisely so this did not move
    --netcheck    262/262
    --eventcheck   83/83
    --health       20/20
    --mancheck     57/57
    --building    200/200
    --solve 60     60 repaired, 60 handed back
    --askcheck    pass
    check-decoys   37/37
    make test-cpu  40 agreed with qemu, 0 diverged; all four cpu gates pass

## What was NOT done

- **`core/session.c`'s help still says `outlets` is "this floor: what every
  room was wired with"**, which is now closer to true but is still not what
  the page does: an empty toilet or corridor is not on it. That file was owned
  by another agent for the hour. The site-level help table and the README were
  corrected; that one line is a follow-up, and it is named here rather than
  left to be found.
- **`--loadcheck`'s own table still prints `SiteDay.sessions`**, so a
  developer reading the calibration output sees the tower's carried total
  while a player sees the judged total. That is the right number for what that
  gate measures and the field now documents itself, but it is the same family
  of trap and `core/loadcheck.c` was out of scope.
- **`worst` was explained, not fixed.** The honest end state is a column whose
  measurement belongs to the trade, the way `done` already does -- a voice
  tenancy's worst *call* rather than its worst *transfer*. That is a change to
  what the busy period records per kind, it moves a column every existing
  check reads, and it wanted more room than this hour had. The footer now
  makes the number unmisreadable in the meantime.
- **No refusal was added to `serve`**, for the reason argued above. If the
  owners of `core/loadcheck.c`, `core/eventcheck.c` and `core/sessioncheck.c`
  want one, the change is small and the three scripted towers need `flat` or a
  vlan on eleven lines between them.
- **Nothing was done about `wants_server` being advisory.** A tenancy that
  asked for a server and never got one is served exactly as well as one that
  did, off whatever is powered. That may be right -- a file is a file -- but
  it means `+server` in `demand` is a hint about what to build rather than a
  term of the lease, and this record only made the hint honest.
