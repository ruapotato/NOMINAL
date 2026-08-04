# D14. A station, its segments, and the people who page you

Supersedes the framing of D13. The mechanics of D9–D13 survive; the container
and the objective change.

## What it is

**You are the sysadmin of a space station.** Not a pilot. There is no flying —
the delivery/waypoint objective is deleted, because nobody's job is measured in
deliveries and the piloting was never interesting (`heading = bearing`).

The station has **segments**. Segments arrive: a contract is accepted, a module
docks, and it needs power, data, atmosphere and somebody to make its systems
work. They arrive whether or not you are ready, which is where the pressure
comes from. Money buys *hardware*, not growth — otherwise a struggling player
simply stops buying and the game stalls.

Each segment is a **host**: its own file server, its own devices, its own
quirks, often its own vendor's idea of what a sensible field name is.

## The objective is uptime

Score is not throughput. It is:

- what stayed up, against the SLA each segment's tenants were promised
- how many times you were paged
- **how many times you were paged twice for the same cause**

That last one is the game. Being paged once is work. Being paged twice for the
same reason is a failure to do the actual job.

## The pager is an IM client

Alarms are not error codes. **People message you**, and people report symptoms,
never causes:

> **kestrel-lab (seg-4)** 09:14
> hey — it's 12° in here and we're losing samples. any idea?

That is `bay_overheating` with a human attached, and it is better in every way:
it makes symptom-not-cause diegetic rather than a design conceit, it gives the
station a population, and it means the difficulty knob is *how vague the
reporter is*, which is a knob real support work actually has.

Tenants also lie, exaggerate, and misattribute — "the network is down" when
their own machine is unplugged. That is not cruelty, it is the job.

## mount, not ssh

Both work. They are for different things, and the game should teach the
difference:

- **`ssh seg-4`** — a shell on that host. What you do to debug one box.
- **`mount seg-4 /n/seg4`** — that host's file server appears in *your*
  namespace. What you do to administer fifty.

The payoff: after `bind /n/seg4/dev/thm /dev/atmo/seg4`, the watchdog you wrote
in hour one covers segment 4 **without being modified, or even knowing segment
4 exists.** No other game in this space can do that, and it is the entire
reason the file model was worth building.

ssh is the trap that feels productive. Mounting is the thing that scales. A
player who only ever ssh's will drown around segment six, which is the lesson.

## The desktop

MATE-shaped, NomnixOS-shaped: a top panel with a menu and a clock, a bottom
taskbar with a window list, real draggable/stackable windows. Apps:

| app | what it is |
|---|---|
| Terminal | the shell. Same `shell_exec` the socket uses. |
| Messages | the IM client. Tenants page you here. |
| Hardware | the slot inventory and the patch panel — wiring is done by dragging a cable, not by typing |
| Monitor | uptime, SLA, incidents, the graphs |
| Files | the namespace, including everything you have mounted |
| Logs | `/var/log`, greppable |

Per NomnixOS `de_scene_file_arch.md`, windows publish a text display list to
`/dev/wsys/<wid>/scene` and the engine is only a compositor and input router.
That is what lets an in-game program open a window, and it means the desktop is
inspectable over the socket like everything else.

## Rejected

- **Buying segments.** Growth you control is not pressure.
- **ssh-only.** Authentic and a dead end; it cannot scale past a handful of
  hosts and it wastes the namespace.
- **Abstract alarm codes.** The IM framing is strictly better and costs nothing.
- **Keeping the piloting.** It was the least sysadmin-like thing in the build.
