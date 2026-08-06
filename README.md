# NOMINAL

You are the IT department of a growing building. Order the gear, carry it in,
run the cable, and configure the operating system on the other end of it.

The operating system is real. Not a prop, not a scripted console: an RV64IM CPU
written from scratch, real compiled binaries, real syscalls, a dynamic linker
that resolves real library versions, a package database on disk, an init system,
services with dependencies, Plan 9 namespaces, and a boot chain that fails at
the stage where something is actually wrong. When you type `netstat` on a server
you just cabled, a program runs on an emulated processor and reads state the
kernel really keeps. A switch, a router and the ISP's handoff are appliances
with a management line and no shell, exactly as they are in a rack, so their
port counters are read with `show <box>` from the tower instead.

That is the whole idea. Everything else follows from it.

## The loop

1. **Money comes from the floors.** Residents and offices pay for service. More
   floors, more tenants, more demand.
2. **Demand outgrows the infrastructure.** A floor of accountants wants a file
   server. A trading office wants latency. Somebody wants their own subnet.
3. **You buy hardware.** It arrives, and it arrives *somewhere* — goods in on
   the ground floor, not your inventory. You carry it to where it needs to go,
   one box at a time because both hands are on it, and the walk is metres of
   real building. Put it down and that is where it lives: every metre of
   copper afterwards is measured from there, and a box with a cable in it
   will not be picked up again until you unplug it.
4. **You make it physical.** Rack it, power it, run copper to the switch. You
   have a spool: you plug one end in, walk to the other end, and the metres come
   off the drum and out of the budget as you go. Walking distance and cable
   distance are different numbers -- you take the corridor and the stairs, the
   copper goes up the riser -- so running one cable from the MDF to the floor
   three comms cupboard is 91 m of your legs and 42 m of cat5e.
5. **You make it work.** The box boots the same OS everything else runs. Give it
   an address, a route, a resolver, a firewall rule, a service. Get it wrong and
   it fails the way a real machine fails, and says so.
6. **Then a day passes.** Tenants move in on their day, their people do a
   day's work over what you built — real DNS, real TCP, real files across
   real copper — and the rent for the work that finished arrives that
   evening. Four fifths of a tenancy's people getting their work done is a
   day they pay for. Three days without it is a complaint, and complaints from
   a third of your tenancies -- never fewer than three -- end the run, so the
   building gets more slack as you let it rather than less. `service` prints
   the number you are counting against.
7. **Then it breaks.** Not because a designer hid a fault — because of something
   you did three floors ago and have forgotten.

Step 7 is the game. Step 5 is why it is interesting.

## Where the difficulty comes from

Nowhere. There is no difficulty constant, and nothing keeps a load number
beside the network: a frame that goes missing goes missing in the stack, on
a port, for a reason the port counters print in words. `load` names the
busiest eight ports in the tower and `show <box>` prints the reason beside
the count -- and on a box with an operating system in it, `netstat -P` on
its own console reads the same counter off the same kernel. Copper takes time to
clock bits onto, a port that is behind holds frames back, and a port that is
further behind than its buffer will hold drops them.

`./build/bf --loadcheck` plays the same tower two ways and prints where each
one falls over. Both are *played*, not assembled: the same `Session` a person
gets over a socket, with kit bought to goods in, carried up the stairs and
switched on — which boots a real operating system whose httpd answers because
netd read the address off its own disk. It grows the building a tenancy at a
time, and a floor of this building holds two or three tenancies, so the table
counts both.

Built the way somebody builds it who has never had to unbuild one — one flat
subnet, cheap copper, a switch per floor with a second daisy-chained off it
when the floor fills up, and one file server in the basement holding
everybody's files — it is comfortable on its first floor, visibly working
hard by the third, and has fallen over well before the fifth. Built with a
vlan per floor and a server in each floor's own cupboard holding that floor's
files, it carries all nine tenancies it has been grown to — five floors and a
hundred and seventy-six desks. The difference is not a number anybody tuned.
It is where the frames go: in the first tower the busiest thing in the
building is the one gigabit port on the one server everybody's files are
behind, and `load` names it.

Note what is NOT doing the work there. Fibre up the risers is not: a floor
switch's access port clocks a gigabit whatever you land on it, so a 42 m
riser costs 450 in fibre and 99 in cat5e and both of them carry a gigabit,
and the binding port in a planned tower is the floor server's own gigabit card
rather than the riser. Fibre is bought here for reach and for ten gigabit:
copper of any grade stops carrying anything at all past 100 m, and cat6
negotiates down from ten gigabit to one past 55 m, while fibre runs 2 km.
Buying it for riser bandwidth nobody is using is one of the ways this game
will take your money and tell you it has.

## Why a real OS matters

Games in this space simulate the terminal: a command returns a canned string
about the device you clicked. That is fine, and it caps the depth at whatever
the designer wrote down.

Here the terminal is the machine, and it follows that:

- **Diagnosis is detection, not lookup.** The evidence is real state, the tools
  read it honestly, and nothing knows which fault is "active" — there is no such
  variable anywhere in the code.
- **Faults compose.** Two things wrong interact the way two real things wrong
  interact, with nobody writing that interaction down.
- **Non-local faults are free.** `pkg verify` on the machine in front of you
  comes back clean when the cause is on another box. No oracle can name it,
  because there is no oracle.
- **Nobody can bluff it, including us.** Every claim the game makes about itself
  is checked by a gate that runs the command.

## Running it

    make bf                 the simulator and its harness
    make gdext              the Godot extension: the same machine, in the game
    make test-cpu           the emulator against qemu, and Linux against Windows
    make test-break         the full gate set

    ./build/bf --desk       play a shift at a terminal, no GUI anywhere near it
    ./build/bf --serve N    the same game over a socket, for playtesting.
                            `tower` stands you up out of the chair and into
                            the building: walk it, buy the kit, run the
                            copper, and get a shell on the box you cabled
    ./build/bf --towersh S  that half alone, one line at a time, over a pipe
    ./build/bf --loadcheck  the loop: a day, the rent, the load, and the
                            calibration — where a naive tower falls over and
                            how much further a planned one carries
    ./build/bf --sitecheck  the rules of the building, and that a person with
                            no eyes can play them over a pipe -- including that
                            every command a help text names really exists
    ./build/bf --netcheck   the stack: frames, vlans, arp, routing, tcp, dns
    ./build/bf --health     every pristine machine boots with every service up
    ./build/bf --solve 60   every generated fault is findable and repairable
    ./build/bf --askcheck   the person on the phone never says anything untrue

    ./Godot_v4.7.1-stable_linux.x86_64 --path game

## The rule

Every technical claim anywhere in this project — in a man page, in a note left
by a previous administrator, on a page of the in-game internet, in a source
comment — must be **true of this machine, verified by running it**. A joke that
names a command the OS does not have teaches the player to distrust everything
else, and the trust is the product.

## Where it came from

This began as a break-fix game: one broken machine per ticket, diagnosed over a
service processor. That game works, and its guts are the foundation here — the
OS, the package system, the fault generator, the desktop, the boot chain, and a
gate set that will not let any of them lie.

What it could not do was stay interesting. A fault a designer hides in one file
on one machine has a floor of one move, because the tools can name the file.
Building the network yourself is what gives a fault a history, and a history is
what makes it worth diagnosing.

What it still could not do, until D25, was come back for you. A blind
playtester of the tower put it exactly: *"They felt like MY decisions; they
did not yet feel like decisions that would come back for me."* Nothing came
back because nothing advanced. Now a day passes, and the network is what has
to carry it.

See `docs/` for the decision records, including the ones that turned out to be
wrong and say so.
