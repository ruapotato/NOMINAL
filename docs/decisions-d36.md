# D36. Their screens, and the lead somebody had to plug in

## What was asked for

The owner, looking at a floor of tenants:

> "Those people at the desks don't seem to show a 2d interface like the one in
> the IT room. I'd like those to act a lot like our main one, but with whatever
> software the end user is using / the user sets up for them."

and, in the same breath:

> "And I don't see cabling for any of the boxes, like they should all end up
> wired by the person playing, real computers not just 3d objects."

Both halves are about the same thing: a tenant's desk was a real device in the
site model — a card that really asks for DHCP, really pulls files, and really
answers a shell when you `sit` at it — drawn as a dark rectangle on a stick
with an invisible wire.

`docs/screenshots/d36-before-call-centre.png` is what he was looking at, and
`d36-after-call-centre.png` is the same room, same seed, same day, afterwards.

## The constraint that decides the whole thing

A booted `Machine` is 18.3 MB (D31, measured) and a full tower is 176 desks. So
176 live desktops is 3.2 GB against a world that is meant to be 73 MB, and 176
live `SubViewport`s is 176 render targets. Neither is on offer.

That leaves exactly one honest shape, and it is a split rather than a
compromise:

**Across the room, a picture of what the model knows. In the chair, the
machine.** Nothing on the glass claims to be a screenshot of a program, and
nothing you type at goes anywhere but a real operating system.

### What is real on those screens

Every number driving `screens.gd` is read out of the session by tower.gd,
and each one is a column somebody can print for themselves:

| what you see | where it comes from |
|---|---|
| WHICH software is on the screen | the tenancy's **trade**, off `service`'s own trade column: office, voice, web host, studio |
| how much of it is lit | the tenancy's **`done`** fraction — 18/18 calls, 80/80 transfers — the number the rent is paid on |
| a red screen with the link error | that desk has no link: `service`'s **`up`** column, the same one that decides whether the person at it has their hand up |
| an amber bar over greyed work | link, and no address: **`addr`**. `up 20 addr 0` is twenty cables and no dhcp, and now the room says so |
| which desk shows what | `up` and `addr` are counts, not names, so they are spent in install order — t7d0 first — which is the same order the postures are spent in, so a screen never disagrees with the person sitting at it |

### What is a depiction, said out loud

**The layout.** There is no window manager on a tenant's disk, no spreadsheet,
no soft phone. A transfer list, a call panel with a trace on it, a request
graph and an ingest timeline are the SHAPE of that trade's work at the size you
can read across a room. They are not photographs of programs and they contain
no text, because text you cannot read is text that is pretending.

This is the one place in this project where a picture is not a claim about the
machine, so it is written at the top of `screens.gd` in those words, and the
line between the two halves is drawn where a player can feel it: walk up, press
[E], and what replaces the picture is a shell.

## The chair is the real half

`sit <desk>` has existed since D31 and did nothing whatever to the window: a
socket client got a prompt and the player at the keyboard got no way in at all.
Now the desk device is something you can aim at — the crosshair reads
`t1d3 -- Ola Jelinek   [E] sit down at their machine`, the name off `desks
<tenant>` — and [E] runs core's own `sit`.

**The window follows the session, not the key.** `_reconcile_seat()` watches
`ses_state()`'s `where` for SES_SEAT and reads WHICH desk off
`session_prompt()`'s `desk:t1d3#`, so `sit t1d3` typed at the socket opens the
same terminal in the running window that [E] does. That is D23's rule applied
to one more thing, and it is why a screenshot of a sitting is a screenshot of
the machine rather than of the room the socket client left behind.

The terminal is `terminal.gd` with `on_command` bound to `site()` — `ses_cmd()`,
the socket's own call — and `prompt_fn` bound to `ses_prompt()`. There is no
second front end and no second opinion: `whoami` answers `root` because a
program ran. Lines typed over the socket while seated are echoed into that
terminal, so an agent driving the game down the wire can photograph what it
did. `docs/screenshots/d36-seat.png` is a desk with no lead in it, from its own
console: `<UP,NO-CARRIER>`, `inet none`, and `/etc/net/interfaces` saying
`address dhcp` and meaning it.

[Esc] does not close the window and then tell core. It types `stand`, core frees
the machine, and the window closes because the session stopped saying you were
sitting down. While you are in the chair that desk's depiction is REMOVED from
the world — there is a real machine behind that one glass, and painting a
picture over the top of the real thing is the only lie this file could still
have told.

## The cabling: it was all there, and none of it was visible

`_draw_cables()` has always drawn every link the site holds, desks included.
The picture was the problem, in two ways, and both are fixed by making the
drawing agree with where copper would really be:

**1. It flew across the office at ceiling height.** A tenancy's office has no
containment in it — an office is not in `TRAY_KINDS` — and the route climbed
straight up out of the machine under the desk to tray height and crossed the
room two and a half metres in the air, where a 6 mm lead is a hairline against
a bare ceiling. What really happens on a floor somebody has patched off a spool
is that the leads run along the skirting to the door and go up in the corridor,
so that is what the leg inside their room does now: 60 mm off the floor, past
the feet of the desks, out through the door, and the climb happens at the cell
OUTSIDE the doorway. `game/tests/tower.gd` measures it — no point of a desk's
run that is inside the room it serves may be more than 750 mm off that floor —
because it looks correct in the data either way.

Two smaller things fell out of it. The cell either side of a doorway is now
kept when a skirted room is involved (the corner-simplification threw away the
entire in-room leg, which is why the first attempt still drew the diagonal),
and a span lying on a floor no longer sags, because the floor is holding it up
and half of the sag was inside the slab.

**2. It was drawn at the diameter of the cable rather than the diameter of the
lead.** `CABLE_R` was 3.2 mm — 6.4 mm across, which is exactly what cat5e is,
and one pixel at four metres. It is 9 mm across now, which is a patch lead with
a moulded boot on it. **The metres and the price are untouched**: this is the
diameter of the picture, not of the copper the game charges you for, and it is
the only fudged number in this record.

An office that has not been cabled is now unmistakable from the doorway without
opening a panel: hands up, no copper on the floor, and twenty red screens.
`docs/screenshots/d36-after-office-unwired.png`.

## What it costs, measured

Same tower (`--seed=7008`), same view, same day, back to back — a HEAD checkout
of `game/` in one process and the working tree in the next, both against the
same `people.gd`, both while the machine was busy with another agent's window:

| | fps | process | draw calls | primitives |
|---|---|---|---|---|
| before | 23 | 55.8 ms | 339 | 331,210 |
| after | 24 | 54.2 ms | 341 | 337,158 |

**Two draw calls and 5,948 primitives** for thirty-eight screens across two
floors and the extra copper. A screen is two triangles; the whole picture is a
fragment shader off `TIME` and four floats of `INSTANCE_CUSTOM`, so nothing is
stepped per frame per desk on the CPU. It is one MultiMesh per floor for the
same reason people.gd is: a MultiMesh is culled by the AABB of its buffer, and
one buffer for the tower is a buffer the size of the tower.

`perf` reports it now — `38 screens in 2 multimesh buffers: 0 no link, 0 no
address, 38 working` — which is both the cost and what is on the monitors, for
a client that cannot see the window.

## The judgement calls

### Where is the glass?

**Read off the desk mesh, not copied out of it.** `people.gd` tags the monitor
glass `P_SCREEN` so its own shader can flicker it, and `screens.gd` finds that
rectangle by reading the mesh back and taking the AABB of the tagged vertices.
Another agent was reshaping that file while this one was being written, which
is exactly the case this defends against: move the monitor and the picture goes
with it. The alternative was six numbers living in two files.

### Does a screen with no address show the software or an error?

**The software, greyed, behind an amber bar** — and no link is the darker case,
the screen almost off with a red bar. They are two different faults and the
player's move is different for each: one wants a lead, the other wants a pool.
A single "broken" screen would have thrown that away, and it is the same amber
and red the door beacons already use.

### Should the depiction move with the day?

Yes, and only as far as the model does. The transfer that is still going fills
on that desk's own phase; the waveform on a call centre's screen has holes in
it in the proportion the tenancy's calls are failing; a studio's playhead only
moves while the stream is going out. All of it scales off `done`, so a room
that is having a bad day looks like one before you read a number.

## What was NOT done

- **"...or the user sets up for them" is not built, and I am not going to
  invent a hook for it.** There is nothing a player configures today that
  changes what a tenant's software IS. The nearest honest thing already shows:
  the `files` column — which server their people pull off, which is entirely
  the player's decision — moves `done`, and `done` is how much of the screen is
  lit. A real version of the ask is a verb that installs something on a
  tenancy's desks, and it would have to live in `core/site.c` because a
  tenancy's fit-out is core's, not the view's. That is a record of its own.
- **A sat-at desk's glass is blank to the room rather than showing the live
  terminal.** One live SubViewport for the seated desk would be affordable —
  the cap of one is the same argument D31 makes about the Machine — but while
  you are seated your view IS that screen, so it would be a render target
  nobody can see. It would only matter for a spectator, and there is not one.
- **The screens carry no text.** A tenancy's name, an address, a clock: all of
  it is legible only close up and none of it is derivable at the resolution the
  glass gets on screen. Text that cannot be read is decoration that looks like
  a claim.
- **The lead crosses the office floor diagonally** rather than following the
  wall to the desk. It is a run on the floor, which is the point, but a
  fit-out would take it round the perimeter. `_tray_route` is a breadth-first
  search over metre cells and giving it a preference for wall cells is a
  bigger change than this hour had.
- **Nothing measures any of this in `--loadcheck`.** The calibration builds
  towers over a pipe with no window in it, so the frame cost is a `perf`
  reading against a running window, as it was for the crowd.
