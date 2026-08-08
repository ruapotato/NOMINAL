# NOMINAL

You are the chief engineer of a starship. You do not fly it and you do not
fight — the crew do that, and whether they can depends entirely on what you
have wired up and whether it is still working.

You spend your time running cable, running power, carrying kit into place, and
repairing what the last fight broke.

## The ship

A hull described the way a hull really is: in **frames**, slices across the
keel lofted one into the next. A wide low command wedge at the bow, a narrow
tall neck, a smaller engineering hull slung below and behind it, and a drive
ring at the stern that the keel passes through.

Everything inside is fitted to that envelope afterwards, which is the right way
round — the outside decides the inside. A **deck** is not a plan somebody drew:
it is wherever the pressure hull has standing room above the floor, so no two
decks are the same shape and some of them do not join the bow to the stern at
all.

    make build/hullshot && build/hullshot > /tmp/hull.ppm    # profile, plan, bow
    make build/deckshot && build/deckshot > /tmp/decks.ppm   # every deck as a plan
    make build/deckwalk && build/deckwalk                    # can you get everywhere

**There is one ship, and it is the same every run**, the way a crew knows one
ship. Where the shafts are, which deck crosses the neck, how far it is from the
core to the bridge at four in the morning — that knowledge is most of what
being the engineer feels like, and a hull that changed every run would throw it
away at the start of every run.

She is 171 m long over six decks, 15,205 m² of floor. Deck 0 is engineering
hull only, because the command section rides too high to have floor at the
keel. Deck 2 has 2,035 m² of floor with only 220 of it reachable from the bow —
floor at both ends and no walkable route between them. Deck 4 is the command
lozenge at its widest, 4,026 m². Nobody wrote any of that down; it falls out of
the frames.

Getting between the two halves means finding a shaft. Seven of them — four
turbolifts and three stairwells — placed by searching for the spots with floor
on every deck of a run, so on deck 2 you go down, cross, and come back up.

## The loops

**The fit-out.** On the first morning only life support is cabled, because
without it there is nobody aboard to play as. Everything else — shields,
sensors, weapons, engines, the bridge terminals — is dark, and you choose what
to bring up first. Shields are the path of least resistance: something
absorbing the first attack buys you time. Weapons need the sensor array *and*
the bridge terminals before they can hit anything, so they are three jobs
rather than one. Engines buy evasion instead of absorption.

**The fight, which you do not fight.** Combat is automatic and real. Given
sensors, working terminals, crew at the bridge and a working weapon system, the
ship targets and shoots on its own. Your part is that every one of those four
is a thing you wired and a thing that can break.

**Repair, which is the game.** Fights sever runs, burn relays and kill arrays.
An alarm, a marker, a walk; you see what is wrong — scorched conduit, a dead
port, no power light — and you fix it. Whatever was behind it is offline while
you work.

**Where to go, which is the pressure.** You set a destination at the computer
and the crew takes the ship there. Danger is a property of the region: safe
space never throws much at you, and anywhere interesting will sever things
faster than one engineer can re-run them unless the ship was built with
redundancy in it.

There is no money and no rent. The pressure is that attacks grow more common
and the ship is only as good as your last repair.

## Where the difficulty comes from

Not from a difficulty number, and not from simulated packets. **Power,
redundancy and placement.**

Everything needs power, down conduit, from the core, and every run has a
budget. A path to a critical system is one path until you build two. When a run
is severed in a fight, what goes dark is decided by the topology you laid — and
by where you put things, because the walk to the fault at four in the morning
is metres of real ship, and the neck that everything routes through is a single
point of failure until you give it a second one.

## The computer

There is one computer: the core, in engineering. Every terminal aboard — the
bridge officers' consoles included — is a terminal onto that same machine, the
way it is on a starship.

That computer is real. An RV64IM CPU written from scratch, real compiled
binaries, real syscalls, a dynamic linker that resolves real library versions, a
package database on disk, an init system, services with dependencies, Plan 9
namespaces, and a boot chain that fails at the stage where something is actually
wrong. When you type `netstat` on it, a program runs on an emulated processor
and reads state the kernel really keeps.

So you can script it, automate with it, tell the ship where to go with it, and
debug it when the ship's computer is what is broken. **Nothing in the game
requires a command line** — it is there for the people who want to go deeper,
and because a ship's computer that was a prop would make everything else here a
prop too.

## Running it

    make bf                 the simulator and its harness
    make gdext              the Godot extension: the same machine, in the game
    make test-cpu           the emulator against qemu, and Linux against Windows
    make test-break         the full gate set

    ./build/bf --health     every pristine machine boots with every service up
    ./build/bf --solve 60   every generated fault is findable and repairable
    ./build/bf --mancheck   every command example in every manual and README,
                            RUN on a booted machine

    ./Godot_v4.7.1-stable_linux.x86_64 --path game

## The rule

Every technical claim anywhere in this project — in a man page, in a note left
by a previous engineer, in a source comment, in this file — must be **true of
this machine, verified by running it**. A joke that names a command the OS does
not have teaches the player to distrust everything else, and the trust is the
product.

The view is never the source of truth. The ship, its systems, its power and its
damage live in the model; the 3D is a view of them. Every quality gain in this
project has come from driving it blind over a socket, and anything that cannot
be driven that way rots.

## Where it came from

This began as a break-fix game — one broken machine per ticket, diagnosed over
a service processor — became an IT-infrastructure game in an office building,
then a space station, and by August 2026 had a genuine packet-level network
simulation in it: frames on a wire, MAC learning, ARP, VLANs, TCP, DHCP leases.

All of that worked, and it was the wrong game. The tell, from the last blind
playthrough: two switches, one addressed router leg, and seventeen desks that
cabled up perfectly and never got an address. Correct networking. Terrible
game.

> "We've sunk way too much time into making this network accurate game when
> really we care about connections now."

So a link is `connected: true/false` now, and the depth moved to power,
redundancy and placement — which is what an engineer actually worries about,
and what survives when the packets go.

See `docs/design.md`, which is the one live design document.
