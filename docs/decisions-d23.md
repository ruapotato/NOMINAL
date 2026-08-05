# D23. The building is the fault generator

## What changed

NOMINAL was a break-fix game: one broken machine per ticket, reached over a
service processor, diagnosed with real tools. That game works. Four blind
playtests all said the diagnosis is genuine detection rather than guessing, and
none of them got stuck unfairly.

It could not stay interesting, and the measurement says why. A playtester scored
fourteen tickets by **decisions** -- points where what you learned changed what
you did next -- and got: one 0-decision, five 1-decision, five 2-decision, two
3-decision, one 5-decision. Five of thirteen single-fault tickets were strictly
one move. Their words on the boredom: *"tolerable in small numbers, corrosive in
clusters"*, and they named the exact moment -- recognising ticket 7 as ticket 4
with a different version number, and typing the whole repair as one line without
reading any output.

The diagnosis of the diagnosis: **the shallow tickets were all in one layer.**
Four of the five were bootloader or init config, and that layer cannot be more
than one move as long as the loader prints the exact path it could not open and
`pkg verify` prints the exact package. Both of those are honest, and neither is
going away.

So the depth cannot come from more faults. Sixty-two faults that all resolve to
"find the broken file, restore the broken file" is one puzzle with sixty-two
skins.

## The decision

The game becomes an IT-infrastructure game set in a growing building, in the
lineage of Tower Networking Inc. -- with the difference that their command line
is a prop and ours is an emulated operating system. You order hardware, take
delivery, carry it where it goes, run cable from a spool priced by the metre or
pay for a permanent jack priced by distance, and configure the same OS on the
other end of every cable. Floors fill with tenants, tenants pay, demand outgrows
the infrastructure, and you build more.

And the networking is REAL. David: *"I want real networking too, not as close as
possible."* Frames on a wire, MAC learning, VLANs, ARP, routing tables, ICMP
errors, TCP, DHCP leases, DNS. Not a reachability model.

## Why this fixes the depth problem rather than papering over it

**The player becomes the cause.** In break-fix a designer hides a fault and you
hunt it; the floor is one move because the tools can name the file. When you
built the network, a fault is a consequence of something you did three floors ago
and have forgotten -- non-local in time and space by construction, with a history
you can reconstruct because you lived it.

**Real networking makes faults emergent instead of authored.** A wrong subnet
mask works on-net and fails off-net because that is arithmetic, not a rule
anybody wrote. Duplicate IP, wrong gateway, VLAN mismatch, a cable in the wrong
port, a switch loop without spanning tree: nobody has to write any of those, and
none can be solved by a lookup because `pkg verify` on the machine in front of
you comes back clean.

**The oracle problem dissolves.** Thirty-seven decoys exist to stop `pkg verify`
being a one-button answer. Across a wire it stops being an oracle on its own,
because the tools are local and the cause is not.

## What is NOT being thrown away

David, explicitly: *"I want to keep our debug tools too in the OS."*

Everything built for break-fix stays and becomes the maintenance half of the
loop. The difference is where the faults come from: **the world supplies the
cause.**

- a blackout -> an unclean shutdown -> filesystem corruption -> `fsck`, then
  `pkg verify`, then `pkg diff` to tell damage from somebody's edit
- a cheap power supply under load -> a machine that boots intermittently
- a disk nobody replaced -> the truncated file the boot log names
- a cable run past interference -> errors that only appear under traffic

The sixty-two fault types, the rescue medium, the service processor, `pkg
verify`/`diff`/`reinstall` with its `.pkgsave`, the boot chain that fails at the
stage where something is actually wrong, the deterministic customer, the desktop
and its diagnostic apps -- all of it keeps working, and `--solve` keeps proving
every generated fault is findable and repairable with the tools that exist.

## What this costs, honestly

- **A 3D shell**, which is a real amount of work, and which makes blind agent
  playtesting much harder. Blind playtests have been the engine of every quality
  gain in this project. The mitigation is the rule that already governs the
  desktop: **the view is never the source of truth.** Ordering, carrying,
  cabling and configuring must all be drivable through a scriptable interface,
  with 3D being how a human does it. If it cannot be played over a socket, it
  cannot be tested, and it will rot.
- **Scale.** Measured: a machine costs 2.2 MB installed and 13.5 MB booted at
  1 MB of guest RAM per process. Roughly 300 booted or 1800 idle in 4 GB. The
  building describes space; which spaces hold equipment is a separate question,
  so a large tower does not mean a large number of live machines.
- **The break-fix ticket stops being the whole game** and becomes its last move.
  That is the point, but it means the ticket-shaped content we have is now a
  component rather than the product.

## The rule that does not change

Every technical claim in this project -- in a man page, in a note from a previous
administrator, on a page of the in-game internet, in a source comment -- must be
true of this machine, verified by running it. Real networking is that rule
applied to the network, which is exactly why the shortcut version was never going
to survive.
