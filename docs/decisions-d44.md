# D44. The gameplay loops, and the clock that makes them loops

David, after playing the station: *"Essentially there's no game here."* And:
*"Design the gameplay loops."*

He is right, and this record is an attempt to say exactly what is missing
rather than to add things. Most of the parts are built. What is missing is the
thing that turns parts into a loop.

## What a loop needs that this does not have

A loop is: something demands attention, you decide, you act, the world answers,
and the answer changes what demands attention next. NOMINAL has all of that
except the first word. **Nothing demands anything until you press `[N]`.**

That single fact removes the tension from every decision in the game:

- Walking is priced in metres and charged honestly -- and it costs you nothing
  you care about, because the world is frozen while you walk.
- A run of conduit at 93% is a thing that will trip -- one day, when you ask
  for one.
- A tenancy three days from a complaint is three keypresses from a complaint.
- The bridge crew have been sitting at dead consoles since the first morning
  and will sit there for ever unless you go and fix them, which you may do at
  any time, in any order, for free.

Every one of those is a *decision with no opportunity cost*, and a decision
with no opportunity cost is not a decision. That is the whole of "there's no
game here", and it is one variable deep.

## The clock is nearly free, which is the surprising part

`site_day()` in `core/siteday.c` is not a day. It is a **four-second busy
period, simulated one millisecond at a time**: `SITE_BUSY_MS` is 4000 and the
loop at the heart of it is `for (int tick = 0; tick < SITE_BUSY_MS; tick++)`,
driving real TCP through the real stack. The simulation is already
tick-shaped and already sub-second. What it is not is *resumable*: setup,
4000 ticks and scoring happen inside one blocking call.

So live time is not a rewrite. It is a split:

    site_day_begin(s)     move-ins, DHCP, build the day's work, reseed, zero
                          the counters                     -- everything before
                          the loop
    site_day_tick(s, n)   n milliseconds of the busy period -- the loop body
    site_day_end(s, rep)  score it, take the rent, run the weather, check the
                          end of the run                    -- everything after

and then `site_day(s, rep)` is those three in a row, which is what every gate
in this project already calls and what `day 1` still means. **No gate changes.**
That constraint is not negotiable: --loadcheck, --eventcheck and --sitecheck
between them drive thousands of days and measure what each one did, and a
clock that could not be stepped deterministically would take all of them with
it.

The window then calls `site_day_tick` from `_process` at a chosen rate, and a
day takes as long in real seconds as we decide a day should take.

### What that costs, honestly

Four things in `site_day()` are written as if the whole period is in scope:

- The per-day RNG is reseeded at the top from `seed ^ (0x0d0a17 * day)`. It has
  to move into `begin`, which is where it already effectively is.
- `xs`, `cs`, `ss` and `ing` are `nom_alloc`'d locals. They have to live in
  `Site` across ticks. That is the largest mechanical change and it is
  mechanical.
- `hottest_port()` and the link-error accounting divide by
  `SITE_BUSY_MS * 1000ull` as a fixed window. They need the elapsed window
  instead, or they lie during a partial day.
- `s->day++` happens at the top and has to stay in `begin`, so that a day in
  progress has a number.

None of those is a design question. All of them are the kind of thing a gate
catches immediately, because `day 1` running begin+4000+end has to produce
byte-identical results to the old `site_day()` -- and that is testable by
running both.

## The three loops

### 1. The minute loop: something is wrong and you are the one who fixes it

This is the break-fix game, and it is the part that already works. Sixty-two
fault types, a real boot chain that fails where something is actually wrong,
`pkg verify` / `diff` / `reinstall`, the rescue medium, the service processor.
`--solve 60` proves every generated fault is findable and repairable with the
tools that exist.

What live time adds is the only thing it was missing: **while you are
diagnosing, the rest of the station is still running.** The tenancy whose
server you are fixing is losing its day. The 93% conduit is still at 93%. That
is what makes "fix it properly" and "fix it now" different choices.

What it needs: nothing new. It needs the clock.

### 2. The shift loop: demand arrives and you build for it

Order, take delivery in goods in, carry it, feed it, cable it, configure it.
Every verb exists and every one of them is gated over the socket. Until this
week most of them had no route in the 3D at all -- no key pulled a lead out,
no key started a run of conduit, the shop was seven clicks behind an unlabelled
bookmark. Those are fixed.

What makes it a loop rather than a checklist is that the demand keeps coming
and the capacity you built has to carry it:

- a tenancy moves in on its day and wants drops
- a deck comes into service and wants a switch, which wants power, which wants
  a run the core still has an output for
- a crew station wants a machine, power, and a cable -- and `crew` names the
  first thing missing, which is a to-do list the model writes itself

The pressure is that all of it takes real minutes now, and the things you have
not got to yet are getting worse while you work.

### 3. The run loop: what you built is what carries you

Days pass, the station grows, and the shape of what you built decides whether
it holds. This is where the four trades live, and they are already genuinely
different: a call is 172 bytes every 20 ms and cannot be helped by buying
bandwidth; a web host's traffic arrives *inwards*; a studio pushes sustained
upload and still reads its media off a file server.

`--loadcheck` measures the whole of this and it is the most convincing thing
in the project: the same tower built naively falls over by five decks, and
built with a vlan per deck and a server in each deck's own cupboard carries
what the naive one could not, *with the same desks doing the same work*, and
nobody tuned a number to make that true.

What the station pivot adds here, and has not built yet, is the enemy: an
attack on a clock that damages specific things, so that the run loop has a
pressure that is not money. That is real work and it is downstream of the
clock, because an attack on a ~30 minute timer is meaningless in a world that
only moves when you ask.

## What this record commits to

1. **The clock, first.** `site_day_begin/tick/end`, with `site_day()` kept as
   the three in a row so no gate changes, and a falsification: the split day
   must produce the same numbers as the batch day, measured, not asserted.
2. **The window drives it**, at a rate that makes a day a few minutes rather
   than a keypress. `[N]` stays, because a gate and a socket client need to
   say "run a day now", and because skipping ahead is a reasonable thing for a
   player to want.
3. **Onboarding out of the model, not a script.** The first hour teaches the
   five verbs by having you do them, and what it asks for next is *the next
   thing that is actually wrong* -- which `crew` and `service` and `conduits`
   already know. A scripted sequence would go stale the first time the world
   changed; a sequence derived from the model cannot.

## What it does not commit to

The deck layouts (#79) are a separate and larger problem and this record does
not pretend to solve them. The measurement is damning -- every deck of every
seed has a byte-for-byte identical histogram for its first twelve rooms, and
seed 1 has seven consecutive identical residential decks -- but a layout is not
a loop, and fixing the loop first means the layout work has something to serve.
