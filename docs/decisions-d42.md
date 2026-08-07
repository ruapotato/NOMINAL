# D42. The web is machines now, and they are out past the handoff

## What was asked for

The owner:

> "For the site you reach, we might want some VM/computers that the user never
> sees or touches that act as the WWW, that the uplink connects to, so the WWW
> does exist in the game, just out of scope for the user."

and:

> "Have the WWW servers be real VMs would be nice. Might be fun to make one of
> the things you can do is hack the shop computers and order things for free —
> though that can come later, just make them real boxes for now."

So: real boxes now, hacking later, and *built so the hacking is possible later
without redoing this*. That last clause is what decided every choice below.

## What it was

`site_new()` gave the ISP's handoff — the box on the MDF wall — every address
in `net_sites.c`, a name server holding the whole zone, and an httpd. The
comment said so in as many words: *"one box on the far side of a /30 is all
the internet this game needs"*. It was true when it was written and it is the
reason D40's escape route worked at all.

What it cost is that nothing this project models applied to the internet. The
supplier's shop was three metres from the player, one hop away, on a wall.
`traceroute halbert.co.uk` had one line in it. The circuit could not slow it,
because there was no circuit between them. And the price list was a C string,
so no act inside the game could ever touch it.

## What it is

    uplink     198.51.100.1/30   the handoff. ONE customer socket, as before.
               198.51.100.5/30   wan0: the way out, and nothing in the tower
                                 can see it or cable into it.
    isp-core   198.51.100.6/30   the ISP's router, four cards.
               198.51.100.9/29
               10.0.2.1/24
               10.0.3.1/24
    isp-ns     198.51.100.10/29  A REAL MACHINE. The zone is in
                                 /etc/net/services on its own disk.
    halbert    10.0.2.73/24      A REAL MACHINE. The supplier. Its httpd
                                 serves /srv/www off its own filesystem.
    www        10.0.2.20/24      NOT a machine: one box holding the addresses
               10.0.3.10/24      of the web's other twenty-odd names, served
               203.0.113.20      out of net_sites.c's table as before.

Played, on seed 7008, with a pc cabled straight into the handoff:

    root@box1# traceroute 10.0.2.73
    traceroute to 10.0.2.73, 12 hops max
    1 198.51.100.1
    2 198.51.100.6
    3 10.0.2.73
    root@box1# ping -c 1 10.0.2.73
      seq=0 reply from 10.0.2.73 time=20 ms
    root@box1# links halbert.co.uk/catalogue
    Catalogue
    =========
    ...
        what        sockets   each socket    price
        switch8           8       1000 Mb      120

Three hops, and the third one is a booted NomnixOS machine whose web daemon
read `/etc/httpd/httpd.conf`, found `DocumentRoot /srv/www`, and served the
file `/srv/www/catalogue` off its own disk.

## The measurement that decided the shape

Asked for before anything was committed to, and it is the whole argument.

    1 machine    rss 1720 -> 91236 kB     boot 121 ms
    32 machines  rss 1800 -> 507952 kB    boot  92 ms each

The marginal machine is **13.0 MB** ((507952 − 294156) / 16 over the 16→32
step) and **92 ms** to install and boot. `net_sites.c` has **30 hostnames on
23 addresses** — not the ten the brief guessed at.

Thirty booted machines is **390 MB and 2.8 seconds**, on every tower. And
towers are not rare: `--sitecheck` builds **71** of them in one run. That is
three minutes of gate time and it buys nothing, because twenty-eight of those
thirty boxes are one-page jokes with no state, no service and nothing a
player could ever do to them.

**So two boxes are real and the rest of the web is a hosting box.** The two
that are real are the two a player can have a relationship with:

- **the shop**, because it is what the money goes through, and because the
  owner named it;
- **the resolver**, because it is the difference between *"the internet is
  gone"* and *"the internet is fine and you cannot look anything up"*, and
  those have different repairs.

Cost, measured end to end rather than added up. Peak RSS of one `--towersh
7008` session that buys a pc, cables it into the handoff, addresses it and
fetches `halbert.co.uk`:

    HEAD               93756 kB
    this              124016 kB      +30.3 MB

Two booted machines is 26 MB of that; the rest is the five extra nodes, their
ports and their sockets in a `Net` that was already preallocated. And on the
gates:

    --sitecheck   35.5s -> 43.0s   (71 towers)
    --netcheck     3.0s ->  3.2s
    --loadcheck  159.0s -> 177.4s
    --health       1.9s ->  2.0s   (does not build a tower at all)
    --building     3.5s ->  3.6s   (nor does this)

**And it is lazy.** The world beyond the handoff is built on the Net's first
`net_step()`, not in `site_new()`. A tower the generator makes and throws
away, and every seed `--building` walks, pays nothing. Seventy-one towers at
185 ms each would be thirteen seconds on `--sitecheck`; it cost seven and a
half, because most of those towers never put a frame on a wire.

## What is real, exactly

Not "real" as a claim. Real as in: the following are true and each is checked
by `--netcheck`, and each check fails without the thing it names.

- **A real OS.** `machine_install()` + `machine_boot()`, the same call
  `--health` makes twenty times. The shop runs init, rc, svcinit, netd, sshd,
  httpd, nft, cron, ntp, syslog and audit. `svc stop httpd` on it closes the
  shop and leaves the rest of the web alone.
- **Real services on real ports, read off its own disk.** The box is
  `netsite_pin()`ed to its node *before* it is booted, so netd reads
  `/etc/net/interfaces` and configures the card the world can see, and every
  listener `bind_services()` opens is opened on that node. Booting first and
  pinning afterwards — which is what the first draft did — put the ruleset and
  the sockets on a node in a different world, and the box came up filtering
  nothing on a network it was not on.
- **A real packet filter.** `/etc/nftables.conf` on the shop's disk, loaded at
  boot by `nft(8)` running *inside* the machine, which reaches the stack
  through `SYS_netctl` — the identical path a player's own `nft` takes on
  their own server. `net_dump_fw` on that node prints the rules and their
  match counts.
- **Real files on a real disk.** `/srv/www/`, one file per page.
  `netsite_www()` reads them through the machine's `Vfs`. Edit the file and
  the next fetch serves the edit; there is a check that does exactly that.

## The generated shop, kept generated

D40's guarantee was that the catalogue page is printed off `KIT[]` in
`core/site.c` at fetch time, so it cannot drift from what the counter charges.
Moving pages onto a disk is exactly the move that would quietly turn that back
into a snapshot.

**It did not, because the disk is laid down by the generator.** `shop_disk()`
walks `net_site_page()` for every page of `halbert.co.uk` and writes
`net_fetch()`'s output — which for `/catalogue` and `/discontinued` is
`gen_page()`, reading `site_kind_name/ports/price/port_mb` at that moment.
There is still exactly one copy of a price in this program and it is in
`KIT[]`.

`--mancheck` is unchanged at **75/75** and still calls `net_fetch()` directly,
which is right: it is checking the generator. What was missing is a check that
the file on the shop's disk is the generator's output and that the fetch over
the wire is the file. `--netcheck` has both now, plus one that reads every
`site_kind_price()` back out of the page that came off the machine.

## What was deliberately left in place for the hacking

Nothing here lets a player hack anything. Everything here is what they would
have to get past, and it is left in a state where getting past it is a real
act against a real system rather than a new feature:

- **The shop is running sshd**, on port 22, with the port dropped by a named
  rule in its own `/etc/nftables.conf` rather than by the chain policy. That
  distinction is not pedantry: netstack's `policy drop` disposes of what no
  rule named *and nothing is listening for*, so a policy alone left 22 open on
  a box running sshd, and the first version of this ruleset was a filter that
  looked shut and was not. `--netcheck` catches it.
- **The price list is a file**, `/srv/www/catalogue`, owned by no package,
  under the document root its own httpd config names. Somebody who gets a
  shell on that box and edits it changes what the shop shows. (It does not yet
  change what `site_order()` charges — `core/site.c` reads `KIT[]`. That gap
  is the hack, and it is deliberate: the interesting version is a player who
  makes the *page* lie and finds out the counter does not.)
- **The resolver's zone is a file**, `/etc/net/services`, replayed by netd at
  every reconfiguration. A record added there is a name that resolves.
- **The boxes are on their own segments behind a router**, so getting to them
  is a routing problem and not a wire.

## The escape route, which is the thing not to break

D40: *"if your core switch dies, you can wire up your main box to use the
uplink and use that to order — just takes being clever to get out of that
tight spot."* It worked because the shop was on the handoff. The shop is not
on the handoff any more, so it had to be played again, in full, on seed 7008:

    f0 MDF> cable box1 uplink
    link 0: box1:0 to uplink:0, 3 m of cat6 through the tray, 83 paid,
            the port comes up at 500 Mb.
    f0 MDF> addr box1 198.51.100.2/30
    f0 MDF> gw box1 198.51.100.1
    f0 MDF> resolver box1 198.51.100.1
    f0 MDF> plug box1
    root@box1# ip route
    198.51.100.0/30 dev eth0 scope link
    default via 198.51.100.1
    root@box1# links halbert.co.uk/catalogue
    Catalogue
    =========
    ...
    order a switch8 [order:switch8] -- to your goods in, in the box.

It still works, and it now works for a better reason: the name is resolved by
the ISP's resolver two hops away, and the page comes off a machine three hops
away over the rate-limited circuit. `--netcheck` builds that exact tower
(`isp_tower()`: `site_new`, `site_install`, `site_cable` into the handoff,
`site_addr` on the WAN /30) and asserts the fetch, so it cannot quietly stop
working.

## What `core/site.c` needs to do, and why it has not been done here

Another agent is in `core/site.c` today. Nothing was reached across for. One
line is wanted, and until it lands the world attaches itself.

**The line.** In `site_new()`, replace this block —

    net_dnsd(s->net, d->node);
    for (int i = 0; ; i++) { ... net_dns_record(...); net_if_alias(...); }
    net_httpd(s->net, d->node, 80);

— with

    net_isp_declare(s->net, d->node);
    net_httpd(s->net, d->node, 80);   /* the load generator, see below */

`net_isp_declare()` says "this is the node the customer's circuit lands on"
and nothing else; the world past it is built on the first tick. The zone, the
aliases and the name server all move out to `isp-ns` and are not site.c's
business any more. `net_httpd` on the handoff stays for now and the next
section says why.

**Until then, the bridge.** `net_step()` looks once, and only once, for a node
that is both a name server for this web *and* the holder of the web's own
addresses on its own card — which is precisely a handoff pretending to be the
internet, and is the only thing in this program that is both. It finds
`site_new()`'s uplink and nothing else; `netsite.c`'s break-fix world has its
zone on `ns1` and its addresses on `web`, two different nodes, and is not
matched. `isp_sniff()` is fifteen lines, it is commented as a bridge, and it
becomes dead the moment the line above lands.

**One other thing site.c could give.** D40 asked for `site_kind_for_sale()`
instead of `price > 0`. Still wanted, still not urgent.

## What was NOT done, with the measurement that stopped it

**A tenancy's day still gets its bytes from the handoff.** `core/siteday.c`
aims every office transfer, every studio upload and every hosting visitor at
the handoff's own address — `/n/2048`, an object of a given size, which is a
load generator and not a page. Pointing it at the real hosting box was tried,
and it works, and the cost is the difficulty curve:

    on the 500 Mb circuit scenario in --sitecheck
      office 100% -> 63%     studio 100% -> 2%

Two extra hops each way doubles the round trip and a windowed TCP transfer is
divided by the round trip. That is not a bug, it is the consequence the whole
record is about — but re-calibrating a tenancy's day is a different piece of
work from building the internet, and doing both at once means neither can be
measured. **The circuit is still crossed either way**: the handoff's port 0 is
the rate-limited one and every byte of a tenancy's day goes through it. What
is not crossed is the two hops.

The handles are in place for whoever does it: `net_isp_web_node()` and
`net_isp_web_addr()` return the hosting box and its public address, forcing
the world to be built if it has not been, and `siteday.c` carries the
measurement above in a comment where the change would go.

**The web is addressed in RFC1918 and it should not be.** Every page in
`net_sites.c` lives in `10.0.2.0/24` or `10.0.3.0/24`. A tower built the way
`--loadcheck` recommends — `10.0.0.0/16` — *contains* `10.0.2.20` in its own
connected subnet, so a desk asking for the wiki ARPs on its own floor and
never sends a packet. That collision is older than this record and this record
does not fix it; what it does is not make it worse, by giving the hosting box
a public address (`203.0.113.20`) for anything aimed at "the internet" rather
than at a name. The honest repair is to move the whole zone into documentation
space, and that is twenty-three addresses in `net_sites.c`, `/etc/hosts` in
`image.c`, every page that quotes an address, `mancheck.c`'s two literals, and
the break-fix game's own site network. It is a day's careful work and it is
worth doing.

**Calls still terminate at the handoff.** A call centre's RTP goes to the
uplink node, which is a carrier's kit at the demarcation point and is honest
enough. Moving it out would move the voice calibration, which is the same
argument as above.

**The ISP's boxes are not diagnosable from inside.** They are out of scope for
the player by design — no shell, no crash cart, not in `look`, not in `show`.
The only way to learn anything about them is over the network, which is the
point. It also means that if one of them ever fails to boot, nothing says so;
`--netcheck` asserting that the shop serves is what stands in for that.

**`net_release_host()` does not unplug a `wan0` cable.** The extra port lives
outside the node's contiguous run, and the release loop walks the run. Nothing
releases the handoff, so it cannot bite today. Written down because the next
person to use `net_wan_nic()` on something that *can* be released will find it.

## Evidence

    ./build/bf --netcheck     285/285   (was 262/262: +23)
    ./build/bf --mancheck      75/75
    ./build/bf --sitecheck    600/600
    ./build/bf --loadcheck     35/35
    ./build/bf --eventcheck    83/83
    ./build/bf --health        20/20
    ./build/bf --building     200/200
    ./build/bf --solve 60      60 repaired and 60 handed back
    ./build/bf --askcheck      clean
    tools/check-decoys.sh      37/37
    make test-cpu              clean

Two mutations, each built in a clean `git archive HEAD` checkout with this
change applied on top and then one thing broken:

    netsite_isp_build() made a no-op -- the web stays on the handoff
                                                       270/285   (15 fail)
    netsite_www() always falls through to net_fetch() -- the shop's
    pages come from the table again rather than off its disk
                                                       284/285   ( 1 fail)

The second is the one that matters, because it is the surgical one. The only
check it fails is *editing that file changes what the shop serves* -- exactly
the claim it broke and nothing else -- and that is the check the hacking will
stand on. Note what it does NOT fail: the page still has the right prices in
it, because a mutation that reads the same generator through a different door
should not, and a gate that failed there would be testing the door.

The twenty-three new assertions are in `check_isp()`. The ones worth naming:

- *not one page of the web is served off the wall socket any more* — a `GET /`
  to `198.51.100.1` is a 404. Against the old handoff it was the front page of
  the wiki.
- *the supplier is three hops away, not on the wall* — `net_traceroute` from a
  box on the WAN /30 returns exactly `198.51.100.1`, `198.51.100.6`,
  `10.0.2.73`. Against the old arrangement it returned one hop.
- *the catalogue is a file in the shop's own document root* — the bytes
  `net_http_get` brought back are `memcmp`-identical to `/srv/www/catalogue`
  in that machine's filesystem.
- *editing that file changes what the shop serves* — the check writes
  `closed for stocktaking` into the file and fetches it back over TCP. This is
  the one that proves the page is served rather than looked up, and it is the
  one the hacking will stand on.
- *stopping httpd on the supplier's box closes the shop* / *and the name still
  resolves, because the resolver is elsewhere* / *netd stopped on the ISP's
  resolver: no name resolves at all* / *and every address still works, which
  is a different repair* — four checks, two faults, and the point of the whole
  record: out there is now a place where two different things can be wrong and
  they look different.
- *the shop's own nftables ruleset drops ssh from outside* — a real
  `net_tcp_connect_wait` to port 22 that does not complete, against a box that
  is genuinely running sshd.
