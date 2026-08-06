# D38. The cable you run with your hands

## The sentence this record exists for

The owner, playing his own game:

> "As is, I can't figure out how to actually attach a cable, run a cable from a
> particular port to another."

Cabling is the central verb of this project. It works over the socket — `cable
core:1 files:0 cat6` — and that is exactly how it stayed broken. Four blind
playtests typed the verb and none of them has hands, so nobody looked at the
one place a player at the keyboard would look: the crosshair.

**The machinery was all there. The signpost pointed at a key that does
nothing.**

    [C] at a port            has run a cable since the tower had ports
    LMB with the spool       runs one, if you drag the spool into a hand
    _cable_from / _cable_port  the end you have put in, mirrored off the session
    the trail of copper      drawn from that port to your hands as you walk
    "cable in hand from ..."  a HUD line saying a run is open

and the only sentence in the entire game that mentioned any of it:

    sw3 port 0  empty  [Tab] spool in hand to cable it

Tab went back to the terminal when the bag moved to [I] — correctly, a shell
completes paths — and nobody changed the hint. So the game's answer to "how do
I run a cable" was a keystroke that did nothing, and `[C]` appeared nowhere at
all: not in the crosshair, not in the HUD, not in `help`, which is core's and
cannot name a key the window owns.

That is the whole finding, and most of this record is what was built once the
key was findable — because a verb you can now reach is a verb whose gaps you
can now feel.

## What the crosshair says now

    uplink port 0  empty  [C] run cat6 from here  [R] grade
    files          [F] serial  [G] pick up  [C] this end in  -- 21 m of cat6, 100

Four things changed in `aim_text()`, and each of them was a small lie before:

- **The key is the one that works.** `[C]` with both hands empty, `[LMB]` if
  the spool is in a hand, because both really do it. The dot goes green when
  the next press would run copper — it used to go green when a spool was in
  your hand, which is the state of the bag rather than the state of the
  building, so the one press that spends money was the press the dot said
  nothing about.
- **A port with a link in it says so first.** With a spool in hand the hint
  read `[LMB] plug in` over a port that already had a cable up in it, and the
  click was refused by core in words the crosshair had just contradicted.
- **The end you are holding is named where it is.** A half-finished run leaves
  a port that the model still calls empty; offering to plug the other end into
  it was offering something core answers with "that is the end you already put
  in".
- **The box offers it too.** `[C]` at a box is its next free port, which is how
  anybody patches a switch, so the whole box carries the offer — including the
  ISP handoff, which is "passive: copper and a label" and is still the far end
  of the first cable anybody runs in this building.

The coordinator relayed a suggestion to reword the dead hint as `empty  put the
spool in a hand  [Tab]`, now that Tab opens the bag. **Not taken, and this is
the reasoning**: it sends the player to an inventory to do something they can
already do by pressing one key at the thing they are looking at. Naming `[C]`
is strictly more true and strictly shorter. The bag is still on [I], the HUD
still says so, and dragging the spool into a hand still arms LMB — the
crosshair reports whichever of the two is armed.

## The metres, while you are still walking

D32 built `quote` because *"there is no way to measure a run before paying for
it"*, and fixed that for somebody typing. A person WALKING a run is in exactly
the position that record describes: the drum pays out behind them and the first
number they see is the invoice. So the quote now follows the body:

    cable in hand from uplink port 0   walk to the other end
      from here: 21 m of cat6, 100
      305 m left on the drum

and the same figure sits in the crosshair beside `[C] this end in`, which is
where the money actually leaves.

**Nothing in the view computes any of it.** It is `quote`'s printed answer,
parsed: `site_run_metres()` for the metres, `site_cable_price()` for the price,
core's own sentence when there is no tray. D32 made that rule structural for
the same reason it applies here — a view that quoted a price the invoice then
disagreed with would be worse than no price at all. There is no metre, no
price, no grade name and no drum length anywhere in `tower.gd`: the drum line
is `spool`'s own sentence ("305 m of cat6 on the spool") split into two
numbers, and the list of grades [R] cycles is read out of `spool`'s own refusal
("no such cable: ?. cat5, cat5e, cat6 or fibre"), so a grade added to
`core/site.c` turns up on that key without anybody editing the view.

### The refusals arrive where a person would notice

Two of them, and both are core's own arithmetic said early rather than a new
rule:

- **the drum will not reach.** `21 m of run and 12 m left on the drum: it will
  not reach` is the same comparison `spool_plug()` makes (`m > spool_left`) --
  printed while you can still walk back, instead of after the refusal.
- **the copper cannot end here.** Stand in a corridor with an end in your hand
  and the HUD says *"there is no cable tray between uplink:0 in f0 MDF #22 and
  f0 corridor #1"* — `quote`'s sentence, and the run really is refused there:
  `site_cable()` returns `SITE_ENOROUTE` for a room with no tray drop.

### What it costs

`quote` allocates a 70.8 MB `Net` to read the negotiated speed off a real port
(D32), which is 36-38 ms measured in the window on this machine. That is fine
for a line a person types and unacceptable sixty times a second, so a quote is
asked **once per (end, destination, grade) and kept** — the metres between two
rooms are a property of the building and the building does not change. In
practice that is one 38 ms frame the first time you enter a room with a run in
your hand, and free thereafter. `perf` prints the count and the last timing:

    3 run quotes asked and kept, last one 38.5 ms

## Where the copper is drawn, which is the judgement call

Two things could be drawn and they disagree, which is the whole question:

> "I think it'd be fun to literally run cable down corridors. Making the end
> game kinda look like there are cables run everywhere. You shouldn't be
> penalized for running cables literally physically wherever you want."

wants the copper where the player walked. The model bills `bld_cable_all()` —
the tray route, up the riser — which is a different line and a different
number. **A view that draws a route nobody is charged for is a lie, and so is
charging for a route the player did not choose.**

The reconciliation is that they are two different objects, and the game already
knows it:

**While the drum is in your hands, the copper is where your feet went.** A
crumb every 800 mm at the height of the feet that dropped it, and the cable is
drawn along them: round a corner and the cable is round the corner, out of the
door and down the corridor and into the far room. That is not a claim about the
invoice; it is a picture of an act, and it is the act the owner asked to see.
It also fixes the other half of his note — *"that should rest on the floor when
we're cabling things"* — because a straight line from the port to your hands
went through walls and through the floor of the room next door, which is the
one thing a person pulling a drum never does.

**The moment the far end goes in, that copper is thrown away** and the run is
drawn where core billed it: through the tray, up the riser, at the metres the
HUD has been quoting the whole time you were walking. Nothing is drawn that
nobody paid for, and the two never contradict each other silently, because the
number in front of you during the walk is the tray number rather than the walk
number. The player watches a 60 m walk lay a 21 m run and can see both.

### What the floor copper costs

Measured in the window, seed 7008, same view, one frame apart: **53.4 ms of
process with the drum on the shelf and 59.3 ms with a run open** across a
building (about thirty crumbs, llvmpipe, no tenants in yet). Most of that is
not new — `_draw_trail()` has rebuilt a mesh every frame since the trail
existed — and the shape is chosen so it does not grow with the walk: the copper
already on the floor is rebuilt only when a crumb is added, and the piece
redrawn per frame is the three points between the last crumb and your hands. A
walk longer than the drum thins the oldest half rather than growing the buffer.

**What this does NOT do is let the player choose the route.** He is right that
it would be better if it did — "wherever you want" implies the metres are yours
to decide — but the route is `bld_cable_all()`'s and lives in `core/building.c`,
which was not mine to change this hour. The honest version of that ask is a
player-chosen tray route that the model then bills, and it is a core change
with a gate of its own. Recorded as not done rather than half done.

## Socket parity, and the gate that holds it

D23's rule: if a person can run a cable by walking, an agent must be able to
run the same cable by typing, and both must charge the same. `deliver` was
built this morning as explicit parity for carrying, with a check that asserts
the typed form and the walked form cost identical money — *"the cost identity
is the entire justification"*. This is that, for copper.

Structurally it is free: `cable_at()` types `spool cat6`, `plug <box>:<port>`,
`plug <box>:<port>` at the same `session_line()` a socket client writes to, so
there is no second implementation to drift. What was missing was the proof, and
one real bug in the other direction:

**`plug uplink:0` typed at the socket left the window showing nothing.**
`_reconcile()` rebuilt devices, cables, floors and the handset off the session
and never read the spool, so an end put in over the wire drew no trail, raised
no HUD line and left the one state whose next keystroke spends money invisible
to exactly the client that cannot see the window. `_sync_cable()` is in the
reconciler now, beside the handset, for the same reason the handset is.

`game/tests/tower.gd` now plays both, on legs:

    deliver files #11              a box at the far end
    [C] at core port 4             one end in, standing at the box
    _walk_to x4                    the MDF, the corridor, the comms cupboard
    [C] at files port 0            the other end in
    cable core:5 files:1 cat6      the same two rooms, typed

and asserts: the crosshair offers `[C]` and no longer offers `[Tab]`; the legs
charged walking metres; the copper on the floor is on the floor (no crumb more
than 350 mm above its own slab); the metres the HUD quoted while walking are
the metres the invoice charged; the floor copper is gone once the run is; and
**the walked run and the typed run cost the same money for the same metres.**

What it prints, on the gate's own seed 200 tower:

    ok   hands empty, the crosshair on an empty port: 'core port 0   empty
         [C] run cat6 from here  [R] grade'
    ok   walked 6 rooms from the MDF to the comms cupboard, drum in hand
    ok   the legs cost 28 m of walking, which the session counted
    ok   31 m of copper lying on the floor behind you, none of it more than 5 mm up
    ok   walked run: 16 m of cable, 96 paid, 28 m of legs
    ok   the metres the HUD quoted on the way are the metres it charged: 16
    ok   the floor copper goes when the run does: what is left is the billed route
    ok   typed run:  16 m of cable, 96 paid
    ok   walking it and typing it cost the same: 16 m, 96

**28 m of legs for 16 m of copper**, which is the number D23 built the floor
plan for and the first time the window has ever shown both at once.

And measured by hand in the window on the seed 7008 tower, uplink:0 to files:0
in the f0 comms cupboard, walked with W and [C] and then typed:

| | metres | paid | money before | money after |
|---|---|---|---|---|
| walked, [C] at each end | 21 m cat6 | 100 | 57,600 | 57,500 |
| typed, `cable uplink:0 files:0 cat6` | 21 m cat6 | 100 | 57,500 | 57,400 |

`docs/screenshots/d38-crosshair-run-from-here.png` is the crosshair on an empty
port; `d38-one-end-in.png` is the end in and the drum paying out;
`d38-copper-on-the-floor.png` is the cable lying where the feet went;
`d38-walked-everywhere.png` is a comms cupboard floor after a walk that
wandered; `d38-run-finished.png` is the same run once it is a link the model
holds, back up in the tray, drum down to 284 m.

## The grade, which no player could choose

`cable_at()` types `spool cat6` and there was no key that said anything else,
so the grade — most of what `quote` exists to help you decide, and the
difference between a hundred megabit riser and a gigabit one — was a decision
only a socket client could make. **[R] takes the next drum off the shelf**, in
the order core lists them, and it is `spool <grade>`: the refusals are core's,
including the one that will not swap a drum with an end of it already in a
socket. The HUD says which drum is in your hands and how much is left on it
whenever you are holding one.

## Three things handed over by the agent who had the lift and the handset

All three are in files this record's author owned, and all three are fixed
here rather than ticketed.

**The reconciler's cache outlived the lead.** `_reconcile_phone()` remembers
the device and lead it last made the prop agree with and returns early when the
session still names that pair — right, because plugging the same box in twice
is not an event. Its `dev < 0` branch cleared the PROP and not the memory, so
`plug core` / `unplug` / `plug core` matched the stale pair on the third line,
took the early return, and left the handset dark while the session sat on a
management line. Rare until [Esc] started taking the lead out through that same
path, and constant afterwards. The cache is cleared with the lead now, and the
sequence is a check in `game/tests/tower.gd`.

**[F] and [H] plugged the prop and never told the session** — the exact fault
the reconciler exists to fix, running the other way: `ses_prompt()` said `f0
MDF>` while the player was typing at a machine. Both keys go through `plug
<box>` and `plug hdmi <box>` now, so pressing [F] is the same act as typing it,
with the same refusals in the same sentences. [U] goes through `detach()`,
which is core's `unplug`. A device the site model does not own — a patch panel
the view drew — has no name to type, so that one is still the prop's own
answer, which is the same exception `aim_text()` makes for it.

**A stale check.** `[Esc]` on the handset now takes the lead out through core;
the gate still asserted the old "handset down, lead still in".

## The gates

    make gdext                      clean
    game/tests/smoke.gd             0 failures
    game/tests/console_speaks.gd    0 failures
    game/tests/rescue_close.gd      0 failures
    game/tests/desk_holds.gd        0 failures
    game/tests/tower.gd             0 failures  (was 1 failing on arrival: the
                                    stale [Esc] check above)

`tower.gd`'s two standing assertions are untouched and still pass: no point of
a desk's run is above 750 mm inside its own office, and all 861 room-to-room
routes climb only where there is a hole in the slab. The new checks are listed
above, plus one for the reconciler's cache.

## What was NOT done

- **The riser is still a shaft.** *"Potentially the riser room should be left
  kind of a corridor where you run cables. But with a ladder so you can
  actually climb up and down."* He is right and it is the better idea, but a
  climbable riser is a walkable volume, a ladder the physics capsule can use,
  and a change to which rooms a walk route may cross — `bld_walk_all()`'s, in
  core. The cabling half took the hour. Nothing here blocks it and nothing here
  presumes it.
- **The route is still the model's.** See the judgement call above: the player
  chooses the ends, not the metres between them.
- **`[G] pick up` is still offered on the ISP handoff**, which core refuses
  because it is screwed to somebody's wall. It is the same class of small lie
  as the ones fixed above and it is one line, but it belongs with a pass over
  every hint in `aim_text()` against what each verb really refuses, rather than
  with a fix for the one instance somebody happened to photograph.
- **Nothing in `--loadcheck` runs a cable by walking**, because the calibration
  builds towers over a pipe with no window in it. The parity gate is the check;
  the calibration is unchanged and unaffected.
