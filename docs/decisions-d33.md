# D33. The desk can now say what it heard

## The sentence this record exists for

A playtester walked round to a call centre agent's chair on a day the
tenancy scored **0 of 18 calls with 29% of its audio concealed**, sat down at
their computer, and found a machine in perfect health:

> `ping 198.51.100.1` → 3/3, 8 ms, 0% loss. `traceroute` → clean two hops.
> `ip addr` → 20,175 packets, 0 dropped. **I found nothing.**

And their diagnosis of the diagnosis:

> *"Voice is the most interesting tenancy in the game and its fault is the
> only one you cannot see from the chair... the flagship new verb and the
> flagship new trade don't meet. The fix isn't a new fault; it's the desk
> being able to say 'yesterday I sent 20,175 RTP packets and 5,850 of them
> didn't get played.'"*

Two features landed on the same day and did not meet. D29 built voice: a call
is real UDP through real ports, ruined by loss, lateness and delay rather
than by bandwidth. D31 built `sit <desk>`: the player can use a tenant's own
machine to find out why that tenant is unhappy. Every instrument on that
machine was blind to the only thing wrong with it.

## First: was the answer already there? `netstat -P`, reproduced

The playtester flagged what they had not tried, and it had to be checked
before anything was built, because a feature that duplicates a command
nobody was pointed at is worse than a signpost.

It is not the answer, and the reason it is not is the whole point. This is
the dead end reproduced on the current build — `./build/bf --towersh 26`, a
tower built the naive way (one core switch, a switch per floor, one file
server in the basement, a 100 Mb circuit), on the day the voice tenancy
scores nothing:

    floor tenant  trade    desks  up  addr   done   worst  strikes
        3      3  voice       16  16    16   0/16  2443ms      11*
          16 of 16 calls broke up: 11.8% of the audio concealed,
          5 ms one way, 15 us of jitter.

    f3 office> sit t3d0
    Wren Farah pushes their chair back and lets you sit down at t3d0.
    "we filed with the landlord. 0 of us got anything done yesterday, out of 16."

    desk:t3d0# ping 198.51.100.1
      seq=0 reply from 198.51.100.1 time=14 ms
      seq=1 reply from 198.51.100.1 time=8 ms
      seq=2 reply from 198.51.100.1 time=8 ms
    --- 3 sent, 3 received, 0% packet loss
    desk:t3d0# traceroute 198.51.100.1
    1 10.0.0.1
    2 198.51.100.1
    desk:t3d0# ip addr
    1: eth0: <UP,LOWER_UP> mtu 1500
        inet 10.0.1.41/16 scope global
        RX packets 21689  TX packets 21898  dropped 0
    desk:t3d0# netstat -P
    port 0  up   1000Mb full 49m tx 21898 rx 21764 drop 0
    desk:t3d0# ss -u
    (this machine holds no sockets at all)

**`netstat -P` answered none of it, and it was right not to.** It reads THIS
card's counters and this card dropped nothing: the audio was thrown away on
the ISP handoff's egress port, three hops away, on frames this machine never
saw go missing. Every line above is true. That is the trap — the tools were
not lying, they were answering a different question, and there was no
instrument on the machine that asked the right one.

`ss -u` shows the second half of the trap and is the harder one: the calls
are *over*. The busy period ended, `siteday.c` hung every stream up, and a
live reading of a stream that no longer exists is a blank screen. Anything
built here had to survive that or it would be useless on the only occasion
anybody would run it.

## What was built

`/bin/voice`, in the shape of `ss`, `netstat -P`, `ip` and `tcpdump`: a real
program on the disk that reads real kernel state through `g_netinfo`. Two
new NETINFO ops, `NETINFO_VOICE` and `NETINFO_VOICENOW`, dispatched in
`netsite_info` beside the other seven.

From the same chair, on the same day, on the same tower:

    desk:t3d0# voice
    1 call out, 1 in, over 6400 ms of wire time (111056 to 117456; the
    clock is at 117491 now)
      dir  calls    sent arrived   lost   late concealed
      out      1     320     319      1      0        1  0.3%
      in       1     320     300     20      0       20  6.2%
      in  is audio this machine RECEIVED and timed itself.
      out is audio it SENT, as the far end reported hearing it.
      concealed is audio frames with no sound to play: lost, or so late
      the 60 ms de-jitter buffer had already played the silence.

    the worst of them, one it received:
    call uplink -> t3d0 (10.0.1.41:16385), 172 bytes every 20ms
      320 sent, 300 arrived, 20 lost, 0 too late to play
      loss 6.2%   jitter 0.0ms   one way 5.0ms (best 4.0ms, worst 8.0ms)
      20 of 320 audio frames had no sound to play (6.2%)
      20 of this call's packets were thrown away on uplink port 0
      the deepest queue they waited in was on uplink port 0, 4.0ms
    verdict: unusable -- 20 packets thrown away on uplink port 0, whose
    egress buffer is full. Nothing can fetch them back in time; move the
    bulk traffic off that port or give it its own path.

    desk:t3d0# voice -l
    no calls

Nobody wrote that diagnosis. `uplink:0` is the ISP handoff's egress into the
building, and it is full because a web host's inbound visitors and three
floors of desks pulling pages share a 100 Mb circuit with a call centre. The
repair is topology and money — a bigger circuit, or the bulk traffic off it
— and the desk named the port off the stream's own frames.

Note also what the desk says the player would otherwise have got wrong: the
damage is **inbound**, on audio arriving from the carrier. The direction the
desk *sends* is almost clean (0.3%). A player who bought a fatter uplink for
the outbound side would have been buying the wrong half.

## Where yesterday's numbers live, and why there

**On the node, in the `Host`, beside the interface counters.** Not in the
`Machine` that gets booted when somebody pulls the chair out, and not in
`core/siteday.c` where the day is.

This was the decision the whole feature turns on, so it is worth writing the
reasoning down rather than the result.

- **Not in the Machine.** D31's rule is that a sat-at desk is destroyed when
  you stand up: 18 MB per desk, freed, and *"nothing you leave on it stays."*
  A counter that lived there would be zero every time it was read, because
  the machine was booted after the calls ended.
- **Not in siteday.** That file owns the day; it is also another agent's, and
  more importantly a number computed there would be **the landlord's view
  piped into the tenant's terminal**. The brief was explicit and it is the
  same rule the whole project runs on: the desk must show what THAT MACHINE
  saw. `service` and `voice` now print the same concealment because they read
  the same counters, not because one was handed the other's answer.
- **On the node**, then — which is exactly where `ip addr`'s 21,689 packets
  already live, and they survive standing up for the same reason.

### What a "run" is, and why it is not a day

`core/netstack.c` has never heard of days and was not going to start. So:

> **A run is a contiguous set of calls.** The record is cleared when a call
> starts at a machine that is not already on one, and each call is folded in
> as it ends.

A tenancy that dials every morning and hangs up every evening therefore
leaves *exactly yesterday's calls* in it, and nothing above the stack had to
tell the stack when yesterday was. The wire-clock milliseconds on the first
line say when the run ran, which is the same clock `tcpdump` stamps frames
with, so the two can be read together.

The alternative — a lifetime total — was rejected on the playtester's own
sentence. "Yesterday I sent 20,175 packets" is a diagnosis. "Since this box
was cabled, across nineteen days of which four were bad, I sent 383,000" is
a number you cannot act on, and it gets *better looking* every day the fault
persists.

## The one shortcut, stated rather than hidden

`in` is what this machine received and timed for itself: honest arithmetic on
packets that really landed on its card.

`out` is what the **far end** heard of what this machine sent, and no
endpoint can know that alone. A real one is told — RTCP receiver reports
(RFC 3550) carry loss and jitter, and the VoIP metrics block (RFC 3611)
carries concealment, which is this number exactly. Nothing puts RTCP frames
on this wire; the report is handed over when the call ends.

**That is a shortcut in how the number travels, not in the number.** Every
packet it counts really crossed a port and was really dropped or really held
past its playout instant. It is said in the header, in the source, in the man
page and here, because the alternative — printing an outbound loss figure and
letting a player assume the endpoint worked it out — is the kind of quiet
invention this project exists not to do.

## The judgement call the brief asked to be recorded

**Should the desk be able to say the call was FINE?**

Yes, and it does, unasked, in the same words:

    verdict: clear -- 0.0% of the audio missing, and 3.0ms each way

The argument against is that a diagnostic which prints on a healthy machine
is noise. The argument for is stronger and it is about what the player learns
from silence: *a tool that only speaks when something is wrong teaches you to
ignore it when it is quiet.* If `voice` printed nothing on a good desk, a
player at a bad desk could not tell "no calls were made" from "the calls were
fine" from "the tool has nothing to say about this", and all three are
different next moves.

And the positive answer is itself a diagnosis, which is the part that decided
it. *"The calls off this desk were clear, so whatever the tenant is unhappy
about, it is not the network under this chair"* sends the player somewhere
else in the building with a fact rather than a hunch. `--netcheck` asserts
both halves: that a clear day says `verdict: clear`, and that it still prints
the counters underneath, so "nothing missing" is evidence and not an absence
of evidence.

## What it costs

    VoiceLeg          40 bytes   x2, the two directions
    VoiceStats       100 bytes   the worst call of the run, kept whole so the
                                 verdict can still name the port that did it
    VoiceLog         208 bytes
    x NET_NODES_MAX   81 KB      on a 73 MB world, and no allocation

One call is kept whole rather than all of them, and the tie-break matters: the
worst by concealment, and where concealment ties, the longer one. That second
clause is deliberate — a call with nothing missing and 187 ms of one-way delay
is the failure no throughput measure in this project can see, and it must not
be the one that gets thrown away.

## The gates

`--netcheck` went from 240 to **262**. The 22 new assertions are one section,
`voice: what the desk can still read after the calls are over`, and they hang
the calls up first, because that is the state the chair is pulled out in.

Every claim was falsified by breaking the thing that makes it true, in a
clean `git archive HEAD` checkout with only this work's files copied over it:

    the change removed                                netcheck   mancheck
    ------------------------------------------------------------------------
    nothing (the work as committed)                    262/262      57/57
    the fold in voice_teardown -- the record dies
      with the stream, as it did yesterday             250/262      -
    voice_log_begin -- no run boundary, so the
      counters accumulate forever                      261/262      -
    /bin/voice removed from the image                  259/262      56/57

The twelve that fall with the fold are every claim that the evidence outlives
the busy period, which is the feature. The one that falls with the boundary
is `dialling again opens a new run and clears the last one` — the numbers stay
correct and stop being *yesterday's*. The three that fall with the binary are
the ones that boot a real machine, pin it to the node with `netsite_pin` and
type `voice` at it; `--mancheck` catches the same removal from the other side,
because a manual page naming a program the disk does not have is exactly what
that gate is for.

The rest of the set, on the working tree:

    --netcheck     262/262     (240 before)
    --sitecheck    449/449
    --loadcheck     35/35
    --eventcheck    83/83
    --health        20/20
    --mancheck      57/57      (56 before: voice(8) is the new page)
    --building     200/200
    --solve 60      60 repaired, 60 handed back
    --askcheck    2844/2844
    check-decoys    37/37
    make test-cpu   40 agreed with qemu, 0 diverged
    bf_asan --netcheck   262/262, no ERROR, no SUMMARY

## What was NOT done

- **`voice` is not on `--mancheck`'s SAFE list, so its examples are checked
  for existence and not RUN.** `core/mancheck.c` was another agent's this
  hour. It is read-only, it takes no argument that can change anything, and
  it belongs on that list beside `ss` and `tcpdump`; adding the one word
  `"voice"` to `SAFE[]` is the whole change. Recorded here rather than done.
- **The seat's own `help` and `service`'s voice line still do not point at
  it.** `guest/sh.c`'s `help` does now, on its own line, because a player
  reading the tools list is the one route that was in reach. But the two
  places a player is most likely to be looking when they need it — the text
  `sit` prints, and the tenancy row that says *"16 of 16 calls broke up"* —
  live in `core/session.c` and `core/site.c`, which are another agent's.
  **This is the single highest-value line left undone**: the feature exists
  and the game still does not say the word `voice` at the moment the player
  needs it. One sentence in each.
- **No flag clears the record and no flag places a call.** The phone system
  dials; there is nothing at the desk to dial with, and a `--reset` would put
  the only copy of the evidence one keystroke from being destroyed.
- **One call is kept whole, not a history of them.** A desk that had one good
  call and one terrible one prints the terrible one and the summed counters.
  Keeping a ring of them is a straightforward extension and 100 bytes each;
  nothing yet needs it, because a tenancy's calls all cross the same copper
  and fail together.
- **`out` and `in` share one `worst` slot.** If the outbound direction is bad
  for one reason and the inbound for another, only the worse of the two is
  named in words — though both are in the table, and the table is what showed
  that this tower's damage is inbound.
- **Nothing has been played from the desks' side for a long run.** D31 said
  the same and it is still true: `--loadcheck` builds towers and never sits
  down in one.
