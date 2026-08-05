# NOMINAL

You are the IT department of a growing building. Order the gear, carry it in,
run the cable, and configure the operating system on the other end of it.

The operating system is real. Not a prop, not a scripted console: an RV64IM CPU
written from scratch, real compiled binaries, real syscalls, a dynamic linker
that resolves real library versions, a package database on disk, an init system,
services with dependencies, Plan 9 namespaces, and a boot chain that fails at
the stage where something is actually wrong. When you type `netstat` on a switch
you just cabled, a program runs on an emulated processor and reads state the
kernel really keeps.

That is the whole idea. Everything else follows from it.

## The loop

1. **Money comes from the floors.** Residents and offices pay for service. More
   floors, more tenants, more demand.
2. **Demand outgrows the infrastructure.** A floor of accountants wants a file
   server. A trading office wants latency. Somebody wants their own subnet.
3. **You buy hardware.** It arrives, and it arrives *somewhere* — goods in, not
   your inventory. You carry it to where it needs to go.
4. **You make it physical.** Rack it, power it, run copper to the switch. You
   have a spool: cable costs by the metre and you choose the route. A permanent
   jack is tidier and costs by distance to install.
5. **You make it work.** The box boots the same OS everything else runs. Give it
   an address, a route, a resolver, a firewall rule, a service. Get it wrong and
   it fails the way a real machine fails, and says so.
6. **Then it breaks.** Not because a designer hid a fault — because of something
   you did three floors ago and have forgotten.

Step 6 is the game. Step 5 is why it is interesting.

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
    ./build/bf --serve N    the same game over a socket, for playtesting
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

See `docs/` for the decision records, including the ones that turned out to be
wrong and say so.
