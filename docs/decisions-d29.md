# D29. Voice: the first fault in this game that is not about throughput

## The sentence this record exists for

David, on where the rent is going to come from as the building fills:

> *"as the office grows, we use more bandwidth and provide different types of
> services. webhosting, voice over IP, content creators like streamers.
> Industries that require bandwidth that could pay a rent... sound issues."*

The last two words are the whole record. Everything this simulator could
produce until today was **throughput**-shaped: a port fills, a queue
overflows, a transfer does not finish, a tenancy does not get its day's work
done and does not pay. `load` names the busiest port, the player buys a
faster thing or moves the load, and the number goes down.

A call is the opposite kind of load and it breaks in the opposite way. G.711
at 20ms is 86 kilobits on the wire -- a **thousandth** of a gigabit -- so no
call in this building has ever been short of bandwidth or ever will be. It is
ruined instead by three things a file transfer does not even notice:

- a packet **lost**, because there is no time to ask for it again;
- a packet **late**, because the buffer that smooths the wire out has already
  played the silence where that sound was meant to go;
- a path **long**, because past about 150ms one way people talk over each
  other.

So a tower can be at twenty per cent utilisation, with every transfer
finishing and every tenancy paid, and the calls unusable. That is a new
*kind* of decision -- the fix is not "buy more bandwidth", it is where the
bulk traffic goes -- and it is the reason this was worth building rather than
adding a fourth number to `load`.

## What was built

Real streams. `net_voice_start()` puts UDP datagrams of a fixed size on the
wire at a fixed rate, through `net_udp_send()`, through the same routing, the
same switch, the same egress queue and the same tail-drop as everything else.
There is no quality number computed beside the stack. The stack produces the
badness, the way it already produces drops, and the receiver measures what
turned up.

The measurement is RFC 3550's, because inventing a scale would have been the
one thing this project is not allowed to do:

- **loss** from sequence numbers, the way a receiver really has to: what it
  expected is the highest sequence it saw minus the first it saw. A stream
  that lost its last four packets does not know they existed.
- **jitter** as the smoothed mean deviation of transit time, `J += (|D| -
  J)/16`, exactly the RFC's recursion, kept in microseconds so the division
  has something left to divide.
- **one-way delay**, which is the one thing this world can measure that a
  real phone cannot: the sender's clock and the receiver's clock are the same
  clock, so the timestamp in the payload is honest arithmetic on one clock
  rather than a subtraction of two that disagree. Recorded as such in the
  source, because a man page that claimed a real phone could do this would be
  the usual lie.

## How a lost or late packet becomes bad audio, arithmetically

This was the judgement call worth getting right, because "and then it sounds
bad" is exactly where a designer's rule usually gets smuggled in.

The receiver has a **de-jitter buffer**, `NET_VOICE_JITTER_MS`, 60ms, three
packets, which is an ordinary fixed one. The first packet to arrive starts
the playout clock; from then on packet *k* is due to be played one buffer
depth plus *k* packet-times after that, **because that is what playing audio
at a constant rate means**. A packet that lands after its own deadline has
missed its turn: the silence has already gone out and there is nowhere to put
the sound. It is counted as arrived *and* as concealed, because both are
true.

So the quality number is:

    concealed = lost + late

a count of 20ms audio frames that had no sound to play. No scale, no
weighting, no MOS. It falls straight out of two subtractions -- one on
sequence numbers and one on the playout clock -- and it is the number a
listener would actually hear.

The thresholds that turn that count into *words* are the industry's and are
named as such in the source: 1% concealed is where a listener starts hearing
it, 5% is where the call is being given up on, 150ms one way is ITU G.114,
30ms is half a de-jitter buffer. **They decide the words. They decide nothing
about the packets.**

## Naming the cause, rather than scoring the call

The brief asked for an answer that names the cause -- the queue on a port,
the loss, or the distance -- and not a score. The problem with that is that a
dropped frame is *gone*: nothing upstream of the drop can ever say which port
did it, so a receiver measuring 8% loss has no way to name the riser.

So the frame carries the stream id (`InFlight.vs`) and `port_tx()` records,
on the stream, the port that threw its packets away and the deepest queue its
packets ever sat in. It changes no behaviour -- a tagged frame is switched,
queued and dropped exactly as an untagged one is -- and it is the only place
in this stack where L1 knows a name from above. It is worth the exception
because the alternative is guessing from the busiest port in the building,
which is a different port whenever two things are busy at once.

`net_voice_verdict()` reads that and nothing else. Four real transcripts,
from the gate, unedited:

    verdict: clear -- 0.0% of the audio missing, and 3.0ms each way

    verdict: unusable -- 6 packets thrown away on core port 0, whose egress
    buffer is full. Nothing can fetch them back in time; move the bulk
    traffic off that port or give it its own path.

    verdict: unusable -- 47 packets arrived too late to play: the queue on
    core port 0 held them for 99.8ms, past the 60ms the receiver buffers.

    verdict: poor -- nothing is missing and the call is still bad: 187.9ms
    each way, past the 150ms at which people talk over each other. 195.8ms of
    it is spent sitting in the queue on core port 0, which is full of
    somebody else's files -- the circuit is not too small for the call, it is
    too small for what is sharing it.

That last one is the one this whole record is for. **Not a packet lost, not a
packet late, two milliseconds of jitter, every transfer on the wire still
finishing -- and the call is unusable.** There is no measure anywhere else in
this project that can see it.

## The numbers: idle, loaded, and idle again

The gate builds the smallest building in which this is true. A core switch
with the file server and the phone system on gigabit legs; a floor switch
upstairs with the desks and one handset; and one riser of the cheap drum
between them, 35m of cat5, which negotiates a hundred megabits. The riser is
not undersized for the call. It is undersized for twenty desks pulling files
at once, and the call is on the wrong side of it.

The same call, unchanged, measured three times:

                          sent  arrived  lost  late  jitter  one way  verdict
    idle riser              50       50     0     0   0.0ms    3.0ms  clear
    twenty desks pulling    75       69     6     0   0.6ms    6.1ms  unusable
    they stop again         50       50     0     0   0.0ms    3.0ms  clear

Riser at the middle row: **99% busy, 436 frames dropped, all twenty transfers
still running**. 8.0% of the audio concealed. The control in the same run is
a second call from the same phone system to a handset on the *core* switch,
so its audio never crosses the riser: through the same busy building, on the
same stack, at the same instant, it loses **nothing**. That is the proof that
what ruined the first call was the shared port and not the hour of the day.

And on a circuit bought by the megabit, where the same 48 KB buffer is 65ms
deep at six megabits and 196ms at two, the other two illnesses appear:

    two megabits, four desks   0 lost, 0 late, jitter 2.9ms, 187.9ms one way
    four megabits, eight desks 12 lost, 47 LATE, jitter 5.9ms, 40.4% concealed

Note which one is worse to listen to. The second loses twelve packets and
conceals fifty-nine, because forty-seven perfectly good packets arrived after
their turn.

## A bug this found in the existing stack, and fixed

`NET_DUE_RING` was 64, with a comment explaining that a frame's delay is
bounded and small: propagation, plus "at most four [milliseconds] of egress
queue, because a port with more than its buffer will hold drops instead of
queueing". That is true of a gigabit -- 48 KB is 393us -- and true enough of a
hundred megabits at 3.9ms. It is **not** true of a circuit sold by the
megabit: the same buffer is 65ms at six and 196ms at two, and a frame that
wanted to wait 196ms was silently landing at 63ms instead.

Which meant a port could print a 201ms peak queue while nothing on it had
ever taken longer than 63ms to arrive. Two numbers out of the same buffer,
disagreeing, in the file whose whole claim is that congestion is one fact
arriving through the layers that really carry it.

Nobody could see it, because the only thing in this world that reads a
one-way delay is a voice stream and there were none. The ring is 512 now,
which covers the whole of a 48 KB buffer down to 0.8 megabits, below any
circuit this world sells, and costs 4 KB. Every other gate is unchanged by
it, which is itself the evidence that nothing was relying on the clamp.

## The gates, and what each new claim costs to falsify

`--netcheck` went from 207 assertions to 240. The 33 new ones are two
sections: `voice: a call is not a transfer` (21) and `voice: a narrow
circuit, where late is as bad as lost` (12).

Every claim was checked by breaking the thing that makes it true, in a clean
`git archive HEAD` checkout with only the three files this agent owns copied
over it, and counting what fell:

    the change removed                          netcheck
    -------------------------------------------------------
    nothing (the work as committed)             240/240
    NET_DUE_RING back to 64                     231/240   (9 fail)
    the per-frame stream tag (`InFlight.vs`)    235/240   (5 fail)

The nine that fall with the ring are every claim about a queue deeper than
63ms: the standing-queue call reads 63.4ms one way instead of 187.9 and
starts losing packets it should not lose, and not one packet is ever late,
because the arithmetic that made it late was being rounded away. The five
that fall with the tag are every sentence with a port's name in it -- the
numbers stay right and the stack stops being able to say whose they are.

The rest of the gate set, on the same isolated checkout where the only
changes are this work:

    --netcheck    240/240      (207 at HEAD)
    --eventcheck   83/83
    --sitecheck   388/388
    --loadcheck    35/35       unchanged, and its assertions untouched
    --health       20/20
    --mancheck     56/56
    --building    200/200
    --solve 60     60 repaired, 60 handed back
    --askcheck   2850/2850
    check-decoys   37/37
    bf_asan --netcheck  no ERROR, no SUMMARY

## The judgement calls

**Prioritisation does not exist yet, and that is deliberate.** Real switches
have queueing disciplines and this stack has none: every port is one FIFO.
The obvious next feature is a `qos` verb that puts voice in a strict-priority
queue, and it was not built, for the reason D23's jack addendum gives about a
jack that is only a dearer cable: *a fix that is one line of config, correct
in every topology, is learned once and applied forever, and stops being a
decision the first time the player learns it.* The problem is the interesting
half. Its fixes today are **topology** -- put the phones on their own path,
put the file server on the floor it serves, do not run a floor's bulk traffic
and its calls down the same 100Mb riser -- and those are decisions that come
back for you, because which rooms hold what changes as the building grows.
A queueing verb is worth building **after** a player has had to solve this
with copper, not instead. Recorded as unbuilt rather than deleted, the way
D23 recorded the jack.

**Sockets, not a side channel.** A stream holds a real UDP socket at each
end, so it shows in `ss`, a filter rule against port 16384 really bites it,
and a call to a machine with nothing listening behaves like a call to a
machine with nothing listening. Two sockets a call, 128 calls maximum, which
is 256 of the world's 2000.

**Audio is consumed at the instant it lands**, in `udp_input`, rather than
left in the one-datagram socket slot for somebody to poll. Two packets in one
millisecond would otherwise overwrite each other -- loss the network did not
cause, in the one measurement whose entire point is that the network caused
it. A real RTP receiver timestamps on arrival for the same reason.

**The destination node is named, not looked up from the address.** A call to
an address the routing cannot reach is then a stream at 100% loss whose
verdict says *"not one packet arrived... they were never routed to a port"* --
a fault a player can find -- rather than a function that returns -1.

**Calls die with the machine.** `net_close_all` hangs up on every stream at
that node, so rebooting a phone hangs up on it, which is true of every phone.

## What it costs

    per stream        ~130 bytes of world, plus two of the 2000 sockets
    128 streams       16.6 KB
    InFlight          +4 bytes a frame (2 for the sub-millisecond arrival
                      instant, 2 for the stream tag) x 4096 = 16 KB
    NET_DUE_RING      64 -> 512, which is 4 KB
    Sock              +4 bytes x 2000 = 8 KB
    total             about 45 KB on a 73 MB world, and no allocation

The sub-millisecond arrival instant is the one addition that is not
bookkeeping. The due ring is a tick wide and a tick is far too coarse to
measure jitter with -- everything that queued for less than a millisecond
would read as perfectly even -- so `InFlight.land_us` carries the remainder
the tick rounding was already throwing away. The arithmetic that produces it
is the arithmetic that was already there.

## The API the tenant-industries layer gets

    int  net_voice_start(Net*, int from, int to, uint32_t dst, uint16_t dport,
                         int payload, int ptime_ms);
    int  net_voice_call (Net*, int from, int to, uint32_t dst);  /* G.711 */
    void net_voice_stop (Net*, int stream);
    void net_voice_stop_node(Net*, int node);
    bool net_voice_active(const Net*, int stream);
    int  net_voice_count (const Net*);
    bool net_voice_stats (const Net*, int stream, VoiceStats *out);
    void net_voice_reset (Net*, int stream);   /* keep talking, forget the numbers */
    void net_voice_verdict(const Net*, int stream, Buf *out);
    void net_dump_voice  (const Net*, int node, Buf *out);

Start it, step the world as usual, read it. `VoiceStats` is in `netstack.h`
with a comment on every field. A tenancy that sells telephony starts a stream
per concurrent call in the morning, reads `conceal_ppm` in the evening and
decides whether it got the service it is paying rent for; the numbers to
judge it by are the ones above -- under 1% is a day nobody complained about.

## What was NOT done

- **No queueing discipline**, as argued above. Every port is one FIFO.
- **No guest-side tool.** `ss`, `netstat -P` and `tcpdump` read real state on
  a box with a shell, and a `voice` or `rtpstat` command in that shape is the
  obvious next thing; it lives in `guest/`, which was another agent's this
  hour. `net_dump_voice()` is written and formatted for it.
- **No codec but one shape.** `net_voice_start` takes any payload and any
  packet time, so G.729 at 20ms or G.711 at 10ms are both expressible, but
  nothing names them and nothing models a codec's own tolerance for loss.
- **Nothing generates voice demand yet.** That is the layer above's, and this
  record exists partly so it knows what it is calling.
- **The 150ms verdict is only reachable through a queue**, not through
  distance: the longest cable this world sells is 2km of fibre and the whole
  building is a few milliseconds across. The branch that blames the path
  itself is written and correct and has never fired in a gate. Said here
  rather than left for somebody to discover.
