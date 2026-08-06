# D25. A day passes, and the network is what has to carry it

## What the playtest said

The first blind playtest of the tower measured fifteen decisions an hour --
fifteen times the break-fix game -- and then said the thing that mattered:
*"They felt like MY decisions; they did not yet feel like decisions that
would come back for me."*

Nothing came back because nothing advanced. Cable refunded in full, floors
opened free from anywhere, no day ever ended, and no traffic ever crossed
anything anybody built. Every limit in `site.h` was real and none of them
were ever reached, because a network with nothing on it never runs out of
anything.

## The decision: a clock, and people on the other end of it

A day passes. Tenancies whose day has come move in and their desks arrive --
one real card per drop they asked for, in the room they rent, plugged into
nothing until the player runs the copper. The desks ask for addresses the
way a computer asks. Then the busy period runs: four seconds of wire time in
which every desk in the building fetches a page and opens a file at the same
moment, through the real stack, over whatever wires the player put between
them. What finishes is what the tenant pays for; four fifths of a tenancy's
people getting their work done is a day they pay for and three days without
it is a complaint; three complaints ends the run.

**Four seconds and not a day.** A network is sized for its peak and fails at
its peak. Simulating the other 86,396 seconds would be the same arithmetic
with more zeroes in it.

## The rule that shaped everything else

David's constraint was explicit: *"Do not let the load model become a second
source of truth beside the netstack: if a frame is dropped, it must be
dropped BY the stack, on a port, for a reason `netstat -P` will show."*

So there is no load model. `core/siteday.c` opens sockets and reads bytes;
everything else is `core/netstack.c`. What made that possible was the one
thing L1 had never done:

**Copper takes time.** `port_tx` now computes the exact serialisation of a
frame -- bits over megabits is microseconds -- and keeps a `busy_us` per
port. A frame offered to a port that is still clocking the last one waits
behind it, and that wait is added to when the frame lands, so it is latency
a player measures with `ping`. A port whose backlog is already deeper than
its 48 KB egress buffer tail-drops, on the port, into the counter
`net_dump_ports` has always printed -- with the reason beside it in words.

Congestion is now the same frames, later or never. Everything a player can
see of it -- rising round trips, `get` that times out, TCP retransmitting,
drops on a port counter -- is that one fact arriving through the layers that
really carry it. The old netcheck assertion that a broadcast storm is
"visible as dropped frames" now reads those drops off the trunk ports, which
is where a real switch shows them and where an oversubscribed uplink shows
them too, because they are the same fault.

## The calibration, measured

`--loadcheck` plays the same building two ways from the same seed, with the
same tenants asking for the same work, and prints the floor each one falls
over on. "Falls over" is the same rule the game uses to decide whether a
tenant pays: fewer than four fifths of the building's people finished.

    NAIVE -- one flat 10.0.0.0/16, a switch per floor off one core switch,
    cheap copper, and no server anywhere, so every file anybody opens comes
    down the landlord's circuit:

      floors  desks   work done   busiest port   util   frames lost
           1     20    40/40 100%   uplink:0      16%           513
           2     38    75/76  98%   uplink:0      32%           718
           3     56   100/112 89%   uplink:0      44%          1669
           4     76   125/152 82%   uplink:0      55%          3164
           5     96   143/186 76%   uplink:0      61%          5263
           9    176   188/321 58%   uplink:0      71%         16268

    PLANNED -- a vlan per floor terminated on a subinterface of the router
    down one trunk, fibre to each floor, and a server in each tenancy's own
    room holding their files and doing their DHCP:

      floors  desks   work done   busiest port   util   frames lost
           1     20    40/40 100%   srv1:0         6%           142
           3     56   112/112 100%  uplink:0       9%           891
           5     96   190/192  98%  uplink:0      17%          2182
           9    176   318/348  91%  uplink:0      33%          7424

Visibly working hard at three floors, fallen over at five, and the planned
build carries every floor it was grown to. The gate asserts that shape and
fails if the naive build survives to nine, because a difficulty curve that
is not there is worse than one that is wrong.

**Nothing in that table is a difficulty constant.** The two builds differ
only in the topology a player would have typed. What decides the curve is
where the frames are addressed: a file on a desk on floor four is either
fetched from a server on floor four -- in which case it never leaves the
switch they share -- or from the internet, in which case it crosses the
riser, the core, the router and a circuit somebody bought.

## Three things the loop found that had been quietly capping the game

None of these were visible until frames had to arrive in quantity.

**A router's ARP cache held sixty-four neighbours.** A tower stalled at
about seventy desks: the router evicted an entry per new conversation and
re-ARPed for a neighbour it had known a millisecond ago. It looked exactly
like congestion and was not. Real gear holds thousands.

**A busy period that ended badly poisoned the next one.** Three hundred
half-finished transfers left three hundred sockets held, and the next
morning nothing could open a connection at all -- which looks like a network
that has died and is only a leak. `net_tcp_reap` is the stack giving up on
connections nobody is using, which is what a stack does.

**A machine whose httpd was running answered nothing.** `netsite_apply`
opened a bare listening socket for the service, so a server the player
powered on accepted connections and said nothing -- and held port 80, so the
site's own `httpd <box>` could not bind beside it. Found by playing it: five
floors of desks fetching files from servers that were configured, cabled,
addressed, pinged, and silent.

## What this costs

- **The world is bigger.** 18 MB of preallocated network, printed by
  `--sitecheck` from `sizeof` rather than from a sentence somebody typed
  once. Every desk a tenancy moves in is a real card in a real broadcast
  domain; three hundred and fifty drops is what the generator really makes.
- **The frame queue had to stop being a linear scan.** Sweeping the queue
  end to end per delivery is fine for a ping and quadratic for the half
  million frames a floor makes in its busy period. Delay is bounded and
  small, so "which millisecond" is a ring of buckets.
- **An ARP cache had to stop being 97 KB.** It held a whole frame per entry,
  which is right -- it is why the first packet to a new neighbour is not
  lost -- but sixty-four of them on each of four hundred hosts is forty
  megabytes of world spent on packets that are almost never held. Held
  frames are a pool now.
- **The receive window is the ceiling everything else lives under.** A
  connection cannot beat a window per round trip, so 12 KB over a six
  millisecond round trip is about sixteen megabits, and that is what one
  desk pulling one file gets on a healthy LAN in this world. Every other
  number in the loop was chosen underneath it, and it is documented where it
  is defined rather than discovered by whoever next wonders why a transfer
  will not go faster.

## The two numbers that were chosen rather than derived

Honesty requires naming them. `SITE_DESK_FILE_KB` and `SITE_DESK_WEB_KB` --
what one person at one desk pulls in the busy period -- are a judgement, and
they are the only judgement in the loop. They are set to a defensible
busy-hour figure for one office worker and to nothing else. Everything
downstream of them, including the entire difficulty curve, is arithmetic
the netstack does.

## Addendum, 2026-08-05: two of the three costs were not there

This record opens by listing what was wrong before D25 -- *"Cable refunded in
full, floors opened free from anywhere, no day ever ended"* -- in a way that
reads as three things fixed. A blind playtester of the build spent ninety
minutes in it and found that only one and a half of them were.

**The day ended.** That half was real, and the loop works.

**Opening a floor was still free**, and still typeable from anywhere, so the
correct play remained to open the whole tower in the first minute. It now
costs the landlord's fit-out, priced by the square metre of let space on the
floor, and somebody has to be standing on the floor to sign it off -- which
means the stairs, because the lift button for a floor nobody has opened is
not lit. The rate is a chosen number in the same sense as the two above, and
it says so where it is defined.

**Cable was in fact never refundable**, which this record's opening sentence
implies was a thing that got fixed and which the code shows was simply never
implemented: `site_uncable` gives the port back and no money. That is the
right behaviour and it is now asserted by a gate rather than believed.

**And the ISP was never billed at all.** `isp` printed "the circuit is 500
Mb, 1540 a month" and across forty-two days -- a month and a half -- not a
penny of it was taken; `spent` only ever equalled hardware plus cable. So the
one genuinely recurring decision in the game, how much circuit to buy against
how much traffic you keep off it by putting a server on the floor, cost the
same either way. The standing charge lands on the thirtieth day now, on the
same thirty-day month the rent is a thirtieth of.

The lesson worth keeping is not about any of the three. It is that this
record asserted them as done, and the assertions were not gated. Everything
above is now checked by `--sitecheck`, which plays the days and reads the
account, so the next version of this paragraph cannot be written from
memory.

## Addendum, 2026-08-05: the advice pointed at a tool you cannot point

David's constraint is quoted above as *"if a frame is dropped, it must be
dropped BY the stack, on a port, for a reason `netstat -P` will show."* The
constraint held. The advice built on top of it did not.

Every degraded day printed `something is dropping -- `netstat -P` it`, and
the same playtester's verdict was that this was *"the single most
trust-destroying thing in the build: the first thing the game asks you to do
is impossible."* They were right. `netstat` is a program on a machine with an
operating system in it. The ports that drop are on switches, routers and the
ISP handoff -- appliances with a management line and no shell, exactly as
they are in a rack -- and `netstat` is not a verb at the tower prompt or on a
management line either.

The counters were always reachable from the tower: `load` names the busiest
eight ports and `show <box>` prints the drops with the reason beside them in
words. The advice names those now, and `netstat -P` is claimed only where a
box has a shell to type it into -- which includes the README, whose opening
paragraph invited the reader to type netstat on a switch.

## Addendum, 2026-08-05: that table counts tenancies, not floors

A playtester read the calibration and then played the game, and the two did
not agree: the gate says a naive build does 89% of the work at three floors,
and their tower at three floors did 63%. They were right to report it and
right not to tune anything, and the answer is that the column was mislabelled
from the day it was written.

`--loadcheck` grows the building one TENANCY at a time -- twenty desks, one
row -- and called the row a floor. The building generator puts two and three
tenancies on a floor. So the gate's third row is fifty-six desks on three
floors only because the first three tenancies happened to land on different
ones; by the fifth row it is ninety-six desks on three floors, and by the
ninth it is a hundred and seventy-six on five. A player with three floors in
service is five or six tenancies in and reads a row that is not theirs.

Both numbers are counted and printed now, and the gate says in words that a
floor holds more than one tenancy. Nothing about the network was tuned,
because nothing about the network was wrong: at the desk count they really
had, the naive curve says about what they measured.

The rest of the gap was not the network either. `--loadcheck` builds its
towers straight onto the site: `site_power` on a Site does not boot an
operating system, so nothing in the calibration has ever had a kernel, an
`/etc/nftables.conf` or an nft(8) in it. Both faults that cost the same
playtester their re-architecture window -- a DHCP server swept away by a box
reading its own config file, and a `policy drop` that ate the DISCOVERs --
existed only once a machine was booted, and the gate could not see either.
That is a real blind spot and it is worth naming: the load gate measures a
network, and the game runs operating systems on it.

## Addendum, 2026-08-05: the demand was capped by the client, not the wire

A playtester reached day sixty with a hundred desks across five tenancies and
reported that no capacity decision ever bit: a tenancy served 36-40/40 every
day for the twelve days its own file server was switched OFF, the busiest
port in the tower at a hundred desks was the ISP handoff at eighteen per
cent, and *"cable grade, circuit size and switch count beyond port exhaustion"*
never mattered. The gate above said otherwise. The gate was right about the
build it measured and the build it measured was not the game.

**Nothing about the two chosen numbers was wrong.** The fault was one line
further out. A desk did its web fetch, and only when that finished did it
open its file: one socket at a time, and a socket in this world cannot beat
one receive window per round trip. Twelve kilobytes over the four
milliseconds a two-hop LAN costs is about twenty-four megabits, and a routed
path is half that. So one desk could offer at most about three megabytes
across the whole busy period -- measured, by asking it for four and watching
every desk in the tower fail identically at exactly 50% whatever the topology
-- and a hundred and seventy-six desks together could not fill much more than
one gigabit link. Every riser in the tower is a gigabit link.

That is the whole answer to all four of the playtester's observations at
once. Cable grade could not matter because the cheapest drum in the catalogue
is already a gigabit. Circuit size could not matter because file traffic was
the only traffic with any weight in it and a server anywhere took all of it
off the circuit. And the tenancy served with its server off was being served,
honestly, by a different server two floors away across the trunk and the
router -- `file_server_for` falls back to any server in the building, which
is what a real client would be pointed at -- and that path had so much
headroom that the fallback cost nothing measurable.

The fix is not a bigger number. A person's machine pulls its page and its
file **at the same time**, because that is what a machine does, and this file
was modelling it as though it did not. Two transfers per desk, started
together, and the ceiling doubles, the file server is exercised on the day it
matters -- previously a desk whose page did not arrive never opened its file,
so the traffic to the thing the architecture is about vanished exactly when
the network was in trouble -- and the busiest port in a flat tower stops
being the landlord's circuit and becomes the one gigabit port on the one
server everybody's files are behind.

The socket pool had to grow with it: two connections per desk with an end at
each side is fourteen hundred sockets for the three hundred and fifty drops
the generator really makes, and at eight hundred the pool would have run out
before the network did. A connection that cannot be opened because the world
is out of sockets is a bottleneck nobody built and nobody can see -- the same
mistake as the sixty-four entry ARP cache. It costs memory and the world
prints what it costs.

**And the calibration is played now, not built.** Both towers in the table
are typed into a `Session` -- the struct `--serve` hands a player -- with the
words a player types: kit bought to goods in, carried up the stairs, put
down, cabled off a drum, switched on. Switching a server on installs and
boots a real machine, and its httpd answers because netd read
`/etc/net/interfaces` off its own disk. The naive build is what somebody
really builds, too: not "no server anywhere", which nobody plays, but one
server in the basement next to the core switch, because that is where the
rack is and it is one cable.
