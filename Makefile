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
BF_SRC = core/util.c core/value.c core/vfs.c core/lex.c core/compile.c \
         core/vm.c core/natives.c core/image.c core/boot.c core/bootrt.c \
         core/breaker.c core/bfstub.c core/bfmain.c

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
build/bf: $(BF_SRC) core/machine.h core/nom.h | build
	$(CC) $(CFLAGS) -o $@ $(BF_SRC)

build/bf_asan: $(BF_SRC) core/machine.h core/nom.h | build
	$(CC) $(CSTD) $(WARN) $(FPFLAGS) -O1 -g -fsanitize=address,undefined \
	  -Icore -o $@ $(BF_SRC)

test-break: build/bf build/bf_asan
	@./build/bf --survey 400 | tail -9
	@echo
	@./build/bf --solve 300 | tail -1
	@echo
	@./build/bf --peel 150 3 | tail -2
	@echo
	@echo "--- under asan/ubsan, 1 to 5 simultaneous faults:"
	@for n in 1 2 3 5; do \
	  ./build/bf_asan --survey 120 $$n 2>&1 | grep -E 'ERROR|SUMMARY|seeds produced' \
	    | sed "s/^/  $$n faults: /"; \
	done


all: $(BIN)

build:
	@mkdir -p build

build/%.o: core/%.c | build
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

$(GDEXT_OUT): $(CORE_SRC) gdext/nominal_gdext.c | build
	@mkdir -p game/bin
	$(CC) $(CFLAGS) -Igdext -fPIC -shared $(CORE_SRC) gdext/nominal_gdext.c -o $@

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

$(WIN_GDEXT): $(CORE_SRC) gdext/nominal_gdext.c
	@mkdir -p game/bin
	$(WINCC) $(WINFLAGS) -Igdext -shared $(CORE_SRC) gdext/nominal_gdext.c -o $@ -lws2_32

# ---------------------------------------------------------------------- tests
check: $(BIN) test-lang test-scenario
	@tools/check_determinism.sh

test-lang: $(BIN)
	@tools/test_lang.sh

test-scenario: $(BIN)
	@tools/test_scenario.sh

-include $(CORE_OBJ:.o=.d) build/main.d

clean:
	rm -rf build game/bin
