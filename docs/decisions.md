# NOMINAL — design decisions and why

Append-only-ish. If you want to change one of these, read the "why" first and
argue with *that*.

## D1. The C core owns everything; Godot is presentation only

The simulation, the virtual file tree, the script interpreter, the shell and
the TCP server all live in `core/`, which is plain C11 with no dependency on
Godot and no third-party libraries. It builds three ways from one source set:

- `build/nominal` — standalone headless binary (`--headless --seed ...`)
- `build/libnominal.a` — static lib
- `game/bin/libnominal.<platform>.so|dll` — GDExtension, links the same lib

**Why:** KICKOFF requires headless runs, determinism and a socket *before* any
visuals. If the sim lived in GDScript or in the scene tree, "headless" would
mean "boot the engine anyway" and iteration would be minutes, not seconds. It
also means the socket protocol is not a second-class remote-control bolt-on:
the desktop and the socket are two front-ends onto the identical core API, so
they cannot drift.

**Rejected:** sim in GDScript (too slow for thousands of VM steps/tick, and
GDScript's dictionary iteration order is an unnecessary determinism hazard);
sim in a Godot-side C++ module (requires a custom engine build, kills the
"drop the binary in the repo" workflow).

## D2. GDExtension in plain C, not Rust, not godot-cpp

`gdext/gdextension_interface.h` is produced by the engine binary itself
(`--dump-gdextension-interface`), so the extension has **zero** fetched
dependencies and cannot drift from the engine version in the repo.

**Why not Rust:** the toolchain here is rustc 1.79; current `godot-rust`
targets much newer compilers and older releases don't know Godot 4.7. That is
a network- and version-fragility risk on a project whose whole point is
reproducibility.

**Why not godot-cpp:** it's a large submodule that must be compiled per
engine version. The C interface is ~10 functions for what we need (register a
class, expose methods, marshal Strings/PackedByteArrays). Not worth it.

**Windows:** done, not deferred. `make windows` cross-compiles both
`build/win/nominal.exe` and `game/bin/libnominal.windows.x86_64.dll` with
mingw-w64. `core/net.c` carries the entire platform surface in one ~30-line
block (winsock vs BSD sockets) and `core/hostfs.c` carries the other three
lines (`mkdir` arity). Nothing else in the core is platform-aware.

## D3. Determinism is structural, not aspirational

Rules enforced in `core/`:

- The sim carries its own tick counter. **No wall-clock time reaches the sim.**
  `time()`/`clock_gettime` are not called anywhere under `core/sim*`, `core/vm*`.
- The only randomness is `core/rng.c`, a splitmix64 seeded from the match seed
  and threaded explicitly. No `rand()`, no `/dev/urandom`.
- **No hash-order iteration.** Script dicts are insertion-ordered arrays; the
  VFS is an ordered tree; device lists are arrays. There is no hash map whose
  iteration order can reach observable state.
- Floating point: sim state is `double`, built with `-ffp-contract=off
  -fno-fast-math -fexcess-precision=standard`. `libm` is **not** linked into
  the sim: `sqrt`, `sin`, `cos`, `atan2` are our own implementations in
  `core/fmath.c` so results don't vary with the host's libm version.
- Structs that get hashed for the replay digest are serialised field-by-field
  in a fixed order; no `memcmp` over padding.

Gate: `tools/check_determinism.sh`, expected to run on every change to the sim
or the language. Four checks:

1. same binary, same seed, twice -> byte-identical replay
2. a different seed -> a *different* replay (proves the seed reaches the sim;
   a determinism check that passes because nothing is random is worthless)
3. `-O0` vs `-O2` -> byte-identical. This is the one that catches floating
   point contraction and reassociation.
4. Linux vs the mingw Windows build, run under wine -> byte-identical.
   Measured, currently passing at 219116 bytes. Skipped with a printed SKIP if
   mingw-w64 or wine is absent, never silently passed.

## D4. The scripting language ("NomScript") is ours, and it is a bytecode VM

Python-flavoured surface syntax, a subset modelled on `nomsh` (see
`~/NomnixOS/docs/HAMSH_SPEC.md`). We own it because we need three things no
embeddable interpreter gives us together: a sandbox by construction (the only
I/O that exists is the virtual file tree), exact determinism, and a **per-tick
instruction budget**.

**Bytecode VM, not a tree-walking evaluator.** This is the load-bearing choice.
A blocking `read("/dev/sensor/contacts")` has to suspend the script *mid-
program* and resume it many ticks later, and the budget has to be countable in
instructions. Both are trivial with an explicit instruction pointer and value
stack, and both are miserable with a recursive tree-walk (you'd need
continuations or a host thread per script).

Blocking is implemented by *not* consuming the operands: a native call that
would block rewinds `ip` to the `OP_CALL` and returns `VM_BLOCKED`. Resuming
re-executes the same call with the same arguments. Retry is therefore
idempotent by construction rather than by care.

## D5. Everything is a file, and blocking is a game mechanic

`/dev/reactor/status`, `/dev/helm/thrust`, `/dev/sensor/contacts`. A device
file is a read callback plus a write callback plus a "would this block right
now" answer. The interesting failures the game is *for* — a stalled main loop,
two writers fighting over the helm, a stale reading — are all consequences of
this model, so the model is not a skin and must not be shortcut with a
side-channel API. There is no `get_shield_status()` and there never will be.

## D6. Home directories are a plain directory tree, zip-shaped from day one

`home/` is the template. A player's home is scripts + a manifest, no binary
state, no absolute paths, so `zip -r me.zip home/` and unzip-and-run is the
whole sharing story. Match results and replays go elsewhere (`runs/`) so a
home stays diffable and swappable.

## D12. The alarm reports a symptom; it does not name the cause

Playtest verdict on D10, from the person who asked for the game: "I'm a bit
worried the gameplay loop sucks." Correct, and specifically:

- the watchdog was a **lookup table**. `/dev/alarm` said `bench_drift` and the
  help said `bench_drift -> calibrate`. That is data entry, not deduction.
- **nothing escalated.** Three faults from a fixed table on a fixed ship. Once
  automated they were noise. "Rewarded for making the game boring" only works
  if the game produces new problems faster than you automate old ones.

Two changes:

**1. Symptoms, not faults.** `/dev/alarm` now reports what the ship can
*notice* — `bearing_unstable`, `bay_overheating`, `power_shortfall` — and each
has three or more possible causes spanning real faults, physics, and the
player's own choices. `bearing_unstable` might be a drifted bench (calibrate,
20 ticks blind), a starved sensor channel (reset a breaker), or simply a cold
bay (stop cooling so hard). Telling them apart means reading
`/dev/sensor/fault`, `/dev/bus/breaker`, `/dev/cpu/bay_temp`. Symptoms are
edge-triggered but re-announce every 80 ticks, because an alarm you can miss
once and never hear again is not an alarm.

**2. Heat wears the ship, and running cool anneals it.** A worn reactor makes
less power, which buys less cooling, which is more heat. That is a spiral you
can see coming and prevent, and it is what makes cooling worth buying even for
a script too lean to care about the throttle.

Wear was first modelled as a one-way ratchet with a cost per delivery. Both
were wrong: the ratchet floored every run by hour two regardless of skill, and
charging wear per delivery punished success. Wear is now purely thermal and
fully recoverable, so a well-run ship works indefinitely and a badly-run one
degrades and stays degraded.

The measurement that says this worked — same seed, same ship, 8000 ticks:

| watchdog | deliveries | wear |
|---|---|---|
| none | 12 | 100% |
| **reacts to every symptom the same way** | **12** | **100%** |
| actually diagnoses | 19 | 0% |

**A lookup table is now worth exactly nothing.** That is the property to
protect, and `tools/test_scenario.sh` fails if it stops being true.

## D11. A trigger is a blocking read

The ask was "many scripts triggered by different things", and the temptation
was an `on <event>:` syntax or a manifest mapping scripts to triggers. Neither
was built, because the file model already answers it:

    sleep(n)                wake in n ticks
    waitfor(path, value)    wake when the file reads as that value
    watch(path)             wake when the file CHANGES, return the new value
    read("/dev/alarm")      wake when something breaks

Four primitives, one shape, no new grammar. You get many scripts on many
triggers by writing many small files that each block on their own thing — which
is what the parallel-VM design already supported. A playtest of four scripts
(boot / pilot / watchdog / thermal) came to 91, 8719, 3330 and 9661 instructions
over a 3000-tick shift; three of the four were suspended at the end.

The load-bearing property is that **the readable way is also the cheap way**.
A suspended script costs one instruction per tick and makes no heat; a poll
loop costs hundreds and cooks the bay it runs in. Good style is not nagged for,
it is paid for. Locked in `tools/test_scenario.sh`: identical seed, identical
ship, the only difference being a `sleep(5)` — 9 deliveries at full budget
versus 7 at a throttled 1045.

Rejected: callbacks (need a scheduler the player cannot see), an `on:` block
(a second grammar for something the first grammar already does), and filename
conventions like `on-alarm.nom` (magic, and unreadable in `ls`).

## D10. The shift does not end, and things break on their own

Scenario one was a puzzle with an ending: reach the beacon, done. Solved once,
solved forever. Following the Tower Networking Inc. steer, arriving is now a
**delivery and a payment**, a new waypoint is issued immediately, and the shift
runs until the clock stops or the ship dies. Deliveries are the score.

Faults arrive on a seeded schedule and each one has three parts, deliberately:
a **visible symptom** in a device file, a **named alarm**, and **exactly one
command** that clears it. `radiator_fouled`, `breaker_tripped <channel>`,
`bench_drift`.

`/dev/alarm` blocks while nothing is wrong and delivers each new fault exactly
once. That is what makes the intended loop cheap: hand-fix the first fault from
a terminal, hand-fix the second, then write a twenty-line daemon that blocks on
`/dev/alarm` and never think about it again.

Measured payoff, same seed: the pilot alone gets crippled by the first breaker
trip and coasts into empty space for 2,700 ticks — **1 delivery**. Add the
watchdog — **12**. KICKOFF says you are rewarded for making the game boring;
this is that, with a number on it.

## D9. The resource is compute, and there is nobody aboard

**Supersedes D8.** D8 answered "who is the life support for?" with "an
unconscious crew". That works for one scenario and collapses the moment the
game becomes a continuous run — you cannot have a permanently comatose crew
across HAUL 3 of infinity. It patched the hole instead of removing it.

**Nobody is aboard. Nobody ever was.** The ship is a hull, a reactor and a
flight computer, and the player is the software. Oxygen is gone entirely. The
scarce resources are **power, compute and heat**, and they form a loop:

    power -> instructions -> waste heat -> warm optics
                                 |
                                 +-> too much heat throttles the computer

Why this and not FTL-style crew management:

1. **It deletes the fiction problem rather than excusing it.** An unmanned ship
   needs no air. The ship being unmanned is now the premise, not a hole.
2. **The resource is the same substance as the gameplay.** The player writes
   scripts; the scripts consume the budget. Optimising your own control loop
   *is* the progression mechanic. Nothing else in this genre gets to do that.
3. **It was already built.** The per-tick instruction budget existed as an
   invisible anti-cheat guardrail. Making it powered, thermal and visible turns
   that guardrail into the core mechanic for free.

Concretely: `/dev/cpu/status` reports `budget`, `executed`, `bay_temp` and
`throttled`. The budget a script gets is `budget_max` scaled by the fraction of
requested power the compute bus received, then again by thermal derating, with
an 8% floor so a brownout is never unrecoverable. **A brownout does not dim a
light; it makes the player's own code run slower.**

The behaviour that justifies the whole design fell out rather than being coded:
a script that never sleeps heats its own bay to 84C and gets throttled to 781
instructions/tick, while the same script with `sleep(10)` sits at 36C and keeps
all 2000. *The punishment for wasting compute is less compute.* Both cases are
locked down in `tools/test_scenario.sh`.

Kept from the old model: the cold-optics bias (the best mechanic in the build)
is thermal, not respiratory, so it survives unchanged — and it now has a
tighter cause, because the thing that warms the optical bench is the flight
computer executing the player's code.

## D8. There is a crew, and the crew is why there is no pilot (SUPERSEDED by D9)

Asked during the first playtest: if the player writes the software, who is the
life support *for*?

Without an answer the scenario has a program flying an empty ship to a beacon
for no stated reason, and its main resource clock is unmotivated. The answer
that makes the whole premise cohere, rather than patching one hole:

**Four people are aboard, and they are unconscious.** That is *why* the ship is
being flown by a script — there is nobody left to fly it. It is why the beacon
is worth reaching. And it is why oxygen is a clock rather than a number.

This costs nothing mechanically (`/dev/life/status` now reports `crew 4`) and
it retires a fiction problem that would otherwise have grown a scenario at a
time. The alternative considered and rejected was to drop life support and make
the resource purely thermal — coherent for an unmanned ship, and it would suit
the cold-optics mechanic, but it removes the only stake in the scenario and
leaves "reach the beacon" as an errand.

**Known weakness, not yet fixed:** oxygen is currently a *weak* clock. Once
life support has full power O2 climbs back to 100% and never threatens a
competent run again, so in practice the pressure is the tick ceiling, not the
crew. The fiction is now right; the tuning is not. Options are lowering regen
below break-even so O2 is a genuine budget, or making cabin heating and
scrubbing compete for the same channel. Deliberately not retuned in the same
change that fixed the fiction — the balance is under test and one variable at a
time.

## D7. Scenario one has no combat

Cold derelict: power is out, life support is failing, route power, bring
subsystems up in order, reach the beacon. Six device files. If this isn't fun
with a terminal and a schematic, lasers won't rescue it — and the honest
report says so.
