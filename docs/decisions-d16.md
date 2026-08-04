# D16. The station, the economy, and the arc

The design, settled. Everything before this is either kept or deleted by it.

## The arc, which is the whole point

> **Start simple. The naive solution is fine at first. As you grow, real choke
> points show up: CPU overloaded, network clogged, power constrained, money
> constrained.**

No hand-authored difficulty curve. Difficulty is growth meeting the topology
you built when you were small, exactly as in Tower Networking Inc (D15).

## The shape

You are the sysadmin of a station. You sit at the **mainframe** — the one
machine with a desktop. Everything else is a box you `ssh` into, or `sshfs`
when ssh-ing to everything stops scaling.

**Segments** dock and become your problem. Each has services with needs
(power, data, compute). A segment whose needs are met earns money. A segment
whose needs are not met earns less, or nothing. That is the entire economic
engine and it means **bad administration is directly visible as a smaller
number**, not as an abstract score.

## Power is the universal currency

Everything meters through one system, and power costs money:

| action | cost |
|---|---|
| running any hardware | its draw, continuously |
| **replicator** — ordering a part | the part's price, plus a power spike |
| **teleporter** — moving to a segment to wire it | a small power cost per trip |

So the power bill is where all your decisions show up. Over-provision and you
bleed money; under-provision and segments miss their service level and stop
earning. There is no separate "score" — there is a bank balance.

The teleporter cost is deliberately small. It should make a trip feel like
something, not make you avoid maintenance.

## What gets deleted

**The piloting, the waypoints and the deliveries.** They were the least
sysadmin-like thing in the build: `heading = bearing; sleep(5)` was never
interesting and nobody's job is measured in deliveries. The station does not
go anywhere.

## What is kept

- the file model, the language, the VM and its instruction budget
- parts, slots, and the rails/spines topology with finite capacity (D15)
- compute → heat → throttle → wear (D9)
- symptoms that do not name their cause (D12)
- ssh / sshfs / bind as equivalent routes to a machine (D14)
- alien salvage and translation shims (D13) — now a source of cheap parts
  with awkward interfaces, rather than the main loop

## The four choke points, and how each one bites

Each has several valid answers, which is the test of whether it is a good
constraint:

**CPU.** More segments means more daemons means the instruction pool is split
thinner. Answers: buy a bigger CPU, buy a second one, write cheaper scripts
(sleep more), downclock something in `/etc`, or let a low-value segment run
degraded.

**Network.** Every device on a spine shares its capacity. Answers: a wider
spine, a second spine, move a chatty device, or accept intermittency on the
segment that pays least.

**Power.** Rails have finite capacity and reactors have finite output.
Answers: a bigger rail, a bigger reactor, split the load, downclock, or shed a
segment deliberately.

**Money.** Everything above costs money you earn from segments that need the
above to work. Answers: fix the cheapest bottleneck first, or run lean and
bank it, or take on a segment you cannot quite support and race to fix it.

## Sequencing

1. delete the piloting; segments earn; power costs money  ← this change
2. replicator and teleporter
3. the MATE desktop on the mainframe, with the IM pager
4. segments arriving on contract, and SLA/uptime scoring
