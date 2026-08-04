# D20. A language model in the box

The customer is played by a language model that ships **inside the game**.
No daemon, no API key, no network. Steam build, Linux and Windows.

## Why the customer needs a model at all

The machine tells you *what* is wrong. Only the customer knows what *changed*,
and a scripted persona can only ever answer the questions its author thought
of. The scripted version (customer.c) is good — eleven topics, five causes,
mood, a denial before the admission — and a player will exhaust it in an hour.
What it cannot do is answer a question nobody anticipated, which is most of
them, because the interesting questions are the ones a specific machine in a
specific state invites.

## Determinism is no longer a constraint

It was a KICKOFF requirement inherited from the previous design and it has
been overtaken. The CPU's determinism tests stay — they are cheap and they
catch real emulator bugs, which is what they were always actually good for —
but determinism no longer vetoes design decisions. Replayable matches were a
feature of a game we are not making.

The machine stays deterministic anyway, because nothing about the model
touches it. The model reads a brief and produces dialogue; it never sees the
disk and cannot change it.

## The runtime: llama.cpp, embedded

- **MIT licensed**, so a commercial Steam release is fine.
- Plain C API (`llama.h`) over a C++ core, designed to be embedded — this is
  the path everyone shipping local inference actually takes.
- Cross-compiles to Windows with the mingw toolchain already used for the
  GDExtension, and to Linux natively.
- CPU-only inference is the target. Steam machines vary too much to depend on
  a GPU, and the model is small enough not to need one.

It links into the existing GDExtension, so the game stays one `.so` and one
`.dll` plus data.

## The model

Requirements, in order:

1. **A permissive licence.** Apache-2.0 or MIT. Anything with a bespoke
   community licence is a legal question we do not need.
2. **Small.** The model is dialogue for one character with a tight brief, not
   a general assistant. Hundreds of megabytes, not gigabytes.
3. **Instruction-tuned.** A base model will not stay in character.
4. **Fast on a CPU.** A reply should feel like a person typing, not a pause.

### Measured, cpu-only, same prompt and same two questions

| model | size | verdict |
|---|---|---|
| SmolLM2-360M-Instruct Q4_K_M | 258 MB | **fails** — echoes the instruction block back at the player, and answered "No." to the one question the brief says to admit |
| **Qwen2.5-0.5B-Instruct Q4_K_M** | 379 MB | **works**, 1.3 s per reply. Keeps the secret on a general question, gives it up on the specific one |
| Qwen3-0.6B Q4_K_M | 378 MB | **fails as shipped** — volunteers the secret unprompted, and emits its `<think>` reasoning into the dialogue |

The winner is **Qwen2.5-0.5B-Instruct**, and the transcript is the reason:

```
"Has anything changed recently?"        -> "No, nothing has changed."
"Did you delete any files to free space?"
                    -> "I only deleted old files from the boot folder."
```

That is exactly the mechanic: it keeps the secret when it should and gives it
up when asked the right question.

Qwen3-0.6B is newer and scores better on benchmarks, and is worse here — it
has a thinking mode that leaks into the reply and it is less obedient about
withholding. Worth retrying with thinking explicitly disabled before it is
ruled out. This is a good reminder that benchmark rank is not the metric; the
metric is whether it can keep one secret for five minutes.

## The prompt is where the quality comes from

A 360M model will not improvise a good support call. It will follow a tight
brief. So:

- **Ground truth in the system prompt.** The breaker already reports exactly
  what it did; that string is the brief. The model is told what really
  happened and told not to volunteer it.
- **A persona with rules, not adjectives.** "You deleted files from /boot to
  free space. You do not mention this unless asked about deleting, tidying or
  disk space. If asked directly twice, admit it." Rules a small model can
  follow beat characterisation it cannot.
- **Short replies, enforced.** A token limit and an instruction; a small model
  rambles if allowed to.
- **The scripted persona stays as the fallback** and as the floor: if the
  model is missing, slow, or produces nothing usable, `customer.c` answers.
  The player never sees a hang.

## What must not happen

- The model must never be able to change the machine. It receives a brief and
  a question and returns text. It has no tools, no filesystem, no syscalls.
- The model must never be the only route to a fix. Everything is diagnosable
  from the machine alone; the customer shortens the search, and a player who
  ignores them entirely can still finish. Otherwise a bad generation becomes
  an unwinnable ticket.
- Loading must not block the game. The model loads on a worker; until it is
  ready, the scripted persona answers.

## Build shape

```
vendor/llama.cpp        pinned, built static
build/libnominal.so     GDExtension + llama, Linux
build/nominal.dll       GDExtension + llama, Windows
game/models/*.gguf      the weights, shipped as game data
```

The headless bench (`bf --serve`) gets the same backend, so a remote player
talks to the same customer as someone at the desktop.

## Sequencing

1. build llama.cpp for Linux, prove a small GGUF loads and answers  ← here
2. the C seam: `llm_ask(brief, history, question) -> text`, with the scripted
   persona as fallback
3. prompt engineering against real tickets until the persona holds up
4. mingw build, and a Windows check under wine
5. ship the weights as game data and wire the Godot side
