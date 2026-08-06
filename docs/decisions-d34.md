# D34. A verb for the movement keys, and a sentence where the bookkeeping goes wrong

## What was measured, and what it actually measured

A playtester reached day 30, was asked whether the game is fun, and said
*"yes... but the early game is still admin."* The number behind it:

> "Roughly 40% of my commands were `lift 0 / go goods / carry X / lift N /
> go comms / drop`, repeated once per box. Buying three switches for one
> floor is nine walk commands and no thought. The **decisions** are good and
> the **interstitial** is filler."

That reads as a tedium report and it was taken as one. It is not. The owner's
reading is the correct one:

> "lift 0 / go goods / carry X / lift N / drop are all things the AI has to do
> because they are not in the 3d space. Those are actions the user will do
> walking around in the 3d space."

The playtester was an agent on a socket. **For the human at the keyboard there
is no filler there at all** -- carrying a box up two floors is the physical act
that makes where you put it mean something, it is what D23 built the floor plan
for, and D27 priced every metre of copper off it. A `deliver` verb sold as a
convenience would have been deleting the game to save typing nobody does. This
record exists partly so nobody makes that mistake again from the same report.

So the brief split into two halves, and they are not the same kind of problem.

## 1. `deliver` -- the movement keys, for a client that has none

Kept, and **documented as socket parity rather than as a convenience**, which
is the only framing under which it should exist.

The rule it serves is session.h's, from D23: *"the view is never the source of
truth... if it cannot be played over a socket, it cannot be tested, and it will
rot."* Blind playtesting is the only quality mechanism this project has ever
had, and a blind tester cannot hold W. `cable` was given exactly this treatment
for exactly this reason, in the owner's words -- *"for things like cabling, we
should have an easy way for agents to do what a person would do moving
around"* -- and `deliver` is that argument applied to carrying.

    deliver <box> [<box>...] <room>

It is `go`, `carry`, `lift`, `go`, `drop` performed in that order. Underneath it
is the long form's own code: `travel_to()` is `lift` then `go`, `carry_box()`
and `drop_box()` were lifted out of `session_line()` unchanged so that both
verbs share one copy, and every refusal is therefore the long form's refusal in
the long form's words. Several boxes is several trips, because both hands are
still on one box.

**The cost identity is not a guard rail around the feature. It is the entire
justification**, and `--sitecheck` plays it both ways on one seed:

    three switches to one floor: walked, and typed
        20 lines, 320 m walked, 1200 spent      go/carry/lift/go/drop
         4 lines, 320 m walked, 1200 spent      buy x3 + one deliver

Same money, same metres, same day, same room at the end. If that check ever
fails, this verb has stopped being parity and should be deleted rather than
repaired -- the comment above `do_deliver()` says so.

Three decisions inside it worth writing down:

**It takes the lift, and that is why the check is a measurement rather than an
assumption.** `lift 3` is not the same metres as three flights of stairs:
`do_lift()` charges the walk to this floor's lift lobby and the ride itself is
free, which is the whole reason anybody ever puts a switch on the eighth floor.
A `deliver` that walked would have been *dearer* than the hands, and just as
wrong. On a floor with no lit button it takes the stairs, which is also what a
person does.

**It does not walk you back**, where `cable` does. `cable` returns because the
next run comes off the same drum in the same cupboard; a delivery ends where
`drop` ends -- in the room with the box, which is where you are about to cable
it from.

**It finds out where a person finds out.** The first draft checked cabled,
jacked, tenant-owned and ISP-owned from a standing start and refused the whole
line without moving. That was tidier and it was wrong: `go core` then `carry
core` charges you the walk and *then* tells you there is copper in the back of
it. Checking early would have made the shorthand cheaper than the hands in
exactly the case where the player got something wrong, which is the one case it
must not be. So only whole-line impossibilities are checked up front -- a name
that is not a box, a name given twice, hands already full, a drum in your hands
-- and everything else is asked at the box, by `carry_box()`. A delivery that
cannot finish stops there, with the boxes before it delivered and this one where
it has always been.

**`deliver` is deliberately NOT in `check_verbs`'s list** of verbs the 3D shell
has an action for. It is the one verb in this file that exists *because* the 3D
shell has an action and the socket does not.

**What was dropped on the way.** A quantity on `buy` (`buy switch24 sw 3`) was
built and then removed. It saved two lines, changed no money and no metres --
and it was a pure player convenience with no parity argument behind it, which is
precisely the category this record is about. The `buy` hint now teaches the long
form first and names `deliver` after it as what a pipe types instead.

## 2. The five places a tenancy lives, and the one pair the game can check

The more valuable half, because it is typed by humans and agents alike. The same
report:

> the bookkeeping around a tenancy is five places to get right -- `vlan 13` =
> tenant 3 = `10.0.3.0/24` = `subif edge 1 13` = `trunk core 2 13` = `trunk
> sw2b 23 13` -- "and the game checks none of them against each other."

They found the consequence the hard way: a subinterface whose vlan was not on
the trunk it rides, with both commands answering `set` and nothing anywhere
saying the two disagreed. That burden is held in the head, so the 3D shell does
not relieve it either.

**What was built is one sentence at the moment of the mistake**, not a
checklist. `subif`, `trunk` and `vlan` now check ONE hop -- the cable in front
of you, the tag one end wears, and whether the socket at the other end will pass
it:

    f0 MDF> subif edge 1 14 10.0.14.1/24
    edge:
    ...
    eth1.14: UP LOWER_UP link/ether 02:4e:4f:00:00:07
        inet 10.0.14.1/24
        vlan 14
      NOTE: eth1.14 on edge wears vlan 14, and core port 2 -- the socket at the
      other end of that cable -- does not carry vlan 14. A frame tagged 14 is
      dropped there, coming and going, so nothing on vlan 14 crosses this link.
      `trunk core 2 14` lets it across; `show core` says what that port carries.

It fires from either end, because `subif` is typed on the router and `trunk` on
the switch and both should hear about the same disagreement -- including `trunk
core 2 -14`, which is a player cutting off a subinterface they configured a
fortnight ago.

**What it deliberately does not do.** It does not check the subnet against the
tenancy, it does not follow the far switch's uplink, and it does not tell you
what the convention should be. Those are the player's to hold. Two of the five
places the playtester listed are now checked against each other; the other three
are the ones where holding a convention IS the game.

**And it does not cry wolf, which is why it is narrower than it looks.**
netstack's `port_carries()` passes a vlan if it is the port's native or in the
allowed set. `net_trunk_allows()` reads the set and `net_dump_trunk()` prints the
native, so `session.c` can compute both without touching `core/netstack.c`. What
it cannot read is a port's mode, so an access port that happens to be in the
right vlan gets no note -- even though a tagged frame really is dropped there.
It under-warns rather than risk saying something false about a link that works,
because a warning printed beside a correct configuration teaches a player to
stop reading them.

**The gate does not assert that a warning is printed. It asserts the warning is
true**, which is the rule this project runs on. A real pc on a real access port
in vlan 14 pings the router's subinterface across the switch:

    a subinterface on a card whose far port does not carry the tag says so   ok
    and it is true: a host in vlan 14 cannot reach its gateway across it     ok
    letting the vlan across the trunk stops the note being printed           ok
    and stops it being true, in the same move: the ping crosses now          ok
    and taking it back off says which subinterface that just cut off         ok
    which is true as well: the ping stops crossing again                     ok
    a subinterface whose trunk does carry it is not warned about             ok
    nor is an ordinary address on an access port                             ok

## The gates

Against a clean `git archive HEAD` checkout with only the new `sessioncheck.c`
copied in: **480/496, sixteen assertions failing.** With the change: **496/496.**
`--sitecheck` was 454/454 before, so the two new sections and the reworked
delivery section are +42 assertions in total.

    --sitecheck  496/496       --netcheck    262/262
    --loadcheck   35/35        --eventcheck   83/83
    --health      20/20        --mancheck     57/57
    --building  200/200        --solve 60     60 repaired, 60 handed back
    --askcheck 2844/2844       check-decoys   37/37
    make test-cpu: 40 agreed with qemu, -O0/-O2 agree, Linux/Windows agree

## What was NOT done

**No accessor was added to `core/netstack.c`**, which was out of scope and owned
by another agent today. The one that would widen the note is a getter for a
port's mode and its access vlan -- `net_port_mode_of()` and `net_port_vlan_of()`,
the read halves of two setters that already exist. With them the note could
speak about access ports (where a tagged frame is dropped outright), and a
`vlans` cross-reference could print, for each vlan in the tower, the
subinterface that terminates it, the trunks that carry it and the access ports
that are in it -- the four of the playtester's five that are structural. Without
them, an access port's vlan cannot be read back from `session.c` at all, only
set. **That is the single change that would unlock the rest of this idea.**

**The note is one hop only.** A vlan that crosses the core switch and dies on
the floor switch's uplink is not caught, because that is a path question rather
than a link question, and a path question wants either a reachability walk or
the accessors above.

**Nothing checks the address plan against the tenancy.** `10.0.3.0/24` being
tenant 3's is a convention the player invented and the game has no opinion
about, correctly.

**No `--loadcheck` scenario uses `deliver`**, and none should: that gate plays
the game the way a person plays it, and its whole value is that it does.
