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
