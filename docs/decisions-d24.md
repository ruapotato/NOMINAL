# D24. A router's ports are real cards, and a computer has a power button

## What the playtest said

The first blind playtest of the tower could not get the building online at
all, and said the failure was structural rather than skill: *"Routers cannot
route, so the network cannot grow past one flat segment."* Their closing line
named the priority: *"Fix the router first -- it's the one that makes a
competent player think they're the problem."*

## Why a router could not route

Two separate faults, and either one alone would have been enough.

**There was no verb that gave a box a second address.** `subif` was documented
as adding a tagged subinterface and instead reconfigured the parent, because
its interface index and its nic both went through `atoi` -- so `subif edge wan
eth0 100 198.51.100.2/30`, which reads perfectly, meant interface 0 on card 0,
and replaced the WAN address with the LAN one. Seventeen tenancies in the gate
seed want a segment of their own and none of them could have one.

**Three of a router's four sockets had nothing behind them.** A host was
created with one interface wired to port 0 and three ports wired to nothing. A
frame arriving on port 1 landed on a port that reported `up`, counted a
receive, and then found no interface that claimed it: dropped, and not in
`drop`, because the counter that would have shown it belonged to an interface
that did not exist. Moving the cable to port 0 fixed it instantly.

## The decision: both, and the sockets first

The brief allowed either fix. We did both, in this order, because they are not
alternatives:

- **Every hole in the back of the box is a real card.** `net_add_host_nics`
  takes the number of sockets the catalogue sells and creates one interface
  per socket. This is what a router IS, and it makes the port count in the
  catalogue, in the site, in the netstack and in `show` one number rather than
  four. A server sold with two sockets used to be given four in the network
  world, so `netstat` inside a machine and `show` outside it disagreed about
  the same box.
- **A subinterface stacks on top of a card and never replaces it.**
  `net_if_subif(node, nic, vlan)` finds or creates the interface for that pair
  and returns its index; the player never types an interface index, because
  the index was the argument they got wrong. Subinterfaces are numbered above
  the sockets and print as `eth0.30`, so a card and a tag on a card are
  visibly different things.

`addr <box>:<nic>` is how the second socket gets an address, spelled the way
it is cabled. With sockets that carry traffic there is no longer such a thing
as a port that cannot, so the fallback the brief asked for -- refuse the cable
rather than eat the frames -- has nothing left to refuse.

## And the deeper one: the OS and the network were two machines

You could buy a pc, cable it, address it and ping it while it had never been
powered on, because the address went onto its network node the moment the
player typed it. Plugging in a serial lead then booted it for the first time
and it became LESS reachable, because its own firewall finally started. The
playtester: *"The 'real OS' and the 'real network' are two different machines
that only get married when you plug the serial lead in."*

So a pc and a server arrive switched off. Powering one on installs it, boots
it, and calls `netsite_apply`, which is the machine applying its own
configuration from its own disk -- and applies nothing when netd is not
running, so a box whose boot failed is on no network at all. Powering it off
takes the addresses, the routes, the ARP cache, the filter and the sockets
with it: they were in its memory, not on the box. A serial lead reads a
console; it does not press a button.

Switches and routers have no power button, and say so. They are appliances
with no operating system modelled in them, and inventing one would be a rule
with a made-up number in it. The join is `site_kind_has_os`.

## What this costs

`site_addr` on a box that is not running is refused rather than staged, so a
script has to power a machine on before configuring it -- one extra line, and
it is the line a person performs. Every gate that builds a tower now says
`power <box> on`, which is the honest shape of the job.

## The measurement

`--netcheck` 106 to 113, `--sitecheck` 90 to 113, and the new checks are the
playtest's own sentences: a frame arriving on any socket reaches the stack, a
box with an address on two sockets routes between them, a subinterface is an
extra address rather than a replacement, a machine nobody switched on answers
nothing with a cable in it and a router beside it, and a cable run that could
not finish leaves no end in a socket for the next line to eat.
