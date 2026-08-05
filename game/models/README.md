# Models — there are none, and that is the finished state

This directory used to hold `Qwen2.5-3B-Instruct-Q4_K_M.gguf` (1.84 GB) and
five losing candidates, 4.8 GB in all. **They are gone, along with the language
model machinery that loaded them.** Nothing in the game reads this directory;
it survives only to carry this note.

## Why

The customer was played by a 3B model for two design revisions (D20, D21).
Four blind playtests measured it, and it lost on every axis that was actually
checkable:

- **60–120 seconds a reply, once nine minutes.** The most-cited fun-killer in
  every report. The bench number that justified the 3B — 5.2 s a reply — was
  taken on an idle machine. In the game the model answers while an emulated
  RV64 CPU boots a kernel on the same box, and the delay scaled with context,
  so it was worst on exactly the long tickets this character exists for.
- **1.84 GB shipped**, plus 488 MB of vendored llama.cpp, against a game whose
  entire source is a few megabytes of C.
- **Two test gates existed only to police it** — `--toolcheck` and
  `--jsoncheck` — because it kept inventing commands that do not exist, after
  two rounds of prompt work aimed at that exact failure.

## What replaced it

`core/customer.c`: a deterministic character with a menu of things you can say
to her, generated from the state of the machine. She answers instantly, she
can only see the bottom of the screen, she misreads characters, she will only
type so much off a phone call, and she never says a word she was not read.

What playtesters quoted back was never a generated sentence. It was *"There is
more above that but it has scrolled off. Do you want me to do it again?"* —
a rule, not prose. Rules are always true, cost microseconds, and cannot invent
a command. `bf --askcheck` proves all of them in milliseconds; the two gates it
replaced took minutes of model time to prove less.

The full reasoning, with the numbers, is in the amendments to
`docs/decisions-d20.md` and `docs/decisions-d21.md`, and at the top of
`core/customer.c`.
