# D15. Complexity comes from topology, not from a fault table

## What Tower Networking Inc. actually does

I went and read how it works rather than guessing. The important finding:

**Nothing in TNI rolls a die to decide what breaks.** Every failure mode is a
consequence of how the player built the network:

- **Switches broadcast to every port.** So as the tower grows, bandwidth
  saturates — not because a fault fired, but because of what a switch *is*.
  A path through switches takes ~12 hops where routers take 4.
- **Routers need routing-table entries.** Each new router requires manual
  updates on every existing router for every provider. N×M configuration
  explosion, purely structural.
- **Packets with no matching route** hit the default port or drop. A missing
  line in a table, not an event.
- **RIP is the relief.** `rip advertise` / `rip listen` lets routers learn from
  each other. You reach for it *because* the manual config became unbearable.

That last point is the whole design: **the scaling pain is the teacher.** You
do it by hand until it hurts, then the game hands you the thing that scales.
That is exactly the arc I want for `ssh` → `sshfs`, and it is validated.

Dynamic events (power cuts, cyberattacks) exist, but they are a *layer on top*
of a system that is already interesting. They are seasoning, not the meal.

## What that means for NOMINAL

My fault system is the weak version: pick a random card, degrade it. The player
learns a lookup of fault→fix and it is over. It has to be replaced with shared
infrastructure that has a topology.

**Rails, spines and loops.** The station carries three shared utilities:

| utility | carries | saturating it |
|---|---|---|
| power rail | MW | everything on the rail browns out together |
| data spine | device traffic | everything on the spine goes intermittent together |
| atmosphere loop | air between segments | a leak anywhere drops pressure everywhere on the loop |

Each has a finite capacity and a finite number of ports. Every device must be
patched into one of each utility it needs. **Which one you pick is the decision**,
and it is the same decision as "which switch do I plug this client into".

## The failure this produces

This is the shape I want, and it is not in the game yet:

> You commission segment 5 and patch its lab into `rail-2`, because rail-2 is
> the nearest one with a free port.
>
> Forty minutes later somebody in **segment 4** messages you: *"it's 12° in
> here and we're losing samples."*
>
> Nothing in segment 4 is broken. Segment 4's scrubber is on rail-2, and rail-2
> is now over capacity, so it is getting 0.9 MW of the 1.5 it needs.
>
> The cause is a cable you patched in a different segment, for a different
> tenant, forty minutes ago.

No die was rolled. The player did this to themselves, which is why it is fair,
and why the fix has many valid forms: move the lab, buy a bigger rail, split
the load across two rails, downclock the lab, or accept it and tell segment 4
they are getting a worse SLA.

## Making it diagnosable: `trace`

A topology the player cannot see is just unfair. The diagnostic is a
`traceroute` for infrastructure — follow a device back through everything it
depends on and show where the chain breaks:

```
$ trace /dev/atmo/seg4
/dev/atmo/seg4
  <- seg4:scrubber     needs 1.50 MW, getting 0.90     DEGRADED
     <- rail-2         7.00 MW capacity, 8.40 demanded  ** SATURATED **
        also on rail-2: seg5:lab (2.40), seg4:heater (1.10), cpu0 (2.00)
        <- reactor0    6.50 MW
```

The last line is the answer and the player reads it themselves. That is the
same job TNI's CLI does when it traces connectivity between equipment.

## Consequences

- The random hardware-fault generator gets demoted to seasoning, exactly as
  TNI's power cuts are. It stays, at a much lower rate.
- `slots`/`lspci` is no longer enough to understand the machine — you need
  `trace`, and eventually a visual patch panel, because a graph is the one
  thing a terminal renders badly. That is the strongest argument yet for the
  DE being more than a terminal.
- Segments arriving is what grows the graph, so growth *is* difficulty, with no
  difficulty curve to hand-author.

Sources: [Tower Networking Inc. on Steam](https://store.steampowered.com/app/2939600/Tower_Networking_Inc/),
[router guide](https://steamcommunity.com/sharedfiles/filedetails/?id=3548481566)
