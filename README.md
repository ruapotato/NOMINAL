# NOMINAL

A programming game in the lineage of Robocode and Crobots, except you fly a
spaceship and the interface is an operating system.

You do not pilot the ship. **You write the software that flies it.** Every
subsystem is a file — `cat /dev/reactor/status`, `echo prime > /dev/reactor/ctl`,
and `/dev/sensor/contacts` blocks until the sensor has something to say. The
file model is not a skin over the game; the interesting failures come from it.

Nobody is aboard. The scarce resources are power, compute and heat, and they
form a loop: power buys instructions, instructions make waste heat, waste heat
is what the optical bench needs to give you an honest bearing — and too much of
it throttles the computer that made it. Your own code is a load on the system
you are trying to fly.

The loop is: workshop → launch → watch it fail → post-mortem → fix the root
cause so it never fails that way again.

## Build and play

    make                       # build/nominal
    make check                 # language tests, scenario tests, determinism gate

    ./build/nominal --headless --home home --seed 7 --log
    ./build/nominal --serve --port 7777 --home home
    telnet 127.0.0.1 7777      # then: help

    make gdext                 # the GDExtension
    ./Godot_v4.7.1-stable_linux.x86_64 --path game

## Layout

    core/       the whole game in plain C11: virtual file tree, NomScript
                compiler and VM, simulation, shell, TCP server. No third-party
                dependencies, no knowledge of Godot.
    gdext/      thin plain-C GDExtension binding core/ into Godot
    game/       the Godot project: the station desktop
    home/       the default player home directory (scripts + examples)
    tools/      test harnesses, the determinism gate, a reference socket client
    docs/       decisions and why, the socket protocol, screenshots

## The three things that came first

Before any of the game: a real network interface, deterministic replayable
matches, and a headless mode. See `docs/decisions.md`.

    ./build/nominal --headless --seed N --home path/ --out replay.json

Same seed plus same scripts produces a byte-identical replay, and
`tools/check_determinism.sh` also checks that an `-O0` build and an `-O2`
build agree — which is what catches floating-point contraction before it
becomes unfixable.

## Status

A shift is playable end to end from a terminal, from the socket, and from the
in-game desktop: bring a cold derelict up, fly waypoint to waypoint, and keep
working while things break on their own. No combat yet, and no economy beyond
credits — you cannot spend them.

Three numbers stand in for "is it fun", all regression-locked in `make check`:

- **diagnosis beats reflex.** Over an 8000-tick shift: no watchdog, 12
  deliveries and a worn-out ship. A watchdog that reacts to every symptom the
  same way — also 12, also worn out. A watchdog that reads the other device
  files and works out which of three causes it is actually looking at — 19
  deliveries, zero wear. A lookup table is worth nothing.
- **good code buys thrust.** The same pilot with and without a `sleep(5)`:
  14 deliveries at full budget versus 7 at a throttled 415, because a poll
  loop cooks the bay it runs in.
- **same seed, byte-identical replay** on Linux and Windows.
