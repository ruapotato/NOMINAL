# D41. The machine you sit at is a box in a room

## What was asked for

D40 built a supplier's website and ended by naming the hole it could not
reach into:

> **The workstation is still not a box on the network.** It is the difference
> between this feature being interesting and this feature being correct, and
> it is three files that belong to other people.

The owner had already decided what the hole is for:

> "If your network fails and you can't order what you need, that is end game
> — not a failure of the game, but a *you did not do it right, try again*."

> "In my mind if your core switch dies, you can wire up your main box to use
> the uplink and use that to order — just takes being clever to get out of
> that tight spot."

and then, when asked which refusals should protect the player's own machine:

> "The computer in the IT room you use should just be like the others — you
> can recable it as you see fit, even pick it up and move it if the player
> feels like it."

So: an ordinary box, with no special rules, that the desktop really runs on.

## What it is

`SDEV_WORKSTATION`: one gigabit socket, an operating system, price 0, not in
the shop. `site_new()` installs it in the MDF on day 0, plugs it into the
wall, switches it on, cables it to `uplink:0` and addresses it for the
handoff — `198.51.100.2/30`, gateway and resolver `198.51.100.1`, which is
what the person who had the job before the player left behind. It is called
`ws`, because the tower prompt has to be able to say it in a line somebody
types.

**Everything else about it is a pc.** No refusal anywhere in `core/site.c`
knows what a workstation is. `carry ws` while it is running is refused with
the sentence a running server gets, and `power ws off` is the way past it;
`carry ws` with a lead in it is refused with the sentence a cabled switch
gets, and `uncable` is the way past that; put it down in a room with no
socket free and it does not come back on until the player does something
about the wall. Played, in full, on seed 7008:

    f0 MDF> carry ws
    refused: ws is running, and you did not pick it up. Lifting a machine
      starts with pulling its plug out, and pulling the plug on something that
      is running is a blackout with one machine in it.
      `power ws off` first -- that is the shutdown, and it costs nothing.
    f0 MDF> power ws off
    f0 MDF> carry ws
    you pick ws up. It goes where you go until you `drop` it.
    f1 comms cupboard> drop
    ws is in f1 comms cupboard #35 now. 1 port, and nothing in any of them yet.
      the lead goes into a wall socket: 3 of this room's 4 left.

Carry it into a full cupboard and the desktop does not come back until the
player buys a socket or carries it somewhere else. That is not a trap: it is
the rule every other box in the building runs on, applied to the one the
player happens to sit at, and there is always a move because the thing is
standing in a room a person can walk to.

### Where it stands, and why the MDF

The owner said "the server room". This generator does not make one on the
ground floor: floor 0 is goods in, the MDF, a comms cupboard, plant and the
riser (`--building 200` enforces that shape). The MDF is the building's frame
room — it is where the ISP's handoff is, it is wired for a frame with eight
sockets, and it is the room a landlord's own machine would be in. It also
makes the day-one lead a three metre patch lead between two boxes on the same
wall rather than a fiction. `game/scripts/tower.gd` already drew the desk
there; now it draws it wherever the **site** says the box is standing.

### What happens to it in the weather

Nothing was written for it, which is the point. It is on the landlord's
outlets, so a blackout takes it down like anything else without a `ups`, and
`site_unclean_stop()` damages its filesystem by the same one-in-twenty it
uses on every other machine. Pulling its plug while it is running is the same
event as the blackout, in the same function, for the same reason D37 gave.
The only thing that is different is what the player sees: the desktop is not
there in the morning, and the reason is in `events` with everything else.

## The lead the building came with, and why one lead has a rule

The shop agent's proposal had one line in it worth more than the rest:

> it spends the handoff's only port, so buying the first switch means
> re-cabling your own machine

That is the escape route performed forwards, on day one, before it is needed.
It is kept. What it cost is a rule about exactly one lead, and the rule is
written down rather than smuggled:

**A socket takes one plug.** On the morning the player gets the keys their
workstation is in the handoff's only port on a lead nobody paid for. When
they cable their first switch to that port, that lead comes out, and the
session says so at the moment it happens:

    f0 MDF> cable core:0 uplink:0
    the lead the building came with comes out of that port to make room:
      ws is off the network now, and the shop on it with it. `cable ws <box>`
      is how it comes back.
    link 1: core:0 to uplink:0, 3 m of cat5e through the tray, 77 paid, the
      port comes up at 500 Mb.

It does not go anywhere else. The player's own machine is off the network,
and the shop with it, until they patch it into whatever they just put in
front of it. `SiteLink.factory` is that lead, there is exactly one of them in
a game, and it has the property once: pull it and it is gone, and every lead
after it refuses with `SITE_EBUSY` like everything else in this file.

**Why the exception exists rather than a plain refusal.** Refusing would make
the first switch anybody buys an error message: `cable core uplink` on day
one would say "uplink port 0 already has a cable in it", and the move it was
asking for is the right move. And there is a measurement behind it, which is
the honest reason it is shaped this way and not the other way:

| day one is... | `--sitecheck` | `--loadcheck` | `--eventcheck` |
|---|---|---|---|
| the box, with no lead in the handoff | 589/610 | 35/35 | 83/83 |
| the lead in, and it refuses to give way | 572/610 | **20/35** | **74/83** |
| the lead in, and it gives way (shipped) | 610/610 | 35/35 | 83/83 |

The middle row is the interesting one and it is why this rule exists.
`core/loadcheck.c` and `core/eventcheck.c` both build their towers with
`cable edge:0 uplink:0` — two of loadcheck's scenarios through a real
`Session` and three straight through `site_cable()` — and a workstation that
holds that port and will not let go takes the internet away from fifteen of
the curve's thirty-five assertions and nine of eventcheck's. Those two files
were out of scope and, more to the point, they are RIGHT: a player cabling
their core switch to the handoff on day one is what everybody does. The
alternative shapes were measured and rejected: a second port on the handoff
throws away the whole decision (and makes the workstation permanently
un-losable, which is the danger D40 exists to create), and no day-one lead at
all means a player's first five minutes are a shop they cannot reach for a
reason nothing in the room explains.

`look` says both halves, because "all 1 ports used" on its own would tell a
player, reasonably and wrongly, that there is nowhere in this building to
plug anything in:

    f0 MDF> look
      kit in this room:
        uplink       uplink    198.51.100.1/30   all 1 ports used
          uplink:0 has the lead the building came with in it, to ws. Cable
          anything else there and that lead comes out.
        ws           workstation 198.51.100.2/30  [an OS is running on it]   all 1 ports used
          ws:0 has the lead the building came with in it, to uplink. Cable
          anything else there and that lead comes out.

## The desktop runs on it

`gdext/nominal_gdext.c` had two workstations in it without knowing: `st->desk`,
the break-fix bench's healthy machine on nobody's network, and now a box in a
room. `DESK(st)` is the one function that decides which, and it is the same
shape as `BLD()` and `SITE()` beside it — the session's when there is one,
the bench's when there is not. `sh_on(0, ...)`, `de_apps()`, `de_requests()`
and `peer_addr()` all go through it, so the desktop, the browser, the files
app and the terminal moved onto the box in the MDF **without one line
changing in `de.gd`**. That is the test of whether the seam was in the right
place.

`core/session.c` builds and pins that Machine in `session_start()` rather
than on first power-on, which is the one place the workstation is treated
differently from a pc — and it is not an exception to the rule, it is the
rule: the site says the box is switched on, so it is switched on, and the
monitor in the MDF has a desktop on it from the first second because a person
walked away from it last night.

The window follows the model rather than assuming:

- `tower.gd` draws the desk in the room the SITE says the box is in, and the
  device it adds is the site's device — `nports 1`, the site's index, the
  site's name — so a lead into `ws:0` in the window and `cable ws core` at the
  prompt are one act on one object. It used to be added with `nports 0,
  site_i -1`: a picture of a computer.
- the monitor is dark when the site says the box has no power in it, and
  `sit_down()` refuses with the reason and the verb. `docs/screenshots/
  d41-workstation-live.png` and `d41-workstation-dark.png` are the same desk
  either side of `power ws off`.
- `_site_sig()`, which is what decides the view has to be rebuilt, now
  includes the button and the plug. Without that the desktop stayed painted
  on the monitor of a machine the site said was dead — `power ws off` moves no
  box and changes no port, so nothing noticed. That was found by taking the
  photograph, which is what the photographs are for.

## The bugs fixed on the way

**`look` in goods in disagreed with itself.** A switch8 in its box, with
nothing in it, printed `8/8 ports used   next free port switch8:0` — one line,
one box, two counts. The port count was read off the NETSTACK, where an
unplugged switch has its ports administratively down rather than empty; the
free port came off the site's link table, which is where a lead in a hole is
recorded. `site_port_used()` / `site_ports_used()` are that one source, and
`dev_line()` reads them. Sixth instance of one-fact-two-answers this week.

**`power` blamed the wall for the plug.** Reported by the coordinator against
HEAD: a pc in goods in, which has two free sockets, was refused with "there is
no free outlet on that room's wall" printed one line above a table saying
there were two, and told to buy a third. `SITE_ENOMAINS` was set from two
places for two reasons and carried one sentence. It is now two codes:
`SITE_ENOMAINS` means the room has no socket left, and `SITE_EUNPLUGGED` means
there is no lead from this box to a wall — with `mains <box> on` in the text,
which is the move and which neither the message, the outlets table nor `help`
had ever named. `site_dump_outlets()` splits its advice on
`site_room_outlets_free()` the same way; its comment already claimed it did.

**`help` at the site prompt named neither the plug nor the socket.** `mains`,
`outlet` and `outlets` are verbs the site answers to and the page did not have
them; the only occurrence of the word "mains" was inside the `ups` entry's
prose. Added, next to `power`, whose refusal is the reason a player is reading
that page. `httpd <dev> [port]` too, which the coordinator's new
`--mancheck` `check_help()` caught in the same pass: the page documented how
to serve names and how to fetch a page, and not how to serve one.

## Evidence

Gates, on a clean rebuild:

| gate | HEAD (e0a6373) | now |
|---|---|---|
| `--sitecheck` | 600/600 | **610/610** |
| `--mancheck` | 75/75 | **79/79** (78 of them the coordinator's gate + `httpd`) |
| `--netcheck` | 285/285 | 285/285 |
| `--loadcheck` | 35/35 | 35/35 |
| `--eventcheck` | 83/83 | 83/83 |
| `--health` | 20/20 | 20/20 |
| `--building 200` | 200/200 | 200/200 |
| `--solve 60` | 60 / 60 | 60 repaired, 60 handed back |
| `--askcheck` | 2844/2844 | 2844/2844 |
| `tools/check-decoys.sh` | 37/37 | 37/37 |
| `make test-cpu` | pass | pass |
| game: smoke, console_speaks, rescue_close, desk_holds, shop_orders, tower | 0 failures | 0 failures |

**Shown failing against HEAD.** A clean `git archive HEAD` checkout in
`.../nominal-workstation-agent-d41-baseline`:

    HEAD + this record's core/sitecheck.c      does not compile
      core/sitecheck.c:80: implicit declaration of 'site_workstation'
      core/sitecheck.c:81: 'SDEV_WORKSTATION' undeclared
      core/sitecheck.c:87: implicit declaration of 'site_port_factory'
      core/sitecheck.c:90: implicit declaration of 'site_kind_for_sale'
      core/sitecheck.c:533: 'SITE_EUNPLUGGED' undeclared

    HEAD + this record's core/sessioncheck.c   582/603  -- 21 failing
    with the feature                           610/610

The session half was written to build against a tree with no workstation in
it, deliberately, the same way D37's did: it finds the machine by the name
`look` prints (`ws_dev()`) rather than by an enum member, so it fails on
assertions rather than at the compiler and the count means something.

**And each behaviour, removed one at a time, in a copy of this tree:**

| mutation | `--sitecheck` | `--loadcheck` | `--eventcheck` |
|---|---|---|---|
| the workstation is not installed at all | 185/238 (the gate does not finish) | — | 6/13 |
| the box is there, the day-one lead is not | 589/610 | 35/35 | 83/83 |
| the lead is in and does not give way | 572/610 | 20/35 | 74/83 |
| `look` counts ports off the netstack again | 609/610 | | |
| one code and one sentence for the plug and the wall | 607/610 | | |
| `mains`/`outlet`/`outlets` out of the site help | 609/610 | | |

And in the window, with `DESK()` returning the bench's machine again — which
is HEAD's behaviour — `game/tests/shop_orders.gd` reports **2 failures**: the
catalogue does not load on day one, and re-cabling the workstation to the
handoff does not bring it back. With the seam in place, 0.

## The owner's scenario, played end to end

`./build/bf --towersh 7008`. Build a tower, kill the core switch by pulling
its plug, and get out of it. Abridged only where a page of catalogue was
printed.

    f0 MDF> order router edge
    f0 MDF> order switch8 core
    ... carried up from goods in, one box a trip ...
    f0 MDF> cable edge:0 uplink:0
    the lead the building came with comes out of that port to make room:
      ws is off the network now, and the shop on it with it. `cable ws <box>`
      is how it comes back.
    link 1: edge:0 to uplink:0, 3 m of cat5e through the tray, 77 paid,
      the port comes up at 500 Mb.
    f0 MDF> cable edge:1 core:0
    f0 MDF> cable ws:0 core:1
    f0 MDF> addr edge 198.51.100.2/30
    f0 MDF> addr edge:1 10.0.1.1/24
    f0 MDF> gw edge 198.51.100.1
    f0 MDF> router edge on
    f0 MDF> addr ws 10.0.1.50/24
    f0 MDF> gw ws 10.0.1.1
    f0 MDF> resolver ws 198.51.100.1
    f0 MDF> plug ws
    serial console on ws.  [UP at target]
    root@ws# links halbert.co.uk
    Halbert & Sons (Trade) Ltd ...

The shop is on the desk, three hops out through a router the player built.
Now the core switch dies:

    f0 MDF> mains core off
    the plug comes out of core.
    f0 MDF> plug ws
    root@ws# ip addr
    1: eth0: <UP,NO-CARRIER> mtu 1500
        inet 10.0.1.50/24
    root@ws# ping -c 1 198.51.100.1
    ping: the interface it would go out of is down
    root@ws# links halbert.co.uk
    links: cannot resolve halbert.co.uk

Everything the player needs is on the line: `NO-CARRIER` on their own card, a
ping that says the interface is down rather than timing out, and a name that
will not resolve because nothing can leave the building. And the way out is
legs and copper:

    f0 MDF> links
       1  edge:0     uplink:0   3 m  cat5e  77  up
       2  edge:1     core:0     3 m  cat5e  77  admin down
       3  ws:0       core:1     3 m  cat5e  77  admin down
    f0 MDF> uncable 3
    f0 MDF> uncable 1
    f0 MDF> cable ws:0 uplink:0
    link 4: ws:0 to uplink:0, 3 m of cat5e, 77 paid, the port comes up at 500 Mb.
    f0 MDF> addr ws 198.51.100.2/30
    f0 MDF> gw ws 198.51.100.1
    f0 MDF> plug ws
    root@ws# ping -c 1 198.51.100.1
      seq=0 reply from 198.51.100.1 time=4 ms
    root@ws# links halbert.co.uk/catalogue
    Catalogue
    =========
    ...
    f0 MDF> order switch8 core2
    core2: a switch8, 8 ports, 120 paid, 58802 left.

Nobody wrote that. It is copper, a card, a /30 with two addresses in it and a
resolver on the far side of a wall — and the only reason it is available is
that the machine the player orders on is a box standing in a room.

The same thing, in the window, is `game/tests/shop_orders.gd`'s last section:
the catalogue loads, `uncable 0`, the catalogue is a failure page with that
machine's own `ip addr` saying `NO-CARRIER` underneath it, `cable ws:0
uplink:0`, and the catalogue is back.

## The judgement calls, in one place

- **The MDF**, for the reasons above, and the window follows the model rather
  than the other way round.
- **No special refusals.** The owner's ruling. What makes it different from a
  pc is two facts and no rules: you did not buy it, and it is not in the shop.
- **`site_kind_for_sale()`** is that second fact written down. It is a column
  in `KIT[]` rather than `price > 0`, which D40 asked for and inferred; the
  supplier's page in `core/net_sites.c` still infers it, because that file was
  out of scope, and the two agree exactly today (`--mancheck` asserts that a
  kind with no price is not on the page, and it now walks one more kind).
- **The day-one lead gives way once.** Measured against the alternatives,
  above.
- **It is on the landlord's outlets and has no battery.** A blackout takes it
  like anything else. `ups ws` is 220 and is exactly as available as it is for
  any other box.

## What was NOT done

- **`core/net_sites.c` still infers "for sale" from the price.** The accessor
  it should call now exists. One line, in a file this record did not own.
- **A display lead into a tower pc still shows the workstation's desktop.**
  D40 named this and it is unchanged: `phone.gd` knows `which 0` and
  `which 1`, and a per-device desktop needs a shell-on-any-box method in the
  extension. What it does now do is go dark when the workstation has no power,
  instead of claiming a picture by construction.
- **`de.gd` was not edited at all.** It did not need to be, which is the good
  news; it also means nothing in the desktop yet SAYS which machine it is on
  beyond the hostname in the panel. A person sitting down at `ws` and a person
  sitting down at the bench's machine see the same window.
- **No `--loadcheck` scenario re-cables the workstation**, so nothing measures
  a sixty-day tower in which the player's own machine has been moved. Same
  gap the jack and the outlets have, and the same answer: it is a playthrough
  rather than a gate.
- **The workstation's Machine is installed eagerly**, one per session, ~13.5 MB.
  That is deliberate (the site says it is switched on) and it is the only box
  in the game that costs memory before anybody touches it.
