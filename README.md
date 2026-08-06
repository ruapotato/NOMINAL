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

1. **Money comes from the floors, and not all of it is the same money.** Four
   trades take leases, and each one asks the network for a different thing and
   pays a different rate for the same square metres:

   | trade | what it wants | rent |
   |---|---|---|
   | office | throughput at nine, and patient | 100% |
   | voice | no loss, no jitter. Not bandwidth | 170% |
   | web host | uptime, and reachable INWARDS | 240% |
   | studio | sustained UPLOAD, all of it | 300% |

   These are not labels on one behaviour. A call is 172 bytes every 20 ms — a
   fiftieth of one office desk — so buying bandwidth cannot help a call centre
   and a queue somebody else fills will ruin it. A web host's traffic arrives
   from the ISP handoff *inwards*, loading the one direction nothing else
   loads, and its lease is uptime: a day its site is down hands rent back. A
   studio pushes sustained upload with a deadline, and still pulls its media
   off a file server, so it is the one trade that wants a local server and a
   big circuit at once. `demand` says which is coming, what it will want and
   what it pays, before you sign.
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
   Or you pay for a **permanent jack** instead, priced on those same tray
   metres: a socket on that room's wall, with the run behind it punched down
   onto one port at the far end for good. It costs more than pulling the run
   once, and it is not there today -- somebody has to come and pull it, which
   is a day plus a day for every forty metres -- and after that every box
   that ever stands in that room plugs into it with a lead. The lead comes
   out when the box goes and the copper stays. Cheap and immediate against
   dearer and permanent, and neither is the right answer twice: a room that
   only ever holds one box was money burnt, and a riser you run off the spool
   three times you have paid for three times.
5. **You make it work.** The box boots the same OS everything else runs. Give it
   an address, a route, a resolver, a firewall rule, a service. Get it wrong and
   it fails the way a real machine fails, and says so.
6. **Then a day passes.** Tenants move in on their day, their people do a
   day's work over what you built — real DNS, real TCP, real files and real
   voice across real copper — and the rent for the work that finished arrives
   that evening. Four fifths of a tenancy's people getting their work done is a
   day they pay for. Three days without it is a complaint, and complaints from
   a third of your tenancies -- never fewer than three -- end the run, so the
   building gets more slack as you let it rather than less. `service` prints
   the number you are counting against.
7. **Then it breaks.** Not because a designer hid a fault — because of something
   you did three floors ago and have forgotten.
8. **And somebody is sitting at every desk.** They are in the rooms their
   tenancy leases, one per desk the tenancy asked for, and the room shows how
   their week is going — hands up when nothing has an address, heads down when
   the tenancy is striking. You can walk over and `sit` at one of their
   machines, because their complaint is a fact about *that* machine rather
   than a number in a report:

       desk:t3d0# ping 198.51.100.1     3 sent, 3 received, 0% loss
       desk:t3d0# voice
         dir  calls    sent arrived   lost   late concealed
         in       1     320     300     20      0        20  6.2%
       verdict: unusable -- 20 packets thrown away on uplink port 0,
       whose egress buffer is full.

   That machine is healthy. The audio died three hops away, on a port on
   another floor, and it was the *inbound* half — so a landlord who bought a
   fatter uplink for the outbound side would have bought the wrong thing.

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
    ./build/bf --mancheck   every command example in every manual, package
                            README and page of the in-game wiki, RUN on a
                            booted machine -- and every command they name
                            proved to exist on it

    ./Godot_v4.7.1-stable_linux.x86_64 --path game -- --seed=S

The window takes the same seed the harness does, so `--seed=7008` and
`--towersh 7008` are the same tower and you can look at the one you played.
It listens on 127.0.0.1:7373 and takes the same verbs a pipe does: the 3D is a
view of the session, never a second copy of it.

Verbs worth knowing before you start, because they are the ones that turn
guessing into deciding:

    demand                  who is coming, when, what trade, and what they pay
    quote <a> <b>           what a cable run would cost BEFORE it is paid for:
                            the tray metres, every grade priced off the spool
                            and as a jack, and what each would come up at over
                            that distance
    service                 every tenancy in its own units -- transfers for an
                            office, calls for a call centre, visitors for a
                            web host
    load                    the busiest ports, and which is dropping
    sit <desk>              their machine, their problem, their tools
    events                  what the world did to your kit overnight

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
