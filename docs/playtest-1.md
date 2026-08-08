# Playtest 1 — the guided run

    make bf gdext
    ./Godot_v4.7.1-stable_linux.x86_64 --path game -- --seed=4242

Seed 4242 is the one I played all of this on, so I know what is coming.

**The one thing to know: the HUD's `next:` line is the game telling you what to
do, and the ring on the minimap is where to do it.** If you are ever lost,
that line and that ring are the answer. When the job is on another deck the
minimap shows a chevron at its edge with the deck number — walk to a lift or
the stairs and go there.

Everything below is what `next` will say, in order, with what it means. You
should not need this document — that is the thing being tested. **Where you
reach for it, write down where.**

---

## What you are looking at

- **Top left** — where you are, what is in your hands, how many decks are in
  service, the day, and `next:`.
- **Bottom left** — the deck plan. You are the amber dot with a needle. The
  blue ring is the job. Green pips on a room mean a power source there has
  ways out left in it.
- **Top right** — money, what the circuit costs, when the rent is billed.

Keys: **`I`** inventory · **`O`** open a deck you are standing on ·
**`N`** run a day now · **`X`** unplug what you are looking at ·
**`C`** start a run of conduit · **`E`** use / sit down.

---

## The first hour, which is the bridge

The crew were aboard before you were and their six consoles are dark. `next`
walks you through the first one and then repeats itself five times, which is
deliberate: by the sixth you should be doing it without reading.

1. **`order pc helm`** — it arrives in goods in on deck 0, not in your hands.
2. **`deliver helm d6.bridge`** — over the socket this walks it for you. In
   the window: go to goods in, pick it up, take the lift, put it down.
3. **`feed helm`** — power comes down conduit from the core in the plant room.
   Nothing in this station runs until a run reaches it.
4. **`power helm on`** — a real machine boots. Watch the console.
5. **`order switch8 bsw`**, **`deliver bsw d6.comms`**, **`feed bsw`**
6. **`cable bsw:0 helm:0 cat5e`** — you plug one end, walk to the other, and
   the metres come off the drum and out of the budget as you go.

**`crew`** at any point says how many are working and what each is short of.

> Watch for: after cabling you have a drum in your hands, and a box takes both.
> `next` should tell you to `spool back` before it tells you to carry
> anything. If it does not, that is a bug and I want to know.

## Then the tenants

Tenancy 1 has the keys on day 1 on deck 1, and 131 desks are coming over the
next 37 days. **`demand`** shows the whole book before you sign anything.

`next` will walk you through: a switch on their deck, `serve <n> <switch>` to
patch their desks, and then the part that is actually a decision —

- They asked for **a broadcast domain of their own**. `serve 1 sw1 31` puts
  them in a vlan as it patches them. If you do that, they will need a
  subinterface on the router and a trunk to carry it, and `service` will tell
  you so by name. That whole exchange is new this week; it is the thing I most
  want tested.
- Nothing hands out addresses until you have a **router**, its LAN leg
  addressed, a **pool** on it, and — this one is easy to miss —
  **`router edge on`**. Without it every desk holds a lease and nothing
  finishes, and the `service` row will now say exactly that.

**`service`** is the page to live on. One row per tenancy, in that trade's own
units, with the reason underneath when there is one.

## What will come for you

- **The mains fails once between days 20 and 30**, then every 17–29 days. Boxes
  that were running come back with a filesystem to check: `events` names them,
  and the repair is the rescue medium and `fsck`, then `pkg verify` and
  `pkg reinstall` if a config was cut in half. A £220 `ups <box>` buys the box
  out of it. A working bridge crew switches things back on overnight.
- **Conduit trips.** A run carries 1500 W. `conduits` prints every run against
  what it is carrying, on demand, for nothing. Take it over and everything
  behind it goes down at once and comes back dirty — and `events` will name
  the run and the arithmetic. This one is entirely your own doing and it is
  meant to be.
- **Disks wear out.** `events` warns for about a fortnight before anything is
  lost. `disk <box>` is £140.

---

## What I want back

1. **Every place you reached for this document.** That is the list of things
   `next` should have said and did not.
2. **Anything the game told you that turned out not to be true.** Every
   technical claim in here is supposed to be true of this machine; five
   playthroughs this week each found one that was not.
3. **Whether the ring is enough**, or whether you wanted a line on the floor.
4. **Where it stopped being interesting**, and what day that was.

Known and not fixed: the port-reach probe (#80) is intermittent, so a crosshair
on a socket occasionally will not take on the first look — look away and back.
The curved-ring spike (#81) is on a branch and off by default.
