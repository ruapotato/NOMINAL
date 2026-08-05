# NOMINAL — build.
#
#   make            build build/nominal (headless + socket server)
#   make check      determinism gate + language and scenario tests
#   make gdext      build the GDExtension for Godot
#   make clean
#
# The floating point flags are load-bearing, not decoration: contraction and
# fast-math would let the compiler reassociate sim arithmetic and break the
# byte-identical replay guarantee. See docs/decisions.md D3.

CC      ?= cc
CSTD     = -std=c11
WARN     = -Wall -Wextra -Wno-unused-parameter
FPFLAGS  = -ffp-contract=off -fno-fast-math
OPT     ?= -O2
# -MMD -MP: without header dependencies, editing sim.h rebuilds only sim.o
# and every other object keeps the old struct layout. That fails as a
# segfault, not as a compile error. Do not remove.
CFLAGS  += $(CSTD) $(WARN) $(FPFLAGS) $(OPT) -Icore -MMD -MP
LDFLAGS +=

CORE_SRC = core/util.c core/value.c core/vfs.c core/lex.c core/compile.c \
           core/vm.c core/natives.c core/parts.c core/man.c core/wreck.c core/station.c core/sim.c core/hostfs.c core/shell.c \
           core/net.c
CORE_OBJ = $(CORE_SRC:core/%.c=build/%.o)

BIN = build/nominal

# The default goal is pinned: rules defined above `all:` would otherwise
# become the default, which silently builds the wrong thing (and made the
# determinism gate compare a binary it had not rebuilt).
.DEFAULT_GOAL := all

.PHONY: all check clean gdext test-lang test-scenario bf test-break cpu test-cpu

# --- break-fix (D17) ---------------------------------------------------
# The new core. `make test-break` is the gate: random corruption must always
# produce a ticket, that ticket must always be visible to pkg verify, and
# repairing it must always get the machine booting again.
# The machine, without a main(): this is what both the harness and the
# GDExtension are built from, so the game and the tests run the same code.
BF_SRC_LIB = core/util.c core/value.c core/vfs.c core/ns.c core/cpu.c \
             core/kernel.c core/image.c core/net_sites.c core/customer.c core/boot.c core/breaker.c \
             core/building.c core/netstack.c core/netsite.c core/site.c
BF_SRC = $(BF_SRC_LIB) core/serve.c core/netcheck.c core/sitecheck.c core/bfmain.c
BF_OBJ = $(BF_SRC:core/%.c=build/%.o)

# Regenerate the embedded guest userland. Needs clang+lld for riscv; the
# generated header is committed so nobody else does.
.PHONY: guest
guest:
	@./tools/mkguest.sh

# --- the cpu (D18) -----------------------------------------------------
# Our machine, RV64IM instruction set. `make test-cpu` is the gate: it
# differential-tests against qemu-riscv64 and then checks the three
# determinism claims (same build twice, -O0 vs -O2, Linux vs Windows).
CPU_SRC = core/util.c core/cpu.c core/cpumain.c

cpu: build/cpu
build/cpu: $(CPU_SRC) core/cpu.h core/nom.h | build
	$(CC) $(CFLAGS) -o $@ $(CPU_SRC)

test-cpu: build/cpu
	@./tools/test_cpu.sh 40

bf: build/bf
# guestbin.h must be a prerequisite: it is generated, and without it here
# make keeps a binary with a stale guest userland embedded in it.
#
# One C compiler, one link, no third-party libraries. D20 put a language model
# in here and this rule linked with $(CXX) against a static llama; the
# amendment on docs/decisions-d20.md says what that cost and why it went.
build/bf: $(BF_OBJ) | build
	$(CC) -o $@ $(BF_OBJ)

# THE DRAW HISTOGRAM. `make faults` answers "does a player ever MEET this
# fault", which is a different question from "does this fault work" and the
# one nobody could answer before.
.PHONY: faults
faults: build/faulthist
	@./build/faulthist 400 1 1 | tail -70

build/faulthist: $(BF_SRC_LIB) tools/faulthist.c core/machine.h core/nom.h \
                 core/kernel.h core/guestbin.h | build
	$(CC) $(CFLAGS) -o $@ $(BF_SRC_LIB) tools/faulthist.c

build/bf_asan: $(BF_SRC) core/machine.h core/nom.h core/abi.h core/cpu.h \
               core/kernel.h core/guestbin.h | build
	$(CC) $(CSTD) $(WARN) $(FPFLAGS) -O1 -g -fsanitize=address,undefined \
	  -Icore -o $@ $(BF_SRC)

test-break: build/bf build/bf_asan build/faulthist
	@./build/bf --health 20 | tail -1
	@echo
	@./build/bf --survey 300 | tail -9
	@echo
	@./build/bf --solve 200 | tail -1
	@echo
	@./build/bf --peel 60 3 | tail -2
	@echo
	@# Is every designed fault reachable, and can any ticket be closed with
	@# no repair? This exits non-zero on the second, which is the failure a
	@# playtester described as teaching you to distrust your own diagnosis.
	@./build/faulthist 150 1 1 | tail -5
	@echo
	@echo "--- under asan/ubsan (slower now: every boot runs a real cpu):"
	@for n in 1 3; do \
	  ./build/bf_asan --survey 25 $$n 2>&1 | grep -E 'ERROR|SUMMARY|seeds produced' \
	    | sed "s/^/  $$n faults: /"; \
	done
	@echo
	@# Is the person in front of the machine honest, and does she constrain
	@# you? This replaced --toolcheck and --jsoncheck, which existed only to
	@# police a language model and took minutes of model time to do it.
	@./build/bf --askcheck | tail -2


all: $(BIN)

build:
	@mkdir -p build

# Objects depend on the FLAGS, not just the sources. Changing CFLAGS -- OPT,
# a -D, a sanitiser -- is invisible to make, so a rebuild happily links objects
# compiled under the old flags. That is how a build with the language model
# ended up silently running the code path without it, and the same trap is
# waiting for the next flag anyone adds. The stamp file makes the flags a real
# dependency.
# `force` makes this rule run every time. Without it the stamp is a file with
# no changing prerequisites, so make considers it up to date forever and the
# whole mechanism does nothing -- which is exactly what happened.
.PHONY: force
force:

build/.flags: force | build
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

# HEADERS ARE PREREQUISITES, OR A REBUILD IS NOT A REBUILD.
#
# This listed only the .c file, so editing core/cpu.h changed nothing that
# `make` could see: objects compiled against the old header linked happily
# against ones compiled against the new. Measuring a change to CPU_MEM_BYTES,
# I got a solve ladder at 6/20 and concluded the change had broken the game.
# It had not -- build/ held a mixture of two ABIs. A clean rebuild gave 20/20,
# and so did the change I had just blamed. I nearly reported a fabricated
# regression and nearly abandoned a real 3x memory saving because of it.
build/%.o: core/%.c $(wildcard core/*.h) build/.flags | build
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(CORE_OBJ) build/main.o
	$(CC) $(CORE_OBJ) build/main.o -o $@ $(LDFLAGS)

build/main.o: core/main.c | build
	$(CC) $(CFLAGS) -c core/main.c -o $@

# ---------------------------------------------------------------- GDExtension
# Plain C, no third-party deps: gdext/gdextension_interface.h came out of the
# engine binary in this repo via --dump-gdextension-interface.
GDEXT_OUT = game/bin/libnominal.linux.x86_64.so

gdext: $(GDEXT_OUT)

# ONE COMMAND, ONE COMPILER, NO CONDITIONAL. This used to fork on whether a
# PIC build of llama.cpp existed: with it, thirteen objects and a C++ link
# against a static llama, pulling libstdc++, libgomp and libdl into the game's
# own binary; without it, a plain C build and a customer who could not answer.
# Two shapes of the same library, and which one a player got depended on what
# happened to be in vendor/. The model is gone (docs/decisions-d20.md), so
# there is one shape, it is plain C11, and it links nothing but libc.
$(GDEXT_OUT): $(BF_SRC_LIB) gdext/nominal_gdext.c | build
	@mkdir -p game/bin
	$(CC) $(CFLAGS) -Igdext -fPIC -shared $(BF_SRC_LIB) gdext/nominal_gdext.c -o $@

# ------------------------------------------------------------------ Windows
# Cross-compiled with mingw-w64. KICKOFF requires exporting to Linux AND
# Windows, so the Windows build is a make target that runs on every machine,
# not a promise. The FP flags matter here too: a replay recorded on Linux has
# to reproduce on Windows.
WINCC     ?= x86_64-w64-mingw32-gcc
WINFLAGS   = $(CSTD) $(WARN) $(FPFLAGS) $(OPT) -Icore
WIN_BIN    = build/win/nominal.exe
WIN_GDEXT  = game/bin/libnominal.windows.x86_64.dll

.PHONY: windows
windows: $(WIN_BIN) $(WIN_GDEXT)

$(WIN_BIN): $(CORE_SRC) core/main.c
	@mkdir -p build/win
	$(WINCC) $(WINFLAGS) $(CORE_SRC) core/main.c -o $@ -lws2_32

$(WIN_GDEXT): $(BF_SRC_LIB) gdext/nominal_gdext.c
	@mkdir -p game/bin
	$(WINCC) $(WINFLAGS) -Igdext -shared $(BF_SRC_LIB) gdext/nominal_gdext.c -o $@ -lws2_32

# ---------------------------------------------------------------------- tests
check: $(BIN) test-lang test-scenario
	@tools/check_determinism.sh

test-lang: $(BIN)
	@tools/test_lang.sh

test-scenario: $(BIN)
	@tools/test_scenario.sh

# EVERY object's header dependencies, not just the station's. The break-fix
# objects were missing from this line, so editing machine.h did not rebuild
# them: the binary kept an old struct layout and behaved as though a field
# were set when it was not. That cost real time three separate times before
# anyone noticed the build was lying.
-include $(wildcard build/*.d)

clean:
	rm -rf build game/bin
