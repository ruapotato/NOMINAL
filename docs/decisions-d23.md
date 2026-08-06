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
pay for a permanent jack priced by distance (BUILT IN D29 -- see the note at
the end of this record), and configure the same OS on the
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


## Addendum, 2026-08-05: the jack does not exist

This record and the README both described a permanent jack, priced by distance to
install, as the tidy counterpart to the spool. The first blind playtester of the
tower went looking for it: *"What a 'permanent jack' is. The README and D23 both
sell it as the counterpart to the spool. It is not in `help` and no verb I tried
creates one."*

They were right. The README has been corrected to describe the spool, which is
real, and this note exists so the idea is not quietly lost. It is still the right
mechanic and worth building, because it is a genuine trade-off with no correct
answer: a run off the spool is cheap and immediate and leaves copper lying where
you walked it, and a jack costs more, takes a trade, and is there for good. Cheap
now against tidy later is exactly the kind of decision that comes back for you six
floors up, which is what this whole pivot was for.

Recording it as unbuilt rather than deleting it, because the reasoning survives the
absence of the feature -- and because a design document that describes things the
machine does not do is the same failure as a man page that does.

## Addendum, 2026-08-06: it exists

`jack`, `patch` and `jacks` are verbs in the tower and in `help`, and
`--sitecheck` fails without them. The note above stands as the reasoning; what
follows is what was actually built, because the reasoning did not decide the
hard part.

**A jack that is only a dearer cable would have been worth nothing.** The
day-34 playtest is the reason: *"Cable is a bill I paid with a rule, not a bill
I sweated. I made the riser decision on floor 1 and then repeated it on floors
2 and 3 without thinking."* A second price for one outcome is another rule to
apply once. So the jack had to differ in KIND, and three levers were on the
table -- days, re-charging on every move, and belonging to the room.

**The lever chosen is that a jack belongs to the ROOM, not to the box**, with
the days as the price of that permanence rather than as a separate mechanic.
`site_jack()` puts a socket on a room's wall and holds a panel port at the far
end for good; `site_patch()` connects whatever is standing in that room for the
price of a lead; `site_uncable()` on that lead leaves the jack in the wall.
That is the one thing a player can buy in this game that survives the box
being carried out. And the trade takes `1 + metres/40` days -- a 42 m riser is
three -- on the same clock a tenancy's three days of fit-out and three strikes
are counted on, so a floor that needs a switch this afternoon cannot be jacked
and has to be spooled.

Both directions of wrong are reachable and neither is a difficulty number:

- the jack is always dearer than the same run off the spool (the spool price
  of the identical `bld_cable_all()` metres, plus a flat fit-out), so a room
  that only ever holds one box is money the player will not get back --
  `site_uncable` has refunded nothing since it was written and this refunds
  nothing either;
- the spool is charged again every time the box moves, and a floor that fills
  up or gets rebuilt pays the whole riser again for the second switch;
- and the panel port at the far end stops being a port. `site_free_port`
  steps over it, so `serve` will not spend it on a desk -- which is a cost
  that only lands the day a core switch runs out of holes.

The break-even on the gate seed's 35 m riser: 95 to spool it once, 185 to jack
it, 12 a lead after that. One connection and the spool won by 102. Three and
the jack won by 64. `jack` prints both numbers at the moment the money leaves,
which is where D27 put the negotiated port speed for the same reason.

**Why the other two levers were not taken as the primary.** Re-charging on
every move is already true of the spool and needed no new verb -- it is the
consequence of the room lever, not an alternative to it. Days alone would have
made the jack a cable with a delay, which is a tax on the impatient rather than
a decision: with nothing else different, waiting is free once the player learns
to order early, and the mechanic evaporates. The room lever is the one that
cannot be learned away, because it turns on something the player cannot know
when they buy it -- whether that room will hold a second box.

**What was NOT done.** `serve` does not run desks off jacks: a jack per desk
is twenty faceplates and twenty panel ports, and the verb belongs to
`core/siteday.c`, which was out of scope. There is no way to take a jack out
again, deliberately -- permanence is what it is for -- and therefore no way to
recover a panel port spent on one. And no scenario in `--loadcheck` uses a
jack, so nothing yet measures a sixty-day tower built the tidy way against one
built off the drum; that is the measurement to widen next, and it is a
playthrough rather than a gate.
