# D13. The ship is a machine you administer, not a vehicle you fly

This supersedes the shape of D9/D10/D12, though not their mechanics.

## The complaint

> "As is I don't think it's much of a game with endless ways to solve the
> challenges presented. I want it to feel like a day in the life of a sysadmin,
> in space."

Correct. The previous build had **exactly one right answer to every problem**,
because every subsystem was hardcoded C with one knob:

- bay too hot? buy radiator power. That is the only lever.
- bearing wrong? calibrate. Or wait for the bay to warm. Two levers, both
  scripted for you in the examples.
- not enough compute? there is nothing you can do. `budget_max` is a constant.

A puzzle with one solution is solved once and then it is content you have
already consumed. That is why it read as a puzzle and not as a job.

## The change: hardware is data, not code

The ship is now a chassis with **slots**. A slot holds a **part** bought from a
catalog. Parts have a power draw, a heat output, a spec, a firmware revision
and a failure rate. `/dev` is **generated from what is actually installed** —
install a second compute card and `/dev/cpu1` appears; pull the sensor and
`/dev/sensor0` is gone and every script that touched it starts failing.

This is what produces many solutions to one problem. "The computer is
throttling" now has at least six answers, and which is correct depends on your
credits, your free slots, your power budget and how you wrote your scripts:

1. buy a radiator and spend power on cooling
2. buy a *better* radiator with the same draw and a higher spec
3. downclock the CPU in `/etc/cpu0.conf` — fewer instructions, less heat
4. buy a second, slower CPU and split your daemons across both
5. rewrite the offending script to sleep more
6. pull something else out of the bay to free the power and the thermal budget
7. accept the throttle, and buy a bigger reactor so the wear does not spiral

None of those is *the* answer. That is the point.

## The sysadmin day

The fantasy is not piloting. It is:

- a ticket arrives; you have hardware and a budget and a deadline
- you read `/var/log/` and `/proc/` to work out what is actually wrong
- you edit a config file in `/etc/` and restart a service
- something you bought last week fails at the worst moment
- the fix you apply at 3am is the one you automate on Tuesday

So the OS has to be real enough to administer: `/proc/<pid>` for every running
script, `/etc` config that daemons actually read, `/var/log` that actually
accumulates, `/sys/slot/N` for the physical bay, and a shell with the verbs a
sysadmin reaches for.

## The DE

Per Hamnix `docs/de_scene_file_arch.md`: **a window publishes a display list as
a human-readable file; the compositor reads those files, z-orders them, and one
rasterizer turns the result into pixels.** `/dev/wsys/<wid>/scene`.

The engine renders nothing it invented. Godot is the rasterizer and the input
router; every pixel traces to a line of text a program wrote. That means the
whole desktop is inspectable over the socket (`cat /dev/wsys/3/scene`),
diffable, and replayable — and it means an in-game program can open a window,
which a hardcoded Godot UI could never allow.

## What is kept

The compute/heat/wear loop (D9), the continuous shift (D10), symptoms rather
than causes (D12), and blocking reads as triggers (D11) all survive unchanged.
They stop being the whole game and become the physics that the hardware plugs
into.
