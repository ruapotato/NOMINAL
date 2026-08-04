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

.PHONY: all check clean gdext test-lang test-scenario

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
