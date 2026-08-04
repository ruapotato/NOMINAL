/* cpumain.c — run an rv64 image on our machine, and prove it is deterministic.
 *
 *   build/cpu <file.elf>          run it, print guest output and the trace
 *   build/cpu --det <file.elf>    run it twice and require identical results
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "nom.h"
#include "cpu.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
/* Windows opens stdout in text mode, which rewrites every \n the guest emits
 * into \r\n. That corrupts guest output and breaks byte-exact comparison
 * against the Linux build -- the cross-platform determinism gate caught this,
 * which is precisely what it is for. */
static void stdout_binary(void) { _setmode(_fileno(stdout), _O_BINARY); }
#else
static void stdout_binary(void) { }
#endif

/* The host side of the syscall ABI. Numbers follow the Linux rv64 convention
 * so an off-the-shelf compiler's runtime lines up, but the SET is ours and it
 * is tiny on purpose: this is the machine's entire connection to the world. */
static int64_t host_syscall(Cpu *c, int64_t n, int64_t a0, int64_t a1,
                            int64_t a2, void *ctx)
{
    (void)ctx;
    switch (n) {
    case 64: {                         /* write(fd, buf, len) */
        if (a2 < 0 || a2 > 1 << 20) return -1;
        char *tmp = nom_alloc((size_t)a2 + 1);
        if (!cpu_read(c, (uint64_t)a1, tmp, (size_t)a2)) { nom_free(tmp); return -1; }
        if (c->out) buf_put(c->out, tmp, (size_t)a2);
        nom_free(tmp);
        return a2;
    }
    case 93:                           /* exit(code) */
        c->exit_code = a0;
        c->trap = TRAP_EXIT;
        return 0;
    default:
        return -1;                     /* unknown syscalls fail, never crash */
    }
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *p = nom_alloc((size_t)n);
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { fclose(f); nom_free(p); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return p;
}

static CpuTrap run_once(const uint8_t *img, size_t len, Buf *out,
                        uint64_t *icount, int64_t *code, char *err, size_t errsz)
{
    Cpu c;
    cpu_init(&c);
    c.syscall = host_syscall;
    c.out = out;
    if (!cpu_load_elf(&c, img, len, err, errsz)) { cpu_free(&c); return TRAP_ILLEGAL; }
    CpuTrap t;
    do { t = cpu_run(&c, 1000000); } while (t == TRAP_BUDGET && c.icount < 50000000);
    *icount = c.icount;
    *code   = c.exit_code;
    if (t != TRAP_EXIT) snprintf(err, errsz, "%s at 0x%llx",
                                 cpu_trap_name(t), (unsigned long long)c.trap_addr);
    cpu_free(&c);
    return t;
}

int main(int argc, char **argv)
{
    stdout_binary();
    bool det = argc > 1 && strcmp(argv[1], "--det") == 0;
    const char *path = det ? argv[2] : argv[1];
    if (!path) { fprintf(stderr, "usage: cpu [--det] <file.elf>\n"); return 2; }
    size_t len; uint8_t *img = slurp(path, &len);
    if (!img) { fprintf(stderr, "cannot read %s\n", path); return 2; }

    Buf o1 = {0}; uint64_t i1 = 0; int64_t c1 = 0; char e1[256] = "";
    CpuTrap t1 = run_once(img, len, &o1, &i1, &c1, e1, sizeof e1);

    if (!det) {
        fwrite(o1.p, 1, o1.len, stdout);
        fflush(stdout);
        /* The trace goes to stderr so stdout is byte-for-byte the guest's
         * output, which is what the differential test against qemu compares. */
        fprintf(stderr, "[%s", cpu_trap_name(t1));
        if (t1 == TRAP_EXIT) fprintf(stderr, " %lld", (long long)c1);
        else fprintf(stderr, ": %s", e1);
        fprintf(stderr, "  %llu instructions]\n", (unsigned long long)i1);
        return t1 == TRAP_EXIT ? 0 : 1;
    }

    Buf o2 = {0}; uint64_t i2 = 0; int64_t c2 = 0; char e2[256] = "";
    CpuTrap t2 = run_once(img, len, &o2, &i2, &c2, e2, sizeof e2);
    bool same = (t1 == t2) && (i1 == i2) && (c1 == c2) &&
                (o1.len == o2.len) && (o1.len == 0 || memcmp(o1.p, o2.p, o1.len) == 0);
    printf("run 1: %s, %llu instructions, %zu bytes out\n",
           cpu_trap_name(t1), (unsigned long long)i1, o1.len);
    printf("run 2: %s, %llu instructions, %zu bytes out\n",
           cpu_trap_name(t2), (unsigned long long)i2, o2.len);
    printf("%s\n", same ? "IDENTICAL" : "DIVERGED");
    return same ? 0 : 1;
}
