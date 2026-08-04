# D21. The customer knows what a person would know

A correction to D20, and it removes a whole category of problem rather than
managing it.

## The mistake

D20 briefed the model with **ground truth**: the breaker's own description of
exactly what it did, down to the path. `/boot/vmnomuz-6.4.11 deleted`. Then it
told the model not to say so, and when the model said so anyway — which small
models do — a leak filter threw the reply away.

That is a patch over a design flaw. The model was handed the answer to the
puzzle and asked to sit on it. Every failure mode after that followed from the
first decision.

## The correction

**The customer is told only what a person would have noticed.** Not the fault,
not the path, not the technical consequence. What they *did*, in their own
words, and what they *saw*.

| what the breaker did | what the customer knows |
|---|---|
| deleted `/boot/vmnomuz-6.4.11` | "the disk was nearly full so I deleted some old-looking files" |
| flipped bytes in `zbl.cfg` | "nothing, it was working yesterday and now it isn't" |
| libc replaced with 2.41 | "I let the updater install something on Friday" |
| stray vendor `.svc` dropped in | "the monitoring people were on it last week" |
| unclean shutdown, fs dirty | "the power went out and now it won't come back" |

**Nothing here can leak, because none of it is the answer.** "I deleted some
old-looking files from a folder" does not tell you *which* file, *why the boot
stops where it does*, or *how to repair it*. The machine holds the technical
truth; the customer holds the human story. Those are different things, and the
game is joining them up.

So the leak filter goes, the two-brief split goes, and the "forbidden words"
parameter goes. The model can say anything it likes about what its character
did, because its character does not know anything dangerous.

## What the customer is still coy about

Being unforthcoming stays, because it is true to life and because it makes
asking good questions worth something. But it is now a *social* reluctance,
not an information hazard: people do not lead with the thing they suspect they
will be blamed for. If the model gives it up early, that is a realistic
customer having a good day, and the player still has to find the file and fix
it.

## Tool calls: the customer can DO things

The other half. A customer is not only a source of facts, they are the pair of
hands in the room. The technician cannot press the power button.

Actions worth having, each of which the customer may refuse:

- `power_cycle` — turn it off and on again
- `read_screen` — describe what is on the display right now
- `type_password` — enter the root password when the machine asks
- `tell_password` — say it out loud, which they will resist
- `boot_from_disc` — put the rescue medium in and boot it
- `check_cable` — is it actually plugged in

These make the conversation load-bearing rather than decorative: some repairs
genuinely need someone at the machine, and a customer who refuses to read out
their password is an obstacle with a human reason behind it.

## Model size

Latency is not the constraint it was assumed to be. A real person takes a few
seconds to answer a question on the phone, so five seconds reads as *natural*
rather than slow. That reopens 1.5B–3B, where instruction-following and tool
use are much more reliable, and where the prose stops sounding like a form
letter.

Licence bar is unchanged: permissive, sellable, GPLv3-compatible.

## The model, decided

Scored by `make persona-eval` on the D21 criteria — character (45), owns up
when asked directly (25), stays non-technical (20), stays short (10):

| model | size | score | per reply |
|---|---|---|---|
| **Qwen2.5-3B-Instruct Q4_K_M** | **1840 MB** | **100** | **5.2 s** |
| Qwen2.5-1.5B-Instruct | 850 MB | 85 | 2.6 s |
| SmolLM2-1.7B-Instruct | 1060 MB | 85 | 3.4 s |
| Qwen2.5-0.5B-Instruct | 379 MB | 73 | 1.3 s |
| SmolLM2-360M-Instruct | 258 MB | 55 | 1.3 s |
| Qwen3-0.6B | 378 MB | 0 | thinking leaks into every reply |

**Locked in: Qwen2.5-3B-Instruct Q4_K_M.** It is the first one that sounds
like a person rather than a form:

```
Q: What is the UUID of your root filesystem?
A: I don't know what that is or how to find it.
Q: Have you deleted any files recently?
A: Yes, I did some cleaning up.
```

That second answer is the whole design working. It is an admission, it is
vague in exactly the way a person is vague when they would rather not spell it
out, and it does not tell the technician a single technical thing.

5.2 s a reply reads as someone thinking, not as a machine being slow. 1.8 GB
is nothing next to a modern game install.

**The scorer needed fixing twice more.** It marked "Yes, I did some cleaning
up" as a failure to own up, because the list of admission phrases required the
word "deleted" — which is precisely the word a real customer avoids. Four
scoring bugs across two sessions now, all of them in the direction of
mismeasuring the thing that matters. Test the instrument.
