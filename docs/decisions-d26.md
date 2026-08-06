# D26. The 3D was played over a socket, and the world kept up

## What was being tested

D23 committed to a rule and predicted the cost of breaking it: *"the view is
never the source of truth... Ordering, carrying, cabling and configuring must
all be drivable through a scriptable interface, with 3D being how a human does
it. If it cannot be played over a socket, it cannot be tested, and it will
rot."*

Until now that was an intention. Every 3D change had been checked by taking a
screenshot and looking at it, which is exactly the loop the owner rejected:
*"Claude playing a video game in three D space by taking screenshots of the
actual user interface is not a fantastic way for it to iterate."*

`game/scripts/wire.gd` closed the gap: a socket on 127.0.0.1:7373 that feeds
lines into the *running window* through the same session the keyboard drives.
This is the record of the first playtest that used it.

## The result

A playtester with no access to the screen played from day 0 to day 53 --
opened five floors, built a seven-VLAN router-on-a-stick, ran fibre to every
floor, served four tenancies, took a mains failure on the chin -- and reported:

> **I found no case where the world failed to follow the text.**

Specifically, and each one verified against a screenshot afterwards rather
than assumed:

- `open` on floors 2/3/4 moved the HUD from "2 of 6 floors in service" to
  "5 of 6", and the next-floor hint retargeted each time.
- `go #99` put the body in the f4 stairwell, HUD reading `floor 4 stairwell
  (30, 12 m)`.
- `carry` then `drop` left the switch drawn racked in the f3 comms cupboard.
- `serve 3 sw3a cat6 13` -- one line -- produced **a bundle of twenty blue
  patch leads running up the cable tray into the switch**.
- `day` raised the report panel in the window with text matching the socket
  byte for byte.

Every failure the playtest found was text-versus-machine. None was
text-versus-world. **The architectural rule holds, and 3D changes are now
testable by an agent that cannot see.**

## What it cost to find out

Eight bugs, and the shape of them is worth recording because it is not the
shape screenshot-driven testing was finding.

The worst were self-contradictions inside a single screen. `show <box>` on a
server that a power cut had killed printed `SWITCHED OFF, and nothing of it is
on the network` in its header and `It is on the network and serves nothing
from it` four lines later. `look` in the same room in the same second said
`[an OS is running on it]`. A screenshot test cannot see that; only something
reading the words can. Their verdict: *"I would have walked away believing the
box was up."*

Then a class of failure that only a text player hits: **`carry` refusing
without saying it refused.** With a drum in your hands it answers *"you have a
drum of cable in your hands. `spool back` puts it on the shelf"* -- a true
sentence that never says the carry did not happen. They walked to the MDF,
typed `drop`, and got *"you are not carrying anything."* It cost them a 36 m
run of cat6 laid to a server still on the floor of goods in. The same verb's
refusal for a cabled box was called exemplary, and is the model to copy: it
names the fault, the diagnostic and the fix.

And one straightforward lie: `help` says day one holds *"exactly one thing:
the ISP's socket on the wall of the MDF"*, while goods in already holds a
router, a switch and a server, £2,400 already spent, with nothing telling you
they are there. They believed the text, bought duplicates, and there is no
`sell`.

## The gap the socket does not close

Two of their notes are about the socket itself rather than the game.

**Inside a serial console the prompt does not change.** `wire.gd` builds the
prompt from the room, so after `plug files` a `dmesg` goes to a different
machine with no visible change in state. For a player with no screen that is
the most dangerous piece of missing state in the interface.

**A socket player cannot aim the camera.** Their end-of-run screenshot showed
an empty floor rather than the four racks they had filled, because the body
was facing the other way and there is no `turn`, `face` or `look at <box>`.
The `day` modal can only be dismissed with `[Esc]`, a keystroke a socket does
not have. If screenshots are to be a *check* on the text -- which is the only
job left for them now -- the text has to be able to point the camera.

## The other thing it proved

The run ended the way the design says it should. One server in the basement
holding everybody's files, everything hairpinning through a single
router-on-a-stick leg, and:

```
port 1  up  10000Mb full 3m tx 176067 rx 177201 drop 13523
        13523 of those drops were this port's egress buffer full: 48 KB is
        40us of wire at 10000Mb, and the queue reached 42us
```

`edge:1` at **3% average utilisation dropping 13,523 frames** -- which is
exactly why `load`'s legend tells you to read the drops and the peak queue
rather than the average, and the playtester followed that instruction and
diagnosed it correctly. Three complaints, lease not renewed, day 53.

That is a player losing to arithmetic they could have seen coming, which is
what D23 was for.
