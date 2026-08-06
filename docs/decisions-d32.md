# D32. A run has a price before it has a bill

## The sentence this record exists for

A playtester who reached day 62 went looking for D28's marginal-copper rule --
the one that makes a run past ninety metres take errors under load -- and could
not find it. They put a switch in an f3 office and got 66 m, and in an f6
office and got 76 m, and concluded the rule was unreachable in this building.
Their verdict:

> **"There is no way to measure a run before paying for it, so exercising the
> marginal-copper rule is guess-and-pay at ~110 a guess."**

They were half right, and the half they were wrong about was corrected in the
wrong direction first -- see the withdrawn correction in d28. Those per-floor
figures were measured with `bld_cable_all()`, the tray distance between two
rooms, and the game bills `site_run_metres()`, which adds a patch lead at each
end. Three metres, on every run, missing from every number. `quote` reads the
billing function, which is the whole point of it, so the numbers below and in
`quote` agree and the ones in that first correction do not.

The rule is reachable from floor two up. What the playtester could not do was
tell which room they were looking at. On this seed's floor
three, `rooms 3` prints twenty-four rooms and calls eleven of them `office`,
and the billed run from the MDF to them ranges from **60 m to 95 m**. One of those
is a fine home run and one of them retrains itself to a hundred megabits under
a floor of desks, and nothing in the game distinguished them until you had
paid.

And it was never only that rule. **Every cable decision D27 built was made
blind.** Cat5 against cat5e against cat6 against fibre; the spool against the
jack; this cupboard against that one. All of them are priced by the metre, and
the metre was a number the game first printed at the moment it charged you for
it. That is a strange property for a game whose central resource is priced by
the metre, and D28 recorded it rather than fixing it because the verb that
would fix it lives in a file another agent held at the time.

This is that verb.

## `quote`, and where it stands

    quote <room|box>          from the room you are standing in
    quote <a> <b>             between two named ends, from wherever you are

An end is a **box** (`core`, `core:2`) or a **room** (`comms`, `f3.office`,
`#41`), which is the two ways a person thinks about a run: the one that ends in
a socket you own, and the one you are asking about before you have bought
anything to put there.

    f0 MDF> quote core #84
    a run from core:0 in f0 MDF #22 to f3 office #84: 95 m through the tray.
      grade   off the spool   as a jack   it comes up at
      cat5             89         179   100 Mb
      cat5e           128         218   1000 Mb
      cat6            171         261   1000 Mb
      fibre           587         677   1000 Mb
      core:0 does 1000 Mb whatever you plug into it, and that is what holds the
      faster grade down. Paying for reach you cannot land is money burnt.
      a jack is 4 days of the trade's time and it is not a socket before then;
      a lead into it afterwards is 12, for every box that ever stands there.
      95 m is past the 90 m copper has margin for: under a floor's load this run
      takes CRC errors, says so in `events`, and retrains itself down. Fibre does not.
      no box is named at f3 office #84:
      those speeds are the MOST this run comes up at, because the port at each
      end has the last word and it arrives with the box.
      nothing was bought, nothing was booked and nothing was charged.

Four decisions on one screen, and every one of them was previously invisible at
the moment it was being made:

- **the tray metres**, which are not the metres you walk and cannot be guessed
  from the floor plan;
- **the grade**, priced and speeded side by side -- over 95 m cat6 costs 43
  more than cat5e and carries exactly the same gigabit, and fibre costs 459
  more and carries the same gigabit too, because the port at the end of it is a
  gigabit port;
- **the spool against the jack**, in money AND in days, which is the half of
  that trade-off the clock decides rather than the budget;
- **the margin**, said about *this run* in *its own metres* rather than as a
  rule the player has to remember.

## Nothing in it is a second copy of the arithmetic

This project has shipped that bug three times in a day: a `demand` footer that
said a switch seats 23 desks after the catalogue made it 22, a boot menu
drawing three entries beside a loader that said one, and prose quoting "ninety
metres" beside a `#define` of 90. A quote is a machine for committing exactly
that error, so the rule was made structural: **every number `quote` prints
comes out of the function that will charge for it or measure it.**

    the metres        site_run_metres() -> site_metres() -> bld_cable_all()
    the spool price   site_cable_price()
    the jack price    site_jack_price()
    the days          site_jack_days()
    the port speeds   net_port_speed(), off a real port
    the margin        SITE_COPPER_MARGIN_M, and a gate that plays it

Two of those needed work.

**`site_run_metres()`.** The rule that the ISP's handoff is outside the
building and the lead into it is a patch lead was written out three times --
in `site_cable`, in `site_jack`, and again in the session's own spool. A quote
would have made a fourth copy of a sentence that has to agree with the invoice.
It is one function now, and all of them call it.

**`site_cable_speed()`.** How far a grade reaches and what it settles at over
that distance are `core/netstack.c`'s rules, private to it, and correctly so:
the frames obey them. Restating "cat 6 stops doing ten gigabit at fifty-five
metres" in `core/site.c` would have been the same bug in its purest form. So
the quote does not restate it -- **it lays a cable and reads the port.** Two
switches and one cable in a `Net` of their own, `net_port_speed()` off it, and
the world thrown away at the end of the call. Nothing in `site.c` knows what
fifty-five metres means, and nothing in it can therefore drift.

That costs one 70.8 MB `Net` per `quote` line typed, allocated and freed. It is
about fifteen milliseconds of a command a person types a few times a floor, and
it buys a number that cannot be wrong.

**The one duplicate that remains is `SITE_COPPER_MARGIN_M`.** The behaviour is
`core/siteday.c`'s and that file was out of scope for this change, so 90
appears in `core/site.h` as well. It is not left on trust: `--sitecheck` builds
the marginal run, puts a floor of desks behind it, turns the days, and asserts
that **`events` calls that run marginal and does not call the 3 m run carrying
the identical frames marginal**. If the number moves in `siteday.c` and not
here, that check fails. A checked duplicate is not a good duplicate, and the
right answer is one `#define`, in `site.h`, the next time anybody may edit both
files.

## The judgement calls, made rather than asked

**A quote costs nothing.** A surveyor's visit is a real thing and there was a
case for charging for one. It was refused for two reasons. The first is that a
fee on asking is a tax on carefulness: the player who reads before spending is
the player this game is trying to produce, and charging them for it teaches the
opposite. The second is decisive -- **every number in a quote is already
printed for free at the moment the money leaves.** `cable` prints the metres,
the price and the negotiated speed; `jack` prints both prices, the days and the
lead. Charging to see them a minute earlier would be charging for the
*ordering* of information, which is not a decision, it is a toll. A quote does
not advance the clock either, and it does not walk you anywhere: reading a plan
is not a journey.

**It quotes both from where you stand and between two named ends.** One name
is the common case and it is the one a body has: you are in the cupboard, with
the drum, wondering what the far end costs. Two names is the one a person has
with the floor plan in front of them and their legs still in the chair -- and
it is the spelling that lets you compare two cupboards without walking to
either. Neither is a shortcut past anything: no metre of copper and no metre of
walking is avoided by asking.

**It refuses to guess about the kit.** A quote is for a route, and since D27 the
port at each end has as much say in the speed as the copper does. Where a box
is named at both ends, the speed column is the real answer and one line under
the table names the port that holds it down. Where an end is a room with
nothing in it yet -- which is *most of the questions worth asking*, because the
whole point is to price a room before putting anything in it -- the table is
the most the run can do and it says so:

      no box is named at f3 office #84:
      those speeds are the MOST this run comes up at, because the port at each
      end has the last word and it arrives with the box.

**The grades are printed cheapest first, and the order is the prices
themselves** rather than a second list of grades that could fall out of step
with the first.

## Played, and it changed the decision

`./build/bf --towersh 7008`, from 60,000. A core switch in the MDF, and floor
three to cable. The plan was the switch in the tenancy's own office, which is
where the desks are and where a player who has not been bitten yet puts it --
and is exactly the mistake D28 describes: *the floor's switch put in the office
with the desks rather than in the comms cupboard, home-run to the core in
copper.*

    f0 MDF> quote core #84
    a run from core:0 in f0 MDF #22 to f3 office #84: 95 m through the tray.
      ...
      cat5e           128         218   1000 Mb
      95 m is past the 90 m copper has margin for: under a floor's load this run
      takes CRC errors, says so in `events`, and retrains itself down. Fibre does not.

    f0 MDF> quote core f3.comms
    a run from core:0 in f0 MDF #22 to f3 comms cupboard #83: 42 m through the tray.
      cat5              79         169   100 Mb
      cat5e             99         189   1000 Mb
      cat6             120         210   1000 Mb
      fibre            450         540   1000 Mb
      core:0 does 1000 Mb whatever you plug into it, and that is what holds the
      faster grades down.

The switch went in the cupboard. Two questions, no money, and the answer to
both of them was a number nobody could have guessed: 95 m against 42 m between
two rooms on the same floor, and a grade decision where the three dearer drums
buy the same gigabit as the cheap one because of a port in the MDF.

Then the run itself, from the cupboard, and the reason this record exists:

    f3 comms cupboard> quote sw3 core
    a run from sw3:0 in f3 comms cupboard #83 to core:0 in f0 MDF #22: 42 m through the tray.
      cat5e            99         189   1000 Mb

    f3 comms cupboard> cable sw3:0 core:0 cat5e
    link 0: sw3:0 to core:0, 42 m of cat5e through the tray, 99 paid, the port comes up at 1000 Mb.

**Quoted 42 m at 99 and coming up at 1000 Mb; billed 42 m at 99, up at 1000
Mb.** That equality is asserted in both gates, in the money that really left
the account, because a quote that disagrees with the bill is worse than no
quote at all.

## The gates

Two new sections, twenty-nine assertions, measured against a clean
`git archive HEAD` checkout in `/home/david/NOMINAL-baseline-quote-agent` with
only the two new check files copied in:

    at HEAD, with the change's gates:   426/449   (23 fail)
    with the change:                    449/449

The twenty-three are the twenty-three this record is about: the verb does not
exist, so nothing measures a run, nothing prices one in four grades, nothing
names the margin, nothing says the port has the last word, and the tower help
names a verb the tower has not got.

The gate is written to COMPILE at HEAD, which took two shims in the baseline
copy and nothing in the shipped one: `SITE_COPPER_MARGIN_M` and
`site_cable_speed()` are helpers this change adds to `core/site.h`, and without
them the file does not build at HEAD and could not be counted at all. They are
not the feature. The feature is the verb, and every failure above is the verb
missing.

Three of the new assertions deliberately **pass at HEAD**, and they are the
ones that bind the quote's margin number to `core/siteday.c`'s behaviour: the
tower is built, a tenancy is served through the 95 m run, the days are turned,
and `events` is asked which run it thinks is marginal. That check asserts the
world's behaviour rather than the quote's text, so it holds on both sides --
which is the point of it.

Everything else, measured after:

    --sitecheck  449/449      --netcheck   240/240
    --loadcheck   35/35       --eventcheck  83/83
    --health      20/20       --mancheck    56/56
    --building  200/200       --solve 60    60 repaired, 60 handed back
    --askcheck  2850/2850     check-decoys  37/37
    test-cpu      pass        asan --sitecheck  clean

`--loadcheck` is untouched and every assertion in it still holds.

## What was deliberately NOT done

**No `quote` for anything but a run.** A verb that priced a switch, a floor
opening and a month of circuit as well would be a second catalogue, and the
catalogue is already printed by `buy`, `open` and `isp` at the moment each of
them charges. The blindness D28 found was specific: it was about metres, and
metres are the one price in this game that comes out of the building rather
than out of a table.

**No quote history and no comparison table.** `quote a b` twice is two lines of
typing and the player can read them both; a verb that remembered the last five
and drew a table would be a spreadsheet in a game about walking to the room.

**`SITE_COPPER_MARGIN_M` is still two `#define`s.** `core/siteday.c` was out of
scope. The gate binds them by playing the day rather than by reading the
number, which is the strongest thing available from this side of the fence, and
it is written down here rather than left for somebody to find.

**The jack is quoted and not booked from the quote.** There is no
`quote --take-it`. Booking is `jack`, it costs money and days, and putting a
one-key path from a price to a purchase in front of a player is the shape of
every interface this project has refused since D25.

**Only the seed 7008 tower was walked.** The two-rooms-on-one-floor spread that
makes this verb necessary was measured across floors on that seed and asserted
on it in the gate; whether a wider floor plate makes the spread larger has not
been played.
