# Models

**Ships with the game: `Qwen2.5-3B-Instruct-Q4_K_M.gguf`** (1.8 GB, Apache-2.0).

Locked in after `make persona-eval` scored every candidate on the only four
things the customer has to do — stay in character, own up when asked directly,
stay non-technical, stay short. It was the only one to score 100, and the only
one that sounds like a person rather than a form:

    Q: What is the UUID of your root filesystem?
    A: I don't know what that is or how to find it.

5.2 s a reply on CPU. That reads as someone thinking, not as a machine being
slow, which is the right feeling for a phone call.

The other files here are the losing candidates, kept so the comparison can be
re-run when something new comes out. They are **not** shipped. `*.gguf` is
gitignored: weights are game data, distributed with the build.

| model | score | per reply | verdict |
|---|---|---|---|
| Qwen2.5-3B-Instruct | 100 | 5.2 s | **ships** |
| Qwen2.5-1.5B-Instruct | 85 | 2.6 s | fallback if size matters |
| SmolLM2-1.7B-Instruct | 85 | 3.4 s | |
| Qwen2.5-0.5B-Instruct | 73 | 1.3 s | |
| SmolLM2-360M-Instruct | 55 | 1.3 s | plays the technician, not the customer |
| Qwen3-0.6B | 0 | — | emits its thinking into the dialogue |

Licence bar: permissive and sellable. Apache-2.0 is GPLv3-compatible but
**not** GPLv2-compatible — settle the game's own licence before shipping.

To re-run the comparison: `make persona-eval` (all models) or
`make persona-eval MODEL=game/models/x.gguf` (one).
