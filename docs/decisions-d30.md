# D30. Four industries, four different questions

## The sentence this record exists for

A playtester reached day 62 and said what a tower full of one business costs:

> "Cable is a bill I paid with a rule, not a bill I sweated. I made the riser
> decision on floor 1 and then repeated it on floors 2 and 3 without thinking."

They were right, and the reason was in `site.h`. Every tenancy in the game was
the same business. They differed in how many desks they had, whether they
wanted a segment of their own and whether they wanted a server -- and every
desk did the identical thing: one page, three files, three concurrent, four
and a half megabytes, all of it DOWN. So a floor only ever asked one question,
"is there enough throughput", and the answer to that question on floor 1 was
the answer on floor 3.

David's words this hour: *"as the office grows, we use more bandwidth and
provide different types of services. webhosting, voice over IP, content
creators like streamers. Industries that require bandwidth that could pay a
rent... let's make the world feel alive and the economy actually makes sense
and be fun."*

## The four trades, and why each one is a different SHAPE and not a flavour

The test applied to every one of them: **does it change what the right build
is?** A trade that wants the same things as an office, more of them, is a
number. A trade that makes a decision you already made the wrong one is a
game.

    OFFICE   the baseline, unchanged. One page off the internet and three
             files off the nearest server, all at once, per desk. Bursty,
             throughput-shaped, tolerant of latency. Served on four fifths.

    VOICE    `net_voice_call` -- the netstack's own streams, landed the same
             morning by another agent -- one each way between the desk and
             the carrier on the far side of the handoff. 172 bytes every 20
             ms, which is a FIFTIETH of what one office desk pulls, so no
             amount of bandwidth will ever help them. What ruins the call is
             concealment: an audio frame with no sound to play, because the
             packet was lost or arrived after the de-jitter buffer had
             already played the silence where it should have gone. Two per
             cent of the audio, or a hundred and fifty milliseconds of
             one-way delay, and the call is not a call. Served on four
             fifths of their CALLS.

    WEBHOST  twenty-four visitors a busy period, at a quarter of a megabyte
             each, arriving FROM the handoff INTO their origin server. That
             is the one direction nothing in this tower has ever been asked
             to carry: it crosses the circuit, the router and the riser
             inwards. What they are buying is that it answers at all, so it
             is nineteen in twenty rather than four in five -- and their
             origin is the box standing in THEIR OWN ROOM, so a day it was
             off is a day they were gone rather than a day they were slow.

    STUDIO   two sustained uploads per suite, 2048 KB each, for the whole
             busy period, to an ingest on the far side of the circuit. All
             or nothing: a stream that arrives late is a dropped stream and
             there is no partial credit. And the suite still opens the
             project off a file server, because an edit bay does -- so a
             studio is the one trade in the building that wants a local
             server AND a big circuit at the same time.

Every byte of all of it goes through `core/netstack.c` as frames on ports.
There is no load model beside the stack, which is `siteday.c`'s own rule and
was not negotiable: a call is real UDP at a real rate, a stream is an
ordinary TCP connection to an ordinary listener, a visitor is an ordinary TCP
connection opened by the handoff. The ingest reads its socket every
millisecond for the same reason -- a receiver that did not would be
throttling the stream in this file rather than on the wire.

## The rent follows, and it is legible before the lease is signed

Rent was `area x rate`. It is `area x rate x what that trade pays`, and the
premium is one table in `site.c` beside the name and the sentence `demand`
prints, because this project has been bitten twice by one fact living in two
places.

    office   100%    throughput at nine, and patient
    voice    170%    no loss, no jitter. Not bandwidth
    webhost  240%    uptime, and reachable INWARDS
    studio   300%    sustained UPLOAD, all of it

`demand` now prints the trade on every row, what that trade will ask the
network for, and the rent -- before the day arrives. Under the table it says
what each trade's demand actually is, in the constants that enforce it rather
than in prose that can drift from them.

**And a web host's outage is billed differently from a slow morning**, which
is the piece of the economy that makes uptime a thing you can buy rather than
a thing you hope for. Everybody else's bad day costs the landlord the rent
they did not earn. A day a web host's origin answered NOTHING costs a day's
rent BACK, out of the account, on top of the rent that did not arrive. It is
the one bill in this game that a two hundred and twenty pound battery pays
for, and it is levied only after the fit-out window, because nobody credits a
service they have not started yet.

## `service` says why, in the tenancy's own terms

`done` used to read 12/20 and mean one thing. It now reads 12/20 and means
four, so it has to say which, and the trade is on the row:

    floor tenant  trade      desks   up  addr   done  worst  strikes rent/day files
        3      3  voice        16   16    16   0/16  3427ms       1      278  files <-
          16 of 16 calls broke up: 34.4% of the audio concealed, 7 ms one
          way, 554 us of jitter.
        2      2  web host     20   20    20   0/24  3016ms       1     1468  none
          their site answered NOTHING from the internet all day. 24 visitors,
          0 served.
          and 1468 of rent handed BACK: they were down, and their lease says
          what that costs.

A voice tenancy is not unhappy because transfers did not finish. `site_tenant_why`
is one function, and the numbers in it were all measured during the busy
period that just ended -- the concealment off `net_voice_stats`, the kilobytes
off what the ingest really read.

## Two floors that need different builds, played

`./build/bf --towersh 26`, from 60,000, no credit. Floor 1 lets to an office
on day 1, floor 2 to a web host on day 6, floor 3 to a call centre on day 10
and two more offices after it. The build is the same on all three floors,
which is exactly what the playtester said they did: a switch in the comms
cupboard, a gigabit riser home-run to the core, one file server in the
basement on a battery, and the web host's own machine carried into the web
host's own office.

Five tenancies, ninety-six desks, all served, on day 20. Then the ordinary
saving: the tower's files are all internal, almost nothing crosses the
handoff, and a hundred megabit circuit looks like plenty.

    day 21: 5 in, 4 served, 96/96 desks addressed, 303/352 transfers finished
            busiest port uplink:0 at 95%; 9928 of 2538379 frames dropped

      floor tenant  trade      desks   up  addr   done  worst  strikes
          1      1  office       20   20    20  71/80  3743ms       0
          2      2  web host     20   20    20  24/24  3260ms       0
          3      3  voice        16   16    16   0/16  3427ms       1
              16 of 16 calls broke up: 34.4% of the audio concealed, 7 ms
              one way, 554 us of jitter.
          3      4  office       20   20    20  76/80  3575ms       0
          3      5  office       20   20    20  72/80  3826ms       0

**Three offices shrugged it off and went on paying. One tenancy was
annihilated by it, and it is the one whose entire business is on that
circuit.** The offices never notice because their files never leave the
building; the calls live on the handoff and nowhere else. That is a decision
that belongs to one floor and not to the others, and there was no such
decision in this game a day ago. Putting the circuit back makes the calls
perfect again on the next day, at 16/16, with nothing else touched.

Then the world does its half. Day 23 is seed 26's mains failure:

    day 23:  ** the building lost mains power at 04:12 and had it back by 04:31.
             ** files was on a battery and stayed up.
             ** wsrv went down with the power and has not been switched back on.

    day 24: 6 in, 4 served, 317/352 transfers finished, 1337 taken, 84079 in hand
            1468 handed BACK to web hosts whose sites were down: their lease is uptime

      floor tenant  trade      desks   up  addr   done  worst  strikes rent/day
          1      1  office       20   20    20  80/80  2995ms       0      611
          2      2  web host     20   20    20   0/24  3016ms       1     1468
              their site answered NOTHING from the internet all day.
              and 1468 of rent handed BACK.
          3      3  voice        16   16    16  16/16  2642ms       0      278
          3      4  office       20   20    20  79/80  3525ms       0      227
          3      5  office       20   20    20  70/80  3867ms       0      221

The same blackout, the same building, the same battery decision made once and
not made twice. Every office is served. The balance goes DOWN on a day the
tower took 1,337 in rent, because the web host's lease is uptime and their
box was the one without a battery. The fix costs 220 and it is the same verb
it has always been.

## Where the trade comes from, and the stream it does NOT come from

The trade is drawn from the seed, weighted off the room: a flat is let to a
person and the person who fills a flat with network is a content creator; a
let floor is let to a company, and about a third of the companies who want a
rack in a building like this are not offices. Across forty-one seeds and 1,457
tenancies the split is 75% office, 19% studio, 3% voice, 3% web host. **The
office stays the common case on purpose**: it is what the whole difficulty
curve was calibrated against, and a tower where every other floor is exotic
would be a different game rather than a richer one.

**It is drawn from its own Rng, and that is not fastidiousness.** The first
version took the roll out of the same stream as `wants_server`, `own_segment`
and the letting queue's slippage term, which shifted every draw after it: seed
7008's schedule moved from days 1, 6, 11, 15, 20, 26, 31 to 1, 7, 13, 18, 23,
29, 33. That did not announce itself as a schedule shift. It announced itself
as **three unrelated blackout checks failing in `--eventcheck`**, in a file
this change never touched, because that gate cables "the tenancy on floor 2 on
day 6" and there was no longer one. A new fact about the world gets a new
stream.

Verified rather than believed, across seeds 7000-7040, 1,457 tenancies, by
diffing `demand` from a clean `git archive HEAD` checkout against this build:

    day, floor, tenant and drops: 0 differences in 1457 rows
    every office's rent:          identical to HEAD, to the pound
    every other trade's rent:     exactly HEAD's rent x the printed premium

## What was tried and taken out again

**A trade changing how many drops a tenancy asks for.** A web host as six
staff and a rack, a studio as eight suites. It reads well and it was wrong:
ports come off floor area because desks come off floor area, and shrinking
them moved every desk-count number in every other gate in the project for no
design gain. A hosting company with a big floor has a big floor. The
differences that matter are what those desks ASK FOR and what the tenancy
PAYS.

**A studio pulling no files at all**, on the grounds that their work is
upload. It broke `--eventcheck`'s blackout variety check for a reason worth
keeping: a box that moved no frames yesterday is dealt PF_CLEAN by design
(D28), so a floor server with only a studio behind it did no work, was dealt
nothing, and the "three different mornings" check had only two. The fix was
on the demand side, where it belonged, and it is better design than what it
replaced: an edit bay pulls its media off a server like everybody else.

**One upload of six megabits a second per suite.** Every stream in a
perfectly healthy tower landed a few kilobytes short, because one TCP flow
across this stack carries about fifteen megabits and no more -- window over
round trip -- so a single upload sized near that ceiling stops measuring the
network and starts measuring the ceiling. **This is exactly the mistake D25
recorded in the other direction** with `SITE_DESK_FILE_KB`, and the answer is
the same one: concurrency, not size. A suite pushes the stream and the archive
copy, and each of them is comfortably inside what one connection can carry.

**A port pair per call, which is not a detail.** The first version gave every
call in the tower the same RTP port on the carrier, so the second call could
not open a socket and twenty agents shared one phone. Every call has its own
pair now, which is what RTP does.

**A web host's origin identified by who owns the box.** It cannot be:
`site_move` says at length that nothing a player carries becomes the tenant's
by being set down, and it says so because a version that did confiscated a
switch a playtester carried into a let office. The origin is identified by the
ROOM it stands in, which is better anyway -- it is a decision the player makes
with their legs, and it is the one that decides whose uptime the site is on.

## The curve

`--loadcheck` is untouched and every assertion in it still holds. The desk
counts are identical to HEAD, because the drops are; what moved is what those
desks ask for.

    NAIVE      1 tenancy   20 desks    80/80   100%   core:1    22%
               4           78         285/294   96%   files:0   68%
               5           98         365/374   97%   files:0   75%
               7          136         357/510   70%   files:0   95%
               9          176         371/638   58%   files:0   97%

    PLANNED    1           20          80/80   100%   srv1:0    20%
               5           98         373/374   99%   srv2:0    47%
               9          176         610/638   95%   srv2:0    47%

Visibly working hard at four tenancies, which is two floors; fallen over at
seven, which is four. **The sentence the gate encodes -- slow around three
floors, outright break at five or so -- still holds, and it is if anything
better centred than it was**: at HEAD the naive build broke at exactly three
floors, which is the lowest number the gate will accept, so there was no
margin under it at all.

Where the shift comes from is worth naming, because it is the only place the
curve moved and it was not tuned. On this seed tenancy 2 is a call centre and
tenancy 5 is a studio, and both of them pull one file per desk rather than
three -- so the naive tower's binding port, the one gigabit card on the one
basement server, is asked for less. What they ask for instead is the circuit,
in both directions, and the planned build's nine-tenancy figure fell from 98%
to 95% for exactly that reason. Nothing about the calibration was adjusted to
produce either number.

## The gates

Every one of these was run after the change, on this tree.

    --sitecheck  420/420      --loadcheck   35/35
    --netcheck   240/240      --eventcheck  83/83
    --health      20/20       --mancheck    56/56
    --building  200/200       --solve 60    60 repaired, 60 handed back
    --askcheck  2850/2850     check-decoys  37/37
    test-cpu    PASS          asan --sitecheck  clean, 420/420
    tools/check_determinism.sh  PASS on all four of its claims

`--sitecheck` went from HEAD's 390 to 420: thirty new assertions in four
sections, on three of this generator's own buildings (7008 for the prices,
22 for the studio and the call centre, 23 for the web host -- chosen because
their letting queues put two trades on one floor inside two weeks, and the
gate finds the tenancies by trade rather than by index so that a change to the
generator makes it move rather than lie).

### Fails without it, measured twice

A literal `git archive HEAD` build of the new gate is impossible: the
assertions name a type HEAD does not have, so the file will not compile there.
Two falsifications were built instead, each in its own directory with this
change's name in the path, and each one is a real removal rather than a
comment.

**`d30-industries-stub` -- every tenancy is an office again**, one line in
`site.c`, everything else identical:

    395/401 site checks pass

Six of the new assertions fail and nineteen more never run at all, because
three of the four sections cannot find the tenancy they are about and say so.
Twenty-five of the thirty are gone.

**`d30-industries-sameshape` -- the trades keep their names, their prices and
their `demand` rows, and every one of them does exactly what an office does**:

    408/420 site checks pass

Twelve fail. That is the stronger number of the two, because it is the one
that separates the labels from the shape:

    the studio's work really went UP, off the handoff's own port
    cut the circuit to 100 Mb and the studio is ruined by it -- UPWARDS
    `service` says so in their own units -- streams, and the KB that went up
    what they are judged on is VISITORS -- exactly a day's worth of them
    and the web host is not slower, they are DOWN
    no other server in the building will answer for their site
    and a day down costs the landlord a day's rent BACK
    `service` says they answered nothing from the internet, in those words
    and says what was handed back, and why
    and the calls were real UDP through the stack, not a number beside it
    a hundred megabit riser to the basement ruins the CALLS
    and it is concealment that did it, measured on the streams

The first pass of the gate had four assertions that this second build passed
vacuously -- a 20-desk office on a 100 Mb circuit dies too, and the sentence
under a row is written from the trade's label. They were tightened until they
were about the shape: the studio's is now `up_kb < up_want_kb` with
`up_want_kb` checked against the constants, and the web host's is that they
are judged on exactly `SITE_WEB_HITS` units, which no number of transfers can
be.

## What was deliberately NOT done

**No difficulty constant, and no price multiplied to make anything hard.** The
rent premiums are what the trade pays, not a lever; the demand constants are
what that business really moves in four seconds; the difference between a
floor that works and one that does not is still entirely where the frames go.

**`--loadcheck` was not adjusted.** It was the thing most at risk and it is
byte-for-byte the same file. Everything above about the curve was measured on
it unchanged.

**The voice call is one-way per stream and there are two of them.** The
netstack holds `NET_VOICE_MAX` = 128 concurrent streams, so this tower seats
sixty-four simultaneous calls. Two voice tenancies of twenty desks is eighty
streams and fits; four would not. A call the world cannot seat is not counted
as a call that went badly -- the same treatment a socket the pool could not
give out gets -- but the ceiling is real and it is named here rather than left
for somebody to find. `core/netstack.h` is another agent's file.

**A studio never fills a 500 Mb circuit on its own.** Twenty suites is eighty
megabytes up per busy period, about a hundred and sixty megabits, and the
tower's six studios together with three hundred desks' worth of page fetches
is roughly what a 500 Mb circuit carries. So the circuit decision is an
endgame decision at the default size and an immediate one the moment anybody
economises, which is what the played transcript above is. Whether that is the
right place for it is a playtest question and not a tuning question.

**Nothing was done about the socket pool.** Four transfers a desk with an end
at each side is eight sockets per office desk, and `NET_SOCK_MAX` is 2000, so
a fully let tower of three hundred and fifty office drops would exhaust it.
That is a pre-existing ceiling and this change relieves it slightly -- a voice
desk uses two sockets and a studio suite five -- but it is still there, and a
connection that cannot be opened because the world is out of sockets is a
bottleneck nobody built and nobody can see.
