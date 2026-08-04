# The station socket protocol

`nominal --serve --port 7777 --home home/` listens on **127.0.0.1 only** and
speaks a line protocol you can drive from `telnet`, from a script, or from the
in-game terminal — which calls the identical `shell_exec()`, so the desktop and
a remote session cannot drift apart.

## Shape

One request per line, terminated by `\n` (a trailing `\r` is tolerated so
telnet works). Every response is:

```
<status line>
<body line>
<body line>
.
```

- The status line is `+OK <summary>`, `-ERR <what went wrong>`, or `+DATA`
  (see `put`).
- Body lines are optional.
- A lone `.` terminates every response, including errors.
- A body line that begins with `.` is sent as `..` (dot-stuffing). Undo it on
  receipt. `put` input is dot-stuffed the same way in the other direction.

That is enough structure to parse without ambiguity and plain enough to type
by hand, which is the whole point.

## Session

```
$ telnet 127.0.0.1 7777
+OK NOMINAL/1 station shell, scenario cold-ship, seed 7
type 'help' for commands; every response ends with a lone '.'
.
```

Sessions share one station. Several terminals onto the same ship is the
intended arrangement, not a bug.

## Commands

### files

| command | meaning |
|---|---|
| `ls [path]` | list a directory |
| `cat <path>` | read a file or device |
| `put <path>` | upload: replies `+DATA`, then send lines, then a lone `.` |
| `write <path> <text>` | write one line — this is how you poke a device by hand |
| `rm` / `mkdir` / `cd` / `pwd` | as expected |

`cat` on a device that has nothing to say answers
`-ERR <path>: would block (device has no reading yet)`. A terminal cannot
suspend the simulation the way a script can, and pretending the device
returned an empty string would be a lie.

### ship

| command | meaning |
|---|---|
| `dev` | list the device tree |
| `attach <path>` | attach a script |
| `detach` | detach every script |
| `launch [seed]` | reset, compile, start |
| `step [n]` | advance n ticks (default 1), then print status |
| `run [maxticks]` | advance until the run ends |
| `reset [seed]` | back to the cold derelict |
| `seed <n>` | set the match seed |
| `budget <n>` | VM instructions per script per tick |

### observe

| command | meaning |
|---|---|
| `stat` | full ship status |
| `log [n]` | last n events |
| `ps` | attached scripts |
| `result` | result JSON |
| `replay [hostpath]` | replay JSON, optionally written to disk |
| `save [hostpath]` | write `/home` back to the host filesystem |

## A whole round, by hand

```
> detach
+OK detached all scripts
.
> attach /home/examples/01-chase.nom
+OK attached /home/examples/01-chase.nom (1 script(s))
.
> launch 7
+OK launched: seed 7, 1 script(s), budget 2000
.
> run
+OK ran 289 tick(s), run is 'won'
run       won  tick 289  seed 7
outcome   reached the beacon at tick 289 with 84% O2 remaining
reactor   online  output 6.50 MW
battery   40.00 / 40
bus       life 2.00/2.0 sensor 1.50/1.5 helm 3.00/4.0 comms 0.00/0.5
life      O2 84.91%  cabin 19.6C  scrubber on
sensor    online  bearing 343.5  range 58.5
helm      heading 343.1  thrust 1.00  speed 5.716
position  982.4, -292.9   beacon range 58.5
hull      100.0%
script    01-chase.nom     sleeping  steps 2020
.
```

## Diagnosing a stuck script

`stat` reports what each script is doing, and for a suspended one it names the
file it is suspended on:

```
script    attempt1.nom     blocked   steps 447  blocked on /dev/sensor/contacts
```

That single line is the post-mortem for the most common first mistake: reading
`/dev/sensor/contacts` before the sensor has power. The read never returns, so
the rest of the boot sequence never runs, so the reactor sits at `idle` and the
crew suffocates several hundred ticks later — a long way from the line that
actually caused it.

## Client

`tools/play.py` is a small reference client (a `Station` class with `send`,
`put` and response framing). It is a client, not part of the game: anything it
does you can do with telnet.
