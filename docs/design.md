# NOMINAL — the design

One live document. The thirty-odd decision records that used to sit beside it
described an office building, then a space station, and were mostly stale; they
are deleted rather than left to lie. This file says what the game is now.

## What it is

**A space exploration game played from the perspective of the chief engineer.**

You are the engineer of a ship. You do not fly it and you do not fight — the
crew do that, automatically, and whether they can depends entirely on what you
have wired up and whether it is still working. You spend your time running
cable, running power, carrying kit, and repairing what the last fight broke.

The verbs are **cabling and maintenance**. Not networking.

## The pivot that got here

This was an office building, then a space station, and by August 2026 it had a
genuine packet-level network simulation in it — frames on a wire, MAC learning,
ARP, VLANs, TCP, DHCP. That was built to David's requirement and it worked, and
it turned out to be the wrong game:

> "We've sunk way too much time into making this network accurate game when
> really we care about connections now. No need to simulate a network that's
> just gonna be is connected true or false."

The tell, from the last playthrough: two switches, one addressed router leg,
seventeen desks that cabled up perfectly and never got an address. Correct
networking. Terrible game.

**A link is now `connected: true/false`.** A device works if there is a powered
path to it and the things it depends on are alive.

## Where the depth goes instead

Cutting packets does not leave "is it plugged in". The strategy moves to
**power, redundancy and placement**:

- Everything needs power, down conduit, from the core. Every run has a budget.
- A path to a critical system is ONE path until you build two. When a run is
  severed in a fight, what goes dark is decided by the topology you laid.
- Placement is physical: how far you have to walk when it fails mid-battle,
  what is behind what, what shares a conduit with what.

That survives cutting the network, and it is what an engineer actually worries
about.

## The ship

**A ship, not a station.** Stationary in Godot's space — the stars and the
other ships move around it, so it reads as flight without the hull ever moving.
That is an implementation detail the player never sees.

Not a Star Trek hull, but that family: a command section, an engineering hull,
and drives. Real windows. The sliding doors and the cable trays survive from
the station; the deck layouts do not.

**Systems, and where they logically live** — the layout follows the systems
rather than the systems being dropped into rooms:

| system | needs | gives |
|---|---|---|
| life support | power | the crew can be aboard at all |
| computer core | power | every terminal on the ship |
| shield array | power, computer | absorbs damage |
| sensor array | power, computer | targets to shoot at, and navigation |
| weapons | power, computer, sensors, bridge terminals | shooting back |
| engines | power, computer | evasion, and going somewhere |
| bridge terminals | power, computer | the crew can use any of the above |

**On day one only LIFE SUPPORT is cabled**, because without it there is nobody
aboard to play as.

## The computer

The emulated OS stays, and gets a reason to exist that it never had before:
**there is one computer**, the core, in Engineering. Every terminal on the ship
— the bridge officers' consoles included — is a terminal onto that same
machine. So the OS is the ship's computer, the way it is in Star Trek.

That means you can still script it, automate with it, and debug it when the
ship computer is what is wrong. It is not the primary loop and nothing in the
game should REQUIRE a command line, but it is there and it is real.

It is also how you tell the ship where to go.

## The loops

### 1. The fit-out, which is the opening

Nothing works but life support. You choose what to bring up first, and the
choice has consequences:

- **Shields first** is the path of least resistance: the first attack is light,
  and something absorbing it buys you time to wire everything else.
- **Weapons first** needs the sensor array AND the bridge terminals before it
  can hit anything, so it is three jobs, not one.
- **Engines first** buys evasion instead of absorption.

If you are fast and focused you should be able to get all of it up before the
first attack arrives. Balancing that window is a job for after it runs.

### 2. The fight, which you do not fight

Combat is **automatic and real**. If there are sensors, working terminals,
crew at the bridge and a working weapon system, the ship targets and shoots,
dogfight style, on its own. Your part is that every one of those four things is
a thing you wired and a thing that can break.

### 3. Repair, which is the game

Fights break things. A severed run, a burnt relay, a dead array. Alarms and a
marker; you walk there, see what is wrong — scorched conduit, a dead port, no
power light — and fix it: replace the part, rerun the conduit, re-cable. While
you work, whatever was behind it is offline.

### 4. Where to go, which is the pressure

You set a destination at the computer and the crew takes the ship there.
**Danger is a property of the region.** Staying in safe space means never
facing a heavy load. Going somewhere interesting means you had better have
built redundancy, because out there things will be severed faster than one
engineer can re-run them.

There is **no money and no rent**. The pressure is that attacks grow more
common and your ship is only as good as the last repair you made.

## What is kept, cut, and open

**Kept:** the emulated OS and everything under it; conduit and the power model;
the cable spool and tray routing; sliding doors; the device catalogue; the day
clock; damage and events; the 3D and its socket-driven interface.

**Cut:** `netstack`'s packet simulation and everything built on it — the
per-trade traffic model, port counters, `load`, most of `--netcheck` and
`--loadcheck`; tenants, rent, complaints; the building generator.

**Open, and to be settled by playing it:** the length of the pre-attack window;
how fast the danger curve rises; whether repair wants a parts inventory or just
an order-and-carry.

## The rule that does not change

The view is never the source of truth. The ship, its systems, its power and its
damage live in the model, and the 3D is a view of them — because every quality
gain in this project has come from playing it blind over a socket, and anything
that cannot be driven that way rots.

Every technical claim the game makes about itself must be true of the machine,
verified by running it.
