# D43. Ten answers to ten questions, and a decision for the first twenty days

## Where this came from

A blind playtester played nine sessions to day 70, fifteen tenancies, and
liked the game. Everything in the first half of this record is something they
could **prove false with another command in the same session** -- this
project's cardinal sin and its recurring one, which D39 wrote down as a rule:
*when a number appears twice, one of the two is eventually wrong; the fix is
to make there be one.*

The second half is the other thing they said, which is about the shape of the
game rather than about its reports:

> Days 1-20 are too easy and slightly boring... money accumulates... there is
> no pressure at all in the first three tenancies; the build is mechanical and
> there is no decision in it.

David's answer to that, in his words:

> Early game decisions should be whether to use low quality, medium quality,
> or high quality gear budgeting while doing the trade-off stuff. How well
> will this work later? As the game progresses, we should have to upgrade
> those early items. I suspect the default gear given to the player is too
> much. You should start with basic server, basic uplink, and a switch with a
> few ports. Just enough to get off the ground, not enough to keep the whole
> system running until day thirty.

## Part one: ten facts with two answers

### 1. A switch that was full and empty at the same time

    > order switch24 core
    > show
      core   switch24  f0 goods in #12    24/24 ports used
    > show core
    port 0  admin down    ...    (all twenty-four the same)

`site_dump()` counted ports whose **netstack state** was not `NOCABLE`, and an
unplugged switch has every port administratively down rather than unoccupied.
So a switch24 still in its box read `24/24` on the summary page, `show core`
under it printed twenty-four empty ports, and the number never moved as the
player cabled.

This is the identical bug that was found in `look` and fixed there -- the
comment above `site_port_used()` in `core/site.h` describes it in the same
words -- and it survived here **because there were two copies of the count**.
There is one now: `site_ports_used()`, off the site's own link table, which is
what `serve`, `cable` and `look` already walked.

The gate does not assert the string. It cables the box and asserts that the
summary moves with the leads: `0/24` off the pallet, `2/24` with two runs in,
and `show core` saying `22 free for a lead` about the same box in the same
second.

### 2. A socket that was advertised and did not exist

    > show pc1
    pc1: pc in f2 office #64, 1 socket
    1 more socket on the back of it, with nothing in it

Both lines are true. netstack's "N **more** sockets" means *the rest of the
ones I did not list*; the reader added them, concluded the box had two, and
spent a session trying to hang a router off `uplink:1`. The handoff has one
port and has never had two -- D42's `net_wan_nic()` deliberately allocates
outside the node's contiguous run precisely so the player still sees one.

The header now carries the whole fact and closes the arithmetic:

    uplink: uplink in f0 MDF #22, 1 socket, numbered 0 to 0, 0 free for a lead

**Numbered** is the fix. The question the player was really asking was *is
there a port 1*, and no wording about counts answers it as directly as the
range does. The free count is `site_ports_spare()`, which steps over a port a
jack holds for good exactly as `cable` and `serve` do -- so a core switch with
a jack on it says 21 free rather than 22, and the gate cables one to prove it.

**Not done, and it is in somebody else's file:** netstack's own trailing
sentence still counts a jack-held port as a socket with nothing in it. It is
in `dump_ports()` in `core/netstack.c` and this change did not own that file.
The site-level jack lines under it still say the port is held for good, so the
page is complete; the count in that one sentence is off by the jacks.

### 3. An error about subnets from a command that takes megabits

    > isp 0    -> refused: that is the network or broadcast address of its
                  own subnet, not a machine's

`site_isp()` reached for `SITE_EADDR` because nothing else fitted. `SITE_EMBIT`
fits, and says the circuit is a number of megabits and the smallest the ISP
sells is ten.

### 4. `vlan` accepted 99999 while `trunk` refused 4095

One rule, two answers, and **the permissive one was the one that touched the
switch**. `site_port_trunk()` has always checked `1..VLAN_ID_MAX`; the access
half never did, so `vlan s8 0 99999` answered `set` about a port netstack had
quietly disposed of. Both verbs now refuse in the same sentence, and the gate
asserts the port was not changed behind the refusal.

### 5. `move` silently picked a room

`help` offers `f2.office` as a room name. Floor two has a dozen offices
belonging to three tenants. `bld_find()` returns the lowest-numbered one and
says nothing, so the box went into a room somebody else is paying for with no
sign a choice had been made.

**The shorthand is not being taken away.** It is how a whole tower gets built
without ever printing a floor plan, and it is unambiguous for `f1.comms`,
`f0.mdf` and every other equipment room. What it owed the player is the fact
that this time it was a choice:

    NOTE: `f2.office` matches 9 rooms on floor 2 and the game picked one:
    #57, the lowest-numbered, which tenant 2 is leasing. The others are #58
    (tenant 2), #59 ... `rooms 2` lists the floor; `#<n>` names one for certain.

Refusing was the other candidate and was rejected for a measured reason:
`move pc1 f2.office` is a line in `--sitecheck`'s own shell script and in the
scripted builds, exactly as D39 found for `serve` with no vlan. A verb that
refused it would unbuild the scripts in files this change does not own, to
serve a case where **saying which one is enough** -- the player who reads the
note fixes it with the next line, and the player who meant any office loses
nothing. The gate asserts both halves: the note on the ambiguous name, and
**no note at all** on `f1.comms`, because a note on every move would be the
other failure.

`site_room_name_matches()` and `site_room_ambiguity()` are in `site.h` so that
anything else taking the shorthand can say the same thing about the same
choice. `go` in `core/session.c` takes it and does not; that file was not
ours.

### 6. `rooms f2` printed floor 0

`atoi("f2")` is 0. Every other verb in the game takes `f2.something`, so
`rooms f3` looks like it must work, and it silently answered about the ground
floor. `rooms 2` and `rooms f2` are one line now; a floor that does not exist
is refused with the range; and a room name is refused and sent to the verbs
that take one. **Silently answering about somewhere else is worse than
refusing**, which is the whole of why this one was worth a code change rather
than a help text.

### 7. The DHCP pool cap, delivered by the wrong half of its own message

    > dhcpd rt 10.9.0.50 180 24 10.9.0.1 10.9.0.1
    a pool of no addresses serves nobody, and a box holds eight pools at most

The pool had 180 addresses. One error code carried two facts and led with the
one the player could disprove by re-reading the line they had just typed --
which is worse than saying nothing, because it teaches them the message is
not about them. `SITE_EPOOL` and `SITE_EPOOLS` now, each saying only what
happened. The cap is in the help page, in the `dhcpd` usage, and in `dhcpd
<box>`, which prints how many of the eight are in use.

### 8. `serve` before move-in day said the wrong thing

Tenant 3 arrives on day 11. On day 6:

    > serve 3 sw2b cat5e 30
    refused: no such device

There is no device in that command. Meanwhile `serve 99` -- a tenancy that
will **never** exist -- got a sentence that named the right verb. The good
message existed and the case a player actually hits did not use it, which is
the most annoying shape a bug of this kind takes.

`site_serve_vlan()` refuses with `SITE_ENOTIN` now, and the shell answers with
the calendar, because the day is what the player needs and the shell is where
the day is in scope:

    tenancy 3 has not moved in yet: they take the lease on day 11 and it is
    day 6. Their desks are not in the room to cable to. `demand` says who is
    coming and when; `day` gets there.

### 9. The game advertised two commands that do not exist

`service` printed, every time:

    `sit` at one of their desks and run `voice` for which port threw it away.

There is no `sit` and no `voice` in the tower shell. They are Session verbs,
in `core/session.c`. **This is the one failure the README calls fatal** -- *a
joke that names a command the OS does not have teaches the player to distrust
everything else* -- and it was doing it for the hardest problem in the game.

The half that was ours: the advice now leads with the verb this shell really
has, and names the other two as somebody else's rather than pretending either
that they do not exist or that they are here.

    From HERE the port is `load`, and `show <box>` prints how many frames it
    threw away and which of the four reasons it was. `sit` and `voice` are
    verbs of the SESSION and not of this shell: they are how you read the
    same fault off the tenant's own machine, and `tower` is the word that
    comes back here from there.

The gate proves both ends: it asserts `site_cmd` does **not** understand `sit`
or `voice`, that it does understand `load`, and that the sentence says so.

**What the right whole answer is**, since the brief asked. The sentence is
now true and it is still a seam. `core/session.c` already has
`tower_verb_note()`, which catches a TOWER verb typed at a machine's shell and
says where it lives -- the exact mirror of this problem, solved once, in one
direction. The whole answer is the other direction: a `SESSIONVERB[]` beside
`TOWERVERB[]`, so that typing `sit` at a tower prompt inside a Session says
*"`sit` is a verb of the chair; you are standing in the building"* instead of
"no such command", and typing it at `--towersh`, which has no chair at all,
says *"there is no chair in this shell -- `--serve` or `--desk` gives you
one"*. That is fifteen lines in a file this change did not own, it needs no
new verb, and it would let the advice go back to naming the tool that is
actually best for the job. Until it exists, naming `load` first is the honest
ordering, because `load` is the one the reader can type.

### 10. The legend drowned the signal

`service` and `load` each printed about thirty-five lines of legend on every
call. By day 60 the legend was ninety per cent of the page, and the day-31
disaster's four `**` lines -- the only place the world tells you what it did
to your kit -- were nearly lost in it. The playtester asked for the legend
behind something like `service ?`.

There is a real tension here with the standing rule that a message must be
honest **and complete**, and moving text is how completeness usually gets
lost. So the split is not "shorten it":

- **the short page keeps every number that is a MEASUREMENT of this building
  today**, plus the one instruction that changes what the player does with the
  rows. `service` keeps the complaint threshold, because that is the number
  being counted against and it moves as the building fills. `load` keeps
  *READ THE DROPS AND THE PEAK QUEUE, not busy*, because a reader who skips it
  misreads every row.
- **the legend keeps the sentences that explain what a column MEANS**, which
  are the same on day 1 and day 60 and are therefore the part worth reading
  once.

`service` is 4 lines now against 39. `load` is 12 against 35. The gate counts
those lines, and then asserts the full text **sentence by sentence** through
`service ?` and `load ?` -- eight named sentences for `service`, three for
`load` -- so nothing can quietly go missing. Six existing checks that used to
read legend strings out of `site_dump_service()` now ask the shell for
`service ?`, so the path a player types is the path the gate walks.

### Evidence for part one

Measured in clean `git archive HEAD` checkouts, never a copy of a working
tree. The reverted tree carries **all forty-four new checks** plus every new
symbol they call, with only the behaviour put back:

    HEAD, clean                                   610/610 site checks pass
    HEAD + the 44 new checks, fixes reverted      628/654 -- 26 FAIL
    HEAD + the 44 new checks + this change        654/654

The twenty-six are: two for the port count, three for the socket header, two
for `isp`, two for `vlan`, four for `rooms`, two for the pool errors, two for
the legends, two for the ambiguous room, four for the tenancy with a date, and
three for the two verbs that do not exist.

## Part two: three grades, and why they are not a quality number

### The rule this had to obey

The difference between grades has to be a **measured consequence**, never a
difficulty constant. Nothing anywhere multiplies anything by a grade. A cheap
switch is not *worse*; it is four sockets that clock a hundred megabits, and
everything that follows -- a queue that fills on a burst a gigabit port rides
out, drops the port counters count and `show <box>` gives the reason for in
words, a floor that outgrows four holes -- follows because netstack is doing
arithmetic with the number on the box.

### The catalogue

    what        sockets   each socket    price   disk rated   battery
    switch4           4        100 Mb       45           --        --
    switch8           8       1000 Mb      120           --        --
    switch24         24       1000 Mb      400           --        --
                             ports 22-23 at 10000 Mb
    router            4      10000 Mb      650           --        --
    pc                1       1000 Mb      480      60 days        --
    minitower         1        100 Mb      460      30 days        --
    server            2       1000 Mb     1350      60 days        --
    rackserver        2      10000 Mb     3400     120 days       yes

That table is printed off `KIT[]` at fetch time by `halbert.co.uk/catalogue`,
which is D40's guarantee and still holds: there is exactly one copy of a price
in this program.

### The axes, and why each one is honest

**Port speed** is the durability axis, and it is the one that does the work.
The egress buffer is the same 48 KB on every port in this game -- it is
`NET_PORT_BUFFER` and it is a compile-time constant in `core/netstack.c`,
which this change did not own -- so a 100 Mb port **drains that buffer ten
times more slowly** than a gigabit one. That is the whole mechanism, and
nobody wrote "the cheap switch is worse" anywhere.

**Port count** is the limit that bites first, and it is what David asked for
by name. A switch4 with its riser in port 0 has three holes; `serve` fills
them, stops with `SITE_ENOPORT`, and the fix is the bigger box rather than a
bigger cheque.

**Disk life** was two constants -- `WEAR_WARN 45`, `WEAR_FAIL 60` -- and every
disk in the building was rated the same, so "buy the cheap server" cost
nothing any instrument could show. It is `site_kind_disk_days()` now: 30 for a
minitower, 60 for a server, 120 for a rack server, with the SMART warning
still at three quarters of the life. It is a **rating and not a countdown**:
wear is added from how hard the box's own port worked that day, so the same
disk lasts a different number of days in two buildings. `events` prints a
`rated` column beside the percentage, because a minitower and a rack server
both reading 50% are fifteen days apart and the page used to say the same
thing about both.

**A battery.** The rack server arrives with one; on anything else `ups <box>`
fits one afterwards for 220. Same flag, same behaviour, and the difference is
that one of them is in the price on the catalogue page and the other is a
decision you can still get wrong on the morning of day twenty-six.

### The measurement that makes it real

Two towers, seed 22, the same building, the same tenancy, the same seven
desks, the same day's work offered, **differing in one purchase**: the box the
riser lands in. Both are built the way the README says a player really builds
-- a second switch daisy-chained off the first when the floor fills up -- so
every frame of those seven desks crosses the one port that differs.

    switch4 riser  100 Mb:  26/28 done,  3535 ms worst,  727 frames lost
    switch8 riser 1000 Mb:  28/28 done,  1752 ms worst,    0 frames lost

Two transfers that did not finish inside the busy period, twice the wall time,
and seven hundred frames thrown away. **And the drops are on the other end of
the riser**, on `core:1` -- the core's port is what has to clock those frames
out into a link the cheap box negotiated down -- which is not a detail, it is
the model: `load` names the port, `show core` says `egress buffer full`, and
the player has to follow the cable. That is the same act the README stakes the
whole difficulty model on.

The gate asserts the *arithmetic and the reason*, not the numbers: that both
towers were offered identical work, that the cheap port really came up at 100,
that the cheap tower finished fewer, that the drops are attributed to a buffer
that overran, and that `load` names the port.

### Falsified

Two mutations, each in a clean `git archive HEAD` checkout with this change on
top and then one thing broken:

    switch4 given 1000 Mb ports -- a grade that is only a lower price
    minitower and rackserver given the server's disk rating -- one
    constant for every box, as it was
                                                        663/675 -- 9 FAIL

Note which nine: both "they differ in..." rows, the port speed read off the
wire, the work not done, the reason on the port, the port `load` named, the
`rated` column, which disk logs first, and the rating-not-a-countdown check.
Note what does **not** fail: the three grades still exist, are still priced in
order, still have their port counts, and `order` still prints a spec. A
mutation that leaves the shape and removes the consequence should fail exactly
the checks about the consequence, and it does.

### The upgrade path is the game's existing physical one

There is no `upgrade` verb and there is not going to be one -- it is the shape
of interface this project has refused since D25. The path is: order the better
box, it lands in goods in, carry it up, cable it, address it, move the service
onto it. The gate asserts `upgrade` is not a verb and that the better box
really arrives in goods in with the sockets the catalogue sold.

**And the copper is charged again**, because it is. A player who put a
switch4 on floor three off the spool pays the whole riser a second time for
the switch8 that replaces it -- which is D23's addendum on the jack arriving
from the other direction, and is the first time in this game that the jack's
permanence has an early-game reason to exist.

### Where the money is said

`order` prints the grade at the moment the money leaves -- what each socket
clocks, what the disk is rated for, whether a battery came with it -- which is
where D27 put the negotiated port speed and where the jack prints its
break-even, for the same reason: a spec a player has to go and look up
afterwards is not a decision they made.

## What was NOT done, and what it costs

**The opening balance was not changed, because it is not in these files.**
`60000` is a literal in `core/bfmain.c` (twice) and `core/session.c`, none of
which this change owned. The arithmetic, so whoever does own them has it:

    the cheapest tower that works        switch4 + minitower       505
    the middling one                     switch8 + server         1470
    a planned four-floor tower           router + core switch24 +
                                         4 x switch24 + 4 x server   8050

plus copper and the circuit. **60,000 is roughly seven planned towers**, and
until it comes down the grades are a decision the player is free to decline by
buying the dear one every time. The recommendation is to open at something
between 3,000 and 5,000 -- enough for a switch, a server, a router and the
copper for one floor, with the second floor having to come out of rent. What
must be checked when it changes: `--loadcheck` calls `site_credit()` for its
own budget and will not notice, so it will keep passing while the game becomes
unwinnable. The thing to run is a played tower, not a gate.

**`NET_PORT_BUFFER` is still one constant for every port**, so the buffer
depth axis the brief named first is not among the four used here. Making it a
property of the box is the right end state -- a cheap switch with a shallow
buffer drops where a dear one with a deep one does not, at the same speed --
and it is a change to `core/netstack.c` and `core/netstack.h`, which this
change did not own. What it would buy: a grade difference at the SAME port
speed, which is the only way to make `switch8` and `switch24` differ in
anything but holes. Today they do not, and that is the honest gap in the
middle of the range.

**The 3D view does not know the three new kinds.** `game/scripts/tower.gd` has
`DEV_U` and `DEV_COL` keyed by kind name, and `switch4`, `minitower` and
`rackserver` are not in either. Three entries each; it is additive and that
file was another agent's this hour.

**`core/session.c` has a second copy of the `order` reply**, at line 3683, and
it is the one a player of `--towersh` and the 3D window actually sees -- so
the grade line this record added prints in the raw site shell and in the gates
and not in the game. That is a fact in two places, which is the thing this
whole record is about, and it is named here rather than left to be found. The
fix is for that branch to call `site_cmd` or to print `site_kind_port_mb`,
`site_kind_disk_days` and `site_kind_has_ups` the way `core/site.c` now does.

**`--loadcheck` still builds with `switch24` and `server`**, deliberately.
Both curves are unmoved to the frame -- naive falls over at 7 tenancies (4
floors), planned carries all 9 -- because the calibration measures **where the
frames go**, and a calibration that changed because the kit changed would stop
measuring that. The honest next measurement is a third curve: the same nine
tenancies built on the cheap grade, which should fall over sooner than the
naive one does, for a reason that is a purchase rather than a topology. That
is a scenario in `core/loadcheck.c`, which was out of scope.

**Nothing was done about the late-game ceiling.** The same playtest: past
roughly 250-300 desks the building hits a ceiling rather than a decision --
one router, four gigabit ports, no second WAN handoff, no way to prioritise a
vlan for the voice tenants. A **high grade of router is exactly where that
goes**, and the axes are already sitting here: more sockets than four, and a
queue discipline per vlan, which is the one thing on this list that the voice
trade would pay 170% for and that no amount of bandwidth substitutes for. It
would want a `net_port_priority()` in netstack and a `priority <sw> <port>
<vlan>` verb, and it should not be built until somebody has played to the
ceiling and can say which of the two hurts first.

**`worst` is still a wall time**, as D39 left it. Unchanged and still
explained rather than fixed.

## Gates

    --sitecheck   675/675    (610 at HEAD: +44 for part one, +21 for part two)
    --netcheck    285/285
    --loadcheck    35/35     both curves identical to HEAD, frame for frame
    --eventcheck   83/83
    --mancheck     85/85     (79 at HEAD: +6, the two new kinds for sale
                              being checked against the generated shop page)
    --health       20/20
    --building     12/12
    --askcheck     2844/2844
    --solve 60     60 repaired, 60 handed back
    check-decoys   37/37
    make test-cpu  40 agreed with qemu, 0 diverged; all four cpu gates pass

Two numbers moved and both are additions rather than changes: `--sitecheck`
by the sixty-five new assertions, and `--mancheck` by six, because
`minitower` and `rackserver` are for sale and that gate checks every kind the
catalogue sells against the page the shop's own httpd serves off its own disk.
Nothing that passed at HEAD stopped passing.
