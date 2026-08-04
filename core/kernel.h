/* kernel.h — running guest programs against a machine's filesystem. */
#ifndef NOM_KERNEL_H
#define NOM_KERNEL_H

typedef struct Proc Proc;

/* Load and run the program at `path` to completion, on its own CPU, with the
 * machine's disk as its filesystem and `console` as its stdout. Returns the
 * program's exit code, or one of the negative SPAWN_* codes from abi.h.
 * `err` receives a human-readable reason when the program could not be run or
 * faulted -- that string is boot console output, so it must read like a
 * machine talking, not like a debugger. */
int64_t kernel_spawn(Machine *m, const char *path, const char *arg,
                     Buf *console, int depth, char *err, size_t errsz);

#endif /* NOM_KERNEL_H */
