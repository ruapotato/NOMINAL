# D18. Our own CPU

The customer's machine will run on a CPU we build, executing real compiled
binaries. This supersedes the interpreted-userland approach of D17, which
stays as the layer above until the CPU can carry it.

## Why not simply run real Hamnix

I measured it rather than guessing. Real Hamnix boots headless under
QEMU/KVM and reaches its interactive shell in **4.08 s**. It also does this:

```
kaslr] offset=0x0000000007200000     <- run 1
kaslr] offset=0x000000000aa00000     <- run 2
```

**Real Hamnix is not deterministic**, and that is KASLR working correctly, not
a defect to fix. Running it directly costs the replay gate, shareable seeds
("see if you can fix 4823"), and cross-platform reproducibility. It also costs
shippability: QEMU cannot be embedded in a Godot export, KVM is Linux-only so
Windows falls back to a much slower TCG, and the content loop — which boots
hundreds of machines to validate them — goes from instant to half an hour.

## The decision

**Our machine, RV64IM instruction set.**

We define the platform: the memory map, the syscall ABI, trap behaviour, and
the determinism guarantees. The *encoding* is a published, stable, deliberately
small standard, and that is an asset rather than a compromise:

- an existing compiler targets it **today**, so the emulator is validated
  against a reference instead of against my own expectations
- Adder gains a third backend with a documented target. Adder already has its
  own IR and its own codegen (`codegen_x86.py`, `codegen_arm64.py`,
  `ir.ad`, `regalloc.ad`, `elf_emit.ad`) — no LLVM — so this is "write a third
  backend beside the two worked examples", not "port a compiler stack".

## What is deliberately absent, and why

| absent | reason |
|---|---|
| floating point | F/D are the largest source of cross-platform divergence. Integer-only makes determinism structural rather than maintained. Soft-float is exact everywhere. |
| cycle counter | `rdtsc`-alikes leak host timing into guest results. The **instruction count** is readable, because it is a deterministic function of the program. |
| randomness | no instruction produces entropy. |
| uninitialised memory | all memory reads as zero until written. |
| undefined behaviour | every encoding either executes or traps. There is no "unpredictable" in this machine. |

The emulator itself is written to the same standard: all arithmetic in
`uint64_t` and reinterpreted, because signed overflow in C is undefined and
undefined means `-O0` and `-O2` may disagree; shift amounts always masked;
division-by-zero and the `INT64_MIN / -1` case given the values the spec pins
down; every load and store bounds-checked before it happens.

## Measured

```
40/40 generated torture programs agree byte-for-byte with qemu-riscv64
PASS  the same image twice gives an identical trace
PASS  -O0 and -O2 emulators agree
PASS  Linux and Windows emulators agree
```

The torture generator targets what emulators get quietly wrong: sign extension
at the 32/64 boundary, shifts by amounts that would be UB in C, the division
edge cases, and loads and stores of every width. Each program folds thousands
of results into one FNV hash, so a single wrong byte fails the test.

`make test-cpu` runs all of it.

### Two bugs the gates caught

- The generator emitted C with undefined behaviour (`INT64_MIN / -1`, shifting
  negatives). Clang correctly compiles UB to a trap instruction, so the test
  was exercising the trap path rather than the arithmetic. Our CPU and QEMU
  had actually agreed.
- **mingw opens stdout in text mode**, rewriting every `\n` the guest emits
  into `\r\n`. That corrupts guest output on Windows and breaks any byte-exact
  comparison. This is exactly what a cross-platform gate is for.

## What this does not do yet

- No MMU. A program loads where its ELF asks and addresses are physical.
  Paging is a later concern and does not change the ISA.
- One hart, no interrupts, no timers.
- Two syscalls (`write`, `exit`). The set is small on purpose: it is the
  machine's entire connection to the world, so the sandbox is structural.
- Nothing of Hamnix runs on it yet. That needs the Adder backend, which is
  work in the Hamnix tree and a separate decision.

## Sequencing

1. the core, validated against a reference  ← this change
2. syscalls backed by the VFS, so a guest program can read the machine's files
3. enough of a runtime that a compiled tool (`ls`, `cat`) works
4. the Adder backend, so real Hamnix userland compiles for this machine
5. the boot chain moves from interpreted scripts (D17) onto compiled binaries
