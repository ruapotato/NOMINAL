# NOMINAL — project kickoff

You are starting a new game from an empty repo. Read this whole file before
writing code.

## What we are building

**NOMINAL** — a programming game in the lineage of Robocode and Crobots, but
you fly a spaceship, and the interface is an operating system.

You do not pilot the ship. **You write the software that flies it.** Between
runs you sit at a workstation on a station/dock: a small windowed desktop with
a script editor, a sensor feed, a power-grid view, a damage schematic, and
terminals. You write scripts, wire them to device files, and launch. Then you
*watch your code fly* and find out what you got wrong.

Every subsystem is a file. `cat /dev/shield/status`. `echo fire > /dev/laser/trigger`.
`/dev/sensor/contacts` blocks until something appears. That is not a skin over
a game — the file model *is* the game, and the interesting failures come from
it: a read that blocks on a dead sensor and stalls your main loop; two scripts
both writing `/dev/helm/thrust`; a stale reading that makes you fire at where
the target *was*; a power budget that browns out the laser mid-charge.

**The loop is:** workshop → launch → watch it fail → post-mortem → fix the root
cause so it never fails that way again. You are rewarded for making the game
boring. That is the design thesis; protect it.

The name is a spaceflight term ("all systems nominal") and it is meant to be
quietly funny every time things are not.

## The single most important requirement

**You — Claude — must be able to play this game yourself, as if you were a
person, without a human present.** That is not a testing convenience; it is the
architecture. Development on this project is expected to be largely autonomous,
and you cannot improve a game you cannot play.

That forces three things, all first-class from day one, none bolted on later:

1. **A real network interface.** The game listens on a local TCP port and
   speaks a plain line-based protocol — effectively telnet/ssh into the
   station and into ships. Everything a player can do at the desktop must
   also be doable over that socket: read files, write files, edit and run
   scripts, launch, observe, read results. You will drive the game through
   this. (It is also a genuine headline feature: this audience will *love*
   attaching with their own terminal, tmux and vim.)

2. **Deterministic, replayable matches.** A run takes a seed plus the player's
   scripts and produces a match file that replays identically on any machine.
   This is what lets you verify a change did not alter behaviour, and it is
   also what makes asynchronous PvP and shared replays possible later. Getting
   determinism right at the start is cheap; retrofitting it is agony. No
   wall-clock time, no unordered iteration, no uninitialised reads in the sim.

3. **A headless mode.** `--headless --seed N --home path/ --out replay.json`
   must run a full encounter with no window and print a machine-readable
   result. This is how you iterate in seconds instead of minutes.

**Ship these three before you ship a laser.**

## Decisions already made — implement, do not relitigate

- **Engine: Godot 4.7.1** (binary is in the repo root). Must export to **Linux
  and Windows**. Do not add dependencies that break either.
- **The ship scripting language is a Python-syntax language of our own**, a
  subset modelled on `nomsh`. **Do not embed Lua, Python, or any third-party
  interpreter.** We own the interpreter because we need: a sandbox by
  construction (the only I/O that exists is the virtual file tree), exact
  determinism, and a **per-tick instruction budget** — otherwise the first
  player to write `while True: pass` hangs the simulation and the first clever
  one wins by burning more CPU than their opponent.
  Implement it as a **GDExtension (C or Rust)**, not GDScript, so stepping
  thousands of instructions per tick stays cheap.
- **Graphics: terminal + 2D schematic.** The encounter view is a tactical
  sensor display — vector/wireframe, high contrast — *not* a camera on space.
  This is diegetic, not a compromise: you are looking at what the ship's
  sensors render. It also means no art pipeline.
- **An in-game desktop**, diegetic: it is the station's workstation, not a
  menu. Familiar *grammar* (windows, panel, launcher — instantly readable),
  strange *dialect* (unusual affordances, dense instrument-panel typography,
  but like nomnix). Terminals in it are
  sessions into ships.
- **Home folders are shareable.** A player's scripts live in a home directory
  that exports to a single zip and imports cleanly, so people can swap setups
  and run each other's. Design the layout for this on day one.

## Reference material

`~/NomnixOS` is a from-scratch operating system with a Plan 9-shape file model,
per-process namespaces, a `/net` file tree, a scene-file desktop, and a
Python-syntax shell (`user/nomsh.ad`). **Read it for how the OS should be laid
out and how the file model should behave** — device files, what blocking on a
dead device does, how a restricted namespace fails, what a shell of this shape
looks like.

**Do not depend on it, link against it, or try to run it.** It is the design
spec and the reference implementation, not a dependency. NOMINAL reimplements
the *model* in a few thousand lines inside the game. The player cannot tell
whether a real kernel is underneath; they can only tell whether it *behaves*
right, and NomnixOS is how you know what right looks like.

Useful specifically: `sys/src/9/port/` (device/file-server shape),
`user/nomsh.ad` (the language and its interpreter), `docs/architecture.md`,
`docs/de_scene_file_arch.md` (windows as display lists in file servers).

## Build this first — a vertical slice, no combat

Resist simulating everything before shipping anything.

**Scenario one: a cold ship.** No weapons, no enemy. You arrive at a derelict,
power is out, life support is failing. Route power, bring subsystems up in the
right order, keep life support alive, reach the beacon. Six device files, one
ship, one map.

If *that* is fun with a terminal, a schematic and the desktop, the game works
and everything after it is content. If it is not, lasers will not rescue it.
Say so plainly rather than adding features to cover it.

## Quality bar

- **Verify by playing, not by asserting.** After any change that touches the
  sim, the language or the UI, *play a round over the socket* and read what
  happened. Screenshot the desktop and **look at it**. A passing unit test that
  nobody played through is not evidence the game works.
- **A test that stubs the thing under test is blind.** If you mock the
  interpreter to test the sim, you have tested neither. Prefer end-to-end runs
  through the real socket and the real interpreter.
- **Never claim what you have not measured.** If you did not run it, say you
  did not run it. If a feature half works, say which half.
- **Determinism is a gate.** Same seed plus same scripts must produce a
  byte-identical replay, on Linux and on Windows. Add a check for this early
  and run it on every change to the sim.
- Keep a short `docs/` note of design decisions and *why*, especially ones you
  rejected. Future-you will otherwise re-litigate them.

## Traps

- **Onboarding is the hard problem.** The nearest comparable game is criticised
  for a steep curve. The first twenty minutes must be one script, one device,
  one visible consequence — `while True: if proximity < 10: power_down()`.
  Earn the complexity.
- **Watching your code run can feel passive.** Keep encounters short and
  restarts instant; the fun is iteration count, so make "try again" take
  seconds.
- **The fun is deduction, not simulation depth.** Build the smallest system
  that can produce a *confusing* symptom — a slow subsystem causing a timeout
  three systems away — and spend the effort on making the diagnostic tools
  honest and the false leads plausible. Depth without ambiguity is a config
  editor.
- Do not gold-plate the desktop before the loop is fun. It is the shell around
  the game, not the game.

## First report back

1. What you laid down (repo layout, Godot project, GDExtension skeleton).
2. The socket protocol, with a transcript of **you** playing a round through it.
3. The determinism check and whether it passes.
4. A screenshot of the desktop, and your honest opinion of whether the cold-ship
   scenario is fun yet.
5. What you are least sure about.
