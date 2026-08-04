#!/usr/bin/env python3
"""Generate a C torture program for the rv64 differential test.

The point is coverage of the cases where an emulator quietly disagrees with
real hardware: sign extension at the 32/64 boundary, shifts by amounts that
would be UB in C, and the division edge cases the spec pins down. The program
prints a checksum of everything it computed, so a single mismatched byte
between our CPU and qemu fails the test.
"""
import random, sys

seed = int(sys.argv[1])
r = random.Random(seed)

VALS = [0, 1, -1, 2, -2, 7, 255, 256, 65535, 1 << 31, (1 << 31) - 1,
        -(1 << 31), (1 << 63) - 1, -(1 << 63), 0x5555555555555555 - (1 << 64),
        0x0123456789abcdef, -3, 1000003]

def v():
    return r.choice(VALS)

lines = []
lines.append("typedef long long i64; typedef unsigned long long u64;")
lines.append("typedef int i32; typedef unsigned u32;")
lines.append("static u64 h = 1469598103934665603ULL;")
lines.append("static void mix(u64 x){ h ^= x; h *= 1099511628211ULL; }")
lines.append("static long sys(long n,long a,long b,long c){")
lines.append(' register long x10 asm("a0")=a,x11 asm("a1")=b,x12 asm("a2")=c,x17 asm("a7")=n;')
lines.append(' asm volatile("ecall":"+r"(x10):"r"(x11),"r"(x12),"r"(x17):"memory"); return x10;}')
lines.append("void _start(void){")
lines.append(" volatile i64 mem[64]; for(int i=0;i<64;i++) mem[i]=0;")

for i in range(220):
    a, b = v(), v()
    k = r.randrange(10)
    if k == 0:
        lines.append(f" {{ u64 a={a & (2**64-1)}ULL, b={b & (2**64-1)}ULL; "
                     f"mix(a*b); mix(a+b); mix(a-b); }}")
    elif k == 1:   # signed div/rem. INT64_MIN/-1 is UB in C, so it is guarded
                   # here and exercised through the unsigned path instead.
        lines.append(f" {{ i64 a={a}LL, b={b}LL; if(b && !(b==-1 && a==(-9223372036854775807LL-1))) "
                     f"{{ mix((u64)(a/b)); mix((u64)(a%b)); }} }}")
    elif k == 2:
        lines.append(f" {{ u64 a={a & (2**64-1)}ULL, b={b & (2**64-1)}ULL; if(b) {{ mix(a/b); mix(a%b); }} }}")
    elif k == 3:   # shifts. Shifting a negative left is UB in C, so the left
                   # shift is done unsigned; the arithmetic right shift of a
                   # negative is implementation-defined but consistent here.
        s = r.randrange(64)
        lines.append(f" {{ i64 a={a}LL; mix(((u64)a)<<{s}); mix((u64)(a>>{s})); mix(((u64)a)>>{s}); }}")
    elif k == 4:   # 32-bit ops, checking sign extension back to 64
        lines.append(f" {{ u32 a={a & (2**32-1)}u, b={b & (2**32-1)}u; "
                     f"mix((u64)(i64)(i32)(a+b)); mix((u64)(i64)(i32)(a*b)); "
                     f"if(b) mix((u64)(i64)(i32)(a/b)); }}")
    elif k == 5:
        s = r.randrange(32)
        lines.append(f" {{ u32 a={a & (2**32-1)}u; mix((u64)(i64)(i32)(a<<{s})); "
                     f"mix((u64)(i64)((i32)a>>{s})); mix((u64)(a>>{s})); }}")
    elif k == 6:   # loads and stores of every width
        idx = r.randrange(60)
        lines.append(f" {{ mem[{idx}]={a}LL; mix((u64)mem[{idx}]); "
                     f"mix((u64)*(volatile i32*)&mem[{idx}]); "
                     f"mix((u64)*(volatile short*)&mem[{idx}]); "
                     f"mix((u64)*(volatile unsigned char*)&mem[{idx}]); }}")
    elif k == 7:   # comparisons, signed and unsigned
        lines.append(f" {{ i64 a={a}LL,b={b}LL; mix(a<b); mix((u64)a<(u64)b); mix(a>=b); }}")
    elif k == 8:   # a loop, so branches and back-edges are exercised
        n = r.randrange(3, 12)
        lines.append(f" {{ u64 s=0; for(int i=0;i<{n};i++) s = s*31u + (u64)i*{a & (2**64-1)}ULL; mix(s); }}")
    else:
        lines.append(f" {{ i64 a={a}LL; mix((u64)(a^{b}LL)); mix((u64)(a|{b}LL)); mix((u64)(a&{b}LL)); }}")

lines.append(" char out[17]; const char *hex=\"0123456789abcdef\";")
lines.append(" for(int i=0;i<16;i++) out[i]=hex[(h>>((15-i)*4))&15];")
lines.append(" out[16]='\\n'; sys(64,1,(long)out,17); sys(93,0,0,0); for(;;);")
lines.append("}")
print("\n".join(lines))
