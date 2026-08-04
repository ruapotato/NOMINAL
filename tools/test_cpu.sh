#!/bin/sh
# test_cpu.sh — differential test of our rv64 core against qemu-riscv64.
#
# Our CPU is only worth anything if it agrees with a reference on the cases
# that are easy to get subtly wrong. Each generated program computes an FNV
# hash over thousands of arithmetic, shift, division and memory results; if
# one byte differs, the hash differs and this fails.
set -u
N=${1:-30}
SC=$(mktemp -d)
trap 'rm -rf "$SC"' EXIT
pass=0; fail=0
for i in $(seq 1 "$N"); do
    python3 tools/cputest/gen.py "$i" > "$SC/t.c"
    clang --target=riscv64-unknown-elf -march=rv64im -mabi=lp64 -nostdlib \
          -ffreestanding -fuse-ld=lld -Wl,-Ttext=0x1000 -O1 \
          -o "$SC/t.elf" "$SC/t.c" 2>/dev/null || { echo "cpu: seed $i FAILED to build"; fail=$((fail+1)); continue; }
    ours=$(./build/cpu "$SC/t.elf" 2>/dev/null | head -1)
    ref=$(qemu-riscv64 "$SC/t.elf" 2>/dev/null | head -1)
    if [ "$ours" = "$ref" ] && [ -n "$ref" ]; then
        pass=$((pass+1))
    else
        fail=$((fail+1))
        echo "cpu: seed $i DIVERGED"
        echo "     ours: $ours"
        echo "     qemu: $ref"
    fi
done
echo "cpu: $pass agreed with qemu, $fail diverged"

# --- the determinism claims, which are the reason this CPU exists ----------
python3 tools/cputest/gen.py 12345 > "$SC/d.c"
clang --target=riscv64-unknown-elf -march=rv64im -mabi=lp64 -nostdlib \
      -ffreestanding -fuse-ld=lld -Wl,-Ttext=0x1000 -O1 -o "$SC/d.elf" "$SC/d.c" 2>/dev/null

# same build, twice
if ./build/cpu --det "$SC/d.elf" >/dev/null 2>&1; then
    echo "cpu: PASS  the same image twice gives an identical trace"
else
    echo "cpu: FAIL  the same image twice diverged"; fail=$((fail+1))
fi

# -O0 against -O2: catches arithmetic the optimiser is free to reassociate
cc -std=c11 -O0 -Icore -o "$SC/cpu_o0" core/util.c core/cpu.c core/cpumain.c 2>/dev/null
cc -std=c11 -O2 -Icore -o "$SC/cpu_o2" core/util.c core/cpu.c core/cpumain.c 2>/dev/null
a=$("$SC/cpu_o0" "$SC/d.elf" 2>/dev/null); b=$("$SC/cpu_o2" "$SC/d.elf" 2>/dev/null)
if [ "$a" = "$b" ] && [ -n "$a" ]; then
    echo "cpu: PASS  -O0 and -O2 emulators agree"
else
    echo "cpu: FAIL  -O0 and -O2 emulators disagree"; fail=$((fail+1))
fi

# Linux against Windows: KICKOFF requires both, so "deterministic on Linux"
# is not the claim being made.
WINE64=/usr/lib/wine/wine64
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && [ -x "$WINE64" ]; then
    if x86_64-w64-mingw32-gcc -std=c11 -O2 -Icore -o "$SC/cpu.exe" \
         core/util.c core/cpu.c core/cpumain.c 2>/dev/null; then
        # A persistent prefix: wine bootstraps a fresh one on first use, and
        # that first run produces nothing on stdout, which looks exactly like
        # a divergence and is not one.
        WP=build/.wineprefix
        mkdir -p "$WP"
        WINEDEBUG=-all WINEPREFIX="$(pwd)/$WP" "$WINE64" cmd /c exit >/dev/null 2>&1
        w=$(WINEDEBUG=-all WINEPREFIX="$(pwd)/$WP" "$WINE64" "$SC/cpu.exe" "$SC/d.elf" 2>/dev/null)
        if [ "$w" = "$b" ] && [ -n "$w" ]; then
            echo "cpu: PASS  Linux and Windows emulators agree"
        else
            echo "cpu: FAIL  the Windows emulator disagrees"; fail=$((fail+1))
        fi
    else
        echo "cpu: SKIP  Windows cross-build failed"
    fi
else
    echo "cpu: SKIP  no mingw/wine"
fi

[ "$fail" -eq 0 ]
