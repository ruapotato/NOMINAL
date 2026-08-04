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

/* Same, but with a parent process, so the child inherits its namespace and
 * working directory. */
int64_t kernel_spawn_p(Machine *m, const char *path, const char *arg,
                       Buf *console, int depth, Proc *parent,
                       char *err, size_t errsz);

/* Run one shell command line as the persistent session, so cd and bind stick.
 * This is the ONLY entry point the terminal, the socket and the desktop use,
 * so none of them can diverge from the others. */
int64_t kernel_run(Machine *m, const char *line, Buf *console);
ProcInfo *kernel_session(Machine *m);

/* The TCP bench: the entire game, with no GUI in the process at all. */
int bench_serve(int port, bool verbose, uint64_t seed0);

#endif /* NOM_KERNEL_H */
