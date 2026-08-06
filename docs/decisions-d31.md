# D31. You can walk round to their side of the problem

## What was asked for

The owner, this hour:

> "let's also add in the virtual people to actually be in their office at a
> computer desk similar to the server room, where if you felt like it you
> could go over to their desk and see what issues they're complaining about
> -- literally using their computer. Basically let's make the world feel
> alive."

## What was already true, and what was missing

A tenancy that moves in installs one `SDEV_DESK` per drop it asked for, in
its own room, named `t7d3` -- the fourth desk of tenancy seven. Those cards
are real: they broadcast a DHCP DISCOVER, they open TCP connections to
whatever server `file_server_for()` picks, and `core/siteday.c` counts
whether the transfers finished. Four fifths finishing is a day the tenancy
pays for; three days below that is a complaint.

So the game already knew, in detail, why a tenancy was unhappy. What it had
no way of doing was **showing the player the same thing from the unhappy
person's chair.** `service` printed `up 20 addr 0` and that was the whole of
the evidence. The one instrument in this project that has never lied -- a
real operating system with real tools on it -- was pointed at every box in
the building except the twenty that were actually complaining.

## The verb

    desks              every tenancy in: desks, what finished, strikes
    desks <tenant>     each desk, who sits at it, its room, its address
    sit <desk>         sit down at it. You must be in their office
    stand              get up

`sit` is refused unless you are standing in the room with the desk, exactly
as `plug`, `carry` and `power` are, and for the same reason: position is the
whole point of the building. `stand` takes you out of the chair; `unplug`
and `leave` are taken as well, because the game has already taught `unplug`
for "put the thing down and be a person in a room again" and a player who
types the word they know should not be typing it at a shell.

The prompt is `desk:t1d3#`. Not `root@t1d3#`, which is the spelling `plug`
uses on the player's own boxes, and not `ada@t1d3#` either: there is one
account on every machine in this game and `/bin/whoami` prints `root`, so a
prompt naming a user would be the game lying in the one place the player
cannot look away from. What the prompt has to carry is the thing that IS
different -- this box is not yours.

## What it costs, measured

A booted `Machine` is the expensive object in this program. Measured on this
build, sampling `VmHWM` of `./build/bf --towersh 7008` to process exit:

| run | peak RSS |
|---|---|
| a session that touches nothing | 75.1 MB |
| the same session, one of the player's own PCs powered and plugged | 93.3 MB |
| the same session, sitting at one tenant's desk | 93.4 MB |
| **sitting at twenty desks of a tenancy, one after another** | **93.6 MB** |

So a sat-at desk costs **18.3 MB of resident memory**, the same as any other
booted box, and **the cap is one**. Twenty sittings cost what one costs.

The cap is not a budget somebody chose. **A person has one backside and sits
in one chair**: `Session.seat` is a single device index, the `Machine` at
`mach[seat]` is installed when the chair comes out and freed when you stand
up, and every line typed while seated goes to that machine, so there is no
input that could ask for a second one. A full tower is 176 desks; had they
all been live that would have been 3.2 GB, against a world that is meant to
be 73 MB plus what is running. A session that never sits at a desk pays
nothing at all, because nothing is installed until somebody pulls a chair
out.

`--sitecheck` asserts all of this rather than trusting it: `sitting at every
desk of a tenancy in turn never holds more than one`.

## The three judgement calls, and what decided each

### Does the desk keep state between sittings, or boot fresh?

**Fresh, and the machine is destroyed when you stand up.** Both are
defensible and only one is cheap, but cheapness is not what decided it. The
memory answer and the ownership answer turn out to be the same decision: a
desk that kept its state would be 18 MB per desk you had ever visited, AND
it would be a machine the player had quietly acquired. Freeing it says the
true thing out loud -- *"nothing you leave on it stays"* -- and the sentence
is honest because the object is gone.

This has a consequence worth stating plainly: **you cannot repair a tenant's
desk.** That is correct here. Their machines are never damaged -- the
weather in `core/siteday.c` skips `SDEV_DESK` -- so a desk is never the
fault. It is an instrument for finding a fault that is always somewhere in
the network the player built. The desk is a window, not a workshop.

### Is a desk you are sat at still doing its day's work?

The question cannot arise, and that is the answer. Every line typed in the
seat goes to the machine, so `day` is not a word this line takes: you cannot
advance the clock from somebody else's chair. Sitting down is a pause with a
console in it, not a turn.

### What happens when you sit at a desk on a floor whose network you have broken?

This is the interesting case and it is the reason the feature exists. It
reads, on the gate seed, like this -- a tenancy of twenty with a switch, a
router, addresses from a real pool, and a complaint filed anyway:

    f1 office> sit t1d3
    Cai Nakamura pushes their chair back and lets you sit down at t1d3.
    [UP at target] -- link and address
    "we filed with the landlord. 0 of us got anything done yesterday, out of 80."

    desk:t1d3# ping 10.0.1.1
      seq=0 reply from 10.0.1.1 time=8 ms
    desk:t1d3# ping 198.51.100.1
      seq=0 destination net unreachable -- a router on the path has no route for it
    desk:t1d3# traceroute 198.51.100.1
    1 10.0.1.1
    2 10.0.1.1
    the trace stops there: 198.51.100.1 never answered.

Their gateway answers and nothing past it does, and the trace names the last
box that works. Nobody wrote that diagnosis: it is an ICMP net-unreachable
from a router that really has no default route, because the player never
cabled it to the handoff. `service` could only ever have said `0 of 80`.

The other common shape, a tenancy with copper in every desk and no pool on
the segment, reads off the same console:

    desk:t1d3# ip addr
    1: eth0: <UP,LOWER_UP> mtu 1500
        inet none -- this card has no address
    desk:t1d3# cat /etc/net/interfaces
    # nobody has ever configured this machine by hand. It asks.
    iface eth0
      address dhcp

It asked, and nothing answered. That is the whole diagnosis and the machine
is what says it.

## The hard part: waking a machine must not change the network

This is the decision the feature turns on, and getting it wrong would have
been much worse than not building the feature.

`core/netsite.c` applies a machine's disk to its node: it tears the node's
addresses, routes, resolver, ARP cache and pools down, reads
`/etc/net/interfaces` back, and puts what the file says on the wire. That is
right for the player's own boxes -- it is what makes a mains failure real --
and it is a loaded gun pointed at a tenant's desk. The obvious
implementation, writing `address dhcp` on a desk and letting `netd` really
ask, would mean that **sitting down at a striking tenancy's computer
re-runs their DHCP and re-reads their resolver**, and a player who made a
tenancy worse by looking at it would never look again.

So the desk's disk is written from **what its card already has on the wire**
-- the lease it is holding, the gateway and resolver that came with it --
rather than from a request to go and get one. `desk_disk()` is deliberately
not `sync_disk()`: that function writes the player's *decisions* onto a box
the player owns, and nobody decided anything about this one. Applying the
file is then a no-op for the network, which `--sitecheck` measures directly:
`standing up leaves the desk's address, gateway and resolver alone`.

The price is one small infidelity, and it is paid in a comment rather than
hidden: `/etc/net/interfaces` on an addressed desk reads as a static
address, so the file says which it is above the stanza --

    # the lease this machine is holding. It asked, something
    # answered, and this is what came back.

A desk with no address writes `address dhcp` and means it literally: `netd`
really asks, really gets nothing, and the file is the plainest possible
statement of the fault.

Two smaller consequences of the same rule:

- **The boot console is not printed.** `plug` prints it, because pressing a
  power button is a thing the player just did. Sitting in a chair is not, and
  a kernel banner would be the game claiming a reboot it does not mean. What
  is printed is the state -- `[UP at target] -- link and address` -- which is
  read off the machine and off the wire.
- **The shipped `/etc/resolv.conf` had to go.** The image ships
  `nameserver 10.0.2.3`, a host in the break-fix world that does not exist in
  the player's building, and reading that file is the first thing anybody
  does at a desk with no network. A desk with no lease gets
  `# a nameserver arrives with the lease. No lease has arrived.` instead,
  which is true.

## The people

Each desk has a person, drawn from the seed and the device index, so the same
tower always seats the same people and `desks 7` prints the same nameplate
twice running. It is not a technical claim and nothing hangs off it. It is
there because a floor of numbered cards is not a floor of people, and because
the 3D shell -- which a fourth agent is building against this -- needs
somewhere to read the nameplate from that is not its own imagination.

Two draws off one hash, given name and surname, because twenty desks drawn
from twenty-six given names put four people called Vik in one office, and a
room of four Viks is not a room of people either.

## Ownership, and what it means here

`carry` has refused a tenant's kit since it was written: *"their kit is
theirs; you are here for the wall, the cupboard and the copper."* Sitting at
somebody's computer to fix their problem is the job; walking off with it is
not, and the same rule had to hold in this new shape.

It holds in three ways, all of them enforced rather than asserted:

- `sit` refuses anything that is not an `SDEV_DESK` and points at `plug`,
  so the verb cannot become a second way onto your own kit;
- you must be standing in their office, which is a leased room you walked to;
- and the machine goes when the chair does, so there is nothing to take.

What is NOT claimed is that you are a limited user. You are root on that
machine, because there is one account on every machine in this game and
`whoami` says so. The seat's own `help` says it out loud: *"there is one
account on it and it is root -- so be careful: the only thing stopping you
is you."* A game that told the player they were unprivileged and then let
them `rm -rf` would have taught them to distrust the next thing it said.

## Driving it over a socket

Everything above is `session_line()`, so it is the same code path the 3D
shell, `--serve` and `--towersh` all use, and D23's rule holds: the model is
what says where a person sits and what is wrong with their machine.
`desks <tenant>` is the surface built for the view -- device name, person,
room number, address, and link/address state, one line each -- so the fourth
agent placing people and desks in 3D reads them out of the model rather than
inventing them.

## The gate

Twenty-six checks in `core/sessioncheck.c`, under `walking to a tenant's desk
and using their computer`. Built against a clean `git archive HEAD` checkout
with only this file copied in -- which is why the check asks the PROMPT where
you are rather than `ses.where == SES_SEAT`: an enum member that does not
exist yet is a build error, and a check that cannot be built against the old
tree cannot be shown to fail without the feature.

    clean HEAD + this gate file      367/390   (23 failing)
    with the feature                 390/390

The three failures outside the new section are the two directions of the help
gate -- the tower help must NAME `desks`, `sit` and `stand`, and each must
ANSWER where the help names it -- plus the verb list every 3D action has to
have a word for.

## What was NOT done

- **No desk ever needs repairing, so `sit` cannot repair one.** The weather
  skips desks and always has. A tenancy whose complaint was a broken machine
  rather than a broken network would be a genuinely new puzzle and it is not
  this one. The place it would go is `core/siteday.c`, which was out of scope
  this hour.
- **You cannot sit at your own PC or server.** `plug` gives a root shell on
  those and a second verb for the same thing would be two spellings of one
  action, which this file already treats as a bug.
- **The person is a name and a chair, not a schedule.** They do not walk to
  the kitchen, they are not absent on Tuesdays, and `sit` never fails because
  somebody is using the machine. Every one of those is a state the 3D shell
  would have to render and the socket would have to expose, and none of them
  changes a decision the player makes.
- **Nothing measures the feature in `--loadcheck`.** Sitting costs 18 MB for
  as long as somebody is sat down and the gate builds towers without ever
  sitting in one, so the calibration is untouched -- which is correct, and
  also means no long run has yet been played from the desks' side. That is a
  playthrough rather than a gate.
