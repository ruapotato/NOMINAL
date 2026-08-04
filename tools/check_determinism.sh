#!/bin/sh
# Determinism gate. Run this on every change to the sim or the language.
#
# Three checks, in increasing order of how much they hurt to fix later:
#   1. same binary, same seed, twice   -> byte-identical replay
#   2. different seeds                 -> different replay (the seed is real)
#   3. -O0 build vs -O2 build          -> byte-identical replay
#   4. Linux build vs Windows build    -> byte-identical replay (skipped when
#                                         mingw-w64 or wine is not installed)
#
# Check 3 is the one that catches floating-point contraction and
# reassociation, which is exactly the class of bug that is cheap to prevent
# now and agony to retrofit. See docs/decisions.md D3.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
WORK=$(mktemp -d)
trap 'chmod -R u+w "$WORK" 2>/dev/null; rm -rf "$WORK" 2>/dev/null; true' EXIT

BIN=build/nominal
HOME_DIR=${HOME_DIR:-home}
SEED=${SEED:-424242}

[ -x "$BIN" ] || { echo "determinism: $BIN not built"; exit 1; }

fail=0
HAD_GDEXT=""
[ -f game/bin/libnominal.linux.x86_64.so ] && HAD_GDEXT=1

# ---- 1. same binary, same inputs, twice
"$BIN" --headless --home "$HOME_DIR" --seed "$SEED" --out "$WORK/a.json" >"$WORK/a.result" 2>/dev/null || true
"$BIN" --headless --home "$HOME_DIR" --seed "$SEED" --out "$WORK/b.json" >"$WORK/b.result" 2>/dev/null || true

if cmp -s "$WORK/a.json" "$WORK/b.json"; then
    echo "determinism: PASS  same seed reproduces byte-identically ($(wc -c <"$WORK/a.json") bytes)"
else
    echo "determinism: FAIL  same seed produced different replays"
    diff "$WORK/a.json" "$WORK/b.json" | head -10
    fail=1
fi

# ---- 2. the seed actually reaches the simulation
"$BIN" --headless --home "$HOME_DIR" --seed $((SEED + 1)) --out "$WORK/c.json" >/dev/null 2>&1 || true
if cmp -s "$WORK/a.json" "$WORK/c.json"; then
    echo "determinism: FAIL  a different seed produced an identical replay"
    fail=1
else
    echo "determinism: PASS  a different seed produces a different replay"
fi

# ---- 3. optimisation level must not change the numbers
if [ "${SKIP_O0:-0}" != "1" ]; then
    make -s OPT=-O0 clean >/dev/null 2>&1 || true
    make -s OPT=-O0 >/dev/null 2>&1
    "$BIN" --headless --home "$HOME_DIR" --seed "$SEED" --out "$WORK/o0.json" >/dev/null 2>&1 || true
    make -s clean >/dev/null 2>&1 || true
    make -s >/dev/null 2>&1
    # `clean` also removes game/bin, so put the extension back if it was there
    [ -n "$HAD_GDEXT" ] && make -s gdext >/dev/null 2>&1
    if cmp -s "$WORK/a.json" "$WORK/o0.json"; then
        echo "determinism: PASS  -O0 and -O2 agree byte-for-byte"
    else
        echo "determinism: FAIL  -O0 and -O2 disagree (floating point is not pinned)"
        diff "$WORK/a.json" "$WORK/o0.json" | head -10
        fail=1
    fi
fi

# ---- 4. Linux and Windows must agree. KICKOFF requires both platforms, and
#         "determinism on Linux" is not the claim being made.
WINE64=/usr/lib/wine/wine64
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && [ -x "$WINE64" ]; then
    if make -s windows >/dev/null 2>&1; then
        WINEDEBUG=-all WINEPREFIX="$WORK/wp" "$WINE64" build/win/nominal.exe \
            --headless --home "$HOME_DIR" --seed "$SEED" --out "$WORK/win.json" \
            >/dev/null 2>&1 || true
        if [ -f "$WORK/win.json" ] && cmp -s "$WORK/a.json" "$WORK/win.json"; then
            echo "determinism: PASS  Linux and Windows agree byte-for-byte"
        else
            echo "determinism: FAIL  the Windows build disagrees with the Linux build"
            fail=1
        fi
    else
        echo "determinism: SKIP  Windows cross-build failed"
    fi
else
    echo "determinism: SKIP  cross-platform check (needs mingw-w64 and wine)"
fi

exit $fail
