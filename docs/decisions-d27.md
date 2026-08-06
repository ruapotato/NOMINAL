# D27. The opening hour becomes a sequence of decisions, and copper costs

## What two playtests measured

**The run opened with a forty-eight day dead zone.** On the gate seed the
first tenancy arrived on **day 49**. A playtester who reached day 85 had the
whole tower built by day 25 and then pressed `day` twenty-four times into an
empty building:

> "The only things that happened were disk wear and a mains failure -- both
> of which *punished me for building early*. With this seed the optimal play
> is to build everything up front and then switch it off, which is the
> opposite of 'demand outgrows the infrastructure'."

**Planning was rewarded but never expensive.** The same run finished with
**94,087 in hand having spent 28,927**, at 23% of a 500 Mb circuit, with
cable at 22% of spend and about 7% of the closing balance:

> "I never once had to choose the cheaper run. Planning wins because the
> naive build is punished, not because the planned build is hard to afford."

And they named why the cable grade never mattered:

> "Port speed is decided by cable grade, and cat6 gives 10 Gb ports at these
> distances. That makes the desk-cable choice feel free."

Independently, an agent doing a visual pass ran **200+ days with seven
tenancies moved in and unserved and never drew a single complaint**, and had
to go overdrawn on purpose to photograph the run-ending alert. All the
pressure in the game was money; none of it was service.

Three separate faults, and each one has a place in the code.

## 1. The schedule was a draw. It is a letting queue now.

The old line, in `site_start`:

    t->day = rng_range(&r, 1, 40) + rm->floor * 12;

Twelve days a floor means a tower whose lowest let floor is the fourth
cannot have a tenant before day forty-nine, and the gate seed is exactly
that. And independent draws bunch: two tenancies land together and then
nothing for a fortnight, so the rate a player feels is noise.

A letting agent does not work like that. Floors let from the bottom,
viewings are booked back to back, and the next lease is signed as the last
one completes. So the schedule is a **queue**: sort by floor, first lease
inside the first few days, and the gap to the next one is how long the last
one takes to fit out -- a day, plus a day per six desks, plus nought to two
days of slippage. A one-drop studio is signed a day or two after the last; a
twenty-desk office buys the player four to six days, which is less than a
tidy build-out of twenty desks takes. **The queue is always slightly ahead
of the player**, which is the loop, and every term in it is this seed's own
building rather than a difficulty constant.

Measured across nine seeds, `demand` before and after:

    seed   tenancies   first tenancy      in by day 30      in by day 60
                       before  after     before  after     before  after
       1          83      34      3           0     12         12     24
       2          12      16      2           3      7          7     12
       3          17      22      1           1      6         10     17
       7          23      32      2           0      6          6     13
      11          23      32      1           0     12         10     23
      42          11      20      1           3      6          6     11
      99          63      13      3           2      9         12     23
    1234          26      33      2           0      7          7     13
    7008          36      19      1           1      6          5     13

The first tenancy is now in on day 1, 2 or 3 on every seed tried. Seed 2's
whole schedule reads: days 2, 7, 11, 16, 19, 23, 28, 32, 37, 43, 49, 55 --
a twenty-desk office every four to six days, for two months.

## 2. A tenancy nobody cabled was waiting patiently. It is not any more.

`siteday.c` said, in as many words: *"Strikes only start once somebody has
been connected -- so a complaint is always about service that got worse,
which is what the player can actually be held to."* It sounds careful. It
made the entire service half of the game optional: that is why an agent
could run two hundred days with seven unserved tenancies and no complaint.

A tenancy that has moved in has signed a lease and the desks are in the
room. Nobody promised them a slow network, and nobody promised them no
network either, and of the two the second is worse. So they get
`SITE_FITOUT_DAYS` -- three days of unpacking -- and after that a day with
not one desk able to work is a strike like any other. Three strikes is a
complaint, so **ignoring a tenancy costs a complaint on the sixth day after
they move in**, and three ignored tenancies end the run exactly as three
badly served ones do.

Combined with (1), that is the pressure: leases arrive every four to six
days and each one starts a six-day clock.

## 3. Ten gigabit was conjured by the cable. Now it has to be bought.

A link runs at the slowest of three things -- the port at each end, and the
copper between. Until now **only the cable had a say**, so a cat 6 patch
lead to a desk negotiated ten gigabit because the run was under fifty-five
metres. The playtester was right that this makes the grade free.

The catalogue now says what the sockets on the back of each box will clock,
and `site_install` sets it; netstack already takes the minimum of port rate
and cable, so this can only ever slow a link down:

    desk, pc, server, switch8        1 Gb on every port
    switch24                         1 Gb on 0..21, 10 Gb on 22 and 23 (SFP+)
    router                           10 Gb on all four

The consequences are the decision. Cat 6 into a desk is money burnt, and the
game says so at the moment the money leaves: `link 3: sw1:2 to t7d0:0, 22 m
of cat6, 124 paid, the port comes up at 1000 Mb, which is what port 0 of
t7d0 does whatever you plug into it.` Ten gigabit exists only between a
router and a core switch's uplink pair -- and there, cat 6 gets it for a
third of fibre's price right up until the run passes fifty-five metres and
quietly becomes a gigabit. That is a cheap-grade decision that bites at real
distances, which is what was asked for.

## 4. Cable was priced as a drum. It is priced as a job now.

The old table charged 38 per hundred metres of cat5e and 12 for the ends. A
twenty metre desk drop was **20**, against a tenancy paying six thousand a
month. That is why nobody ever chose the cheaper run.

In a real building the drum is the cheap part; what costs is the person
pulling, terminating, punching down and testing, and that lands **per run**.
So `ends` is now the larger half of a desk drop, and -- this is the point --
**the labour is the same whatever is on the drum**, so the three copper
grades have nearly the same ends and differ almost entirely by the metre:

              per 100 m   ends      20 m drop   80 m riser
    cat5             20     70          74            86
    cat5e            55     75          86           119
    cat6             95     80          99           156
    fibre           260    340         392           548

Read the two right-hand columns, because they are the whole design. On a
desk drop the cheapest copper saves a seventh, which is not worth the risk
and **should not be**: a desk pulls nine megabits and a hundred megabit drop
carries that fine, so a game that punished cat 5 at a desk would be lying.
On an eighty metre riser it saves nearly a third -- and that is the one run
where a hundred megabit takes a floor of desks with it. Cheap copper is a
temptation exactly where it is a mistake and free where it is not.

**And cat 5 is buyable at last.** `CAB_CAT5` -- a hundred megabit, for good
-- has been in the catalogue and in the netstack since the pivot, and
`kind_arg` in `session.c` refused the word, so no player could ever buy the
one genuinely regrettable drum in the game. `--loadcheck` has asserted since
D25 that a hundred megabit run under two floors of desks fills to 97%. The
game named the sin and did not sell it.

## What it costs, played

`--towersh`, from 60,000, no credit, a competent build: a vlan per floor on
a subinterface of the router, a switch per floor home-run to the core switch's
uplink port, a server in each floor's cupboard doing that floor's DHCP and
holding its files, cat5e to the desks, fibre up the risers.

    seed 2       day 30: 7 tenancies, 126 desks, all served
                         27,247 spent, 51,966 in hand, 19,213 taken in rent
                 day 60: 12 tenancies (the whole tower), 226 desks, all served
                         51,868 spent, 104,697 in hand, 96,565 taken in rent
                         cable 15,875 over 138 runs -- 31% of spend

    seed 7008    day 30: 6 tenancies, 118 desks, all served
                         26,587 spent, 73,928 in hand, 40,515 taken in rent
                 day 60: 13 tenancies, 256 desks, all served
                         58,090 spent, 142,250 in hand, 140,340 taken in rent
                         cable 15,179 over 129 runs -- 26% of spend

**The same build, the same seed, on a clean HEAD checkout**, so the before
and after differ only in this change:

                          HEAD, day 60        D27, day 60
    tenancies served               7                  12
    desks                        126                 226
    spent                     18,806              51,868
    of which cable             2,016              15,875
    cable, % of spend           10.7%               30.6%
    cable, % of balance          2.6%               15.2%
    in hand                   77,451             104,697

Sixty days of HEAD is what sixty days of D27 gets to by day thirty. The
playtester's own figures -- 94,087 in hand having spent 28,927 at day 85,
cable 22% of spend and 7% of the balance -- sit alongside those in the same
place.

**And the cheap riser, played.** The same seed, the same build, the same
schedule, with cat 5 up the risers instead of fibre. It saved 2,787 on eight
runs, and:

    day 22. 5 tenancies in, 2 of them served. 86 of 86 desks addressed.
    126 of 344 transfers finished. busiest port sw1:23, clocking 99%.
    THE RUN IS OVER: three tenancies have filed a complaint.

That is the whole brief in one transcript: a wrong answer you can afford to
make, that refunds nothing, and that comes back for you two weeks later on a
port the game names.

**Where the pressure actually landed, and it is honest to say so.** It is
the first month. On seed 2 a competent player is 27,247 down out of a 60,000
float by day 30 with only 19,213 of rent in -- two thirds of their capital
gone, four switches, three servers and a hundred and twenty-six drops
bought, with five more tenancies queued behind. **A fully let tower still
prints money**: by day 60 the balance is higher than HEAD's, because twelve
tenancies are paying instead of seven. That is the loop working as the owner
described it, not a failure to make the game hard, and the fix if it ever
needs one is more tower rather than a bigger price.

## What the play found that no gate would have

Seed 42's run **ended on day 20**, and the cause is worth the whole exercise.
The script put a switch and a server in the same fifteen square metre comms
cupboard on floor two. On day 17 `srv2` shut itself down on temperature --
the heat rule, doing its job. With the floor's server down, floor two's file
traffic fell back to the internet, the 500 Mb circuit went to 98% busy, and
**two tenancies filed on day 19 and two more on day 20**.

Before D27 that same downed server cost nothing: D25's own addendum records
a tenancy served 36-40/40 for the twelve days its file server was switched
off. Now one heat trip on a floor server ends a run in three days. That is
the difference between a world that has events in it and a world where the
events matter.

## The gates, and the two scenarios that were rewritten rather than weakened

Every assertion in `--loadcheck` is untouched and the curve still holds. The
tenancy ordering changed (the queue lets from the bottom), so the same nine
steps land on slightly different floors, and the naive build is if anything
harsher than before:

    NAIVE      1 tenancy   20 desks   80/80    100%   core:1     22%
               3           58         232/232   99%   files:0    60%
               5           98         262/392   66%   files:0    95%
               9          176         192/704   27%   files:0    98%

    PLANNED    1           20          80/80   100%   srv1:0     20%
               5           98         390/392   99%   srv2:0     60%
               9          176         696/704   98%   srv2:0     60%

Comfortable on its first floor, visibly working hard by the third, fallen
over at five tenancies -- which is the sentence `--loadcheck` was built to
check, and it checks it unchanged. **The planned build's fibre risers now
run at a gigabit**, because they land on a floor switch's access port, and
nothing about the table moved: the binding port in a planned tower is the
floor server's own gigabit card at 60%, not the riser. That is worth writing
down, because it means fibre in this tower is bought for distance and for
the core trunk, not for riser bandwidth nobody is using.

Two gate SCENARIOS stopped being meaningful and were rewritten in place.
Neither assertion was lowered.

**`--sessioncheck`'s strike scenario asserted the bug.** It read *"a tenancy
you have never cabled takes no strike, however long you leave it"* and
proved it by running twenty days past a move-in with no copper in the
building. That is precisely the behaviour this record removes. It now
asserts strictly more: the grace period is real, AND it ends, AND the ending
is a complaint on the sixth day. The `help` text it reads was rewritten to
match, because a help text that describes a rule the machine no longer has
is the same failure as a man page that does.

**`--eventcheck` and `--sitecheck`'s bill scenario ran the clock for weeks
with one tenancy cabled and the rest ignored.** Since D27 that ends the run
halfway to the blackout -- correctly. Both now turn the clock a day at a
time and forgive the tenancies the scenario never promised anything to,
exactly as `keep_measuring` has always done in `--loadcheck`, and both say
so where they do it. The tenancy each scenario DID cable keeps every number
it earned. `TENANT_IN` in eventcheck was a constant 19 tied to the old
schedule and is 1 now.

    --loadcheck   35/35        --netcheck    196/196
    --sitecheck  254/254       --eventcheck   47/47
    --health      20/20        --mancheck     36/36
    --building  200/200        --solve 60     60 repaired, 60 handed back
    --askcheck  2850/2850      check-decoys   37/37

`--sitecheck` gained six assertions, four of them a new section that lays
three three-metre patch leads in one room and reads the negotiated speed off
`net_port_speed`: cat 6 between a router and a core switch's uplink is ten
gigabit, the same cat 6 into a server's card is a gigabit, fibre into a
cheap eight-port switch is a gigabit, and the fibre that bought no extra
speed cost four times the copper that bought none either.

## What was deliberately NOT done

**No price was multiplied.** The brief said not to, and the reason it would
not have worked is above: the money was never the problem with the cable
decision. The problem was that cat 6 gave ten gigabit to a desk that has a
gigabit card in it, and that a run cost less than a rounding error on a
month's rent. Both of those are now wrong in the code rather than expensive
in the table.

**The fit-out rate, the ISP price and the two demand numbers were not
touched.** `SITE_DESK_FILE_KB`, `SITE_DESK_WEB_KB` and `SITE_DESK_FILES` are
still the only chosen numbers in the load model, and the difficulty curve is
still arithmetic the netstack does. Adding a fourth chosen number to make
the endgame poorer would have been the difficulty constant this project has
refused since D25.

**No new kit was added**, and this is the one place the design is
compromised. The honest model of a core switch aggregating nine fibre risers
is a box with more than two ten gigabit ports in it, and this catalogue has
no such box: a `switch24` has an SFP+ *pair*, so a tower with nine floor
switches home-run to one core cannot have nine ten gigabit risers. It does
not bite today -- the risers are not the bottleneck at any size this game
reaches -- but it will the day a floor's traffic outgrows a gigabit, and the
answer then is a twelve-port ten gigabit aggregation switch in the
catalogue at a price that makes buying one a decision.

**One wart could not be fixed, because `core/netstack.c` was out of scope
for this change.** `show <box>` prints, on a port whose kit is slower than
its cable, `(fibre carries 10000Mb; the circuit is 1000Mb)`. "The circuit"
was written when the ISP handoff was the only rate-limited port in the
world, and it is now the wrong noun on every port but that one. The sentence
is not untrue -- the numbers are both right -- but the word is wrong and it
should read "the port". It is named here rather than left for somebody to
find.

**The competent build was measured on three seeds, not thirty.** Each
sixty-day playthrough is fifteen minutes of real time, and the schedule
itself was checked across nine seeds because that is cheap. If the day-60
balance matters more than the day-30 one, that is the measurement to widen.
