# D40. The supplier, and the fact that you have to be able to reach it

## What was asked for

The owner:

> "I want a browser app you order things from on the user's desktop computer
> in the server room."

and, a few minutes later, the thing that decides the shape of the whole
record:

> "If your network fails and you can't order what you need, that is end game
> — not a failure of the game, but a *you did not do it right, try again*."

> "In my mind if your core switch dies, you can wire up your main box to use
> the uplink and use that to order — just takes being clever to get out of
> that tight spot."

So this is not a shop with a safety net under it. It is a shop on the
internet, and the internet is a thing the player owns and can break.

## The shop is printed off the catalogue, never typed

`halbert.co.uk` is a trade supplier: a front page, a delivery-and-returns
page, a catalogue and a discontinued list. The last two have **no body in
`PAGES[]` at all** — they are computed at fetch time from `KIT[]` in
`core/site.c`, through `site_kind_name/ports/price/port_mb/has_os/by_name`.

**How a page can be computed while the rest stay literal.** A `NULL` body
means the page is generated, and `gen_page()` says by what. The alternative
was a fifth member on `Page` holding a function pointer, and it was rejected
for a boring reason that is nonetheless the right one: with `-Wextra` on,
four hundred existing initialisers that do not mention the new member warn,
one each. Four hundred `, NULL`s to serve two pages is a worse trade than one
branch in `net_fetch`. Everything else about a generated page is identical —
it is a `PAGES` entry, so it is in the nameserver's zone, in the 404 index,
and in the enumeration `--mancheck` walks. A computed page is held to exactly
the same standard as a written one, which is why it lives in the same table.

**The rule inside the generator.** Every FACT is read from the catalogue at
the moment of the fetch; every OPINION is typed. The supplier's copy about a
product contains **no numbers at all** — not a price, not a port count, not a
speed — because the moment a sentence says "four hundred" it is a second copy
of `KIT[]` and this project has shipped that bug five times in a day. So
switch24's paragraph says *"the top ones are faster than the rest. The table
says which"*, and the sentence that follows the copy is generated:

    24 sockets, 1000 Mb each except ports 22 to 23, which are 10000 Mb. It is
    an appliance. There is no shell on it and no button: give it a socket and
    it comes up, and you talk to it over its management line rather than by
    logging in.

**What is for sale** had to be inferred, because nothing in `site.h` says.
It is `price > 0`: the ISP's handoff and the tenant's own desk cost the player
nothing and are not the landlord's to buy. A shop sells what it can charge
for. If that ever stops being true the honest fix is a `site_kind_for_sale()`
accessor in `core/site.c`, and this file should use it.

**The discontinued page filters itself.** It lists `hub`, `switch16` and
`printserver` — with the reason the counter would give — and every name is
checked against `site_kind_by_name()` as the page prints. A name the building
starts to understand is dropped rather than advertised as unavailable, so the
page cannot become a lie by somebody else's commit.

### What the catalogue widening will need from this file: nothing

Measured rather than asserted. In a scratch checkout, `switch48` — 48 ports,
700, four SFP+ on the end — was added to `KIT[]` and `SiteDevKind` and
nothing else was touched:

    what        sockets   each socket    price
    switch8           8       1000 Mb      120
    ...
    switch48         48       1000 Mb      700   ports 44-47 at 10000 Mb

    switch48
    --------
    New line, and we have not had one long enough to have an opinion about it.
    The numbers in the table are the manufacturer's and they are what you will
    be charged.

    48 sockets, 1000 Mb each except ports 44 to 47, which are 10000 Mb...

    order a switch48 [order:switch48] -- to your goods in, in the box.

A new product appears in the table, gets its own section, its own generated
specification, its own order link, and an honest line saying the counter has
not written it up yet. The gate picked it up in the same run and asserted its
numbers. **A product is in the shop because it exists.**

## Ordering goes through the verb, and there is no basket

An `<a href="order:switch8">` is what `mailto:` is in a real browser: a link
the CLIENT handles rather than fetches. `browser.gd` handles it by asking to
confirm — there is no `sell` in this game and no refund anywhere in it, so a
mis-click would be money with no undo — and then calling
`machine.ses_cmd("order switch8")`, which is `session_line()`, the identical
call a socket client makes when they type it in the building.

The window knows nothing about what it is selling. The kind comes off the
page, the price is never seen by GDScript at all, and the receipt is core's
own sentence:

    switch8: a switch8, 8 ports, 120 paid, 57480 left.
    the van leaves it in f0 goods in #12, 50 m from here.

`links` renders the same page at a prompt and prints `[order:switch8]` beside
the link, which is what a text browser does with a scheme it does not handle;
the page says so, and the same order is one word at the tower prompt.

## The connectivity question, and how it was resolved

The first draft of this feature had the supplier's front page say the order
line was not the website — *"say the word where you stand in the building and
the van comes anyway"* — a safety net so that a player with a broken network
could still buy the part that fixes it.

**That was wrong and it is gone.** The owner overruled it, and he is right:
a shop that is always reachable is a shop that costs nothing to lose, and the
network stops being load-bearing. The page now says the opposite, in the
supplier's voice:

> Which does mean that if you cannot reach this page, you cannot buy
> anything. We are aware. Several of our customers have pointed out, at
> length, that the one thing you might urgently need a switch for is a network
> that is too broken to order a switch over. Our position is that we are a
> hardware supplier and not a telephone exchange.

Three things had to be true for that to be a loss rather than a bug.

**1. It must be legible when it bites.** `browser.gd` used to put the raw one
line from `links` in the page area and the word "failed" in the status bar.
"cannot resolve halbert.co.uk" is four different faults wearing one sentence.
A failed fetch is now a page: what could not be reached, the machine's own
complaint, and then `cat /etc/resolv.conf`, `ip addr` and `ip route` RUN on
that machine with their answers printed underneath. Nothing is diagnosed and
nothing is guessed — every line under the rule is output — and the closing
paragraph names the next thing to type rather than an opinion. The supplier's
own front page carries the same four commands in the same order, cheapest
first, and `--mancheck` runs them.

`docs/screenshots/d40-shop-unreachable.png` is that page on seed 7008, and it
is not a contrived state: this seed's workstation shipped with one of the
image's decoy edits in `/etc/resolv.conf` (`# changed 12 March -- the .3
resolver was timing out at peak`), so the first thing the shop teaches you is
to read your own resolver. One line of `ed` and the catalogue loads.

**2. It must not be a silent stall, and it adds no new end condition.** A
player who cannot order cannot fix the tower; the tenancies stop getting their
work done; `siteday.c` counts the strikes, files the complaints at three, and
ends the run when they pass `site_complaints_allowed()`, saying which way. One
rule doing the work rather than two. Nothing was added and nothing needed to
be.

**3. It must not be premature.** Nothing announces defeat while there is a
move on the board, because nothing announces anything: unreachable is a page,
not a verdict.

## The escape route, and the thing that is missing

The owner's escape — re-cable your own box to the handoff — **works today**,
and nobody coded it. It falls out of two things that were already true:
`site_new()` puts the whole of `net_sites.c`'s zone and an httpd **on the
uplink device itself** (`core/site.c`, the "one box on the far side of a /30
is all the internet this game needs" comment), and copper is copper.

Played over `--towersh 7008`, in full, with the core switch killed by pulling
its plug:

    f0 MDF> mains core off
    the plug comes out of core.
    root@box1# ip addr
    1: eth0: <UP,NO-CARRIER> mtu 1500
        inet 198.51.100.2/30 scope global
    root@box1# ping -c 1 198.51.100.1
    ping: the interface it would go out of is down
    root@box1# links halbert.co.uk
    links: cannot resolve halbert.co.uk

    f0 MDF> links
       0  core:0    uplink:0   3 m  cat6  83  admin down
       1  box1:0    core:1     3 m  cat6  83  no cable

    f0 MDF> uncable 1
    f0 MDF> uncable 0
    f0 MDF> cable box1 uplink
    link 2: box1:0 to uplink:0, 3 m of cat6, 83 paid, the port comes up at 500 Mb.
    root@box1# ping -c 1 198.51.100.1
      seq=0 reply from 198.51.100.1 time=2 ms
    root@box1# links halbert.co.uk/catalogue
    Catalogue
    =========
    ...
    f0 MDF> order switch8 core2
    core2: a switch8, 8 ports, 120 paid, ... left.

A competent player can tell what they did to themselves at every step:
`NO-CARRIER` on the card, `admin down` on the link in `links`, and a ping that
says the interface is down rather than timing out.

**What is NOT true yet, and it is the real gap.** The workstation the browser
runs on is *not a device in the site*: `tower.gd` adds it with
`nports = 0, site_i = -1`, and its Machine lives on `netsite.c`'s own
10.0.2.0/24 world, which the player never built and cannot cable. So breaking
the tower cannot take the shop away from the WINDOW — only from a box in the
building. The escape above is real; the danger it escapes from is not, on the
one machine the owner named.

Closing that is core's work and it was not reached across for. What is needed,
exactly:

- **`core/site.h` / `core/site.c`**: a device kind for the player's own
  workstation — one port, gigabit, `has_os` true, price 0 — installed by
  `site_new()` in the MDF at day 0, powered, and **not orderable**
  (`site_install` already refuses `kind <= SDEV_UPLINK`; a kind above it needs
  its own refusal, and the same predicate would give the shop a real
  `site_kind_for_sale()` instead of `price > 0`). Cabled at day 0 to
  `uplink:0`, which is the shape of the whole decision: it spends the
  handoff's only port, so buying the first switch means re-cabling your own
  machine, and the player learns the escape route on day one by doing it
  forwards.
- **`core/session.c`**: build that device's `Machine` and `netsite_pin()` it
  to `ses->s.net`, as it already does for a box you put a serial lead into.
- **`game/scripts/tower.gd` / `phone.gd`**: bind the desk's `de.gd` to THAT
  machine rather than to `sh_on(0)`. (`phone.gd` has the same limitation
  already: a display lead into a tower pc shows a desktop bound to the
  workstation, not to the box the lead is in.)

With those three, everything in this record starts biting without another
line in `net_sites.c` or `browser.gd`.

## Evidence

    ./build/bf --mancheck                              75/75   (was 57/57)
    HEAD checkout + this gate only                     57/58   -- the shop is not there
    HEAD + the shop, one price typed instead of read   74/75   -- `switch8: 8 sockets, 1000 Mb, 120  FAIL`
                                                                  "the page row says `  8  1000 Mb  99`"
    game/tests/shop_orders.gd                          0 failures

The mutation that matters is the second one. Retuning a price in `site.c` does
NOT fail the gate — the page follows it, which is the entire point — so the
test had to be a page that restates rather than reads, and that is what it
catches.

`--mancheck` gained 18 assertions: five runnable examples off the supplier's
front page (`cat /etc/resolv.conf`, `ip addr`, `ip route`, `traceroute`,
`ping`, all run on a booted machine) and thirteen catalogue assertions that
read the page's own numbers back and compare them with `site_kind_*`, product
by product, including that a kind with no price is not on the page and that
every `order:` link names a kind the building accepts.

Screenshots, all from one windowed run of `--seed=7008`:

    d40-shop-unreachable.png   the failure page, with the workstation's own
                               decoy resolver visible in it
    d40-shop-on-the-desk.png   the shop on the monitor in the MDF, from across
                               the desk, before sitting down
    d40-shop-seated.png        [E], and the catalogue full size
    d40-shop-confirm.png       "Order one switch8?" -- there are no returns
    d40-shop-receipt.png       core's own sentence, 120 paid
    d40-goods-in.png           the box, on the floor of goods in, 57480 in hand

## What was NOT done

- **The workstation is still not a box on the network.** See above. It is the
  difference between this feature being interesting and this feature being
  correct, and it is three files that belong to other people.
- **`de.gd` gained `--browse=<url>`** and nothing else. It is the browser's
  `--run=`, for the same reason: the interesting states of this app are
  several clicks deep and a client with no mouse cannot photograph them. It
  goes through `_go()`, so it can only reach what a player can reach.
- **No lead times, and the page says why.** "Ordered is delivered" is what
  `site_order()` really does, so a lead time would have been the first lie on
  the page. Stock is expressed as presence: if it is on the catalogue the van
  has it, and if we cannot get it, it is on `/discontinued`.
- **The shop cannot be reached from the tower's own web server.** A tenancy's
  desks and the boxes the player builds are on the player's network, and the
  supplier is out past the handoff. That is right, but it means a floor with
  no route out has no shop either, which is the same fact as everything else
  in this record.
- **`look` in goods in prints `8/8 ports used` for a switch nobody has cabled**
  and `next free port switch8:0` on the same line. Two facts about the same
  box disagreeing, in the room the shop delivers to. It is not this record's
  code and it was not touched, but it is the exact shape of defect this
  project keeps finding and somebody should have it.
